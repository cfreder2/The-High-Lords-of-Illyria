#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <map>
#include <span>
#include <unordered_map>
#include <utility>
#include <tuple>
#include <vector>

#include "illyria/sim/edges.h"
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
// A flier's flight cycle (World::updateFlight): a ground run at this times
// its walking pace to take off, a tick of stamina spent for each tick in the
// air, and one restored for every kFlierRestTicks resting on the ground (half
// the rate flight spends it). The campaign marches fliers at its long-run pace.
inline constexpr Fixed kTakeoffRunPace = Fixed::fromRatio(3, 2);
inline constexpr uint32_t kFlierRestTicks = 2;

enum class ProjectileKind : uint8_t {
    Arrow,     // sticks where it lands, blocked by shields
    Bomb,      // a satchel charge: laid, it lies still until a blast or fire sets it off (a burning fuse, then it explodes)
    Fireball,  // explodes on any impact
    Debris,    // something thrown by an explosion (or a deflected arrow); settles as a remain
    Stone,     // a catapult's stone: a high arc, then it smashes whatever it lands on
    Molotov,   // a grenadier's bottle of burning oil: shatters on any impact into a burst of fire, which sets off satchels
};

// Persistent physical leftovers (docs/GAME_DESIGN.md §5.5). They are part of
// the simulation so every peer sees the same battlefield and explosions can
// throw them about again. A Blade is a sword, axe or spear a scavenger took
// from a body and threw (world_scavenge.cpp): it lies where it lands, to be
// taken up again. (A body's own blade is not a remain: it stays with him
// until taken, Unit::looted.)
enum class RemainKind : uint8_t { None, Arrow, Head, Arm, Leg, Bone, Torso, Chunk, Blade };

// What a weapon does to flesh (docs/GAME_DESIGN.md §5.5, the death matrix):
// how the killing blow decides the body that is left, and the sound it makes.
enum class DamageType : uint8_t { Slash, Cleave, Blunt, Pierce, Explosive, Fire };
// How a soldier died (world_death.cpp): fell whole, lost a limb or his head,
// was blown (or hacked) to pieces, was shot dead, or burned.
enum class DeathKind : uint8_t { None, Fell, Severed, Gibbed, Pierced, Charred };
const char* damageTypeName(DamageType t);
const char* deathKindName(DeathKind k);

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
    int32_t drawTicks = 0;    // ticks drawn and aimed at a fresh mark before the first shot (the reload covers later ones)
    int32_t damage = 0;       // direct-hit damage (arrows)
    Fixed dispersionAt100m;   // radius of the dispersion circle at 100 m for a rank-0 unit
    Fixed autoRange;          // idle units shoot at enemies within this range; zero disables
    Fixed maxRange;           // farther targets are approached on foot before shooting
    Fixed minRange;           // nearer targets are not shot at (a catapult would hit itself)
    ProjectileKind kind = ProjectileKind::Arrow;
    DamageType damageType = DamageType::Pierce;
    Explosive explosive;      // bombs and fireballs
};

struct MeleeWeapon {
    int32_t damage = 0;  // zero means the unit cannot fight in melee
    Fixed reach;         // gap between the two units' edges at which blows land
    int32_t cooldownTicks = 0;
    Fixed knockback;              // m/s given to a 1-mass target
    int32_t bonusVsMountedPct = 0;
    DamageType damageType = DamageType::Slash;
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

// A scavenger (world_scavenge.cpp): with no orders he holds and fights like
// anyone; ordered to raid (Command::Raid) he runs his own fight from there,
// taking what lies about the field (a satchel, a blade, a head, a limb) and
// hurling it at the soft and the straggling, as far as he can see.
struct Scavenging {
    Fixed sight;              // zero: not a scavenger. How far he sees, raiding: things to take, men, threats
    Fixed throwSpeed;         // m/s off his hand
    Fixed throwRange;         // the farthest he throws from
    Fixed dispersionAt100m;   // as a bow's, for a recruit
    int32_t windupTicks = 0;  // drawing back before the throw
};

// Satchel charges a unit carries to lay (world_satchels.cpp): each lies
// where it is put until a blast or fire reaches it.
struct Satchels {
    int32_t count = 0;        // carried into battle; zero: none
    Explosive charge;         // each one's blast
    int32_t layTicks = 0;     // between one laid and the next
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
    Scavenging scavenge;
    Satchels satchels;
};

// A body part's soldier type unknown (arrows, and parts from before it was kept).
inline constexpr uint8_t kNoSource = 0xFF;

// What a scavenger is about, and what he holds (world_scavenge.cpp).
struct Scavenger {
    // Raiding: looking about (or cutting down a soft man with his blade),
    // running for a prize, closing to throw it, falling back after.
    enum class Mind : uint8_t { Scout, Fetch, Strike, Retreat };
    enum class Prize : uint8_t { None, Remain, Corpse, Satchel };
    Mind mind = Mind::Scout;
    Prize prize = Prize::None;
    uint32_t prizeRef = 0;      // the remain's slot, the body's id or the satchel's id
    Vec3 goal;                  // where he is running: the prize, round the foes he sees, or the one place he falls back to
    EntityId mark = kNoEntity;  // whom he means to hit
    Vec3 markSeen;              // where the mark stood a tick ago (he leads him)
    int32_t timer = 0;          // the wind-up left, or how much longer he may spend falling back
    // A raid (Command::Raid): turned loose from where he was ordered, until
    // another order, until his pack has lost half its men, or until nothing
    // worth taking has been in his sight for a while; then he holds.
    bool raiding = false;
    bool wary = false;     // driven out of missile troops' reach this raid: he no longer goes looking round
    uint32_t pack = 0;     // the raiders let loose together about him (World::formPacks), by number
    int32_t packMen = 0;   // how many the pack was when the raid began
    Vec3 home;             // where the raid began: he falls back toward it
    int32_t quiet = 0;     // ticks with nothing worth taking in sight
    int32_t replan = 0;    // falling back: ticks before he may choose another place to run to
    EntityId shotBy = kNoEntity;  // raiding, the missile trooper who just struck him, seen or not: he falls back from him
    // In his hands, one thing at a time: a body part or a blade (as the
    // remain it was: kind, whose soldier, of what type), or a satchel charge.
    RemainKind held = RemainKind::None;
    uint8_t heldSource = kNoSource;
    PlayerId heldOwner = 0;
    bool satchel = false;
    int32_t fuse = 0;  // the satchel's fuse left, burning down in his hands
    Explosive blast;   // and its charge
    bool holding() const { return satchel || held != RemainKind::None; }
};

// Player input, the only thing exchanged between lockstep peers.
struct Command {
    // AttackGate: missile troops shoot at the gate nearest `target`.
    // AttackUnit: go after `targetUnit` (of either side: a force attack on a
    // friend only ever when ordered). AttackObject: a gate, wall or piece of
    // scenery (world_orders.cpp says who can harm what). LaySatchel: walk to
    // `target` and lay a satchel charge there (those who carry them). Raid: a
    // scavenger let loose to raid from where he stands (world_scavenge.cpp);
    // it needs no target, and anyone else ignores it.
    // SetGate: the holder's hand on his gate (`objectIndex`; no soldier named): how it is to be kept (`gatePin`).
    enum class Type : uint8_t { Move, AttackGround, Stop, AttackGate, AttackUnit, AttackObject, LaySatchel, Raid, SetGate };
    enum class Object : uint8_t { Gate, Wall, Obstacle };
    enum class GatePin : uint8_t { Auto, Open, Shut };

