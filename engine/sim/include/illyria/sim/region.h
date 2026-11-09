#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "illyria/sim/noise.h"
#include "illyria/sim/terrain.h"
#include "illyria/sim/vec3.h"

namespace illyria::sim {

class Continent;

// What the ground is made of, after Warlords' terrain types. Decides movement
// speed and where things can stand.
enum class Ground : uint8_t { Plains, Road, Forest, Swamp, Shallows, DeepWater, Rock, Bridge, Mud, Sand };

// A static object in the world: scenery that blocks movement and stops
// missiles (docs/GAME_DESIGN.md §5.4, tier B).
struct Obstacle {
    enum class Kind : uint8_t { Pine, Oak, Bush, Rock, Boulder, House, Pillar, Log, Tower };
    Kind kind = Kind::Rock;
    Vec3 pos;           // base, on the ground
    Fixed radius;       // collision radius (houses: unused; logs: half the length)
    Fixed height;
    Fixed halfX, halfY; // houses: axis-aligned footprint
    uint8_t variant = 0;  // houses: kTownHouse for those in a castle (drawn with the castle)
    static constexpr uint8_t kTownHouse = 200;
    static constexpr uint8_t kSiteStone = 201;  // a ruin's, temple's or sage's tower's stones (drawn with the site)
    static constexpr uint8_t kAltar = 3;        // houses: a shrine's altar, a low block of stone
    bool solid = true;  // bushes are not
    // Standing stone a soldier keeps his drawn breadth from (stoneStandoff),
    // not just his radius: a site's stones, pillars (a pillar's radius is its
    // foot's, as drawn) and a shrine's altar. Routes keep clear of it exactly (NavGrid).
    bool standingStone() const { return variant == kSiteStone || kind == Kind::Pillar || (kind == Kind::House && variant == kAltar); }
};

struct Polyline {
    std::vector<Vec3> points;  // z unused
    Fixed halfWidth;
    std::vector<Fixed> widths = {};  // half width at each point, where it varies (roads); else halfWidth
    Fixed widthAt(size_t seg, Fixed t) const {
        if (widths.size() != points.size() || seg + 1 >= widths.size()) return halfWidth;
        return widths[seg] + (widths[seg + 1] - widths[seg]) * t;
    }
};

// A walkway above the ground: a bridge deck, a castle's wall-walk or a ramp up to one.
// It runs from centre - dir * halfSpan to centre + dir * halfSpan, halfWidth
// either side; its top is centre.z in the middle and rises by `rise` end to end.
// A tower's gallery (`arc`) is a ring round centre instead, from `inner` out
// to halfSpan, as far either side of dir as its sweep's cosine `cosHalf`.
struct Deck {
    Vec3 centre, dir;
    Fixed halfSpan, halfWidth;
    bool foot = false;   // a narrow timber footbridge, not a stone road bridge
    Fixed rise = {};     // ramps climb; bridges and walls are level
    bool raised = false; // on a castle (walls and ramps) or a ruin's platform, not over water
    bool ramp = false;   // a castle ramp, a ruin's platform or its stair: solid down to the ground
    bool arc = false;    // a gallery round a tower, corbelled out: open below
    Fixed inner = {}, cosHalf = {};
    bool contains(Fixed x, Fixed y) const;
    Fixed heightAt(Fixed x, Fixed y) const;
};

// A raised floor soldiers walk on, solid down to the ground: a ruin's
// platform. Its deck and its stair's are in Region::decks(); the only way on
// or off is the stair, from `foot` on the ground before it to `bottom`, its
// lowest step. NavGrid routes over it round the stones that stand on it.
// It stands on a stepped base wider than its deck (the model's plinth),
// solid to the ground but for the stair cut into its front: the box from
// baseLo to baseHi (z unused), which men on the ground keep their standoff
// from (stoneStandoff) and routes their gait's clearance.
struct Floor {
    size_t deck = 0, stair = 0;
    Vec3 foot, bottom;
    Vec3 baseLo, baseHi;
};

// A wall that blocks soldiers and missiles: a slab from a to b (z = its
// base: above the ground over a gate passage), halfThick either side, up to
// top. A curtain wall's slab stops at its walk, which soldiers walk on; along
// the walk's outer edge (the `out` side) stands the parapet, low at the
// embrasures and up to top on the merlons between them (Continent::Castle).
struct WallSeg {
    Vec3 a, b;
    Fixed halfThick, top;
    Fixed walk = {};  // the wall-walk's height, if it has one
    Vec3 out = {};    // unit, flat: the parapet's side (zero: no walk, no parapet)
    std::vector<Fixed> crenels = {};  // embrasure middles, metres from a
};

struct Rampart;

// A castle gate: two heavy timber doors across the gap left in the curtain
// wall, from a to b (z = the ground), halfThick either side, up to top. While
// it stands it blocks walkers and missiles like the wall; once its hit points
// are gone (World tracks them) it lies broken open for good.
struct Gate {
    Vec3 a, b;
    Fixed halfThick, top;
    Vec3 out;           // unit, horizontal: from the town out through the gate
    int32_t maxHp = 1;
    int32_t ring = 0;   // the rampart (ring of walls) it opens
    // True inside that ring's walls.
    static bool inside(const Rampart& ring, const Vec3& p);
};

// A ring of castle walls with a walk along the top, and the ramps up to it.
struct Rampart {
    std::vector<Vec3> corners;  // z = the walk
    // The way round it: the walk's middle line, bent round the inner side of
    // every tower on its gallery (z = the walk).
    std::vector<Vec3> way;
    Fixed halfThick;
    Vec3 centre;  // inside the ring
    std::vector<std::pair<Vec3, Vec3>> ramps;  // (foot on the ground, head at the walk)
    // Where an archer stands to shoot out through each embrasure: on the
    // walk against the parapet (z = the walk), and the way out.
    struct Embrasure {
        Vec3 pos, out;
    };
    std::vector<Embrasure> embrasures;
};

// A generated tactical region: terrain, water, ground types, roads and
// scenery. Fully deterministic from its seed (fixed point throughout).
class Region {
public:
    // The 1 km battle region of Illyria: northern mountains, a river running
    // south through a ford to a lake, roads with a village and a temple ruin,
    // forests, swamp, and an earthwork berm above the ford.
    static Region generateIllyria(uint64_t seed);
    // A full-detail window of the continent, `cells` x 4 m square, centred on
    // `centre` (continent coordinates). Region coordinates are local: the
    // window's south-west corner is (0, 0); origin() maps back to the continent.
    static Region fromContinent(const Continent& continent, const Vec3& centre, int32_t cells);
    // How far from a site's middle nothing grows (metres): a ruin stands in a glade past the ring armies march round it, the
    // others out to the corners of their footprint (the detail land of the battle's far country keeps the same).
    static Fixed siteClearing(bool ruin, Fixed size);
    // A bare test ground: `cells` x 4 m, heights per sample ((cells + 1)^2, or
    // empty for level), ground per cell, no water, roads or scenery.
    static Region fromGround(int32_t cells, const std::vector<Ground>& ground, std::vector<Fixed> heights = {});

