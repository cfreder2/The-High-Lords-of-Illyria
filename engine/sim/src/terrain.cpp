#include "illyria/sim/terrain.h"

#include "illyria/sim/noise.h"

namespace illyria::sim {

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
                h += noise::value(octaveSeed, x, y, o.spacing) * o.weight;
                octaveSeed = octaveSeed * 6364136223846793005ULL + 1442695040888963407ULL;
            }
            // Square the noise to flatten valleys into plains and sharpen hilltops.
            t.samples_.push_back(h * h * maxHeight);
        }
    }
    return t;
}

Terrain Terrain::fromSamples(int32_t cells, Fixed cellSize, std::vector<Fixed> samples) {
    Terrain t;
    t.cells_ = cells;
    t.cellSize_ = cellSize;
    t.samples_ = std::move(samples);
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
    // Each cell is two triangles split along the a-d diagonal, exactly as the
    // renderer draws it, so what lands on the ground sits on the visible ground.
    if (tx >= ty) return a + (b - a) * tx + (d - b) * ty;
    return a + (c - a) * ty + (d - c) * tx;
}

}  // namespace illyria::sim
