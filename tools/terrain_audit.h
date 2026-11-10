// Measures a generated castle and the roads round it, for the audit tool
// (tools/terrainaudit.cpp) and the regression tests (tests/test_terrain_audit.cpp).
// Geometry in doubles: it reports and never feeds the sim.
#pragma once

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <vector>

#include "illyria/sim/continent.h"

namespace illyria::sim::audit {

struct P {
    double x = 0, y = 0;
};
inline P pt(const Vec3& v) { return {v.x.toDouble(), v.y.toDouble()}; }
inline P operator+(P a, P b) { return {a.x + b.x, a.y + b.y}; }
inline P operator-(P a, P b) { return {a.x - b.x, a.y - b.y}; }
inline P operator*(P a, double k) { return {a.x * k, a.y * k}; }
inline double dot(P a, P b) { return a.x * b.x + a.y * b.y; }
inline double len(P a) { return std::sqrt(dot(a, a)); }
inline P unit(P a) { const double l = len(a); return l > 0 ? a * (1 / l) : P{}; }

inline Vec3 unitVec(const Vec3& v) {
    const Fixed l = v.flat().length();
    return l > Fixed::zero() ? v.flat() / l : Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()};
}
inline double segDist(P p, P a, P b) {
    const P ab = b - a;
    const double l2 = dot(ab, ab);
    const double t = l2 > 0 ? std::clamp(dot(p - a, ab) / l2, 0.0, 1.0) : 0.0;
    return len(p - (a + ab * t));
}
inline bool segCross(P a, P b, P c, P d) {
    const auto cr = [](P o, P p, P q) { return (p.x - o.x) * (q.y - o.y) - (p.y - o.y) * (q.x - o.x); };
    const double d1 = cr(a, b, c), d2 = cr(a, b, d), d3 = cr(c, d, a), d4 = cr(c, d, b);
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}
inline bool inside(const std::vector<P>& poly, P p) {
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
        if ((poly[i].y > p.y) != (poly[j].y > p.y) && p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y) + poly[i].x) in = !in;
    return in;
}

// Whether a point lies on a road, as the ground there says (Continent::landGround).
inline bool onRoad(const Continent& c, P p) {
    const Continent::Sample s = c.sample(Fixed::fromRatio(static_cast<int64_t>(p.x * 1000), 1000), Fixed::fromRatio(static_cast<int64_t>(p.y * 1000), 1000));
    return s.roadDist < Fixed::fromInt(3);
}
inline Fixed fx(double v) { return Fixed::fromRatio(static_cast<int64_t>(std::llround(v * 1000)), 1000); }
inline bool wetAt(const Continent& c, P p) {
    Fixed surface;
    Fixed deck;
    return c.waterSurfaceOf(c.sample(fx(p.x), fx(p.y)), surface) && !c.onBridge(fx(p.x), fx(p.y), deck);
}
// Distance from a point to the nearest road's middle line, less that road's half width there.
inline double roadGap(const Continent& c, P p) {
    double best = 1e9;
    for (const Polyline& rd : c.roads())
        for (size_t i = 0; i + 1 < rd.points.size(); ++i) {
            const double w = rd.widthAt(i, Fixed::zero()).toDouble();
            best = std::min(best, segDist(p, pt(rd.points[i]), pt(rd.points[i + 1])) - w);
        }
    return best;
}

struct Gate {
    P mouth, out, mid;
    double thick = 0;
};
inline Gate gateOf(const Continent::Castle::Ring& ring) {
    const size_t n = ring.corners.size();
    const P a = pt(ring.corners[ring.gateEdge]), b = pt(ring.corners[(ring.gateEdge + 1) % n]);
    Gate g;
    g.mid = (a + b) * 0.5;
    P centre{};
    for (const Vec3& q : ring.corners) centre = centre + pt(q) * (1.0 / static_cast<double>(n));
    P nrm = unit(P{-(b - a).y, (b - a).x});
    if (dot(nrm, g.mid - centre) < 0) nrm = nrm * -1;
    g.out = nrm;
    g.thick = ring.thick.toDouble();
    g.mouth = g.mid + nrm * (g.thick * 0.5);
    return g;
}
// A place's gate: a walled town's own; a village's is where its lane meets its green (no wall, so no thickness).
inline Gate gateOf(const Continent::City& city) {
    if (city.walled) return gateOf(city.castle.rings.front());
    Gate g;
    g.out = unit(pt(city.castle.keepAxis));
    g.mid = g.mouth = pt(city.pos) + g.out * city.castle.courtyardRadius.toDouble();
    return g;
}

