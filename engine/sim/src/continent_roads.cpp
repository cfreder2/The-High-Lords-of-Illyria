// The roads that lead to every town's gate. The design's roads (continent.cpp)
// join the realms; the towns are placed beside them where they can be, and
// this joins each of the rest to the network: a cheapest path over the land
// (not through water, rock or another town's walls; round steep ground and
// swamp), from the town's middle to the nearest road or to the road another
// town has already been joined by. The path decides which way the town's gate
// faces (planCastles); once the castle stands, the road is laid from the
// gate's mouth along it as one more road (roads_), so everything that knows
// the roads (the ground, bridges over creeks, what is placed clear of them)
// treats it like the rest.
#include <algorithm>
#include <bit>
#include <cstdint>
#include <queue>
#include <set>

#include "continent_shapes.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/noise.h"

namespace illyria::sim {

using namespace continent_shapes;

namespace {

constexpr int64_t kCell = 16;                   // metres between the path finder's nodes
constexpr uint32_t kClosed = 0, kBase = 10;     // a node's cost per metre, tenths (kClosed: no way through)
constexpr uint32_t kInf = 0xFFFFFFFFu;
// A road is "near" a node within this many metres of its middle.
constexpr int64_t kNear = 12;
// Metres of straight road in front of a gate before it bends towards its path.
constexpr int64_t kForecourt = 8;
// A town cut off from every road (on an isle) has this much road at its gate.
constexpr int64_t kStub = 40;
// The widest river (its half width) a road is bridged across.
constexpr Fixed kBridgeable = Fixed::fromInt(12);
// The longest stretch of water a road is bridged over (metres).
constexpr int64_t kMaxSpan = 70;

// Road classes (docs/TERRAIN_AUDIT.md, "Road classes"): a road's half width in metres at 1, 2, 3 and 4 lanes (4.4, 6.6, 10 and
// 14 m across), and the traffic (castle-to-castle journeys using it, a capital counting four) at which it has one lane and
// four, a lane more for each equal step of the logarithm between.
constexpr Fixed kLaneHalf[4] = {r(11, 5), r(33, 10), m(5), m(7)};
constexpr int64_t kTrafficOneLane = 800, kTrafficFourLanes = 5000;
constexpr Fixed kMinHalfWidth = m(2);  // never narrower than this (a lane, less the wobble)
constexpr Fixed kTaperPerMetre = r(1, 10);  // how fast a road's half width may change along it
constexpr int64_t kRoadStep = 6;            // metres between a finished road's points

}  // namespace

void Continent::keepRoadsDry() {
    // The design's roads were drawn before the rivers meandered and the lakes spread, and some run in the water for
    // stretches (the Westrun's, along its own bed). Any way point in it, away from the crossings the roads are meant
    // to use, moves to the nearest dry ground: its bank, a few metres clear.
    for (Polyline& rd : roads_) {
        for (size_t i = 0; i < rd.points.size(); ++i) {
            Vec3& p = rd.points[i];
            bool crossing = false;
            for (const Crossing& c : crossings_) crossing = crossing || (c.pos - p).flat().lengthSq() < m(45) * m(45);
            Fixed surface;
            if (crossing || !waterSurfaceOf(sample(p.x, p.y), surface)) continue;
            bool done = false;
            for (int64_t reach = 6; reach <= 150 && !done; reach += 6) {
                for (int k = 0; k < 16 && !done; ++k) {
                    const Fixed a = Fixed::fromRatio(k, 16) * 6283 / 1000;
                    const Vec3 q = p + Vec3{cos(a), sin(a), Fixed::zero()} * m(reach);
                    const Sample s = sample(q.x, q.y);
                    if (s.land < m(30) || waterSurfaceOf(s, surface) || s.riverDist < s.riverHalfWidth + m(12)) continue;
                    p = {q.x, q.y, Fixed::zero()};
                    done = true;
                }
            }
        }
    }
    bucketRoads();
}

void Continent::routeApproaches() {
    const int32_t nx = static_cast<int32_t>((kWidth + kCell - 1) / kCell), ny = static_cast<int32_t>((kHeight + kCell - 1) / kCell);
    const size_t count = static_cast<size_t>(nx) * static_cast<size_t>(ny);
    const auto at = [&](int32_t x, int32_t y) { return static_cast<size_t>(y) * static_cast<size_t>(nx) + static_cast<size_t>(x); };
    const auto centre = [&](int32_t x, int32_t y) { return Vec3{m(x * kCell + kCell / 2), m(y * kCell + kCell / 2), Fixed::zero()}; };

    // What each node costs to cross (tenths of a unit a metre), its height, and whether a road is by it.
    std::vector<uint32_t> cost(count, kBase);
    std::vector<Fixed> height(count);
    std::vector<uint8_t> net(count, 0), shut(count, 0);
    // What water costs, from a sample: a creek or a narrow river can be bridged (placeBridges, where the
    // road's stretch crosses it); a broad river, a lake, a pool or the sea cannot.
    const auto water = [&](const Sample& s) -> uint32_t {
        Fixed surface;
        if (s.land < m(20)) return kClosed;
        if (!waterSurfaceOf(s, surface)) return kBase;
        if (s.inLake) return kClosed;
        if (s.inBasin) return kBase * 60;
        const bool river = s.riverDist < s.riverHalfWidth + m(6) && s.height < s.riverSurface;
        if (river) return s.riverHalfWidth <= kBridgeable ? kBase * 25 : kClosed;
        return kBase * 40;
    };
    for (int32_t y = 0; y < ny; ++y)
        for (int32_t x = 0; x < nx; ++x) {
            const Vec3 p = centre(x, y);
            const Sample s = sample(p.x, p.y);
            const size_t i = at(x, y);
            height[i] = s.height;
            net[i] = s.roadDist < m(kNear - 3);
            uint32_t c = water(s);
            if (c == kBase)
                switch (landGround(p.x, p.y, s.height, s.mountain, s.roadDist)) {
                    case Ground::Rock: c = kClosed; break;
                    case Ground::Swamp: c = kBase * 5; break;
                    case Ground::Forest: case Ground::Mud: c = kBase * 2; break;
                    case Ground::Shallows: case Ground::DeepWater: c = kClosed; break;
                    default: break;
                }
            // Water anywhere in the node, a sample every 6 m (a pond is smaller than a node).
            for (int k = 0; k < 9 && c != kClosed; ++k) {
                if (k == 4) continue;
                const uint32_t w = water(sample(p.x + m((k % 3 - 1) * 6), p.y + m((k / 3 - 1) * 6)));
                c = w == kClosed ? kClosed : std::max(c, w);
            }
            if (net[i] && c == kClosed) c = kBase;  // (a ford, or a causeway: the road is there already)
            cost[i] = c;
            shut[i] = c == kClosed && !net[i] && s.land >= m(20);  // (not the sea: that is far enough)
        }
    // A road keeps a node's width from water (its edge is not a bank) where it can: dear, not shut, so it can still go between.
    for (int32_t y = 0; y < ny; ++y)
        for (int32_t x = 0; x < nx; ++x) {
            const size_t i = at(x, y);
            if (net[i] || cost[i] == kClosed) continue;
            for (int k = 0; k < 8; ++k) {
                const int32_t X = x + (k < 3 ? k - 1 : k < 5 ? (k == 3 ? -1 : 1) : k - 6), Y = y + (k < 3 ? -1 : k < 5 ? 0 : 1);
                if (X >= 0 && Y >= 0 && X < nx && Y < ny && shut[at(X, Y)]) cost[i] = std::max(cost[i], kBase * 6);
            }
        }
    // Inside a town's walls only that town's own roads go.
    const auto inTown = [&](size_t ci, int32_t x, int32_t y) {
        const Vec3 p = centre(x, y);
        return (p - cities_[ci].pos).flat().length() < cities_[ci].radius + m(kCell / 2);
    };
    std::vector<int32_t> owner(count, -1);
    for (size_t ci = 0; ci < cities_.size(); ++ci) {
        const int64_t reach = (cities_[ci].radius / kCell).floorToInt() + 3;
        const int32_t cx = static_cast<int32_t>((cities_[ci].pos.x / kCell).floorToInt()), cy = static_cast<int32_t>((cities_[ci].pos.y / kCell).floorToInt());
        for (int32_t y = std::max<int32_t>(0, cy - static_cast<int32_t>(reach)); y <= std::min<int32_t>(ny - 1, cy + static_cast<int32_t>(reach)); ++y)
            for (int32_t x = std::max<int32_t>(0, cx - static_cast<int32_t>(reach)); x <= std::min<int32_t>(nx - 1, cx + static_cast<int32_t>(reach)); ++x)
                if (inTown(ci, x, y)) {
                    owner[at(x, y)] = static_cast<int32_t>(ci);
                    net[at(x, y)] = 0;  // (a road through the walls is no way in)
                }
    }

    struct Entry {
        uint32_t cost;
        uint32_t node;
        bool operator>(const Entry& o) const { return cost != o.cost ? cost > o.cost : node > o.node; }
    };
    using Queue = std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>>;
    // The price of a step from node a to its neighbour b: the metres, dearer
    // across slopes (as the square of the gradient, none beyond 1 in 2.5).
    const auto step = [&](size_t a, size_t b, uint32_t metres, uint32_t& out) {
        if (cost[b] == kClosed) return false;
        const Fixed rise = abs(height[b] - height[a]);
        const int64_t percent = (rise * 100 / metres).floorToInt();
        if (percent > 40) return false;
        const uint32_t mult = (cost[a] + cost[b]) / 2 + static_cast<uint32_t>(percent * percent / 3);
        out = metres * mult;
        return true;
    };
    static constexpr int dx[8] = {1, -1, 0, 0, 1, 1, -1, -1}, dy[8] = {0, 0, 1, -1, 1, -1, 1, -1};
    const auto cellOf = [&](const Vec3& p) {
        return at(static_cast<int32_t>(std::min<int64_t>(nx - 1, (p.x / kCell).floorToInt())), static_cast<int32_t>(std::min<int64_t>(ny - 1, (p.y / kCell).floorToInt())));
    };

    std::vector<uint32_t> dist(count);
    std::vector<uint32_t> from(count);
    // Which towns first: the ones nearest the roads, so the farther ones can
    // join the spurs those lay.
    std::vector<size_t> order(cities_.size());
    {
        std::fill(dist.begin(), dist.end(), kInf);
        Queue q;
        for (size_t i = 0; i < count; ++i)
            if (net[i]) {
                dist[i] = 0;
                q.push({0, static_cast<uint32_t>(i)});
            }
        while (!q.empty()) {
            const Entry e = q.top();
            q.pop();
            if (e.cost > dist[e.node]) continue;
            const int32_t x = static_cast<int32_t>(e.node % static_cast<size_t>(nx)), y = static_cast<int32_t>(e.node / static_cast<size_t>(nx));
            for (int k = 0; k < 8; ++k) {
                const int32_t X = x + dx[k], Y = y + dy[k];
                if (X < 0 || Y < 0 || X >= nx || Y >= ny) continue;
                uint32_t c;
                if (!step(e.node, at(X, Y), k < 4 ? kCell : kCell * 3 / 2, c)) continue;
                if (e.cost + c < dist[at(X, Y)]) {
                    dist[at(X, Y)] = e.cost + c;
                    q.push({e.cost + c, static_cast<uint32_t>(at(X, Y))});
                }
            }
        }
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return dist[cellOf(cities_[a].pos)] < dist[cellOf(cities_[b].pos)]; });
    }

    approach_.assign(cities_.size(), {});
    approachOrder_.clear();
    for (size_t ci : order) {
        const size_t start = cellOf(cities_[ci].pos);
        std::fill(dist.begin(), dist.end(), kInf);
        Queue q;
        dist[start] = 0;
        q.push({0, static_cast<uint32_t>(start)});
        size_t goal = count;
        while (!q.empty()) {
            const Entry e = q.top();
            q.pop();
            if (e.cost > dist[e.node]) continue;
            if (net[e.node] && owner[e.node] < 0) {
                goal = e.node;
                break;
            }
            const int32_t x = static_cast<int32_t>(e.node % static_cast<size_t>(nx)), y = static_cast<int32_t>(e.node / static_cast<size_t>(nx));
            for (int k = 0; k < 8; ++k) {
                const int32_t X = x + dx[k], Y = y + dy[k];
                if (X < 0 || Y < 0 || X >= nx || Y >= ny) continue;
                const size_t b = at(X, Y);
                // Other towns' walls are shut; its own are open.
                if (owner[b] >= 0 && static_cast<size_t>(owner[b]) != ci) continue;
                uint32_t c;
                // (Its pad is level and dry, as far as its walls reach; the ring outside them is as the land is.)
                if (owner[b] >= 0 && (centre(X, Y) - cities_[ci].pos).flat().length() < cities_[ci].radius * r(9, 10)) c = (k < 4 ? kCell : kCell * 3 / 2) * kBase;
                else if (!step(e.node, b, k < 4 ? kCell : kCell * 3 / 2, c)) continue;
                if (e.cost + c < dist[b]) {
                    dist[b] = e.cost + c;
                    from[b] = e.node;
                    q.push({e.cost + c, static_cast<uint32_t>(b)});
                }
            }
        }
        if (goal == count) {  // cut off (on an isle): a stub of road at its gate, and the gate it would have had
            approachOrder_.push_back(ci);
            continue;
        }
        std::vector<Vec3> path;
        for (size_t n = goal; n != start; n = from[n]) {
            path.push_back(centre(static_cast<int32_t>(n % static_cast<size_t>(nx)), static_cast<int32_t>(n / static_cast<size_t>(nx))));
            net[n] = 1;
        }
        std::reverse(path.begin(), path.end());
        approach_[ci] = std::move(path);
        approachOrder_.push_back(ci);
    }
    // The design's roads do not all meet (the Sorrow's, the Westrun's...), and the towns have joined whichever was
    // nearest: join the networks, the smaller each to the largest by its cheapest way, so one road leads to every town.
    {
        std::vector<int32_t> comp(count, -1);
        std::vector<size_t> size;
        for (size_t i = 0; i < count; ++i) {
            if (!net[i] || comp[i] >= 0) continue;
            const int32_t id = static_cast<int32_t>(size.size());
            size.push_back(0);
            std::vector<size_t> stack{i};
            comp[i] = id;
            while (!stack.empty()) {
                const size_t n = stack.back();
                stack.pop_back();
                ++size[static_cast<size_t>(id)];
                const int32_t x = static_cast<int32_t>(n % static_cast<size_t>(nx)), y = static_cast<int32_t>(n / static_cast<size_t>(nx));
                for (int32_t Y = std::max(0, y - 1); Y <= std::min(ny - 1, y + 1); ++Y)
                    for (int32_t X = std::max(0, x - 1); X <= std::min(nx - 1, x + 1); ++X)
                        if (net[at(X, Y)] && comp[at(X, Y)] < 0) {
                            comp[at(X, Y)] = id;
                            stack.push_back(at(X, Y));
                        }
            }
        }
        std::vector<int32_t> ids(size.size());
        for (size_t i = 0; i < ids.size(); ++i) ids[i] = static_cast<int32_t>(i);
        std::stable_sort(ids.begin(), ids.end(), [&](int32_t a, int32_t b) { return size[static_cast<size_t>(a)] > size[static_cast<size_t>(b)]; });
        links_.clear();
        for (size_t k = 1; k < ids.size(); ++k) {
            const int32_t mine = ids[k];
            std::fill(dist.begin(), dist.end(), kInf);
            Queue q;
            for (size_t i = 0; i < count; ++i)
                if (comp[i] == mine) {
                    dist[i] = 0;
                    q.push({0, static_cast<uint32_t>(i)});
                }
            size_t goal = count;
            while (!q.empty()) {
                const Entry e = q.top();
                q.pop();
                if (e.cost > dist[e.node]) continue;
                if (comp[e.node] == ids[0] && owner[e.node] < 0) {
                    goal = e.node;
                    break;
                }
                const int32_t x = static_cast<int32_t>(e.node % static_cast<size_t>(nx)), y = static_cast<int32_t>(e.node / static_cast<size_t>(nx));
                for (int j = 0; j < 8; ++j) {
                    const int32_t X = x + dx[j], Y = y + dy[j];
                    if (X < 0 || Y < 0 || X >= nx || Y >= ny || owner[at(X, Y)] >= 0) continue;
                    uint32_t c;
                    if (!step(e.node, at(X, Y), j < 4 ? kCell : kCell * 3 / 2, c)) continue;
                    if (e.cost + c < dist[at(X, Y)]) {
                        dist[at(X, Y)] = e.cost + c;
                        from[at(X, Y)] = e.node;
                        q.push({e.cost + c, static_cast<uint32_t>(at(X, Y))});
                    }
                }
            }
            if (goal == count) continue;
            std::vector<Vec3> path;
            size_t n = goal;
            for (; comp[n] != mine; n = from[n]) {
                path.push_back(centre(static_cast<int32_t>(n % static_cast<size_t>(nx)), static_cast<int32_t>(n / static_cast<size_t>(nx))));
                net[n] = 1;
                comp[n] = ids[0];
            }
            path.push_back(centre(static_cast<int32_t>(n % static_cast<size_t>(nx)), static_cast<int32_t>(n / static_cast<size_t>(nx))));
            for (size_t i = 0; i < count; ++i)
                if (comp[i] == mine) comp[i] = ids[0];
            std::reverse(path.begin(), path.end());
            links_.push_back(std::move(path));
        }
    }
}

