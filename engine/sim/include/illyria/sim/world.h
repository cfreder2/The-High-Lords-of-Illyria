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

enum class ProjectileKind : uint8_t {
    Arrow,     // sticks where it lands, blocked by shields
    Bomb,      // bounces and rolls until its fuse runs out, then explodes
    Fireball,  // explodes on any impact
    Debris,    // something thrown by an explosion (or a deflected arrow); settles as a remain
};

// Persistent physical leftovers (docs/GAME_DESIGN.md §5.5). They are part of
// the simulation so every peer sees the same battlefield and explosions can
// throw them about again.
enum class RemainKind : uint8_t { None, Arrow, Head, Arm, Leg, Bone };

struct Explosive {
    Fixed radius;
    int32_t damage = 0;      // at the centre, falling off linearly to the edge
    Fixed impulse;           // m/s given to a 1-mass body at the centre
    int32_t fuseTicks = 0;   // bombs only
    Fixed bounce;            // vertical restitution for bombs
};

struct RangedWeapon {
    Fixed muzzleSpeed;        // metres/second; zero means the unit has no ranged attack
    int32_t reloadTicks = 0;  // ticks between shots
    int32_t damage = 0;       // direct-hit damage (arrows)
    Fixed dispersionAt100m;   // radius of the dispersion circle at 100 m for a rank-0 unit
    Fixed autoRange;          // idle units shoot at enemies within this range; zero disables
    ProjectileKind kind = ProjectileKind::Arrow;
    Explosive explosive;      // bombs and fireballs
};

struct MeleeWeapon {
    int32_t damage = 0;  // zero means the unit cannot fight in melee
    Fixed reach;         // gap between the two units' edges at which blows land
    int32_t cooldownTicks = 0;
    Fixed knockback;              // m/s given to a 1-mass target
    int32_t bonusVsMountedPct = 0;
};

// A physical shield covering a frontal arc (§3.2). Blows and arrows from
// inside the arc are blocked with this probability; flanks are unprotected.
struct Shield {
    Fixed blockChance;  // 0..1; zero means no shield
    Fixed arcCos = Fixed::fromRatio(1, 2);  // cos(half-angle): 0.5 == 120 degree cone
};

struct Healing {
    int32_t amount = 0;  // zero means not a healer
    Fixed range;
    int32_t cooldownTicks = 0;
    int32_t charges = 0;
};

// Melee damage bonuses by ground, after Warlords' terrain bonuses.
struct TerrainBonus {
    int32_t openPct = 0;   // flat ground
    int32_t hillsPct = 0;  // slopes
};

struct UnitStats {
    uint8_t type = 0;  // UnitType, for presentation
    Fixed radius;
    Fixed height;
    Fixed moveSpeed;  // metres/second
    int32_t maxHp = 1;
    RangedWeapon ranged;
    MeleeWeapon melee;
    Fixed aggroRange;  // idle units close to melee with enemies within this range
    Shield shield;
    Healing healing;
    TerrainBonus terrain;
    Fixed flyAltitude;  // cruising height above ground; zero for ground units
    Fixed mass = Fixed::one();
    bool mounted = false;
    bool bleeds = true;  // false: bones and dust instead of blood
};

// One soldier (or beast). Squads are a campaign-layer grouping of these.
struct Unit {
    EntityId id = kNoEntity;
    PlayerId owner = 0;
    UnitStats stats;
    int32_t hp = 0;
    int32_t rank = 0;  // veterancy; shrinks the dispersion circle

    Vec3 pos;
    Vec3 facing;  // unit-length, horizontal
    Vec3 vel;     // only while thrown through the air
    bool airborne = false;
    bool engaged = false;  // fought in melee this tick (fliers swoop down)

    bool hasMoveTarget = false;
    Vec3 moveTarget;
    bool hasAttackTarget = false;
    Vec3 attackTarget;
    int32_t reloadRemaining = 0;
    int32_t meleeCooldown = 0;
    int32_t healCooldown = 0;
    int32_t healCharges = 0;

