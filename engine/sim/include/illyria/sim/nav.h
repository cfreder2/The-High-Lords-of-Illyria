#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "illyria/sim/gait.h"
#include "illyria/sim/region.h"

namespace illyria::sim {

// Climbing costs: a walker's speed divides by 1 + grade x this (x its gait's
// climb percent); going down is a little quicker.
inline constexpr Fixed kClimbCost = Fixed::fromRatio(5, 2);
// Slopes steeper than this slow walkers down even across them.
inline constexpr Fixed kSteepSlope = Fixed::fromRatio(7, 20);
inline constexpr Fixed kSteepSpeed = Fixed::fromRatio(4, 5);

// Coarse walkability grid for ground units, with A* routing around deep water
// and buildings. Costs are travel time for each gait (gait.h): a cell costs
// what the best ground in it costs, since a walker crossing it keeps to that
// strip (a road through a field), so paths are the fastest, not the shortest.
// Integer costs and fixed tie-breaking: deterministic.
class NavGrid {
public:
    static constexpr int64_t kCellSize = 8;
    // Castles need finer cells, so a gate is open and a wall is closed.
    static constexpr int64_t kCastleCellSize = 4;

    // Keeps a reference to the region's terrain: the region must outlive it.
    explicit NavGrid(const Region& region);
    int64_t cellSize() const { return cell_; }

    // Fills `out` with waypoints ending at `to`, the fastest way for this
    // gait. A straight line no slower than any detour gives a single
    // waypoint. Returns false if no route was found (callers then walk
    // straight and let collisions stop them).
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait = Gait::Foot) const;
    // True if a ground unit can walk the straight segment.
    bool clearLine(const Vec3& from, const Vec3& to) const;
    bool blocked(const Vec3& p) const;
    // Time to walk the straight segment at this gait, in metres of level
    // plains (walls and water not checked: see clearLine). `slowest`, if
    // given, gets the slowest ground's speed met on the way.
    Fixed travelTime(const Vec3& from, const Vec3& to, Gait gait, Fixed* slowest = nullptr) const;
    // The same along a polyline from `from` through `points`.
    Fixed travelTime(const Vec3& from, const std::vector<Vec3>& points, Gait gait) const;
    // Castle gates start shut, closing the cells they stand in; a broken gate
    // opens them again for good.
    void openGate(size_t gate);
    int32_t cells() const { return n_; }

private:
    // findPath once off any bridge deck.
    bool route(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait) const;
    int32_t cellOf(Fixed v) const;
    int32_t index(int32_t x, int32_t y) const { return y * n_ + x; }
    // Per-gait step costs per metre (x100), built on first use; 0: blocked.
    const std::vector<uint16_t>& costs(Gait gait) const;
    uint16_t cellCost(Gait gait, int32_t cell) const;
    // Where a walker of this gait crosses the cell: the middle of its best ground.
    Vec3 waypoint(int32_t cell, Gait gait) const;
    // The open cell nearest p, or -1 if none is near.
    int32_t nearestOpen(const Vec3& p) const;
    int32_t climbCost(int32_t from, int32_t to, int32_t step, bool diagonal, int32_t climbPct) const;
    Ground fineGround(Fixed x, Fixed y) const;
    Fixed speedAt(const Vec3& p, Gait gait) const;

    const Terrain* terrain_ = nullptr;
    int32_t n_ = 0;
    int64_t cell_ = kCellSize;
    int32_t sub_ = 1;  // terrain cells across a nav cell
    std::vector<uint8_t> open_;     // 0: blocked (water, cliff, wall, house, shut gate)
    std::vector<uint16_t> mask_;    // bit per walkable ground found in the cell
    std::vector<uint8_t> steep_;    // slopes that slow walkers
    std::vector<Fixed> height_;     // ground at the cell's centre
    // Per gate: the cells it closes, with whether each was open before.
    std::vector<std::vector<std::pair<int32_t, uint8_t>>> gateCells_;
    std::vector<Deck> decks_;    // routes over a bridge go end to end along its deck
    // Ground per terrain cell.
    std::vector<uint8_t> fine_;
    int32_t fineCells_ = 0;
    Fixed fineSize_;
    mutable std::array<std::vector<uint16_t>, kGaitCount> cost_;
    // A* scratch, kept between searches to spare the allocations.
    mutable std::vector<int32_t> g_, parent_;
};

}  // namespace illyria::sim
