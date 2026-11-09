// Measures a generated castle and the roads round it, for the audit tool
// (tools/terrainaudit.cpp) and the regression tests (tests/test_terrain_audit.cpp).
// Geometry in doubles: it reports and never feeds the sim.
#pragma once

#include <algorithm>
#include <cmath>
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
    const Continent::Castle::Ring& ring = k.rings[0];
    const Gate g = gateOf(ring);
    for (double d = 0; d <= 4.0 && !r.gateRoad; d += 1.0) r.gateRoad = onRoad(c, g.mouth + g.out * d);
    r.spur = std::max(0.0, roadGap(c, g.mouth));
    r.linked = r.gateRoad && r.spur < 4.0;
    r.gateWet = wetAt(c, g.mouth + g.out * 2.0) || wetAt(c, g.mouth + g.out * 8.0);
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
