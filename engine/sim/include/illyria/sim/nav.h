#pragma once

#include <cstdint>
#include <vector>

#include "illyria/sim/region.h"

namespace illyria::sim {

// Coarse walkability grid for ground units, with A* routing around deep water
// and buildings. Integer costs and fixed tie-breaking: deterministic.
class NavGrid {
public:
    static constexpr int64_t kCellSize = 8;

    explicit NavGrid(const Region& region);

    // Fills `out` with waypoints ending at `to`. A clear straight line gives a
    // single waypoint. Returns false if no route was found (callers then walk
    // straight and let collisions stop them).
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out) const;
    // True if a ground unit can walk the straight segment.
    bool clearLine(const Vec3& from, const Vec3& to) const;
    bool blocked(const Vec3& p) const;
    int32_t cells() const { return n_; }

private:
    int32_t cellOf(Fixed v) const;
    int32_t index(int32_t x, int32_t y) const { return y * n_ + x; }

    int32_t n_ = 0;
    std::vector<int32_t> cost_;  // 0: blocked
};

}  // namespace illyria::sim
