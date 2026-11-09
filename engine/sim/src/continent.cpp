#include "illyria/sim/continent.h"

#include <algorithm>
#include <cstdint>

#include "continent_shapes.h"
#include "illyria/sim/campaign.h"
#include "illyria/sim/hash.h"
#include "illyria/sim/noise.h"

namespace illyria::sim {

using namespace continent_shapes;

namespace {

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

// Normalised elliptical distance: < 1 inside.
Fixed ellipse(const Continent::Area& a, Fixed x, Fixed y) {
    const Fixed dx = (x - a.centre.x) / a.rx, dy = (y - a.centre.y) / a.ry;
    return sqrt(dx * dx + dy * dy);
}

constexpr Fixed kValleyReach = scaled(450);
// How far a sample looks for the nearest river (its riverDist goes no
// further): far enough for everything placed by it (towns keep their walls
// clear of the water, the relief and the landmarks keep off its banks).
constexpr Fixed kRiverReach = Fixed::fromInt(300);
constexpr Fixed kValleySlope = Fixed::fromRatio(6, 100);
constexpr Fixed kSurfaceBelowLand = Fixed::fromRatio(3, 2);
// Cities' walls: half the size they were first drawn (three quarters of what
// they were before the map shrank to a third), the walls themselves, their
// towers, gates and ramps as thick and as wide as ever.
constexpr Fixed kCapitalRadius = Fixed::fromInt(85), kTownRadius = Fixed::fromInt(55);
// A mountain-scale height (Continent::peakHeight).
constexpr Fixed peakM(int64_t v) { return Continent::peakHeight(v); }
// Metres between two towns' walls (their radii): beyond kMaxWallGap the placer
// prefers the better site to the wider gap; it never goes below kMinWallGap.
constexpr int64_t kMaxWallGap = 180, kMinWallGap = 30;
// No town this near Oakhaven Ford: the set battle there is fought on open
// ground (Battle::setUpFord: the two armies 230 m apart either side of it).
constexpr Fixed kFordClear = Fixed::fromInt(350);

}  // namespace

Continent::Continent(uint64_t seed, Relief relief) : seed_(seed) {
    // The sea along the west coast and around the north-west, and an isle.
    sea_.push_back({N(-100, 0), N(-60, 1000), scaled(2100), false});
    sea_.push_back({N(0, 1080), N(400, 1100), scaled(2000), false});
    sea_.push_back({N(60, 880), N(75, 905), scaled(380), true});

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
    ridges_.push_back({"Thornspine Mountains", {{N(200, 930), N(260, 905), N(330, 885), N(400, 905), N(470, 880), N(530, 900)}, scaled(700)}, peakM(430)});
    ridges_.push_back({"Graycrown Mountains", {{N(180, 610), N(240, 595), N(300, 615)}, scaled(650)}, peakM(380)});
    ridges_.push_back({"Graycrown Mountains", {{N(375, 620), N(430, 600), N(505, 615)}, scaled(650)}, peakM(400)});
    ridges_.push_back({"Ashen Hills", {{N(250, 360), N(310, 345), N(360, 365)}, scaled(450)}, peakM(170)});
    ridges_.push_back({"Ironveil Range",
                       {{N(950, 1020), N(915, 880), N(935, 760), N(900, 640), N(940, 520), N(905, 400), N(935, 270), N(900, 140), N(930, -20)}, scaled(900)},
                       peakM(460)});
    ridges_.push_back({"Frostfang Spur", {{N(910, 720), N(840, 705), N(770, 715)}, scaled(450)}, peakM(240)});
    ridges_.push_back({"Cinder Spur", {{N(905, 300), N(830, 280), N(760, 290)}, scaled(450)}, peakM(260)});
    ridges_.push_back({"Dragonback Mountains", {{N(60, 90), N(140, 60), N(220, 85), N(290, 55), N(340, 90)}, scaled(650)}, peakM(390)});
    ridges_.push_back({"Northmoor Hills", {{N(700, 940), N(790, 960)}, scaled(600)}, peakM(140)});
    ridges_.push_back({"Southdown Hills", {{N(690, 90), N(790, 70)}, scaled(400)}, peakM(120)});
    // The east is broken mountain country, not open plain.
    ridges_.push_back({"Beren Heights", {{N(640, 775), N(700, 760), N(760, 785)}, scaled(420)}, peakM(190)});
    ridges_.push_back({"Ungoeth Mountains", {{N(790, 925), N(835, 885), N(870, 850)}, scaled(480)}, peakM(330)});
    ridges_.push_back({"Kestrel Fells", {{N(735, 575), N(790, 570), N(845, 545)}, scaled(360)}, peakM(170)});
    ridges_.push_back({"Grimtor Hills", {{N(660, 300), N(700, 330)}, scaled(360)}, peakM(140)});
    ridges_.push_back({"Barrow Downs", {{N(120, 380), N(170, 360)}, scaled(320)}, peakM(90)});
    ridges_.push_back({"Witchmoor Hills", {{N(330, 120), N(380, 160)}, scaled(300)}, peakM(100)});
    // Passes: notches through the ranges (z = radius).
    passes_.push_back({N(905, 640).x, N(905, 640).y, scaled(550)});
    passes_.push_back({N(330, 885).x, N(330, 885).y, scaled(450)});
    passes_.push_back({N(220, 85).x, N(220, 85).y, scaled(420)});

    swamps_.push_back({"Mirefen", N(700, 640), scaled(650), scaled(800)});
    swamps_.push_back({"Saltmarsh", N(120, 225), scaled(400), scaled(320)});
    forests_.push_back({"Darkwood", N(170, 800), scaled(900), scaled(1200)});
    forests_.push_back({"Elderwood", N(400, 520), scaled(700), scaled(600)});
    forests_.push_back({"Thornwood", N(760, 560), scaled(500), scaled(700)});
    forests_.push_back({"Longwood", N(420, 230), scaled(380), scaled(2400)});
    forests_.push_back({"Eastwood", N(700, 200), scaled(600), scaled(500)});
    forests_.push_back({"Northwood", N(700, 820), scaled(500), scaled(400)});
    lakes_.push_back({"Mirrormere", N(200, 275), scaled(300), scaled(220)});
    lakes_.push_back({"Silverlake", N(790, 425), scaled(340), scaled(250)});
    lakes_.push_back({"Longmere", N(420, 235), scaled(110), scaled(1300)});  // the long lake in the Longwood
    // The Heartmere, where the Aldwater meets the Westrun and the Sorrow: a
    // broad lake at the rivers' own level, an isle off its eastern shore with
    // an abbey's ruins, and a narrow spit of land, one company wide, the only
    // way across on foot. It must stay last of the lakes (its surface is the
    // rivers', see below).
    lakes_.push_back({"Heartmere", N(578, 449), scaled(900), scaled(720)});
    isles_.push_back({{"Abbey Isle", N(598, 444), scaled(190), scaled(150)}, lakes_.size() - 1, m(7)});
    isles_.push_back({{"the Spit", N(633, 446), scaled(330), m(16)}, lakes_.size() - 1, r(6, 5)});

    crossings_.push_back({Crossing::Kind::Ford, "Oakhaven Ford", N(582, 680)});
    crossings_.push_back({Crossing::Kind::Ford, "Westrun Ford", N(300, 440)});
    crossings_.push_back({Crossing::Kind::Ford, "Sorrow Ford", N(560, 220)});
    crossings_.push_back({Crossing::Kind::Ford, "Emberflow Ford", N(730, 372)});
    crossings_.push_back({Crossing::Kind::Ford, "Greymere Ford", N(420, 735)});
    crossings_.push_back({Crossing::Kind::Bridge, "Kingsbridge", N(576, 520)});
    crossings_.push_back({Crossing::Kind::Bridge, "Old Bridge", N(160, 462)});
    crossings_.push_back({Crossing::Kind::Bridge, "Southbridge", N(566, 140)});
    crossings_.push_back({Crossing::Kind::Bridge, "Greymere Bridge", N(210, 705)});
    // Footbridges: narrow timber spans away from the roads, a way over for
    // infantry that no cart or column can use quickly.
    crossings_.push_back({Crossing::Kind::Footbridge, "Heron Footbridge", N(583, 702)});
    crossings_.push_back({Crossing::Kind::Footbridge, "Millrace Footbridge", N(568, 300)});
    crossings_.push_back({Crossing::Kind::Footbridge, "Hangman's Span", N(240, 451)});

    // Roads join the realms through the crossings and passes (and keep off
    // the lakes' wandering shores: half a campaign cell clear).
    const Fixed roadHalf = m(3);
    roads_.push_back({{N(80, 560), N(200, 555), N(330, 540), N(450, 530), N(576, 520), N(680, 515), N(790, 540), N(860, 600), N(905, 640), N(1000, 650)}, roadHalf});
    roads_.push_back({{N(300, 760), N(360, 750), N(420, 735), N(500, 700), N(582, 680), N(700, 690), N(760, 690)}, roadHalf});
    roads_.push_back({{N(330, 1000), N(330, 885), N(330, 800), N(250, 760), N(210, 705), N(230, 620), N(300, 560), N(330, 540), N(300, 440),
                       N(320, 330), N(265, 300), N(160, 180), N(220, 85), N(230, 0)}, roadHalf});
    roads_.push_back({{N(680, 515), N(660, 420), N(730, 372), N(700, 250), N(566, 140), N(450, 120), N(330, 90)}, roadHalf});
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

    shapeRoads();
    // The sea took the old shores: rivers end a little way out to sea (so
    // they meet the water), and roads stop where they come down to it.
    {
        const auto land = [&](const Vec3& p) { return landDistance(p.x, p.y) > Fixed::zero(); };
        const auto coast = [&](Vec3 in, Vec3 out) {  // in on land, out at sea
            for (int k = 0; k < 24; ++k) {
                const Vec3 mid = (in + out) * r(1, 2);
                (land(mid) ? in : out) = mid;
            }
            return out;
        };
        const auto ashore = [&](std::vector<Vec3>& p, bool mouth) {  // trims the sea off the end
            for (size_t i = 1; i < p.size(); ++i) {
                if (land(p[i]) || !land(p[i - 1])) continue;
                Vec3 c = coast(p[i - 1], p[i]);
                if (mouth) {
                    const Vec3 d = (p[i] - p[i - 1]).flat();
                    c = c + d / d.length() * m(80);
                }
                p.resize(i);
                p.push_back(c);
                return;
            }
        };
        for (River& rv : rivers_) {
            // A spring the sea took rises again just inside the coast.
            std::reverse(rv.points.begin(), rv.points.end());
            ashore(rv.points, false);
            std::reverse(rv.points.begin(), rv.points.end());
            if (!rv.points.empty() && !land(rv.points.front()) && rv.points.size() > 1) {
                const Vec3 d = (rv.points[1] - rv.points[0]).flat();
                rv.points.front() = rv.points.front() + d / d.length() * scaled(180);
            }
            ashore(rv.points, true);
        }
        for (Polyline& rd : roads_) {
            // Points and widths trimmed together (the coast point takes its neighbour's width).
            std::vector<Vec3> withW(rd.points.size());
            for (size_t i = 0; i < rd.points.size(); ++i) withW[i] = {rd.points[i].x, rd.points[i].y, rd.widths[i]};
            ashore(withW, false);
            std::reverse(withW.begin(), withW.end());
            ashore(withW, false);
            std::reverse(withW.begin(), withW.end());
            rd.points.clear();
            rd.widths.clear();
            for (const Vec3& p : withW) {
                rd.points.push_back({p.x, p.y, Fixed::zero()});
                rd.widths.push_back(p.z > Fixed::zero() ? p.z : rd.halfWidth);
            }
        }
    }
    bucketRoads();
    if (relief == Relief::Baked) loadBakedRelief();
    computeRiverSurfaces();
    for (const Area& lake : lakes_) {
        Fixed mountain;
        lakeSurfaces_.push_back(reliefHeight(lake.centre.x, lake.centre.y, mountain) - m(3));
    }
    // The Heartmere lies at the level of the rivers that meet in it.
    lakeSurfaces_.back() = rivers_.front().points.back().z;
    keepRoadsDry();
    placeBridges();
    planSetPieces();
    placeCities();
    routeApproaches();
    planCastles();
    layApproaches();
    placeSites();
    // Then the battle-scale relief and the landmarks, clear of all of them.
    dress();
    // Last, level the ground each ruin stands on (its platform is a building
    // that sits flat, not a slope; carve).
    for (const Site& s : sites_)
        if (s.kind == Site::Kind::Ruin) ruinPads_.push_back(s.pos);
}

void Continent::bucketRoads() {
    // Roads by bucket, so sampling looks only at the segments near a point.
    bucketNx_ = static_cast<int32_t>(kWidth / kStreamBucket) + 1;
    bucketNy_ = static_cast<int32_t>(kHeight / kStreamBucket) + 1;
    roadBuckets_.assign(static_cast<size_t>(bucketNx_) * bucketNy_, {});
    const Fixed reach = m(66);
    for (uint32_t ri = 0; ri < roads_.size(); ++ri) {
        const Polyline& road = roads_[ri];
        for (uint32_t si = 0; si + 1 < road.points.size(); ++si) {
            const Vec3 &a = road.points[si], &b = road.points[si + 1];
            const int64_t x0 = std::max<int64_t>(0, ((min(a.x, b.x) - reach) / kStreamBucket).floorToInt());
            const int64_t x1 = std::min<int64_t>(bucketNx_ - 1, ((max(a.x, b.x) + reach) / kStreamBucket).floorToInt());
            const int64_t y0 = std::max<int64_t>(0, ((min(a.y, b.y) - reach) / kStreamBucket).floorToInt());
            const int64_t y1 = std::min<int64_t>(bucketNy_ - 1, ((max(a.y, b.y) + reach) / kStreamBucket).floorToInt());
            for (int64_t by = y0; by <= y1; ++by)
                for (int64_t bx = x0; bx <= x1; ++bx) roadBuckets_[static_cast<size_t>(by * bucketNx_ + bx)].push_back({ri, si});
        }
    }
}

void Continent::planCastles() {
    const uint64_t sc = noise::nextSeed(seed_ ^ 0xCA571E);
    for (size_t ci = 0; ci < cities_.size(); ++ci) {
        City& city = cities_[ci];
        Castle& k = city.castle;
        const auto roll = [&](int64_t n) { return noise::lattice(sc, static_cast<int64_t>(ci), n); };
        const Vec3 c = city.pos;
        // The gate faces the nearest road.
        Vec3 toward{Fixed::zero(), -Fixed::one(), Fixed::zero()};
        // (The way its road goes, once it has left the walls; else the nearest road's.)
        const std::vector<Vec3>& way = approach_[ci];
        size_t leaves = 0;
        while (leaves < way.size() && (way[leaves] - c).flat().length() < city.radius + m(8)) ++leaves;
        if (leaves < way.size()) {
            toward = unitFlat(way[leaves] - c);
        } else {
            Fixed best = m(100000);
            for (const Polyline& road : roads_) {
                size_t seg = 0;
                Fixed t;
                const Fixed d = polylineDist(road.points, c.x, c.y, m(3000), seg, t);
                if (d >= best || d < m(1)) continue;
                best = d;
                const Vec3 p = road.points[seg] + (road.points[seg + 1] - road.points[seg]) * t;
                toward = unitFlat(p - c);
            }
        }
        const Vec3 side{-toward.y, toward.x, Fixed::zero()};
        const Fixed R = city.radius * 4 / 5;
        k.shape = city.capital ? 2 : static_cast<uint8_t>((roll(1) * 4).floorToInt());
        // Corners in the frame (side, toward), then into the world.
        std::vector<Vec3> local;
        switch (k.shape) {
            case 0: {  // square
                for (int i = 0; i < 4; ++i) {
                    const Fixed sx = (i == 0 || i == 3) ? -R : R, sy = i < 2 ? R : -R;
                    local.push_back({sx * r(17, 20), sy * r(17, 20), Fixed::zero()});
                }
                break;
            }
            case 1: {  // long: a rectangle stretched along the road
                for (int i = 0; i < 4; ++i) {
                    const Fixed sx = (i == 0 || i == 3) ? -R * r(3, 5) : R * r(3, 5), sy = i < 2 ? R * r(23, 20) : -R * r(23, 20);
                    local.push_back({sx, sy, Fixed::zero()});
                }
                break;
            }
            case 2: {  // polygon, its corners pushed in and out: 8 sides for a capital,
                       // 5 or 6 for a town (any more and its walls are too short for a ramp)
                const int n = city.capital ? 8 : 5 + static_cast<int>((roll(2) * 2).floorToInt());
                for (int i = 0; i < n; ++i) {
                    // Rotate so an edge, not a corner, faces the gate.
                    const Fixed a = Fixed::fromRatio(2 * i + 1, 2 * n) * 6283 / 1000 + Fixed::fromRatio(1571, 1000);
                    const Fixed rr = R * (r(17, 20) + roll(10 + i) * r(1, 4));
                    local.push_back({cos(a) * rr, sin(a) * rr, Fixed::zero()});
                }
                break;
            }
            default: {  // shield: flat front with the gate, pointed back
                local = {{-R, R * r(3, 5), Fixed::zero()}, {R, R * r(3, 5), Fixed::zero()}, {R * r(4, 5), -R * r(1, 2), Fixed::zero()},
                         {Fixed::zero(), -R * r(6, 5), Fixed::zero()}, {-R * r(4, 5), -R * r(1, 2), Fixed::zero()}};
                break;
            }
        }
        const auto world = [&](const Vec3& l) { return Vec3{c.x + side.x * l.x + toward.x * l.y, c.y + side.y * l.x + toward.y * l.y, c.z}; };
        // Thick enough that two men pass on the walk behind the parapet.
        const Fixed wallH = city.capital ? m(11) : m(7) + roll(3) * 3, thick = city.capital ? r(9, 2) : m(4);
        Castle::Ring outer;
        outer.height = wallH;
        outer.thick = thick;
        outer.gateWidth = m(8);
        for (const Vec3& l : local) outer.corners.push_back(world(l));
        // The gate goes in the edge whose middle faces the road best.
        Fixed bestDot = -m(10);
        for (size_t i = 0; i < outer.corners.size(); ++i) {
            const Vec3 mid = (outer.corners[i] + outer.corners[(i + 1) % outer.corners.size()]) * r(1, 2);
            const Fixed dot = unitFlat(mid - c).dot(toward);
            if (dot > bestDot) {
                bestDot = dot;
                outer.gateEdge = i;
            }
        }
        k.rings.push_back(outer);
        if (city.capital) {
            // An inner bailey: higher walls round the keep, square, so each
            // wall is long enough to take a ramp up to its walk.
            Castle::Ring inner;
            inner.height = wallH * 6 / 5;
            inner.thick = thick;
            inner.gateWidth = m(6);
            for (int i = 0; i < 4; ++i) {
                const Fixed a = Fixed::fromRatio(2 * i + 1, 8) * 6283 / 1000 + Fixed::fromRatio(1571, 1000);
                inner.corners.push_back(world({cos(a) * R / 2, sin(a) * R / 2 - R * r(1, 5), Fixed::zero()}));
            }
            inner.gateEdge = 0;
            Fixed bd = -m(10);
            for (size_t i = 0; i < inner.corners.size(); ++i) {
                const Vec3 mid = (inner.corners[i] + inner.corners[(i + 1) % inner.corners.size()]) * r(1, 2);
                const Fixed dot = unitFlat(mid - c).dot(toward);
                if (dot > bd) {
                    bd = dot;
                    inner.gateEdge = i;
                }
            }
            k.rings.push_back(inner);
        }
        // A pair of towers flanking each gate, and towers at the corners where
        // the wall turns sharply and at about every kTowerSpacing metres of wall
        // between (no more than that: a tower to every corner of a ring of
        // short walls crowds it; and none standing against a gate tower).
        for (Castle::Ring& ring : k.rings) {
            const size_t n = ring.corners.size();
            const Vec3 a = ring.corners[ring.gateEdge], b = ring.corners[(ring.gateEdge + 1) % n];
            const Vec3 mid = (a + b) * r(1, 2), along = unitFlat(b - a);
            Vec3 gateTower[2];
            for (int s = -1; s <= 1; s += 2) gateTower[(s + 1) / 2] = mid + along * (ring.gateTowerAt() * s);
            // Walking round from the gate's far side: the wall since the last tower.
            ring.towered.assign(n, 0);
            Fixed run = (ring.corners[(ring.gateEdge + 1) % n] - gateTower[1]).flat().length();
            for (size_t step = 1; step <= n; ++step) {
                const size_t i = (ring.gateEdge + step) % n;
                const Vec3 prev = ring.corners[(i + n - 1) % n], next = ring.corners[(i + 1) % n];
                const bool sharp = unitFlat(ring.corners[i] - prev).dot(unitFlat(next - ring.corners[i])) < Castle::kSharpCorner;
                bool clear = true, apart = true;
                for (const Vec3& g : gateTower) {
                    const Fixed d = (ring.corners[i] - g).flat().length();
                    clear = clear && d > ring.thick * 3 + m(8);
                    apart = apart && d > ring.thick * 27 / 10 + m(3);
                }
                const Fixed onward = (next - ring.corners[i]).flat().length();
                if (apart && (sharp || (clear && run >= m(Castle::kTowerSpacing)))) {
                    ring.towered[i] = 1;
                    run = Fixed::zero();
                }
                run += onward;
            }
            for (size_t i = 0; i < n; ++i)
                if (ring.towered[i]) k.towers.push_back({ring.corners[i], ring.thick * 3 / 2, ring.height * 4 / 3, roll(40 + static_cast<int64_t>(i)) < r(2, 5)});
            for (const Vec3& g : gateTower) k.towers.push_back({g, ring.thick * 6 / 5, ring.height * 5 / 4, false});
        }
        // The keep stands back from the gate; the courtyard lies between.
        k.keepAxis = toward;
        k.keepHalf = city.capital ? m(10) : m(5) + roll(4) * 2;
        k.keepHeight = city.capital ? m(28) : m(15) + roll(5) * 6;
        // (A capital's in the middle of its inner bailey.)
        k.keep = city.capital ? world({Fixed::zero(), -R * r(1, 5), Fixed::zero()}) : world({(roll(6) - r(1, 2)) * R * r(2, 5), -R * r(2, 5), Fixed::zero()});
        // (A capital's inner bailey reaches to 0.19 R from the middle, so its courtyard, where the men muster and the
        // militia stand, lies beyond it: between the bailey's wall and the outer wall, which is at 0.78 R or more.)
        k.courtyard = world({Fixed::zero(), R * (city.capital ? r(9, 20) : r(3, 10)), Fixed::zero()});
        k.courtyardRadius = R * (city.capital ? r(1, 4) : r(7, 20));
        // Ramps up the inner faces: three on the outer ring (either side of
        // the gate and the back wall, or the next ones round where the inner
        // bailey is in the way) and one inside the inner bailey. Each starts on
        // the ground and climbs along the wall, 1 in 2.5 (as steep as 1 in 2
        // where the wall is short), to a level landing at the walk.
        for (size_t ri = 0; ri < k.rings.size(); ++ri) {
            const Castle::Ring& ring = k.rings[ri];
            const size_t n = ring.corners.size();
            const Fixed climb = ring.walk();
            const size_t g = ring.gateEdge, back = (g + n / 2) % n;
            const std::vector<size_t> edges = ri == 0 ? std::vector<size_t>{(g + n - 1) % n, (g + 1) % n, back, (back + 1) % n, (back + n - 1) % n}
                                                      : std::vector<size_t>{back, (back + 1) % n, (back + n - 1) % n};
            size_t placed = 0;
            for (size_t e : edges) {
                if (placed == (ri == 0 ? 3u : 1u)) break;
                const Vec3 a = ring.corners[e], b = ring.corners[(e + 1) % n];
                const Vec3 along = unitFlat(b - a);
                Vec3 in{-along.y, along.x, Fixed::zero()};
                if (in.dot(ring.centre() - a) < Fixed::zero()) in = -in;
                const Fixed len = (b - a).flat().length();
                // Clear of the corner towers' galleries at both ends, where
                // there is room; on a short wall the foot goes in under the
                // first one (high overhead), but the landing stays clear of
                // the last by a stretch of the walk to step onto, and of the
                // foot of the tower.
                const Fixed gallery = ring.thick * 3 / 2 + Castle::kGalleryWidth + r(5, 2);
                const Fixed room = len - gallery * 2 - Castle::kRampLanding;
                const Fixed run = min(climb * 5 / 2, max(climb * 2, room));
                const Fixed from = min(len / 2 - (run + Castle::kRampLanding) / 2, len - gallery - run - Castle::kRampLanding);
                if (from < ring.thick * 3 / 2 + m(1)) continue;
                const Vec3 start = a + along * from + in * (ring.thick / 2 + Castle::kRampHalfWidth);
                Castle::Ramp rp{start, start + along * run, Castle::kRampHalfWidth, ri};
                rp.head.z = c.z + climb;
                // Not against another ring's walls or towers (room to walk round it).
                bool clear = true;
                for (size_t oi = 0; oi < k.rings.size() && clear; ++oi) {
                    if (oi == ri) continue;
                    const Castle::Ring& other = k.rings[oi];
                    const Fixed keepOff = other.thick * 3 / 2 + Castle::kRampHalfWidth + m(3);
                    for (int64_t q = 0; q <= 8 && clear; ++q) {
                        const Vec3 p = start + along * ((run + Castle::kRampLanding) * q / 8);
                        for (size_t j = 0; j < other.corners.size() && clear; ++j) {
                            Fixed t;
                            clear = sqrt(segDistSq(p.x, p.y, other.corners[j], other.corners[(j + 1) % other.corners.size()], t)) > keepOff;
                        }
                    }
                }
                if (!clear) continue;
                k.ramps.push_back(rp);
                ++placed;
            }
        }
        // Houses in rows about 11 m apart (none nearer another than 8 m, the
        // longest a house is), clear of the walls and their ramps, the keep,
        // the courtyard and the street to the gate.
        const int rows = city.capital ? 12 : 8;
        const Fixed step = R * 2 / rows;
        for (int iy = 0; iy < rows; ++iy) {
            for (int ix = 0; ix < rows; ++ix) {
                const Fixed lx = -R + step * ix + step / 2 + (roll(1000 + iy * 32 + ix) - r(1, 2)) * step / 4;
                const Fixed ly = -R + step * iy + step / 2 + (roll(2000 + iy * 32 + ix) - r(1, 2)) * step / 4;
                if (abs(lx) < m(6)) continue;  // the high street
                const Vec3 p = world({lx, ly, Fixed::zero()});
                if ((p - k.courtyard).flat().length() < k.courtyardRadius + m(4)) continue;
                if ((p - k.keep).flat().length() < k.keepHalf * 3 / 2 + m(6)) continue;
                // Inside the outer ring with room to spare from its walls.
                bool inside = true;
                const Castle::Ring& ring = k.rings.front();
                for (size_t i = 0; i < ring.corners.size() && inside; ++i) {
                    const Vec3 a = ring.corners[i], b = ring.corners[(i + 1) % ring.corners.size()];
                    const Vec3 e = unitFlat(b - a);
                    Vec3 in{-e.y, e.x, Fixed::zero()};
                    if (in.dot(c - a) < Fixed::zero()) in = -in;
                    inside = (p - a).flat().dot(in) > ring.thick + m(6);
                }
                if (!inside) continue;
                if (k.rings.size() > 1) {
                    // Not across the inner bailey's walls either.
                    const Castle::Ring& in2 = k.rings[1];
                    Vec3 mid{};
                    for (const Vec3& q : in2.corners) mid += q;
                    mid = mid / Fixed::fromInt(static_cast<int64_t>(in2.corners.size()));
                    if ((p - mid).flat().length() < R * r(13, 20)) continue;
                }
                const bool turned = roll(3000 + iy * 32 + ix) < r(1, 2);
                const Fixed hx = m(2) + roll(4000 + iy * 32 + ix) * 2, hy = Fixed::fromRatio(17, 10) + roll(5000 + iy * 32 + ix) * Fixed::fromRatio(13, 10);
                const Fixed halfX = turned ? hy : hx, halfY = turned ? hx : hy;
                // Clear of the galleries round the towers (the walk goes round them).
                bool clear = true;
                for (const Castle::Tower& t : k.towers) {
                    const Fixed dx = max(abs(p.x - t.pos.x) - halfX, Fixed::zero()), dy = max(abs(p.y - t.pos.y) - halfY, Fixed::zero());
                    clear = clear && dx * dx + dy * dy > (t.radius + Castle::kGalleryWidth + r(1, 2)) * (t.radius + Castle::kGalleryWidth + r(1, 2));
                }
                if (!clear) continue;
                k.houses.push_back({p, halfX, halfY, m(3) + roll(6000 + iy * 32 + ix) * 3});
            }
        }
    }
}

void Continent::placeBridges(size_t firstRoad, size_t firstCrossing) {
    // Where roads cross creeks: small stone bridges, one per crossing.
    for (size_t roadIndex = firstRoad; roadIndex < roads_.size(); ++roadIndex) {
        const Polyline& road = roads_[roadIndex];
        for (size_t i = 0; i + 1 < road.points.size(); ++i) {
            const Vec3 a = road.points[i], b = road.points[i + 1];
            const Fixed minX = min(a.x, b.x), maxX = max(a.x, b.x), minY = min(a.y, b.y), maxY = max(a.y, b.y);
            for (const River& st : streams_) {
                for (size_t bi = 0; bi < st.boxes.size(); ++bi) {
                    const River::Box& box = st.boxes[bi];
                    if (box.maxX < minX || box.minX > maxX || box.maxY < minY || box.minY > maxY) continue;
                    const size_t end = std::min(st.points.size() - 1, (bi + 1) * River::kSpan);
                    for (size_t k = bi * River::kSpan; k < end; ++k) {
                        const Vec3 p = st.points[k], q = st.points[k + 1];
                        // Segment intersection, in road (t) and stream (u) parameters.
                        const Fixed rx = b.x - a.x, ry = b.y - a.y, sx = q.x - p.x, sy = q.y - p.y;
                        const Fixed den = rx * sy - ry * sx;
                        if (abs(den) < r(1, 100)) continue;
                        const Fixed t = ((p.x - a.x) * sy - (p.y - a.y) * sx) / den;
                        const Fixed u = ((p.x - a.x) * ry - (p.y - a.y) * rx) / den;
                        if (t < Fixed::zero() || t > Fixed::one() || u < Fixed::zero() || u > Fixed::one()) continue;
                        Crossing cr{Crossing::Kind::Bridge, "", {a.x + rx * t, a.y + ry * t, Fixed::zero()}};
                        cr.halfWidth = m(5) / 2;
                        cr.halfSpan = st.widths[k] + m(9);
                        crossings_.push_back(cr);
                    }
                }
            }
        }
    }
    for (size_t crossingIndex = firstCrossing; crossingIndex < crossings_.size(); ++crossingIndex) {
        Crossing& cr = crossings_[crossingIndex];
        if (!cr.bridged()) continue;
        const bool foot = cr.kind == Crossing::Kind::Footbridge;
        // Along the road it carries, or straight across the river it spans.
        size_t seg = 0;
        Fixed t, best = m(1000000);
        for (const Polyline& road : roads_) {
            const Fixed d = polylineDist(road.points, cr.pos.x, cr.pos.y, m(30), seg, t);
            if (!foot && d < best) {
                best = d;
                cr.dir = unitFlat((road.points[seg + 1] - road.points[seg]).flat());
            }
        }
        if (foot || best > m(29)) {
            Fixed nearest = m(400);
            const Vec3 wanted = cr.pos;
            for (const River& rv : rivers_) {
                const Fixed d = riverDist(rv, wanted.x, wanted.y, m(400), seg, t);
                if (d >= nearest) continue;
                nearest = d;
                const Vec3 along = unitFlat((rv.points[seg + 1] - rv.points[seg]).flat());
                cr.pos = rv.points[seg] + (rv.points[seg + 1] - rv.points[seg]) * t;
                cr.dir = {-along.y, along.x, Fixed::zero()};
            }
        }
        cr.pos.z = Fixed::zero();
        if (cr.halfWidth == Fixed::zero()) cr.halfWidth = foot ? r(9, 10) : m(4);
        // Span the water to dry banks on both sides.
        Fixed surface = -m(1000), reach[2] = {m(6), m(6)}, bank = -m(1000);
        {
            Fixed s0;
            if (waterSurfaceOf(sample(cr.pos.x, cr.pos.y), s0)) surface = s0;
        }
        for (int side = 0; side < 2; ++side) {
            const Vec3 d = side ? -cr.dir : cr.dir;
            for (int64_t k = 2; k <= 90; k += 2) {
                const Vec3 p = cr.pos + d * m(k);
                const Sample s = sample(p.x, p.y);
                Fixed sf;
                if (waterSurfaceOf(s, sf)) {
                    surface = max(surface, sf);
                    continue;
                }
                if (s.height < surface + r(1, 2)) continue;
                reach[side] = m(k + 4);
                bank = max(bank, s.height);
                break;
            }
        }
        cr.halfSpan = max(cr.halfSpan, max(reach[0], reach[1]));
        cr.deck = max(bank, surface + (foot ? r(3, 2) : m(3)));
        if (surface < -m(999)) cr.deck = max(bank, heightAt(cr.pos.x, cr.pos.y) + Fixed::one());
    }
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
    // The open sea all round: Illyria is an island. Its coast runs a third of
    // a kilometre to 1.4 km inside the map's edge, its corners worn round, with bays
    // and headlands at every scale.
    {
        const Fixed hx = m(kWidth / 2), hy = m(kHeight / 2), round = scaled(3200);
        const Fixed qx = abs(x - hx) - (hx - round), qy = abs(y - hy) - (hy - round);
        const Fixed ox = max(qx, Fixed::zero()), oy = max(qy, Fixed::zero());
        const Fixed inside = round - sqrt(ox * ox + oy * oy) - min(max(qx, qy), Fixed::zero());  // metres in from the worn edge
        const uint64_t s3 = noise::nextSeed(s2 ^ 0x15A1D), s4 = noise::nextSeed(s3), s5 = noise::nextSeed(s4);
        const uint64_t s6 = noise::nextSeed(s5);
        Fixed margin = scaled(150) + noise::fbm(s6, x, y, scaledI(7000)) * scaledI(2600) + noise::fbm(s3, x, y, scaledI(2600)) * scaledI(900) +
                       (noise::fbm(s4, x, y, scaledI(1100)) - r(1, 2)) * scaledI(600) + (noise::fbm(s5, x, y, scaledI(340)) - r(1, 2)) * scaledI(220);
        // The mountains run out to the sea as headlands rather than drown.
        for (const Ridge& rg : ridges_) {
            Fixed t;
            for (size_t i = 0; i + 1 < rg.line.points.size(); ++i) {
                const Fixed d = sqrt(segDistSq(x, y, rg.line.points[i], rg.line.points[i + 1], t));
                if (d < scaled(1500)) margin = margin - (scaled(1500) - d) * r(3, 4);
            }
        }
        sea = min(sea, inside - margin);
    }
    const Fixed wobble = (noise::fbm(s1, x, y, scaledI(2400)) - r(1, 2)) * scaledI(900) + (noise::fbm(s2, x, y, scaledI(500)) - r(1, 2)) * scaledI(200);
    return max(sea + wobble, island + wobble / 2);
}

Fixed Continent::massifMask(Fixed x, Fixed y, Fixed& peak) const {
    const uint64_t sBase = noise::nextSeed(seed_ ^ 0xBA5E), sSmall = noise::nextSeed(sBase), sRidge = noise::nextSeed(sSmall);
    Fixed massif;
    peak = Fixed::zero();
    for (const Ridge& rg : ridges_) {
        size_t seg = 0;
        Fixed t;
        const Fixed reach = rg.line.halfWidth * r(11, 5);
        const Fixed d = polylineDist(rg.line.points, x, y, reach, seg, t);
        if (d >= reach) continue;
        const Fixed ragged = d + (noise::fbm(sRidge ^ 0x7A6, x, y, scaledI(700)) - r(1, 2)) * rg.line.halfWidth;
        const Fixed shape = Fixed::one() - noise::smoothstep(Fixed::zero(), reach, max(Fixed::zero(), ragged));
        if (shape * rg.peak > massif * max(peak, Fixed::one())) {
            massif = shape;
            peak = rg.peak;
        }
    }
    return massif;
}

Fixed Continent::reliefHeight(Fixed x, Fixed y, Fixed& mountain) const {
    const Fixed z = baseHeight(x, y, mountain);
    if (!baked_) return z;
    // What the rain did to the design, between the bake's 32 m samples
    // (Catmull-Rom, so no creases run along the grid).
    const Fixed fx = clamp(x / reliefCell_, Fixed::one(), Fixed::fromInt(reliefNx_ - 3));
    const Fixed fy = clamp(y / reliefCell_, Fixed::one(), Fixed::fromInt(reliefNy_ - 3));
    const int64_t ix = fx.floorToInt(), iy = fy.floorToInt();
    const auto weights = [](Fixed t, Fixed w[4]) {
        const Fixed t2 = t * t, t3 = t2 * t;
        w[0] = (-t + t2 * 2 - t3) / 2;
        w[1] = (Fixed::fromInt(2) - t2 * 5 + t3 * 3) / 2;
        w[2] = (t + t2 * 4 - t3 * 3) / 2;
        w[3] = (t3 - t2) / 2;
    };
    Fixed wx[4], wy[4];
    weights(fx - Fixed::fromInt(ix), wx);
    weights(fy - Fixed::fromInt(iy), wy);
    int64_t sum = 0;  // dm, scaled by both weights' raw units
    for (int j = 0; j < 4; ++j) {
        const int16_t* row = &reliefDelta_[static_cast<size_t>((iy - 1 + j) * reliefNx_ + ix - 1)];
        int64_t line = 0;
        for (int i = 0; i < 4; ++i) line += static_cast<int64_t>(row[i]) * wx[i].raw;
        sum += line * wy[j].raw;
    }
    return z + Fixed::fromRaw((sum >> 16) / 10);
}

Fixed Continent::baseHeight(Fixed x, Fixed y, Fixed& mountain) const {
    const uint64_t sBase = noise::nextSeed(seed_ ^ 0xBA5E), sSmall = noise::nextSeed(sBase), sRidge = noise::nextSeed(sSmall);
    const uint64_t sHillMask = noise::nextSeed(sRidge ^ 0x4111), sHill = noise::nextSeed(sHillMask), sRoll = noise::nextSeed(sHill);
    const Fixed east = x / kWidth;
    // Broad swells of the land.
    Fixed z = m(10) + noise::fbm(sBase, x, y, scaledI(2400)) * scaledI(60);
    // Gentle rolling relief everywhere, worn by water (no ground is dead flat).
    z += (noise::eroded(sRoll, x, y, scaledI(650), 6) - r(1, 3)) * scaled(22);
    // Hill country comes and goes in patches kilometres across, rougher eastward.
    const Fixed hilly = noise::smoothstep(r(7, 20), r(7, 10), noise::fbm(sHillMask, x, y, scaledI(2600)) + east / 4);
    z += noise::eroded(sHill, x, y, scaledI(1300), 7) * hilly * (scaled(55) + east * scaledI(60));
    z += (noise::value(sHill ^ 0x77, x, y, 140) - r(1, 2)) * 4;  // hummocks
    // Mountains: each range is a massif whose ragged edge fades into
    // foothills, carved by ridged multifractal crests and ravines.
    mountain = Fixed::zero();
    Fixed peak;
    const Fixed massif = massifMask(x, y, peak);
    if (massif > Fixed::zero()) {
        // Warp the domain a little so ridgelines wander rather than run on a grid.
        const Fixed wx = x + (noise::value(sRidge ^ 0x51, x, y, scaledI(1700)) - r(1, 2)) * scaledI(600);
        const Fixed wy = y + (noise::value(sRidge ^ 0x52, x, y, scaledI(1700)) - r(1, 2)) * scaledI(600);
        const Fixed crests = noise::ridgedMF(sRidge, wx, wy, scaledI(2200), 5);
        const Fixed worn = noise::eroded(sRidge ^ 0xE20D, x, y, scaledI(900), 5);
        const Fixed lift = massif * massif * (Fixed::fromInt(3) - massif * 2);  // smooth foothills
        mountain = peak * lift * (r(1, 5) + crests * r(7, 10) + worn * r(1, 10));
        // Crags, buttresses and gullies tens of metres across, rougher the higher it climbs.
        const Fixed crag = noise::ridgedMF(sRidge ^ 0xC4A6, wx, wy, 240, 3);
        mountain += lift * (crag - r(1, 3)) * (m(6) + mountain / 12);
    }
    for (const Vec3& p : passes_) {
        // A saddle with a ragged outline, not a round notch.
        const Fixed dx = x - p.x, dy = y - p.y;
        const Fixed rr = p.z * (r(4, 5) + noise::value(sRidge ^ 0x9A55, x, y, scaledI(300)) * r(2, 5));
        mountain = mountain * (Fixed::one() - bump(sqrt(dx * dx + dy * dy) / rr) * r(7, 10));
    }
    return z + mountain;
}

void Continent::shapeRoads() {
    // Dirt roads, not ruled lines: a smooth curve through the road's way
    // points, sampled every 40 m, wandering gently from side to side (but
    // straight through its way points, where roads meet, cross rivers and climb
    // passes), and wider or narrower as it goes, from about 2 m to 4.5 m each
    // side of its middle.
    std::vector<Vec3> anchors;  // (x, y), z = radius held straight
    for (const Polyline& rd : roads_)
        for (const Vec3& p : rd.points) anchors.push_back({p.x, p.y, scaled(210)});
    for (const Crossing& c : crossings_) anchors.push_back({c.pos.x, c.pos.y, scaled(240)});
    for (size_t ri = 0; ri < roads_.size(); ++ri) {
        Polyline& rd = roads_[ri];
        const std::vector<Vec3> ctrl = rd.points;
        std::vector<Vec3> curve;
        for (size_t i = 0; i + 1 < ctrl.size(); ++i) {
            const Vec3 p0 = ctrl[i == 0 ? 0 : i - 1], p1 = ctrl[i], p2 = ctrl[i + 1], p3 = ctrl[std::min(i + 2, ctrl.size() - 1)];
            const int64_t steps = std::max<int64_t>(1, ((p2 - p1).flat().length() / 40).floorToInt());
            for (int64_t k = 0; k < steps; ++k) {
                const Fixed t = Fixed::fromRatio(k, steps), t2 = t * t, t3 = t2 * t;
                const Fixed two = m(2), three = m(3), four = m(4), five = m(5);
                const Vec3 a = p1 * two, b = p2 - p0, c = p0 * two - p1 * five + p2 * four - p3, d = p1 * three - p0 - p2 * three + p3;
                curve.push_back((a + b * t + c * t2 + d * t3) * r(1, 2));
            }
        }
        curve.push_back(ctrl.back());
        const uint64_t sr = noise::nextSeed(seed_ ^ (0x40AD + ri * 1013));
        std::vector<Vec3> out(curve.size());
        rd.widths.assign(curve.size(), rd.halfWidth);
        Fixed arc;
        for (size_t i = 0; i < curve.size(); ++i) {
            if (i > 0) arc += (curve[i] - curve[i - 1]).flat().length();
            const Vec3 prev = curve[i == 0 ? 0 : i - 1], next = curve[std::min(i + 1, curve.size() - 1)];
            const Vec3 dir = (next - prev).flat();
            const Fixed len = dir.length();
            Vec3 normal;
            if (len > Fixed::zero()) normal = {-dir.y / len, dir.x / len, Fixed::zero()};
            Fixed calm = Fixed::one();
            for (const Vec3& an : anchors) calm = min(calm, noise::smoothstep(an.z, an.z * 3, (curve[i] - Vec3{an.x, an.y, Fixed::zero()}).flat().length()));
            // Bends round every hillock and copse, and broad sweeps over a few kilometres.
            const Fixed bend = (noise::fbm(sr, arc, m(static_cast<int64_t>(ri) * 6000), 420) - r(1, 2)) * 2 * m(22);
            const Fixed sweep = (noise::value(sr ^ 0x5EE, arc, m(static_cast<int64_t>(ri) * 8000), scaledI(2200)) - r(1, 2)) * 2 * scaled(80);
            out[i] = curve[i] + normal * ((bend + sweep) * calm);
            out[i].z = Fixed::zero();
            rd.widths[i] = m(3) * (r(7, 10) + noise::fbm(sr ^ 0x31D, arc, m(static_cast<int64_t>(ri) * 7000), 700) * r(4, 5));
        }
        rd.points = std::move(out);
    }
}

void Continent::meanderRivers() {
    // Real rivers wind. Each course becomes a smooth curve through its control
    // points, sampled every 20 m, then swings from side to side by an amount
    // that grows with the river. It runs straight through fords, bridges and
    // junctions, so crossings and branches stay exactly on the water.
    std::vector<Vec3> anchors;  // (x, y), z = radius of calm water
    for (const Crossing& c : crossings_) anchors.push_back({c.pos.x, c.pos.y, &c == &crossings_.front() ? m(110) : m(130)});
    for (const River& rv : rivers_) {
        anchors.push_back({rv.points.front().x, rv.points.front().y, scaled(300)});
        anchors.push_back({rv.points.back().x, rv.points.back().y, scaled(300)});
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
        const Fixed amp = rv.halfWidth * 5 + m(6), bigAmp = (rv.halfWidth * 12 + m(40)) * 2 / 9;
        const int64_t wave = (rv.halfWidth * 28).floorToInt() + 160, bigWave = scaledI(2600);
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
                bigCalm = min(bigCalm, noise::smoothstep(an.z, an.z + scaled(800), dist));
            }
            const Fixed swing = (noise::fbm(sm, arc, m(static_cast<int64_t>(ri) * 5000), wave) - r(1, 2)) * 2;
            const Fixed wander = (noise::value(sm ^ 0xB16, arc, m(static_cast<int64_t>(ri) * 9000), bigWave) - r(1, 2)) * 2;
            out[i] = curve[i] + normal * (swing * amp * calm + wander * bigAmp * bigCalm);
            out[i].z = Fixed::zero();
            // Wider in the slow reaches, narrower where it runs fast.
            rv.widths[i] = rv.halfWidth * (r(11, 20) + noise::fbm(sm ^ 0x5157, arc, m(static_cast<int64_t>(ri) * 5000), wave / 2) * r(11, 10));
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
            Fixed ground = reliefHeight(rv.points[i].x, rv.points[i].y, mountain);
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
    Fixed z = reliefHeight(x, y, s.mountain);
    if (s.land < Fixed::zero()) {
        z = max(-m(40), -m(2) + s.land / 60);
    } else {
        // Beaches at the shore, the land rising smoothly behind them (no capped plateau).
        z = r(1, 2) + (z - r(1, 2)) * noise::smoothstep(Fixed::zero(), scaled(700), s.land) + s.land / 200;
    }
    s.height = z;
    return s;
}

void Continent::carve(Sample& s, Fixed x, Fixed y) const {
    Fixed z = s.height;
    // The lowest bed of water here (river, lake or creek): the towns' pads
    // blend over the land around them, never into the water.
    Fixed bed = m(100000);

    // Distance to the nearest road, as if every road were 3 m from its middle
    // to its edge: wider stretches count as nearer, narrow ones farther.
    s.roadDist = m(60);
    if (!roadBuckets_.empty()) {
        const int64_t bx = clamp(x / kStreamBucket, Fixed::zero(), Fixed::fromInt(bucketNx_ - 1)).floorToInt();
        const int64_t by = clamp(y / kStreamBucket, Fixed::zero(), Fixed::fromInt(bucketNy_ - 1)).floorToInt();
        for (const auto& [ri, si] : roadBuckets_[static_cast<size_t>(by * bucketNx_ + bx)]) {
            const Polyline& road = roads_[ri];
            Fixed t;
            const Fixed centre = sqrt(segDistSq(x, y, road.points[si], road.points[si + 1], t));  // sets t first
            const Fixed d = centre - (road.widthAt(si, t) - m(3));
            s.roadDist = min(s.roadDist, max(Fixed::zero(), d));
        }
    } else {
        size_t seg = 0;
        Fixed t;
        for (const Polyline& road : roads_) s.roadDist = min(s.roadDist, polylineDist(road.points, x, y, m(60), seg, t));
    }

    // Rivers: a broad valley, then the channel itself.
    s.riverDist = max(kValleyReach, kRiverReach) + m(1000);
    for (const River& rv : rivers_) {
        size_t seg = 0;
        Fixed t;
        const Fixed d = riverDist(rv, x, y, max(kValleyReach, kRiverReach) + rv.halfWidth * 2, seg, t);
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
            bed = min(bed, z);
        } else {
            // The valley floor near the river, its walls blending back into
            // the land's own slopes (no crease where a limit cuts in).
            const Fixed floor = s.riverSurface + r(2, 5) + (s.riverDist - hw) * kValleySlope;
            if (z > floor) z = floor + (z - floor) * noise::smoothstep(kValleyReach * r(3, 10), kValleyReach, s.riverDist - hw);
        }
    }

