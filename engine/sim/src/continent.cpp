#include "illyria/sim/continent.h"

#include <algorithm>

#include "illyria/sim/hash.h"
#include "illyria/sim/noise.h"

namespace illyria::sim {

namespace {

constexpr Fixed m(int64_t v) { return Fixed::fromInt(v); }
constexpr Fixed r(int64_t num, int64_t den) { return Fixed::fromRatio(num, den); }

// A point given in per-mille of the continent's width and height.
Vec3 N(int64_t u, int64_t v) {
    return {Fixed::fromRatio(u * Continent::kWidth, 1000), Fixed::fromRatio(v * Continent::kHeight, 1000), Fixed::zero()};
}

Fixed segDistSq(Fixed px, Fixed py, const Vec3& a, const Vec3& b, Fixed& t) {
    const Fixed dx = b.x - a.x, dy = b.y - a.y;
    const Fixed len2 = dx * dx + dy * dy;
    t = Fixed::zero();
    if (len2 > Fixed::zero()) t = clamp(((px - a.x) * dx + (py - a.y) * dy) / len2, Fixed::zero(), Fixed::one());
    const Fixed cx = a.x + dx * t - px, cy = a.y + dy * t - py;
    return cx * cx + cy * cy;
}

// Nearest distance from (px, py) to a polyline, ignoring segments farther than `limit`.
// Also reports the segment index and parameter of the nearest point.
Fixed polylineDist(const std::vector<Vec3>& pts, Fixed px, Fixed py, Fixed limit, size_t& seg, Fixed& segT) {
    Fixed best = limit * limit;
    bool found = false;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec3& a = pts[i];
        const Vec3& b = pts[i + 1];
        if (px < min(a.x, b.x) - limit || px > max(a.x, b.x) + limit || py < min(a.y, b.y) - limit ||
            py > max(a.y, b.y) + limit) {
            continue;
        }
        Fixed t;
        const Fixed d = segDistSq(px, py, a, b, t);
        if (d < best) {
            best = d;
            seg = i;
            segT = t;
            found = true;
        }
    }
    return found ? sqrt(best) : limit;
}

Fixed bump(Fixed t) {
    const Fixed a = abs(t);
    if (a >= Fixed::one()) return Fixed::zero();
    const Fixed s = Fixed::one() - a * a;
    return s * s;
}

// Normalised elliptical distance: < 1 inside.
Fixed ellipse(const Continent::Area& a, Fixed x, Fixed y) {
    const Fixed dx = (x - a.centre.x) / a.rx, dy = (y - a.centre.y) / a.ry;
    return sqrt(dx * dx + dy * dy);
}

std::vector<Vec3> densify(const std::vector<Vec3>& pts, Fixed step) {
    std::vector<Vec3> out;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec3 d = pts[i + 1] - pts[i];
        const int64_t n = std::max<int64_t>(1, (d.flat().length() / step).floorToInt());
        for (int64_t k = 0; k < n; ++k) out.push_back(pts[i] + d * Fixed::fromRatio(k, n));
    }
    out.push_back(pts.back());
    return out;
}

constexpr Fixed kValleyReach = Fixed::fromInt(450);
constexpr Fixed kValleySlope = Fixed::fromRatio(6, 100);
constexpr Fixed kSurfaceBelowLand = Fixed::fromRatio(3, 2);

}  // namespace

