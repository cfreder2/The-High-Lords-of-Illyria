#pragma once

#include <cstdint>
#include <memory>
#include <map>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

#include "illyria/sim/fixed.h"
#include "illyria/sim/nav.h"
#include "illyria/sim/region.h"
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
enum class RemainKind : uint8_t { None, Arrow, Head, Arm, Leg, Bone, Torso, Chunk };

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
    Fixed maxRange;           // farther targets are approached on foot before shooting
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
    // Fliers walk by default. They take off only for long moves, after a
    // ground run, cruise over the terrain, tire, and must land to recover.
    // They never fight from the air.
    Fixed flyAltitude;       // cruising height above ground; zero for ground units
    Fixed flySpeed;          // airspeed (moveSpeed is their ground speed)
    Fixed takeoffRun;        // ground run before lifting off
    Fixed minFlight;         // shorter moves are walked
    int32_t staminaTicks = 0;  // flight time on a full rest
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
    // Veterancy, earned by kills (World::promote). Soldiers rise from recruit
    // (0) through regular and veteran to elite (3); a hero's rank is their
    // level less one (0-8). Each rank hits harder, blocks more often and
    // shoots tighter; a hero also leads (see kLeadershipReach).
    int32_t rank = 0;
    int32_t kills = 0;
    int32_t blessings = 0;  // from temples, each like one more rank in a fight

    Vec3 pos;
    Vec3 facing;  // unit-length, horizontal
    Vec3 vel;     // only while thrown through the air
    bool airborne = false;  // thrown by a blow or blast (not flying)
    bool engaged = false;   // fought in melee this tick

    enum class Flight : uint8_t { Grounded, TakingOff, Flying, Landing };
    Flight flight = Flight::Grounded;
    Fixed runway;           // distance run so far while taking off
    int32_t stamina = 0;    // flight ticks left

    std::vector<Vec3> path;  // waypoints of the current move order
    uint32_t pathIndex = 0;

    bool hasMoveTarget = false;
    Vec3 moveTarget;
    Fixed marchSpeed;       // pace of the current move order (0 = own speed)
    bool gibbed = false;    // blown apart: no corpse, only its parts
    bool hasAttackTarget = false;
    Vec3 attackTarget;
    int32_t reloadRemaining = 0;
    int32_t meleeCooldown = 0;
    int32_t healCooldown = 0;
    int32_t healCharges = 0;

    bool alive() const { return hp > 0; }
    bool inAir() const { return airborne || flight == Flight::Flying || flight == Flight::Landing; }
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
    bool bleeding = false;  // a fresh body part: leaves blood where it strikes and skids
    Fixed smear;            // distance skidded since its last blood mark
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
    Fixed speed = {};  // Move: marching pace (m/s) so a squad keeps formation; 0 = the unit's own
};

// Something that happened this tick, for rendering and audio. Not part of the
// simulation state, but derived only from it, so identical on every peer.
struct ImpactEvent {
    // Bleed: a flung body part struck or skidded here (radius = size of the mark).
    enum class Kind : uint8_t { Hit, Ground, Blocked, Explosion, Heal, Bleed };
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
    // Ground; a flat plane at z == 0 when null. Ignored when `region` is set.
    std::shared_ptr<const Terrain> terrain;
    // A full region: terrain plus water, ground types and scenery.
    std::shared_ptr<const Region> region;
    // Remains are kept in a ring; the oldest are recycled beyond this.
    uint32_t maxRemains = 40000;
};

class World {
public:
    explicit World(const WorldConfig& config);

    EntityId spawnUnit(PlayerId owner, const Vec3& pos, const Vec3& facing, const UnitStats& stats);
    // A unit raised by the campaign: its rank (a hero's level less one) and
    // blessings, with a hero's extra hit points for the levels gained.
    void setVeterancy(EntityId id, int32_t rank, int32_t blessings);

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
    const Region* region() const { return config_.region.get(); }

private:
    // Buckets of indices by grid cell. Lookups only, never iterated, so the
    // unordered map cannot make results depend on hashing order.
    class SpatialHash {
    public:
        explicit SpatialHash(int64_t cellMetres = 4) : cell_(cellMetres) {}
        void clear() { buckets_.clear(); }
        void insert(uint32_t index, Fixed x, Fixed y) { buckets_[key(cellOf(x), cellOf(y))].push_back(index); }
        void remove(uint32_t index, Fixed x, Fixed y);
        // Calls fn(index) for items whose cell overlaps the square of `radius` around (x, y).
        template <typename Fn>
        void query(Fixed x, Fixed y, Fixed radius, Fn&& fn) const {
            const int64_t x0 = cellOf(x - radius), x1 = cellOf(x + radius);
            const int64_t y0 = cellOf(y - radius), y1 = cellOf(y + radius);
            for (int64_t cy = y0; cy <= y1; ++cy)
                for (int64_t cx = x0; cx <= x1; ++cx) {
                    auto it = buckets_.find(key(cx, cy));
                    if (it == buckets_.end()) continue;
                    for (uint32_t i : it->second) fn(i);
                }
        }