void Continent::layApproaches() {
    const size_t firstRoad = roads_.size();
    const size_t firstCrossing = crossings_.size();
    // The road networks laid so far (a union of roads, by the road each joined), so a link is only laid between two that are apart.
    std::vector<size_t> net(roads_.size());
    for (size_t i = 0; i < net.size(); ++i) net[i] = i;
    const auto root = [&](size_t i) {
        while (net[i] != i) i = net[i] = net[net[i]];
        return i;
    };
    const auto unite = [&](size_t a, size_t b) { net[root(a)] = root(b); };
    for (size_t i = 0; i < firstRoad; ++i)
        for (size_t j = i + 1; j < firstRoad; ++j) {
            bool meets = false;
            for (const Vec3& v : roads_[i].points) {
                size_t seg = 0;
                Fixed u;
                meets = meets || polylineDist(roads_[j].points, v.x, v.y, m(6), seg, u) < m(6);
            }
            if (meets) unite(i, j);
        }
    // The nearest of the roads laid so far to p within `reach`, not in network `skip` (SIZE_MAX: any); and where on it.
    const auto nearest = [&](const Vec3& p, Fixed reach, size_t skip, size_t& road, Vec3& join) {
        Fixed best = reach;
        bool found = false;
        for (size_t ri = 0; ri < roads_.size(); ++ri) {
            if (skip != SIZE_MAX && root(ri) == skip) continue;
            size_t s = 0;
            Fixed u;
            const Fixed d = polylineDist(roads_[ri].points, p.x, p.y, reach, s, u);
            if (d < best) {
                best = d;
                road = ri;
                join = roads_[ri].points[s] + (roads_[ri].points[s + 1] - roads_[ri].points[s]) * u;
                found = true;
            }
        }
        return found;
    };
    // A smooth curve through the control points, a point every 12 m.
    const auto curve = [&](const std::vector<Vec3>& ctrl) {
        Polyline road;
        road.halfWidth = r(5, 2);
        for (size_t i = 0; i + 1 < ctrl.size(); ++i) {
            const Vec3 p0 = ctrl[i == 0 ? 0 : i - 1], p1 = ctrl[i], p2 = ctrl[i + 1], p3 = ctrl[std::min(i + 2, ctrl.size() - 1)];
            const int64_t steps = std::max<int64_t>(1, ((p2 - p1).flat().length() / 12).floorToInt());
            for (int64_t k = 0; k < steps; ++k) {
                const Fixed u = Fixed::fromRatio(k, steps), u2 = u * u, u3 = u2 * u;
                const Vec3 c = p1 * m(2) + (p2 - p0) * u + (p0 * m(2) - p1 * m(5) + p2 * m(4) - p3) * u2 + (p1 * m(3) - p0 - p2 * m(3) + p3) * u3;
                road.points.push_back({c.x * r(1, 2), c.y * r(1, 2), Fixed::zero()});
            }
        }
        road.points.push_back({ctrl.back().x, ctrl.back().y, Fixed::zero()});
        return road;
    };
    // Where a road laid from `from` on ends: at the first point that touches a road of another network (within kTouch), or
    // where it comes nearest to one (within 64 m): cut there, the end moved onto that road. A path wanders a little and its
    // smoothing overshoots, so without this a road ran on past the one it joins and doubled back: the loops and spurs beside
    // a junction. False if it comes near none.
    constexpr Fixed kTouch = r(9, 2);
    struct Join {
        size_t road;
        Vec3 at;
    };
    std::vector<Join> joins;  // where each laid road met another, and on which
    const auto cutAtJoin = [&](Polyline& road, size_t from, size_t skip, Join& out) {
        size_t cut = road.points.size(), bestK = 0;
        Fixed bestD = m(64);
        Join bestJ{0, {}};
        bool found = false;
        for (size_t k = from; k < road.points.size() && cut == road.points.size(); ++k) {
            size_t rr = 0;
            Vec3 jj;
            if (!nearest(road.points[k], m(64), skip, rr, jj)) continue;
            const Fixed d = (road.points[k] - jj).flat().length();
            if (d <= kTouch) {
                cut = k;
                bestK = k;
                bestJ = {rr, jj};
                found = true;
            } else if (d <= bestD) {
                bestD = d;
                bestK = k;
                bestJ = {rr, jj};
                found = true;
            }
        }
        if (!found) return false;
        road.points.resize(bestK + 1);
        if ((road.points.back() - bestJ.at).flat().length() <= kTouch) road.points.back() = bestJ.at;
        else road.points.push_back(bestJ.at);
        out = bestJ;
        return true;
    };
    const auto finish = [&](Polyline& road) {
        road.widths.assign(road.points.size(), road.halfWidth);
        roads_.push_back(std::move(road));
        net.push_back(roads_.size() - 1);
    };
    std::vector<size_t> gateRoad(cities_.size(), SIZE_MAX);
    for (size_t ci : approachOrder_) {
        const City& city = cities_[ci];
        const Castle::Ring& ring = city.castle.rings.front();
        const size_t n = ring.corners.size();
        const Vec3 a = ring.corners[ring.gateEdge], b = ring.corners[(ring.gateEdge + 1) % n];
        const Vec3 mid = (a + b) * r(1, 2);
        const Vec3 out = unitFlat(mid - city.pos);
        const Fixed reach = (mid - city.pos).flat().length() + ring.thick;
        // From the gate straight out, then along the path from where it leaves the walls.
        std::vector<Vec3> ctrl{mid, mid + out * (ring.thick / 2 + m(kForecourt))};
        const std::vector<Vec3>& path = approach_[ci];
        size_t first = 0;
        while (first < path.size() && (path[first] - city.pos).flat().length() < reach + m(kForecourt) + m(kCell)) ++first;
        if (path.empty()) ctrl.push_back(mid + out * (ring.thick / 2 + m(kStub)));
        for (size_t i = first; i < path.size(); ++i)
            if (i + 1 == path.size() || (i - first) % 2 == 1) ctrl.push_back(path[i]);
        Polyline road = curve(ctrl);
        if (road.points.size() < 2) continue;
        Join j{};
        // (Not within the forecourt and the first stretch out of the gate.)
        size_t from = 0;
        while (from < road.points.size() && (road.points[from] - mid).flat().length() < ring.thick / 2 + m(kForecourt + 14)) ++from;
        from = std::min(from, road.points.size() - 1);
        const bool joined = !path.empty() && cutAtJoin(road, from, SIZE_MAX, j);
        finish(road);
        gateRoad[ci] = roads_.size() - 1;
        if (joined) {
            unite(roads_.size() - 1, j.road);
            joins.push_back(j);
        }
    }
    // The links between the networks, from one road to another: only where the two ends are in different networks.
    for (const std::vector<Vec3>& path : links_) {
        std::vector<Vec3> ctrl;
        for (size_t i = 0; i < path.size(); ++i)
            if (i == 0 || i + 1 == path.size() || i % 2 == 1) ctrl.push_back(path[i]);
        size_t startRoad = 0;
        Vec3 start;
        if (!nearest(ctrl.front(), m(kCell * 4), SIZE_MAX, startRoad, start)) continue;
        ctrl.front() = start;
        Polyline road = curve(ctrl);
        Join j{};
        if (road.points.size() < 2 || !cutAtJoin(road, 1, root(startRoad), j)) continue;
        finish(road);
        unite(roads_.size() - 1, startRoad);
        unite(roads_.size() - 1, j.road);
        joins.push_back(j);
    }
    approach_.clear();
    approachOrder_.clear();
    links_.clear();
    // A design road's free end that runs on past a junction for under kStubReach metres is a dead-end spur: it stops at the junction.
    constexpr Fixed kStubReach = m(100);
    for (size_t ri = 0; ri < firstRoad; ++ri) {
        for (int end = 0; end < 2; ++end) {
            Polyline& road = roads_[ri];
            if (road.points.size() < 3) continue;
            const Vec3 tip = end ? road.points.back() : road.points.front();
            const Sample s = sample(tip.x, tip.y);
            size_t other = 0;
            Vec3 jj;
            // Free: on open land, clear of the map's edge, and touching no other road.
            if (s.land < m(40) || tip.x < m(40) || tip.y < m(40) || tip.x > m(kWidth - 40) || tip.y > m(kHeight - 40)) continue;
            bool touching = false;
            for (size_t o = 0; o < roads_.size() && !touching; ++o) {
                if (o == ri) continue;
                size_t sg = 0;
                Fixed u;
                touching = polylineDist(roads_[o].points, tip.x, tip.y, m(6), sg, u) < m(6);
            }
            (void)other;
            (void)jj;
            if (touching) continue;
            // The join nearest the tip along this road, within reach.
            Fixed bestArc = kStubReach;
            size_t cutSeg = 0;
            Vec3 cutAt;
            bool have = false;
            for (const Join& j : joins) {
                if (j.road != ri) continue;
                size_t sg = 0;
                Fixed u;
                if (polylineDist(road.points, j.at.x, j.at.y, m(3), sg, u) >= m(3)) continue;
                // Metres from the tip to the join along the road.
                Fixed arc = end ? (road.points[sg + 1] - j.at).flat().length() : (j.at - road.points[sg]).flat().length();
                if (end) for (size_t k = sg + 1; k + 1 < road.points.size(); ++k) arc += (road.points[k + 1] - road.points[k]).flat().length();
                else for (size_t k = 0; k < sg; ++k) arc += (road.points[k + 1] - road.points[k]).flat().length();
                if (arc < bestArc) {
                    bestArc = arc;
                    cutSeg = sg;
                    cutAt = j.at;
                    have = true;
                }
            }
            if (!have) continue;
            if (end) {
                road.points.resize(cutSeg + 1);
                road.points.push_back(cutAt);
            } else {
                road.points.erase(road.points.begin(), road.points.begin() + static_cast<std::ptrdiff_t>(cutSeg + 1));
                road.points.insert(road.points.begin(), cutAt);
            }
            road.widths.assign(road.points.size(), road.halfWidth);
        }
    }
    // Spikes: a point where the road turns right back on itself (a hook at a road's end, or in a path's smoothing) is dropped.
    for (Polyline& road : roads_) {
        bool again = true;
        while (again && road.points.size() > 2) {
            again = false;
            for (size_t i = 1; i + 1 < road.points.size(); ++i) {
                const Vec3 u = (road.points[i] - road.points[i - 1]).flat(), v = (road.points[i + 1] - road.points[i]).flat();
                const Fixed lu = u.length(), lv = v.length();
                if (lu == Fixed::zero() || lv == Fixed::zero() || lu > m(40) || lv > m(40)) continue;
                if (u.x * v.x + u.y * v.y < -(lu * lv) * r(17, 100)) {  // turns by more than 100 degrees
                    road.points.erase(road.points.begin() + static_cast<std::ptrdiff_t>(i));
                    again = true;
                    break;
                }
            }
        }
        road.widths.assign(road.points.size(), road.halfWidth);
    }
    bucketRoads();
    placeBridges(firstRoad, firstCrossing);
    // Where a road still runs through water (a narrow river, a pool): a bridge over the stretch.
    const size_t firstSpan = crossings_.size();
    for (size_t ri = firstRoad; ri < roads_.size(); ++ri) {
        const Polyline& road = roads_[ri];
        bool wet = false;
        Vec3 from;
        const auto bridge = [&](const Vec3& to) {
            if ((to - from).flat().length() < m(kMaxSpan)) crossings_.push_back({Crossing::Kind::Bridge, "", (from + to) * r(1, 2)});
        };
        Vec3 last;
        for (size_t i = 0; i + 1 < road.points.size(); ++i) {
            const Vec3 a = road.points[i], b = road.points[i + 1];
            const int64_t steps = std::max<int64_t>(1, ((b - a).flat().length() / 3).floorToInt());
            for (int64_t k = 0; k < steps; ++k) {
                const Vec3 p = a + (b - a) * Fixed::fromRatio(k, steps);
                Fixed surface, deck;
                const bool inWater = waterSurfaceOf(sample(p.x, p.y), surface) && !onBridge(p.x, p.y, deck);
                if (inWater && !wet) from = p;
                if (!inWater && wet) bridge(last);
                wet = inWater;
                last = p;
            }
        }
        if (wet) bridge(last);
    }
    placeBridges(roads_.size(), firstSpan);
    // A bridge a castle's wall stands over is no bridge (the town was set on its creek, and the wall's walk is the only way
    // there): where the deck comes within 4 m of the outer wall's corners' circle, the road fords the creek instead.
    crossings_.erase(std::remove_if(crossings_.begin() + static_cast<std::ptrdiff_t>(firstCrossing), crossings_.end(),
                                    [&](const Crossing& cr) {
                                        if (!cr.bridged() || cr.kind == Crossing::Kind::Footbridge) return false;
                                        for (const City& city : cities_) {
                                            Fixed reach;
                                            for (const Vec3& q : city.castle.rings.front().corners) reach = max(reach, (q - city.pos).flat().length());
                                            for (int64_t k = 0; k <= 20; ++k) {
                                                const Fixed s = -cr.reachBehind() + (cr.reachBehind() + cr.reachAhead()) * Fixed::fromRatio(k, 20);
                                                if ((cr.pos + cr.dir * s - city.pos).flat().length() < reach + m(4)) return true;
                                            }
                                        }
                                        return false;
                                    }),
                     crossings_.end());
    // Every road bridge carries a road: one the road no longer passes (the design's, set before the roads were drawn from the
    // towns and meandered) moves to where the nearest road is in the water, and is spanned afresh; one with no road beyond
    // both its ends (a stub trimmed, a road that fords the creek) is no bridge.
    {
        const auto roadNear = [&](const Vec3& p, Fixed reach, size_t& road, size_t& seg, Fixed& t) {
            Fixed best = reach;
            bool found = false;
            for (size_t ri = 0; ri < roads_.size(); ++ri) {
                size_t sg = 0;
                Fixed u;
                const Fixed d = polylineDist(roads_[ri].points, p.x, p.y, reach, sg, u);
                if (d < best) best = d, road = ri, seg = sg, t = u, found = true;
            }
            return found;
        };
        std::vector<Crossing> kept;
        for (Crossing cr : crossings_) {
            if (cr.kind == Crossing::Kind::Ford) {  // (one in deep water that no road crosses: a bridge that was too wide to span, over a lake)
                size_t r0 = 0, s0 = 0;
                Fixed t0;
                if (groundAt(cr.pos.x, cr.pos.y) == Ground::DeepWater && !roadNear(cr.pos, m(30), r0, s0, t0)) continue;
            }
            if (cr.kind != Crossing::Kind::Bridge) {
                kept.push_back(cr);
                continue;
            }
            // The creek or pool a road bridge was made for may be gone: a town's pad, laid since, levels the ground it ran in,
            // and the lakes and rivers have shifted. Spanned again on the ground as it is; if there is no water under its length
            // it is no bridge (a deck over dry land, its ramps down to nothing).
            if (cr.name.empty()) {
                // (From the water nearest along its axis, within 60 m; none, and it is no bridge.)
                bool wet = false;
                for (Fixed along = Fixed::zero(); along <= m(60) && !wet; along += Fixed::one())
                    for (int side = 0; side < 2 && !wet; ++side) {
                        const Vec3 p = cr.pos + cr.dir * (side ? -along : along);
                        Fixed surface;
                        if (!waterSurfaceOf(sample(p.x, p.y), surface)) continue;
                        wet = true;
                        cr.pos = {p.x, p.y, Fixed::zero()};
                    }
                if (!wet) continue;
                cr.rampA = cr.rampB = cr.endA = cr.endB = Fixed::zero();
                spanCrossing(cr);
                if (!cr.bridged()) continue;  // (water too wide for a bridge, and no road wades it here: not a crossing at all)
            }
            size_t ri = 0, sg = 0;
            Fixed t;
            if (!roadNear(cr.pos, m(6), ri, sg, t)) {
                // The nearest road point in water within 220 m.
                bool found = false;
                Fixed best = m(220);
                Vec3 at, dir;
                for (const Polyline& rd : roads_)
                    for (size_t i = 0; i + 1 < rd.points.size(); ++i) {
                        const Vec3 p = (rd.points[i] + rd.points[i + 1]) * r(1, 2);
                        const Fixed d = (p - cr.pos).flat().length();
                        Fixed surface;
                        if (d < best && waterSurfaceOf(sample(p.x, p.y), surface)) {
                            best = d;
                            at = p;
                            dir = unitFlat(rd.points[i + 1] - rd.points[i]);
                            found = true;
                        }
                    }
                if (!found) continue;  // (water no road crosses: a bridge over it would carry nothing, and stand in a lake)
                cr.pos = {at.x, at.y, Fixed::zero()};
                cr.dir = dir;
                cr.rampA = cr.rampB = cr.endA = cr.endB = Fixed::zero();
                spanCrossing(cr);
                if (!cr.bridged()) continue;  // (too wide to span, and no road wades it: not a crossing)
            }
            kept.push_back(cr);
        }
        crossings_ = std::move(kept);
    }
    // A bridge with no road beyond one of its ends (a stub trimmed, a road that fords the creek) is no bridge (the road is within 30 m of
    // the point 25 m past the end: the pull onto the axis brings it on); the design's named ones stay.
    crossings_.erase(std::remove_if(crossings_.begin(), crossings_.end(),
                                    [&](const Crossing& cr) {
                                        if (cr.kind != Crossing::Kind::Bridge || !cr.name.empty()) return false;
                                        for (int side = 0; side < 2; ++side) {
                                            const Vec3 beyond = cr.pos + cr.dir * (side ? -cr.reachBehind() - m(25) : cr.reachAhead() + m(25));
                                            bool road = false;
                                            for (const Polyline& rd : roads_) {
                                                size_t sg = 0;
                                                Fixed u;
                                                road = road || polylineDist(rd.points, beyond.x, beyond.y, m(12), sg, u) < m(12);
                                            }
                                            if (!road) return true;
                                        }
                                        return false;
                                    }),
                     crossings_.end());
    shapeRoads(gateRoad);
    bucketRoads();
}

}  // namespace illyria::sim