    // Lakes sit in basins with a flat surface.
    for (size_t i = 0; i < naturalLakes(); ++i) applyLake(i, z, s, x, y);
    // Isles rise out of their lake: a rounded hump, its shore a little above the water.
    for (const Isle& isle : isles_) {
        const Fixed q = ellipse(isle.area, x, y);
        if (q >= r(3, 2)) continue;
        const Fixed surface = lakeSurfaces_[isle.lake];
        const Fixed hump = max(Fixed::zero(), Fixed::one() - q * q);
        // Its shore shelves into the lake as gently as the lake's own.
        const Fixed shore = surface + r(1, 2) + (Fixed::one() - q) * max(isle.area.ry / 6, m(4));
        z = max(z, min(surface + r(1, 2) + isle.rise * hump * hump, shore));
        if (z >= surface) s.inLake = false;
    }
    if (s.inLake) bed = min(bed, z);

    // The battle-scale relief: knolls, ridges and the landmarks, then the
    // landmarks' own ponds in the ground they shaped (continent_relief.cpp).
    if (dressed_) {
        const Fixed before = z;
        z += reliefAt(x, y, s);
        shapeLandmarks(z, x, y);
        for (size_t i = naturalLakes(); i < lakes_.size(); ++i) applyLake(i, z, s, x, y);
        s.relief = z - before;
    }