    bool alive() const { return hp > 0; }
};

struct Projectile {
    EntityId id = kNoEntity;
    EntityId shooter = kNoEntity;
    PlayerId owner = 0;
    ProjectileKind kind = ProjectileKind::Arrow;
    RemainKind remain = RemainKind::None;  // what a Debris projectile settles as
    Vec3 pos;
    Vec3 vel;
    int32_t damage = 0;
    int32_t ageTicks = 0;
    int32_t fuse = 0;
    bool resting = false;
    Explosive explosive;
};

struct Remain {
    RemainKind kind = RemainKind::None;
    PlayerId owner = 0;  // whose soldier a body part came from
    Vec3 pos;
    Vec3 dir;            // arrows: shaft direction (tip first); parts: lying direction
    uint32_t tick = 0;   // when it came to rest
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
    enum class Kind : uint8_t { Hit, Ground, Blocked, Explosion, Heal };
    Kind kind = Kind::Hit;
    Vec3 pos;
    Vec3 dir;                         // direction of the blow or projectile
    EntityId projectile = kNoEntity;  // kNoEntity for a melee blow
    EntityId hitUnit = kNoEntity;     // unit hit, blocking or healed
    bool killed = false;
    Fixed radius;                     // explosions
};

struct WorldConfig {
    uint64_t seed = 0;
    Fixed gravity = Fixed::fromRatio(981, 100);
    // Constant acceleration applied to projectiles, in m/s^2.
    Vec3 wind;
    // Ground; a flat plane at z == 0 when null.
    std::shared_ptr<const Terrain> terrain;
    // Remains are kept in a ring; the oldest are recycled beyond this.
    uint32_t maxRemains = 40000;
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
    // Ring of remains; slots whose kind is None are empty.
    const std::vector<Remain>& remains() const { return remains_; }
    // Remain slots that changed during the last step.
    const std::vector<uint32_t>& changedRemains() const { return changedRemains_; }
    const Unit* findUnit(EntityId id) const;
    Fixed groundHeight(Fixed x, Fixed y) const;

private:
    Unit* mutableUnit(EntityId id);
    void applyCommand(const Command& cmd);
    void updateUnit(Unit& unit);
    void updateAirborne(Unit& unit);
    void fire(Unit& unit, const Vec3& target);
    void fight(Unit& unit, Unit& enemy);
    bool tryHeal(Unit& unit);
    Unit* nearestEnemy(const Unit& unit, Fixed range);
    // Returns true if the blow killed the target.
    bool damage(Unit& target, int32_t amount);
    bool shieldBlocks(const Unit& target, const Vec3& incomingDir, Fixed chanceScale);
    void knock(Unit& target, const Vec3& dir, Fixed impulse);
    void updateProjectiles();
    // Integrates a bouncing body; returns true once it has come to rest.
    bool bounce(Projectile& p, Fixed restitution);
    void explode(const Vec3& at, const Explosive& ex, PlayerId owner);
    void spawnGibs(const Unit& victim, const Vec3& from, Fixed strength);
    void spawnDebris(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& vel);
    void placeRemain(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& dir);
    Fixed slopeAt(Fixed x, Fixed y) const;

    WorldConfig config_;
    Rng rng_;
    uint32_t tick_ = 0;
    EntityId nextId_ = 1;
    std::vector<Unit> units_;
    std::vector<Projectile> projectiles_;
    std::vector<Projectile> spawned_;  // created while projectiles_ is being updated
    std::vector<ImpactEvent> impacts_;
    std::vector<Remain> remains_;
    uint64_t remainsAdded_ = 0;
    std::vector<uint32_t> changedRemains_;
    struct PendingExplosion {
        Vec3 at;
        Explosive ex;
        PlayerId owner;
    };
    std::vector<PendingExplosion> pendingExplosions_;
};

}  // namespace illyria::sim