Continent::Continent(uint64_t seed) : seed_(seed) {
    // The sea along the west coast and around the north-west, and an isle.
    sea_.push_back({N(-100, 0), N(-60, 1000), m(2100), false});
    sea_.push_back({N(0, 1080), N(400, 1100), m(2000), false});
    sea_.push_back({N(60, 880), N(75, 905), m(380), true});

    // Rivers, source to mouth. Branches start where their parent ends.
    rivers_.push_back({"River Aldwater",
                       {N(585, 1000), N(578, 920), N(590, 840), N(575, 760), N(582, 680), N(570, 600), N(576, 520), N(562, 450)},
                       m(16), r(7, 2)});
    rivers_.push_back({"Westrun",
                       {N(562, 450), N(500, 445), N(440, 438), N(370, 430), N(300, 440), N(230, 452), N(160, 462), N(80, 470), N(0, 476)},
                       m(13), m(3)});
    rivers_.push_back({"Sorrow",
                       {N(562, 450), N(566, 380), N(572, 300), N(560, 220), N(566, 140), N(555, 60), N(560, -20)},
                       m(13), m(3)});
    rivers_.push_back({"Emberflow", {N(790, 425), N(780, 390), N(730, 372), N(660, 380), N(600, 386), N(566, 380)}, m(9), r(5, 2)});
    rivers_.push_back({"Greymere", {N(330, 700), N(270, 690), N(210, 705), N(150, 715), N(90, 712), N(0, 710)}, m(9), r(5, 2)});
    rivers_.push_back({"Wending", {N(300, 290), N(230, 282), N(170, 268), N(110, 262), N(0, 255)}, m(8), m(2)});
    rivers_.push_back({"Coldbrook", {N(800, 900), N(720, 880), N(650, 855), N(590, 840)}, m(7), m(2)});

    // Mountain chains. The gaps between them, and the passes, are the chokepoints.
    ridges_.push_back({"Thornspine Mountains", {{N(200, 930), N(260, 905), N(330, 885), N(400, 905), N(470, 880), N(530, 900)}, m(700)}, m(430)});
    ridges_.push_back({"Graycrown Mountains", {{N(180, 610), N(240, 595), N(300, 615)}, m(650)}, m(380)});
    ridges_.push_back({"Graycrown Mountains", {{N(375, 620), N(430, 600), N(505, 615)}, m(650)}, m(400)});
    ridges_.push_back({"Ashen Hills", {{N(250, 360), N(310, 345), N(360, 365)}, m(450)}, m(170)});
    ridges_.push_back({"Ironveil Range",
                       {{N(950, 1020), N(915, 880), N(935, 760), N(900, 640), N(940, 520), N(905, 400), N(935, 270), N(900, 140), N(930, -20)}, m(900)},
                       m(460)});
    ridges_.push_back({"Frostfang Spur", {{N(910, 720), N(840, 705), N(770, 715)}, m(450)}, m(240)});
    ridges_.push_back({"Cinder Spur", {{N(905, 300), N(830, 280), N(760, 290)}, m(450)}, m(260)});
    ridges_.push_back({"Dragonback Mountains", {{N(60, 90), N(140, 60), N(220, 85), N(290, 55), N(340, 90)}, m(650)}, m(390)});
    ridges_.push_back({"Northmoor Hills", {{N(700, 940), N(790, 960)}, m(600)}, m(140)});
    ridges_.push_back({"Longwood Ridge", {{N(420, 360), N(425, 260), N(415, 160), N(420, 100)}, m(220)}, m(60)});
    ridges_.push_back({"Southdown Hills", {{N(690, 90), N(790, 70)}, m(400)}, m(120)});
    // Passes: notches through the ranges (z = radius).
    passes_.push_back({N(905, 640).x, N(905, 640).y, m(550)});
    passes_.push_back({N(330, 885).x, N(330, 885).y, m(450)});
    passes_.push_back({N(220, 85).x, N(220, 85).y, m(420)});

    swamps_.push_back({"Mirefen", N(700, 640), m(650), m(800)});
    swamps_.push_back({"Saltmarsh", N(120, 225), m(400), m(320)});
    forests_.push_back({"Darkwood", N(170, 800), m(900), m(1200)});
    forests_.push_back({"Elderwood", N(400, 520), m(700), m(600)});
    forests_.push_back({"Thornwood", N(760, 560), m(500), m(700)});
    forests_.push_back({"Longwood", N(420, 230), m(380), m(2400)});
    forests_.push_back({"Eastwood", N(700, 200), m(600), m(500)});
    forests_.push_back({"Northwood", N(700, 820), m(500), m(400)});
    lakes_.push_back({"Mirrormere", N(200, 275), m(300), m(220)});
    lakes_.push_back({"Silverlake", N(790, 425), m(340), m(250)});

    crossings_.push_back({Crossing::Kind::Ford, "Oakhaven Ford", N(582, 680)});
    crossings_.push_back({Crossing::Kind::Ford, "Westrun Ford", N(300, 440)});
    crossings_.push_back({Crossing::Kind::Ford, "Sorrow Ford", N(560, 220)});
    crossings_.push_back({Crossing::Kind::Ford, "Emberflow Ford", N(730, 372)});
    crossings_.push_back({Crossing::Kind::Bridge, "Kingsbridge", N(576, 520)});
    crossings_.push_back({Crossing::Kind::Bridge, "Old Bridge", N(160, 462)});
    crossings_.push_back({Crossing::Kind::Bridge, "Southbridge", N(566, 140)});
    crossings_.push_back({Crossing::Kind::Bridge, "Greymere Bridge", N(210, 705)});

    // Roads join the realms through the crossings and passes.
    const Fixed roadHalf = m(3);
    roads_.push_back({{N(80, 560), N(200, 555), N(330, 540), N(450, 530), N(576, 520), N(680, 515), N(790, 540), N(860, 600), N(905, 640), N(1000, 650)}, roadHalf});
    roads_.push_back({{N(300, 760), N(420, 720), N(582, 680), N(700, 690), N(760, 690)}, roadHalf});
    roads_.push_back({{N(330, 1000), N(330, 885), N(330, 800), N(250, 760), N(210, 705), N(230, 620), N(300, 560), N(330, 540), N(300, 440),
                       N(320, 330), N(230, 300), N(160, 180), N(220, 85), N(230, 0)}, roadHalf});
    roads_.push_back({{N(680, 515), N(640, 420), N(730, 372), N(700, 250), N(566, 140), N(450, 120), N(330, 90)}, roadHalf});
    roads_.push_back({{N(450, 240), N(560, 220), N(680, 230)}, roadHalf});
    roads_.push_back({{N(300, 440), N(230, 455), N(160, 462), N(100, 470)}, roadHalf});

    labels_ = {{"WESTMARCH", N(250, 790), 0}, {"VALDOR", N(260, 200), 0}, {"EASTREACH", N(790, 530), 0},
               {"Sunderwave Sea", N(40, 600), 2}, {"Isle of the Seer", N(70, 935), 2}, {"River Aldwater", N(600, 780), 2},
               {"Thornspine Mts", N(380, 930), 1}, {"Graycrown Mts", N(330, 645), 1}, {"Ironveil Range", N(940, 830), 1},
               {"Dragonback Mts", N(200, 30), 1}, {"Ashen Hills", N(300, 330), 1}, {"Frostfang Spur", N(840, 740), 1},
               {"Cinder Spur", N(830, 255), 1}, {"Darkwood", N(170, 840), 3}, {"Longwood", N(450, 300), 3},
               {"Mirefen", N(700, 640), 3}, {"Oakhaven Ford", N(582, 680), 4}, {"Kingsbridge", N(576, 520), 4}};

    computeRiverSurfaces();
    for (const Area& lake : lakes_) {
        Fixed mountain;
        lakeSurfaces_.push_back(baseHeight(lake.centre.x, lake.centre.y, mountain) - m(3));
    }
    placeCities();
    placeSites();
}