    PlayerId player = 0;
    EntityId unit = kNoEntity;
    Type type = Type::Stop;
    Vec3 target;
    Fixed speed = {};  // Move: marching pace (m/s) so a squad keeps formation; 0 = the unit's own
    // Move: one of a squad marching from its centre `marchFrom` to `marchTo`
    // (target is this soldier's place there): all follow the squad's route.
    bool march = false;
    Vec3 marchFrom = {}, marchTo = {};
    // Move: the way to face on arrival (Myth's gesture rotation); zero keeps
    // the way he walked.
    Vec3 face = {};
    // Move: a force march, deaf to the enemy on the way.
    bool force = false;
    // AttackUnit: the target, and a friend to rejoin once it is down.
    EntityId targetUnit = kNoEntity;
    EntityId rally = kNoEntity;
    // AttackObject: what (`target` is the spot tapped on it): a gate's or a
    // wall ring's index in the region, or an obstacle's.
    Object object = Object::Gate;
    int32_t objectIndex = -1;
    // SetGate: Auto lets it open to his men bound through and shut behind them; Open and Shut hold it so.
    GatePin gatePin = GatePin::Auto;
    // Shift: add this order to the end of the soldier's queue (an order that replaces his current one and clears the
    // queue when false). A soldier with no order of his own begins it at once. Meaningless for Stop, Raid and SetGate.
    bool queue = false;
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
    // Broken and standing: his flight made no ground last tick (a wall or a crowd has him, or no foe is near to run from
    // and he has stopped to breathe), so he is no longer running and his foes may strike him (World::flee). A beaten
    // garrison's men who have nowhere to go would else stand there unhunted for ever.
    bool cornered = false;
    bool fleeing() const { return routed && !cornered; }
    int32_t calmTicks = 0;  // since the last shock, for recovering
    // Steadiness lent by the veterans and heroes near him (percent off a
    // shock), counted at each morale census; and the ticks before he may
    // call "Hold the line!" again.
    int32_t steady = 0;
    int32_t callCooldown = 0;
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
    Vec3 arriveFacing;      // the way to turn on arrival (zero: as he walked)
    bool gibbed = false;    // blown apart: no corpse, only its parts
    // Taken out of the world (World::removeUnit): nobody, hit points none, drawn and counted by no one. His place in the
    // list stays, so that a unit's id less one is always his place in it (what everything presenting the world relies on).
    bool removed = false;
    bool hasAttackTarget = false;
    Vec3 attackTarget;
    int32_t attackGate = -1;  // the gate the attack target is on, if any
    int32_t reloadRemaining = 0;
    // A bowman's draw (RangedWeapon::drawTicks): ticks of it so far, as of the
    // tick he last held a mark in range; it starts over whenever he leaves it.
    int32_t drawn = 0;
    uint32_t drawTick = 0;
    int32_t meleeCooldown = 0;
    int32_t healCooldown = 0;
    int32_t healCharges = 0;
    // Orders beyond walking and shooting (world_orders.cpp).
    // AttackUnit: the soldier (any side's, when ordered) he goes after; chosen
    // when he picked it himself after a kill (then he lets it run if it flees).
    EntityId targetUnit = kNoEntity;
    bool targetChosen = false;
    Vec3 targetSeen;  // where the target stood last tick: missile troops lead him
    // AttackUnit: the friend to rejoin once the target is down, and where
    // beside him (his old place in the formation).
    EntityId rally = kNoEntity;
    Vec3 rallyOffset;
    // Move: deaf to the enemy on the way (Myth's force march); else he
    // marches with initiative: fights whoever strikes him or crosses his
    // path, on a leash from his route (engageFoe), then marches on.
    bool forceMarch = false;
    EntityId engageFoe = kNoEntity;
    Vec3 routeFrom;          // where his current leg of the route began
    int32_t graceTicks = 0;  // just ordered away from a melee: no initiative yet
    // A marching soldier remembers who last struck at him, and when.
    EntityId lastAttacker = kNoEntity;
    uint32_t attackedTick = 0;
    // How he died (world_death.cpp), for the body left behind and its sound.
    struct Death {
        DeathKind kind = DeathKind::None;
        DamageType cause = DamageType::Blunt;
        Vec3 dir;              // the killing blow's travel, flat and unit length
        int32_t overkill = 0;  // how far below zero health the blow went
        RemainKind severed = RemainKind::None;  // the part cut off
        uint32_t tick = 0;
        // The killing arrow, left in him: its shaft (tip first) and how high up it went in.
        bool arrowPinned = false;
        Vec3 arrowDir;
        Fixed arrowHeight;
    };
    Death death;
    int32_t shieldArrows = 0;  // arrows caught in his shield and stuck there
    bool looted = false;       // dead: a scavenger took his blade
    Scavenger scav;            // scavengers only (UnitStats::scavenge)
    // Struck by something a scavenger threw: by whom, and when. With no
    // order of his own he turns his shield to him (World::shieldTarget).
    EntityId thrower = kNoEntity;
    uint32_t thrownTick = 0;
    // Satchel charges still carried (UnitStats::satchels), the ticks before
    // he may lay the next, and whether he lays one where his move ends (an order).
    int32_t satchels = 0;
    int32_t layCooldown = 0;
    bool layOrdered = false;
    // Orders to come (Command::queue), the next first: each begins when the one before is done or given up (World::advanceQueue).
    // At most World::kMaxQueuedOrders. Kept as the Commands that will be applied, so a later order is planned when it begins.
    std::vector<Command> queue;
    uint32_t orderTick = 0;  // when his current order began (hashed only while a queue waits)

