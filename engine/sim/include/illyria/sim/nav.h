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
    // straight and let collisions stop them). Onto or off a raised floor
    // (Region::Floor) the way is its stair; on it, round its stones.
    bool findPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait = Gait::Foot) const;
    // True if a ground unit can walk the straight segment: on a floor, clear
    // of its stones; onto or off one, only by the foot of its stair.
    bool clearLine(const Vec3& from, const Vec3& to) const;
    // On the ground, blocked (water, cliff, wall, house, shut gate), or
    // under a raised floor (which is solid to the ground).
    bool blocked(const Vec3& p) const;
    // Which raised floor a point is on (its deck or its stair), or -1; with
    // `byHeight`, a point below its surface (on the ground beside it) is not.
    int floorAt(const Vec3& p, bool byHeight) const;
    // A key for routes from or to a point on a floor (its floor and fine
    // cell, from 0), or -1 off them: routes there are cached per fine cell.
    int32_t floorCell(const Vec3& p, bool byHeight) const;
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
    // A raised floor walked on its own fine grid (half-metre cells over the
    // floor's and the stair's decks, less what its stones and a man's
    // breadth round them take up).
    struct FloorGrid {
        Deck floor, stair;
        Vec3 foot, bottom;  // Region::Floor
        Fixed x0, y0;       // the grid's south-west corner
        int32_t nx = 0, ny = 0;
        std::vector<uint8_t> open;
        bool covers(Fixed x, Fixed y) const { return floor.contains(x, y) || stair.contains(x, y); }
        int32_t cellAt(Fixed x, Fixed y) const;  // -1 off the grid
        Vec3 centreOf(int32_t cell) const;
    };
    std::vector<FloorGrid> floors_;
    void buildFloors(const Region& region);
    // The fine cell nearest p that is open, or -1.
    int32_t nearestFloorCell(const FloorGrid& f, const Vec3& p) const;
    bool floorClear(const FloorGrid& f, const Vec3& a, const Vec3& b) const;
    // Appends the way across a floor from `from` to `to` (both on it).
    void floorRoute(const FloorGrid& f, const Vec3& from, const Vec3& to, std::vector<Vec3>& out) const;
    // findPath on the ground (over bridges by their ends).
    bool groundRoute(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait) const;
    bool groundClear(const Vec3& from, const Vec3& to) const;
    bool groundBlocked(const Vec3& p) const;
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
    // Cells a route crosses elsewhere than at their middle, sorted by cell: a
    // gate passage's (down the passage's middle line, the cell's middle may be
    // in the stone either side), and those beside a ramp (a man's breadth off
    // its side, not scraping up it).
    std::vector<std::pair<int32_t, Vec3>> through_;
    std::vector<Deck> decks_;    // routes over a bridge go end to end along its deck
    std::vector<uint8_t> walled_;  // per deck: a castle wall stands across it (no way over)
    // A castle's stone, which no straight line on the ground may cut through
    // though the cells are too coarse to see it: its walls (round-ended, as
    // they push men out), its towers, and its ramps (with their landings)
    // where they stand higher than a step, each a man's breadth wider. Ramps
    // are boxes, [lo, hi] along dir from centre (the deck itself starts at foot).
    struct StoneBox {
        Vec3 centre, dir;
        Fixed lo, hi, halfWidth, foot;
    };
    struct StoneWall {
        Vec3 a, b;
        Fixed reach;
    };
    std::vector<StoneBox> stoneBoxes_;
    std::vector<StoneWall> stoneWalls_;
    std::vector<std::pair<Vec3, Fixed>> stoneDiscs_;  // towers: centre, radius
    // Per cell, the stone within a cell of it (offsets into stoneIds_: boxes,
    // then walls, then towers); empty with no castle.
    std::vector<uint32_t> stoneAt_;
    std::vector<uint16_t> stoneIds_;
    mutable std::vector<uint32_t> stoneSeen_;  // scratch: tested in this line already
    mutable uint32_t stoneStamp_ = 0;
    // Whether the segment cuts through stone `id`.
    bool hitsStone(uint32_t id, const Vec3& from, const Vec3& to) const;
    // Ground per terrain cell.
    std::vector<uint8_t> fine_;
    int32_t fineCells_ = 0;
    Fixed fineSize_;
    mutable std::array<std::vector<uint16_t>, kGaitCount> cost_;
    // A* scratch, kept between searches to spare the allocations.
    mutable std::vector<int32_t> g_, parent_;
};

}  // namespace illyria::sim
