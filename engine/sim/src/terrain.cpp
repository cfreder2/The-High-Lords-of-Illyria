#include "illyria/sim/terrain.h"

namespace illyria::sim {

namespace {

// Integer hash of a lattice point to a value in [0, 1).
Fixed latticeValue(uint64_t seed, int64_t x, int64_t y) {
    uint64_t h = seed ^ (static_cast<uint64_t>(x) * 0x9E3779B97F4A7C15ULL) ^
                 (static_cast<uint64_t>(y) * 0xC2B2AE3D27D4EB4FULL);
    h ^= h >> 33;
    h *= 0xFF51AFD7ED558CCDULL;
    h ^= h >> 33;
    h *= 0xC4CEB9FE1A85EC53ULL;
    h ^= h >> 33;
    return Fixed::fromRaw(static_cast<int64_t>(h & (Fixed::kOneRaw - 1)));
}

Fixed smoothstep(Fixed t) { return t * t * (Fixed::fromInt(3) - t * 2); }

// Value noise with lattice spacing `spacing` metres, in [0, 1).
Fixed valueNoise(uint64_t seed, Fixed x, Fixed y, int64_t spacing) {
    const Fixed fx = x / spacing, fy = y / spacing;
    const int64_t ix = fx.floorToInt(), iy = fy.floorToInt();
    const Fixed tx = smoothstep(fx - Fixed::fromInt(ix));
    const Fixed ty = smoothstep(fy - Fixed::fromInt(iy));
    const Fixed a = latticeValue(seed, ix, iy), b = latticeValue(seed, ix + 1, iy);
    const Fixed c = latticeValue(seed, ix, iy + 1), d = latticeValue(seed, ix + 1, iy + 1);
    const Fixed top = a + (b - a) * tx;
    const Fixed bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

}  // namespace

Terrain Terrain::generate(uint64_t seed, int32_t cells, Fixed cellSize, Fixed maxHeight) {
    struct Octave {
        int64_t spacing;
        Fixed weight;
    };
    // Weights sum to 1.
    const Octave octaves[] = {{320, Fixed::fromRatio(8, 15)},
                              {160, Fixed::fromRatio(4, 15)},
                              {80, Fixed::fromRatio(2, 15)},
                              {40, Fixed::fromRatio(1, 15)}};

    Terrain t;
    t.cells_ = cells;
    t.cellSize_ = cellSize;
    t.samples_.reserve(static_cast<size_t>(cells + 1) * (cells + 1));
    for (int32_t iy = 0; iy <= cells; ++iy) {
        for (int32_t ix = 0; ix <= cells; ++ix) {
            const Fixed x = cellSize * ix, y = cellSize * iy;
            Fixed h;
            uint64_t octaveSeed = seed;
            for (const Octave& o : octaves) {
                h += valueNoise(octaveSeed, x, y, o.spacing) * o.weight;
                octaveSeed = octaveSeed * 6364136223846793005ULL + 1442695040888963407ULL;
            }
            // Square the noise to flatten valleys into plains and sharpen hilltops.
            t.samples_.push_back(h * h * maxHeight);
        }
    }
    return t;
}

Fixed Terrain::heightAt(Fixed x, Fixed y) const {
    const Fixed maxCoord = size();
    x = clamp(x, Fixed::zero(), maxCoord);
    y = clamp(y, Fixed::zero(), maxCoord);
    const Fixed fx = x / cellSize_, fy = y / cellSize_;
    int32_t ix = static_cast<int32_t>(fx.floorToInt());
    int32_t iy = static_cast<int32_t>(fy.floorToInt());
    if (ix >= cells_) ix = cells_ - 1;
    if (iy >= cells_) iy = cells_ - 1;
    const Fixed tx = fx - Fixed::fromInt(ix), ty = fy - Fixed::fromInt(iy);
    const Fixed a = sample(ix, iy), b = sample(ix + 1, iy);
    const Fixed c = sample(ix, iy + 1), d = sample(ix + 1, iy + 1);
    const Fixed top = a + (b - a) * tx;
    const Fixed bottom = c + (d - c) * tx;
    return top + (bottom - top) * ty;
}

}  // namespace illyria::sim