    bool alive() const { return hp > 0; }
    // In the world at all: a man taken out of it (removed) has no hit points either, but is no corpse; every loop over the
    // men that treats the fallen (corpses, gibs, deaths, remains) asks this first. A loop that only takes the living
    // (`alive()`: morale, orders, gates, satchels, matches, targets, sight) needs not: a removed man is hp 0 and is no one
    // of them, and deaths are only ever told of men who died in the world.
    bool present() const { return !removed; }
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
    DamageType cause = DamageType::Pierce;  // the shooter's missile
    bool resting = false;
    bool bleeding = false;  // a fresh body part: leaves blood where it strikes and skids
    Fixed smear;            // distance skidded since its last blood mark
    Explosive explosive;
    // A body part: the type of the soldier it came from, so it is drawn as a
    // piece of him (not hashed: it only chooses the picture).
    uint8_t source = kNoSource;
    // Hurled by a scavenger's hand (shooter is he): a head breaks the man it
    // strikes, a blade cuts him, a satchel bursts where it strikes.
    bool thrown = false;
};

struct Remain {
    RemainKind kind = RemainKind::None;
    PlayerId owner = 0;  // whose soldier a body part came from
    Vec3 pos;
    Vec3 dir;            // arrows: shaft direction (tip first); parts: lying direction
    uint32_t tick = 0;   // when it came to rest
    uint8_t source = kNoSource;  // as Projectile::source
};


// A soldier whose target went down chose what to do next (world_orders.cpp):
// press on against another foe, or go back to his friends (or his march).
// Derived from the simulation like RallyCall, for the voices.
struct OrderDecision {
    EntityId unit = kNoEntity;
    PlayerId owner = 0;
    bool pressOn = false;
    EntityId target = kNoEntity;  // the new foe when pressing on
    int32_t odds = 0;             // his chance against it, per mille, as it truly was
};

// Something that happened this tick, for rendering and audio. Not part of the
// simulation state, but derived only from it, so identical on every peer.
struct ImpactEvent {
    // Bleed: a flung body part struck or skidded here (radius = size of the mark).
    // Smash: a stone shattered here (radius = its reach). Gate: a blow or blast
    // struck a gate (gate = which; killed = it broke).
    // Burst: a Molotov shattered here and its fire spread (radius).
    enum class Kind : uint8_t { Hit, Ground, Blocked, Explosion, Heal, Bleed, Smash, Gate, Burst };
    Kind kind = Kind::Hit;
    Vec3 pos;
    Vec3 dir;                         // direction of the blow or projectile
    EntityId projectile = kNoEntity;  // kNoEntity for a melee blow
    EntityId hitUnit = kNoEntity;     // unit hit, blocking or healed
    bool killed = false;
    Fixed radius;                     // explosions
    int32_t gate = -1;
    DamageType cause = DamageType::Blunt;  // melee blows: the striker's weapon
};

// A soldier killed this tick, and how (the death matrix, world_death.cpp):
// for the sounds and voices. Derived from the simulation like ImpactEvent.
struct DeathEvent {
    EntityId unit = kNoEntity;
    PlayerId owner = 0;
    uint8_t type = 0;  // UnitType
    DeathKind kind = DeathKind::Fell;
    DamageType cause = DamageType::Blunt;
    Vec3 pos;
    Vec3 dir;
    int32_t overkill = 0;
    EntityId killer = kNoEntity;
    RemainKind severed = RemainKind::None;
};

// The killing blow's character, for the death matrix.
struct Blow {
    DamageType type = DamageType::Blunt;
    Vec3 dir;  // travel of the blow or missile (missiles: their velocity)
    Vec3 at;   // where it struck
    bool missile = false;  // an arrow (or other missile) rather than a hand blow
    bool blast = false;    // a blast or stone: explode() throws the pieces itself
};
// Thresholds of the death matrix (world_death.cpp): the overkill at which a
// cut takes off a limb (twice that, the head), and at which a blast, or any
// blow at all, tears a body apart.
inline constexpr int32_t kSeverOverkillSlash = 12;
inline constexpr int32_t kSeverOverkillCleave = 7;
inline constexpr int32_t kGibOverkillBlast = 5;
inline constexpr int32_t kObliterateOverkill = 50;
inline constexpr int32_t kMaxShieldArrows = 5;

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

// A veteran, elite or hero called out to steady wavering or broken friends
// this tick ("Hold the line!"), lifting the morale of those within earshot.
// Derived from the simulation like HeroDeath, for the voices and the HUD.
struct RallyCall {
    enum class Kind : uint8_t { Veteran, Hero };
    EntityId caller = kNoEntity;
    PlayerId owner = 0;
    Vec3 pos;
    Kind kind = Kind::Veteran;
    int32_t heard = 0;    // friends whose morale it lifted
    int32_t rallied = 0;  // of them, broken men it brought back
};

// A hero's hit points grow by this share of a fresh hero's with every level.
inline constexpr int32_t kHeroLevelHpPct = 8;

inline constexpr int32_t kMoraleFull = 1000;
inline constexpr int32_t kMoraleWaver = 450;
inline constexpr int32_t kMoraleBreak = 200;
inline bool Unit::wavering() const { return !routed && morale < kMoraleWaver; }

// The doors take this long to swing open or shut, blocking all the while.
inline constexpr uint32_t kGateSwingTicks = 3 * kTicksPerSecond;

// A castle gate's state in battle; its shape is the Region's Gate.
struct GateState {
    static constexpr PlayerId kNoHolder = 255;
    // A gate swings: Shut and Open stand still; Opening and Closing take kGateSwingTicks, and block like Shut.
    enum class Mode : uint8_t { Shut, Opening, Open, Closing };
    int32_t hp = 0;
    PlayerId holder = kNoHolder;  // whose town it is: they do not hack at it (kNoHolder: nobody's)
    uint32_t brokenTick = 0;      // when it broke (hp 0)
    Mode mode = Mode::Shut;
    uint32_t since = 0;           // the tick the swing began (a reversed swing is dated as if it had begun from the other end)
    // The holder's men bound through it, or the garrison luring a foe in, ask it open until this tick (World::updateGates).
    uint32_t openUntil = 0;
    // The holder's hand on it (Command::SetGate): held open or shut against the asking above.
    Command::GatePin pin = Command::GatePin::Auto;
    // A garrison keeps it (World::keepGates): its men take their stations by role when a foe comes near.
    bool garrisoned = false;
    bool broken() const { return hp <= 0; }
    bool opened() const { return mode == Mode::Open; }
    bool passable() const { return broken() || mode == Mode::Open; }
    // How far open the leaves are, per mille (0 shut, 1000 open), at tick `now`.
    int32_t swing(uint32_t now) const;
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
    // The kind of land each side is at home on (Land bits, edges.h; 0: none): its men strike harder there
    // (kEdgeRealmPct) and walk quicker over it (kRealmPacePct). By PlayerId; sides beyond the array have none.
    std::array<uint8_t, 8> sideLand = {};
};

class World {
public:
    explicit World(const WorldConfig& config);

    EntityId spawnUnit(PlayerId owner, const Vec3& pos, const Vec3& facing, const UnitStats& stats);
    // A unit raised by the campaign: its rank (a hero's level less one) and
    // blessings, with a hero's extra hit points for the levels gained.
    void setVeterancy(EntityId id, int32_t rank, int32_t blessings);
    // The campaign's squads come into the world to fight and leave it after
    // (campaign_world.cpp): a soldier's wounds, kills and morale as he last
    // stood, and taking him out again (the bodies of the fallen stay until then).
    void restoreUnit(EntityId id, int32_t hp, int32_t kills, int32_t morale);
    // The orders waiting behind his own (Unit::queue), as a save keeps them: put back, at most kMaxQueuedOrders.
    void restoreQueue(EntityId id, std::vector<Command> queue);
    void testQueue(EntityId id, const Command& c) { if (Unit* u = mutableUnit(id)) u->queue.push_back(c); }  // (tests)
    // Taking a man out of the world leaves his place in the list, empty (Unit::removed), and the next man to come takes the
    // lowest empty place (so that a world with fights always going on does not grow for ever): a unit's place is his id less
    // one, `units()[id - 1]`, as in any battle. A place is given to a man afresh each time (generation): whatever a
    // presentation keeps by place is kept for the man who was there, and is forgotten when another comes.
    void removeUnit(EntityId id);
    uint32_t generation(EntityId id) const { return id >= 1 && id <= spawns_.size() ? spawns_[id - 1] : 0; }
    // Everything of a fight but the gates (their wounds, and the broken ones
    // open for good): no soldiers, missiles or remains, the dice and the clock
    // as they start. What a campaign's world is between fights, so that it is
    // the same on every machine whether or not it has seen one (campaign_world.cpp).
    void clearFight(uint64_t seed);

    // Advances the simulation by exactly one tick. Every peer must call this
    // with the same commands for the same tick.
    void step(std::span<const Command> commands);

    // Hash of the complete simulation state, compared between peers to detect desyncs.
    uint64_t checksum() const;