    // Streams from the drainage bake: a creek in a small gully of its own.
    if (!streams_.empty()) {
        const int64_t bx = clamp(x / kStreamBucket, Fixed::zero(), Fixed::fromInt(bucketNx_ - 1)).floorToInt();
        const int64_t by = clamp(y / kStreamBucket, Fixed::zero(), Fixed::fromInt(bucketNy_ - 1)).floorToInt();
        constexpr Fixed kGully = Fixed::fromInt(40);
        for (const auto& [si, bi] : streamBuckets_[static_cast<size_t>(by * bucketNx_ + bx)]) {
            const River& st = streams_[si];
            const River::Box& box = st.boxes[bi];
            const Fixed limit = kGully + st.halfWidth * 2;
            if (x < box.minX - limit || x > box.maxX + limit || y < box.minY - limit || y > box.maxY + limit) continue;
            const size_t end = std::min(st.points.size() - 1, (bi + 1) * River::kSpan);
            for (size_t i = bi * River::kSpan; i < end; ++i) {
                Fixed t;
                const Fixed d2 = segDistSq(x, y, st.points[i], st.points[i + 1], t);
                if (d2 >= s.streamDist * s.streamDist) continue;
                s.streamDist = sqrt(d2);
                s.streamHalfWidth = st.widths[i] + (st.widths[i + 1] - st.widths[i]) * t;
                s.streamSurface = st.points[i].z + (st.points[i + 1].z - st.points[i].z) * t;
            }
        }
        const Fixed hw = s.streamHalfWidth;
        if (s.streamDist < hw) {
            const Fixed t = s.streamDist / hw;
            z = min(z, s.streamSurface - (r(2, 5) + hw / 5) * (Fixed::one() - t * t));
            bed = min(bed, z);
        } else if (s.streamDist < hw + kGully) {
            const Fixed floor = s.streamSurface + r(1, 5) + (s.streamDist - hw) * r(3, 10);
            if (z > floor) z = floor + (z - floor) * noise::smoothstep(kGully * r(1, 5), kGully, s.streamDist - hw);
        }
    }
    // Water pooled in hollows the bake found (its shore follows the land).
    if (baked_ && !reliefBasin_.empty()) {
        const int64_t ix = clamp((x + Fixed::fromInt(reliefCell_ / 2)) / reliefCell_, Fixed::zero(), Fixed::fromInt(reliefNx_ - 1)).floorToInt();
        const int64_t iy = clamp((y + Fixed::fromInt(reliefCell_ / 2)) / reliefCell_, Fixed::zero(), Fixed::fromInt(reliefNy_ - 1)).floorToInt();
        const int16_t b = reliefBasin_[static_cast<size_t>(iy * reliefNx_ + ix)];
        if (b != INT16_MIN) {
            s.inBasin = true;
            s.basinSurface = Fixed::fromRatio(b, 10);
        }
    }