Fixed Continent::landDistance(Fixed x, Fixed y) const {
    Fixed sea = m(100000), island = -m(100000);
    for (const SeaShape& s : sea_) {
        Fixed t;
        const Fixed d = sqrt(segDistSq(x, y, s.a, s.b, t)) - s.radius;
        if (s.land) island = max(island, -d);
        else sea = min(sea, d);
    }
    const uint64_t s1 = noise::nextSeed(seed_ ^ 0xC0A57), s2 = noise::nextSeed(s1);
    const Fixed wobble = (noise::fbm(s1, x, y, 2400) - r(1, 2)) * 900 + (noise::fbm(s2, x, y, 500) - r(1, 2)) * 200;
    return max(sea + wobble, island + wobble / 2);
}

Fixed Continent::baseHeight(Fixed x, Fixed y, Fixed& mountain) const {
    const uint64_t sBase = noise::nextSeed(seed_ ^ 0xBA5E), sSmall = noise::nextSeed(sBase), sRidge = noise::nextSeed(sSmall);
    Fixed z = m(10) + noise::fbm(sBase, x, y, 1800) * 55 + (noise::fbm(sSmall, x, y, 450) - r(1, 2)) * 14;
    mountain = Fixed::zero();
    for (const Ridge& rg : ridges_) {
        size_t seg = 0;
        Fixed t;
        const Fixed reach = rg.line.halfWidth * r(8, 5);
        const Fixed d = polylineDist(rg.line.points, x, y, reach, seg, t);
        if (d >= reach) continue;
        // A spine of crests and peaks: sharp ridged noise, strongest along the chain's axis.
        const Fixed shape = Fixed::one() - noise::smoothstep(Fixed::zero(), reach, d);
        const Fixed crest = noise::ridged(sRidge, x, y, 650);
        const Fixed peaks = noise::value(sRidge ^ 0x9EAC, x, y, 260);
        const Fixed rough = r(3, 20) + crest * 13 / 20 + peaks * crest / 5;
        mountain = max(mountain, rg.peak * shape * rough * (shape + Fixed::one()) / 2);
    }
    for (const Vec3& p : passes_) {
        const Fixed dx = x - p.x, dy = y - p.y;
        mountain = mountain * (Fixed::one() - bump(sqrt(dx * dx + dy * dy) / p.z) * 4 / 5);
    }
    return z + mountain;
}