    uint32_t tick() const { return tick_; }
    const std::vector<Unit>& units() const { return units_; }
    const std::vector<Projectile>& projectiles() const { return projectiles_; }
    const std::vector<ImpactEvent>& impacts() const { return impacts_; }
    // Heroes killed during the last step (for morale, the HUD and the voices).
    const std::vector<HeroDeath>& heroDeaths() const { return heroDeaths_; }
    // Rally calls made during the last step (world_morale.cpp).
    const std::vector<RallyCall>& rallyCalls() const { return rallyCalls_; }
    // What soldiers whose targets fell chose during the last step.
    const std::vector<OrderDecision>& orderDecisions() const { return decisions_; }
    // Who can harm what (world_orders.cpp): engines and bombers batter walls,
    // houses and trees; anyone can attack a gate (missile troops shoot it,
    // the rest hack at it).
    static bool canBatter(const UnitStats& s);
    // A soldier's fighting power as he sees it (hit points x blows a second x
    // prowess), and his perceived chance, per mille, against `foe` there
    // with the friends and foes about (world_orders.cpp).
    int64_t fightingPower(const Unit& u) const;
    int32_t oddsAgainst(const Unit& u, const Unit& foe) const;
    // Everyone killed during the last step, and how (world_death.cpp).
    const std::vector<DeathEvent>& deathEvents() const { return deathEvents_; }
    // Ring of remains; slots whose kind is None are empty.
    const std::vector<Remain>& remains() const { return remains_; }
    // Remain slots that changed during the last step.
    const std::vector<uint32_t>& changedRemains() const { return changedRemains_; }
    const Unit* findUnit(EntityId id) const;
    // How a soldier is going where he is going, for the paths drawn on the map (read-only: nothing here is in the
    // checksum or changes the step). Idle: nowhere. Hunt: after one quarry, deaf to the rest (an AttackUnit order).
    // Move: a force march, deaf to the enemy on the way. Patrol: marching with initiative, fighting whoever strikes
    // him or crosses his path (the ordinary Move).
    enum class Stance : uint8_t { Idle, Patrol, Move, Hunt };
    static Stance stanceOf(const Unit& u);
    // How a queued order will be walked (the same words: Move for a force march, Hunt for an attack, else Patrol), for drawing the queue.
    static Stance stanceOfOrder(const Command& c);
    // Whether a soldier has an order in hand: a march, an attack, a pursuit or a raid. (Idle soldiers fight on their own.)
    static bool hasOrder(const Unit& u);
    // At most this many orders wait in a soldier's queue; one more is dropped. Eight legs is a patrol round a town or a
    // plan of attack; more would make every man carry, hash and save a long script, and a line of numbers on the screen.
    static constexpr size_t kMaxQueuedOrders = 8;
    // A standing attack on the ground, a wall, a house or a tree never ends of itself: with orders waiting it is over after this long.
    static constexpr uint32_t kStandingAttackTicks = 10 * kTicksPerSecond;
    // The rest of his route: the waypoints of his path still ahead of him, the last where he can get to (a hunter's: the
    // path planned to his quarry, then the quarry's place now). Empty for Idle. Does not include where he stands.
    // It stops at a gate that is shut, opening or closing across it (a swinging gate blocks like a shut one), and goes
    // through one that stands open or is broken.
    // `hidden` (by gate, if given): gates the one asking cannot see, which count as shut whatever they are doing, so
    // that the line's shape never tells their state.
    void routeOf(const Unit& u, std::vector<Vec3>& out, const std::vector<uint8_t>* hidden = nullptr) const;
    // Castle gates, in the order of the region's gates.
    const std::vector<GateState>& gates() const { return gates_; }
    // Battle setup: whose town each gate belongs to, and a gate already battered.
    void setGateHolder(size_t gate, PlayerId holder);
    void setGateHp(size_t gate, int32_t hp);
    // A garrison keeps the gate (keepGates, world_keep.cpp); the pin is the holder's hand on it (Command::SetGate).
    void setGateGarrisoned(size_t gate, bool on);
    void setGatePin(size_t gate, Command::GatePin pin);
    // Whether this man is one of a garrisoned gate's keepers (inside its walls, near it, on its side): the gate's strategy
    // gives him his station, so no other thinking (the computer lords' campaign AI) orders him about.
    bool gateKeeper(const Unit& u) const;
    Fixed groundHeight(Fixed x, Fixed y) const;
    // The least elevation (its tangent) of an arc from `origin` down onto
    // `mark` that passes over everything on the way, up to `shortOf` from it:
    // the ground, rocks, trees, houses, walls and standing gates, but not the
    // trees and bushes within 6 m of the mark (the wood that shelters him).
    // Very low when nothing rises into the way.
    Fixed clearingTan(const Vec3& origin, const Vec3& mark, Fixed shortOf) const;
    const Region* region() const { return config_.region.get(); }
    // In the river: water no one walks in (deep, or beside or under a bridge) or the bank under a bridge's deck, with no
    // deck under his feet at height z. A man thrown or shoved in swims for the bank.
    bool inRiver(Fixed x, Fixed y, Fixed z) const;
    // Whose land each part of the field is (the campaign's territory, which changes as the war goes; a set battle
    // or a match is fought on nobody's): a side for each square of `cell` metres, `nx` to a row from `origin` (a place of the
    // world), 255 for nobody's. A soldier standing on his own side's land has the home-land edge (edges.h). Not part
    // of the world's own state: whoever sets it sets it again when the land changes hands, and after a load.
    void setLandHolders(std::vector<uint8_t> holders, int32_t nx, Fixed cell, const Vec3& origin);
    // The side that holds the land at a place, or -1.
    int32_t landHolderAt(Fixed x, Fixed y) const;
    // The Land bits (edges.h) the side is at home on, and whether a man of it stands on such land now.
    uint8_t sideLand(PlayerId side) const { return side < config_.sideLand.size() ? config_.sideLand[side] : 0; }
    bool onOwnLand(const Unit& u) const;
    bool onOwnLand(const Unit& u, Ground under) const;  // (the ground under him already known)
    // The small edges a soldier fights with (world_edges.cpp, edges.h): his
    // side's land, ground his kind knows, his rank, a hero beside him, and
    // (against a foe dz above him over dist) the high ground.
    Edges edgesOf(const Unit& u, Fixed dist = Fixed::zero(), Fixed dz = Fixed::zero()) const;
    // Damage multipliers, in percent (world_combat.cpp): for where a blow
    // (or missile) lands, and for an arrow plunging at `vel` from above.
    static int32_t sidePct(HitSide side, bool missile);
    static int32_t plungePct(const Vec3& vel);
    // Shakes a unit's morale by `points` (at a man's resolve), scaled by its
    // type's moraleShockPct and steadied by veterancy.
    void shakeMorale(Unit& u, int32_t points);

    // Match rules (world_match.cpp): what a game mode does to the soldiers
    // outside their own doing, the same on every peer.
    // A living soldier mended by up to `amount` (Horde's rest between rounds).
    void heal(EntityId id, int32_t amount);
    // A hero one level up (to 9, tougher at once), a soldier one rank (to elite).
    void levelUp(EntityId id);
    // A fallen soldier raised again (Horde's hero returning between waves): a
    // fresh soldier of his side with his stats (levels and all), rank, kills
    // and blessings, at `hpPct` of his full health, at `pos`. The fallen one
    // stays where he fell. Its id, or kNoEntity when `fallen` is not dead.
    EntityId raiseFallen(EntityId fallen, const Vec3& pos, const Vec3& facing, int32_t hpPct);
    // A side out of the match (its flag taken): every soldier of it breaks,
    // and none ever rallies.
    void breakSide(PlayerId owner);
    bool sideBroken(PlayerId owner) const;
    // A match's field, a circle round `centre` (radius zero: none, the
    // default): a soldier outside it loses `pctPerSecond` of his full health
    // every second, so armies stay on the field the match loaded (and a
    // straggler cannot hide off it).
    void setZone(const Vec3& centre, Fixed radius, int32_t pctPerSecond);
    const Vec3& zoneCentre() const { return zoneCentre_; }
    Fixed zoneRadius() const { return zoneRadius_; }
    bool outsideZone(const Vec3& p) const;
    // A body part (`kind`, from a soldier of `source`'s type, of `owner`'s
    // side) flung from `pos` at `vel`: by a hand, or in tests. Flying fast,
    // it hurts the first man it strikes.
    void launchPart(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& vel, uint8_t source = kNoSource) {
        spawnDebris(kind, owner, pos, vel, source);
    }
    // A satchel charge (`ex`) laid at rest on the ground at `pos`: unlit
    // (`fuseTicks` zero), as a grenadier lays it, it waits for a blast or
    // fire; else its fuse burns that long (in tests). Its id.
    EntityId placeSatchel(PlayerId owner, const Vec3& pos, const Explosive& ex, int32_t fuseTicks = 0);
    // What bursts on impact (fireballs, stones, Molotovs) rather than bouncing or sticking.
    static bool burstsOnImpact(ProjectileKind k) {
        return k == ProjectileKind::Fireball || k == ProjectileKind::Stone || k == ProjectileKind::Molotov;
    }
    // Whether a man can stand at `p`: not inside a building, a wall or deep water (the nav grid).
    bool standable(const Vec3& p) const { return !nav_ || !nav_->blocked(p); }
    // The sandbox's tuning (game mode_sandbox.h; a match never calls it):
    // every living soldier of `type` takes `stats`, his wounds kept in proportion.
    void retune(uint8_t type, const UnitStats& stats);

private:
    // Buckets of indices by grid cell. Lookups only, never iterated, so the
    // unordered map cannot make results depend on hashing order.
    class SpatialHash {
    public:
        explicit SpatialHash(int64_t cellMetres = 4) : cell_(cellMetres), slots_(64) {}
        // Empties every cell, keeping the room the lists grew to (the grid is rebuilt every step).
        void clear() {
            for (const uint32_t s : used_) {
                slots_[s].items.clear();
                slots_[s].used = false;
            }
            used_.clear();
        }
        void insert(uint32_t index, Fixed x, Fixed y) { slot(key(cellOf(x), cellOf(y))).items.push_back(index); }
        void remove(uint32_t index, Fixed x, Fixed y);
        // Calls fn(index) for items whose cell overlaps the square of `radius` around (x, y): cell by cell, row by row,
        // each cell's items in the order they were inserted.
        template <typename Fn>
        void query(Fixed x, Fixed y, Fixed radius, Fn&& fn) const {
            const int64_t x0 = cellOf(x - radius), x1 = cellOf(x + radius);
            const int64_t y0 = cellOf(y - radius), y1 = cellOf(y + radius);
            for (int64_t cy = y0; cy <= y1; ++cy)
                for (int64_t cx = x0; cx <= x1; ++cx) {
                    const Slot* s = find(key(cx, cy));
                    if (s == nullptr) continue;
                    for (uint32_t i : s->items) fn(i);
                }
        }