namespace illyria::sim {

using namespace continent_shapes;

namespace {

// Lanes (1 to 4, in thousandths) for a traffic of f journeys: the logarithm of f between kTrafficOneLane and kTrafficFourLanes.
int64_t lanesOf(int64_t f) {
    const auto log2k = [](int64_t v) {  // log2 in thousandths, piecewise linear between the powers of two
        if (v < 1) return int64_t{0};
        const int64_t w = std::bit_width(static_cast<uint64_t>(v)) - 1;
        return w * 1000 + ((v - (int64_t{1} << w)) * 1000) / (int64_t{1} << w);
    };
    const int64_t lo = log2k(kTrafficOneLane), hi = log2k(kTrafficFourLanes);
    return 1000 + std::clamp<int64_t>((log2k(f) - lo) * 3000 / (hi - lo), 0, 3000);
}

Fixed halfWidthOfLanes(int64_t lanesK) {
    const int64_t i = std::clamp<int64_t>((lanesK - 1000) / 1000, 0, 2);
    const Fixed t = Fixed::fromRatio(std::clamp<int64_t>(lanesK - 1000 - i * 1000, 0, 1000), 1000);
    return kLaneHalf[i] + (kLaneHalf[i + 1] - kLaneHalf[i]) * t;
}

}  // namespace

void Continent::shapeRoads(const std::vector<size_t>& gateRoad) {
    // 1. Every road resampled to a point every kRoadStep metres or less (the line itself does not change).
    for (Polyline& road : roads_) {
        std::vector<Vec3> pts;
        for (size_t i = 0; i + 1 < road.points.size(); ++i) {
            const Vec3 a = road.points[i], b = road.points[i + 1];
            const int64_t steps = std::max<int64_t>(1, ((b - a).flat().length() + m(kRoadStep - 1)).floorToInt() / kRoadStep);
            for (int64_t k = 0; k < steps; ++k) pts.push_back(a + (b - a) * Fixed::fromRatio(k, steps));
        }
        if (!road.points.empty()) pts.push_back(road.points.back());
        road.points = std::move(pts);
    }
    const size_t roadCount = roads_.size();
    std::vector<size_t> base(roadCount + 1, 0);
    for (size_t ri = 0; ri < roadCount; ++ri) base[ri + 1] = base[ri] + roads_[ri].points.size();
    const size_t nodes = base[roadCount];
    const auto at = [&](size_t node) -> const Vec3& {
        const size_t ri = static_cast<size_t>(std::upper_bound(base.begin(), base.end(), node) - base.begin()) - 1;
        return roads_[ri].points[node - base[ri]];
    };

    // 2. The network as a graph: along each road, and across wherever two roads' points are within a junction's reach.
    struct Edge {
        uint32_t a, b;
        int64_t cost;
    };
    std::vector<Edge> edges;
    std::vector<uint8_t> junction(nodes, 0);  // 1 within a few points of another road's
    for (size_t ri = 0; ri < roadCount; ++ri)
        for (size_t i = 0; i + 1 < roads_[ri].points.size(); ++i)
            edges.push_back({static_cast<uint32_t>(base[ri] + i), static_cast<uint32_t>(base[ri] + i + 1), (roads_[ri].points[i + 1] - roads_[ri].points[i]).flat().length().raw});
    {
        constexpr int64_t kCellG = 8;
        const int64_t gw = kWidth / kCellG + 2, gh = kHeight / kCellG + 2;
        std::vector<std::vector<uint32_t>> grid(static_cast<size_t>(gw * gh));
        const auto cellOf = [&](const Vec3& p) { return std::pair<int64_t, int64_t>{std::clamp<int64_t>(p.x.floorToInt() / kCellG, 0, gw - 1), std::clamp<int64_t>(p.y.floorToInt() / kCellG, 0, gh - 1)}; };
        std::vector<uint32_t> roadOf(nodes);
        for (size_t ri = 0; ri < roadCount; ++ri)
            for (size_t k = base[ri]; k < base[ri + 1]; ++k) roadOf[k] = static_cast<uint32_t>(ri);
        for (size_t node = 0; node < nodes; ++node) {
            const Vec3& p = at(node);
            const auto [cx, cy] = cellOf(p);
            for (int64_t dy = -1; dy <= 1; ++dy)
                for (int64_t dx = -1; dx <= 1; ++dx) {
                    const int64_t X = cx + dx, Y = cy + dy;
                    if (X < 0 || Y < 0 || X >= gw || Y >= gh) continue;
                    for (uint32_t other : grid[static_cast<size_t>(Y * gw + X)]) {
                        if (roadOf[other] == roadOf[node]) continue;
                        const Fixed d = (at(other) - p).flat().length();
                        if (d > m(7)) continue;
                        edges.push_back({other, static_cast<uint32_t>(node), d.raw + 1});
                        for (int64_t k = -3; k <= 3; ++k) {
                            const int64_t a = static_cast<int64_t>(node) + k, b = static_cast<int64_t>(other) + k;
                            if (a >= static_cast<int64_t>(base[roadOf[node]]) && a < static_cast<int64_t>(base[roadOf[node] + 1])) junction[static_cast<size_t>(a)] = 1;
                            if (b >= static_cast<int64_t>(base[roadOf[other]]) && b < static_cast<int64_t>(base[roadOf[other] + 1])) junction[static_cast<size_t>(b)] = 1;
                        }
                    }
                }
            grid[static_cast<size_t>(cy * gw + cx)].push_back(static_cast<uint32_t>(node));
        }
    }
    std::vector<std::vector<uint32_t>> adj(nodes);
    for (uint32_t e = 0; e < edges.size(); ++e) {
        adj[edges[e].a].push_back(e);
        adj[edges[e].b].push_back(e);
    }

    // 3. Traffic: every pair of castles takes the cheapest way between their gates; each edge counts the journeys over it.
    std::vector<int64_t> flow(edges.size(), 0);
    {
        std::vector<int64_t> weight(nodes, 0);
        std::vector<size_t> sources;
        for (size_t ci = 0; ci < cities_.size(); ++ci) {
            if (gateRoad[ci] == SIZE_MAX) continue;
            weight[base[gateRoad[ci]]] += cities_[ci].capital ? 4 : 1;
            sources.push_back(base[gateRoad[ci]]);
        }
        struct Item {
            int64_t cost;
            uint32_t node;
            bool operator>(const Item& o) const { return cost != o.cost ? cost > o.cost : node > o.node; }
        };
        std::vector<int64_t> dist(nodes);
        std::vector<int64_t> below(nodes);
        std::vector<uint32_t> parentEdge(nodes);
        std::vector<uint32_t> order;
        for (size_t src : sources) {
            std::fill(dist.begin(), dist.end(), INT64_MAX);
            std::fill(parentEdge.begin(), parentEdge.end(), UINT32_MAX);
            order.clear();
            std::priority_queue<Item, std::vector<Item>, std::greater<Item>> q;
            dist[src] = 0;
            q.push({0, static_cast<uint32_t>(src)});
            while (!q.empty()) {
                const Item it = q.top();
                q.pop();
                if (it.cost > dist[it.node]) continue;
                order.push_back(it.node);
                for (uint32_t e : adj[it.node]) {
                    const uint32_t v = edges[e].a == it.node ? edges[e].b : edges[e].a;
                    if (it.cost + edges[e].cost < dist[v]) {
                        dist[v] = it.cost + edges[e].cost;
                        parentEdge[v] = e;
                        q.push({dist[v], v});
                    }
                }
            }
            for (uint32_t v : order) below[v] = v == src ? 0 : weight[v];
            for (size_t k = order.size(); k-- > 1;) {
                const uint32_t v = order[k], e = parentEdge[v];
                const uint32_t parent = edges[e].a == v ? edges[e].b : edges[e].a;
                flow[e] += weight[src] * below[v];
                below[parent] += below[v];
            }
        }
    }

    // 4. Width from traffic: the lanes at each point (smoothed along the road), the half width they make, a small
    // slow wobble, and never wider than the town's gate or a bridge's deck lets it be; changing no faster than kTaperPerMetre.
    roadTraffic_.assign(roadCount, {});
    for (size_t ri = 0; ri < roadCount; ++ri) {
        Polyline& road = roads_[ri];
        const size_t n = road.points.size();
        std::vector<int64_t> lanes(n, 1000);
        roadTraffic_[ri].assign(n, 0);
        for (size_t i = 0; i < n; ++i) {
            // The traffic on the road's edges either side of this point (junction edges have it too).
            int64_t f = 0;
            if (i > 0) f = std::max(f, flow[base[ri] + i - 1]);
            if (i + 1 < n) f = std::max(f, flow[base[ri] + i]);
            roadTraffic_[ri][i] = f;
            lanes[i] = lanesOf(f);
        }
        for (int pass = 0; pass < 24; ++pass) {
            std::vector<int64_t> next = lanes;
            for (size_t i = 0; i < n; ++i) next[i] = (lanes[i > 0 ? i - 1 : i] + 2 * lanes[i] + lanes[i + 1 < n ? i + 1 : i]) / 4;
            lanes = std::move(next);
        }
        const uint64_t sr = noise::nextSeed(seed_ ^ (0x70AD + ri * 811));
        std::vector<Fixed> hw(n);
        Fixed arc;
        for (size_t i = 0; i < n; ++i) {
            if (i > 0) arc += (road.points[i] - road.points[i - 1]).flat().length();
            const Fixed base = halfWidthOfLanes(lanes[i]);
            const Fixed wobble = (noise::fbm(sr, arc, m(static_cast<int64_t>(ri) * 5000), 70) - r(1, 2)) * 2 * (r(1, 5) + base / 25);
            hw[i] = max(kMinHalfWidth, base + wobble);
            // A town's gate (its pad is the walls, and its road narrows to the gate), 8 m of road for each metre of half width.
            Fixed gap = m(100000);
            for (const City& city : cities_) {
                const Fixed dx = abs(road.points[i].x - city.pos.x), dy = abs(road.points[i].y - city.pos.y);
                if (dx > city.radius + m(80) || dy > city.radius + m(80)) continue;
                gap = min(gap, (road.points[i] - city.pos).flat().length() - city.radius);
            }
            hw[i] = min(hw[i], kLaneHalf[0] + max(Fixed::zero(), gap) / 8 + r(3, 10));
        }
        for (size_t i = 1; i < n; ++i) hw[i] = min(hw[i], hw[i - 1] + kTaperPerMetre * kRoadStep);
        for (size_t i = n - 1; i-- > 0;) hw[i] = min(hw[i], hw[i + 1] + kTaperPerMetre * kRoadStep);
        for (Fixed& w : hw) w = max(kMinHalfWidth, w);
        road.widths = std::move(hw);
        Fixed sum;
        for (const Fixed& w : road.widths) sum += w;
        road.halfWidth = n ? sum / static_cast<int64_t>(n) : road.halfWidth;
    }

    // 5. Bends round the boulders and rocks that stand on a road's line (Region::worthDodging): each bends it aside by a smooth
    // bump of the road, so its edge keeps kDodgeMargin from the stone. Not near a junction, a bridge or a town.
    const uint64_t sceneSeed = Region::sceneryContinentSeed(seed_);
    for (size_t ri = 0; ri < roadCount; ++ri) {
        Polyline& road = roads_[ri];
        const size_t n = road.points.size();
        if (n < 8) continue;
        std::vector<Fixed> arcs(n);
        for (size_t i = 1; i < n; ++i) arcs[i] = arcs[i - 1] + (road.points[i] - road.points[i - 1]).flat().length();
        std::vector<Vec3> normal(n);
        for (size_t i = 0; i < n; ++i) {
            const Vec3 d = unitFlat(road.points[std::min(i + 1, n - 1)] - road.points[i == 0 ? 0 : i - 1]);
            normal[i] = {-d.y, d.x, Fixed::zero()};
        }
        std::vector<uint8_t> pinned(n, 0);
        for (size_t i = 0; i < n; ++i) {
            pinned[i] = junction[base[ri] + i] || arcs[i] < m(30) || arcs[n - 1] - arcs[i] < m(30);
            for (const Crossing& c : crossings_)
                if ((road.points[i] - c.pos).flat().length() < max(c.reachBehind(), c.reachAhead()) + m(80)) pinned[i] = 1;
            for (const City& city : cities_)
                if ((road.points[i] - city.pos).flat().length() < city.radius + m(24)) pinned[i] = 1;
        }
        std::vector<Fixed> pushLeft(n), pushRight(n);  // metres to move left (stone on the right), and right
        std::set<std::pair<int64_t, int64_t>> seen;
        Fixed hwMax;
        for (const Fixed& w : road.widths) hwMax = max(hwMax, w);
        const Fixed span = hwMax + m(9);
        for (size_t i = 0; i + 1 < n; ++i) {
            const Vec3 &a = road.points[i], &b = road.points[i + 1];
            const int64_t gx0 = ((min(a.x, b.x) - span) / Region::kSceneryStep).floorToInt() - 1, gx1 = ((max(a.x, b.x) + span) / Region::kSceneryStep).floorToInt() + 1;
            const int64_t gy0 = ((min(a.y, b.y) - span) / Region::kSceneryStep).floorToInt() - 1, gy1 = ((max(a.y, b.y) + span) / Region::kSceneryStep).floorToInt() + 1;
            for (int64_t gy = gy0; gy <= gy1; ++gy)
                for (int64_t gx = gx0; gx <= gx1; ++gx) {
                    if (!seen.insert({gx, gy}).second) continue;
                    const Region::SceneryPoint pt = Region::sceneryPoint(sceneSeed, gx, gy);
                    // Cheap first: only a point that could be a rock on plains or in a wood is worth sampling the land for.
                    Obstacle o;
                    bool rocky = false;
                    for (Ground g : {Ground::Plains, Ground::Forest}) rocky = rocky || (Region::sceneryAt(sceneSeed, pt, g, false, Fixed::zero(), o) && (o.kind == Obstacle::Kind::Rock || o.kind == Obstacle::Kind::Boulder));
                    if (!rocky) continue;
                    const Sample s = sample(pt.x, pt.y);
                    Fixed surface;
                    if (waterSurfaceOf(s, surface)) continue;
                    const Ground g = landGround(pt.x, pt.y, s.height, s.mountain, m(60));
                    if (!Region::sceneryAt(sceneSeed, pt, g, false, s.height, o) || !Region::worthDodging(o)) continue;
                    // Where on the road it stands, and on which side.
                    size_t seg = 0;
                    Fixed t;
                    const Fixed d = polylineDist(road.points, pt.x, pt.y, span, seg, t);
                    if (d >= span) continue;
                    const Fixed hw = road.widths[seg] + (road.widths[seg + 1] - road.widths[seg]) * t;
                    const Fixed need = hw + o.radius + r(Region::kDodgeMarginTenths, 10);
                    if (d >= need) continue;
                    const Vec3 closest = road.points[seg] + (road.points[seg + 1] - road.points[seg]) * t;
                    const Vec3 side = unitFlat(road.points[seg + 1] - road.points[seg]);
                    const bool onLeft = side.x * (pt.y - closest.y) - side.y * (pt.x - closest.x) > Fixed::zero();
                    const Fixed push = need - d + r(1, 10);
                    const Fixed along = arcs[seg] + (arcs[seg + 1] - arcs[seg]) * t;
                    const Fixed half = max(m(14), need * r(12, 5));
                    // Within the bump: pinned points or water spoil it, and the stone is only cleared off the road then.
                    bool spoiled = false;
                    for (size_t k = 0; k < n && !spoiled; ++k) {
                        if (arcs[k] < along - half || arcs[k] > along + half) continue;
                        spoiled = pinned[k];
                        const Fixed w = bump((arcs[k] - along) / half) * push;
                        const Vec3 moved = road.points[k] + normal[k] * (onLeft ? -w : w);
                        Fixed deck;
                        spoiled = spoiled || (waterSurfaceOf(sample(moved.x, moved.y), surface) && !onBridge(moved.x, moved.y, deck));
                    }
                    if (spoiled) continue;
                    for (size_t k = 0; k < n; ++k) {
                        if (arcs[k] < along - half || arcs[k] > along + half) continue;
                        const Fixed w = bump((arcs[k] - along) / half) * push;
                        Fixed& slot = onLeft ? pushRight[k] : pushLeft[k];  // a stone on the left is passed on the right
                        slot = max(slot, w);
                    }
                }
        }
        for (size_t k = 0; k < n; ++k) {
            const Fixed shift = pushLeft[k] - pushRight[k];
            if (shift != Fixed::zero()) road.points[k] = road.points[k] + normal[k] * shift;
        }
    }
    // The road runs onto each bridge along its axis, centred on the deck and the ramps, and straight on it for kApproach metres
    // before each end (so the way on is a straight line along the deck's width, not a turn at its foot): the road's points over
    // the span and that far out are drawn onto the axis, the pull fading out over as far again. Only the road that crosses it
    // (one with a point within 6 m of the axis over the span); a point the pull would put in water is left where it was.
    constexpr int64_t kApproach = 32;
    // The road ends that lie on another road (a junction), to put back on it once the pull has moved either.
    struct Attach {
        size_t road;
        int side;  // 0 the first point, 1 the last
        size_t onto;
    };
    std::vector<Attach> attached;
    for (size_t ri = 0; ri < roadCount; ++ri) {
        if (roads_[ri].points.size() < 2) continue;  // (a short road, a gate's last stretch to a road beside it, joins as any other)
        bool atGate = false;  // (a gate road's first point stays at its gate)
        for (size_t gr : gateRoad) atGate = atGate || gr == ri;
        for (int side = 0; side < 2; ++side) {
            if (side == 0 && atGate) continue;
            const Vec3 end = side ? roads_[ri].points.back() : roads_[ri].points.front();
            for (size_t oi = 0; oi < roadCount; ++oi) {
                size_t sg = 0;
                Fixed u;
                if (oi != ri && polylineDist(roads_[oi].points, end.x, end.y, m(4), sg, u) < m(4)) {
                    attached.push_back({ri, side, oi});
                    break;
                }
            }
        }
    }
    for (const Crossing& c : crossings_) {
        if (c.kind != Crossing::Kind::Bridge) continue;
        const Vec3 across{-c.dir.y, c.dir.x, Fixed::zero()};
        for (Polyline& road : roads_) {
            bool over = false;
            for (const Vec3& p : road.points) {
                const Fixed along = (p - c.pos).flat().dot(c.dir), side = (p - c.pos).flat().dot(across);
                over = over || (along > -c.reachBehind() && along < c.reachAhead() && abs(side) < m(6));
            }
            if (!over) continue;
            std::vector<std::pair<Vec3, Vec3>> moves;  // where a point was, and where it went
            // A road laid from a town's gate stays at the gate: the pull fades in over its first 40 m.
            bool fromGate = false;
            for (size_t gr : gateRoad) fromGate = fromGate || (gr != SIZE_MAX && &roads_[gr] == &road);
            Fixed arcFromStart;
            Vec3 previous = road.points.front();
            for (Vec3& p : road.points) {
                arcFromStart += (p - previous).flat().length();
                previous = p;
                const Fixed along = (p - c.pos).flat().dot(c.dir), side = (p - c.pos).flat().dot(across);
                if (abs(side) > m(60) || along < -c.reachBehind() - m(2 * kApproach) || along > c.reachAhead() + m(2 * kApproach)) continue;
                const Fixed outside = max(max(-c.reachBehind() - along, along - c.reachAhead()) - m(kApproach), Fixed::zero());
                Fixed pull = Fixed::one() - noise::smoothstep(Fixed::zero(), m(kApproach), outside);
                if (fromGate) pull = pull * noise::smoothstep(Fixed::zero(), m(40), arcFromStart);
                const Vec3 moved = p - across * (side * pull);
                Fixed surface, deck;
                if (waterSurfaceOf(sample(moved.x, moved.y), surface) && !onBridge(moved.x, moved.y, deck)) continue;
                moves.push_back({p, moved});
                p = moved;
            }
        }
    }
    // Each junction end back on the road it joined, the road's last 6 points following by less and less.
    for (const Attach& a : attached) {
        Polyline& road = roads_[a.road];
        const size_t n = road.points.size();
        Vec3& end = a.side ? road.points.back() : road.points.front();
        size_t sg = 0;
        Fixed u;
        if (polylineDist(roads_[a.onto].points, end.x, end.y, m(60), sg, u) >= m(60)) continue;
        const std::vector<Vec3>& on = roads_[a.onto].points;
        const Vec3 target = on[sg] + (on[sg + 1] - on[sg]) * u;
        const Vec3 delta = target - end;
        const size_t follow = std::min<size_t>(7, n - 1);  // (never the road's other end)
        for (size_t k = 0; k < follow; ++k) {
            Vec3& q = a.side ? road.points[n - 1 - k] : road.points[k];
            q = q + delta * Fixed::fromRatio(static_cast<int64_t>(follow - k), static_cast<int64_t>(follow));
        }
    }
    // The bridges carry the road's width.
    for (Crossing& c : crossings_) {
        if (c.kind != Crossing::Kind::Bridge) continue;
        Fixed best = m(30), hw = c.halfWidth;
        for (const Polyline& road : roads_) {
            size_t seg = 0;
            Fixed t;
            const Fixed d = polylineDist(road.points, c.pos.x, c.pos.y, best, seg, t);
            if (d < best) {
                best = d;
                hw = max(hw, road.widths[seg] + (road.widths[seg + 1] - road.widths[seg]) * t);
            }
        }
        c.halfWidth = hw;
    }
}

}  // namespace illyria::sim
