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
// the rate flight spends it). The conquest marches fliers at its long-run pace.
inline constexpr Fixed kTakeoffRunPace = Fixed::fromRatio(3, 2);
inline constexpr uint32_t kFlierRestTicks = 2;

enum class ProjectileKind : uint8_t {
    Arrow,     // sticks where it lands, blocked by shields
    Bomb,      // a satchel charge: laid, it lies still until a blast or fire sets it off (a burning fuse, then it explodes)
    Fireball,  // explodes on any impact
    Debris,    // something thrown by an explosion (or a deflected arrow); settles as a remain
    Stone,     // a catapult's boulder: a high arc, a smash where it comes down, and then it bounces and rolls on (world_boulder.cpp)
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
    // Missiles carried into battle, none made there (zero: as many as he likes, a bowman's quiver). A man with a
    // store is thrifty with it on his own (World::worthAShot) and out of it has only his hands.
    int32_t ammo = 0;
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

// How a squad stands (the game's formations, game::Formation, with one more for "none"). The world knows each man's so
// that every formation has its strength and its cost (world_formation.cpp). None: a man of no squad, or one never formed.
enum class FormationKind : uint8_t { None, Line, DoubleLine, Box, Wedge, Loose, Encircle, Vee };
inline constexpr int kFormationKinds = 8;

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
    // SetStance: the soldier's stance with no order in hand (`hold`): Hold keeps his place, else he goes to the help of a
    // friend fighting near him (world_tactics.cpp). It touches no order he has. Rally: the lord's call (`unit` is the
    // hero): every broken man of his side in earshot turns back at once, and then he must wait before he calls again.
    // SetFormation: the formation the soldier is to keep (`formation`): no order of his own either. Answer: he goes to the
    // help of the friend `targetUnit`, who has called (world_help.cpp), as a man with no order would of himself.
    // Detonate: one missile, now, at the laid satchel nearest `target` (within World::kDetonatePick of it; else at
    // the ground there), walking into reach first: no waiting on friends in the blast, which is the player's business
    // (world_satchels.cpp). HoldFire: with `hold`, he throws, looses and lays nothing of his own accord (orders only).
    enum class Type : uint8_t { Move, AttackGround, Stop, AttackGate, AttackUnit, AttackObject, LaySatchel, Raid, SetGate, SetStance, Rally, SetFormation, Answer, Detonate, HoldFire };
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
    // Move: a force march, deaf to the enemy on the way. LaySatchel: laid beforehand (the deployment, before the battle
    // begins: Battle sets it only then): the charge lies there at once, if the place is within World::kPrepareReach of
    // him and no foe is within World::kPrepareClear of it; else he walks there and lays it as ever.
    bool force = false;
    // Move: ground to be occupied (a storm's objective, world_siege.cpp): once sent there he comes back to it after a fight
    // or a rout, until he is given another order. (Not the stance below: a man occupying ground still goes to his friends.)
    bool occupy = false;
    // AttackUnit: the target, and a friend to rejoin once it is down.
    EntityId targetUnit = kNoEntity;
    EntityId rally = kNoEntity;
    // AttackObject: what (`target` is the spot tapped on it): a gate's or a
    // wall ring's index in the region, or an obstacle's.
    Object object = Object::Gate;
    int32_t objectIndex = -1;
    // SetGate: Auto lets it open to his men bound through and shut behind them; Open and Shut hold it so.
    GatePin gatePin = GatePin::Auto;
    // SetStance: stand fast (true) or support (false).
    bool hold = false;
    // SetFormation: which.
    FormationKind formation = FormationKind::None;
    // Shift: add this order to the end of the soldier's queue (an order that replaces his current one and clears the
    // queue when false). A soldier with no order of his own begins it at once. Meaningless for Stop, Raid and SetGate.
    bool queue = false;
};