// Where a road runs through water that is neither a bridge nor a ford (points every 4 m: its first such point, and how many).
struct WetRun {
    size_t road = 0;
    P first;
    int points = 0;
};
inline std::vector<WetRun> wetRoads(const Continent& c) {
    std::vector<WetRun> out;
    for (size_t ri = 0; ri < c.roads().size(); ++ri) {
        const Polyline& rd = c.roads()[ri];
        WetRun run{ri, {}, 0};
        for (size_t i = 0; i + 1 < rd.points.size(); ++i) {
            const P a = pt(rd.points[i]), b = pt(rd.points[i + 1]);
            const int steps = std::max(1, static_cast<int>(len(b - a) / 4));
            for (int k = 0; k < steps; ++k) {
                const P p = a + (b - a) * (static_cast<double>(k) / steps);
                const Continent::Sample s = c.sample(fx(p.x), fx(p.y));
                Fixed surface;
                if (c.waterSurfaceOf(s, surface) && !s.atFord && !wetAt(c, p) == false) {
                    // Deep enough to matter: more than knee deep, and not a ford.
                    if (surface - s.height > Fixed::fromRatio(1, 2)) {
                        if (!run.points) run.first = p;
                        ++run.points;
                    }
                }
            }
        }
        if (run.points) out.push_back(run);
    }
    return out;
}


