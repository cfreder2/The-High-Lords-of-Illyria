#include "illyria/sim/continent.h"

#include <algorithm>
#include <cstdint>

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

Vec3 unitFlat(const Vec3& v) {
    const Fixed len = v.flat().length();
    return len > Fixed::zero() ? v.flat() / len : Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()};
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

Continent::Continent(uint64_t seed, Relief relief) : seed_(seed) {
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
    // Footbridges: narrow timber spans away from the roads, a way over for
    // infantry that no cart or column can use quickly.
    crossings_.push_back({Crossing::Kind::Footbridge, "Heron Footbridge", N(583, 702)});
    crossings_.push_back({Crossing::Kind::Footbridge, "Millrace Footbridge", N(568, 300)});
    crossings_.push_back({Crossing::Kind::Footbridge, "Hangman's Span", N(240, 451)});

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

    if (relief == Relief::Baked) loadBakedRelief();
    computeRiverSurfaces();
    for (const Area& lake : lakes_) {
        Fixed mountain;
        lakeSurfaces_.push_back(reliefHeight(lake.centre.x, lake.centre.y, mountain) - m(3));
    }
    placeBridges();
    placeCities();
    planCastles();
    placeSites();
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
        {
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
            case 2: {  // polygon of 5 to 8 sides, its corners pushed in and out
                const int n = city.capital ? 8 : 5 + static_cast<int>((roll(2) * 4).floorToInt());
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
        const Fixed wallH = city.capital ? m(11) : m(7) + roll(3) * 3, thick = city.capital ? m(4) : m(3);
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
            // An inner bailey: higher walls round the keep.
            Castle::Ring inner;
            inner.height = wallH * 4 / 3;
            inner.thick = thick;
            inner.gateWidth = m(6);
            for (int i = 0; i < 6; ++i) {
                const Fixed a = Fixed::fromRatio(2 * i + 1, 12) * 6283 / 1000 + Fixed::fromRatio(1571, 1000);
                inner.corners.push_back(world({cos(a) * R * r(9, 20), sin(a) * R * r(9, 20) - R * r(1, 5), Fixed::zero()}));
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
        // Towers on every corner, and a pair flanking each gate.
        for (const Castle::Ring& ring : k.rings) {
            const size_t n = ring.corners.size();
            for (size_t i = 0; i < n; ++i) {
                k.towers.push_back({ring.corners[i], ring.thick * 3 / 2, ring.height * 4 / 3, roll(40 + static_cast<int64_t>(i)) < r(2, 5)});
            }
            const Vec3 a = ring.corners[ring.gateEdge], b = ring.corners[(ring.gateEdge + 1) % n];
            const Vec3 mid = (a + b) * r(1, 2), along = unitFlat(b - a);
            for (int s = -1; s <= 1; s += 2) {
                k.towers.push_back({mid + along * ((ring.gateWidth / 2 + ring.thick) * s), ring.thick * 6 / 5, ring.height * 5 / 4, false});
            }
        }
        // The keep stands back from the gate; the courtyard lies between.
        k.keepAxis = toward;
        k.keepHalf = city.capital ? m(13) : m(7) + roll(4) * 3;
        k.keepHeight = city.capital ? m(28) : m(15) + roll(5) * 6;
        k.keep = world({(roll(6) - r(1, 2)) * R * r(2, 5), -R * (city.capital ? r(1, 5) : r(2, 5)), Fixed::zero()});
        k.courtyard = world({Fixed::zero(), R * r(3, 10), Fixed::zero()});
        k.courtyardRadius = R * r(7, 20);
        // Stairs up the inner face of the walls either side of the gate, and the back wall.
        {
            const Castle::Ring& ring = k.rings.front();
            const size_t n = ring.corners.size();
            const Fixed climb = ring.height - Fixed::fromRatio(13, 10);
            const Fixed run = climb * 4 / 3;
            for (size_t e : {(ring.gateEdge + n - 1) % n, (ring.gateEdge + 1) % n, (ring.gateEdge + n / 2) % n}) {
                const Vec3 a = ring.corners[e], b = ring.corners[(e + 1) % n];
                const Vec3 along = unitFlat(b - a);
                Vec3 in{-along.y, along.x, Fixed::zero()};
                if (in.dot(c - a) < Fixed::zero()) in = -in;
                const Fixed len = (b - a).flat().length();
                const Fixed halfWidth = Fixed::fromRatio(13, 10);
                const Vec3 start = a + along * (len / 2 - run / 2) + in * (ring.thick / 2 + halfWidth);
                Castle::Stair st{start, start + along * run, halfWidth};
                st.head.z = c.z + climb;
                k.stairs.push_back(st);
            }
        }
        // Houses in rows, clear of the walls, keep, courtyard and the street to the gate.
        const int rows = city.capital ? 16 : 11;
        const Fixed step = R * 2 / rows;
        for (int iy = 0; iy < rows; ++iy) {
            for (int ix = 0; ix < rows; ++ix) {
                const Fixed lx = -R + step * ix + step / 2 + (roll(1000 + iy * 32 + ix) - r(1, 2)) * step / 3;
                const Fixed ly = -R + step * iy + step / 2 + (roll(2000 + iy * 32 + ix) - r(1, 2)) * step / 3;
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
                    inside = (p - a).flat().dot(in) > ring.thick + m(9);
                }
                if (!inside) continue;
                if (k.rings.size() > 1) {
                    // Not across the inner bailey's walls either.
                    const Castle::Ring& in2 = k.rings[1];
                    Vec3 mid{};
                    for (const Vec3& q : in2.corners) mid += q;
                    mid = mid / Fixed::fromInt(static_cast<int64_t>(in2.corners.size()));
                    if ((p - mid).flat().length() < R * r(3, 5)) continue;
                }
                const bool turned = roll(3000 + iy * 32 + ix) < r(1, 2);
                const Fixed hx = m(2) + roll(4000 + iy * 32 + ix) * 2, hy = Fixed::fromRatio(17, 10) + roll(5000 + iy * 32 + ix) * Fixed::fromRatio(13, 10);
                k.houses.push_back({p, turned ? hy : hx, turned ? hx : hy, m(3) + roll(6000 + iy * 32 + ix) * 3});
            }
        }
    }
}

void Continent::placeBridges() {
    // Where roads cross creeks: small stone bridges, one per crossing.
    for (const Polyline& road : roads_) {
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
    for (Crossing& cr : crossings_) {
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
    const Fixed wobble = (noise::fbm(s1, x, y, 2400) - r(1, 2)) * 900 + (noise::fbm(s2, x, y, 500) - r(1, 2)) * 200;
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
        const Fixed ragged = d + (noise::fbm(sRidge ^ 0x7A6, x, y, 700) - r(1, 2)) * rg.line.halfWidth;
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
    Fixed z = m(10) + noise::fbm(sBase, x, y, 2400) * 60;
    // Gentle rolling relief everywhere, worn by water (no ground is dead flat).
    z += (noise::eroded(sRoll, x, y, 650, 6) - r(1, 3)) * 22;
    // Hill country comes and goes in patches kilometres across, rougher eastward.
    const Fixed hilly = noise::smoothstep(r(7, 20), r(7, 10), noise::fbm(sHillMask, x, y, 2600) + east / 4);
    z += noise::eroded(sHill, x, y, 1300, 7) * hilly * (m(55) + east * 60);
    z += (noise::value(sHill ^ 0x77, x, y, 140) - r(1, 2)) * 4;  // hummocks
    // Mountains: each range is a massif whose ragged edge fades into
    // foothills, carved by ridged multifractal crests and ravines.
    mountain = Fixed::zero();
    Fixed peak;
    const Fixed massif = massifMask(x, y, peak);
    if (massif > Fixed::zero()) {
        // Warp the domain a little so ridgelines wander rather than run on a grid.
        const Fixed wx = x + (noise::value(sRidge ^ 0x51, x, y, 1700) - r(1, 2)) * 600;
        const Fixed wy = y + (noise::value(sRidge ^ 0x52, x, y, 1700) - r(1, 2)) * 600;
        const Fixed crests = noise::ridgedMF(sRidge, wx, wy, 2200, 5);
        const Fixed worn = noise::eroded(sRidge ^ 0xE20D, x, y, 900, 5);
        const Fixed lift = massif * massif * (Fixed::fromInt(3) - massif * 2);  // smooth foothills
        mountain = peak * lift * (r(1, 5) + crests * r(7, 10) + worn * r(1, 10));
        // Crags, buttresses and gullies tens of metres across, rougher the higher it climbs.
        const Fixed crag = noise::ridgedMF(sRidge ^ 0xC4A6, wx, wy, 240, 3);
        mountain += lift * (crag - r(1, 3)) * (m(6) + mountain / 12);
    }
    for (const Vec3& p : passes_) {
        // A saddle with a ragged outline, not a round notch.
        const Fixed dx = x - p.x, dy = y - p.y;
        const Fixed rr = p.z * (r(4, 5) + noise::value(sRidge ^ 0x9A55, x, y, 300) * r(2, 5));
        mountain = mountain * (Fixed::one() - bump(sqrt(dx * dx + dy * dy) / rr) * r(7, 10));
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
        z = r(1, 2) + (z - r(1, 2)) * noise::smoothstep(Fixed::zero(), m(700), s.land) + s.land / 200;
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
            // The valley floor near the river, its walls blending back into
            // the land's own slopes (no crease where a limit cuts in).
            const Fixed floor = s.riverSurface + r(2, 5) + (s.riverDist - hw) * kValleySlope;
            if (z > floor) z = floor + (z - floor) * noise::smoothstep(kValleyReach * r(3, 10), kValleyReach, s.riverDist - hw);
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
    if ((mountain > m(70) && height > m(70)) || height > m(180)) return Ground::Rock;
    // Beaches: low ground along the sea is sand, wider where the shore is flat.
    if (height < m(5)) {
        const uint64_t sb = noise::nextSeed(seed_ ^ 0xBEAC);
        const Fixed reach = m(50) + noise::fbm(sb, x, y, 400) * 120 - height * 12;
        if (landDistance(x, y) < reach) return Ground::Sand;
    }
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
    if (x < m(250) || y < m(250) || x > m(kWidth - 250) || y > m(kHeight - 250)) return false;
    centre = sample(x, y);
    Fixed surface;
    if (centre.land < m(40) || centre.height < m(2) || centre.mountain > maxMountain) return false;
    if (waterSurfaceOf(centre, surface)) return false;
    if (centre.riverDist < centre.riverHalfWidth + radius + m(25)) return false;
    if (centre.streamDist < centre.streamHalfWidth + radius / 2 || centre.inBasin) return false;
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