    private:
        // An open-addressing table of cells (looked up by key, never walked, so its order is not the game's).
        struct Slot {
            int64_t key = 0;
            bool used = false;
            std::vector<uint32_t> items;
        };
        int64_t cellOf(Fixed v) const { return v.floorToInt() >= 0 ? v.floorToInt() / cell_ : (v.floorToInt() - cell_ + 1) / cell_; }
        static int64_t key(int64_t cx, int64_t cy) { return (cx << 32) ^ (cy & 0xFFFFFFFF); }
        size_t home(int64_t k) const { return static_cast<size_t>((static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ull) >> 20) & (slots_.size() - 1); }
        const Slot* find(int64_t k) const {
            for (size_t i = home(k);; i = (i + 1) & (slots_.size() - 1)) {
                if (!slots_[i].used) return nullptr;
                if (slots_[i].key == k) return &slots_[i];
            }
        }
        Slot* find(int64_t k) { return const_cast<Slot*>(std::as_const(*this).find(k)); }
        Slot& slot(int64_t k) {
            if ((used_.size() + 1) * 2 > slots_.size()) grow();
            for (size_t i = home(k);; i = (i + 1) & (slots_.size() - 1)) {
                if (!slots_[i].used) {
                    slots_[i].used = true;
                    slots_[i].key = k;
                    used_.push_back(static_cast<uint32_t>(i));
                    return slots_[i];
                }
                if (slots_[i].key == k) return slots_[i];
            }
        }
        void grow() {
            std::vector<Slot> old(slots_.size() * 2);
            old.swap(slots_);
            used_.clear();
            for (Slot& s : old)
                if (s.used) slot(s.key).items = std::move(s.items);
        }
        int64_t cell_;
        std::vector<Slot> slots_;
        std::vector<uint32_t> used_;  // the slots in use
    };

    // The grounded soldiers by place, rebuilt every step (separateBodies) and asked many times: 4 m cells, each
    // cell's soldiers in one run of a flat list, in the order of their index. insert() collects, build() sorts them
    // into cells; the table is only looked up by key, never walked in its own order. The table holds four cells of a
    // row to a bucket, their soldiers one after the other in the list, so that the cells of a row that a query
    // covers are one run (two at most when it crosses a bucket) and cost one lookup.
    class UnitGrid {
    public:
        void clear() {
            pending_.clear();
            buckets_.clear();
            items_.clear();
        }
        void insert(uint32_t index, Fixed x, Fixed y) { pending_.push_back({key(cellOf(x) >> 2, cellOf(y)), static_cast<unsigned>(cellOf(x) & 3), index}); }
        void build() {
            size_t size = 64;
            while (size < pending_.size() * 3 / 2) size *= 2;
            buckets_.assign(size, Bucket{});
            where_.resize(pending_.size());
            for (size_t k = 0; k < pending_.size(); ++k) {
                Bucket& b = bucket(pending_[k].key);
                b.key = pending_[k].key;
                ++b.off[pending_[k].cell];
                where_[k] = static_cast<uint32_t>(&b - buckets_.data());
            }
            uint32_t at = 0;
            for (Bucket& b : buckets_) {
                uint16_t sum = 0;  // the cells' ends for now, their starts once filled
                for (unsigned c = 0; c < 4; ++c) b.off[c] = sum += b.off[c];
                b.off[4] = sum;
                b.start = at;
                at += sum;
            }
            items_.resize(pending_.size());
            for (size_t k = pending_.size(); k-- > 0;) {  // (backwards, so that each cell's run ends up in index order)
                Bucket& b = buckets_[where_[k]];
                items_[b.start + --b.off[pending_[k].cell]] = pending_[k].index;
            }
        }
        // Calls run(first, last) for the soldiers of the cells that overlap the square of `radius` round (x, y), as
        // ranges of positions in items(): row by row, cell by cell, each cell's in the order of their index.
        template <typename Run>
        void queryRuns(Fixed x, Fixed y, Fixed radius, Run&& run) const {
            if (buckets_.empty()) return;
            const int64_t x0 = cellOf(x - radius), x1 = cellOf(x + radius);
            const int64_t y0 = cellOf(y - radius), y1 = cellOf(y + radius);
            for (int64_t cy = y0; cy <= y1; ++cy)
                for (int64_t bx = x0 >> 2; bx <= x1 >> 2; ++bx) {
                    const int64_t k = key(bx, cy);
                    for (size_t i = home(k);; i = (i + 1) & (buckets_.size() - 1)) {
                        const Bucket& b = buckets_[i];
                        if (b.key == kNone) break;
                        if (b.key != k) continue;
                        const unsigned lo = bx == x0 >> 2 ? static_cast<unsigned>(x0 & 3) : 0, hi = bx == x1 >> 2 ? static_cast<unsigned>(x1 & 3) : 3;
                        if (b.off[lo] != b.off[hi + 1]) run(b.start + b.off[lo], b.start + b.off[hi + 1]);
                        break;
                    }
                }
        }
        // Calls fn(index) for each of those soldiers.
        template <typename Fn>
        void query(Fixed x, Fixed y, Fixed radius, Fn&& fn) const {
            queryRuns(x, y, radius, [&](uint32_t first, uint32_t last) {
                for (uint32_t m = first; m < last; ++m) fn(items_[m]);
            });
        }
        const std::vector<uint32_t>& items() const { return items_; }

    private:
        static constexpr int64_t kCell = 4, kNone = INT64_MIN;
        struct Bucket {
            int64_t key = kNone;
            uint32_t start = 0;
            uint16_t off[5] = {};  // where each of the four cells' soldiers start from `start`, and the end of the last
        };
        struct Pending {
            int64_t key;
            unsigned cell;  // which of the bucket's four
            uint32_t index;
        };
        static int64_t cellOf(Fixed v) { return v.floorToInt() >= 0 ? v.floorToInt() / kCell : (v.floorToInt() - kCell + 1) / kCell; }
        static int64_t key(int64_t bx, int64_t cy) { return (bx << 32) ^ (cy & 0xFFFFFFFF); }
        size_t home(int64_t k) const { return static_cast<size_t>((static_cast<uint64_t>(k) * 0x9E3779B97F4A7C15ull) >> 20) & (buckets_.size() - 1); }
        Bucket& bucket(int64_t k) {
            size_t i = home(k);
            while (buckets_[i].key != kNone && buckets_[i].key != k) i = (i + 1) & (buckets_.size() - 1);
            return buckets_[i];
        }
        std::vector<Pending> pending_;
        std::vector<uint32_t> where_, items_;
        std::vector<Bucket> buckets_;
    };

