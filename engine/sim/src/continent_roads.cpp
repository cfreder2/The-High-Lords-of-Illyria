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
#include <cstdint>
#include <queue>

#include "continent_shapes.h"
#include "illyria/sim/continent.h"

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
    // A road's end snapped onto the road that is near it (the one that took the path to it).
    const auto snap = [&](Vec3& end) {
        Fixed best = m(kCell * 4);
        Vec3 join = end;
        bool found = false;
        for (const Polyline& rd : roads_) {
            size_t s = 0;
            Fixed u;
            const Fixed d = polylineDist(rd.points, end.x, end.y, m(kCell * 4), s, u);
            if (d < best) {
                best = d;
                join = rd.points[s] + (rd.points[s + 1] - rd.points[s]) * u;
                found = true;
            }
        }
        if (found) end = join;
    };
    // A smooth curve through the control points, a point every 12 m.
    const auto lay = [&](const std::vector<Vec3>& ctrl) {
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
        road.widths.assign(road.points.size(), road.halfWidth);
        if (road.points.size() > 1) roads_.push_back(std::move(road));
    };
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
        if (!path.empty()) snap(ctrl.back());
        lay(ctrl);
    }
    // The links between the networks, from one road to another.
    for (const std::vector<Vec3>& path : links_) {
        std::vector<Vec3> ctrl;
        for (size_t i = 0; i < path.size(); ++i)
            if (i == 0 || i + 1 == path.size() || i % 2 == 1) ctrl.push_back(path[i]);
        snap(ctrl.front());
        snap(ctrl.back());
        lay(ctrl);
    }
    approach_.clear();
    approachOrder_.clear();
    links_.clear();
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
}

}  // namespace illyria::sim