void Continent::computeRiverSurfaces() {
    // Water surfaces fall monotonically downstream, a little below the land;
    // branches start at their parent's surface; rivers reaching the sea end at 0.
    for (River& rv : rivers_) {
        rv.points = densify(rv.points, m(100));
        Fixed start;
        bool joined = false;
        for (const River& parent : rivers_) {
            if (&parent == &rv || parent.points.empty() || parent.points.back().z == Fixed::zero()) continue;
            if ((parent.points.back() - rv.points.front()).flat().lengthSq() < m(1)) {
                start = parent.points.back().z;
                joined = true;
            }
        }
        Fixed surface = m(100000);
        for (size_t i = 0; i < rv.points.size(); ++i) {
            Fixed mountain;
            const Fixed land = landDistance(rv.points[i].x, rv.points[i].y);
            Fixed ground = baseHeight(rv.points[i].x, rv.points[i].y, mountain);
            if (land < Fixed::zero()) ground = Fixed::zero();
            surface = min(surface, ground - kSurfaceBelowLand);
            if (i == 0 && joined) surface = start;
            surface = max(surface, Fixed::fromRatio(1, 10));
            rv.points[i].z = surface;
        }
    }
    // Tributaries end on another river: lower that river's surface below the junction, never raise it.
    for (River& rv : rivers_) {
        const Vec3 mouth = rv.points.back();
        for (River& other : rivers_) {
            if (&other == &rv) continue;
            for (Vec3& p : other.points) {
                if ((p - mouth).flat().lengthSq() < m(2500)) rv.points.back().z = max(rv.points.back().z, p.z);
            }
        }
        for (size_t i = rv.points.size() - 1; i-- > 0;) rv.points[i].z = max(rv.points[i].z, rv.points[i + 1].z);
    }
}

Continent::Sample Continent::sample(Fixed x, Fixed y) const {
    Sample s = landform(x, y);
    carve(s, x, y);
    return s;
}

Continent::Sample Continent::landform(Fixed x, Fixed y) const {
    Sample s;
    s.land = landDistance(x, y);
    Fixed z = baseHeight(x, y, s.mountain);
    if (s.land < Fixed::zero()) {
        z = max(-m(40), -m(2) + s.land / 60);
    } else {
        // Gentle beaches near the shore; inland the land may rise freely.
        z = min(z, r(1, 2) + s.land * 4 / 100 + max(Fixed::zero(), s.land - m(400)) * 2 / 5);
    }
    s.height = z;
    return s;
}

