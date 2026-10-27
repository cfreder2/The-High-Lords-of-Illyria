#pragma once

#include <cstdint>
#include <vector>

#include "illyria/sim/fixed.h"

namespace illyria::sim {

// Heightfield over a square region whose corner is the world origin. Heights
// are sampled on a regular grid and interpolated over two triangles per cell,
// all in fixed point, so every peer (and the renderer) agrees on the ground.
class Terrain {
public:
    // Rolling hills generated from `seed`: `cells` x `cells` grid cells of
    // `cellSize` metres each, heights in [0, maxHeight].
    static Terrain generate(uint64_t seed, int32_t cells, Fixed cellSize, Fixed maxHeight);
    // Wraps precomputed samples: (cells + 1)^2 heights, row-major.
    static Terrain fromSamples(int32_t cells, Fixed cellSize, std::vector<Fixed> samples);

    // Ground height at a point. Points outside the region use the nearest edge.
    Fixed heightAt(Fixed x, Fixed y) const;

    int32_t cells() const { return cells_; }
    Fixed cellSize() const { return cellSize_; }
    Fixed size() const { return cellSize_ * cells_; }
    // (cells + 1)^2 samples, row-major with x varying fastest.
    const std::vector<Fixed>& samples() const { return samples_; }
    Fixed sample(int32_t ix, int32_t iy) const { return samples_[iy * (cells_ + 1) + ix]; }

private:
    int32_t cells_ = 0;
    Fixed cellSize_;
    std::vector<Fixed> samples_;
};

}  // namespace illyria::sim