    // Cities stand on levelled pads that blend into the land around them. A
    // town's own pad is level out to its radius, whatever its neighbours'
    // blends do (towns stand closer together than their blends reach).
    const City* on = nullptr;
    for (const City& c : cities_) {
        const Fixed dx = abs(x - c.pos.x), dy = abs(y - c.pos.y), reach = c.radius * 2;
        if (dx >= reach || dy >= reach) continue;
        const Fixed d = sqrt(dx * dx + dy * dy);
        if (d >= reach) continue;
        if (d <= c.radius) on = &c;
        const Fixed w = Fixed::one() - noise::smoothstep(c.radius, reach, d);
        z = z + (c.pos.z - z) * w;
    }
    if (on) {
        // Level and dry: no pool or creek of the bake's floods a town's pad
        // (the rain fell on the towns where the design had them).
        z = on->pos.z;
        s.inBasin = false;
        s.streamDist = m(1000);
    } else {
        z = min(z, bed);
    }
    // So do the ruins, on smaller ones (Site::kRuinPad).
    for (const Vec3& p : ruinPads_) {
        const Fixed inner = m(Site::kRuinPad), reach = inner + m(Site::kRuinPadBlend);
        const Fixed dx = abs(x - p.x), dy = abs(y - p.y);
        if (dx >= reach || dy >= reach) continue;
        const Fixed d = sqrt(dx * dx + dy * dy);
        if (d >= reach) continue;
        const Fixed w = Fixed::one() - noise::smoothstep(inner, reach, d);
        z = z + (p.z - z) * w;
    }