void Continent::carve(Sample& s, Fixed x, Fixed y) const {
    Fixed z = s.height;

    // Rivers: a broad valley, then the channel itself.
    s.riverDist = kValleyReach + m(1000);
    for (const River& rv : rivers_) {
        size_t seg = 0;
        Fixed t;
        const Fixed d = polylineDist(rv.points, x, y, kValleyReach + rv.halfWidth, seg, t);
        if (d < s.riverDist) {
            s.riverDist = d;
            s.riverHalfWidth = rv.halfWidth;
            s.riverDepth = rv.depth;
            s.riverSurface = rv.points[seg].z + (rv.points[seg + 1].z - rv.points[seg].z) * t;
        }
    }
    if (s.riverDist < kValleyReach + s.riverHalfWidth) {
        for (const Crossing& c : crossings_) {
            if (c.kind != Crossing::Kind::Ford) continue;
            const Fixed dx = x - c.pos.x, dy = y - c.pos.y;
            if (dx * dx + dy * dy < m(45) * m(45)) s.atFord = true;
        }
        const Fixed hw = s.riverHalfWidth + (s.atFord ? m(6) : Fixed::zero());
        const Fixed depth = s.atFord ? r(3, 5) : s.riverDepth;
        if (s.riverDist < hw) {
            const Fixed t = s.riverDist / hw;
            z = min(z, s.riverSurface - depth * (Fixed::one() - t * t));
        } else {
            z = min(z, s.riverSurface + r(2, 5) + (s.riverDist - hw) * kValleySlope);
        }
    }

    // Lakes sit in basins with a flat surface.
    for (size_t i = 0; i < lakes_.size(); ++i) {
        const Area& lake = lakes_[i];
        const Fixed q = ellipse(lake, x, y);
        if (q > m(3)) continue;
        const Fixed surface = lakeSurfaces_[i];
        if (q < Fixed::one()) {
            s.inLake = true;
            s.lakeSurface = surface;
            z = min(z, surface - Fixed::one() - (Fixed::one() - q) * 3);
        } else {
            z = min(z, surface + r(2, 5) + (q - Fixed::one()) * lake.rx / 20);
        }
    }

    // Cities stand on levelled pads that blend into the land around them.
    for (const City& c : cities_) {
        const Fixed dx = abs(x - c.pos.x), dy = abs(y - c.pos.y), reach = c.radius * 2;
        if (dx >= reach || dy >= reach) continue;
        const Fixed d = sqrt(dx * dx + dy * dy);
        if (d >= reach) continue;
        const Fixed w = Fixed::one() - noise::smoothstep(c.radius, reach, d);
        z = z + (c.pos.z - z) * w;
    }

    size_t seg = 0;
    Fixed t;
    s.roadDist = m(60);
    for (const Polyline& road : roads_) s.roadDist = min(s.roadDist, polylineDist(road.points, x, y, m(60), seg, t));
    s.height = z;
}

bool Continent::waterSurfaceOf(const Sample& s, Fixed& surface) const {
    if (s.land < Fixed::zero() && s.height < Fixed::zero()) {
        surface = Fixed::zero();
        return true;
    }
    if (s.inLake && s.height < s.lakeSurface) {
        surface = s.lakeSurface;
        return true;
    }
    if (s.riverDist < s.riverHalfWidth + m(6) && s.height < s.riverSurface) {
        surface = s.riverSurface;
        return true;
    }
    return false;
}

bool Continent::onBridge(Fixed x, Fixed y, Fixed roadDist) const {
    if (roadDist > m(4)) return false;
    for (const Crossing& c : crossings_) {
        if (c.kind != Crossing::Kind::Bridge) continue;
        const Fixed dx = x - c.pos.x, dy = y - c.pos.y;
        if (dx * dx + dy * dy < m(40) * m(40)) return true;
    }
    return false;
}

Ground Continent::landGround(Fixed x, Fixed y, Fixed height, Fixed mountain, Fixed roadDist) const {
    if (roadDist < m(3)) return Ground::Road;
    for (const City& c : cities_) {
        const Fixed dx = abs(x - c.pos.x), dy = abs(y - c.pos.y);
        if (dx < c.radius && dy < c.radius && dx * dx + dy * dy < c.radius * c.radius) return Ground::Plains;
    }
    if ((mountain > m(70) && height > m(70)) || height > m(180)) return Ground::Rock;
    for (const Area& a : swamps_) {
        const uint64_t sw = noise::nextSeed(seed_ ^ 0x5A4);
        if (ellipse(a, x, y) + (noise::fbm(sw, x, y, 300) - r(1, 2)) / 2 < Fixed::one()) return Ground::Swamp;
    }
    const uint64_t sf = noise::nextSeed(seed_ ^ 0xF0E57);
    const Fixed n = noise::fbm(sf, x, y, 600);
    for (const Area& a : forests_) {
        if (ellipse(a, x, y) + (n - r(1, 2)) < Fixed::one()) return Ground::Forest;
    }
    // Scattered woods elsewhere, thinning with height.
    if (n > r(67, 100) && height < m(150)) return Ground::Forest;
    return Ground::Plains;
}

