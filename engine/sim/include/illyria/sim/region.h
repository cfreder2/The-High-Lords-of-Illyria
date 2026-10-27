#pragma once

#include <cstdint>
#include <vector>

#include "illyria/sim/terrain.h"
#include "illyria/sim/vec3.h"

namespace illyria::sim {

class Continent;

// What the ground is made of, after Warlords' terrain types. Decides movement
// speed and where things can stand.
enum class Ground : uint8_t { Plains, Road, Forest, Swamp, Shallows, DeepWater, Rock, Bridge };

// A static object in the world: scenery that blocks movement and stops
// missiles (docs/GAME_DESIGN.md §5.4, tier B).
struct Obstacle {
    enum class Kind : uint8_t { Pine, Oak, Bush, Rock, Boulder, House, Pillar, Log };
    Kind kind = Kind::Rock;
    Vec3 pos;           // base, on the ground
    Fixed radius;       // collision radius (houses: unused; logs: half the length)
    Fixed height;
    Fixed halfX, halfY; // houses: axis-aligned footprint
    uint8_t variant = 0;
    bool solid = true;  // bushes are not
};

struct Polyline {
    std::vector<Vec3> points;  // z unused
    Fixed halfWidth;
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
    // Ponds: (x, y) centre, z = radius.
    const std::vector<Vec3>& ponds() const { return ponds_; }
    // Where armies are meant to meet: the ford.
    Vec3 ford() const { return ford_; }
    // Hash of everything generated, for cross-platform checks.
    uint64_t checksum() const;

private:
    // Trees, undergrowth and rocks on a jittered grid, clear of roads, water,
    // the first `buildings` obstacles, and a circle around `clearCentre`.
    void scatterScenery(uint64_t seed, size_t buildings, const Vec3& clearCentre, Fixed clearRadius);

    Terrain terrain_;
    Fixed waterLevel_;
    std::vector<Ground> ground_;  // per terrain cell
    std::vector<Obstacle> obstacles_;
    std::vector<Polyline> roads_, rivers_, berms_;
    std::vector<Vec3> ponds_;
    Vec3 ford_;
    Vec3 origin_;
    std::vector<Fixed> water_;  // per sample
};

// Movement speed multiplier for a ground type (zero: impassable on foot).
Fixed groundSpeed(Ground g);

}  // namespace illyria::sim