    s.height = z;
}

void Continent::applyLake(size_t i, Fixed& z, Sample& s, Fixed x, Fixed y) const {
    const Area& lake = lakes_[i];
    Fixed q = ellipse(lake, x, y);
    if (q > m(3)) return;
    // Shores wander: bays, points and coves, not a drawn ellipse.
    const uint64_t sl = noise::nextSeed(seed_ ^ (0x1A4E + i * 131));
    q = q * (r(4, 5) + noise::fbm(sl, x, y, std::max<int64_t>(scaledI(180), (lake.rx / 3).floorToInt())) * r(2, 5));
    const Fixed surface = lakeSurfaces_[i];
    // A shelving shore, about 1 in 6, that runs on into the basin rim:
    // no step at the waterline, so no band of rock along it.
    const Fixed scale = max(lake.rx / 6, scaled(60));
    // A road along the shore keeps to a low causeway above the water.
    const Fixed causeway = Fixed::one() - noise::smoothstep(m(6), m(16), s.roadDist);
    if (causeway > Fixed::zero()) q = max(q, Fixed::one() + causeway * r(3, 5) / scale);
    const Fixed shelf = (q - Fixed::one()) * scale;
    if (q < Fixed::one()) {
        s.inLake = true;
        s.lakeSurface = surface;
        z = min(z, max(surface - Fixed::one() - (Fixed::one() - q) * 3, surface + shelf));
    } else {
        // The basin rim rises fast so nearby hills keep their shape.
        const Fixed beyond = max(Fixed::zero(), q - r(3, 2));
        z = min(z, surface + shelf + beyond * beyond * 600);
    }
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
    if (s.streamDist < s.streamHalfWidth + m(2) && s.height < s.streamSurface) {
        surface = s.streamSurface;
        return true;
    }
    if (s.inBasin && s.height < s.basinSurface) {
        surface = s.basinSurface;
        return true;
    }
    return false;
}