bool Continent::waterSurface(Fixed x, Fixed y, Fixed& surface) const { return waterSurfaceOf(sample(x, y), surface); }

Fixed Continent::heightAt(Fixed x, Fixed y) const { return sample(x, y).height; }

Ground Continent::groundAt(Fixed x, Fixed y) const {
    const Sample s = sample(x, y);
    Fixed surface;
    if (waterSurfaceOf(s, surface)) {
        if (onBridge(x, y, s.roadDist)) return Ground::Bridge;
        return surface - s.height < r(11, 10) ? Ground::Shallows : Ground::DeepWater;
    }
    return landGround(x, y, s.height, s.mountain, s.roadDist);
}

namespace {

const char* const kCityNames[] = {
    "Abbotsmere", "Alder Cross", "Amberley", "Ashcombe", "Barrowdale", "Beckhollow", "Blackwater", "Brackenford",
    "Bramblegate", "Briarwick", "Brightwell", "Brookhaven", "Cairnmoor", "Caldermouth", "Coldharbour", "Copperhill",
    "Crowmarsh", "Dawnmere", "Deepdale", "Dunmarrow", "Eastwatch", "Elmstead", "Embervale", "Fairhollow",
    "Fallowfield", "Fenwick", "Foxburrow", "Galeshead", "Glenmarsh", "Goldfurrow", "Greywater", "Hallowmere",
    "Harrowgate", "Hawksridge", "Hearthstone", "Highbarrow", "Hollowmere", "Juniper Hill", "Kestrel Point", "Larkspur",
    "Lindenholt", "Lonmere", "Marrowby", "Millbrook", "Mistlow", "Northgate", "Oakenshaw", "Pennyroyal",
    "Quarrystone", "Ravenmoor", "Redcliff", "Rookhaven", "Rowanbury", "Saltcombe", "Silverdale", "Southmere",
    "Stonebridge", "Stormwatch", "Thistledown", "Thornbury", "Tidewater", "Valewick", "Wardenhold", "Westerholm",
    "Whitecliff", "Willowmere", "Windholt", "Wolfsden", "Wyrmhold", "Yarrowby", "Yewdale", "Cindergate",
    "Frosthollow", "Mossbank", "Highmoor", "Larchmont"};
constexpr size_t kCityNameCount = sizeof kCityNames / sizeof kCityNames[0];

const char* const kRuinAdjectives[] = {"Ashen", "Sunken", "Hollow", "Broken", "Black", "Silent",
                                       "Drowned", "Weeping", "Shattered", "Forsaken", "Howling", "Grey"};
const char* const kRuinNouns[] = {"Barrow", "Tomb", "Crypt", "Tower", "Keep", "Shrine",
                                  "Vault", "Halls", "Catacombs", "Spire", "Henge"};
const char* const kTempleNames[] = {"Temple of the Dawn", "Temple of the Tides", "Temple of the Stars", "Temple of the Oak"};

}  // namespace

const std::vector<Continent::Faction>& Continent::factions() {
    static const std::vector<Faction> kFactions = {
        {"The Aurelian Throne", "Kingshold"}, {"The Highcrag Giants", "Highcrag"}, {"The Ironhold Dwarves", "Deepforge"},
        {"The Orcs of Grath", "Grathmaw"},    {"The Sylvaran Elves", "Sylvaris"},   {"The Sunreach Riders", "Sunreach"},
        {"The Moonward Order", "Moonhaven"},  {"Lord Malvoren", "Dreadspire"}};
    return kFactions;
}

