#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "illyria/sim/fixed.h"
#include "illyria/sim/rng.h"
#include "illyria/sim/terrain.h"
#include "illyria/sim/vec3.h"

namespace illyria::sim {

using EntityId = uint32_t;
using PlayerId = uint8_t;

inline constexpr EntityId kNoEntity = 0;
inline constexpr int32_t kTicksPerSecond = 30;
inline constexpr Fixed kTickDt = Fixed::fromRatio(1, kTicksPerSecond);

struct RangedWeapon {
    Fixed muzzleSpeed;        // metres/second; zero means the unit has no ranged attack
    int32_t reloadTicks = 0;  // ticks between shots
    int32_t damage = 0;
    Fixed dispersionAt100m;   // radius of the dispersion circle at 100 m for a rank-0 unit
    Fixed autoRange;          // idle units shoot at enemies within this range; zero disables
};

struct MeleeWeapon {
    int32_t damage = 0;  // zero means the unit cannot fight in melee
    Fixed reach;         // gap between the two units' edges at which blows land
    int32_t cooldownTicks = 0;
};

struct UnitStats {
    Fixed radius;
    Fixed height;
    Fixed moveSpeed;  // metres/second
    int32_t maxHp = 1;
    RangedWeapon ranged;
    MeleeWeapon melee;
    Fixed aggroRange;  // idle units close to melee with enemies within this range
};

// One soldier. Squads are a campaign-layer grouping of these.
struct Unit {
    EntityId id = kNoEntity;
    PlayerId owner = 0;
    UnitStats stats;
    int32_t hp = 0;
    int32_t rank = 0;  // veterancy; shrinks the dispersion circle

    Vec3 pos;
    Vec3 facing;  // unit-length, horizontal

    bool hasMoveTarget = false;
    Vec3 moveTarget;
    bool hasAttackTarget = false;
    Vec3 attackTarget;
    int32_t reloadRemaining = 0;
    int32_t meleeCooldown = 0;

    bool alive() const { return hp > 0; }
};

struct Projectile {
    EntityId id = kNoEntity;
    EntityId shooter = kNoEntity;
    Vec3 pos;
    Vec3 vel;
    int32_t damage = 0;
    int32_t ageTicks = 0;
};

// Player input, the only thing exchanged between lockstep peers.
struct Command {
    enum class Type : uint8_t { Move, AttackGround, Stop };

    PlayerId player = 0;
    EntityId unit = kNoEntity;
    Type type = Type::Stop;
    Vec3 target;
};

// Something that happened this tick, for rendering and audio. Not part of the
// simulation state, but derived only from it, so identical on every peer.
struct ImpactEvent {
    Vec3 pos;
    Vec3 dir;                        // direction of the blow or projectile
    EntityId projectile = kNoEntity; // kNoEntity for a melee blow
    EntityId hitUnit = kNoEntity;    // kNoEntity when a projectile hit the ground
    bool killed = false;
};

struct WorldConfig {
    uint64_t seed = 0;
    Fixed gravity = Fixed::fromRatio(981, 100);
    // Constant acceleration applied to projectiles, in m/s^2.
    Vec3 wind;
    // Ground; a flat plane at z == 0 when null.
    std::shared_ptr<const Terrain> terrain;
};

class World {
public:
    explicit World(const WorldConfig& config);

    EntityId spawnUnit(PlayerId owner, const Vec3& pos, const Vec3& facing, const UnitStats& stats);

    // Advances the simulation by exactly one tick. Every peer must call this
    // with the same commands for the same tick.
    void step(std::span<const Command> commands);

    // Hash of the complete simulation state, compared between peers to detect desyncs.
    uint64_t checksum() const;

    uint32_t tick() const { return tick_; }
    const std::vector<Unit>& units() const { return units_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<ImpactEvent>& impacts() const { return impacts_; }
    const Unit* findUnit(EntityId id) const;
    Fixed groundHeight(Fixed x, Fixed y) const;

private:
    Unit* mutableUnit(EntityId id);
    void applyCommand(const Command& cmd);
    void updateUnit(Unit& unit);
    void fire(Unit& unit, const Vec3& target);
    void fight(Unit& unit, Unit& enemy);
    Unit* nearestEnemy(const Unit& unit, Fixed range);
    // Returns true if the blow killed the target.
    bool damage(Unit& target, int32_t amount);
    void updateProjectiles();

    WorldConfig config_;
    Rng rng_;
    uint32_t tick_ = 0;
    EntityId nextId_ = 1;
    std::vector<Unit> units_;
    std::vector<Projectile> projectiles_;
    std::vector<ImpactEvent> impacts_;
};

}  // namespace illyria::sim