// One soldier (or beast). Squads are a conquest-layer grouping of these.
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
    // The stance of a man with no order (Command::SetStance): Hold keeps his place and strikes only what comes to him; else
    // (support, the default) he goes to the help of a friend fighting within kSupportReach: `support` is the foe he has
    // gone to meet, looked for afresh a few times a second (world_tactics.cpp).
    bool hold = false;
    EntityId support = kNoEntity;
    // A lord: ticks before he may call the Rally again (Command::Rally).
    int32_t rallyCooldown = 0;
    // His squad (0: none; the game's, or a Conquest army's id) and the formation it keeps (world_formation.cpp). `ranked`:
    // squad-mates within a few strides of him at the last census (a formation is its men together). `disorder`: ticks until
    // he has re-formed (after a charge, struck in the flank, in woods, mud or water): no formation counts meanwhile.
    uint32_t squad = 0;
    FormationKind formation = FormationKind::None;
    int32_t ranked = 0;
    int32_t disorder = 0;
    // Calls for help (world_help.cpp). `called`: the tick his squad last called (0: never). `helping`: the friend whose
    // call he is answering (his march to him is the answer's, not an order: any order ends it). `shotTick`, `flankTick`:
    // when a missile last struck him, and a blow in his flank or back (0: never).
    uint32_t called = 0;
    EntityId helping = kNoEntity;
    uint32_t helpSince = 0;
    bool helpFlank = false;  // the answer was commanded (Command::Answer): horse ride round to the foe's flank; of itself it goes straight to the friend
    // Posted: where an order's march ended is ground he keeps. He fights what comes within kPostLeash of it and helps a
    // friend whose foe is within it, goes back to it after, and does not leave it for a call. Any order ends it (Stop: he
    // is free again, and goes where the support stance takes him).
    bool posted = false;
    Vec3 postAt;
    // A rider who has charged home wheels away to re-form and come again: ticks of it left, and the way he rides.
    int32_t wheel = 0;
    Vec3 wheelDir;
    uint32_t shotTick = 0, flankTick = 0;
    // Woods (world_formation.cpp): the sides with a man near enough to see him among the trees, a bit each, as of the
    // last census (every side, for a man in the open); and the tick he last stood unseen by every foe (0: never).
    uint8_t seenBy = 0xFF;
    uint32_t hidTick = 0;
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
    bool afloat = false;       // dead: the water bears him, and a river carries him (world_drift.cpp)
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
    // Missiles left of a store (RangedWeapon::ammo; unused when it is zero), whether he keeps them for orders alone
    // (Command::HoldFire), and whether the attack in hand is one direct shot (Command::Detonate).
    int32_t ammo = 0;
    bool holdFire = false;
    bool detonate = false;
    // A dwarf or a wizard with a foe closing on him (World::shyAway): who. His friends near him come to it (supportFoe).
    EntityId menacedBy = kNoEntity;
    // A blast thrower holding his fire for a friend in the way (world_aoe.cpp): ticks held so far in this wait, and the
    // tick he last held. Both rest at zero once he fires (or is left alone for a while).
    int32_t aoeHeld = 0;
    uint32_t aoeHeldTick = 0;
    // The same for a grenadier's spark kept from a laid satchel with a friend by it (shootSatchel).
    int32_t sparkHeld = 0;
    uint32_t sparkHeldTick = 0;
    // His move or attack is a garrison's own (World::keepGates, World::stationMen: his station, or a foe come into the
    // walls), not an order of the player's. Any order given to him (World::startOrder) clears it. A man with a move, an
    // attack or a queue and this unset is "ordered", and is not one of a gate's keepers (world_keep.cpp).
    bool stationed = false;
    // Sent to occupy a place (Command::occupy): with nothing else in hand he goes back to it (World::occupyGround), a rout
    // and a rally later too. Any other order ends it.
    bool occupying = false;
    Vec3 occupyAt;
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
    // A boulder that has come down (world_boulder.cpp): it bounces and rolls on over the ground until it rests, and
    // `rolled` is how far it has gone on the ground (its turning, for the picture, and its furrow).
    bool landed = false;
    Fixed rolled;
};

// A boulder's own size and throw (world_boulder.cpp). Catapults throw at a fixed elevation (tan 4/3, 53 degrees): a
// high, plunging arc.
inline constexpr Fixed kStoneRadius = Fixed::fromRatio(11, 20);
inline constexpr Fixed kStoneElevation = Fixed::fromRatio(4, 3);

// What a boulder can strike that is not a man (World::strikeStructure).
enum class Structure : uint8_t { Gate, Wall, Tower, House, Rock, Tree };

// A boulder's predicted way over the ground (World::stonePath): where it comes down, then where it is and how fast at
// each tick after, to its rest.
struct StonePath {
    struct Point {
        Vec3 pos;
        Fixed speed;  // over the ground
    };
    Vec3 landing;
    std::vector<Point> roll;
};

struct Remain {
    RemainKind kind = RemainKind::None;
    PlayerId owner = 0;  // whose soldier a body part came from
    Vec3 pos;
    Vec3 dir;            // arrows: shaft direction (tip first); parts: lying direction
    uint32_t tick = 0;   // when it came to rest
    uint8_t source = kNoSource;  // as Projectile::source
    bool afloat = false;  // a part the water bears, carried by a river (world_drift.cpp)
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
    // A boulder (world_boulder.cpp; projectile = it, dir = its velocity): Bounce, it struck the ground and went on
    // (radius = how hard, m/s); Splash, it went into water and stopped; Struck, it hit a wall, tower, house, rock, tree
    // or gate (radius = the blow, World::strikeStructure's energy; gate = the Structure struck).
    // Splash: a flying part (or a loose arrow) went into the water here, at the surface (radius = how big a splash).
    // Doused: a Molotov or a satchel met water here and did nothing. Steam: a fireball burst on water, a third of itself (radius).
    enum class Kind : uint8_t { Hit, Ground, Blocked, Explosion, Heal, Bleed, Smash, Gate, Burst, Bounce, Splash, Struck, Doused, Steam };
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

// A hero fell this tick (for morale, the HUD, a conquest log). Like
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
    // Order: the lord's Rally, ordered (Command::Rally), which brings back every broken man in earshot.
    enum class Kind : uint8_t { Veteran, Hero, Order };
    EntityId caller = kNoEntity;
    PlayerId owner = 0;
    Vec3 pos;
    Kind kind = Kind::Veteran;
    int32_t heard = 0;    // friends whose morale it lifted
    int32_t rallied = 0;  // of them, broken men it brought back
};