    private:
        int64_t cellOf(Fixed v) const { return v.floorToInt() >= 0 ? v.floorToInt() / cell_ : (v.floorToInt() - cell_ + 1) / cell_; }
        static int64_t key(int64_t cx, int64_t cy) { return (cx << 32) ^ (cy & 0xFFFFFFFF); }
        int64_t cell_;
        std::unordered_map<int64_t, std::vector<uint32_t>> buckets_;
    };

    Unit* mutableUnit(EntityId id);
    void applyCommand(const Command& cmd);
    void updateUnit(Unit& unit);
    void updateFlight(Unit& unit);
    void updateAirborne(Unit& unit);
    // Walks toward `to` at the unit's ground speed; returns true on arrival.
    bool walk(Unit& unit, const Vec3& to, Fixed speedScale = Fixed::one());
    void planPath(Unit& unit, const Vec3& to);
    void groundPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out);
    // Routes onto, along and off castle wall-walks by their stairs; false if neither end is on one.
    bool planRampartPath(Unit& unit, const Vec3& to);
    void separateBodies();
    void kickRemains(Unit& unit, const Vec3& moveDir);
    void fire(Unit& unit, const Vec3& target);
    void fight(Unit& unit, Unit& enemy);
    bool tryHeal(Unit& unit);
    Unit* nearestEnemy(const Unit& unit, Fixed range);
    // Returns true if the blow killed the target.
    bool damage(Unit& target, int32_t amount, Unit* attacker = nullptr);
    bool shieldBlocks(const Unit& target, const Vec3& incomingDir, Fixed chanceScale);
    void knock(Unit& target, const Vec3& dir, Fixed impulse);
    void updateProjectiles();
    // Integrates a bouncing body; returns true once it has come to rest.
    bool bounce(Projectile& p, Fixed restitution, Fixed friction);
    // Debris (body parts, thrown arrows): low bounces that keep their speed,
    // then a skid to rest. Returns true once at rest.
    bool tumble(Projectile& p);
    // Credits a kill and promotes the killer when it has earned a rank.
    void creditKill(Unit& killer);
    // Damage multiplier, in percent, from the unit's rank, blessings and any
    // hero leading it.
    int32_t prowessPct(const Unit& u) const;
    // The surface a walker at height z stands on here: the ground, or a deck
    // within a step of z (not a wall-walk high above a gate passage).
    Fixed standingHeight(Fixed x, Fixed y, Fixed z) const;
    // True for a defender on a wall-walk struck from outside (behind the merlons).
    bool onRampartFacing(const Unit& u, const Vec3& vel) const;
    // Earliest solid obstacle crossed by segment a->b, as a fraction in [0, 1); -1 if none.
    Fixed obstacleHit(const Vec3& a, const Vec3& b, Fixed limitT) const;
    void explode(const Vec3& at, const Explosive& ex, PlayerId owner);
    void spawnGibs(Unit& victim, const Vec3& from, Fixed strength);
    void spawnDebris(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& vel);
    void placeRemain(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& dir);
    void clearRemain(uint32_t slot);
    Fixed slopeAt(Fixed x, Fixed y) const;
    Ground groundAt(Fixed x, Fixed y) const;
    bool isWater(Fixed x, Fixed y) const;

    WorldConfig config_;
    std::unique_ptr<NavGrid> nav_;
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
    SpatialHash obstacleGrid_{8};
    SpatialHash remainGrid_{4};
    SpatialHash unitGrid_{4};
    // Paths planned this tick, by (start cell, goal cell): squads share routes.
    std::map<std::pair<int64_t, int64_t>, std::vector<Vec3>> pathCache_;
};

}  // namespace illyria::sim