bool Continent::buildable(Fixed x, Fixed y, Fixed radius, Fixed maxMountain, Sample& centre, Fixed& slope) const {
    if (x < m(250) || y < m(250) || x > m(kWidth - 250) || y > m(kHeight - 250)) return false;
    centre = sample(x, y);
    Fixed surface;
    if (centre.land < m(40) || centre.height < m(2) || centre.mountain > maxMountain) return false;
    if (waterSurfaceOf(centre, surface)) return false;
    if (centre.riverDist < centre.riverHalfWidth + radius + m(25)) return false;
    for (const Area& lake : lakes_)
        if (ellipse(lake, x, y) < r(9, 5)) return false;
    if (landGround(x, y, centre.height, centre.mountain, centre.roadDist) == Ground::Swamp) return false;
    Fixed worst;
    const Fixed dirs[4][2] = {{Fixed::one(), Fixed::zero()}, {-Fixed::one(), Fixed::zero()}, {Fixed::zero(), Fixed::one()}, {Fixed::zero(), -Fixed::one()}};
    for (const auto& d : dirs) {
        const Sample e = sample(x + d[0] * radius, y + d[1] * radius);
        if (waterSurfaceOf(e, surface)) return false;
        worst = max(worst, abs(e.height - centre.height));
    }
    slope = worst / radius;
    return true;
}

void Continent::placeCities() {
    // Capitals: one castle in each faction's homeland, on the flattest spot nearby.
    const Vec3 homes[kFactionCount] = {N(470, 545), N(260, 860), N(250, 150), N(810, 770),
                                       N(700, 830), N(200, 360), N(700, 470), N(800, 160)};
    std::vector<City> cities;
    for (int f = 0; f < kFactionCount; ++f) {
        Fixed bestScore = m(1000000);
        City best{factions()[f].capital, homes[f], m(170), static_cast<uint8_t>(f), true};
        for (int64_t dy = -700; dy <= 700; dy += 50) {
            for (int64_t dx = -700; dx <= 700; dx += 50) {
                const Fixed x = homes[f].x + m(dx), y = homes[f].y + m(dy);
                Sample c;
                Fixed slope;
                if (!buildable(x, y, m(170), m(60), c, slope)) continue;
                const Fixed score = slope * 4000 + sqrt(m(dx * dx + dy * dy));
                if (score < bestScore) {
                    bestScore = score;
                    best.pos = {x, y, c.height};
                }
            }
        }
        cities.push_back(best);
    }

    // The neutral cities: the best of a jittered grid, preferring roads and
    // river valleys, never crowding each other or the battlefield at the ford.
    struct Candidate {
        Vec3 pos;
        Fixed score;
        int64_t order;
    };
    std::vector<Candidate> candidates;
    const uint64_t sj = noise::nextSeed(seed_ ^ 0xC17E5);
    const Vec3 ford = crossings_.front().pos;
    int64_t order = 0;
    for (int64_t j = 0; j * 200 < kHeight; ++j) {
        for (int64_t i = 0; i * 200 < kWidth; ++i, ++order) {
            const Fixed x = m(i * 200 + 100) + (noise::lattice(sj, i, j) - r(1, 2)) * 120;
            const Fixed y = m(j * 200 + 100) + (noise::lattice(sj ^ 0x77, i, j) - r(1, 2)) * 120;
            if ((Vec3{x, y, Fixed::zero()} - ford).flat().lengthSq() < m(750) * m(750)) continue;
            Sample c;
            Fixed slope;
            if (!buildable(x, y, m(110), m(40), c, slope)) continue;
            if (slope > r(1, 8)) continue;
            Fixed score = noise::lattice(sj ^ 0x5C0, i, j) - slope * 3;
            if (c.roadDist < m(250)) score += r(3, 5);
            if (c.riverDist < m(600)) score += r(2, 5);
            candidates.push_back({{x, y, c.height}, score, order});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score != b.score ? a.score > b.score : a.order < b.order;
    });
    constexpr size_t kTotal = 80;
    std::vector<City> chosen;
    for (int64_t spacing = 1300; spacing >= 600; spacing -= 50) {
        chosen = cities;
        for (const Candidate& c : candidates) {
            if (chosen.size() >= kTotal) break;
            bool clear = true;
            for (const City& o : chosen) clear = clear && (c.pos - o.pos).flat().lengthSq() >= m(spacing) * m(spacing);
            if (clear) chosen.push_back({"", c.pos, m(110), kNeutral, false});
        }
        if (chosen.size() >= kTotal) break;
    }
    for (size_t i = kFactionCount; i < chosen.size(); ++i) chosen[i].name = kCityNames[(i - kFactionCount) % kCityNameCount];
    cities_ = std::move(chosen);
}