bool Continent::onBridge(Fixed x, Fixed y, Fixed& deck) const {
    for (const Crossing& c : crossings_) {
        if (!c.bridged()) continue;
        const Fixed dx = x - c.pos.x, dy = y - c.pos.y;
        if (abs(dx) > c.halfSpan + c.halfWidth || abs(dy) > c.halfSpan + c.halfWidth) continue;
        const Fixed along = dx * c.dir.x + dy * c.dir.y, across = dx * c.dir.y - dy * c.dir.x;
        if (abs(along) <= c.halfSpan && abs(across) <= c.halfWidth) {
            deck = c.deck;
            return true;
        }
    }
    return false;
}

Ground Continent::landGround(Fixed x, Fixed y, Fixed height, Fixed mountain, Fixed roadDist) const {
    if (roadDist < m(3)) return Ground::Road;
    for (const City& c : cities_) {
        const Fixed dx = abs(x - c.pos.x), dy = abs(y - c.pos.y);
        if (dx < c.radius && dy < c.radius && dx * dx + dy * dy < c.radius * c.radius) return Ground::Plains;
    }
    if ((mountain > peakM(70) && height > peakM(70)) || height > peakM(180)) return Ground::Rock;
    // Beaches: low ground along the sea is sand, wider where the shore is flat.
    if (height < m(5)) {
        const uint64_t sb = noise::nextSeed(seed_ ^ 0xBEAC);
        const Fixed reach = m(50) + noise::fbm(sb, x, y, 400) * 120 - height * 12;
        if (landDistance(x, y) < reach) return Ground::Sand;
    }
    for (const Area& a : swamps_) {
        const uint64_t sw = noise::nextSeed(seed_ ^ 0x5A4);
        if (ellipse(a, x, y) + (noise::fbm(sw, x, y, scaledI(300)) - r(1, 2)) / 2 < Fixed::one()) return Ground::Swamp;
    }
    const uint64_t sf = noise::nextSeed(seed_ ^ 0xF0E57);
    const Fixed n = noise::fbm(sf, x, y, scaledI(600));
    for (const Area& a : forests_) {
        if (ellipse(a, x, y) + (n - r(1, 2)) < Fixed::one()) return Ground::Forest;
    }
    // Scattered woods elsewhere, thinning with height: groves a battlefield
    // holds a few of, as large as they ever were (the forests' edges above
    // wander on the map's own scale).
    if (noise::fbm(sf, x, y, 400) > r(67, 100) && height < peakM(150)) return Ground::Forest;
    return Ground::Plains;
}

bool Continent::waterSurface(Fixed x, Fixed y, Fixed& surface) const { return waterSurfaceOf(sample(x, y), surface); }

Fixed Continent::heightAt(Fixed x, Fixed y) const { return sample(x, y).height; }

