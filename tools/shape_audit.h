// How natural are the continent's lakes and islands (docs/TERRAIN_AUDIT.md)? Each body's outline is rasterised from the
// sim's own samples and compared with the ellipse of the same area, centre and spread (its second moments): a drawn
// oval scores 0, a wandering shore scores well above. Doubles; reports only. Shared by tools/terrainaudit.cpp and
// tests/test_terrain_audit.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include "illyria/sim/continent.h"

namespace shapeaudit {

struct Shape {
    std::string name;
    bool lake = true;
    double area = 0;        // m^2
    double deviation = 0;   // |body xor its equivalent ellipse| / body area; an ellipse is 0
    double solidity = 0;    // body area / convex hull area; a convex shape is 1
    double lobes = 0;       // std deviation of the radius about the centroid, over its mean; a circle is 0
};

// The body: every grid cell (of `cell` metres, in the window about (cx, cy)) for which `in` holds, kept if it joins the
// cell at the centre.
inline Shape measure(const std::string& name, bool lake, double cx, double cy, double reach, double cell, const std::function<bool(double, double)>& in) {
    const int n = static_cast<int>(2 * reach / cell) + 1;
    std::vector<char> mask(static_cast<size_t>(n) * n), seen(mask.size(), 0);
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) mask[static_cast<size_t>(j) * n + i] = in(cx - reach + i * cell, cy - reach + j * cell);
    // The component nearest the centre.
    int best = -1;
    double bd = 1e18;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i)
            if (mask[static_cast<size_t>(j) * n + i]) {
                const double d = std::hypot(i * cell - reach, j * cell - reach);
                if (d < bd) bd = d, best = j * n + i;
            }
    Shape out;
    out.name = name;
    out.lake = lake;
    if (best < 0) return out;
    std::vector<int> stack{best}, cells;
    seen[static_cast<size_t>(best)] = 1;
    while (!stack.empty()) {
        const int c = stack.back();
        stack.pop_back();
        cells.push_back(c);
        const int ci = c % n, cj = c / n;
        const int nb[4][2] = {{ci + 1, cj}, {ci - 1, cj}, {ci, cj + 1}, {ci, cj - 1}};
        for (const auto& q : nb)
            if (q[0] >= 0 && q[1] >= 0 && q[0] < n && q[1] < n) {
                const size_t k = static_cast<size_t>(q[1]) * n + q[0];
                if (mask[k] && !seen[k]) seen[k] = 1, stack.push_back(static_cast<int>(k));
            }
    }
    const double A = static_cast<double>(cells.size());
    double mx = 0, my = 0;
    for (int c : cells) mx += c % n, my += c / n;
    mx /= A, my /= A;
    double sxx = 0, syy = 0, sxy = 0;
    for (int c : cells) {
        const double dx = c % n - mx, dy = c / n - my;
        sxx += dx * dx, syy += dy * dy, sxy += dx * dy;
    }
    sxx /= A, syy /= A, sxy /= A;
    // The ellipse of the same second moments: semi axes 2 sqrt(eigenvalue).
    const double tr = sxx + syy, det = sxx * syy - sxy * sxy, disc = std::sqrt(std::max(0.0, tr * tr / 4 - det));
    const double l1 = tr / 2 + disc, l2 = std::max(1e-9, tr / 2 - disc);
    const double theta = 0.5 * std::atan2(2 * sxy, sxx - syy);
    const double a = 2 * std::sqrt(l1), b = 2 * std::sqrt(l2), ct = std::cos(theta), st = std::sin(theta);
    int xorCells = 0;
    for (int j = 0; j < n; ++j)
        for (int i = 0; i < n; ++i) {
            const double dx = i - mx, dy = j - my, u = (dx * ct + dy * st) / a, v = (-dx * st + dy * ct) / b;
            const bool inE = u * u + v * v < 1.0;
            const bool inB = seen[static_cast<size_t>(j) * n + i];
            xorCells += inE != inB;
        }
    out.area = A * cell * cell;
    out.deviation = xorCells / A;
    // Convex hull (monotone chain) over cell centres.
    std::vector<std::pair<int, int>> pts;
    for (int c : cells) pts.push_back({c % n, c / n});
    std::sort(pts.begin(), pts.end());
    std::vector<std::pair<int, int>> hull(2 * pts.size() + 2);
    size_t k = 0;
    const auto cross = [](const std::pair<int, int>& o, const std::pair<int, int>& p, const std::pair<int, int>& q) {
        return static_cast<long>(p.first - o.first) * (q.second - o.second) - static_cast<long>(p.second - o.second) * (q.first - o.first);
    };
    for (size_t i = 0; i < pts.size(); ++i) {
        while (k >= 2 && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        hull[k++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = k + 1; i-- > 0;) {
        while (k >= t && cross(hull[k - 2], hull[k - 1], pts[i]) <= 0) --k;
        hull[k++] = pts[i];
    }
    double hullArea = 0;
    for (size_t i = 0; i + 1 < k; ++i) hullArea += static_cast<double>(hull[i].first) * hull[i + 1].second - static_cast<double>(hull[i + 1].first) * hull[i].second;
    hullArea = std::abs(hullArea) / 2 + 0.5 * static_cast<double>(k);  // and half a cell about the rim
    out.solidity = std::min(1.0, A / std::max(hullArea, A));
    // Radius about the centroid by direction (24 bins): its spread over its mean.
    double rmax[24] = {};
    for (int c : cells) {
        const double dx = c % n - mx, dy = c / n - my;
        const int bin = static_cast<int>((std::atan2(dy, dx) + 3.14159265358979) / (2 * 3.14159265358979) * 24) % 24;
        rmax[bin] = std::max(rmax[bin], std::hypot(dx, dy));
    }
    double mean = 0, var = 0;
    for (double rr : rmax) mean += rr;
    mean /= 24;
    for (double rr : rmax) var += (rr - mean) * (rr - mean);
    out.lobes = mean > 0 ? std::sqrt(var / 24) / mean : 0;
    return out;
}

// Every lake (and pond), every isle, rock and spit of the lakes, and the Isle of the Seer in the sea.
inline std::vector<Shape> bodies(const illyria::sim::Continent& c, double cell = 2.0) {
    using namespace illyria::sim;
    std::vector<Shape> out;
    const auto F = [](double v) { return Fixed::fromRatio(static_cast<int64_t>(std::llround(v * 1000)), 1000); };
    for (const Continent::Area& lake : c.lakes()) {
        const double cx = lake.centre.x.toDouble(), cy = lake.centre.y.toDouble(), rx = lake.rx.toDouble(), ry = lake.ry.toDouble();
        const double reach = std::max(rx, ry) * 1.5;
        // The water of this lake only (a lake's window never holds another).
        out.push_back(measure(lake.name, true, cx, cy, reach, std::max(cell, reach / 400), [&](double x, double y) {
            const double dx = (x - cx) / rx, dy = (y - cy) / ry;
            if (dx * dx + dy * dy > 2.25) return false;
            const Continent::Sample s = c.sample(F(x), F(y));
            return s.inLake && s.height < s.lakeSurface;
        }));
    }
    const std::vector<Continent::Area> isles = c.isles();
    for (size_t i = 0; i < isles.size(); ++i) {
        const Continent::Area& a = isles[i];
        const double cx = a.centre.x.toDouble(), cy = a.centre.y.toDouble(), reach = std::max(a.rx, a.ry).toDouble() * 1.5;
        out.push_back(measure(a.name, false, cx, cy, reach, std::max(1.0, reach / 300), [&](double x, double y) {
            if (c.isleDistance(i, F(x), F(y)) >= Fixed::fromRatio(14, 10)) return false;
            const Continent::Sample s = c.sample(F(x), F(y));
            Fixed w;
            return !c.waterSurfaceOf(s, w);
        }));
    }
    for (const Continent::Label& l : c.labels())
        if (l.name == "Isle of the Seer")
            out.push_back(measure(l.name, false, l.pos.x.toDouble(), l.pos.y.toDouble(), 300, 3, [&](double x, double y) { return c.landform(F(x), F(y)).land > Fixed::zero(); }));
    return out;
}

}  // namespace shapeaudit