void Continent::placeSites() {
    const uint64_t sj = noise::nextSeed(seed_ ^ 0x5173);
    const auto farFrom = [&](const Vec3& p, Fixed fromCities, Fixed fromSites) {
        for (const City& c : cities_)
            if ((p - c.pos).flat().lengthSq() < fromCities * fromCities) return false;
        for (const Site& s : sites_)
            if ((p - s.pos).flat().lengthSq() < fromSites * fromSites) return false;
        return true;
    };
    // The best spot within `reach` of a point.
    const auto near = [&](const Vec3& at, int64_t reach, Fixed maxMountain, Vec3& out) {
        Fixed bestScore = m(1000000);
        for (int64_t dy = -reach; dy <= reach; dy += 25) {
            for (int64_t dx = -reach; dx <= reach; dx += 25) {
                Sample c;
                Fixed slope;
                const Fixed x = at.x + m(dx), y = at.y + m(dy);
                if (!buildable(x, y, m(25), maxMountain, c, slope) || !farFrom({x, y, Fixed::zero()}, m(500), m(500))) continue;
                const Fixed score = slope * 400 + sqrt(m(dx * dx + dy * dy));
                if (score < bestScore) {
                    bestScore = score;
                    out = {x, y, c.height};
                }
            }
        }
        return bestScore < m(1000000);
    };
    // The two sages: one on the Isle of the Seer, one high in the Ironveil.
    Vec3 p;
    if (near(N(66, 893), 300, m(400), p)) sites_.push_back({Site::Kind::Sage, "The Seer's Tower", p});
    if (near(N(880, 560), 500, m(400), p)) sites_.push_back({Site::Kind::Sage, "The Hermit of the Peaks", p});
    // Four temples, one in each quarter of the land.
    const Vec3 temples[4] = {N(400, 800), N(650, 900), N(380, 300), N(650, 330)};
    for (int i = 0; i < 4; ++i)
        if (near(temples[i], 800, m(80), p)) sites_.push_back({Site::Kind::Temple, kTempleNames[i], p});

    // Ruins: in the wild places, away from cities and from each other.
    struct Candidate {
        Vec3 pos;
        Fixed score;
        int64_t order;
    };
    std::vector<Candidate> candidates;
    int64_t order = 0;
    for (int64_t j = 0; j * 160 < kHeight; ++j) {
        for (int64_t i = 0; i * 160 < kWidth; ++i, ++order) {
            const Fixed x = m(i * 160 + 80) + (noise::lattice(sj, i, j) - r(1, 2)) * 100;
            const Fixed y = m(j * 160 + 80) + (noise::lattice(sj ^ 0x77, i, j) - r(1, 2)) * 100;
            Sample c;
            Fixed slope;
            if (!buildable(x, y, m(25), m(150), c, slope) || slope > r(1, 3)) continue;
            Fixed score = noise::lattice(sj ^ 0x2A1, i, j);
            if (landGround(x, y, c.height, c.mountain, c.roadDist) == Ground::Forest) score += r(1, 2);
            if (c.mountain > m(15)) score += r(2, 5);
            if (c.roadDist > m(300)) score += r(3, 10);
            candidates.push_back({{x, y, c.height}, score, order});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score != b.score ? a.score > b.score : a.order < b.order;
    });
    constexpr size_t kRuins = 34;
    size_t ruins = 0;
    for (const Candidate& c : candidates) {
        if (ruins >= kRuins) break;
        if (!farFrom(c.pos, m(650), m(950))) continue;
        std::string name = std::string(kRuinAdjectives[ruins % 12]) + " " + kRuinNouns[(ruins * 7) % 11];
        sites_.push_back({Site::Kind::Ruin, std::move(name), c.pos});
        ++ruins;
    }
}

uint64_t Continent::checksum() const {
    StateHasher h;
    for (const City& c : cities_) {
        h.addI64(c.pos.x.raw);
        h.addI64(c.pos.y.raw);
        h.addI64(c.pos.z.raw);
        h.addU64(c.owner);
    }
    for (const Site& s : sites_) {
        h.addU64(static_cast<uint64_t>(s.kind));
        h.addI64(s.pos.x.raw);
        h.addI64(s.pos.y.raw);
        h.addI64(s.pos.z.raw);
    }
    for (const River& rv : rivers_)
        for (const Vec3& p : rv.points) h.addI64(p.z.raw);
    return h.value();
}

}  // namespace illyria::sim
