#pragma once

// Geometry shared by continent.cpp and continent_relief.cpp: metres, map
// positions and distances to segments and polylines, all in fixed point.

#include <cstdint>
#include <vector>

#include "illyria/sim/continent.h"

namespace illyria::sim::continent_shapes {

constexpr Fixed m(int64_t v) { return Fixed::fromInt(v); }
constexpr Fixed r(int64_t num, int64_t den) { return Fixed::fromRatio(num, den); }
// The map is laid out at two ninths of the size it was first designed at, a
// third each way of the 8.5 x 10.7 km it was before One World (docs/MAP.md):
// every distance on the map's own scale (coasts, ranges, lakes, forests, the
// spacing of cities, noise wavelengths) goes through these, so the land keeps
// its shape. What a battle sees up close keeps its size: river and road
// widths, bridges, gates, ramps, houses, the ruins, the knolls and gorges.
constexpr int64_t scaledI(int64_t v) { return v * 2 / 9; }
constexpr Fixed scaled(int64_t v) { return Fixed::fromRatio(v * 2, 9); }

// A point given in per-mille of the continent's width and height.
inline Vec3 N(int64_t u, int64_t v) {
    return {Fixed::fromRatio(u * Continent::kWidth, 1000), Fixed::fromRatio(v * Continent::kHeight, 1000), Fixed::zero()};
}

inline Fixed segDistSq(Fixed px, Fixed py, const Vec3& a, const Vec3& b, Fixed& t) {
    const Fixed dx = b.x - a.x, dy = b.y - a.y;
    const Fixed len2 = dx * dx + dy * dy;
    t = Fixed::zero();
    if (len2 > Fixed::zero()) t = clamp(((px - a.x) * dx + (py - a.y) * dy) / len2, Fixed::zero(), Fixed::one());
    const Fixed cx = a.x + dx * t - px, cy = a.y + dy * t - py;
    return cx * cx + cy * cy;
}

// Nearest distance from (px, py) to a polyline, ignoring segments farther than `limit`.
// Also reports the segment index and parameter of the nearest point.
inline Fixed polylineDist(const std::vector<Vec3>& pts, Fixed px, Fixed py, Fixed limit, size_t& seg, Fixed& segT) {
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

inline Vec3 unitFlat(const Vec3& v) {
    const Fixed len = v.flat().length();
    return len > Fixed::zero() ? v.flat() / len : Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()};
}

inline Fixed bump(Fixed t) {
    const Fixed a = abs(t);
    if (a >= Fixed::one()) return Fixed::zero();
    const Fixed s = Fixed::one() - a * a;
    return s * s;
}

}  // namespace illyria::sim::continent_shapes