// The road network as a graph: road points within kMerge of another road's (or a road's own, apart along the road)
// are one node, and each run of road between two nodes is an edge. Used to find the dead-end stubs and the little
// loops a junction can leave: a stub is a free end (not a gate, not the coast or the map's edge) with less than
// kStub metres of road behind it, a loop a cycle shorter than kLoop metres.
struct RoadGraph {
    struct Edge {
        size_t a, b;
        double length;
    };
    std::vector<P> at;        // a node's first point
    std::vector<int> gate;    // 1 where a castle's gate is
    std::vector<int> free;    // 1 where a road ends here on open land (no coast, no map edge)
    std::vector<Edge> edges;
    std::vector<size_t> roadOfEdge;
};
struct RoadFlaw {
    P at;
    double length = 0;
};
struct RoadFlaws {
    std::vector<RoadFlaw> stubs, loops;
};
inline RoadFlaws roadFlaws(const Continent& c, double merge = 5.0, double kStub = 100.0, double kLoop = 150.0) {
    struct Pt {
        P p;
        size_t road, index;
    };
    std::vector<Pt> pts;
    std::vector<std::vector<size_t>> ofRoad(c.roads().size());
    for (size_t ri = 0; ri < c.roads().size(); ++ri) {
        const Polyline& rd = c.roads()[ri];
        for (size_t i = 0; i + 1 < rd.points.size(); ++i) {
            const P a = pt(rd.points[i]), b = pt(rd.points[i + 1]);
            const int steps = std::max(1, static_cast<int>(std::ceil(len(b - a) / 3.0)));
            for (int k = 0; k < steps; ++k) {
                ofRoad[ri].push_back(pts.size());
                pts.push_back({a + (b - a) * (static_cast<double>(k) / steps), ri, ofRoad[ri].size() - 1});
            }
        }
        if (!rd.points.empty()) {
            ofRoad[ri].push_back(pts.size());
            pts.push_back({pt(rd.points.back()), ri, ofRoad[ri].size() - 1});
        }
    }
    // Cluster: points of different roads within `merge`, or of one road within `merge` but far along it (a loop back on itself
    // is a loop, not one place), are one node.
    std::vector<size_t> node(pts.size());
    for (size_t i = 0; i < node.size(); ++i) node[i] = i;
    const auto find = [&](size_t i) {
        while (node[i] != i) i = node[i] = node[node[i]];
        return i;
    };
    std::vector<std::vector<size_t>> grid;
    const double cell = merge;
    const int gw = static_cast<int>(Continent::kWidth / cell) + 2, gh = static_cast<int>(Continent::kHeight / cell) + 2;
    grid.assign(static_cast<size_t>(gw) * gh, {});
    const auto cellOf = [&](P p) { return std::pair<int, int>{std::clamp(static_cast<int>(p.x / cell), 0, gw - 1), std::clamp(static_cast<int>(p.y / cell), 0, gh - 1)}; };
    for (size_t i = 0; i < pts.size(); ++i) {
        const auto [cx, cy] = cellOf(pts[i].p);
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int X = cx + dx, Y = cy + dy;
                if (X < 0 || Y < 0 || X >= gw || Y >= gh) continue;
                for (size_t j : grid[static_cast<size_t>(Y) * gw + X]) {
                    if (len(pts[i].p - pts[j].p) > merge) continue;
                    const bool same = pts[i].road == pts[j].road;
                    if (same && std::abs(static_cast<long>(pts[i].index) - static_cast<long>(pts[j].index)) < 14) continue;  // (a bend's two legs are near each other: 40 m along is another stretch)
                    node[find(i)] = find(j);
                }
            }
        grid[static_cast<size_t>(cellOf(pts[i].p).second) * gw + cellOf(pts[i].p).first].push_back(i);
    }
    std::vector<P> gates;
    for (const Continent::City& city : c.cities()) gates.push_back(gateOf(city).mid);
    // Nodes and edges.
    std::map<size_t, size_t> id;
    RoadGraph g;
    const auto nodeId = [&](size_t i) {
        const size_t r = find(i);
        const auto it = id.find(r);
        if (it != id.end()) return it->second;
        id[r] = g.at.size();
        g.at.push_back(pts[i].p);
        g.gate.push_back(0);
        g.free.push_back(0);
        return g.at.size() - 1;
    };
    for (size_t ri = 0; ri < ofRoad.size(); ++ri) {
        if (ofRoad[ri].empty()) continue;
        size_t prev = nodeId(ofRoad[ri].front());
        double run = 0;
        for (size_t k = 1; k < ofRoad[ri].size(); ++k) {
            run += len(pts[ofRoad[ri][k]].p - pts[ofRoad[ri][k - 1]].p);
            const size_t cur = nodeId(ofRoad[ri][k]);
            if (cur == prev && k + 1 < ofRoad[ri].size()) continue;
            if (cur != prev) {
                g.edges.push_back({prev, cur, run});
                g.roadOfEdge.push_back(ri);
            }
            run = 0;
            prev = cur;
        }
    }
    for (size_t i = 0; i < pts.size(); ++i)
        for (const P& gp : gates)
            if (len(pts[i].p - gp) < 3.0) g.gate[nodeId(i)] = 1;
    // A road's two ends: free unless at a gate, near the coast or the map's edge.
    for (const std::vector<size_t>& pr : ofRoad) {
        if (pr.empty()) continue;
        for (size_t end : {pr.front(), pr.back()}) {
            const P p = pts[end].p;
            const Continent::Sample s = c.sample(fx(p.x), fx(p.y));
            const bool edge = p.x < 40 || p.y < 40 || p.x > Continent::kWidth - 40 || p.y > Continent::kHeight - 40;
            if (!edge && s.land > Fixed::fromInt(40)) g.free[nodeId(end)] = 1;
        }
    }
    RoadFlaws out;
    std::vector<int> degree(g.at.size(), 0);
    std::vector<std::vector<size_t>> adj(g.at.size());
    for (size_t e = 0; e < g.edges.size(); ++e) {
        ++degree[g.edges[e].a];
        ++degree[g.edges[e].b];
        adj[g.edges[e].a].push_back(e);
        adj[g.edges[e].b].push_back(e);
    }
    for (size_t n = 0; n < g.at.size(); ++n)
        if (degree[n] == 1 && g.free[n] && !g.gate[n]) {
            // The run of road behind the free end, up to the first junction or gate.
            double total = 0;
            size_t cur = n, from = SIZE_MAX;
            while (true) {
                size_t next = SIZE_MAX, via = 0;
                for (size_t e : adj[cur]) {
                    if (e == from) continue;
                    next = g.edges[e].a == cur ? g.edges[e].b : g.edges[e].a;
                    via = e;
                }
                if (next == SIZE_MAX) break;
                total += g.edges[via].length;
                from = via;
                cur = next;
                if (degree[cur] != 2 || g.gate[cur]) break;
            }
            // (A whole road from a gate to nowhere, a town on an isle, is no spur off anything.)
            if (total < kStub && degree[cur] > 1) out.stubs.push_back({g.at[n], total});
        }
    // Loops: the shortest way back round each edge.
    for (size_t e = 0; e < g.edges.size(); ++e) {
        const size_t s = g.edges[e].a, t = g.edges[e].b;
        std::vector<double> dist(g.at.size(), 1e18);
        std::vector<std::pair<double, size_t>> heap{{0.0, s}};
        dist[s] = 0;
        while (!heap.empty()) {
            std::pop_heap(heap.begin(), heap.end(), std::greater<>());
            const auto [d, u] = heap.back();
            heap.pop_back();
            if (d > dist[u] || d >= kLoop) continue;
            for (size_t f : adj[u]) {
                if (f == e) continue;
                const size_t v = g.edges[f].a == u ? g.edges[f].b : g.edges[f].a;
                if (d + g.edges[f].length < dist[v]) {
                    dist[v] = d + g.edges[f].length;
                    heap.push_back({dist[v], v});
                    std::push_heap(heap.begin(), heap.end(), std::greater<>());
                }
            }
        }
        const double around = dist[t] + g.edges[e].length;
        if (dist[t] < 1e17 && around < kLoop) out.loops.push_back({g.at[s], around});
    }
    return out;
}