// Position told (world_tactics.cpp): a blow that landed with the ground or the facing behind it, for the word floated over
// the squad it happened to. Derived from the simulation like RallyCall; not part of its state.
struct TacticEvent {
    // Flanked, Rear: struck in the flank or the back (a charge, or a blow on a man busy with another). Broken: a charge
    // there broke him on the spot. Braced: his pike stopped a charge head on. Ford: struck while wading. HighGround:
    // struck from above. Volley: shot from a height, or shot while wading.
    // Mire: struck while in mud or swamp. Uphill: he struck up a slope (slower, weaker). Cover: the trees (or his loose
    // order) spared him an arrow. Ambush: struck by men he had not seen among the trees. Disordered: his formation is
    // lost. Square: a blow at his flank or back met a square's front. Charge: a rider's charge struck him (`detail`:
    // 0 front, 1 flank, 2 rear), told of every charge whatever else it did. Downhill: that charge had the slope behind it.
    enum class Kind : uint8_t { Flanked, Rear, Broken, Braced, Ford, HighGround, Volley, Mire, Uphill, Cover, Ambush, Disordered, Square, Charge, Downhill };
    Kind kind = Kind::Flanked;
    EntityId unit = kNoEntity;  // the man it happened to (Braced: the pikeman who stood)
    PlayerId owner = 0;         // his side
    Vec3 pos;
    int32_t detail = 0;
};

