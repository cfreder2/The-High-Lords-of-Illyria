#pragma once

// Deterministic fixed-point noise for world generation. Integer hashing and
// fixed-point interpolation only, so every platform builds identical worlds.

#include <cstdint>

#include "illyria/sim/fixed.h"

namespace illyria::sim::noise {

// Integer hash of a lattice point to a value in [0, 1).
inline Fixed lattice(uint64_t seed, int64_t x, int64_t y) {
    uint64_t h = seed ^ (static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL) ^
                 (static_cast<uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return Fixed::fromRaw(static_cast<int64_t>(h & (Fixed::kOneRaw - 1)));
}

inline Fixed smoothstep(Fixed t) { return t * t * (Fixed::fromInt(3) - t * 2); }

// smoothstep of x between edges a and b, clamped to [0, 1].
inline Fixed smoothstep(Fixed a, Fixed b, Fixed x) { return smoothstep(clamp((x - a) / (b - a), Fixed::zero(), Fixed::one())); }

// Value noise with lattice spacing `spacing` metres, in [0, 1).
inline Fixed value(uint64_t seed, Fixed x, Fixed y, int64_t spacing) {
    const Fixed fx = x / spacing, fy = y / spacing;
    const int64_t ix = fx.floorToInt(), iy = fy.floorToInt();
    const Fixed tx = smoothstep(fx - Fixed::fromInt(ix));
    const Fixed ty = smoothstep(fy - Fixed::fromInt(iy));
    const Fixed a = lattice(seed, ix, iy), b = lattice(seed, ix + 1, iy);
    const Fixed c = lattice(seed, ix, iy + 1), d = lattice(seed, ix + 1, iy + 1);
    const Fixed top = a + (b - a) * tx;
    const Fixed bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

inline uint64_t nextSeed(uint64_t s) { return s * 6364136223846793005ULL + 1442695040888963407ULL; }

// Four octaves of value noise, largest features `spacing` metres across, in [0, 1).
inline Fixed fbm(uint64_t seed, Fixed x, Fixed y, int64_t spacing) {
    const Fixed weights[] = {Fixed::fromRatio(8, 15), Fixed::fromRatio(4, 15), Fixed::fromRatio(2, 15),
                             Fixed::fromRatio(1, 15)};
    Fixed h;
    for (const Fixed& w : weights) {
        h += value(seed, x, y, spacing) * w;
        seed = nextSeed(seed);
        spacing = spacing > 2 ? spacing / 2 : 1;
    }
    return h;
}

// Ridged noise: sharp crests where value noise crosses its midpoint, in [0, 1].
inline Fixed ridged(uint64_t seed, Fixed x, Fixed y, int64_t spacing) {
    const Fixed r = Fixed::one() - abs(fbm(seed, x, y, spacing) * 2 - Fixed::one());
    return r * r;
}

}  // namespace illyria::sim::noise
