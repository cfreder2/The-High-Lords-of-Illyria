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

// polylineDist over a river, skipping runs of segments whose boxes are too far.
Fixed riverDist(const Continent::River& rv, Fixed px, Fixed py, Fixed limit, size_t& seg, Fixed& segT) {
    Fixed best = limit * limit;
    bool found = false;
    for (size_t b = 0; b < rv.boxes.size(); ++b) {
        const Continent::River::Box& box = rv.boxes[b];
        if (px < box.minX - limit || px > box.maxX + limit || py < box.minY - limit || py > box.maxY + limit) continue;
        const size_t end = std::min(rv.points.size() - 1, (b + 1) * Continent::River::kSpan);
        for (size_t i = b * Continent::River::kSpan; i < end; ++i) {
            Fixed t;
            const Fixed d = segDistSq(px, py, rv.points[i], rv.points[i + 1], t);
            if (d < best) {
                best = d;
                seg = i;
                segT = t;
                found = true;
            }
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
    // The Greymere leaves the Aldwater and runs west to the sea, cutting off the
    // north-west country: only Greymere Bridge and Greymere Ford lead in.
    rivers_.push_back({"Greymere",
                       {N(575, 760), N(500, 748), N(420, 735), N(330, 722), N(270, 712), N(210, 705), N(150, 715), N(90, 712), N(0, 710)},
                       m(11), r(11, 4)});
    rivers_.push_back({"Wending", {N(300, 290), N(230, 282), N(170, 268), N(110, 262), N(0, 255)}, m(8), m(2)});
    rivers_.push_back({"Coldbrook", {N(725, 880), N(650, 855), N(590, 840)}, m(7), m(2)});

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
    ridges_.push_back({"Southdown Hills", {{N(690, 90), N(790, 70)}, m(400)}, m(120)});
    // The east is broken mountain country, not open plain.
    ridges_.push_back({"Beren Heights", {{N(640, 775), N(700, 760), N(760, 785)}, m(420)}, m(190)});
    ridges_.push_back({"Ungoeth Mountains", {{N(790, 925), N(835, 885), N(870, 850)}, m(480)}, m(330)});
    ridges_.push_back({"Kestrel Fells", {{N(735, 575), N(790, 570), N(845, 545)}, m(360)}, m(170)});
    ridges_.push_back({"Grimtor Hills", {{N(660, 300), N(700, 330)}, m(360)}, m(140)});
    ridges_.push_back({"Barrow Downs", {{N(120, 380), N(170, 360)}, m(320)}, m(90)});
    ridges_.push_back({"Witchmoor Hills", {{N(330, 120), N(380, 160)}, m(300)}, m(100)});
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
    lakes_.push_back({"Longmere", N(420, 235), m(110), m(1300)});  // the long lake in the Longwood

    crossings_.push_back({Crossing::Kind::Ford, "Oakhaven Ford", N(582, 680)});
    crossings_.push_back({Crossing::Kind::Ford, "Westrun Ford", N(300, 440)});
    crossings_.push_back({Crossing::Kind::Ford, "Sorrow Ford", N(560, 220)});
    crossings_.push_back({Crossing::Kind::Ford, "Emberflow Ford", N(730, 372)});
    crossings_.push_back({Crossing::Kind::Ford, "Greymere Ford", N(420, 735)});
    crossings_.push_back({Crossing::Kind::Bridge, "Kingsbridge", N(576, 520)});
    crossings_.push_back({Crossing::Kind::Bridge, "Old Bridge", N(160, 462)});
    crossings_.push_back({Crossing::Kind::Bridge, "Southbridge", N(566, 140)});
    crossings_.push_back({Crossing::Kind::Bridge, "Greymere Bridge", N(210, 705)});

    // Roads join the realms through the crossings and passes.
    const Fixed roadHalf = m(3);
    roads_.push_back({{N(80, 560), N(200, 555), N(330, 540), N(450, 530), N(576, 520), N(680, 515), N(790, 540), N(860, 600), N(905, 640), N(1000, 650)}, roadHalf});
    roads_.push_back({{N(300, 760), N(360, 750), N(420, 735), N(500, 700), N(582, 680), N(700, 690), N(760, 690)}, roadHalf});
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
               {"Mirefen", N(700, 640), 3}, {"Oakhaven Ford", N(582, 680), 4}, {"Kingsbridge", N(576, 520), 4},
               {"Ungoeth Mts", N(830, 930), 1}, {"Beren Heights", N(700, 795), 1}, {"Kestrel Fells", N(790, 600), 1},
               {"Longmere", N(445, 235), 2}};

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
    // Rolling hill country comes and goes in patches a few kilometres across,
    // with open plains between; it grows rougher toward the east.
    const uint64_t sHillMask = noise::nextSeed(sRidge ^ 0x4111), sHill = noise::nextSeed(sHillMask);
    const Fixed east = x / kWidth;
    const Fixed hilly = noise::smoothstep(r(2, 5), r(7, 10), noise::fbm(sHillMask, x, y, 2600) + east / 5);
    z += noise::fbm(sHill, x, y, 520) * hilly * (m(26) + east * 30);
    z += (noise::value(sHill ^ 0x77, x, y, 140) - r(1, 2)) * 5;  // hummocks
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

void Continent::meanderRivers() {
    // Real rivers wind. Each course becomes a smooth curve through its control
    // points, sampled every 20 m, then swings from side to side by an amount
    // that grows with the river. It runs straight through fords, bridges and
    // junctions, so crossings and branches stay exactly on the water.
    std::vector<Vec3> anchors;  // (x, y), z = radius of calm water
    for (const Crossing& c : crossings_) anchors.push_back({c.pos.x, c.pos.y, &c == &crossings_.front() ? m(110) : m(130)});
    for (const River& rv : rivers_) {
        anchors.push_back({rv.points.front().x, rv.points.front().y, m(300)});
        anchors.push_back({rv.points.back().x, rv.points.back().y, m(300)});
    }
    for (size_t ri = 0; ri < rivers_.size(); ++ri) {
        River& rv = rivers_[ri];
        const std::vector<Vec3> ctrl = rv.points;
        std::vector<Vec3> curve;
        for (size_t i = 0; i + 1 < ctrl.size(); ++i) {
            const Vec3 p0 = ctrl[i == 0 ? 0 : i - 1], p1 = ctrl[i], p2 = ctrl[i + 1], p3 = ctrl[std::min(i + 2, ctrl.size() - 1)];
            const int64_t steps = std::max<int64_t>(1, ((p2 - p1).flat().length() / 20).floorToInt());
            for (int64_t k = 0; k < steps; ++k) {
                const Fixed t = Fixed::fromRatio(k, steps), t2 = t * t, t3 = t2 * t;
                // Catmull-Rom.
                const Fixed two = m(2), three = m(3), four = m(4), five = m(5);
                const Vec3 a = p1 * two, b = p2 - p0, c = p0 * two - p1 * five + p2 * four - p3, d = p1 * three - p0 - p2 * three + p3;
                curve.push_back((a + b * t + c * t2 + d * t3) * r(1, 2));
            }
        }
        curve.push_back(ctrl.back());
        // Swing sideways along the arc: wavelength and amplitude follow the river's size.
        const uint64_t sm = noise::nextSeed(seed_ ^ (0xBE4D + ri * 977));
        // Two scales: broad wandering over kilometres, and tighter meanders.
        const Fixed amp = rv.halfWidth * 5 + m(6), bigAmp = rv.halfWidth * 18 + m(40);
        const int64_t wave = (rv.halfWidth * 28).floorToInt() + 160, bigWave = 2600;
        std::vector<Vec3> out(curve.size());
        rv.widths.assign(curve.size(), rv.halfWidth);
        Fixed arc;
        for (size_t i = 0; i < curve.size(); ++i) {
            if (i > 0) arc += (curve[i] - curve[i - 1]).flat().length();
            const Vec3 prev = curve[i == 0 ? 0 : i - 1], next = curve[std::min(i + 1, curve.size() - 1)];
            Vec3 dir = (next - prev).flat();
            const Fixed len = dir.length();
            Vec3 normal;
            if (len > Fixed::zero()) normal = {-dir.y / len, dir.x / len, Fixed::zero()};
            Fixed calm = Fixed::one(), bigCalm = Fixed::one();
            for (const Vec3& an : anchors) {
                const Fixed dist = (curve[i] - Vec3{an.x, an.y, Fixed::zero()}).flat().length();
                calm = min(calm, noise::smoothstep(an.z, an.z * 2, dist));
                bigCalm = min(bigCalm, noise::smoothstep(an.z, an.z + m(800), dist));
            }
            const Fixed swing = (noise::fbm(sm, arc, m(static_cast<int64_t>(ri) * 5000), wave) - r(1, 2)) * 2;
            const Fixed wander = (noise::value(sm ^ 0xB16, arc, m(static_cast<int64_t>(ri) * 9000), bigWave) - r(1, 2)) * 2;
            out[i] = curve[i] + normal * (swing * amp * calm + wander * bigAmp * bigCalm);
            out[i].z = Fixed::zero();
            // Wider in the slow reaches, narrower where it runs fast.
            rv.widths[i] = rv.halfWidth * (r(4, 5) + noise::fbm(sm ^ 0x5157, arc, m(static_cast<int64_t>(ri) * 5000), wave / 2) * r(1, 2));
        }
        rv.points = std::move(out);
        rv.boxes.clear();
        for (size_t b = 0; b * River::kSpan + 1 < rv.points.size(); ++b) {
            River::Box box{m(1000000), m(1000000), -m(1000000), -m(1000000)};
            const size_t end = std::min(rv.points.size() - 1, (b + 1) * River::kSpan);
            for (size_t i = b * River::kSpan; i <= end; ++i) {
                box.minX = min(box.minX, rv.points[i].x);
                box.minY = min(box.minY, rv.points[i].y);
                box.maxX = max(box.maxX, rv.points[i].x);
                box.maxY = max(box.maxY, rv.points[i].y);
            }
            rv.boxes.push_back(box);
        }
    }
}

void Continent::computeRiverSurfaces() {
    // Water surfaces fall monotonically downstream, a little below the land;
    // branches start at their parent's surface; rivers reaching the sea end at 0.
    meanderRivers();
    for (River& rv : rivers_) {
        Fixed start;
        bool joined = false;
        for (const River& parent : rivers_) {
            if (&parent == &rv || parent.points.empty() || parent.points.back().z == Fixed::zero()) continue;
            if ((parent.points.back() - rv.points.front()).flat().lengthSq() < m(1)) {
                start = parent.points.back().z;
                joined = true;
            }
            // Leaving mid-course: start at the parent's surface where they part.
            if (&parent < &rv && !joined) {
                for (const Vec3& p : parent.points) {
                    if ((p - rv.points.front()).flat().lengthSq() < m(2500)) {
                        start = p.z;
                        joined = true;
                    }
                }
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
        const Fixed d = riverDist(rv, x, y, kValleyReach + rv.halfWidth * 2, seg, t);
        if (d < s.riverDist) {
            s.riverDist = d;
            s.riverHalfWidth = rv.widths[seg] + (rv.widths[seg + 1] - rv.widths[seg]) * t;
            s.riverDepth = rv.depth;
            s.riverSurface = rv.points[seg].z + (rv.points[seg + 1].z - rv.points[seg].z) * t;
        }
    }
    if (s.riverDist < kValleyReach + s.riverHalfWidth) {
        // Fords: the river spreads wider and shallower over a gravel bar,
        // easing in and out rather than starting at a line.
        Fixed shoal;
        for (const Crossing& c : crossings_) {
            if (c.kind != Crossing::Kind::Ford) continue;
            const Fixed dx = x - c.pos.x, dy = y - c.pos.y;
            const Fixed d2 = dx * dx + dy * dy;
            if (d2 < m(45) * m(45)) s.atFord = true;
            if (d2 < m(90) * m(90)) shoal = max(shoal, Fixed::one() - noise::smoothstep(m(30), m(90), sqrt(d2)));
        }
        const Fixed hw = s.riverHalfWidth + shoal * 6;
        const Fixed depth = s.riverDepth + (r(3, 5) - s.riverDepth) * shoal;
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
            // The basin rim rises fast so nearby hills keep their shape.
            const Fixed beyond = max(Fixed::zero(), q - r(3, 2));
            z = min(z, surface + r(2, 5) + (q - Fixed::one()) * lake.rx / 6 + beyond * beyond * 600);
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