// Road classes by width (docs/TERRAIN_AUDIT.md): the metres of road that are a single lane (half width under 2.9 m), a main
// road (2.9 to 4.2) and a trunk road (3 or 4 lanes: 4.2 and over), the widest and narrowest, and the steepest change of
// half width along a road (metres per metre).
struct RoadClasses {
    double metres[3] = {0, 0, 0};
    double widest = 0, narrowest = 1e9, steepest = 0, total = 0;
    size_t trunkRoads = 0, roads = 0;
};
inline RoadClasses roadClasses(const Continent& c) {
    RoadClasses out;
    for (const Polyline& rd : c.roads()) {
        bool trunk = false;
        ++out.roads;
        for (size_t i = 0; i + 1 < rd.points.size(); ++i) {
            const double l = len(pt(rd.points[i + 1]) - pt(rd.points[i])), w = rd.widths[i].toDouble(), w2 = rd.widths[i + 1].toDouble();
            out.metres[w >= 4.2 ? 2 : w >= 2.9 ? 1 : 0] += l;
            trunk = trunk || w >= 4.2;
            out.total += l;
            out.widest = std::max(out.widest, w);
            out.narrowest = std::min(out.narrowest, w);
            if (l > 0.5) out.steepest = std::max(out.steepest, std::abs(w2 - w) / l);
        }
        out.trunkRoads += trunk;
    }
    return out;
}

struct CastleReport {
    size_t towers = 0;
    bool gateRoad = false;     // a road cell within 4 m of the gate's mouth, straight out from it
    double spur = 0;           // metres from the mouth to the nearest road's edge
    bool linked = false;       // the road at the gate runs on to the network (a road reaches the gate and the spur has no gap)
    bool gateWet = false;      // water at or just before the gate
    bool roadThroughWall = false;  // a road's middle line crosses the outer wall
    int overlook = 0;          // embrasures on the outer ring with a clear shot down onto the approach to the gate
    double slope = 0;
    int crowded = 0;           // pairs of towers whose faces are within 2 m of each other
    double minGap = 1e9;       // metres between the nearest two towers' faces
    double perimeter = 0;      // the outer wall's length
    int sharpCorners = 0;      // outer-ring corners that turn the wall less than 65 degrees
};

