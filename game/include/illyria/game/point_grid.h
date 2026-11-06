#pragma once

// Points on the ground plane in square cells over the box they stand in, so that "which of these are near here" looks
// at the few cells round a place, not at every point (presentation only: floats). The cells are a counting sort of the
// points, each cell's in the order they were given.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "illyria/game/gmath.h"

namespace illyria::game {

class PointGrid {
public:
    PointGrid(const std::vector<V3>& points, float cell) : cell_(cell), items_(points.size()) {
        if (points.empty()) return;
        minX_ = minY_ = 1e12f;
        float maxX = -1e12f, maxY = -1e12f;
        for (const V3& p : points) {
            minX_ = std::min(minX_, p.x);
            maxX = std::max(maxX, p.x);
            minY_ = std::min(minY_, p.y);
            maxY = std::max(maxY, p.y);
        }
        w_ = static_cast<int>((maxX - minX_) / cell_) + 1;
        h_ = static_cast<int>((maxY - minY_) / cell_) + 1;
        start_.assign(static_cast<size_t>(w_) * h_ + 1, 0);
        for (const V3& p : points) ++start_[slot(p.x, p.y) + 1];
        for (size_t c = 0; c + 1 < start_.size(); ++c) start_[c + 1] += start_[c];
        std::vector<uint32_t> fill(start_.begin(), start_.end() - 1);
        for (uint32_t k = 0; k < points.size(); ++k) items_[fill[slot(points[k].x, points[k].y)]++] = k;
    }
    // Calls fn(k) for each point whose cell overlaps the square of `reach` round (x, y).
    template <typename Fn>
    void near(float x, float y, float reach, Fn&& fn) const {
        if (start_.empty()) return;
        const int x0 = std::max(cellX(x - reach), 0), x1 = std::min(cellX(x + reach), w_ - 1);
        const int y0 = std::max(cellY(y - reach), 0), y1 = std::min(cellY(y + reach), h_ - 1);
        for (int cy = y0; cy <= y1; ++cy)
            for (int cx = x0; cx <= x1; ++cx) {
                const size_t c = static_cast<size_t>(cy) * w_ + cx;
                for (uint32_t m = start_[c]; m < start_[c + 1]; ++m) fn(items_[m]);
            }
    }

private:
    int cellX(float x) const { return static_cast<int>(std::floor((x - minX_) / cell_)); }
    int cellY(float y) const { return static_cast<int>(std::floor((y - minY_) / cell_)); }
    size_t slot(float x, float y) const { return static_cast<size_t>(cellY(y)) * w_ + cellX(x); }
    float cell_, minX_ = 0, minY_ = 0;
    int w_ = 0, h_ = 0;
    std::vector<uint32_t> start_, items_;
};

}  // namespace illyria::game