    // The solid obstacles by place (they never move): 8 m cells over the box they stand in, as one run of records
    // per row of cells, each cell's in the order of their index. A record holds what is needed to tell an obstacle
    // too far to matter, so a query reads little of the obstacles themselves.
    class ObstacleGrid {
    public:
        void build(const std::vector<Obstacle>& obstacles);
        // Calls fn(index) for obstacles whose cell overlaps the square of `radius` round (x, y), cell by cell.
        template <typename Fn>
        void query(Fixed x, Fixed y, Fixed radius, Fn&& fn) const {
            visit(x, y, radius, [&](const Rec& r) { fn(r.index); });
        }
        // As query, less the obstacles that cannot come within `keep` of `at` (those whose box, grown by `keep`, does not
        // hold it). `at` is read afresh for each, for fn may move what it points to: the cells are those of `radius`
        // round where it was at the start, and the rest is tested where it is now, as a loop over all of them would.
        template <typename Fn>
        void queryNear(const Vec3& at, Fixed radius, Fixed keep, Fn&& fn) const {
            visit(at.x, at.y, radius, [&](const Rec& r) {
                const Fixed reach = keep + r.extent;
                if (abs(at.x - r.x) < reach && abs(at.y - r.y) < reach) fn(r.index);
            });
        }

    private:
        static constexpr int64_t kCell = 8;
        struct Rec {
            Fixed x, y, extent;
            uint32_t index;
        };
        static int64_t cellOf(Fixed v) { return v.floorToInt() >= 0 ? v.floorToInt() / kCell : (v.floorToInt() - kCell + 1) / kCell; }
        template <typename Fn>
        void visit(Fixed x, Fixed y, Fixed radius, Fn&& fn) const {
            const int64_t x0 = std::max(cellOf(x - radius) - x0_, int64_t{0}), x1 = std::min(cellOf(x + radius) - x0_, w_ - 1);
            const int64_t y0 = std::max(cellOf(y - radius) - y0_, int64_t{0}), y1 = std::min(cellOf(y + radius) - y0_, h_ - 1);
            for (int64_t cy = y0; cy <= y1 && x0 <= x1; ++cy)
                for (uint32_t k = start_[static_cast<size_t>(cy * w_ + x0)], end = start_[static_cast<size_t>(cy * w_ + x1 + 1)]; k < end; ++k) fn(recs_[k]);
        }
        int64_t x0_ = 0, y0_ = 0, w_ = 0, h_ = 0;  // the box, in cells
        std::vector<uint32_t> start_;
        std::vector<Rec> recs_;
    };