// A squad in trouble called for help this tick (world_help.cpp), and who answers. Derived from the simulation like
// RallyCall, for the voices, the cards, the minimap and the event line. A side is told only of its own calls.
struct HelpCall {
    // Outnumbered in the melee; struck in the flank or back; wavering; under missiles it cannot answer; its lord has fallen.
    enum class Why : uint8_t { Outnumbered, Flanked, Wavering, Shot, HeroDown };
    EntityId caller = kNoEntity;  // a man of the squad that calls
    PlayerId owner = 0;
    uint32_t squad = 0;
    Vec3 pos;                     // the middle of the squad
    Why why = Why::Outnumbered;
    uint32_t answer = 0;              // the squad that answers (0: none is free)
    EntityId answerer = kNoEntity;    // a man of it
    Vec3 answerPos;                   // its middle
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
    // ... and also takes in its idle men standing outside the doors within 70 m (fled and rallied, or driven out). A human
    // holder's men are free to stand where they were put: his gate keeps only the men inside his walls.
    bool recalls = true;
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
    // A unit raised by the conquest: its rank (a hero's level less one) and
    // blessings, with a hero's extra hit points for the levels gained.
    void setVeterancy(EntityId id, int32_t rank, int32_t blessings);
    // The conquest's squads come into the world to fight and leave it after
    // (conquest_world.cpp): a soldier's wounds, kills and morale as he last
    // stood, and taking him out again (the bodies of the fallen stay until then).
    void restoreUnit(EntityId id, int32_t hp, int32_t kills, int32_t morale);
    // The orders waiting behind his own (Unit::queue), as a save keeps them: put back, at most kMaxQueuedOrders.
    void restoreQueue(EntityId id, std::vector<Command> queue);
    void testQueue(EntityId id, const Command& c) { if (Unit* u = mutableUnit(id)) u->queue.push_back(c); }  // (tests)
    // A blow dealt to a man at the start of the next step, as a weapon's would be (tests, and the desktop's `deaths` demo:
    // docs/PLAYTEST.md). No game calls it; the blows waiting are not state.
    void testBlow(EntityId id, int32_t amount, const Blow& blow) { testBlows_.push_back({id, amount, blow}); }
    // Taking a man out of the world leaves his place in the list, empty (Unit::removed), and the next man to come takes the
    // lowest empty place (so that a world with fights always going on does not grow for ever): a unit's place is his id less
    // one, `units()[id - 1]`, as in any battle. A place is given to a man afresh each time (generation): whatever a
    // presentation keeps by place is kept for the man who was there, and is forgotten when another comes.
    void removeUnit(EntityId id);
    uint32_t generation(EntityId id) const { return id >= 1 && id <= spawns_.size() ? spawns_[id - 1] : 0; }
    // Everything of a fight but the gates (their wounds, and the broken ones
    // open for good): no soldiers, missiles or remains, the dice and the clock
    // as they start. What a conquest's world is between fights, so that it is
    // the same on every machine whether or not it has seen one (conquest_world.cpp).
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
    // Position that told during the last step (world_tactics.cpp).
    const std::vector<TacticEvent>& tactics() const { return tactics_; }
    // Calls for help made during the last step (world_help.cpp).
    const std::vector<HelpCall>& helpCalls() const { return helpCalls_; }
    // A man's squad and its formation, as he is raised (the game's squads, a Conquest's armies).
    void setSquad(EntityId id, uint32_t squad, FormationKind formation);
    // Formations (world_formation.cpp). Whether he fights in his formation now: he has one, squad-mates stand by him, he
    // is not broken and not disordered.
    bool formed(const Unit& u) const;
    // The ground under him, as it tells in a fight: firm; a ford he wades; mud or swamp he is mired in; woods (that
    // break up horse and close ranks). A kind at home on the ground (gait.h) stands firm on it.
    enum class Footing : uint8_t { Firm, Ford, Mire, Woods };
    Footing footing(const Unit& u) const;
    // Among the trees (any kind of man): arrows are stopped, and he is hidden from afar.
    bool inWoods(const Unit& u) const;
    // Hidden from `side`: among the trees with no man of that side within kSpotReach of him.
    bool concealed(const Unit& u, PlayerId side) const;
    static constexpr Fixed kSpotReach = Fixed::fromInt(25);
    // A call lasts this long on a card and a map; a squad calls again no sooner than kCallAgainTicks.
    static constexpr uint32_t kCallShownTicks = 8 * kTicksPerSecond, kCallAgainTicks = 25 * kTicksPerSecond;
    // Whether a man is free to answer a call: no order, nobody to fight, not holding, not broken, a fighter, a bowman or a healer.
    bool freeToAnswer(const Unit& u) const;
    // What a formation does, for the manual and the HUD (percent; 0: nothing): to his blows, to shocks to his nerve, to
    // missiles landing on him, and the arrows it spares him.
    struct FormationRule {
        int32_t blowPct, asidePct, shockPct, flankShockPct, struckAsidePct, struckFrontPct, shotPct, sparePct;
        bool allRound, tight;
    };
    static const FormationRule& formationRule(FormationKind f);
    // Position (world_tactics.cpp). A man wading (in the shallows, on his feet, his kind not at home in water) strikes at
    // kWadeBlowPct less and is struck harder by a man on dry ground and by missiles.
    bool wading(const Unit& u) const;
    // What position adds to (or takes from) a blow of `attacker` on `target`, in percent: wading, and the high ground
    // (dz: the target's height over the attacker, over `dist`).
    int32_t positionPct(const Unit& attacker, const Unit& target, Fixed dist, Fixed dz) const;
    // A missile's share more for the height it was loosed from over its mark, in percent (100: none).
    static int32_t heightShotPct(Fixed above);
    // ... and for a mark who is wading.
    int32_t wadeShotPct(const Unit& mark) const;
    // Whether a man with no order stands idle: nothing in hand, nobody to fight or shoot, not broken (for the cards).
    static bool standsIdle(const Unit& u);
    // The Rally (Command::Rally). A broken man: the ticks until he rallies on his own as things stand (the lord within
    // kRallyLordReach halves it), or -1 while a foe is too near for him to rally at all; 0 for a man not broken.
    int32_t rallyTicks(const Unit& u) const;
    // A lord: whether he may call the Rally now, the ticks until he may, and how many broken men of his are in earshot.
    bool canRally(const Unit& lord) const;
    int32_t rallyHeard(const Unit& lord) const;
    static constexpr int32_t kRallyCooldownTicks = 60 * kTicksPerSecond;
    static constexpr Fixed kRallyLordReach = Fixed::fromInt(20);
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
    // The water (world_drift.cpp): the metres of it over the bed at a point (none: 0), the current there (m/s, along
    // the nearest river's line; nothing in still water and on dry land), and how deep a body of these stats rides:
    // in less it grounds. Read by the step and by the presentation alike (the cache behind flowAt is a function of
    // the ground alone).
    Fixed waterDepth(Fixed x, Fixed y) const;
    Vec3 flowAt(Fixed x, Fixed y) const;
    static Fixed draughtOf(const UnitStats& s);
    // Whether a stone bridge's pier (or its cutwater) stands in the water at a point: what floats lodges against it.
    bool pierAt(Fixed x, Fixed y) const;
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
    // A charge laid beforehand (Command::force on a LaySatchel): how far from where he stands, and how clear of any foe.
    static constexpr Fixed kPrepareReach = Fixed::fromInt(160);
    static constexpr Fixed kPrepareClear = Fixed::fromInt(50);
    bool canPrepare(const Unit& u, const Vec3& at) const;
    // Command::Detonate takes the laid satchel within this of the spot shown.
    static constexpr Fixed kDetonatePick = Fixed::fromInt(4);
    // Whether his missile's fire sets off a satchel (a Molotov, a fireball): who may be ordered to Detonate.
    static bool sparks(const UnitStats& s) {
        return s.ranged.muzzleSpeed > Fixed::zero() && (s.ranged.kind == ProjectileKind::Molotov || s.ranged.kind == ProjectileKind::Fireball);
    }
    // The few who throw a blast and cannot fight: a grenadier, a wizard (not a dragon, who can). Kept out of the melee
    // (World::shyAway), guarded by the men near them (supportFoe), and the first mark of a foe's bowmen (autoTarget).
    static bool precious(const UnitStats& s) { return s.satchels.count > 0 || (sparks(s) && s.flyAltitude == Fixed::zero()); }
    // Whether he has a missile to loose (a store spent leaves him his hands).
    static bool armed(const Unit& u) { return u.stats.ranged.muzzleSpeed > Fixed::zero() && (u.stats.ranged.ammo == 0 || u.ammo > 0); }
    // Water puts out oil and powder: nothing burns or is laid where it stands over the ground a hand deep or more (the
    // water that is drawn, World::waterDepth: a bank beside the shallows is dry, though its ground is of the ford's kind).
    static constexpr Fixed kDouseDepth = Fixed::fromRatio(1, 10);
    bool douses(const Vec3& at) const { return waterDepth(at.x, at.y) >= kDouseDepth; }
    // What a man carries, as the conquest kept it between fights (a negative count: his full load).
    void restoreLoad(EntityId id, int32_t satchels, int32_t ammo);
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
    void setGateGarrisoned(size_t gate, bool on, bool recallsOutside = true);
    void setGatePin(size_t gate, Command::GatePin pin);
    // The one rule for whose gate it is and who keeps it, in a Conquest town and a Skirmish castle alike: every gate is kept by
    // its holder's garrison (keepGates); a human holder's keepers are his idle men inside his walls only, and his pin overrules
    // them; a computer's or a neutral town's also take in idle men outside the doors.
    void keepGate(size_t gate, PlayerId holder, bool humanHolder) {
        setGateHolder(gate, holder);
        setGateGarrisoned(gate, true, !humanHolder);
    }
    // Whether this man is one of a garrisoned gate's keepers (inside its walls, near it, on its side): the gate's strategy
    // gives him his station, so no other thinking (the computer lords' conquest AI) orders him about.
    bool gateKeeper(const Unit& u) const;
    // A garrison standing in a walled town is stationed as one (world_keep.cpp): bowmen on the wall-walk, one to an embrasure,
    // spread round the ring; foot in the keepers' horseshoe behind the doors; casters, healers, engines and the lord behind
    // the foot. This is where men are placed, not an order they keep: they are ordinary men, and the player's hand is free.
    enum class GarrisonRole : uint8_t { Wall, Foot, Rear };
    static GarrisonRole garrisonRole(const UnitStats& stats);
    struct GarrisonPost {
        Vec3 pos, face;
        GarrisonRole role = GarrisonRole::Foot;  // Wall: an embrasure on the walk; Foot: a place in the courtyard (the men beyond the embrasures too)
    };
    static constexpr size_t kNoGate = static_cast<size_t>(-1);
    // The gate of the walled town whose heart is `heart` (region coordinates): of the rings round it, the outer one's. kNoGate if none.
    size_t gateOfTown(const Vec3& heart) const;
    // The stations of `men` (one post each, in their order) for the garrison of this gate's ring; places already held by men in
    // the world are left. Empty if the gate is not one. A ring of two doors has its bowmen and foot shared between them (the
    // lord and the rear behind this gate's), so that neither door is crowded or bare.
    // (`except`: men in the world who are among them, whose own places do not count as held.)
    std::vector<GarrisonPost> garrisonPosts(size_t gate, const std::vector<const UnitStats*>& men, const std::vector<EntityId>& except = {}) const;
    // Sends these men (idle or not: the caller says who) to their stations in this gate's ring, each to the nearest of its role.
    void stationMen(size_t gate, const std::vector<EntityId>& ids);
    Fixed groundHeight(Fixed x, Fixed y) const;
    // The least elevation (its tangent) of an arc from `origin` down onto
    // `mark` that passes over everything on the way, up to `shortOf` from it:
    // the ground, rocks, trees, houses, walls and standing gates, but not the
    // trees and bushes within 6 m of the mark (the wood that shelters him).
    // Very low when nothing rises into the way.
    Fixed clearingTan(const Vec3& origin, const Vec3& mark, Fixed shortOf) const;
    const Region* region() const { return config_.region.get(); }
    // Whose land each part of the field is (the conquest's territory, which changes as the war goes; a set battle
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
    // A beaten host quits the field (RoutRule, mode_rules.h): breakSide, and every man of it runs, deaf to the enemy,
    // for the edge of the ground away from the foes' middle.
    void quitField(PlayerId owner);
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
    // What bursts on impact (fireballs, stones, Molotovs) rather than bouncing or sticking. (A stone then rolls on.)
    static bool burstsOnImpact(ProjectileKind k) {
        return k == ProjectileKind::Fireball || k == ProjectileKind::Stone || k == ProjectileKind::Molotov;
    }
    // The boulder (world_boulder.cpp). A stone thrown by `engine` at `mark`: where it would come down and roll, over
    // the ground as it is (slopes, soft ground, water, trees and stone in its way), the men not reckoned. The same
    // steps the thrown stone takes; for the aim (World::fireAt) and for showing the player before he orders it.
    StonePath stonePath(const Unit& engine, const Vec3& mark) const;
    // A boulder let go at `pos` with `vel` (in tests): as a catapult's in the air. Its id. And one thrown as `engine`
    // would throw it at `mark`, with no scatter (in tests, and the boulder demo of the --play scripts).
    EntityId launchStone(PlayerId owner, const Vec3& pos, const Vec3& vel);
    EntityId throwStone(const Unit& engine, const Vec3& mark);
    // THE HOOK FOR CASTLE DAMAGE (docs/GAME_DESIGN.md, "The boulder"). Every blow of a boulder on something built or
    // grown comes through here: `kind` what it struck, `where`, `dir` the stone's travel, `energy` the blow in the
    // stone's own damage (100 is a catapult's stone straight from the air; one that rolled into it has less, by its
    // speed), `rolled` whether it rolled into it. Today it tells the presentation (ImpactEvent::Kind::Struck) and, for
    // a gate a stone ROLLED into, takes the doors' hit points (a stone from the air does that through its smash,
    // World::blastGates). Walls, towers and houses have no hit points yet: the work that gives them some takes them here.
    void strikeStructure(Structure kind, const Vec3& where, const Vec3& dir, Fixed energy, bool rolled);
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
    // finds exactly what a loop over every soldier would. (On a conquest's
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
    // Whether a flier bound for this place (or this man) takes wing: a long way, or up onto a wall-walk or down off one.
    bool takesWing(const Unit& unit, const Vec3& goal) const;
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
    bool openStraight(const Vec3& a, const Vec3& b, Gait gait) const;
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
    // `placed`: a missile ordered at a place (a Detonate order's, at a charge): thrown with the wind allowed for, so that
    // it comes down where it was sent; Lobbed, by the high arc as well, over the men standing before the mark (the flat
    // throw bursts on the first man in its way).
    enum class Placed : uint8_t { No, Flat, Lobbed };
    // Whether his missile can be lobbed from `origin` onto `target`: in the high arc's reach, clear of walls and trunks
    // all the way, and clear of the boughs over his own head (which a missile strikes as it does a trunk).
    bool canLob(const Unit& u, const Vec3& origin, const Vec3& target, Vec3* velocity = nullptr) const;
    bool fire(Unit& unit, const Vec3& target, bool atWalk = false, Placed placed = Placed::No);
    // `fire`, and the reload that follows, for any shooter (world_aoe.cpp). One with a blast to throw (stones, fireballs,
    // Molotovs) chooses where it falls so as to spare his own side, or holds his fire a while (true, loosing nothing,
    // when held); a bowman's flat shot is as ever: whoever stands in the line takes it. False: no clear shot.
    bool fireAt(Unit& unit, const Vec3& target, bool atWalk = false);
    // Where a missile aimed at `aim` from `origin` bursts: a flat shot (fireball, Molotov) on the first man in its way.
    Vec3 burstPoint(const Unit& shooter, const Vec3& origin, const Vec3& aim) const;
    // A foe within a few metres of him: he is not to wait on his aim.
    bool overrun(const Unit& u) const;
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
    // The garrison of one ring's doors (all of them, the first of the ring's gates thinking for the rest): shared among the
    // doors by where the foes are, each door's keepers in their stations.
    void keepDoors(const std::vector<size_t>& doors);
    // The gates (in the world's order) of the ring this gate opens: a castle may have more than one door.
    std::vector<size_t> doorsOfRing(size_t gate) const;
    // garrisonPosts for the one door.
    std::vector<GarrisonPost> garrisonPostsAt(size_t gate, const std::vector<const UnitStats*>& men, const std::vector<EntityId>& except, const std::vector<Vec3>& alsoHeld) const;
    // Ranks across the courtyard behind a gate for each of the groups of men (counts) in turn, each group in ranks of its own,
    // every place open ground, centred on the doors' axis and clear of `avoid`: the places of each group, left to right.
    std::vector<std::vector<Vec3>> yardRanks(const Gate& g, const Rampart& ring, const std::vector<int32_t>& groups, Fixed gap, const std::vector<Vec3>& avoid) const;
    // Open courtyard ground inside the ring: not a ramp's foot or a stair, nor under the wall-walk's gallery.
    bool courtyardSpot(const Rampart& ring, const Vec3& p) const;
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
    // An engine's stand-off (world_gates.cpp): the nearest standing gate of another's town whose doors lie within `grab` of
    // `near` and within the engine's own throw of where it stands, or -1.
    int engineGateInReach(const Unit& u, const Vec3& near, Fixed grab) const;
    // Sets the engine to batter that gate from where it is.
    void batterGate(Unit& u, int gate);
    // A siege played as one (world_siege.cpp). Derived from the men's own state at the start of each step, never saved:
    // the engines at work on a standing gate (their owner, the gate, their middle, and the way from them to it) ...
    struct Battery {
        PlayerId owner;
        int32_t gate;
        Vec3 centre, toward;
    };
    // ... and, at each breach not yet old, whether a side's wave is gathered (go) or men are still coming.
    struct Rally {
        int32_t gate;
        PlayerId owner;
        bool go;
    };
    std::vector<Battery> batteries_;
    std::vector<Rally> rallies_;
    void updateSiege();
    // Held at a breach: he is bound through it and waits outside for the rest of his side to come up.
    bool rallyHold(const Unit& u) const;
    // Escort: a foe at a sortie against engines, for the man (idle, near them) to meet; and his place on the line in front of
    // the engines, walked to or held (false: no engine of his side near enough).
    Unit* escortFoe(const Unit& u);
    bool escortSpot(Unit& u);
    // A man sent to occupy ground (Unit::occupying) who stands idle away from it walks back to it.
    bool occupyGround(Unit& u);
    // Suppression: the garrison's man on a wall-walk within reach, for a shooter outside the walls to answer first; and the
    // man a shooter takes on his own (that, unless a foe is on him, else the nearest).
    Unit* wallShooter(const Unit& u, Fixed reach);
    // One of his own side's (a grounded one) within `radius` of the place.
    bool friendNear(const Unit& u, const Vec3& at, Fixed radius) const;
    // An engine with no order and no mark in reach, the garrison's men on the wall a little beyond it: it walks up to its own reach.
    bool creepToWall(Unit& u);
    Unit* autoTarget(const Unit& u);
    // How far a man looses on his own: an engine at four fifths of its throw (a stone at the whole of it scatters wide of one man), the rest at autoRange.
    Fixed autoReach(const Unit& u) const;
    // He looses at autoTarget if one is in his reach (true: there is one, whether or not the bow is ready).
    bool shootNearby(Unit& u);
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
    void laySatchel(Unit& u, const Vec3& toward, Fixed reach = Fixed::zero());
    // Command::Detonate begun: the mark is the laid satchel nearest the spot shown, or the spot.
    void startDetonate(Unit& u, const Vec3& at);
    // ... and carried out, a tick of it: into reach, then the one shot, loosed direct (true: the order is done).
    bool detonateStep(Unit& u);
    // A man with a store of missiles and no order asks whether this mark is worth one: a cluster in the blast
    // (kThriftCluster men, a great beast counting for them all), or a foe on top of him; never a man running away,
    // and a grenadier's last bottle is kept for his satchels.
    bool worthAShot(const Unit& u, const Unit& target) const;
    // A dwarf or a wizard with no order and a foe within kShyReach of him: back behind the nearest of his side's
    // fighting men, or straight away from the foe (true: he is walking). Not a man told to hold; one caught at arm's
    // length fights as he can.
    bool shyAway(Unit& u);
    // A satchel's blast shakes those it leaves standing near it (twice its reach), of every side.
    void blastShock(const Vec3& at, const Explosive& ex);
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
    // How many men may strike one foe at once, and whether that many already do (a foe ringed, so the rest must wait or go round).
    int32_t meleeCap(const Unit& unit, const Unit& foe) const;
    bool meleeFull(const Unit& unit, const Unit& foe) const;
    // A free place on the ring at which `unit` strikes `foe`, the nearest to him and away from his friends; false when all are held.
    bool ringSlot(const Unit& unit, const Unit& foe, Vec3& slot) const;
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
    // What a man gains at a census once calm (`friends` near him, a hero within his lead, a lord close by a broken man).
    int32_t recoveryGain(const Unit& u, int32_t friends, bool heroLeads, bool lordClose) const;
    // The lord's Rally (Command::Rally).
    void orderRally(Unit& lord);
    // Support (world_tactics.cpp): the foe of a friend fighting near him that an idle man goes to meet (null: none, or he
    // holds), and how he goes: foot straight in, horse round to the flank.
    Unit* supportFoe(Unit& u);
    // Holding, he meets only the foe who has come within a few strides of him.
    bool withinHold(const Unit& u, const Unit& foe) const;
    void support(Unit& u, Unit& foe);
    // A charge's shock, by where it lands, to the man struck and (less) the friends at his shoulder.
    void chargeShock(Unit& enemy, HitSide side, bool busy);
    void tell(TacticEvent::Kind kind, const Unit& u, int32_t detail = 0) { tactics_.push_back({kind, u.id, u.owner, u.pos, detail}); }
    // Formations (world_formation.cpp): what his formation adds to his blow where it lands (percent), to a blow or a
    // missile landing on him, and to a shock to his nerve; whether it has no flank (a square); whether it turns slowly.
    int32_t formBlowPct(const Unit& u, HitSide side) const;
    int32_t formStruckPct(const Unit& target, HitSide side, bool missile) const;
    int32_t formShockPct(const Unit& u) const;
    int32_t formFlankShockPct(const Unit& u) const;
    bool allRound(const Unit& u) const;
    bool turnsSlowly(const Unit& u) const;
    // An arrow meant for him that never lands: among trees, or in loose order with room to step aside (told: Cover).
    bool arrowSpared(const Unit& u);
    // His formation is lost for `ticks` (told once, when it goes).
    void disorder(Unit& u, int32_t ticks);
    // The census of the ranks and the woods, with the morale census: squad-mates near, who sees whom among the trees.
    void updateRanks();
    // The first blow out of hiding: what it does to the nerve of the man struck.
    void ambush(Unit& striker, Unit& struck);
    // Calls for help and their answers (world_help.cpp).
    void callForHelp();
    // Sets a man on his way to a friend who has called (the sim's own answer, and Command::Answer).
    void answerCall(Unit& u, const Unit& caller, bool flank = false);
    static constexpr Fixed kPostLeash = Fixed::fromInt(20);
    bool withinPost(const Unit& u, const Unit& foe) const { return (foe.pos - u.postAt).flat().lengthSq() <= kPostLeash * kPostLeash; }
    // A posted man with nothing to fight who is off his post walks back to it (true: he walks).
    bool returnToPost(Unit& u);
    // A man answering: true while he is on his way (his march), false once he is there (support takes him in) or it is over.
    bool answering(Unit& u);
    // Pikes with no order stand to receive horse coming at the gallop: turned to it, braced (true: he stands).
    bool braceFor(Unit& u, const Unit& foe);
    // A healer with no order goes to a wounded friend near him who is out of his reach (true: he walks).
    bool seekWounded(Unit& u);
    // What a man looses from where he stands, in percent of his shot (a man in the water shoots worse).
    int32_t loosePct(const Unit& shooter) const;
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
    // Earliest solid obstacle crossed by segment a->b, as a fraction in [0, 1); -1 if none.
    // `stone` (if given) tells whether it was stone (walls, towers, houses,
    // rocks), off which arrows glance, or wood (trees, gates), which they stick in.
    // `what` (if given): which obstacle of the region it was, or kHitWall, kHitGate.
    static constexpr int32_t kHitWall = -1, kHitGate = -2;
    Fixed obstacleHit(const Vec3& a, const Vec3& b, Fixed limitT, bool masonryOnly = false, bool* stone = nullptr, int32_t* what = nullptr) const;
    // The boulder (world_boulder.cpp). How it came down: on the ground, on a man, or against something standing.
    enum class StoneFall : uint8_t { Ground, Man, Obstacle };
    // A stone in the air struck at `at` (its smash is the caller's): it goes on as a thing on the ground. False if it is
    // gone (shattered on a wall-walk, sunk).
    bool landStone(Projectile& p, const Vec3& at, StoneFall how, int32_t what);
    // One tick of a stone on the ground: its way, and the men under it. True when it is gone.
    bool rollStone(Projectile& p);
    // One tick of its way alone (no man, no chance): what it met, for rollStone to tell and stonePath to follow.
    struct StoneStep {
        enum class Met : uint8_t { Nothing, Bounce, Water, Obstacle, Rest } met = Met::Nothing;
        Vec3 at;
        Fixed blow;        // Bounce: its speed into the ground; Obstacle: its speed into the thing
        int32_t what = 0;  // Obstacle: as obstacleHit's
    };
    StoneStep stepStone(Projectile& p) const;
    // The ground's answer to a stone coming down on it at p.pos (stepStone's, and the first landing's).
    StoneStep groundStone(Projectile& p) const;
    // The land's rise each way under a stone on `floor` there (x, y; nothing on a deck, which is level).
    Vec3 stoneSlope(Fixed x, Fixed y, Fixed floor) const;
    // What a step met, told and done: dust, a splash, the blow on a structure. True when the stone is gone.
    bool tellStone(Projectile& p, const StoneStep& step);
    Structure structureOf(int32_t what) const;
    // The velocity `engine` gives a stone to come down at `at`, and where it leaves the arm (`origin`), as World::fire has it.
    Vec3 stoneLaunch(const Unit& engine, const Vec3& at, Vec3& origin) const;
    // What a stone thrown by `u` at `mark` would do as it rolls on from there, to the foe and to his own side, as
    // World::fireAt's tally of a blast reckons a wound (one stone's worth; `spread` is how wide of the mark it may fall).
    struct RollToll {
        Fixed foe, friends;
        bool badlyHurtFriend = false;
    };
    RollToll rollToll(const Unit& u, const Vec3& mark, Fixed spread) const;
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
    // An order's place on a wall-walk, drawn in from the parapet to where a body of this radius can stand (keepOnWalk holds
    // him off the stone: an embrasure's spot is for a bowman, and a bigger man sent to it would press at the merlons with
    // his order never done).
    Vec3 insideParapet(const Vec3& p, Fixed radius) const;
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
    // The dead in the water (world_drift.cpp). A body come to rest (killed, or landed after a blast) is borne up if
    // the water is deep enough for it; a part laid in a slot likewise (false: it lies on the bed); what never floats
    // (an arrow, a blade, a bone); a flying thing that has just gone under (it tells of the splash); and the step
    // that carries everything afloat.
    void floatBody(Unit& u);
    bool floatRemain(uint32_t slot);
    static bool sinksInWater(RemainKind kind);
    bool plunge(const Projectile& p, const Vec3& before);
    void driftAfloat();
    bool carry(Vec3& pos, Fixed draught, bool heavy, uint32_t age, uint32_t who) const;
    bool drift(Vec3& pos, Fixed draught, bool heavy, uint32_t age, uint32_t who) const;
    void applyZone();
    Fixed slopeAt(Fixed x, Fixed y) const;
    bool steeperThan(Fixed x, Fixed y, Fixed limit) const;  // slopeAt(x, y) > limit, without the root
    Fixed slopeSquared(Fixed x, Fixed y) const;
    Ground groundAt(Fixed x, Fixed y) const;
    bool isWater(Fixed x, Fixed y) const;

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
    std::vector<TacticEvent> tactics_;
    std::vector<HelpCall> helpCalls_;
    std::array<uint32_t, 8> firstCall_ = {};  // by side: the tick of its first call for help in this fight (0: none yet), state
    std::vector<uint8_t> wet_;  // updateMorale's scratch: who is wading, by place, as of this census (derived, not state)
    std::vector<OrderDecision> decisions_;
    std::vector<DeathEvent> deathEvents_;
    // Soldiers killed this step (owner, where), for their friends' morale.
    std::vector<std::pair<PlayerId, Vec3>> deaths_;
    std::vector<Remain> remains_;
    std::vector<GateState> gates_;
    std::vector<PlayerId> brokenSides_;  // breakSide, ascending
    uint64_t remainsAdded_ = 0;
    std::vector<uint32_t> changedRemains_;
    // What the water is carrying, in the order it took them: a body (its id) or a part (its slot).
    struct Floater {
        bool unit;
        uint32_t ref;
    };
    std::vector<Floater> afloat_;
    mutable std::unordered_map<uint64_t, Vec3> flowCache_;  // flowAt, a square at a time: looked up, never walked
    struct PendingExplosion {
        Vec3 at;
        Explosive ex;
        PlayerId owner;
        ProjectileKind kind;
        DamageType cause;
    };
    std::vector<PendingExplosion> pendingExplosions_;
    struct TestBlow {
        EntityId id;
        int32_t amount;
        Blow blow;
    };
    std::vector<TestBlow> testBlows_;  // (testBlow)
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