    const Terrain& terrain() const { return terrain_; }
    Fixed waterLevel() const { return waterLevel_; }
    // Continent coordinates of the region's (0, 0).
    Vec3 origin() const { return origin_; }
    // Water surface height per terrain sample, or kDry where there is none.
    static constexpr int64_t kDryRaw = -(int64_t{1} << 40);
    const std::vector<Fixed>& waterSurfaces() const { return water_; }
    Ground groundAt(Fixed x, Fixed y) const;
    Ground cellGround(int32_t ix, int32_t iy) const { return ground_[iy * terrain_.cells() + ix]; }
    const std::vector<Obstacle>& obstacles() const { return obstacles_; }
    const std::vector<Polyline>& roads() const { return roads_; }
    const std::vector<Polyline>& rivers() const { return rivers_; }
    const std::vector<Polyline>& berms() const { return berms_; }
    const std::vector<Deck>& decks() const { return decks_; }
    const std::vector<Floor>& floors() const { return floors_; }
    const std::vector<WallSeg>& walls() const { return walls_; }
    const std::vector<Rampart>& ramparts() const { return ramparts_; }
    const std::vector<Gate>& gates() const { return gates_; }
    // The rampart whose walk this point is on, or -1.
    int rampartAt(Fixed x, Fixed y, Fixed z) const;
    // True on a bridge deck, with the deck's height.
    bool deckAt(Fixed x, Fixed y, Fixed& height) const;
    // The highest deck here no higher than `ceiling` (one can stand under a
    // wall-walk, in a gate passage); false if there is none.
    bool deckBelow(Fixed x, Fixed y, Fixed ceiling, Fixed& height) const;
    // True where a castle ramp's slope here rises above z: inside its solid
    // body (it is earth and stone down to the ground, not a bridge).
    bool inRamp(Fixed x, Fixed y, Fixed z) const;
    // True where the ground at this exact point lies under water.
    bool underWater(Fixed x, Fixed y) const;
    // Ponds: (x, y) centre, z = radius.
    const std::vector<Vec3>& ponds() const { return ponds_; }
    // Where armies are meant to meet: the ford.
    Vec3 ford() const { return ford_; }
    // Hash of everything generated, for cross-platform checks.
    uint64_t checksum() const;

    // The decks, walls and ramparts near a point or a box, in index order, found
    // through a grid built the first time it is asked (the ground of a whole
    // continent has over a thousand decks and nine hundred walls, and a soldier
    // only meets the few near him). What comes back is every one that a loop
    // over them all could find at the point or in the box: a deck whose box
    // holds the point, a wall whose box (padded by its half thickness and a
    // metre) meets the box, a rampart whose way (padded by its walk and two
    // metres) passes by the point.
    std::span<const uint32_t> decksAt(Fixed x, Fixed y) const;  // (a view into the grid, no copy)
    void wallsNear(Fixed x0, Fixed y0, Fixed x1, Fixed y1, std::vector<uint32_t>& out) const;
    std::span<const uint32_t> wallsAt(Fixed x, Fixed y) const;  // (a view into the grid, no copy)
    std::span<const uint32_t> rampartsAt(Fixed x, Fixed y) const;
    // The gates within kGateReach of a point (a box round one end of the doors: all a loop over every gate that
    // passes the same box test could find), floors whose base box meets the box, and floors whose stair's box holds
    // the point: indices ascending.
    static constexpr Fixed kGateReach = Fixed::fromInt(60);
    std::span<const uint32_t> gatesNear(Fixed x, Fixed y) const;
    void floorBasesNear(Fixed x0, Fixed y0, Fixed x1, Fixed y1, std::vector<uint32_t>& out) const;
    std::span<const uint32_t> floorStairsAt(Fixed x, Fixed y) const;