// The embrasures of ring 0 that can shoot onto the ground `ahead` metres in front of its gate.
inline int overlookAt(const Continent&, const Continent::City& city, double ahead, double range = 55) {
    const Continent::Castle& k = city.castle;
    if (!city.walled) return 0;  // no wall to shoot from
    const Continent::Castle::Ring& ring = k.rings[0];
    const Gate g = gateOf(ring);
    const P target = g.mouth + g.out * ahead;
    const size_t n = ring.corners.size();
    std::vector<P> poly;
    for (const Vec3& q : ring.corners) poly.push_back(pt(q));
    const double walk = ring.walk().toDouble();
    const double ground = city.pos.z.toDouble();
    int count = 0;
    for (size_t e = 0; e < n; ++e) {
        const P a = poly[e], b = poly[(e + 1) % n];
        const P along = unit(b - a);
        P nrm{-along.y, along.x};
        P centre{};
        for (const P& q : poly) centre = centre + q * (1.0 / static_cast<double>(n));
        if (dot(nrm, (a + b) * 0.5 - centre) < 0) nrm = nrm * -1;
        for (const Fixed& cr : ring.crenels(e)) {
            const P at = a + along * cr.toDouble() + nrm * (g.thick * 0.5);
            const P to = target - at;
            if (len(to) > range || len(to) < 1) continue;
            if (dot(unit(to), nrm) < 0.5) continue;  // within 60 degrees of straight out of the embrasure
            bool clear = true;
            for (size_t j = 0; j < n && clear; ++j)
                if (j != e && segCross(at, target, poly[j], poly[(j + 1) % n])) clear = false;
            for (const Continent::Castle::Tower& t : k.towers) {
                if (!clear) break;
                if (segDist(pt(t.pos), at, target) < t.radius.toDouble() + 0.2 && len(pt(t.pos) - at) > t.radius.toDouble() + 0.5) clear = false;
            }
            if (clear) ++count;
        }
    }
    (void)walk;
    (void)ground;
    return count;
}

inline CastleReport inspect(const Continent& c, const Continent::City& city) {
    CastleReport r;
    const Continent::Castle& k = city.castle;
    r.towers = k.towers.size();
    const Gate g = gateOf(city);
    for (double d = 0; d <= 4.0 && !r.gateRoad; d += 1.0) r.gateRoad = onRoad(c, g.mouth + g.out * d);
    r.spur = std::max(0.0, roadGap(c, g.mouth));
    r.linked = r.gateRoad && r.spur < 4.0;
    r.gateWet = wetAt(c, g.mouth + g.out * 2.0) || wetAt(c, g.mouth + g.out * 8.0);
    if (!city.walled) return r;  // a village: its lane, and nothing of walls and towers
    const Continent::Castle::Ring& ring = k.rings[0];
    std::vector<P> poly;
    for (const Vec3& q : ring.corners) poly.push_back(pt(q));
    for (const Polyline& rd : c.roads())
        for (size_t i = 0; i + 1 < rd.points.size() && !r.roadThroughWall; ++i)
            for (size_t j = 0; j < poly.size(); ++j)
                if (segCross(pt(rd.points[i]), pt(rd.points[i + 1]), poly[j], poly[(j + 1) % poly.size()])) {
                    // A road may cross the wall only at the gate.
                    const P hit = (poly[j] + poly[(j + 1) % poly.size()]) * 0.5;
                    if (j != ring.gateEdge || len(hit - g.mid) > 40) r.roadThroughWall = true;
                }
    r.overlook = overlookAt(c, city, 10);
    for (size_t i = 0; i < k.towers.size(); ++i)
        for (size_t j = i + 1; j < k.towers.size(); ++j) {
            const double gap = len(pt(k.towers[i].pos) - pt(k.towers[j].pos)) - k.towers[i].radius.toDouble() - k.towers[j].radius.toDouble();
            r.minGap = std::min(r.minGap, gap);
            r.crowded += gap < 2.0;
        }
    for (size_t i = 0; i < poly.size(); ++i) {
        const P a = poly[(i + poly.size() - 1) % poly.size()], b = poly[i], d = poly[(i + 1) % poly.size()];
        r.perimeter += len(d - b);
        r.sharpCorners += dot(unit(b - a), unit(d - b)) > std::cos(65.0 * 3.14159265 / 180);
    }
    r.slope = 0;
    for (const P& q : poly) {
        const double h = c.heightAt(fx(q.x), fx(q.y)).toDouble();
        r.slope = std::max(r.slope, std::abs(h - city.pos.z.toDouble()) / std::max(1.0, len(q - pt(city.pos))));
    }
    return r;
}

}  // namespace illyria::sim::audit
