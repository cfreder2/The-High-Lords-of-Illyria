#pragma once

#include <cstdint>
#include <memory>
#include <map>
#include <span>
#include <unordered_map>
#include <utility>
#include <tuple>
#include <vector>

#include "illyria/sim/fixed.h"
#include "illyria/sim/gait.h"
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
    Stone,     // a catapult's stone: a high arc, then it smashes whatever it lands on
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
    Fixed minRange;           // nearer targets are not shot at (a catapult would hit itself)
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
    Gait gait = Gait::Foot;  // speed over each kind of ground (gait.h)
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
    // Morale (world_morale.cpp): how hard shocks shake it, in percent of a
    // man's; 0 never breaks (the mindless dead, heroes).
    int32_t moraleShockPct = 100;
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
    // The enemy it is trading blows with: it keeps to that one while in reach,
    // so anyone else coming at its flank or rear finds it off guard.
    EntityId foe = kNoEntity;
    // A rider's momentum: speed built up at the gallop (m/s), spent in the
    // first blow of a charge, lost on stopping or in melee; and the grade of
    // that run (negative downhill), which multiplies or saps the impact.
    Fixed momentum;
    Fixed chargeGrade;
    // Morale, 0..kMoraleFull: shaken by losses, blows from behind, charges,
    // being outnumbered and a hero's fall; it wavers below kMoraleWaver and
    // breaks below kMoraleBreak, fleeing and ignoring orders until it rallies.
    int32_t morale = 1000;
    bool routed = false;
    int32_t calmTicks = 0;  // since the last shock, for recovering
    bool wavering() const;

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
    int32_t attackGate = -1;  // the gate the attack target is on, if any
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
    // AttackGate: missile troops shoot at the gate nearest `target`.
    enum class Type : uint8_t { Move, AttackGround, Stop, AttackGate };

    PlayerId player = 0;
    EntityId unit = kNoEntity;
    Type type = Type::Stop;
    Vec3 target;
    Fixed speed = {};  // Move: marching pace (m/s) so a squad keeps formation; 0 = the unit's own
    // Move: one of a squad marching from its centre `marchFrom` to `marchTo`
    // (target is this soldier's place there): all follow the squad's route.
    bool march = false;
    Vec3 marchFrom = {}, marchTo = {};
};

// Something that happened this tick, for rendering and audio. Not part of the
// simulation state, but derived only from it, so identical on every peer.
struct ImpactEvent {
    // Bleed: a flung body part struck or skidded here (radius = size of the mark).
    // Smash: a stone shattered here (radius = its reach). Gate: a blow or blast
    // struck a gate (gate = which; killed = it broke).
    enum class Kind : uint8_t { Hit, Ground, Blocked, Explosion, Heal, Bleed, Smash, Gate };
    Kind kind = Kind::Hit;
    Vec3 pos;
    Vec3 dir;                         // direction of the blow or projectile
    EntityId projectile = kNoEntity;  // kNoEntity for a melee blow
    EntityId hitUnit = kNoEntity;     // unit hit, blocking or healed
    bool killed = false;
    Fixed radius;                     // explosions
    int32_t gate = -1;
};

// Where a blow or missile lands on its target, by the way the target faces
// (world_combat.cpp): the front, a flank, or the rear.
enum class HitSide : uint8_t { Front, Flank, Rear };
HitSide hitSide(const Vec3& targetFacing, const Vec3& travel);
// A veteran's shield pips, 0 to 3: a soldier's rank (regular, veteran,
// elite), or a hero's level in thirds. What the health bar shows.
int32_t veteranGrade(const Unit& u);

// A hero fell this tick (for morale, the HUD, a campaign log). Like
// ImpactEvent, derived from the simulation state and not part of it.
struct HeroDeath {
    EntityId hero = kNoEntity;
    PlayerId owner = 0;
    Vec3 pos;
    EntityId killer = kNoEntity;  // kNoEntity for a blast or anything without a striker
};

inline constexpr int32_t kMoraleFull = 1000;
inline constexpr int32_t kMoraleWaver = 450;
inline constexpr int32_t kMoraleBreak = 200;
inline bool Unit::wavering() const { return !routed && morale < kMoraleWaver; }

