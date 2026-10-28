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
    // Castles need finer cells, so a gate is open and a wall is closed.
    static constexpr int64_t kCastleCellSize = 4;

    explicit NavGrid(const Region& region);
    int64_t cellSize() const { return cell_; }

    // Fills `out` with waypoints ending at `to`. A clear straight line gives a
    // single waypoint. Returns false if no route was found (callers then walk
    // straight and let collisions stop them).
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out) const;
    // True if a ground unit can walk the straight segment.
    bool clearLine(const Vec3& from, const Vec3& to) const;
    bool blocked(const Vec3& p) const;
    // Castle gates start shut, closing the cells they stand in; a broken gate
    // opens them again for good.
    void openGate(size_t gate);
    int32_t cells() const { return n_; }

private:
    int32_t cellOf(Fixed v) const;
    int32_t index(int32_t x, int32_t y) const { return y * n_ + x; }

    int32_t n_ = 0;
    int64_t cell_ = kCellSize;
    std::vector<int32_t> cost_;  // 0: blocked
    // Per gate: the cells it closes, with the cost each had before.
    std::vector<std::vector<std::pair<int32_t, int32_t>>> gateCells_;
    std::vector<Deck> decks_;    // routes over a bridge go end to end along its deck
    // Deep water per terrain cell, where a bridge cell is otherwise open.
    std::vector<uint8_t> deep_;
    int32_t fine_ = 0;
    Fixed fineSize_;
};

}  // namespace illyria::sim