Ground Continent::groundAt(Fixed x, Fixed y) const {
    const Sample s = sample(x, y);
    Fixed surface;
    if (waterSurfaceOf(s, surface)) {
        Fixed deck;
        if (onBridge(x, y, deck)) return Ground::Bridge;
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
    if (x < scaled(250) || y < scaled(250) || x > m(kWidth) - scaled(250) || y > m(kHeight) - scaled(250)) return false;
    centre = sample(x, y);
    Fixed surface;
    if (centre.land < m(40) || centre.height < m(2) || centre.mountain > maxMountain) return false;
    if (waterSurfaceOf(centre, surface)) return false;
    if (centre.riverDist < centre.riverHalfWidth + radius + m(15)) return false;
    if (centre.streamDist < centre.streamHalfWidth + radius / 2 || centre.inBasin) return false;
    // Clear of the lakes' wandering shores, however small the lake.
    for (const Area& lake : lakes_)
        if (ellipse(lake, x, y) < r(9, 5) || ellipse({lake.name, lake.centre, lake.rx * r(6, 5) + radius + m(10), lake.ry * r(6, 5) + radius + m(10)}, x, y) < Fixed::one())
            return false;
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
    // Beside the roads rather than astride them (every road's edge outside
    // the walls) where there is room.
    const auto offRoads = [&](Fixed x, Fixed y, Fixed radius) {
        for (const Polyline& rd : roads_) {
            size_t seg = 0;
            Fixed t;
            if (polylineDist(rd.points, x, y, radius + m(5), seg, t) < radius + rd.widthAt(seg, t)) return false;
        }
        return true;
    };
    // Capitals: one castle in each faction's homeland, on the flattest spot nearby.
    const Vec3 homes[kFactionCount] = {N(470, 545), N(260, 860), N(250, 150), N(810, 770),
                                       N(700, 830), N(200, 360), N(700, 470), N(800, 160)};
    std::vector<City> cities;
    for (int f = 0; f < kFactionCount; ++f) {
        Fixed bestScore = m(1000000);
        City best{factions()[f].capital, homes[f], kCapitalRadius, static_cast<uint8_t>(f), true};
        for (int64_t dy = -scaledI(700); dy <= scaledI(700); dy += scaledI(50)) {
            for (int64_t dx = -scaledI(700); dx <= scaledI(700); dx += scaledI(50)) {
                const Fixed x = homes[f].x + m(dx), y = homes[f].y + m(dy);
                Sample c;
                Fixed slope;
                if (onSetPiece({x, y, Fixed::zero()}, kCapitalRadius + m(20)) || !buildable(x, y, kCapitalRadius, peakM(60), c, slope)) continue;
                const Fixed score = slope * scaledI(4000) + sqrt(m(dx * dx + dy * dy)) + (offRoads(x, y, kCapitalRadius) ? Fixed::zero() : m(200));
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
    const int64_t grid = scaledI(100);
    for (int64_t j = 0; j * grid < kHeight; ++j) {
        for (int64_t i = 0; i * grid < kWidth; ++i, ++order) {
            const Fixed x = m(i * grid + grid / 2) + (noise::lattice(sj, i, j) - r(1, 2)) * scaledI(60);
            const Fixed y = m(j * grid + grid / 2) + (noise::lattice(sj ^ 0x77, i, j) - r(1, 2)) * scaledI(60);
            if ((Vec3{x, y, Fixed::zero()} - ford).flat().lengthSq() < kFordClear * kFordClear) continue;
            if (onSetPiece({x, y, Fixed::zero()}, kTownRadius + m(20))) continue;
            Sample c;
            Fixed slope;
            // Up into the foothills and on slopes to 1 in 5 (the pad levels
            // it): the land is crowded since the map shrank to a third.
            if (!buildable(x, y, kTownRadius, peakM(80), c, slope)) continue;
            if (slope > r(1, 5)) continue;
            Fixed score = noise::lattice(sj ^ 0x5C0, i, j) - slope * 3;
            if (c.roadDist < scaled(250)) score += r(3, 5);
            if (c.riverDist < scaled(600)) score += r(2, 5);
            // (Never astride a road: its walls would run across it, and a road has one gate to lead to.)
            if (!offRoads(x, y, kTownRadius)) continue;
            candidates.push_back({{x, y, c.height}, score, order});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score != b.score ? a.score > b.score : a.order < b.order;
    });
    // Spaced by their walls, not their centres (a capital is wider than a
    // town), and spread as evenly as the land allows: each in turn goes
    // where the gap to the nearest walls is widest (the best scored first,
    // while there is room for kMaxWallGap anywhere), never nearer than
    // kMinWallGap.
    constexpr size_t kTotal = 80;
    std::vector<City> chosen = cities;
    std::vector<Fixed> gapTo(candidates.size(), m(100000));
    const auto keepClear = [&](const City& o) {
        for (size_t i = 0; i < candidates.size(); ++i)
            gapTo[i] = min(gapTo[i], (candidates[i].pos - o.pos).flat().length() - kTownRadius - o.radius);
    };
    for (const City& o : chosen) keepClear(o);
    while (chosen.size() < kTotal) {
        size_t best = candidates.size();
        Fixed widest = -m(kMaxWallGap);
        for (size_t i = 0; i < candidates.size(); ++i) {
            if (gapTo[i] < m(kMinWallGap)) continue;
            const Fixed gap = min(gapTo[i], m(kMaxWallGap));
            if (gap >= widest && (gap > widest || best == candidates.size())) {
                widest = gap;
                best = i;
            }
        }
        if (best == candidates.size()) break;
        chosen.push_back({"", candidates[best].pos, kTownRadius, kNeutral, false});
        keepClear(chosen.back());
    }
    for (size_t i = kFactionCount; i < chosen.size(); ++i) chosen[i].name = kCityNames[(i - kFactionCount) % kCityNameCount];
    cities_ = std::move(chosen);
}

std::vector<Continent::Area> Continent::isles() const {
    std::vector<Area> out;
    for (const Isle& isle : isles_) out.push_back(isle.area);
    return out;
}

std::vector<Vec3> Continent::isleWalk() const {
    std::vector<Vec3> out;
    for (const Isle& isle : isles_) {
        const Area& a = isle.area;
        const bool alongX = a.rx >= a.ry;
        const int64_t half = (alongX ? a.rx : a.ry).floorToInt();
        for (int64_t t = -half; t <= half; t += 4) {
            const Vec3 p = a.centre + Vec3{alongX ? m(t) : Fixed::zero(), alongX ? Fixed::zero() : m(t), Fixed::zero()};
            Fixed surface;
            if (!waterSurface(p.x, p.y, surface)) out.push_back({p.x, p.y, Fixed::zero()});
        }
    }
    return out;
}

std::vector<uint8_t> Continent::walkableCells() const {
    // As Campaign::initTerrain has them: water (but a bridge, or land along
    // an isle's middle) and mountain are closed to walkers. Rivers are left
    // open between cells: they are crossed at the fords and bridges.
    constexpr int64_t cell = Campaign::kCell;
    const int32_t nx = static_cast<int32_t>(kWidth / cell), ny = static_cast<int32_t>(kHeight / cell);
    std::vector<uint8_t> open(static_cast<size_t>(nx) * ny, 0), reached(open.size(), 0);
    for (int32_t y = 0; y < ny; ++y)
        for (int32_t x = 0; x < nx; ++x) {
            const Fixed px = m(x * cell + cell / 2), py = m(y * cell + cell / 2);
            const Sample s = sample(px, py);
            Fixed surface, deck;
            const bool wet = waterSurfaceOf(s, surface) && !onBridge(px, py, deck);
            const bool mountain = landGround(px, py, s.height, s.mountain, s.roadDist) == Ground::Rock && s.mountain > peakM(90);
            open[static_cast<size_t>(y) * nx + x] = !wet && !mountain;
        }
    for (const Vec3& p : isleWalk()) open[static_cast<size_t>(p.y.floorToInt() / cell) * nx + p.x.floorToInt() / cell] = 1;
    std::vector<int32_t> queue;
    for (const City& c : cities_) {
        const int32_t k = static_cast<int32_t>(c.pos.y.floorToInt() / cell) * nx + static_cast<int32_t>(c.pos.x.floorToInt() / cell);
        if (!reached[static_cast<size_t>(k)]) queue.push_back(k);
        reached[static_cast<size_t>(k)] = 1;
    }
    for (size_t head = 0; head < queue.size(); ++head) {
        const int32_t cx = queue[head] % nx, cy = queue[head] / nx;
        for (int32_t dy = -1; dy <= 1; ++dy)
            for (int32_t dx = -1; dx <= 1; ++dx) {
                const int32_t x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= nx || y >= ny) continue;
                const size_t k = static_cast<size_t>(y) * nx + x;
                if (reached[k] || !open[k]) continue;
                reached[k] = 1;
                queue.push_back(static_cast<int32_t>(k));
            }
    }
    return reached;
}

void Continent::placeSites() {
    const uint64_t sj = noise::nextSeed(seed_ ^ 0x5173);
    // Every site is reached on foot (a hero walks with his stack): on a
    // campaign cell joined to the towns by open ground, the mountains' by
    // their passes.
    const std::vector<uint8_t> walkable = walkableCells();
    const auto onFoot = [&](Fixed x, Fixed y) {
        const int64_t cx = x.floorToInt() / Campaign::kCell, cy = y.floorToInt() / Campaign::kCell, nx = kWidth / Campaign::kCell;
        return cx >= 0 && cy >= 0 && cx < nx && cy < kHeight / Campaign::kCell && walkable[static_cast<size_t>(cy * nx + cx)] != 0;
    };
    const auto farFrom = [&](const Vec3& p, Fixed fromCities, Fixed fromSites) {
        for (const City& c : cities_)
            if ((p - c.pos).flat().lengthSq() < fromCities * fromCities) return false;
        for (const Site& s : sites_)
            if ((p - s.pos).flat().lengthSq() < fromSites * fromSites) return false;
        return true;
    };
    // The best spot within `reach` of a point (twice and three times as far
    // if there is none: in the high mountains, the nearest reached on foot).
    const auto near = [&](const Vec3& at, int64_t reach, Fixed maxMountain, Vec3& out) {
        Fixed bestScore = m(1000000);
        for (int64_t wider = 1; wider <= 3 && bestScore == m(1000000); ++wider) {
            const int64_t far = reach * wider;
            for (int64_t dy = -far; dy <= far; dy += 10) {
                for (int64_t dx = -far; dx <= far; dx += 10) {
                    Sample c;
                    Fixed slope;
                    const Fixed x = at.x + m(dx), y = at.y + m(dy);
                    if (!onFoot(x, y) || !buildable(x, y, m(25), maxMountain, c, slope) || !farFrom({x, y, Fixed::zero()}, scaled(500), scaled(500)) ||
                        onSetPiece({x, y, Fixed::zero()}, m(70)))
                        continue;
                    const Fixed score = slope * scaledI(400) + sqrt(m(dx * dx + dy * dy));
                    if (score < bestScore) {
                        bestScore = score;
                        out = {x, y, c.height};
                    }
                }
            }
        }
        return bestScore < m(1000000);
    };
    // The two sages: one on the Isle of the Seer, one high in the Ironveil.
    Vec3 p;
    if (near(N(66, 893), scaledI(300), peakM(400), p)) sites_.push_back({Site::Kind::Sage, "The Seer's Tower", p});
    if (near(N(880, 560), scaledI(500), peakM(400), p)) sites_.push_back({Site::Kind::Sage, "The Hermit of the Peaks", p});
    // Four temples, one in each quarter of the land.
    const Vec3 temples[4] = {N(400, 800), N(650, 900), N(380, 300), N(650, 330)};
    for (int i = 0; i < 4; ++i)
        if (near(temples[i], scaledI(800), peakM(80), p)) sites_.push_back({Site::Kind::Temple, kTempleNames[i], p});

    // Ruins: in the wild places, away from cities and from each other.
    struct Candidate {
        Vec3 pos;
        Fixed score;
        int64_t order;
    };
    std::vector<Candidate> candidates;
    int64_t order = 0;
    const int64_t grid = scaledI(160);
    for (int64_t j = 0; j * grid < kHeight; ++j) {
        for (int64_t i = 0; i * grid < kWidth; ++i, ++order) {
            const Fixed x = m(i * grid + grid / 2) + (noise::lattice(sj, i, j) - r(1, 2)) * scaledI(100);
            const Fixed y = m(j * grid + grid / 2) + (noise::lattice(sj ^ 0x77, i, j) - r(1, 2)) * scaledI(100);
            Sample c;
            Fixed slope;
            if (!onFoot(x, y) || !buildable(x, y, m(25), peakM(150), c, slope) || slope > r(1, 3)) continue;
            Fixed score = noise::lattice(sj ^ 0x2A1, i, j);
            if (landGround(x, y, c.height, c.mountain, c.roadDist) == Ground::Forest) score += r(1, 2);
            if (c.mountain > peakM(15)) score += r(2, 5);
            if (c.roadDist > scaled(300)) score += r(3, 10);
            candidates.push_back({{x, y, c.height}, score, order});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        return a.score != b.score ? a.score > b.score : a.order < b.order;
    });
    // The abbey's ruins on Abbey Isle in the Heartmere, reached by the Spit.
    {
        const Vec3 at = isles_.front().area.centre;
        sites_.push_back({Site::Kind::Ruin, "The Drowned Abbey", {at.x, at.y, sample(at.x, at.y).height}});
    }
    // As far from the towns and each other as still leaves room for all of
    // them: the land is crowded since the map shrank to a third.
    constexpr size_t kRuins = 33;
    const size_t before = sites_.size();
    for (int64_t pull = 0; pull <= 8; ++pull) {
        sites_.resize(before);
        size_t ruins = 0;
        for (const Candidate& c : candidates) {
            if (ruins >= kRuins) break;
            if (!farFrom(c.pos, scaled(650 - pull * 40), scaled(950 - pull * 60)) || onSetPiece(c.pos, m(70))) continue;
            std::string name = std::string(kRuinAdjectives[ruins % 12]) + " " + kRuinNouns[(ruins * 7) % 11];
            sites_.push_back({Site::Kind::Ruin, std::move(name), c.pos});
            ++ruins;
        }
        if (ruins >= kRuins) break;
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
    for (const Crossing& cr : crossings_) {
        h.addU64(static_cast<uint64_t>(cr.kind));
        for (Fixed v : {cr.pos.x, cr.pos.y, cr.dir.x, cr.dir.y, cr.halfSpan, cr.halfWidth, cr.deck}) h.addI64(v.raw);
    }
    h.addU64(streams_.size());
    for (const River& st : streams_)
        for (const Vec3& p : st.points) {
            h.addI64(p.x.raw);
            h.addI64(p.z.raw);
        }
    h.addU64(landmarks_.size());
    for (const Landmark& lm : landmarks_) {
        h.addU64(static_cast<uint64_t>(lm.kind));
        for (Fixed v : {lm.centre.x, lm.centre.y, lm.centre.z, lm.radius, lm.rise, lm.top}) h.addI64(v.raw);
        for (const Vec3& p : lm.line)
            for (Fixed v : {p.x, p.y, p.z}) h.addI64(v.raw);
        for (const Obstacle& o : lm.buildings)
            for (Fixed v : {o.pos.x, o.pos.y, o.pos.z, o.halfX, o.halfY, o.height}) h.addI64(v.raw);
    }
    return h.value();
}

// The baked relief, compiled in from engine/sim/data/illyria_relief.bin
// (generated by tools/terrainbake). Little-endian:
//   "ILRF", u32 version, i32 nx, ny, cell;
//   i16 delta[nx * ny] (dm the land was worn down or built up from the design),
//   i16 basin[nx * ny] (dm water level, INT16_MIN = none);
//   u32 streams; per stream u32 n, then n x {i32 x dm, i32 y dm, i16 surface dm, u16 half width cm}.
namespace data {
extern const unsigned char kRelief[];
extern const size_t kReliefSize;
}  // namespace data

void Continent::loadBakedRelief() {
    size_t at = 0;
    const auto u32 = [&] {
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) v |= static_cast<uint32_t>(data::kRelief[at++]) << (8 * k);
        return v;
    };
    const auto i32 = [&] { return static_cast<int32_t>(u32()); };
    const auto i16 = [&] {
        const uint16_t v = static_cast<uint16_t>(data::kRelief[at] | (data::kRelief[at + 1] << 8));
        at += 2;
        return static_cast<int16_t>(v);
    };
    if (data::kReliefSize < 20 || data::kRelief[0] != 'I' || data::kRelief[1] != 'L' || data::kRelief[2] != 'R' || data::kRelief[3] != 'F') return;
    at = 4;
    if (u32() != 3) return;
    reliefNx_ = i32();
    reliefNy_ = i32();
    reliefCell_ = i32();
    const size_t n = static_cast<size_t>(reliefNx_) * reliefNy_;
    if (data::kReliefSize < at + n * 4 + 4) return;
    reliefDelta_.resize(n);
    reliefBasin_.resize(n);
    for (size_t i = 0; i < n; ++i) reliefDelta_[i] = i16();
    for (size_t i = 0; i < n; ++i) reliefBasin_[i] = i16();
    const uint32_t count = u32();
    for (uint32_t k = 0; k < count; ++k) {
        River st;
        const uint32_t points = u32();
        Fixed widest;
        for (uint32_t i = 0; i < points; ++i) {
            const int32_t xd = i32(), yd = i32();
            const int16_t surf = i16();
            const uint16_t hw = static_cast<uint16_t>(i16());
            st.points.push_back({Fixed::fromRatio(xd, 10), Fixed::fromRatio(yd, 10), Fixed::fromRatio(surf, 10)});
            st.widths.push_back(Fixed::fromRatio(hw, 100));
            widest = max(widest, st.widths.back());
        }
        st.halfWidth = widest;
        st.depth = widest / 3 + r(2, 5);
        for (size_t b = 0; b * River::kSpan + 1 < st.points.size(); ++b) {
            River::Box box{m(1000000), m(1000000), -m(1000000), -m(1000000)};
            const size_t end = std::min(st.points.size() - 1, (b + 1) * River::kSpan);
            for (size_t i = b * River::kSpan; i <= end; ++i) {
                box.minX = min(box.minX, st.points[i].x);
                box.minY = min(box.minY, st.points[i].y);
                box.maxX = max(box.maxX, st.points[i].x);
                box.maxY = max(box.maxY, st.points[i].y);
            }
            st.boxes.push_back(box);
        }
        if (st.points.size() >= 2) streams_.push_back(std::move(st));
    }
    // Bucket every box into the 256 m cells it may affect.
    bucketNx_ = static_cast<int32_t>(kWidth / kStreamBucket) + 1;
    bucketNy_ = static_cast<int32_t>(kHeight / kStreamBucket) + 1;
    streamBuckets_.assign(static_cast<size_t>(bucketNx_) * bucketNy_, {});
    for (uint32_t si = 0; si < streams_.size(); ++si) {
        const River& st = streams_[si];
        const Fixed reach = m(40) + st.halfWidth * 2 + m(1);
        for (uint32_t bi = 0; bi < st.boxes.size(); ++bi) {
            const River::Box& box = st.boxes[bi];
            const int64_t x0 = std::max<int64_t>(0, ((box.minX - reach) / kStreamBucket).floorToInt());
            const int64_t x1 = std::min<int64_t>(bucketNx_ - 1, ((box.maxX + reach) / kStreamBucket).floorToInt());
            const int64_t y0 = std::max<int64_t>(0, ((box.minY - reach) / kStreamBucket).floorToInt());
            const int64_t y1 = std::min<int64_t>(bucketNy_ - 1, ((box.maxY + reach) / kStreamBucket).floorToInt());
            for (int64_t by = y0; by <= y1; ++by)
                for (int64_t bx = x0; bx <= x1; ++bx) streamBuckets_[static_cast<size_t>(by * bucketNx_ + bx)].push_back({si, bi});
        }
    }
    baked_ = true;
}

}  // namespace illyria::sim