    // The scenery grid: a point every 5 m of the continent (at continent
    // metres 5 gx, 5 gy, jittered), each drawn from its own hash, so every
    // window cut from the continent grows the same trees in the same places
    // (and the renderer can grow them anywhere: game::DetailLand).
    static constexpr int64_t kSceneryStep = 5;
    struct SceneryPoint {
        Fixed x, y;  // continent coordinates
        Fixed roll, size;
        uint32_t variant = 0;
    };
    static SceneryPoint sceneryPoint(uint64_t seed, int64_t gx, int64_t gy);
    // The scenery seed of windows cut from a continent with this seed.
    static uint64_t sceneryContinentSeed(uint64_t continentSeed) { return continentSeed ^ 0x5CE7E; }
    // What grows at a point whose ground is `g` (byWater: shallows or deep
    // water within 4 m) at height z; false if nothing. Sets the obstacle's
    // kind, radius, height, variant and solidity (not its position).
    static bool sceneryAt(uint64_t seed, const SceneryPoint& p, Ground g, bool byWater, Fixed z, Obstacle& o);

private:
    // Trees, undergrowth and rocks on a jittered grid, clear of roads, water,
    // the first `buildings` obstacles, a circle around `clearCentre`, and the
    // `clearings` (centre, radius in x: the sites standing in the window).
    void scatterScenery(uint64_t seed, size_t buildings, const Vec3& clearCentre, Fixed clearRadius,
                        const std::vector<Vec3>& clearings = {});

    Terrain terrain_;
    Fixed waterLevel_;
    std::vector<Ground> ground_;  // per terrain cell
    std::vector<Obstacle> obstacles_;
    std::vector<Polyline> roads_, rivers_, berms_;
    std::vector<Vec3> ponds_;
    std::vector<Deck> decks_;
    std::vector<Floor> floors_;
    std::vector<WallSeg> walls_;
    std::vector<Rampart> ramparts_;
    std::vector<Gate> gates_;
    // The grids of decks, walls and ramparts (decksAt and the rest), built on
    // first use: 16 m cells over the ground, each with the indices whose boxes meet it.
    struct BoxGrid {
        bool built = false;
        int32_t n = 0;
        std::vector<uint32_t> start, items;
    };
    mutable BoxGrid deckGrid_, wallGrid_, rampGrid_, gateGrid_, floorBaseGrid_, floorStairGrid_;
    // Each deck's middle and how far its box reaches, in the decks' order: the cheap first test of Deck::contains,
    // read from a few compact numbers instead of the deck (decksAt's lists are tested on every step of every man).
    struct DeckReach {
        Fixed x, y, reach;
    };
    mutable std::vector<DeckReach> deckReach_;
    bool deckHolds(uint32_t k, Fixed x, Fixed y) const;
    void buildGrids() const;
    static std::span<const uint32_t> cellOf(const BoxGrid& g, Fixed x, Fixed y);
    static void cellsOf(const BoxGrid& g, Fixed x0, Fixed y0, Fixed x1, Fixed y1, std::vector<uint32_t>& out);
    Vec3 ford_;
    Vec3 origin_;
    std::vector<Fixed> water_;  // per sample
};

// Movement speed multiplier for a ground type (zero: impassable on foot).
Fixed groundSpeed(Ground g);

// Whether the cell at (x, y), z high, is a trodden margin of the water (Ground::Mud): barely above a wet cell within about two
// cells, in patches. `waterAt(dx, dy)` is the water surface raw of the cell dx, dy away (Region::kDryRaw if dry). One rule for
// the battle's Region and the close-up land of the Conquest, so they agree. The reach is ragged (each cell's own, 1 to 2
// cells), not a box of cells: the margin follows the shore, not a staircase of the grid.
template <class WaterAt>
inline bool muddyShore(uint64_t seed, Fixed x, Fixed y, Fixed z, WaterAt&& waterAt) {
    if (noise::fbm(seed, x, y, 24) <= Fixed::fromRatio(2, 5)) return false;
    const int64_t reach2 = 1 + (noise::fbm(seed ^ 0xA11, x, y, 5) * 10).floorToInt() / 3;  // 1 to 4 (cells squared)
    for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx * dx + dy * dy > reach2) continue;
            const Fixed w = waterAt(dx, dy);
            if (w.raw != Region::kDryRaw && z - w < Fixed::fromRatio(9, 10)) return true;
        }
    return false;
}

}  // namespace illyria::sim