    Unit* mutableUnit(EntityId id);
    // The soldiers by place, for the scans that look at those near one (the
    // nearest enemy, the census, who is in his way): a flat grid of 8 m cells
    // built at the start of each step by counting sort: the cells row by row over the box the soldiers stand in,
    // each cell's soldiers in the order of their index.
    // Soldiers move less than kBroadMargin between builds, so a query looks that
    // much further than it asked and the caller tests the exact distance, which
    // finds exactly what a loop over every soldier would. (On a campaign's
    // ground of thousands of soldiers that loop is the whole cost of a tick.)
    static constexpr int64_t kBroadCell = 8;
    static constexpr int64_t kBroadMargin = 4;
    static constexpr int64_t kBroadCells = 2048;  // (a row of cells is 16 km; farther out is the last cell)
    static int64_t broadCell(int64_t metres) {
        return std::clamp<int64_t>((metres >= 0 ? metres / kBroadCell : (metres - kBroadCell + 1) / kBroadCell) + 1024, 0, kBroadCells - 1);
    }
    std::vector<uint32_t> broadStart_, broadItems_, broadFill_;
    std::vector<PlayerId> broadSide_;  // the side of each item (a soldier never changes his)  // per cell of the box: where its soldiers start in the items
    int64_t broadX0_ = 0, broadY0_ = 0, broadW_ = 0, broadH_ = 0;  // the box, in cells
    bool broadValid_ = false;
    void buildBroad();
    template <typename Fn>
    void nearby(Fixed x, Fixed y, Fixed radius, Fn&& fn) const;
    // As nearby, without the soldiers of one side (told from a flat list of the sides, not by looking at each).
    template <typename Fn>
    void nearbyNotOf(PlayerId side, Fixed x, Fixed y, Fixed radius, Fn&& fn) const;
    void applyCommand(const Command& cmd);
    // Begins an order (everything applyCommand does once the soldier is found and may be ordered). Not the queue's business.
    void startOrder(Unit& u, const Command& cmd);
    // The queue: whether a soldier has an order in hand, whether his current order is over, and the next one's beginning.
    void advanceQueue(Unit& u);
    // Orders (world_orders.cpp).
    void applyOrder(Unit& u, const Command& cmd);
    // Goes after his AttackUnit target: closes and fights it, or keeps to a
    // good bowshot and shoots it.
    void pursue(Unit& u);
    // Marching with initiative: true when he is fighting this tick instead of walking on.
    bool marchInitiative(Unit& u);
    // His target is down: press on against another or go back (OrderDecision).
    void targetDown(Unit& u, bool marching);
    // The enemy within `reach` of him and within the leash of his route, or null.
    Unit* crosser(Unit& u, Fixed reach);
    // How far he is from the leg of his route he is on.
    Fixed offRoute(const Unit& u, const Vec3& p) const;
    // Walks along u.path; returns true at its end.
    bool followPath(Unit& u, Fixed pace);
    // A marching soldier struck by `by` remembers it (to answer him).
    void noteAttack(Unit& target, const Unit& by);
    void updateUnit(Unit& unit);
    void updateFlight(Unit& unit);
    void updateAirborne(Unit& unit);
    // Walks toward `to` at the unit's ground speed; returns true on arrival.
    bool walk(Unit& unit, const Vec3& to, Fixed speedScale = Fixed::one());
    void planPath(Unit& unit, const Vec3& to);
    // A place on the ground sent to inside standing stone or a ruin's base,
    // or nearer it than his standoff: the nearest place clear of it (else
    // he would walk into the stone for ever). Places on a ruin's floor stand.
    Vec3 clearOfStone(const Vec3& p, const UnitStats& stats) const;
    // A soldier's share of its squad's route: the squad's one fastest route
    // from `from` to `goal`, with the soldier's file kept where the ground allows.
    void planMarch(Unit& unit, const Vec3& to, const Vec3& from, const Vec3& goal);
    void groundPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait);
    // Routes onto, along and off castle wall-walks and their ramps (up and
    // down the ramps only, and round by the gates on the ground between);
    // false if neither end is up on a castle's walls (or the unit cannot climb).
    bool planRampartPath(Unit& unit, const Vec3& to);
    void separateBodies();
    void kickRemains(Unit& unit, const Vec3& moveDir);
    // Looses at a target; false (and nothing loosed) when no arc gets there
    // past the walls, gates and trees in the way.
    // `atWalk`: the target is a defender on a wall-walk, shot at direct (never lobbed).
    bool fire(Unit& unit, const Vec3& target, bool atWalk = false);
    // At a mark in range this tick, called every tick he is: true when his
    // reload has run out and his bow is drawn (a fresh mark is drawn on first).
    bool readyToLoose(Unit& unit);
    // Whether a missile launched so flies clear of walls, gates and houses
    // until near its mark (woods catch some arrows; archers shoot into them).
    bool clearFlight(const Vec3& origin, const Vec3& velocity, const Vec3& target, Fixed shortOf) const;
    // Castle gates (world_gates.cpp).
    // The intact gate this soldier should hack at, or -1.
    int gateToHack(Unit& unit);
    // Queued behind others at a shut gate it is ordered through: where along
    // the doors it can get at them itself.
    bool placeAtGate(const Unit& unit, Vec3& spot) const;
    // Come up to a shut gate his orders lead through and that he will not
    // hack at (his own side's, or he has no blow to strike): nobody passes
    // it, so he stops there (or against his own men stopped before it).
    // Wait: his own gate, which is swinging open for him: he stands, his order kept.
    enum class GateHold : uint8_t { Free, Wait, Halt };
    GateHold heldAtGate(const Unit& unit) const;
    // Whether a shut gate lies between a man on the ground and a place on the other side of the walls it opens (a foe there
    // is not to be walked at: the way is round, or through the doors).
    bool shutGateBetween(const Unit& unit, const Vec3& to) const;
    // routeOf's cut: the way up to the first door that is not passable.
    void stopAtShutGate(const Unit& u, std::vector<Vec3>& way, const std::vector<uint8_t>* hidden) const;
    void updateGates();  // a gate opens to its holder's men who are bound through it, and shuts behind them
    void keepGates();    // garrisoned gates: stations by role, the lure (world_keep.cpp)
    void applyGateOrder(const Command& cmd);
    // What a Move order sets on a man (the route is planned after it): the goal, the pace, the way to face there.
    void beginMove(Unit& unit, const Vec3& target, Fixed speed, const Vec3& face);
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
    // Scavengers (world_scavenge.cpp).
    // Raiding (Command::Raid): his own fight from where he was let loose,
    // as far as he can see: fetch, throw, fall back, go again.
    void raid(Unit& u);
    // The raid is over: he holds where he is, keeping what he holds.
    void endRaid(Unit& u);
    // The raiders let loose this tick, in packs: each with those of his side
    // let loose within reach of him (by id, the first leading).
    void formPacks();
    // How many of his pack still stand; its strength about him (the raiders
    // near him, by fightingPower).
    int32_t packLeft(const Unit& u) const;
    int64_t packStrength(const Unit& u) const;
    // The foes within reach of `at`, as a threat to him (the armoured, those
    // who can catch him and braced pikes count more); and the missile troops
    // he sees whose reach covers `spot`, graded by how deep in it the spot
    // lies (`mark` himself excused; those beside him are in the crowd).
    // `covered`: whether any reach covers it. Missile troops seen by any of
    // his pack are known to him (they cry out).
    int64_t crowdAt(const Unit& u, const Vec3& at) const;
    int64_t missileCover(const Unit& u, const Vec3& spot, const Unit* mark, bool* covered) const;
    // His odds, per mille, with his pack about him against a crowd and the missiles.
    int32_t raidOdds(const Unit& u, int64_t crowd, int64_t cover) const;
    // The best foe in sight to throw at (`throwing`) or to cut down, that the
    // odds allow, out of missile troops' reach if any is; or null.
    Unit* choosePrey(Unit& u, bool throwing);
    // Falls back to one place well clear of the foes about him, out of the
    // missile troops' reach if he can, toward where the raid began; runs to
    // it (false once there, or out of time).
    void fallBack(Unit& u);
    Vec3 retreatSpot(const Unit& u) const;
    bool fallingBack(Unit& u, bool think);
    // Nothing worth taking in sight: round the foes he does see, toward their rear.
    void prowl(Unit& u, bool think);
    // Ordered at one soldier with something in hand: he throws it at him
    // (true); empty-handed he goes after him as anyone would (false).
    bool scavengeOrdered(Unit& u);
    // Not raiding, with a satchel burning down in his hands: he throws it.
    void shedBurning(Unit& u);
    // The heavies' answer: an idle soldier struck by a throw from a foe he
    // cannot reach, or with one he cannot catch at hand (`near`), turns his
    // shield to him and holds his place. Whom to face, or null.
    Unit* shieldTarget(Unit& u, Unit* near);
    // A scavenger faster than him and beyond his reach: not worth the chase.
    bool outruns(const Unit& u, const Unit& foe) const;
    // A satchel in his hands burns down; at the last it falls from them.
    void burnHeld(Unit& u);
    // He falls with something in his hands: it drops where he fell.
    void dropHeld(Unit& u);
    // A blast reaching a man with a satchel in his hands sets it off.
    void setOffHeld(Unit& u);
    // Satchel charges (world_satchels.cpp).
    // Lays one where he stands, toward `toward`, and steps back from it.
    void laySatchel(Unit& u, const Vec3& toward);
    // Idle with satchels left and the enemy coming: lays one in their path (true).
    bool layInPath(Unit& u);
    // A Molotov thrower's shot at a satchel lying among the enemy, if he has
    // one (true, whether or not he could loose this tick).
    bool shootSatchel(Unit& u);
    // He falls with satchels still on him: to fire or a blast (`setOff`)
    // they all go up at once, one big blast; else they spill where he falls.
    void packBlast(Unit& u, bool setOff);
    // How a scavenger moves: toward `goal`, round armoured foes, by a route when the way is not clear.
    void scavengeWalk(Unit& u, const Vec3& goal);
    // The armoured foes near him: how close the nearest is inside its
    // danger (positive: too close), and which way is away from them all.
    Fixed scavengeDanger(const Unit& u, Fixed reach, Vec3* away) const;
    // The best thing in sight to take; where to go round the foes he sees.
    bool choosePrize(Unit& u);
    Vec3 prowlGoal(const Unit& u) const;
    // Where his prize is now; false when it is gone (taken, kicked, blown up).
    bool prizeAt(const Unit& u, Vec3& at) const;
    void takePrize(Unit& u);
    // Closes on `mark` with what he holds, draws back and throws it; true once thrown.
    bool strike(Unit& u, Unit& mark);
    // Hurls what he holds at `at`.
    void hurl(Unit& u, const Vec3& at);
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
    // Veterans and heroes who see friends wavering or broken call out to them.
    void callRallies();
    // The morale at which a broken soldier rallies, and how clear of foes he
    // must be: veterans come back sooner.
    int32_t rallyMorale(const Unit& u) const;
    Fixed safeReach(const Unit& u) const;
    void flee(Unit& u);
    void shakeBySide(Unit& u, HitSide side, bool missile);
    // Missile reach from the unit's height over the target: farther down a hill, shorter up one.
    Fixed shotRange(const Unit& u, Fixed base, const Vec3& target) const;
    bool tryHeal(Unit& unit);
    Unit* nearestEnemy(const Unit& unit, Fixed range);
    // Returns true if the blow killed the target.
    bool damage(Unit& target, int32_t amount, Unit* attacker = nullptr, const Blow& blow = {});
    // The death matrix (world_death.cpp): how the killing blow leaves the body.
    void resolveDeath(Unit& victim, const Blow& blow, int32_t overkill, const Unit* killer);
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
    // Damage multiplier, in percent: a hero's levels, blessings, and the edges.
    int32_t prowessPct(const Unit& u) const;
    // The same without the edges: what a hero's levels and blessings add.
    int32_t gracePct(const Unit& u) const;
    // base x pctA% x pctB%, the fraction rounded up by chance in proportion
    // (world_edges.cpp): so a few percent more is a few percent more on
    // average, not nothing until it crosses a whole point.
    int32_t scaledDamage(int32_t base, int32_t pctA, int32_t pctB = 100);
    // The surface a walker at height z stands on here: the ground, or a deck
    // within a step of z (not a wall-walk high above a gate passage).
    Fixed standingHeight(Fixed x, Fixed y, Fixed z) const;
    bool bridgeStep(Fixed x, Fixed y, Fixed z, Fixed ground, Fixed& deck) const;
    // Earliest solid obstacle crossed by segment a->b, as a fraction in [0, 1); -1 if none.
    // `stone` (if given) tells whether it was stone (walls, towers, houses,
    // rocks), off which arrows glance, or wood (trees, gates), which they stick in.
    Fixed obstacleHit(const Vec3& a, const Vec3& b, Fixed limitT, bool masonryOnly = false, bool* stone = nullptr) const;
    // Castle walls (world_walls.cpp).
    // Earliest entry of segment a->b into a wall's stone: its slab below the
    // walk, the parapet, a merlon (not an embrasure); -1 if none.
    Fixed wallHit(const Vec3& a, const Vec3& b, Fixed limitT) const;
    // What a falling thing at height z lands on here: the ground, or a deck
    // not far above it (not the wall-walk over a gate passage it flies through).
    Fixed surfaceBelow(Fixed x, Fixed y, Fixed z) const;
    // A point on a curtain wall's walk: the wall, its direction, and where
    // along it and how far out from its middle line the point lies.
    struct WalkSpot {
        const WallSeg* seg = nullptr;
        Vec3 dir;
        Fixed along, across, len;
    };
    bool walkSpot(const Vec3& p, WalkSpot& spot) const;
    // Keeps a soldier on a wall-walk inside the parapet, the living and the
    // dead (who lie along the walk); stops a body thrown against the parapet.
    void keepOnWalk(Unit& u);
    // A soldier on the walk shooting out over the wall leans into his
    // embrasure (origin moves out to it); false when he stands behind a merlon.
    bool walkShot(const Unit& shooter, const Vec3& target, Vec3& origin) const;
    // Where to aim at a mark standing at `aim` (his feet), `height` tall: a
    // defender up on a wall-walk is shot at in the chest, over the parapet
    // (true; aim moves up), anyone else at their feet.
    bool aimHigh(const Unit& shooter, Vec3& aim, Fixed height) const;
    // A gate breaking lets the arrows stuck in it fall.
    void dropGateArrows(int gate);
    void explode(const Vec3& at, const Explosive& ex, PlayerId owner, ProjectileKind kind = ProjectileKind::Bomb,
                 DamageType cause = DamageType::Explosive);
    void spawnGibs(Unit& victim, const Vec3& from, Fixed strength);
    // A part skidding on a slope too steep to grip rolls and slides on down it (world_death.cpp).
    // Returns true while it is rolling.
    bool rollDownhill(Projectile& p);
    void spawnDebris(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& vel, uint8_t source = kNoSource);
    // The first living man (not `ignore`) a flying thing passes through between two points.
    Unit* debrisHit(const Vec3& a, const Vec3& b, EntityId ignore = kNoEntity);
    void placeRemain(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& dir, uint8_t source = kNoSource);
    void clearRemain(uint32_t slot);
    void applyZone();
    Fixed slopeAt(Fixed x, Fixed y) const;
    bool steeperThan(Fixed x, Fixed y, Fixed limit) const;  // slopeAt(x, y) > limit, without the root
    Fixed slopeSquared(Fixed x, Fixed y) const;
    Ground groundAt(Fixed x, Fixed y) const;
    bool isWater(Fixed x, Fixed y) const;
    // One in the river (inRiver) swims for the nearest ground to stand on, and does nothing else until he stands on it.
    void swimAshore(Unit& u);
    // A bridge's deck holds the men on it against the press of bodies (a blow can still throw one off).
    void keepOnBridge(Unit& u);