// A castle gate's state in battle; its shape is the Region's Gate.
struct GateState {
    static constexpr PlayerId kNoHolder = 255;
    int32_t hp = 0;
    PlayerId holder = kNoHolder;  // whose town it is: they do not hack at it (kNoHolder: nobody's)
    uint32_t brokenTick = 0;      // when it broke (hp 0)
    bool broken() const { return hp <= 0; }
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
    // Heroes killed during the last step: the hook for morale (not yet modelled).
    const std::vector<HeroDeath>& heroDeaths() const { return heroDeaths_; }
    // Ring of remains; slots whose kind is None are empty.
    const std::vector<Remain>& remains() const { return remains_; }
    // Remain slots that changed during the last step.
    const std::vector<uint32_t>& changedRemains() const { return changedRemains_; }
    const Unit* findUnit(EntityId id) const;
    // Castle gates, in the order of the region's gates.
    const std::vector<GateState>& gates() const { return gates_; }
    // Battle setup: whose town each gate belongs to, and a gate already battered.
    void setGateHolder(size_t gate, PlayerId holder);
    void setGateHp(size_t gate, int32_t hp);
    Fixed groundHeight(Fixed x, Fixed y) const;
    const Region* region() const { return config_.region.get(); }
    // Damage multipliers, in percent (world_combat.cpp): for where a blow
    // (or missile) lands, and for an arrow plunging at `vel` from above.
    static int32_t sidePct(HitSide side, bool missile);
    static int32_t plungePct(const Vec3& vel);
    // Shakes a unit's morale by `points` (at a man's resolve), scaled by its
    // type's moraleShockPct and steadied by veterancy.
    void shakeMorale(Unit& u, int32_t points);

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
    // A soldier's share of its squad's route: the squad's one fastest route
    // from `from` to `goal`, with the soldier's file kept where the ground allows.
    void planMarch(Unit& unit, const Vec3& to, const Vec3& from, const Vec3& goal);
    void groundPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait);
    // Routes onto, along and off castle wall-walks by their stairs; false if neither end is on one.
    bool planRampartPath(Unit& unit, const Vec3& to);
    void separateBodies();
    void kickRemains(Unit& unit, const Vec3& moveDir);
    void fire(Unit& unit, const Vec3& target);
    // Castle gates (world_gates.cpp).
    // The intact gate this soldier should hack at, or -1.
    int gateToHack(Unit& unit);
    // Queued behind others at a shut gate it is ordered through: where along
    // the doors it can get at them itself.
    bool placeAtGate(const Unit& unit, Vec3& spot) const;
    void hackGate(Unit& unit, int gate);
    void damageGate(int gate, int32_t amount, const Vec3& at, const Vec3& dir);
    // Earliest crossing of an intact gate by segment a->b, as a fraction; -1 if none.
    Fixed gateHit(const Vec3& a, const Vec3& b, Fixed limitT) const;
    // Pushes a walker out of the intact gates.
    void keepOutOfGates(Unit& unit);
    // Blasts and stones batter the gates within reach.
    void blastGates(const Vec3& at, const Explosive& ex, ProjectileKind kind);
    // Where to aim at a gate from `from`: its face on that side, half way up.
    Vec3 gateAimPoint(int gate, const Vec3& from) const;
    // A route to a target beyond a shut gate: up to the gate's face (false if no gate lies between).
    bool routeToGate(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait);
    void fight(Unit& unit, Unit& enemy);
    // Battle mechanics in world_combat.cpp.
    // The enemy to fight: the foe still in reach, or the nearest within aggro range.
    Unit* meleeTarget(Unit& unit, EntityId lastFoe);
    // Turns toward a flat unit direction at the unit's own pace; true once facing it.
    bool turnToward(Unit& unit, const Vec3& dir) const;
    // Melee damage multiplier (percent) of a blow from `attacker` to `target`,
    // before facing: ground, high ground, prowess, the bonus against riders.
    int32_t blowPct(const Unit& attacker, const Unit& target, Fixed dist, Fixed dz) const;
    // A rider's first blow with momentum behind it.
    bool chargeReady(const Unit& unit) const;
    void charge(Unit& unit, Unit& enemy, const Vec3& dir, Fixed dist, Fixed dz);
    // Builds or spends a rider's momentum from how far it moved this tick.
    void trackMomentum(Unit& unit, const Vec3& before);
    // Veterancy beyond damage: quicker blows and reloads, quicker to rise, harder to hurt.
    int32_t veteranSteps(const Unit& u) const;
    int32_t attackTicks(const Unit& u, int32_t baseTicks) const;
    int32_t recoverTicks(const Unit& u) const;
    int32_t defendedDamage(const Unit& u, int32_t amount) const;
    // Morale (world_morale.cpp): shocks from this tick's deaths, the census of
    // friends and foes, recovery, breaking and rallying; and a broken unit's flight.
    void updateMorale();
    void flee(Unit& u);
    void shakeBySide(Unit& u, HitSide side, bool missile);
    // Missile reach from the unit's height over the target: farther down a hill, shorter up one.
    Fixed shotRange(const Unit& u, Fixed base, const Vec3& target) const;
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
    void explode(const Vec3& at, const Explosive& ex, PlayerId owner, ProjectileKind kind = ProjectileKind::Bomb);
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
    std::vector<HeroDeath> heroDeaths_;
    // Soldiers killed this step (owner, where), for their friends' morale.
    std::vector<std::pair<PlayerId, Vec3>> deaths_;
    std::vector<Remain> remains_;
    std::vector<GateState> gates_;
    uint64_t remainsAdded_ = 0;
    std::vector<uint32_t> changedRemains_;
    struct PendingExplosion {
        Vec3 at;
        Explosive ex;
        PlayerId owner;
        ProjectileKind kind;
    };
    std::vector<PendingExplosion> pendingExplosions_;
    SpatialHash obstacleGrid_{8};
    SpatialHash remainGrid_{4};
    SpatialHash unitGrid_{4};
    // Paths planned this tick, by (gait, start cell, goal cell): squads share routes.
    std::map<std::tuple<int, int64_t, int64_t>, std::vector<Vec3>> pathCache_;
};

}  // namespace illyria::sim
