// The battle-scale relief (docs/MAP.md): what makes a kilometre of battlefield
// more than gently rolling grass. The design's hills and ranges are kilometres
// across; this adds what a soldier sees:
//   - knolls and mounds everywhere, crowded together in patches of broken ground;
//   - ridge country: patches of parallel ridges, one behind another;
//   - landmarks: gorges split through raised tablelands, and a few set pieces
//     made by hand (Church Hill, The Narrows, The Terraces).
// It is laid on once the towns, sites and bridges are placed and keeps clear of
// them (and of the water), so the strategic map stands where the design put it.
// Its own slopes stay under 1 in 1, and it eases off on the mountains; only
// the gorges' walls, the road's cut and a few crags are cliffs. Nothing is
// dug out: a gorge's floor and a road's bench are the old ground or above it,
// so wherever could be reached before still can.

#include <algorithm>

#include "continent_shapes.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/noise.h"

namespace illyria::sim {

using namespace continent_shapes;

namespace {

// Knolls, hillocks and tors: at most one in each cell this wide (its centre anywhere in it).
constexpr int64_t kKnollCell = 95, kHillockCell = 240, kTorCell = 150;
// Ridge country: at most one patch in each cell this wide.
constexpr int64_t kRidgeCell = 1250;
// Gorges found by search: this many at most, this far apart (beyond their own size).
constexpr size_t kMaxGorges = 12;
constexpr int64_t kGorgeSpacing = 500;
// The church hill's road: its grade, and its half width.
constexpr Fixed kHillRoadGrade = Fixed::fromRatio(3, 40);
constexpr Fixed kHillRoadHalf = Fixed::fromRatio(5, 2);

Vec3 rotate(const Vec3& v, Fixed c, Fixed s) { return {v.x * c - v.y * s, v.x * s + v.y * c, Fixed::zero()}; }

// Normalised elliptical distance: < 1 inside.
Fixed ellipseQ(const Continent::Area& a, Fixed x, Fixed y) {
    const Fixed dx = (x - a.centre.x) / a.rx, dy = (y - a.centre.y) / a.ry;
    return sqrt(dx * dx + dy * dy);
}

uint64_t landmarkSeed(uint64_t seed, const Continent::Landmark& lm) {
    return noise::nextSeed(seed ^ static_cast<uint64_t>(lm.centre.x.raw * 31 + lm.centre.y.raw));
}

// A hill's outline wanders in and out round it: the distance from its centre
// scaled by noise read on a circle (so the hill stays one piece).
Fixed wanderingRadius(uint64_t s, const Vec3& centre, Fixed dx, Fixed dy, Fixed dist, Fixed amount) {
    if (dist < Fixed::one()) return dist;
    const Fixed ux = dx / dist, uy = dy / dist;
    const Fixed w = noise::value(s, centre.x + ux * 100, centre.y + uy * 100, 60) - r(1, 2);
    return dist / (Fixed::one() + w * amount);
}

}  // namespace

void Continent::dress() {
    naturalLakes_ = lakes_.size();
    // The set pieces, placed by hand on open ground with room round them.
    addChurchHill("Church Hill", N(511, 503));         // above the Heartmere, below Kingshold
    addGorgeOnRoad("The Narrows", N(188, 226), m(200));  // the south road through the downs by Wardenhold
    addTerraces("The Terraces", N(532, 385));          // the fields above Hallowmere
    findGorges();
    bucketRoads();  // the church hill's road joins the others
    landmarkBuckets_.assign(static_cast<size_t>(bucketNx_) * bucketNy_, {});
    for (uint32_t li = 0; li < landmarks_.size(); ++li) {
        const Landmark& lm = landmarks_[li];
        const int64_t x0 = std::max<int64_t>(0, ((lm.centre.x - lm.radius) / kStreamBucket).floorToInt());
        const int64_t x1 = std::min<int64_t>(bucketNx_ - 1, ((lm.centre.x + lm.radius) / kStreamBucket).floorToInt());
        const int64_t y0 = std::max<int64_t>(0, ((lm.centre.y - lm.radius) / kStreamBucket).floorToInt());
        const int64_t y1 = std::min<int64_t>(bucketNy_ - 1, ((lm.centre.y + lm.radius) / kStreamBucket).floorToInt());
        for (int64_t by = y0; by <= y1; ++by)
            for (int64_t bx = x0; bx <= x1; ++bx) landmarkBuckets_[static_cast<size_t>(by * bucketNx_ + bx)].push_back(li);
    }
    buildClearGrid();
    dressed_ = true;
}

const Continent::Landmark* Continent::landmarkAt(Fixed x, Fixed y) const {
    if (landmarkBuckets_.empty()) return nullptr;
    const int64_t bx = clamp(x / kStreamBucket, Fixed::zero(), Fixed::fromInt(bucketNx_ - 1)).floorToInt();
    const int64_t by = clamp(y / kStreamBucket, Fixed::zero(), Fixed::fromInt(bucketNy_ - 1)).floorToInt();
    for (const uint32_t li : landmarkBuckets_[static_cast<size_t>(by * bucketNx_ + bx)]) {
        const Landmark& lm = landmarks_[li];
        const Fixed dx = x - lm.centre.x, dy = y - lm.centre.y;
        if (dx * dx + dy * dy < lm.radius * lm.radius) return &lm;
    }
    return nullptr;
}

// --- The set pieces ----------------------------------------------------------

void Continent::addChurchHill(const std::string& name, Vec3 at) {
    // A hill 330 m across standing 40 m over the land, steep in its upper
    // part; its top is level ground with a church and a pond. A road leaves
    // the nearest highway, reaches the hill's foot and climbs round it at
    // 1 in 13, cut into the slope as a level bench, to the top.
    Landmark lm;
    lm.kind = Landmark::Kind::ChurchHill;
    lm.name = name;
    const Sample c0 = sample(at.x, at.y);
    lm.centre = {at.x, at.y, c0.height};
    lm.rise = m(40);
    lm.summit = m(40);
    lm.radius = m(190);
    lm.top = c0.height + lm.rise;
    const Fixed foot = m(170);
    const auto hillAt = [&](const Vec3& p) { return sample(p.x, p.y).height + landmarkRise(lm, p.x, p.y); };

    // Where the road leaves the highway: the nearest point of any road.
    Vec3 junction = at;
    {
        Fixed best = m(100000);
        for (const Polyline& rd : roads_) {
            size_t seg = 0;
            Fixed t;
            const Fixed d = polylineDist(rd.points, at.x, at.y, m(1500), seg, t);
            if (d >= best || d >= m(1500)) continue;
            best = d;
            junction = rd.points[seg] + (rd.points[seg + 1] - rd.points[seg]) * t;
        }
    }
    Vec3 dir = unitFlat(junction - at);
    Polyline road{{junction, (junction + at + dir * foot) * r(1, 2), at + dir * foot}, kHillRoadHalf};
    // The climb: each step of 5 m rises a thirteenth of that if the slope
    // allows, turning round the hill (anticlockwise) as tightly as it must.
    const Fixed ds = m(5);
    Fixed radius = foot, z = hillAt(at + dir * foot);
    lm.line.push_back({at.x + dir.x * foot, at.y + dir.y * foot, z});
    for (int step = 0; step < 400 && radius > lm.summit - m(2) && z < lm.top - r(1, 2); ++step) {
        const auto pointFor = [&](Fixed dr) {
            const Fixed turn = sqrt(max(Fixed::zero(), ds * ds - dr * dr)) / radius;
            return at + rotate(dir, cos(turn), sin(turn)) * (radius + dr);
        };
        const Fixed want = z + ds * kHillRoadGrade;
        Fixed lo = -ds * r(19, 20), hi = ds / 2, dr;
        Fixed zNext = want;
        if (hillAt(pointFor(lo)) < want) {
            dr = lo;  // too gentle to climb at the grade even heading straight in
            zNext = max(z, hillAt(pointFor(lo)));
        } else if (hillAt(pointFor(hi)) > want) {
            dr = hi;  // the road cuts in a little deeper
        } else {
            for (int k = 0; k < 14; ++k) {
                const Fixed mid = (lo + hi) / 2;
                (hillAt(pointFor(mid)) > want ? lo : hi) = mid;
            }
            dr = (lo + hi) / 2;
        }
        const Vec3 p = pointFor(dr);
        dir = unitFlat(p - at);
        radius = (p - at).flat().length();
        z = min(zNext, lm.top);
        lm.line.push_back({p.x, p.y, z});
    }
    // On over the edge onto the level top.
    const Vec3 end = at + dir * (lm.summit - m(12));
    lm.line.push_back({end.x, end.y, lm.top});
    // Each step either heads in or turns round, so the course zigzags a
    // little: smooth it into easy curves (the bench cuts and fills the odd metre).
    for (int pass = 0; pass < 6; ++pass) {
        std::vector<Vec3> smooth = lm.line;
        for (size_t i = 1; i + 1 < lm.line.size(); ++i) smooth[i] = (lm.line[i - 1] + lm.line[i] * m(2) + lm.line[i + 1]) / m(4);
        lm.line = std::move(smooth);
    }
    for (size_t i = 1; i < lm.line.size(); ++i) lm.line[i].z = max(lm.line[i].z, lm.line[i - 1].z);
    for (size_t i = 1; i < lm.line.size(); ++i) road.points.push_back({lm.line[i].x, lm.line[i].y, Fixed::zero()});
    road.widths.assign(road.points.size(), kHillRoadHalf);
    roads_.push_back(road);

    // The church, its tower, a few graves and the pond, turned whichever
    // quarter keeps them farthest from where the road comes up.
    struct Piece {
        Obstacle::Kind kind;
        Fixed x, y, z, halfX, halfY, radius, height;
        uint8_t variant;
    };
    const Piece pieces[] = {
        {Obstacle::Kind::House, m(6), -m(3), m(0), r(17, 2), r(18, 5), m(0), m(9), 0},            // the nave
        {Obstacle::Kind::House, -r(23, 5), -m(3), m(0), r(12, 5), r(12, 5), m(0), m(15), 3},      // the tower, stone
        {Obstacle::Kind::House, -r(23, 5), -m(3), m(15), r(27, 10), r(27, 10), m(0), r(9, 2), 1},  // its roof
        {Obstacle::Kind::Pillar, m(3), -m(11), m(0), m(0), m(0), r(3, 10), r(9, 10), 0},           // graves
        {Obstacle::Kind::Pillar, r(11, 2), -m(12), m(0), m(0), m(0), r(3, 10), r(9, 10), 1},
        {Obstacle::Kind::Pillar, m(8), -m(11), m(0), m(0), m(0), r(3, 10), r(9, 10), 2},
        {Obstacle::Kind::Pillar, r(21, 2), -m(12), m(0), m(0), m(0), r(3, 10), r(9, 10), 0},
        {Obstacle::Kind::Pillar, m(13), -m(11), m(0), m(0), m(0), r(3, 10), r(9, 10), 1},
    };
    const Vec3 pond{-m(14), m(12), Fixed::zero()};
    const Fixed pondRx = m(9), pondRy = m(7);
    const auto turned = [](Fixed x, Fixed y, int k) {
        switch (k) {
            case 1: return Vec3{-y, x, Fixed::zero()};
            case 2: return Vec3{-x, -y, Fixed::zero()};
            case 3: return Vec3{y, -x, Fixed::zero()};
            default: return Vec3{x, y, Fixed::zero()};
        }
    };
    int quarter = 0;
    Fixed roomiest = -m(1000);
    for (int k = 0; k < 4; ++k) {
        Fixed room = m(1000);
        for (const Vec3& q : lm.line) {
            const Vec3 local = q - Vec3{at.x, at.y, q.z};
            if (local.flat().length() > lm.summit + m(10)) continue;
            room = min(room, (local.flat() - turned(pond.x, pond.y, k)).length() - pondRx);
            for (const Piece& pc : pieces) room = min(room, (local.flat() - turned(pc.x, pc.y, k)).length() - max(pc.halfX, pc.halfY));
        }
        if (room > roomiest) {
            roomiest = room;
            quarter = k;
        }
    }
    for (const Piece& pc : pieces) {
        Obstacle o;
        o.kind = pc.kind;
        const Vec3 p = turned(pc.x, pc.y, quarter);
        o.pos = {at.x + p.x, at.y + p.y, lm.top + pc.z};
        o.halfX = quarter % 2 ? pc.halfY : pc.halfX;
        o.halfY = quarter % 2 ? pc.halfX : pc.halfY;
        o.radius = pc.radius;
        o.height = pc.height;
        o.variant = pc.variant;
        lm.buildings.push_back(o);
    }
    // The pond is a lake like the others, its surface a little below the top.
    const Vec3 pp = turned(pond.x, pond.y, quarter);
    lakes_.push_back({name + " Pond", {at.x + pp.x, at.y + pp.y, Fixed::zero()}, quarter % 2 ? pondRy : pondRx, quarter % 2 ? pondRx : pondRy});
    lakeSurfaces_.push_back(lm.top - r(2, 5));
    landmarks_.push_back(std::move(lm));
}

void Continent::addGorgeOnRoad(const std::string& name, Vec3 at, Fixed halfLength) {
    // A gorge with the road along its floor: the road's own course from a
    // little before the tableland to a little past it.
    size_t ri = 0, seg = 0;
    Fixed t, best = m(100000);
    for (size_t i = 0; i < roads_.size(); ++i) {
        size_t s = 0;
        Fixed tt;
        const Fixed d = polylineDist(roads_[i].points, at.x, at.y, m(1000), s, tt);
        if (d < best) {
            best = d;
            ri = i;
            seg = s;
            t = tt;
        }
    }
    if (best >= m(1000)) return;
    const std::vector<Vec3>& pts = roads_[ri].points;
    const Vec3 mid = pts[seg] + (pts[seg + 1] - pts[seg]) * t;
    Landmark lm;
    lm.kind = Landmark::Kind::Gorge;
    lm.name = name;
    lm.centre = {mid.x, mid.y, sample(mid.x, mid.y).height};
    lm.halfLength = halfLength;
    lm.halfWidth = m(140);
    lm.rise = m(28);
    lm.floor = m(15);
    lm.radius = halfLength + m(40);
    // The road's points within reach, in order.
    const Fixed reach = halfLength * r(7, 5);
    for (const Vec3& p : pts)
        if ((p - mid).flat().length() < reach) lm.line.push_back({p.x, p.y, Fixed::zero()});
    if (lm.line.size() < 2) return;
    lm.axis = unitFlat(lm.line.back() - lm.line.front());
    landmarks_.push_back(std::move(lm));
}

void Continent::addTerraces(const std::string& name, Vec3 at) {
    // A low hill stepped into five level fields, each about 3 m above the
    // next, their faces steep but walkable.
    Landmark lm;
    lm.kind = Landmark::Kind::Terraces;
    lm.name = name;
    lm.centre = {at.x, at.y, sample(at.x, at.y).height};
    lm.steps = 5;
    lm.rise = m(16);
    lm.summit = m(26);
    lm.radius = m(125);
    lm.top = lm.centre.z + lm.rise;
    landmarks_.push_back(std::move(lm));
}

// --- Gorges found by search -------------------------------------------------

bool Continent::roomForLandmark(Vec3 at, Fixed radius, bool roadsAllowed) const {
    // Clear of the towns' pads, the sites, the crossings and the lakes.
    if (at.x < m(700) || at.y < m(700) || at.x > m(kWidth - 700) || at.y > m(kHeight - 700)) return false;
    const auto near = [&](const Vec3& p, Fixed gap) { return (p - at).flat().lengthSq() < gap * gap; };
    for (const City& c : cities_)
        if (near(c.pos, c.radius * r(11, 5) + radius + m(30))) return false;
    for (const Site& s : sites_)
        if (near(s.pos, radius + m(70))) return false;
    for (const Crossing& c : crossings_)
        if (near(c.pos, radius + m(120))) return false;
    for (const Area& lake : lakes_)
        if (ellipseQ(lake, at.x, at.y) < r(8, 5)) return false;
    // The ground itself: dry, clear of rivers, creeks, pools and roads, not
    // swamp, and no steeper than the foothills.
    const int64_t reach = radius.floorToInt();
    Fixed lowest = m(100000), highest = -m(100000);
    for (int64_t dy = -reach; dy <= reach; dy += 40) {
        for (int64_t dx = -reach; dx <= reach; dx += 40) {
            if (dx * dx + dy * dy > reach * reach) continue;
            const Fixed x = at.x + m(dx), y = at.y + m(dy);
            const Sample s = sample(x, y);
            Fixed surface;
            lowest = min(lowest, s.height);
            highest = max(highest, s.height);
            if (highest - lowest > m(45)) return false;
            if (s.land < m(150) || waterSurfaceOf(s, surface) || s.inBasin || s.mountain > m(110)) return false;
            if (s.riverDist < s.riverHalfWidth + m(220) || s.streamDist < s.streamHalfWidth + m(30)) return false;
            if (!roadsAllowed && s.roadDist < m(60)) return false;
            if (landGround(x, y, s.height, s.mountain, s.roadDist) == Ground::Swamp) return false;
        }
    }
    return true;
}

void Continent::findGorges() {
    // A gorge wherever there is room for one: a tableland a few hundred
    // metres long with a cleft along it that wanders a little, open at both
    // ends. The land is crowded with towns, so every spot every 100 m is
    // tried, in an order drawn by lot, and the gorges kept well apart.
    const uint64_t sg = noise::nextSeed(seed_ ^ 0x6046E);
    struct Spot {
        int64_t i, j;
        Fixed lot;
    };
    std::vector<Spot> spots;
    constexpr int64_t kStep = 100;
    for (int64_t j = 7; j * kStep < kHeight - 700; ++j)
        for (int64_t i = 7; i * kStep < kWidth - 700; ++i) spots.push_back({i, j, noise::lattice(sg, i, j)});
    std::stable_sort(spots.begin(), spots.end(), [](const Spot& a, const Spot& b) { return a.lot < b.lot; });
    size_t found = 0;
    for (const Spot& sp : spots) {
        if (found >= kMaxGorges) break;
        const int64_t i = sp.i, j = sp.j;
        const auto roll = [&](int64_t k) { return noise::lattice(sg ^ static_cast<uint64_t>(k * 0x9E37), i, j); };
        Landmark lm;
        lm.kind = Landmark::Kind::Gorge;
        const Fixed x = m(i * kStep), y = m(j * kStep);
        lm.halfLength = m(140) + roll(3) * 60;
        lm.halfWidth = m(100) + roll(4) * 35;
        lm.rise = m(20) + roll(5) * 12;
        lm.floor = m(11) + roll(6) * 4;
        lm.radius = lm.halfLength + m(30);
        bool apart = true;
        for (const Landmark& o : landmarks_) {
            const Fixed gap = lm.radius + o.radius + m(kGorgeSpacing);
            apart = apart && (o.centre - Vec3{x, y, Fixed::zero()}).flat().lengthSq() > gap * gap;
        }
        if (!apart || !roomForLandmark({x, y, Fixed::zero()}, lm.radius, false)) continue;
        lm.centre = {x, y, sample(x, y).height};
        lm.axis = {cos(roll(7) * kPi), sin(roll(7) * kPi), Fixed::zero()};
        const Vec3 side{-lm.axis.y, lm.axis.x, Fixed::zero()};
        // The floor: across the tableland and out past both ends, every 10 m.
        const int64_t half = (lm.halfLength * r(27, 20)).floorToInt();
        bool gentle = true;
        Fixed last;
        for (int64_t a = -half; a <= half; a += 10) {
            const Fixed wander = (noise::value(sg ^ 0x11, m(a), m((i + j * 1000) * 977), 140) - r(1, 2)) * lm.halfWidth * r(3, 5);
            const Vec3 p = lm.centre + lm.axis * m(a) + side * wander;
            lm.line.push_back({p.x, p.y, Fixed::zero()});
            // Its floor must be walkable: the old ground along it no steeper than 1 in 6.
            const Fixed h = sample(p.x, p.y).height;
            if (a > -half && abs(h - last) > r(10, 6)) gentle = false;
            last = h;
        }
        if (!gentle) continue;
        landmarks_.push_back(std::move(lm));
        ++found;
    }
}

// --- Where the relief may rise ------------------------------------------------

void Continent::buildClearGrid() {
    clearNx_ = static_cast<int32_t>(kWidth / kClearCell) + 2;
    clearNy_ = static_cast<int32_t>(kHeight / kClearCell) + 2;
    clear_.assign(static_cast<size_t>(clearNx_) * clearNy_, 255);
    const auto cellRange = [&](Fixed lo, Fixed hi, int32_t n, int64_t& a, int64_t& b) {
        a = std::max<int64_t>(0, (lo / kClearCell).floorToInt());
        b = std::min<int64_t>(n - 1, (hi / kClearCell).floorToInt() + 1);
    };
    const auto lower = [&](int64_t ix, int64_t iy, Fixed v) {
        uint8_t& c = clear_[static_cast<size_t>(iy * clearNx_ + ix)];
        c = std::min<uint8_t>(c, static_cast<uint8_t>((clamp(v, Fixed::zero(), Fixed::one()) * 255).floorToInt()));
    };
    // Nothing within `inner` of a point, all of it from `outer` on.
    const auto ring = [&](const Vec3& p, Fixed inner, Fixed outer) {
        int64_t x0, x1, y0, y1;
        cellRange(p.x - outer, p.x + outer, clearNx_, x0, x1);
        cellRange(p.y - outer, p.y + outer, clearNy_, y0, y1);
        for (int64_t iy = y0; iy <= y1; ++iy)
            for (int64_t ix = x0; ix <= x1; ++ix) {
                const Fixed dx = m(ix * kClearCell) - p.x, dy = m(iy * kClearCell) - p.y;
                const Fixed d2 = dx * dx + dy * dy;
                if (d2 >= outer * outer) continue;
                lower(ix, iy, noise::smoothstep(inner, outer, sqrt(d2)));
            }
    };
    for (const City& c : cities_) ring(c.pos, c.radius * 2, c.radius * 2 + m(70));
    for (const Site& s : sites_) ring(s.pos, m(30), m(100));
    for (const Crossing& c : crossings_) ring(c.pos, m(50), m(150));
    for (const Landmark& lm : landmarks_) ring(lm.centre, lm.radius, lm.radius + m(70));
    // The pools the bake found, and the creeks: their banks stay as they were.
    if (baked_ && !reliefBasin_.empty()) {
        for (int32_t iy = 0; iy < reliefNy_; ++iy)
            for (int32_t ix = 0; ix < reliefNx_; ++ix)
                if (reliefBasin_[static_cast<size_t>(iy * reliefNx_ + ix)] != INT16_MIN)
                    ring({m(ix * reliefCell_), m(iy * reliefCell_), Fixed::zero()}, m(20), m(64));
    }
    for (const River& st : streams_) {
        for (size_t i = 0; i + 1 < st.points.size(); ++i) {
            const Vec3 a = st.points[i], b = st.points[i + 1];
            const Fixed hw = max(st.widths[i], st.widths[i + 1]), outer = hw + m(56);
            int64_t x0, x1, y0, y1;
            cellRange(min(a.x, b.x) - outer, max(a.x, b.x) + outer, clearNx_, x0, x1);
            cellRange(min(a.y, b.y) - outer, max(a.y, b.y) + outer, clearNy_, y0, y1);
            for (int64_t iy = y0; iy <= y1; ++iy)
                for (int64_t ix = x0; ix <= x1; ++ix) {
                    Fixed t;
                    const Fixed d2 = segDistSq(m(ix * kClearCell), m(iy * kClearCell), a, b, t);
                    if (d2 >= outer * outer) continue;
                    lower(ix, iy, noise::smoothstep(hw + m(8), outer, sqrt(d2)));
                }
        }
    }
    // The lakes' shores, and the swamps (a few low hummocks, no more).
    const auto overArea = [&](const Area& a, Fixed reach, auto&& value) {
        int64_t x0, x1, y0, y1;
        cellRange(a.centre.x - a.rx * reach, a.centre.x + a.rx * reach, clearNx_, x0, x1);
        cellRange(a.centre.y - a.ry * reach, a.centre.y + a.ry * reach, clearNy_, y0, y1);
        for (int64_t iy = y0; iy <= y1; ++iy)
            for (int64_t ix = x0; ix <= x1; ++ix) lower(ix, iy, value(ellipseQ(a, m(ix * kClearCell), m(iy * kClearCell))));
    };
    for (size_t i = 0; i < naturalLakes(); ++i) overArea(lakes_[i], m(2), [](Fixed q) { return noise::smoothstep(r(13, 10), m(2), q); });
    for (const Area& sw : swamps_) overArea(sw, r(3, 2), [](Fixed q) { return r(7, 20) + r(13, 20) * noise::smoothstep(r(9, 10), r(3, 2), q); });
}

// --- The relief ---------------------------------------------------------------

Fixed Continent::reliefAt(Fixed x, Fixed y, const Sample& s) const {
    // How freely it may rise here: clear of everything that wants level ground.
    const Fixed fx = clamp(x / kClearCell, Fixed::zero(), Fixed::fromInt(clearNx_ - 2));
    const Fixed fy = clamp(y / kClearCell, Fixed::zero(), Fixed::fromInt(clearNy_ - 2));
    const int64_t ix = fx.floorToInt(), iy = fy.floorToInt();
    const Fixed tx = fx - Fixed::fromInt(ix), ty = fy - Fixed::fromInt(iy);
    const auto at = [&](int64_t i, int64_t j) { return Fixed::fromInt(clear_[static_cast<size_t>(j * clearNx_ + i)]); };
    const Fixed top = at(ix, iy) + (at(ix + 1, iy) - at(ix, iy)) * tx, bottom = at(ix, iy + 1) + (at(ix + 1, iy + 1) - at(ix, iy + 1)) * tx;
    Fixed mask = (top + (bottom - top) * ty) / 255;
    if (mask <= Fixed::zero()) return Fixed::zero();
    // On land, off the mountains (which have crags enough), out of the river valleys.
    mask = mask * noise::smoothstep(m(30), m(200), s.land) * (Fixed::one() - noise::smoothstep(m(40), m(110), s.mountain)) *
           noise::smoothstep(s.riverHalfWidth + m(40), s.riverHalfWidth + m(180), s.riverDist);
    if (mask <= Fixed::zero()) return Fixed::zero();

    // Mounds of one size, at most one in each cell `cell` wide (its centre
    // anywhere in it, never longer than the cell): rounded, some drawn out
    // long, never steeper than 1.5 x `steepest` (so under 1 in 1). Scattered
    // thinly over open country and crowded together in patches of broken ground.
    const auto mounds = [&](uint64_t sk, int64_t cell, Fixed sparse, Fixed crowded, Fixed widest, Fixed steepest) {
        Fixed top;
        const int64_t cx = (x / cell).floorToInt(), cy = (y / cell).floorToInt();
        for (int64_t j = cy - 1; j <= cy + 1; ++j) {
            for (int64_t i = cx - 1; i <= cx + 1; ++i) {
                const Fixed kx = m(i * cell) + noise::lattice(sk ^ 1, i, j) * cell;
                const Fixed ky = m(j * cell) + noise::lattice(sk ^ 2, i, j) * cell;
                const Fixed width = widest * (r(9, 20) + noise::lattice(sk ^ 3, i, j) * r(11, 20));  // the short half axis
                const Fixed length = min(width * (Fixed::one() + noise::lattice(sk ^ 4, i, j) * r(9, 10)), m(cell));
                const Fixed dx = x - kx, dy = y - ky;
                if (abs(dx) >= length || abs(dy) >= length) continue;
                // Whether there is one here (from where it stands, so it is all or nothing).
                const Fixed chance = sparse + (crowded - sparse) * noise::smoothstep(r(3, 10), r(3, 5), noise::value(noise::nextSeed(seed_ ^ 0xB40C), kx, ky, 1300));
                if (noise::lattice(sk, i, j) >= chance) continue;
                const Fixed angle = noise::lattice(sk ^ 5, i, j) * kPi, c = cos(angle), sn = sin(angle);
                const Fixed u = (dx * c + dy * sn) / length, v = (dy * c - dx * sn) / width;
                // A ragged outline, not a drawn ellipse.
                const Fixed q2 = (u * u + v * v) * (r(22, 25) + noise::value(sk ^ 7, x, y, 30) * r(6, 25));
                if (q2 >= Fixed::one()) continue;
                const Fixed height = width * steepest * (r(3, 8) + noise::lattice(sk ^ 6, i, j) * r(5, 8));
                top = max(top, height * (Fixed::one() - q2) * (Fixed::one() - q2));
            }
        }
        return top;
    };
    // Knolls 40 to 100 m across, hillocks twice that, and here and there a
    // tor: a steep little knob of rock 20 to 45 m across.
    const Fixed knoll = max(max(mounds(noise::nextSeed(seed_ ^ 0x4E011), kKnollCell, r(9, 20), r(19, 20), m(50), r(9, 25)),
                                mounds(noise::nextSeed(seed_ ^ 0x4111C), kHillockCell, r(2, 5), r(17, 20), m(110), r(8, 25))),
                            mounds(noise::nextSeed(seed_ ^ 0x7012), kTorCell, r(1, 20), r(3, 10), m(22), r(9, 20)));
    // Ridge country: patches a kilometre or so long where the land runs in
    // parallel ridges 140 to 210 m apart, one behind another, their crests
    // rising and falling (saddles to cross by), their lines wandering.
    Fixed ridge;
    {
        const uint64_t sr = noise::nextSeed(seed_ ^ 0x21D6E);
        const int64_t cx = (x / kRidgeCell).floorToInt(), cy = (y / kRidgeCell).floorToInt();
        for (int64_t j = cy - 1; j <= cy + 1; ++j) {
            for (int64_t i = cx - 1; i <= cx + 1; ++i) {
                if (noise::lattice(sr, i, j) >= r(3, 5)) continue;
                const Fixed px = m(i * kRidgeCell + kRidgeCell / 2) + (noise::lattice(sr ^ 1, i, j) - r(1, 2)) * (kRidgeCell / 2);
                const Fixed py = m(j * kRidgeCell + kRidgeCell / 2) + (noise::lattice(sr ^ 2, i, j) - r(1, 2)) * (kRidgeCell / 2);
                const Fixed along = m(420) + noise::lattice(sr ^ 3, i, j) * 300, across = m(300) + noise::lattice(sr ^ 4, i, j) * 180;
                const Fixed dx = x - px, dy = y - py;
                if (abs(dx) >= along || abs(dy) >= along) continue;
                const Fixed angle = noise::lattice(sr ^ 5, i, j) * kPi, c = cos(angle), sn = sin(angle);
                const Fixed v = dx * c + dy * sn, u = dy * c - dx * sn;  // v along the ridges, u across them
                const Fixed q = sqrt((v / along) * (v / along) + (u / across) * (u / across));
                if (q >= Fixed::one()) continue;
                const Fixed period = m(140) + noise::lattice(sr ^ 6, i, j) * 70;
                const Fixed height = period * (r(2, 25) + noise::lattice(sr ^ 7, i, j) * r(1, 20));
                const Fixed wander = (noise::value(sr ^ 0x77, x, y, 420) - r(1, 2)) * period * r(9, 20);
                const Fixed phase = (u + wander) / period + noise::lattice(sr ^ 8, i, j);
                const Fixed f = phase - Fixed::fromInt(phase.floorToInt());
                const Fixed crest = noise::smoothstep(r(1, 5), Fixed::one(), Fixed::one() - abs(f * 2 - Fixed::one()));
                const Fixed rise = r(2, 5) + noise::value(sr ^ 0x5AD, x, y, 560) * r(3, 5);
                ridge = max(ridge, height * crest * rise * (Fixed::one() - noise::smoothstep(r(11, 20), Fixed::one(), q)));
            }
        }
    }
    // Roads keep a little of it: they climb over the lesser rises in a shallow cutting.
    const Fixed road = r(3, 10) + r(7, 10) * noise::smoothstep(m(4), m(50), s.roadDist);
    return max(knoll, ridge) * mask * road;
}

Fixed Continent::landmarkRise(const Landmark& lm, Fixed x, Fixed y) const {
    const Fixed dx = x - lm.centre.x, dy = y - lm.centre.y;
    if (abs(dx) >= lm.radius || abs(dy) >= lm.radius) return Fixed::zero();
    const uint64_t sl = landmarkSeed(seed_, lm);
    switch (lm.kind) {
        case Landmark::Kind::ChurchHill: {
            // Steep above, a long gentle skirt below.
            const Fixed d = sqrt(dx * dx + dy * dy);
            if (d >= lm.radius) return Fixed::zero();
            const Fixed re = wanderingRadius(sl, lm.centre, dx, dy, d, r(1, 4));
            const Fixed upper = Fixed::one() - noise::smoothstep(lm.summit, m(100), re);
            const Fixed lower = Fixed::one() - noise::smoothstep(lm.summit + m(25), m(165), re);
            return lm.rise * (upper * r(17, 25) + lower * r(8, 25));
        }
        case Landmark::Kind::Terraces: {
            const Fixed d = sqrt(dx * dx + dy * dy);
            if (d >= lm.radius) return Fixed::zero();
            const Fixed re = wanderingRadius(sl, lm.centre, dx, dy, d, r(6, 25));
            const Fixed outer = lm.radius - m(25), band = (outer - lm.summit) / (lm.steps - 1), riser = lm.rise / lm.steps;
            Fixed z;
            for (int32_t k = 0; k < lm.steps; ++k) {
                const Fixed edge = lm.summit + band * k;
                z += riser * (Fixed::one() - noise::smoothstep(edge - m(3), edge + m(3), re));
            }
            return z;
        }
        case Landmark::Kind::Gorge: {
            const Fixed u = dx * lm.axis.x + dy * lm.axis.y, v = dy * lm.axis.x - dx * lm.axis.y;
            Fixed q = sqrt((u / lm.halfLength) * (u / lm.halfLength) + (v / lm.halfWidth) * (v / lm.halfWidth));
            q = q * (Fixed::one() + (noise::value(sl, x, y, 90) - r(1, 2)) * r(7, 20));  // a ragged edge
            if (q >= Fixed::one()) return Fixed::zero();
            const Fixed mesa = Fixed::one() - noise::smoothstep(r(1, 2), Fixed::one(), q);
            size_t seg = 0;
            Fixed t;
            const Fixed d = polylineDist(lm.line, x, y, lm.floor + m(40), seg, t);
            const Fixed floor = lm.floor * (r(17, 20) + noise::value(sl ^ 3, x, y, 60) * r(3, 10));
            // Sheer walls from the floor, a ledge, and where the tableland is
            // thickest a second cliff set back behind it.
            const Fixed wall = noise::smoothstep(floor, floor + m(8), d);
            const Fixed upper = (Fixed::one() - noise::smoothstep(r(1, 5), r(9, 20), q)) * noise::smoothstep(floor + m(18), floor + m(27), d);
            const Fixed rough = r(9, 10) + noise::fbm(sl ^ 5, x, y, 40) * r(1, 5);
            return lm.rise * (mesa * wall * r(13, 20) + upper * r(7, 20)) * rough;
        }
    }
    return Fixed::zero();
}

void Continent::shapeLandmarks(Fixed& z, Fixed x, Fixed y) const {
    if (landmarkBuckets_.empty()) return;
    const int64_t bx = clamp(x / kStreamBucket, Fixed::zero(), Fixed::fromInt(bucketNx_ - 1)).floorToInt();
    const int64_t by = clamp(y / kStreamBucket, Fixed::zero(), Fixed::fromInt(bucketNy_ - 1)).floorToInt();
    for (const uint32_t li : landmarkBuckets_[static_cast<size_t>(by * bucketNx_ + bx)]) {
        const Landmark& lm = landmarks_[li];
        const Fixed rise = landmarkRise(lm, x, y);
        if (lm.kind != Landmark::Kind::ChurchHill) {
            z += rise;
            continue;
        }
        const Fixed dx = x - lm.centre.x, dy = y - lm.centre.y;
        if (abs(dx) >= lm.radius || abs(dy) >= lm.radius) continue;
        Fixed h = z + rise;
        // The level top.
        const Fixed d = sqrt(dx * dx + dy * dy);
        const Fixed re = wanderingRadius(landmarkSeed(seed_, lm), lm.centre, dx, dy, d, r(1, 4));
        h = h + (lm.top - h) * (Fixed::one() - noise::smoothstep(lm.summit - m(10), lm.summit + m(2), re));
        // The road's bench: level across, cut into the slope above it and
        // built up below.
        size_t seg = 0;
        Fixed t;
        const Fixed reach = kHillRoadHalf + m(6);
        const Fixed off = polylineDist(lm.line, x, y, reach, seg, t);
        if (off < reach) {
            const Fixed surface = lm.line[seg].z + (lm.line[seg + 1].z - lm.line[seg].z) * t;
            h = h + (surface - h) * (Fixed::one() - noise::smoothstep(kHillRoadHalf + Fixed::one(), reach, off));
        }
        z = h;
    }
}

}  // namespace illyria::sim