    WorldConfig config_;
    std::vector<uint8_t> landHolders_;
    int32_t landNx_ = 0;
    Fixed landCell_ = Fixed::one();
    Vec3 landOrigin_;
    std::unique_ptr<NavGrid> nav_;
    Rng rng_;
    uint32_t tick_ = 0;
    Vec3 zoneCentre_;
    Fixed zoneRadius_;
    int32_t zonePct_ = 0;
    EntityId nextId_ = 1;
    EntityId nextUnitId_ = 1;  // the id of the next new place in units_ (a place's id is its index plus one, so units_ stays sorted by id)
    uint32_t packs_ = 0;  // raiding packs formed so far (formPacks)
    std::vector<Unit> units_;
    std::vector<EntityId> freeSlots_;  // the places men were taken out of, descending: the next man takes the last (the lowest), so a spawn costs O(1) and a removal O(places empty)
    std::vector<uint32_t> spawns_;     // how many men have come to each place (generation)
    std::vector<Projectile> projectiles_;
    std::vector<Projectile> spawned_;  // created while projectiles_ is being updated
    std::vector<ImpactEvent> impacts_;
    std::vector<HeroDeath> heroDeaths_;
    std::vector<RallyCall> rallyCalls_;
    std::vector<OrderDecision> decisions_;
    std::vector<DeathEvent> deathEvents_;
    // Soldiers killed this step (owner, where), for their friends' morale.
    std::vector<std::pair<PlayerId, Vec3>> deaths_;
    std::vector<Remain> remains_;
    std::vector<GateState> gates_;
    std::vector<PlayerId> brokenSides_;  // breakSide, ascending
    uint64_t remainsAdded_ = 0;
    std::vector<uint32_t> changedRemains_;
    struct PendingExplosion {
        Vec3 at;
        Explosive ex;
        PlayerId owner;
        ProjectileKind kind;
        DamageType cause;
    };
    std::vector<PendingExplosion> pendingExplosions_;
    ObstacleGrid obstacleGrid_;
    Fixed obstacleReach_;  // the largest obstacle's reach from its middle (grid queries look this far)
    SpatialHash remainGrid_{4};
    UnitGrid unitGrid_;
    // separateBodies' scratch: where each soldier of unitGrid_ stands and how wide, in the grid's own order (a push
    // moves both, and the copy is kept in step), and where in that order each soldier is.
    struct Body {
        Fixed x, y, radius;
        uint32_t index;
    };
    std::vector<Body> bodies_;
    std::vector<uint32_t> bodyOf_;
    // Paths planned this tick, by (gait, start cell, goal cell): squads share routes.
    std::map<std::tuple<int, int64_t, int64_t>, std::vector<Vec3>> pathCache_;
};

template <typename Fn>
void World::nearby(Fixed x, Fixed y, Fixed radius, Fn&& fn) const {
    if (!broadValid_) {
        for (uint32_t i = 0; i < units_.size(); ++i) fn(i);
        return;
    }
    const int64_t reach = radius.floorToInt() + 1 + kBroadMargin;
    const int64_t px = x.floorToInt(), py = y.floorToInt();
    // The cells asked for that lie in the box; a row of them is one stretch of the items.
    const int64_t cx0 = std::max(broadCell(px - reach) - broadX0_, int64_t{0}), cx1 = std::min(broadCell(px + reach) - broadX0_, broadW_ - 1);
    const int64_t cy0 = std::max(broadCell(py - reach) - broadY0_, int64_t{0}), cy1 = std::min(broadCell(py + reach) - broadY0_, broadH_ - 1);
    for (int64_t cy = cy0; cy <= cy1 && cx0 <= cx1; ++cy)
        for (uint32_t k = broadStart_[static_cast<size_t>(cy * broadW_ + cx0)], end = broadStart_[static_cast<size_t>(cy * broadW_ + cx1 + 1)]; k < end; ++k)
            fn(broadItems_[k]);
}

template <typename Fn>
void World::nearbyNotOf(PlayerId side, Fixed x, Fixed y, Fixed radius, Fn&& fn) const {
    if (!broadValid_) {
        for (uint32_t i = 0; i < units_.size(); ++i) fn(i);
        return;
    }
    const int64_t reach = radius.floorToInt() + 1 + kBroadMargin;
    const int64_t px = x.floorToInt(), py = y.floorToInt();
    const int64_t cx0 = std::max(broadCell(px - reach) - broadX0_, int64_t{0}), cx1 = std::min(broadCell(px + reach) - broadX0_, broadW_ - 1);
    const int64_t cy0 = std::max(broadCell(py - reach) - broadY0_, int64_t{0}), cy1 = std::min(broadCell(py + reach) - broadY0_, broadH_ - 1);
    for (int64_t cy = cy0; cy <= cy1 && cx0 <= cx1; ++cy)
        for (uint32_t k = broadStart_[static_cast<size_t>(cy * broadW_ + cx0)], end = broadStart_[static_cast<size_t>(cy * broadW_ + cx1 + 1)]; k < end; ++k)
            if (broadSide_[k] != side) fn(broadItems_[k]);
}

}  // namespace illyria::sim
