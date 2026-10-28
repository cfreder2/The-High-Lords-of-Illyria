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

// Value noise and its gradient (per lattice cell), in [0, 1).
inline Fixed valueD(uint64_t seed, Fixed x, Fixed y, int64_t spacing, Fixed& dx, Fixed& dy) {
    const Fixed fx = x / spacing, fy = y / spacing;
    const int64_t ix = fx.floorToInt(), iy = fy.floorToInt();
    const Fixed tx = fx - Fixed::fromInt(ix), ty = fy - Fixed::fromInt(iy);
    const Fixed ux = smoothstep(tx), uy = smoothstep(ty);
    const Fixed dux = tx * (Fixed::one() - tx) * 6, duy = ty * (Fixed::one() - ty) * 6;
    const Fixed a = lattice(seed, ix, iy), b = lattice(seed, ix + 1, iy);
    const Fixed c = lattice(seed, ix, iy + 1), d = lattice(seed, ix + 1, iy + 1);
    const Fixed k1 = b - a, k2 = c - a, k3 = a - b - c + d;
    dx = dux * (k1 + k3 * uy);
    dy = duy * (k2 + k3 * ux);
    return a + k1 * ux + k2 * uy + k3 * ux * uy;
}

// Eroded fractal noise, in [0, 1): each finer octave is damped where the
// coarser ones already slope, so hillsides stay smooth while crests sharpen
// and valleys branch, much like land worn by water (after Inigo Quilez).
inline Fixed eroded(uint64_t seed, Fixed x, Fixed y, int64_t spacing, int octaves) {
    Fixed sum, norm, gx, gy, amp = Fixed::fromRatio(1, 2);
    for (int i = 0; i < octaves; ++i) {
        Fixed dx, dy;
        const Fixed v = valueD(seed, x, y, spacing, dx, dy);
        gx += dx;
        gy += dy;
        sum += amp * v / (Fixed::one() + gx * gx + gy * gy);
        norm += amp;
        amp = amp / 2;
        seed = nextSeed(seed);
        spacing = spacing > 2 ? spacing / 2 : 1;
    }
    return sum / norm;
}

// Gradient (Perlin) noise, roughly in [-1, 1]. Unlike value noise it has no
// lattice-aligned creases, which matters for ridges built from |noise|.
inline Fixed gradient(uint64_t seed, Fixed x, Fixed y, int64_t spacing) {
    const Fixed fx = x / spacing, fy = y / spacing;
    const int64_t ix = fx.floorToInt(), iy = fy.floorToInt();
    const Fixed tx = fx - Fixed::fromInt(ix), ty = fy - Fixed::fromInt(iy);
    // Twelve directions around the circle.
    static const Fixed kCos[12] = {Fixed::fromRatio(1000, 1000), Fixed::fromRatio(866, 1000), Fixed::fromRatio(500, 1000),
                                   Fixed::zero(), Fixed::fromRatio(-500, 1000), Fixed::fromRatio(-866, 1000),
                                   Fixed::fromRatio(-1000, 1000), Fixed::fromRatio(-866, 1000), Fixed::fromRatio(-500, 1000),
                                   Fixed::zero(), Fixed::fromRatio(500, 1000), Fixed::fromRatio(866, 1000)};
    const auto corner = [&](int64_t cx, int64_t cy, Fixed dx, Fixed dy) {
        const int k = static_cast<int>((lattice(seed, cx, cy).raw * 12) >> 16);
        return kCos[k] * dx + kCos[(k + 3) % 12] * dy;
    };
    const auto fade = [](Fixed t) { return t * t * t * (t * (t * 6 - Fixed::fromInt(15)) + Fixed::fromInt(10)); };
    const Fixed u = fade(tx), v = fade(ty);
    const Fixed n00 = corner(ix, iy, tx, ty), n10 = corner(ix + 1, iy, tx - Fixed::one(), ty);
    const Fixed n01 = corner(ix, iy + 1, tx, ty - Fixed::one()), n11 = corner(ix + 1, iy + 1, tx - Fixed::one(), ty - Fixed::one());
    const Fixed top = n00 + (n10 - n00) * u, bottom = n01 + (n11 - n01) * u;
    return (top + (bottom - top) * v) * Fixed::fromRatio(7, 5);
}

// Ridged multifractal, in [0, 1]: sharp crests whose finer ridges grow only
// where the coarser ones are already high, so ranges branch into spurs and
// ravines rather than lumps (after Musgrave).
inline Fixed ridgedMF(uint64_t seed, Fixed x, Fixed y, int64_t spacing, int octaves) {
    Fixed sum, norm, weight = Fixed::one(), amp = Fixed::one();
    for (int i = 0; i < octaves; ++i) {
        Fixed signal = Fixed::one() - min(Fixed::one(), abs(gradient(seed, x, y, spacing)));
        signal = signal * signal * weight;
        weight = clamp(signal * 2, Fixed::zero(), Fixed::one());
        sum += signal * amp;
        norm += amp;
        amp = amp * Fixed::fromRatio(11, 20);
        seed = nextSeed(seed);
        spacing = spacing > 2 ? spacing / 2 : 1;
    }
    return sum / norm;
}

}  // namespace illyria::sim::noise
