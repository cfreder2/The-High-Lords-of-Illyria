// Measures the continent's river water (docs/TERRAIN_AUDIT.md): is every river's surface below its banks, flat across a
// section, and gently falling along its course? Doubles; it reports and never feeds the sim. Shared by
// tools/terrainaudit.cpp and tests/test_terrain_audit.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

#include "illyria/sim/continent.h"

namespace wateraudit {

struct RiverReport {
    size_t river = 0;
    int sections = 0;
    int walls = 0;          // sections whose outermost water is deeper than kWallDepth: the edge is a cliff of water
    double tallestWall = 0; // metres
    int steep = 0;          // course steps (kStep m) falling faster than kMaxFall
    double steepest = 0;    // metres of fall per metre of course
    double biggestStep = 0; // largest fall between two neighbouring sections, metres
    int uphill = 0;         // steps where the surface rises downstream
};

constexpr double kWallDepth = 0.75;  // the carved bank itself shelves about 0.5 m a metre at the rim; more than this is a cliff of water
constexpr double kStep = 20.0;
constexpr double kMaxFall = 0.11;    // a gentle slope: 1 in 9 (the sim holds 1 in 10)

inline std::vector<RiverReport> rivers(const illyria::sim::Continent& c) {
    using namespace illyria::sim;
    std::vector<RiverReport> out;
    const auto F = [](double v) { return Fixed::fromRatio(static_cast<int64_t>(std::llround(v * 1000)), 1000); };
    for (size_t ri = 0; ri < c.rivers().size(); ++ri) {
        const Continent::River& rv = c.rivers()[ri];
        RiverReport rep;
        rep.river = ri;
        double prev = 0, prevLen = 1;
        for (size_t i = 0; i + 1 < rv.points.size(); i += 1) {
            const double x = rv.points[i].x.toDouble(), y = rv.points[i].y.toDouble();
            const double dx = rv.points[i + 1].x.toDouble() - x, dy = rv.points[i + 1].y.toDouble() - y;
            const double len = std::sqrt(dx * dx + dy * dy);
            if (len < 1e-6) continue;
            const double nx = -dy / len, ny = dx / len;
            const double hw = rv.widths.empty() ? rv.halfWidth.toDouble() : rv.widths[i].toDouble();
            double rim = -1;
            bool sea = false;
            for (int side = -1; side <= 1; side += 2) {
                double last = -1;
                for (double o = 0; o <= hw + 14; o += 1.0) {
                    const Continent::Sample s = c.sample(F(x + nx * o * side), F(y + ny * o * side));
                    Fixed w;
                    if (!c.waterSurfaceOf(s, w)) break;
                    if (s.inLake || s.land < Fixed::zero()) { sea = true; break; }
                    last = w.toDouble() - s.height.toDouble();
                }
                rim = std::max(rim, last);
            }
            const double z = rv.points[i].z.toDouble();
            if (sea) { prev = z; prevLen = len; continue; }  // a mouth: the lake or the sea carries on
            ++rep.sections;
            if (rim > kWallDepth) {
                ++rep.walls;
                rep.tallestWall = std::max(rep.tallestWall, rim);
            }
            if (i > 0) {
                const double fall = (prev - z) / prevLen;
                rep.steepest = std::max(rep.steepest, fall);
                rep.biggestStep = std::max(rep.biggestStep, prev - z);
                if (fall > kMaxFall) ++rep.steep;
                if (fall < -0.001) ++rep.uphill;
            }
            prev = z;
            prevLen = len;
        }
        out.push_back(rep);
    }
    return out;
}

}  // namespace wateraudit
