#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "illyria/game/battle_capture.h"
#include "illyria/game/camera.h"
#include "illyria/game/gmath.h"
#include "illyria/game/path_lines.h"
#include "illyria/game/sfx.h"
#include "illyria/game/voice.h"
#include "illyria/sim/conquest.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/mode_rules.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::game {

class BattleMode;  // mode.h

// How a squad arranges itself, as in Myth: each soldier keeps his place as
// the squad marches, and the whole squad moves at its slowest member's pace
// (slower still in the tight formations: see formationPace).
//   Line        one rank, everyone abreast (turned, it is a column)
//   DoubleLine  two ranks: the shield wall
//   Box         a hollow square, a man at each corner, every side facing
//               out: nothing takes it in the flank
//   Wedge       one at the point, two behind, three behind them
//   Loose       a wide, ragged skirmish line: missile troops, out of reach
//               of one stray blast
//   Encircle    a wide crescent around the point, everyone facing outward
//   Vee         a V, the point at the back and the arms reaching forward:
//               whatever comes at the point is taken in both flanks
enum class Formation : uint8_t { Line, DoubleLine, Box, Wedge, Loose, Encircle, Vee };
constexpr int kFormationCount = 7;
// The picker's order, which is also the desktop keys 1-7 (0 is Scatter).
constexpr Formation kFormationOrder[kFormationCount] = {Formation::Line,  Formation::DoubleLine, Formation::Box,  Formation::Wedge,
                                                        Formation::Loose, Formation::Encircle,   Formation::Vee};
const char* formationName(Formation f);
// The world's name for it (sim::FormationKind has one more, None, first).
constexpr sim::FormationKind simFormation(Formation f) { return static_cast<sim::FormationKind>(static_cast<int>(f) + 1); }
// The picker's tenth entry, after the nine: the selection as it stands
// (Battle::keepShape), its own arrangement kept as it moves.
constexpr int kKeepShapeId = kFormationCount;
constexpr const char* kKeepShapeName = "";
// A place in a formation, relative to its centre (x to the right, y forward),
// and the way to face there (forward is {0, 1}).
struct FormationSlot {
    V3 pos;
    V3 face{0, 1, 0};
};
// `spacing` is the gap between neighbours, from the biggest soldier's size
// (formationSpacing); the loose formations open it wider.
std::vector<FormationSlot> formationLayout(Formation f, int count, float spacing);
std::vector<V3> formationSlots(Formation f, int count, float spacing);
// Marching pace in this formation, as a share of the slowest member's: the
// tight ones (Box, the lines) a little slower than the loose ones.
float formationPace(Formation f);
// The gap a formation of this soldier keeps between neighbours.
float formationSpacing(sim::UnitType type);
// What a squad of this type forms by default (the computer's choice too):
// lines for foot, a checkerboard for missile troops, a wedge for horse.
Formation defaultFormation(sim::UnitType type);

// What a squad's banner shows, as in Warlords: one strength from 1 to 9,
// the soldiers' rank (chevrons), a hero's level and the blessings carried.
struct Standing {
    int strength = 1;
    int rank = 0;       // soldiers: 0 recruit .. 3 elite (the survivors' average)
    int level = 0;      // heroes: 1..9; 0 for other squads
    int blessings = 0;
    int kills = 0;
    int broken = 0;     // soldiers routed (fleeing, deaf to orders)
    int battles = 0;    // the fights it has been in before this one (a Conquest's squad, a march's): its history
};

struct Squad {
    std::string name;
    sim::PlayerId owner;
    sim::UnitType type;
    std::vector<sim::EntityId> members;
    Formation formation = Formation::DoubleLine;
    mutable V3 facing{1, 0, 0};  // the way it last marched (presentation of orders, not sim state)
    // A garrison holds its town and never marches out: its gate's keepers (World::keepGates) give it its stations
    // (archers on the walls, foot behind the doors), and it meets whoever comes in.
    bool garrison = false;
    // Where it was last sent (the computer re-plans a chase only when that moves enough).
    mutable V3 goal{};
    mutable bool hasGoal = false;
    // In Conquest (battle_conquest.cpp) a squad is an army of a stack: its men are `members`, and what its card shows
    // that no unit holds is told by the army (`told`, `arms`, `rare`, `command`, `hired`).
    uint32_t army = 0, stack = 0;
    int total = 0;  // the squad's full size (0: its members)
    bool told = false;
    Standing standing{};
    std::string arms{};  // a hero's artifacts
    bool rare = false, hired = false;
    int command = 0;
};

// Which battle to fight.
enum class Scenario {
    Ford,      // Oakhaven Ford: the Red Hand comes over the river
    Siege,     // a neutral town near Oakhaven, held by its own small garrison
    Capital,   // the nearest capital: a castle of two doors and an inner bailey
    Field,     // open ground (the first hill the continent offers): two armies and nothing between them
    Mode,      // a match: Horde or capture the flag (mode.h), on a field it fills itself
};

// A squad bought: `squads` squads of `type`, as many men each as the Conquest raises (Conquest::squadSize) at its price.
struct Buy {
    sim::UnitType type;
    int squads;
};

// A squad carried from an earlier battle (the March, march.h): its name, kind and full strength, and every man left of it
// as he stands. `hp` is thousandths of his full health; `slot` his place among the squad's men when the battle began
// (Battle::carried tells it, so that whoever keeps the roster knows which man this is).
struct CarriedMan {
    int hp = 1000, rank = 0, kills = 0, slot = 0;
};
struct CarriedSquad {
    std::string name;
    sim::UnitType type = sim::UnitType::LightInfantry;
    int full = 0;
    std::vector<CarriedMan> men;
};

// A skirmish as the Skirmish screen sets it up: a Conquest with a fence, so its army is bought at the Conquest's prices from a
// gold budget and the computer plays as a lord does (AiSkill). `budget` 0 is the classic muster of the Ford and the village
// siege (the armies the first builds were made with); every other map, and any budget, buys.
struct SkirmishSetup {
    // The computer plays Easy: a smaller purse (kEasyPursePct of the player's), and it comes late and piecemeal (Battle::runEnemyAi, aiFight).
    bool easy = false;
    // The computer plays Hard: a larger purse in the open (Battle::kOpenHardPursePct), a shorter wait before it goes in, and it answers its own calls for help.
    bool hard = false;
    int budget = 0;
    std::vector<Buy> army;  // what the player bought (empty: bought for him from the budget)
    bool defend = false;    // a siege: hold the castle against the other side's army instead of storming it
    // The March: the player's army is these squads as they stand (its lord among them), not one bought; and the other
    // side raises this much gold (0: as much as the player's purse).
    std::vector<CarriedSquad> roster;
    int foePurse = 0;
};
// The types a skirmish may buy: what a Conquest city trains (Conquest::trainable).
std::vector<sim::UnitType> skirmishTypes();
// Gold for an army at the Conquest's prices.
int armyCost(const std::vector<Buy>& army);
// An army bought for `budget` gold: a mix of foot, bowmen, horse and (with `engines`) catapults, as a lord raises one.
inline constexpr int kFieldEngines = 8, kSiegeEngines = 4;
std::vector<Buy> buyArmy(int budget, bool engines, int every = kFieldEngines, int most = 99);

// A battle fought from the conquest: the armies of each side, as they stand.
const sim::Continent::City& siegeTown(const sim::Continent& c);
// The capital nearest Oakhaven Ford, for a siege of two doors.
const sim::Continent::City& capitalTown(const sim::Continent& c);

// What a squad does on the battlefield, for orders, the AI and HUD icons.
// Scouts (the hyena-folk) are let loose to raid (sim Command::Raid,
// world_scavenge.cpp): the computer brings its packs near the foe and lets
// them loose (Battle::aiRaid); the player does with T or the Raid button.
enum class Role { Melee, Ranged, Bomber, Caster, Healer, Flier, Siege, Scout };
Role roleOf(sim::UnitType type);

// A big army ordered into a rank formation (Line, Double line, Loose) or kept
// as it stands when that is one long rank forms a block instead, as in Myth:
// the front is capped by blockFrontage (men a rank, scaling with the square
// root of the army), the rest in ranks behind. By post: foot in front, the
// horse on the flanks of the front, heroes in the middle behind the foot,
// then missile troops and casters, and the engines last.
enum class Post { Foot, Cavalry, Hero, Rear, Siege };
Post postOf(sim::UnitType type);
// The most men abreast in the block of `count` men.
int blockFrontage(int count);
// Where `types[i]` stands in the block (x right, y forward, centred), as in
// formationLayout; the places of one post are interchangeable, so which man
// takes which is for the caller (Battle::planBlock) to decide.
// `front`: men abreast of the whole front, the horse on the wings included (0: blockFrontage); kept between 8 and the army.
std::vector<FormationSlot> blockLayout(const std::vector<sim::UnitType>& types, int front = 0);
// The men abreast a facing drag of `metres` makes (0 when too short to mean a width: the usual block).
int dragFrontage(float metres);

// Decals derived only from simulation events, so every peer accumulates the
// same ones (docs/GAME_DESIGN.md §5.5).
struct GroundMark {
    // Furrow: a stretch of the track a rolling boulder scuffs through the grass (yaw: the way it went).
    enum class Kind : uint8_t { Blood, Crater, Dust, Furrow };
    Kind kind;
    V3 pos;
    float yaw;
    float size;
    uint32_t tick;
    uint32_t seed;
};

// What the water shows (battle_water.cpp), read by its shader: a ring spreading on the surface from where something
// went in or waded (`born` in game seconds, `size` 0.2 a foot or an arrow .. 2.6 a catapult's stone), and a cloud of
// blood in it, carried and thinned by the current. Cosmetic, like the particles.
struct WaterRing {
    V3 pos;
    float born;
    float size;
};
struct WaterDye {
    V3 pos;
    float amount;  // 1: a man's death
    float radius;  // metres, growing
    float age;     // seconds
    float strength() const;  // what is left of it
    static float life();     // seconds until it is gone
};

// Purely cosmetic, per client: fire, smoke, sparks, dirt, healing light, the water's spray, steam.
struct Particle {
    enum class Kind : uint8_t { Fire, Smoke, Spark, Dirt, Heal, Flash, Spray, Steam };
    Kind kind;
    V3 pos;
    V3 vel;
    float age;
    float life;
    float size;
};

// A grid of continent samples in battle-local coordinates, for drawing beyond
// the battlefield and for the camera: the whole continent every 16 m (as fine
// as its baked relief, since the map shrank to a third), and a ring every 16 m
// around the battlefield.
struct FarGrid {
    static constexpr float kCell = 16.0f;
    float cell = kCell;
    int nx = 0, ny = 0;
    V3 min;                       // local coordinates of sample (0, 0)
    std::vector<float> height;    // ground, or the water surface where wet
    std::vector<uint8_t> ground;  // sim::Ground
    std::vector<float> depth;     // water depth where wet, else 0
    float heightAt(float x, float y) const;
    bool contains(float x, float y) const {
        return x >= min.x && y >= min.y && x <= min.x + cell * static_cast<float>(nx - 1) && y <= min.y + cell * static_cast<float>(ny - 1);
    }
    // Samples the continent over [min, min + cell * (n - 1)].
    void fill(const sim::Continent& c, const V3& origin, V3 localMin, int nxIn, int nyIn, float cellIn);
    // The same a few rows at a time: reset, then fill rows [j0, j1).
    void reset(V3 localMin, int nxIn, int nyIn, float cellIn);
    void fillRows(const sim::Continent& c, const V3& origin, int j0, int j1);
};

// One playable battle: owns the deterministic world, turns player input into
// commands, drives the enemy, and keeps presentation state (interpolation,
// ground marks, particles) for the renderer.
class Battle {
public:
    static constexpr sim::PlayerId kPlayer = 0;
    static constexpr sim::PlayerId kEnemy = 1;
    static constexpr uint64_t kRegionSeed = 20261027;
    static constexpr int32_t kTerrainCells = 256;
    static constexpr float kCellSize = 4.0f;
    static constexpr float kWorldSize = kTerrainCells * kCellSize;
    static constexpr uint32_t kMaxRemains = 30000;
    static constexpr size_t kMaxGroundMarks = 30000;
    // Conquest: beyond this from the men (metres) an order is not the world's (no fight is there to be ordered), it is a
    // march of their squads (battle_conquest.cpp).
    static constexpr float kLocalReachM = 200.0f;
    // A siege's besiegers set their engines up this far before a door (inside their 150 m throw less its margin, and the
    // 140 m at which they begin on a gate unbidden), and a camp before a side wall this far from it (the 120 m at which they
    // shell the walks).
    static constexpr float kBatteryM = 132.0f, kFlankM = 112.0f;
    // The besiegers break off when this share (percent) of their fighting men, or fewer, still stand unbroken.
    static constexpr int kSpentPct = 30;
    // A siege's besiegers buy with this share (percent) of the keepers' purse.
    static constexpr int kSiegeOddsPct = 150;
    static constexpr float kWallReachM = 160.0f;  // an engine left to itself comes up to the walks' men from this far (World::creepToWall)
    static constexpr float kSiegeScope = 250.0f;  // the engines of a computer side take the gates this near them (aimEngines): one siege at a time
    static constexpr float kGatherMetres = 60.0f;  // an attack on a foe farther than this is a march together first
    static constexpr size_t kMaxParticles = 4000;
    static constexpr size_t kMaxWaterRings = 256, kMaxWaterDye = 40;

    explicit Battle(uint64_t seed, Scenario scenario = Scenario::Ford, const SkirmishSetup& setup = {});
    // An empty field for a match (Scenario::Mode): the mode raises the armies.
    // `continent` may be one already built (it is the same for every battle).
    // With `farReachM`, only the continent within that many metres of the
    // field is loaded for the horizon (a match's camera never leaves its
    // field, setZone), else all of it.
    Battle(uint64_t seed, const sim::Vec3& centre, std::shared_ptr<const sim::Continent> continent = {},
           float farReachM = 0);
    ~Battle();
    // For a conquest battle: what became of every army.
    Scenario scenario() const { return scenario_; }
    // A siege the player keeps the walls of (SkirmishSetup::defend).
    bool defending() const { return defend_; }
    // The computer's siege has been given up (its host is spent): the keepers have won.
    bool besiegersBrokeOff() const { return brokeOff_; }
    // A siege's view, for the camera: the point to look at (between the castle and the armies that come to its doors), how far
    // off to stand to hold the whole wall and every besieging squad, and the way out of the first (the road's) door. False when the
    // battle is no siege.
    bool siegeFocus(V3& at, float& distance, V3& out) const;
    // Who the player is fighting, for the HUD.
    const std::string& foeName() const { return foeName_; }

    // Runs as many fixed ticks as `seconds` of wall time covers (times the game speed).
    void advance(double seconds);
    // Fraction of the way from the previous tick to the current one, for interpolation.
    float alpha() const { return alphaSet_ ? alphaOver_ : static_cast<float>(accumulator_ * sim::kTicksPerSecond); }

    // Conquest (game/src/battle_conquest.cpp): the whole island's war is a battle too, presented by this one Battle:
    // it shows the conquest's world exactly as it shows its own, from the same places. The world must be on a region
    // with this battle's origin (the field is made at the island's: Conquest::islandOrigin), so that the world's
    // places are the battle's. Whoever steps the conquest calls beforeWorldStep() and afterWorldStep() round each
    // step of its world (Conquest::onWorldStep), says how far between ticks it is, and calls presentConquest() once a
    // frame, after the steps, and flushOrders() once before them.
    void borrow(sim::Conquest* conquest);  // nullptr: back to its own world
    bool borrowing() const { return conquest_ != nullptr; }
    sim::Conquest* conquest() const { return conquest_; }
    void beforeWorldStep();
    void afterWorldStep();
    void setAlpha(float a) { alphaSet_ = true, alphaOver_ = a; }
    // The fight in the borrowed world was cleared away (its men are gone, new ones are not those): forget them.
    void forgetMen();
    // Every man as the conquest says he stands this frame (Conquest::visibleSoldiers): the squads of the armies he is
    // of (every man is a unit of the world), who is selected or looked at, and the selection kept through a man's
    // boarding a boat and landing (it is of men, not of the world's units, which come and go).
    void presentConquest(const std::vector<sim::Conquest::Soldier>& men);
    // The player's orders for the conquest, out as Orders (Conquest::submit): the commands to men in the world, as a
    // skirmish's step would take them; the marches are submitted as they are given (marchOrder).
    void flushOrders();
    // Every man of a stack of the player's selected (the page's, at the start: over the capital).
    void selectStack(uint32_t stack);
    // The stack the selection is of (all its men in one), or 0: the stack panel's, the voyage's, the search's.
    uint32_t selectedStack() const { return selStack_; }
    // The army (squad) the selection is exactly (every man of it and no other), or 0.
    uint32_t selectedArmy() const;

    // What the viewer may see of the others (Conquest::canSee, through the conquest this field presents): a man of
    // `owner` at battle-local x, y that it refuses is not drawn, picked or looked at. A skirmish's field sees all.
    bool sees(uint8_t owner, float x, float y) const;
    // The man nearest the finger on the screen within `tapPx` pixels, at any height and any distance (a dot is as
    // much a man as a sprite): a unit of the world (his id).
    struct ManPick {
        enum class Kind : uint8_t { None, Unit } kind = Kind::None;
        uint32_t id = 0;
        size_t index = 0;
        float px = 1e30f;  // how far off the finger he is, in pixels
    };
    ManPick manAt(const Camera& camera, float x, float y, float tapPx) const;
    // The standing gate under the finger, or -1.
    int gateAt(const Camera& camera, float x, float y, float pixelScale) const;
    // What the player looks at and cannot order: view-only, grey. One man not his (a unit of the world) or a gate; a tap on any of them with nothing selected looks at it, and any other tap lets it go.
    struct Inspected {
        enum class Kind : uint8_t { None, Unit, Man, Gate } kind = Kind::None;  // Man: in Conquest, by his id (Conquest::Man)
        uint32_t id = 0;
    };
    const Inspected& inspected() const { return inspected_; }
    bool isInspected(sim::EntityId id) const { return inspectedUnit_ == id && id != 0; }
    // What the card of the one looked at shows (the page prints it).
    struct Glance {
        bool any = false, gate = false, awake = false, broken = false;
        // A gate of ours: how open it is (0 shut .. 1000 open) and how it is held (0 let go, 1 open, 2 shut).
        bool ours = false, opens = false;  // opens: standing open or swinging open (a tap shuts it)
        int swing = 0, pin = 0;
        const char* type = "";
        uint8_t owner = 0;
        int hp = -1, max = -1, rank = 0, morale = 0, hpGate = 0, stack = 0;
    };
    Glance glance() const;


    // Input. Selection is as in Myth (game/src/battle_command.cpp): tapping
    // one of ours selects him alone, tapping him again everyone of his type
    // in view; `additive` (shift-click) adds or removes him instead.
    void tap(const Camera& camera, float x, float y, float pixelScale, bool additive = false);
    // A right click (game/src/battle_orders.cpp): the order a tap there gives
    // the selection, but never a change of who is selected; on one of ours
    // they march to where he stands. Nothing selected: nothing happens.
    // `append` (Shift on the desktop): the order is added to the end of each man's queue (sim::Command::queue) instead of
    // replacing the one in hand. A march beyond the men's reach in a conquest is a stack's, and cannot be queued: it replaces.
    void orderTap(const Camera& camera, float x, float y, float pixelScale, bool append = false);
    // A mouse's left click (game/src/battle_command.cpp): selection only. One of
    // ours is picked as a tap picks him; anything else is looked at, and the
    // ground (shift not held) lets everyone go. Orders are the right click's.
    // With an order mode set (Ctrl, Alt, Z, V, the attack button) it is the tap.
    void selectTap(const Camera& camera, float x, float y, float pixelScale, bool additive = false);
    // The mouse scheme: a box that ends where it began is a selectTap, not a tap.
    void setMouseScheme(bool on) { mouseScheme_ = on; }
    void selectSquad(int index);
    void select(std::vector<sim::EntityId> units);
    void clearSelection();
    // Every living soldier of ours of this type that the camera can see.
    void selectVisibleOfType(const Camera& camera, sim::UnitType type);
    // Ours inside a screen rectangle (pixels).
    void selectInRect(const Camera& camera, float x0, float y0, float x1, float y1, bool additive = false);
    // Every hero of ours.
    void selectHeroes();
    // Saved groups, keys 1-9 and 0 (slots 0-9): Alt+number on the desktop,
    // the "+" card on a phone.
    // A selection's arrangement, kept as it marches (keepShape): each
    // soldier's place from its middle and the way he faces there, and the way
    // the whole faced when it was taken.
    struct Shape {
        std::vector<sim::EntityId> ids;
        std::vector<V3> offset, face;
        V3 front{1, 0, 0};
    };
    // A group is a list of men: in a skirmish their unit ids; in a conquest their man ids (a unit id there is a place in the
    // world that men leave and come to as they board a boat and land), found again each time among the men.
    struct Group {
        std::vector<sim::EntityId> units;  // skirmish
        std::vector<uint32_t> men;         // conquest
        Formation formation = Formation::Line;
        Shape shape;  // as it stood when saved: recalled, it marches so
    };
    static constexpr int kGroupSlots = 10;
    void assignGroup(int slot);
    // The first free slot, or -1 when all are taken.
    int saveGroup();
    bool recallGroup(int slot);
    void clearGroup(int slot);
    // Ungroup: forgets every group whose living men are all selected. How many went.
    int ungroupSelection();
    // Tab (dir +1) and Shift+Tab (-1): select the next or previous group of units. The cycle runs over the saved groups first, in
    // key order (1-9, then 0), then over the squads that are in no saved group, in squad order; it wraps at both ends. From a
    // selection that is none of them, forward goes to the first, back to the last. False when there is nothing to cycle to.
    bool cycleGroup(int dir);
    const Group& group(int slot) const { return groups_[static_cast<size_t>(slot)]; }
    // A group's living men now (unit ids), as the selection has them.
    struct GroupMen {
        std::vector<sim::EntityId> units;
        int alive() const { return static_cast<int>(units.size()); }
    };
    GroupMen groupMen(int slot) const;
    size_t groupSize(int slot) const;           // men ever put in it, the fallen too (0: the slot is free)
    const char* groupLabel(int slot) const;     // what kind they are: a type's name, or "Mixed"

    // Orders to the selection (for tests and hooks as well as the input).
    // At a gate, as tapping it.
    void orderAtGate(int gate);
    // The player's hand on a gate of his (sim::Command::SetGate, through the one command list): held open or shut, or let go
    // to open to his men bound through it. `swingGate` is a tap on it: open if it stood shut, shut if it stood open.
    bool gateIsOurs(int gate) const;
    void pinGate(int gate, sim::Command::GatePin pin);
    void swingGate(int gate);
    // The O key and the card's buttons: the gate looked at, else the one of ours nearest the selection (or the men).
    // `release` lets it go (Shift+O) instead.
    void swingGateByKey(bool release);
    // March there, as tapping the ground, facing as they do now: as they stand
    // (keepShape), their own arrangement moved there; in a chosen formation,
    // re-formed there.
    void orderMove(const V3& to);
    // March there and face that way on arrival (pressed there and dragged
    // the way to face: pressEnd), the whole arrangement turned to it.
    // `frontMetres`: the drag's length, the width the front is made (0: the usual block).
    void orderMoveFacing(const V3& to, const V3& facing, float frontMetres = 0);
    void stopSelected();
    // Tactics told and commanded (battle_tactics.cpp; the rules are the world's: world_tactics.cpp).
    // The selection's stance with no order in hand: Hold (they keep their place), or support (the default: they go to the
    // help of a friend fighting within 40 m). holdSelected switches between the two.
    void holdSelected();
    bool selectionHolds() const;
    // What a squad's card says beyond its men: idle (no order, nothing to fight), holding, and broken: the seconds until it
    // rallies on its own (0: not broken; -1: a foe is too near for it to rally).
    // ... calling for help (a few seconds after its call), answering a call (on its way to the squad that called), and the
    // ground it stands on, in a word or two ("high ground", "in the ford", "in woods"; "" on plain ground).
    struct SquadState {
        bool idle = false, hold = false;
        int rally = 0;
        bool calling = false, answering = false;
        const char* ground = "";
    };
    // The ground at a place as it would tell for the selection sent there ("ford", "woods", "higher ground"; "" plain).
    const char* groundAt(const V3& at) const;
    // A call for help the local side has heard, for the map and the mark over the squad: where, when, and who goes.
    struct Call {
        uint32_t tick = 0;
        V3 pos;
        int squad = -1, answer = -1;  // squads_ indices (-1: none)
        sim::EntityId caller = sim::kNoEntity;  // a man of the squad that called
    };
    const std::vector<Call>& calls() const { return calls_; }
    // Answering by hand: the latest call still standing is chosen, and the nearest free squad of the player's selected
    // (false and a notice when nobody calls; `at`: where the caller is, for the camera).
    bool answerCall(V3& at);
    // The squad a man's world squad number names (World::setSquad), or -1.
    int squadOfNumber(uint32_t number) const;
    SquadState squadState(const Squad& s, bool ground = false) const;
    const char* groundTag(const Squad& s) const;
    // Selects the next of the player's squads standing idle (its index; -1 and a notice when none is).
    int selectNextIdle();
    // The lord's Rally: every broken man of his within 40 m turns back at once; then he waits a minute. False (and a notice)
    // when there is no lord, he has just called, or nobody broken hears him.
    bool orderRally();
    struct RallyState {
        bool lord = false;  // a lord of the player's is on the field
        int cooldown = 0;   // seconds until he may call again
        int heard = 0;      // broken men in his earshot now
    };
    RallyState rallyState() const;
    // A word floated over the men it happened to ("Flanked", "In the ford", "Braced"): where, and whether it is good news
    // for the viewer. Only what he can see (sees()).
    struct Float {
        uint32_t tick = 0;
        sim::EntityId unit = sim::kNoEntity;
        V3 pos;
        const char* text = "";
        bool good = false;
    };
    static constexpr uint32_t kFloatShown = 3 * sim::kTicksPerSecond;
    const std::vector<Float>& floats() const { return floats_; }
    // Planning (client side only; the sim and the other peers know nothing of it until the release). While on, the orders a
    // gesture gives (a right click, a facing drag, an attack, a formation, scatter, raid) are not sent but held, per man, and
    // drawn as pending paths for every man that has any. Releasing sends them all in the one tick in the order given, exactly as if
    // given then: a plain order replaces and a queued one appends. A plain order held replaces the man's held ones (as it would
    // replace them on arrival). Halt is not held: it halts at once and drops that man's held orders. The plan is dropped when the
    // battle or its world changes, and a man dead at the release is skipped.
    bool planning() const { return planning_; }
    void setPlanning(bool on);  // off: releases
    void togglePlanning() { setPlanning(!planning_); }
    void releasePlan();         // sends what is held, and leaves planning
    void discardPlan();         // drops what is held, and leaves planning
    size_t plannedCount() const { return plan_.size(); }
    // Attacks (game/src/battle_orders.cpp). Everyone selected after one
    // soldier, of either side (a force attack on a friend): missile troops
    // keep to a bowshot and shoot him, the rest run him down. When he falls
    // each decides for himself: another foe, or back to his squad.
    void orderAttackUnit(sim::EntityId target);
    // Missile troops shoot at the spot; the rest march there, fighting
    // whoever they meet.
    void orderAttackGround(const V3& at);
    // The selected who carry satchels walk there and lay one each, a few metres apart.
    void orderLaySatchels(const V3& at);
    // The selected scavengers let loose to raid from where they stand (sim
    // Command::Raid); false (and a notice) when none is selected.
    bool orderRaid();
    // The special action (the desktop's T): each selected soldier's own. The
    // scavengers raid at once; with grenadiers selected, the next tap on the
    // ground lays their satchels (OrderMode::LaySatchel; again, it is let go).
    void specialAction();
    // Whether any selected soldier is a scavenger (the Raid button shows), or carries satchels.
    bool selectionRaids() const;
    bool selectionLaysSatchels() const;
    // March there deaf to the enemy (Myth's force march): to break away, or
    // to run past.
    void orderForceMarch(const V3& to, const V3* facing = nullptr);
    // What lies under the finger.
    struct Pick {
        enum class Kind : uint8_t { None, Own, Enemy, Gate, Wall, Obstacle, Ground };
        Kind kind = Kind::None;
        int index = -1;  // the unit (index), gate, wall ring or obstacle
        int squad = -1;  // zoomed far out: the squad nearest the finger (index -1)
        V3 at;           // where on the ground
    };
    Pick pickAt(const Camera& camera, float x, float y, float pixelScale) const;
    // Where each soldier's picture was last drawn (the renderer's): per unit,
    // its bottom-left and top-right corners in the world (6 floats; NaN when
    // not drawn as a picture). unitAt picks a man by it: one lying down is
    // drawn across the ground, not above his feet.
    void setSpriteBoxes(std::vector<float> boxes) { spriteBoxes_ = std::move(boxes); }
    // At a gate, a wall or a house or tree: who can, attacks it (missile
    // troops shoot a gate and the rest go to hack it; only engines,
    // grenadiers and wizards harm anything at a wall, house or tree).
    void orderAttackObject(const Pick& pick);
    // How the next tap is read: as usual, as an attack on whatever is tapped
    // (the phone's attack button, Ctrl-click or A on the desktop), or as a
    // force march (a long press on the attack button, Alt-click or M).
    // Attack and force-march modes last for one order.
    // LaySatchel: the next tap on the ground sends the selected grenadiers
    // there to lay a satchel each (sim Command::LaySatchel).
    enum class OrderMode : uint8_t { Normal, Attack, ForceMarch, LaySatchel };
    void setOrderMode(OrderMode m) { orderMode_ = m; }
    OrderMode orderMode() const { return orderMode_; }
    // Modifier keys held on the desktop: force (Ctrl/Cmd) and march (Alt).
    void setKeys(bool force, bool march) {
        forceKey_ = force;
        marchKey_ = march;
    }
    // What a tap there would do, for the desktop cursor and the tests.
    // GateSwing: one of our own gates, which a tap opens or shuts.
    enum class Intent : uint8_t { None, Select, Attack, AttackObject, AttackGround, Cannot, Move, ForceMarch, GateSwing };
    Intent intentAt(const Camera& camera, float x, float y, float pixelScale) const;
    // The last order refused or explained ("Only engines ... can harm walls"), and when (tick).
    const std::string& notice() const { return notice_; }
    uint32_t noticeTick() const { return noticeTick_; }
    // What just happened to the player's own, for a short strip (battle_events.cpp): squads that broke or were
    // wiped out, a hero fallen, the last army of a Conquest destroyed. One path for a skirmish and a Conquest.
    // `at` counts the world's steps (not its day's tick); a line is kept a while (kEventKeep) and shown for less.
    struct Event {
        uint32_t at = 0;
        std::string text;
    };
    static constexpr uint32_t kEventShown = 7 * sim::kTicksPerSecond, kEventKeep = 30 * sim::kTicksPerSecond;
    const std::vector<Event>& events() const { return events_; }
    uint32_t eventClock() const { return eventClock_; }
    // Orders the player has given since the game began (a march across the Conquest, or a squad's in a fight): the
    // first-order hint fades on the first.
    uint32_t ordersGiven() const { return ordersGiven_; }
    // Where the player's soldiers are going and what they are after, for the
    // ground markers: shown for each soldier selected (not his squad- or
    // group-mates) and, fading, for a few seconds after any order; only his
    // current order, until he is there. Heights are the floor (floorAt).
    struct OrderMark {
        enum class Kind : uint8_t { Slot, Target, Spot };
        Kind kind = Kind::Slot;
        sim::EntityId unit = sim::kNoEntity;  // who is going (Slot) or attacking
        V3 pos;     // the place he will stand, or the target (interpolated), or the spot
        V3 face;    // Slot: the way he will face there
        V3 from;    // Target, Spot: the attacker, for the line
        float size = 1;
        float opacity = 1;  // 1 while selected; fading after an order
    };
    std::vector<OrderMark> orderMarks() const;
    // How far the shooters selected can throw or loose: one ring for each kind of shooter selected, round the middle of
    // those of it selected (the radius is the weapon's furthest reach, `inner` the nearest it can shoot).
    struct RangeRing {
        V3 centre;
        float radius = 0;
        float inner = 0;
    };
    std::vector<RangeRing> rangeRings() const;
    // Where the selected catapults' boulders would come down and roll (sim::World::stonePath, the very steps the stone
    // takes): at the place the pointer is over while a ground attack is the next click (aimHover), else at the ground
    // each is ordered to shell. One for each engine in reach of its mark (a few at most).
    struct StoneForecast {
        V3 landing;
        std::vector<V3> roll;  // from the landing to where it would lie
    };
    std::vector<StoneForecast> stoneForecasts() const;
    // The pointer is over (x, y): if a click there would be a ground attack, the forecasts are shown for it.
    void aimHover(const Camera& camera, float x, float y, float pixelScale);
    // The paths the player's soldiers will walk (path_lines.h), shown as the order markers are: for each soldier
    // selected, and fading for a moment after an order; the player's own men only, so no foe's route is ever told.
    // Men going one way are one line. `perMan`: a line for each man, and `faintMarches` (Conquest): the player's
    // marching stacks too, faint. The line's heights are the floor (floorAt).
    std::vector<RouteLine> pathLines(bool perMan, bool faintMarches) const;
    // How strongly a soldier's order markers show (0: not at all).
    float markOpacity(size_t index) const;
    // Tests and hooks: the town's gates given to a side (and its garrison to keep them), a gate battered down at once; every archer of ours
    // looses at a spot on the ground.
    void breakGate(int gate);
    void giveGates(sim::PlayerId holder) { holdGates(holder); }
    void testShootAt(const V3& at);
    // Test hook: a soldier taken out of the world (as dead, to the orders).
    void testRemove(sim::EntityId unit) { world_->removeUnit(unit); }
    // Test hook: an order put in a soldier's queue (any side's), as the sim keeps it.
    void testQueueFor(sim::EntityId unit, const sim::Command& c) { world_->testQueue(unit, c); }
    // Test hook: one soldier (of either side) ordered to walk to a spot.
    void testMove(sim::EntityId unit, const V3& to);
    // Run! Everyone selected scatters a few metres outward from their centre.
    void scatter();
    // Every one of the player's squads at the nearest enemy: the foot charge,
    // missile troops close to a long bowshot.
    void chargeAll();
    // The selection's formation; it re-forms where it stands, and marches in
    // it until it is selected afresh or keepShape.
    void setFormation(Formation f);
    void cycleFormation();
    // As they stand: the selection marches in its own arrangement, each
    // soldier where he was last sent beside the others (shapeOf), turned as a
    // whole to a new front. What every selection starts in.
    void keepShape();
    bool keepingShape() const { return selKeep_; }
    // The arrangement the selection would keep (its saved group's, recalled).
    Shape selectionShape() const;

    // A press held on the battlefield (Gestures' hold): drawing a selection
    // box, or a move's facing: pressed where they are to go, dragged the way
    // they are to face, the ghost of them there turning with it (pressed on
    // one of the selected: they turn where they stand). `box` forces a box
    // (shift on the desktop); otherwise it is a box when nothing is selected
    // or the press starts on one of ours. False: not taken (the camera pans).
    bool pressBegin(const Camera& camera, float x, float y, float pixelScale, bool box);
    void pressMove(const Camera& camera, float x, float y);
    // `additive`: for a box, adds to the selection; for a facing line, appends the march to the queues (as orderTap's `append`).
    void pressEnd(const Camera& camera, float x, float y, bool cancelled, bool additive = false);
    struct Drag {
        enum class Mode : uint8_t { None, Box, Facing };
        Mode mode = Mode::None;
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;  // the box, in pixels
        float pixelScale = 1;
        V3 anchor, facing{1, 0, 0};             // where they go, and the way they will face
        bool aimed = false;                     // dragged far enough to set a facing
        V3 tip;                                 // where the finger is, on the ground
        std::vector<FormationSlot> ghost;       // the formation there, in battle coordinates
    };
    const Drag& drag() const { return drag_; }
    void togglePause() { paused_ = !paused_; }
    void cycleSpeed() { speed_ = speed_ >= 4 ? 1 : speed_ * 2; }
    void setSpeed(int speed) { speed_ = std::clamp(speed, 1, 4); }
    // Time slowed to this share (the sandbox's slow motion; 1 as usual).
    void setSlowMotion(float share) { slow_ = std::clamp(share, 0.05f, 1.0f); }
    float slowMotion() const { return slow_; }
    // Paused: one tick on (the sandbox's frame step).
    void stepOnce() {
        if (paused_) step();
    }

    // A match's field (sim World::setZone): a circle round `centre` (battle
    // coordinates) whose edge wounds whoever stands beyond it, `pctPerSecond`
    // of his health a second. Radius zero: none.
    void setZone(V3 centre, float radius, int32_t pctPerSecond);
    V3 zoneCentre() const { return zoneCentre_; }
    float zoneRadius() const { return zoneRadius_; }

    // Matches (mode.h, game/src/battle_modes.cpp). The mode runs every tick:
    // it gathers each seat's orders, plays the computer's seats and keeps the
    // score; while one runs, the built-in enemy stands down.
    void setMode(std::unique_ptr<BattleMode> mode);
    BattleMode* mode() const { return mode_.get(); }
    // A squad raised during the battle (a wave, reinforcements); its index.
    int addSquad(const std::string& name, sim::PlayerId owner, sim::UnitType type, int count, V3 centre, V3 facing,
                 int rank = 0);
    // Where such a squad can stand: `centre` if every man of its formation
    // can, else the nearest place within `reachM` where they all can (a ruin,
    // a house or a pond in the way), else `centre`.
    V3 clearSpot(sim::UnitType type, int count, V3 centre, V3 facing, float reachM = 150) const;
    // The side the person at this screen plays (kPlayer unless a match seats
    // him elsewhere): whose soldiers he selects and orders, whose voices he
    // hears as his own. Presentation only: the simulation never reads it, so
    // every peer of a match plays the same world from its own side.
    sim::PlayerId localSide() const { return local_; }
    void setLocalSide(sim::PlayerId side);
    // A squad of the local player's side seated by someone else (an ally):
    // seen and fought beside, but not his to order.
    void setForeign(size_t squad, bool foreign);
    bool controllable(const sim::Unit& u) const {
        return u.owner == local_ && !(u.id <= foreign_.size() && foreign_[u.id - 1]);
    }
    bool controllable(const Squad& s) const { return s.owner == local_ && (s.members.empty() || controllable(world_->units()[s.members.front() - 1])); }
    // How many a squad's card counts: its men that are alive, and the squad's full size.
    int totalCount(const Squad& s) const { return s.total > 0 ? s.total : static_cast<int>(s.members.size()); }
    // A fallen soldier back in his squad (Horde's hero returning): a fresh
    // soldier with the fallen one's stats, rank, kills and blessings, at
    // `hpPct` of his health, takes his place in the squad. Its id.
    sim::EntityId returnFallen(size_t squad, sim::EntityId fallen, V3 at, V3 facing, int32_t hpPct);
    // The computer's orders to a squad. Steer: march there with initiative
    // (a squad already on its way to about the same place keeps its route).
    // ForceMarch: deaf to the enemy. Attack: every soldier after one foe.
    void aiSteer(size_t squad, const V3& to);
    void aiForceMarch(size_t squad, const V3& to);
    void aiAttack(size_t squad, sim::EntityId target);
    void aiHalt(size_t squad);
    // A computer's lord calls the Rally when enough of his broken men hear him: the player's own command (Command::Rally).
    void aiRally(size_t squad);
    // The computer's purse on Easy, as a share of the player's.
    static constexpr int kEasyPursePct = 75;
    // In the open (the Field) Hard raises this share of the player's purse; Normal is his equal (docs/BALANCE.md).
    static constexpr int kOpenHardPursePct = 125;
    // Easy comes late: it stands this long before it marches (unless the player comes to it).
    static constexpr uint32_t kEasyWaitTicks = 45 * sim::kTicksPerSecond;
    // How well the computer plays (decision quality, never stats). Easy: the
    // nearest foe, marching piecemeal, archers wherever they like, the
    // broken fighting on. Normal: weak and exposed foes first (healers,
    // casters, engines, archers, the lord, the wounded), the army massed
    // before it engages, missile troops and the lord behind the foot, the
    // shattered pulled back to mend. The conquest's Easy/Hard map onto them.
    // Hard: Normal, and it goes in on one point, answers its calls with horse round the flank, and gives the bows less time.
    enum class AiSkill : uint8_t { Easy, Normal, Hard };
    void setAiSkill(AiSkill s) { aiSkill_ = s; }
    AiSkill aiSkill() const { return aiSkill_; }
    // Whether the squad is shattered enough that the computer pulls it back
    // (a third of its men left, or half of them wavering), with a friend to fall back on.
    bool aiShattered(const Squad& s) const;
    // The foe the computer would go for from `around` within `reach`: the
    // nearest, but weak and exposed ones count nearer (Normal). The one
    // place the computer reads the enemy: where fog will filter.
    sim::EntityId pickFoe(sim::PlayerId owner, const V3& around, float reach, bool mounted) const;
    // The computer's scavengers (the hyena-folk): marched toward `goal`
    // until a foe is within kRaidCallM of them, then let loose to raid
    // (sim Command::Raid); left to it while any of them raids, and rested a
    // while when a raid ends before they are sent again.
    static constexpr float kRaidCallM = 40.0f;
    static constexpr uint32_t kRaidRestTicks = 15 * sim::kTicksPerSecond;
    void aiRaid(size_t squad, const V3& goal);
    // The computer's general handling of a squad: the nearest foe within
    // `reach` of `around` is met (missile troops keep a bowshot, horse take
    // it in the flank), else it goes to `goal`; healers keep near their
    // friends, engines throw from where they stand. Routed: beyond orders.
    void aiFight(size_t squad, const V3& goal, const V3& around, float reach);
    // The battle's AI for the computer lords' men standing in the field of a Conquest (Conquest::setThinker): the commands
    // of the other sides' squads are added to `out`, the step's. Garrisons at their posts are the conquest's own (fightAi).
    void conquestAi(std::vector<sim::Command>& out);
    // The nearest living, unbroken foe of `owner` to `at` within `reach` (or kNoEntity).
    sim::EntityId nearestFoe(sim::PlayerId owner, const V3& at, float reach) const;

    // Queries.
    const sim::World& world() const { return *world_; }
    sim::World& mutableWorld() { return *world_; }
    const std::vector<Squad>& squads() const { return squads_; }
    // Test hook: every soldier of a squad raised to this rank (shield pips on the health bars).
    void promoteSquad(size_t squad, int rank) {
        if (squad < squads_.size())
            for (sim::EntityId id : squads_[squad].members) world_->setVeterancy(id, rank, 0);
    }
    // The squad the selection is (all its living soldiers and no one else), or -1.
    int selectedSquad() const;
    const std::vector<sim::EntityId>& selection() const { return selection_; }
    // Living selected soldiers.
    std::vector<sim::EntityId> selectedAlive() const;
    bool isSelected(sim::EntityId id) const { return id > 0 && id <= selectedFlag_.size() && selectedFlag_[id - 1]; }
    Formation selectionFormation() const { return selFormation_; }
    // The gate last tapped (its health shown over it), or -1; any other tap lets it go.
    int inspectedGate() const { return inspected_.kind == Inspected::Kind::Gate ? static_cast<int>(inspected_.id) : -1; }
    // Conquest's fights on this field (its world is not this battle's): show what its blows and wounds did (blood,
    // dust, sparks, splinters), and move the sparks and smoke on.
    void stepEffects(float seconds);
    size_t selectionSize() const { return selectedAlive().size(); }
    // The types of those selected, one name for them all ("Mixed" when they are not).
    const char* selectionLabel() const;
    // The small edges of the first of those selected (an awake man's, as the world has him; a sleeper's, as his army has them
    // on the map: sim/edges.h), "hero leads +2%", or "" with none.
    std::string selectionEdges() const;
    bool selectionBatters() const;
    // 0 none of the squad selected, 1 some, 2 all its living soldiers.
    int squadSelection(const Squad& s) const;
    int groupSelection(int slot) const;
    bool paused() const { return paused_; }
    int speed() const { return speed_; }
    // 0 in progress, 1 victory, 2 defeat. A battle is won when no foe is left, when the town's heart is taken, or when
    // a host has quit the field (sim::RoutRule) and the pursuit is over.
    int outcome() const;
    // The end of a battle (battle_end.cpp). The side that has quit the field and is being chased off it (or kNobody).
    sim::PlayerId routedSide() const { return rout_.quitter(); }
    // The pursuit: seconds of it left (0: none going on), and the kinds that ride a beaten host down unasked.
    int pursuitSeconds() const;
    static bool pursues(sim::UnitType type);
    static constexpr uint32_t kPursueLookTicks = sim::kTicksPerSecond;  // how often a pursuer with nobody in hand picks a runner
    // Decided: the clock runs slow over the last blow (kLastBlowTicks at kLastBlowPace), then the fight stops for good.
    // The ticks are the world's, so every peer stops on the same one; the slowing is the clock's alone.
    static constexpr uint32_t kLastBlowTicks = 8;
    static constexpr double kLastBlowPace = 0.25;
    bool decided() const { return decidedTick_ != UINT32_MAX; }
    bool over() const { return decided() && world_->tick() >= decidedTick_ + kLastBlowTicks; }
    // What the results sheet says: how long, what each of the local side's squads did and lost, who fell.
    struct Result {
        int outcome = 0;
        enum class How : uint8_t { Wiped, Rout, Flag } how = How::Wiped;
        int seconds = 0, killed = 0, lost = 0;
        struct Row {
            std::string name;
            int alive = 0, total = 0, kills = 0;  // total: the men it began this battle with
            bool broke = false;
            int rank = 0, promoted = 0;  // the survivors' rank (Standing; a hero's level), and how many of them rose a rank in this battle
            bool hero = false;
        };
        int promoted = 0;                 // men of the local side who rose a rank and lived
        bool conquest = false;            // a fight of a Conquest (fightReport): the war goes on; a squad there has one rank
        std::vector<Row> rows;
        std::string best;                 // the squad with the most kills ("" with none)
        std::vector<std::string> fallen;  // heroes fallen, the local side's first
    };
    Result result() const;
    // A Conquest has no end of battle, but its fights end (battle_events.cpp, observeFight): when the local side's
    // men have fought and then not struck a blow for kFightOverTicks, the results sheet of that fight (who was in it,
    // what each lost and killed, who rose a rank, whether a host quit the field). It stays until dismissed; whoever
    // steps the conquest holds its clock while it is up. A brush under kFightWorth dead is not told.
    static constexpr uint32_t kFightFreshTicks = 5 * sim::kTicksPerSecond, kFightOverTicks = 8 * sim::kTicksPerSecond;
    static constexpr int kFightWorth = 6;
    const Result* fightReport() const { return fightTold_ ? &fightReport_ : nullptr; }
    void dismissFightReport() { fightTold_ = false; }
    // The local side's squads as they stand now, for whoever carries them to another battle (march.h): every squad it
    // began with, the wiped out too (no men), each living man with his health, rank and kills.
    std::vector<CarriedSquad> carried() const;
    // Deployment: the battle waits at its first tick for the player to set his line. Orders are taken (they are held
    // as any order is until the next step, and their markers show); beginBattle starts the clock. The app asks for it
    // (a skirmish from the menu); a battle made by a test or a hook runs at once. Single player: over the network the
    // start is every seat's to agree, as a change of pace is (docs/NETCODE.md).
    void setDeploying(bool on) { deploy_ = on; }
    bool deploying() const { return deploy_; }
    void beginBattle() { deploy_ = false; }
    // A town's capture point (battle_capture.h); inactive in the field.
    const CapturePoint& capturePoint() const { return capture_; }
    // A siege in a player's words, for every HUD (battle_capture.cpp). The doors of the besieged town with what is left of
    // each (percent), the outer wall's first and a bailey's last; one line on the flag (what blocks it, or how far it has
    // climbed); and once the battle is decided, why (the result screen's line).
    struct SiegeGate {
        const char* name;
        int hpPct;
    };
    std::vector<SiegeGate> siegeGates() const;
    std::string siegeLine() const;
    std::string siegeResult() const;
    // Test hook: the gates battered in and every squad of ours sent to the capture point.
    void testStorm();
    int aliveCount(const Squad& s) const;
    Standing standing(const Squad& s) const;
    V3 squadCentre(const Squad& s, bool interpolated = true) const;
    int squadOf(sim::EntityId id) const { return squadOfUnit_[id - 1]; }

    // Ground height anywhere: the battlefield exactly, the rest of the continent coarsely.
    float heightAt(float x, float y) const;
    // What something at p stands on, or is above: the ground, or a deck (a
    // castle's wall-walk or stair, a bridge) under it. Where the decals under
    // a soldier (shadow, selection ring) and the order markers lie.
    float floorAt(const V3& p) const;
    // Where the ray through a pixel first meets what a man could be sent onto:
    // the ground, a deck (a wall-walk, a ramp, a bridge), or a wall's stone,
    // which puts the spot on its walk. False when it meets nothing.
    bool pickFloor(const Camera& camera, float x, float y, V3& hit) const;
    bool inBattlefield(float x, float y) const { return x >= 0 && y >= 0 && x <= kWorldSize && y <= kWorldSize; }
    const FarGrid& farGrid() const { return far_; }
    const FarGrid& nearGrid() const { return near_; }
    const sim::Region& region() const { return *region_; }
    const sim::Continent& continent() const { return *continent_; }
    std::shared_ptr<const sim::Continent> continentShared() const { return continent_; }
    // Continent coordinates of this battlefield's (0, 0).
    V3 origin() const { return toV3(region_->origin()); }
    // The ground of the world presented (its gates, walls, decks): the field's own region, or the borrowed world's.
    const sim::Region& worldRegion() const { return *world_->region(); }
    bool inWorld(float x, float y) const {
        const float size = static_cast<float>(worldRegion().terrain().cells()) * kCellSize;
        return x >= 0 && y >= 0 && x < size && y < size;
    }
    const std::vector<float>& heights() const { return heights_; }

    // Interpolated presentation positions.
    V3 unitPosition(size_t index) const;
    bool unitMoving(size_t index) const;
    uint32_t deathTick(size_t index) const { return deathTick_[index]; }
    // When a soldier last struck or loosed, and was last hurt (UINT32_MAX: never).
    uint32_t attackTick(size_t index) const { return attackTick_[index]; }
    uint32_t hitTick(size_t index) const { return hitTick_[index]; }
    template <typename Fn>
    void forEachProjectile(Fn&& fn) const;

    // Ground marks live in a ring: the k-th one ever added is at index k % capacity.
    const std::vector<GroundMark>& groundMarks() const { return marks_; }
    uint64_t groundMarksAdded() const { return marksAdded_; }
    // Remain slots changed since the renderer last asked (then cleared).
    std::vector<uint32_t> takeDirtyRemains();
    const std::vector<Particle>& particles() const { return particles_; }
    // The water's rings (a ring buffer: old ones are overwritten; the reader keeps those still spreading) and its blood.
    const std::vector<WaterRing>& waterRings() const { return rings_; }
    const std::vector<WaterDye>& waterDye() const { return dye_; }
    // The water over a point of the world presented: its surface's height and the metres of it; false on dry land.
    bool waterAt(float x, float y, float& surface, float& depth) const;
    // Centre of the fiercest fighting of the last few seconds; false if all is quiet.
    bool hotSpot(V3& at) const;
    double stepMs() const { return stepMs_; }
    // The soldiers' voices (voice.h): cues from each tick and every order.
    Voice& voice() { return voice_; }
    const Voice& voice() const { return voice_; }
    // The sound effects (sfx.h): cues from each tick, and the ambience.
    Sfx& sfx() { return sfx_; }
    const Sfx& sfx() const { return sfx_; }

private:
    Scenario scenario_ = Scenario::Ford;
    bool defend_ = false;
    // A siege the computer lays: its fighting men at the start (engines apart), and whether it has broken off (runEnemyAi).
    int besiegers_ = 0;
    bool brokeOff_ = false;
    V3 siegeCentre_{}, siegeOut_{};
    float siegeRadius_ = 0;
    int siegeDoors_ = 0;
    std::string foeName_ = "Red Hand";
    void setUpFord(const SkirmishSetup& setup);
    void setUpField(const SkirmishSetup& setup);
    void setUpSiege(const sim::Continent::City& town, const SkirmishSetup& setup);
    // The player's army (what he bought, else bought for him) and the other side's, from the setup's budget.
    std::vector<Buy> playerBuys(const SkirmishSetup& setup, bool engines, int every = kFieldEngines) const;
    std::vector<Buy> foeBuys(const SkirmishSetup& setup, bool engines, int every = kFieldEngines, int pct = 100) const;
    // An army in ranks (foot in front, bowmen behind, horse on the wings, engines last) about `anchor`, facing `facing`.
    // `roster`: these squads as they stand instead (SkirmishSetup::roster), their lord among them. `skirmishers`: the
    // bowmen stand out in front of the foot (open ground: the battle opens with their arrows).
    void muster(sim::PlayerId owner, const std::vector<Buy>& army, V3 anchor, V3 facing, bool lord = false, bool enginesFirst = false,
                const std::vector<CarriedSquad>* roster = nullptr, bool skirmishers = false);
    // The last squad made takes a carried squad's men: each his health, rank and kills, and the squad its full strength.
    void seatCarried(const CarriedSquad& c);
    void build(uint64_t seed, const sim::Vec3& centre, float farReachM = 0);
    void finish();
    void spawnSquad(const std::string& name, sim::PlayerId owner, sim::UnitType type, int count, V3 centre,
                    V3 facing);
    void step();
    void runEnemyAi();
    // A computer side with engines at a standing gate of the other's: where its battery nearest `from` is and the gate being
    // battered. Its other squads escort the battery nearest them (aiEscort) until the gate is down.
    bool aiSiege(sim::PlayerId owner, const V3& from, V3& engines, V3& gate) const;
    // A gate the computer's engines may be sent at: in a siege with a capture point, only the besieged town's own doors (its
    // outer wall's and its bailey's), not a neighbouring town's that the field happens to hold; else any (aimEngines' scope).
    bool siegeGate(size_t gate) const;
    // A second battery (another is nearer the gate) with the foe's men on a wall-walk in its reach: it shells them, unordered.
    bool aiShellsWalls(const Squad& s) const;
    // The computer's siege is spent (kSpentPct of its fighting men or fewer stand): it marches off. True once it has.
    bool aiBreakOff();
    void aiEscort(size_t squad, const V3& engines, const V3& gate);
    // Where the army's foot stands against `foe`: how far toward it the front reaches (metres along `dir`), false with no foot.
    bool footFront(sim::PlayerId owner, const Squad& except, const V3& dir, float& front) const;
    // A squad of the owner's well behind this one (aiFight's massing): it would arrive piecemeal, ahead of the rest.
    bool armyStrung(const Squad& s, const V3& dir) const;
    void collectEvents();
    void applyImpacts(const std::vector<sim::ImpactEvent>& events, const sim::World& source, V3 shift, bool own);
    // Each tick's deaths, seen and heard (battle_deaths.cpp): blood rain, smoke, sound effects.
    void presentDeaths();
    void addMark(GroundMark::Kind kind, V3 pos, float yaw, float size, uint32_t seed);
    void emit(Particle::Kind kind, V3 pos, V3 vel, float life, float size);
    // The water (battle_water.cpp). An event that fell in or on it is shown by the water and not by the ground (true):
    // a ring and spray, a stone's column, a fire's steam, a struck man's blood in it and the splash of his fall. Each
    // tick: the waders' rings and wakes, the wounded dripping, the dye going downstream.
    bool presentInWater(const sim::ImpactEvent& e, const V3& pos, const sim::Unit* victim, const V3& body);
    void stepWater();
    void noteWound(const sim::Unit& victim);
    void ring(const V3& at, float size, float delay = 0);
    void splash(const V3& at, float size, const V3& travel);
    void waterColumn(const V3& at, float reach);
    void steam(const V3& at, float reach);
    void dyeWater(const V3& at, float amount);
    void updateParticles(float seconds);
    void moveSquad(const Squad& squad, const V3& to);
    // Men sent through a gate (they hack at it while it stands). Through a foe's, from outside, the order carries on to the
    // ground behind it (groundBehind) and they hold that until told otherwise.
    void stormGate(sim::PlayerId owner, const std::vector<sim::EntityId>& through, Formation formation, int gate);
    // The ground a gate guards: the courtyard of the town whose walls it is in, or for an inner bailey's gate the open
    // ground behind its doors. False where there is none.
    bool groundBehind(int gate, V3& at) const;
    // Soldiers marching as one formation to `to`: facing `face` there when
    // given, else the way they march.
    // `force`: a force march, deaf to the enemy on the way.
    void tellFormation(sim::PlayerId owner, const std::vector<sim::EntityId>& men, Formation formation);
    // Calls for help as the world made them (World::helpCalls): heard, shown and told for the local side's; the computer's
    // squads sent to its own (aiAnswer), since they always have an order in hand and the world sends only free men.
    void collectCalls();
    void aiAnswer(const sim::HelpCall& call);
    // The computer's choice of formation, by the same rules the player has (world_formation.cpp): a square against horse
    // coming, loose order under arrows it cannot answer, else its kind's own.
    void aiFormation(size_t squad);
    std::vector<Call> calls_;
    void moveGroup(sim::PlayerId owner, std::vector<sim::EntityId> units, Formation formation, const V3& to,
                   const V3* face, const V3& fallbackFacing, bool force = false);
    // Who goes where, and faces which way.
    struct MarchOrder {
        sim::EntityId id;
        V3 target, face;
    };
    // The block (blockLayout) of a big army, men placed by post and then by where they stand.
    static bool blockFits(Formation formation, int count);
    bool standsTooWide(const std::vector<sim::EntityId>& units) const;
    std::vector<MarchOrder> planBlock(const std::vector<sim::EntityId>& units, const V3& to, const V3& facing, int front = 0) const;
    // A formation at `to`: the soldiers nearest the front take the front
    // ranks, keeping their order left to right, so no one crosses another.
    std::vector<MarchOrder> planFormation(const std::vector<sim::EntityId>& units, Formation formation, const V3& to,
                                          const V3& facing, int front = 0) const;
    // An arrangement moved to `to` and turned to face `facing`; within each
    // squad the men take its places by the least walking in all, so none
    // crosses another and no squad takes another's.
    std::vector<MarchOrder> planShape(const Shape& shape, const std::vector<sim::EntityId>& units, const V3& to,
                                      const V3& facing) const;
    // The arrangement of these soldiers: each where he was last sent (the
    // order's men together, moved to where they now stand), else where he is.
    Shape shapeOf(const std::vector<sim::EntityId>& units) const;
    // Places on a rock, a tree or a house moved just clear of it, where a man can stand.
    void keepClear(std::vector<MarchOrder>& plan) const;
    // The selection's march (or these of it): as they stand or in its formation.
    std::vector<MarchOrder> planSelection(const std::vector<sim::EntityId>& units, const V3& to, const V3& facing, int front = 0) const;
    void marchSelection(const std::vector<sim::EntityId>& units, const V3& to, const V3* facing, bool force, int front = 0);
    // The orders out, everyone arriving together at the slowest one's pace
    // (times `pace`), all on the one route from `from` to `to`; and where each
    // was sent remembered (slots_).
    void sendMarch(sim::PlayerId owner, const std::vector<MarchOrder>& plan, const V3& from, const V3& to, const V3& front, float pace,
                   bool force);
    std::vector<FormationSlot> placeFormation(const std::vector<sim::EntityId>& units, Formation formation, const V3& to,
                                              const V3& facing) const;
    V3 centreOf(const std::vector<sim::EntityId>& units) const;
    V3 selectionFacing() const;
    void refreshSelection();
    // The nearest living soldier under the finger (index), or -1.
    int unitAt(const Camera& camera, float x, float y, float pixelScale) const;
    void updateGhost();
    // The computer's orders: the same as moveSquad, but a squad already on
    // its way to about the same place keeps its route (routes cost time).
    void steerSquad(const Squad& squad, const V3& to);
    // Where riders should ride to take a foe locked in melee in the flank or
    // rear; false when there is none (or they are already on its back).
    bool flankPoint(const Squad& riders, V3& out) const;
    void issue(sim::PlayerId player, sim::EntityId unit, sim::Command::Type type, const V3& target);
    // The player just gave these soldiers an order (their markers show a while).
    void markOrdered(const std::vector<sim::EntityId>& units);
    void say(const std::string& notice);
    // The strip of events (events()), called once after each step of the world.
    void observeSquads();
    void observeFight();
    struct FightWatch {
        struct Entry {
            uint32_t army = 0;
            std::string name;
            int alive = 0, kills = 0, rank = 0;
            bool hero = false;
        };
        std::vector<Entry> armies;  // the local side's squads that have fought in it, as each stood when it joined
        uint32_t began = 0, last = 0;  // on the events' clock
        bool weQuit = false, foeQuit = false;
    };
    FightWatch fight_;
    Result fightReport_;
    bool fightTold_ = false;
    void announce(const std::string& text);
    void collectTactics();
    sim::EntityId lordOf(sim::PlayerId owner, bool controllableOnly) const;
    std::vector<Float> floats_;
    std::map<uint64_t, uint32_t> floatSaid_;  // by word, side and place: when it was last floated
    int idleCursor_ = -1;                     // the squad the idle key last chose
    std::vector<uint32_t> aiAnswering_;       // by squad: the tick until which the computer leaves it to its answer
    // Of these, the one nearest p.
    sim::EntityId nearestOf(const std::vector<sim::EntityId>& ids, const V3& p) const;
    // Sieges: the town's gates belong to its defenders.
    void holdGates(sim::PlayerId holder);
    // The squad's orders at a gate: engines and bombers shoot at it, the rest go through it.
    void attackGate(const Squad& squad, int gate);
    void attackGateWith(sim::PlayerId owner, const std::vector<sim::EntityId>& units, Formation formation, int gate);
    // Enemy catapults: the player's gate when they besiege, else the nearest foe in reach.
    void aimEngines(const Squad& s);

    std::shared_ptr<const sim::Continent> continent_;
    std::shared_ptr<const sim::Region> region_;
    FarGrid far_, near_;
    std::unique_ptr<sim::World> ownWorld_;  // the battle's own world, if it has one
    sim::World* world_ = nullptr;           // the world it presents: its own, or one it borrows (borrow)
    std::vector<float> heights_;
    std::vector<Squad> squads_;
    std::vector<int> squadOfUnit_;  // indexed by EntityId - 1
    std::vector<sim::Command> pending_;
    // The orders a gesture gives are gathered by an OrderScope: the outermost one, when it ends, settles what came into
    // pending_ since it began (battle_orders.cpp): flagged to append to the queues if the gesture was Shift's.
    // `append`: the gesture's orders are added to the queues (Shift).
    class OrderScope {
    public:
        OrderScope(Battle& b, bool append) : b_(b), first_(b.pending_.size()), outer_(b.scopeDepth_++ == 0) {
            if (outer_) b.appending_ = append;
        }
        ~OrderScope() {
            --b_.scopeDepth_;
            if (!outer_) return;
            b_.settleOrders(first_);
            b_.appending_ = false;
        }
        OrderScope(const OrderScope&) = delete;
        OrderScope& operator=(const OrderScope&) = delete;

    private:
        Battle& b_;
        size_t first_;
        bool outer_;
    };
    int scopeDepth_ = 0;
    bool appending_ = false;
    // Held orders (setPlanning): a man's command, or a conquest's far march of squads; in the order given.
    struct Planned {
        sim::Command cmd;
        uint32_t man = 0;  // conquest: the man (a unit's id there is made anew when the men are put back)
        uint32_t seq = 0;
        bool far = false;
        std::vector<uint32_t> armies;  // far: the squads marched
        V3 to;
    };
    bool planning_ = false;
    std::vector<Planned> plan_;
    uint32_t planSeq_ = 0;
    sim::EntityId unitOfPlanned(const Planned& p) const;
    void plannedRoutes(std::vector<RouteIn>& routes) const;  // pathLines: the legs held
    void settleOrders(size_t first);
    void queuedRoutes(std::vector<RouteIn>& routes) const;  // pathLines: the legs of the queues
    std::vector<sim::EntityId> selection_;  // ascending, so orders go out in one order everywhere
    std::vector<float> spriteBoxes_;  // setSpriteBoxes
    std::vector<uint8_t> selectedFlag_;     // by EntityId - 1
    Formation selFormation_ = Formation::Line;
    bool selKeep_ = true;  // as they stand (keepShape), not selFormation_
    Inspected inspected_;
    sim::EntityId inspectedUnit_ = 0;  // the unit of the world he is, awake (Conquest), or the unit looked at
    // Conquest (battle_conquest.cpp). The selection there is of men (their ids), so that it holds through a man's
    // boarding and landing; selection_ is what it is of the world's units now.
    sim::Conquest* conquest_ = nullptr;
    std::vector<uint32_t> selMen_;
    std::vector<uint32_t> manOfUnit_;      // by EntityId - 1: the man a unit is (0: none)
    uint32_t selStack_ = 0;
    // Who an order is for, and where it goes as a march of squads (marchOrder): the one place an order of the
    // selection becomes the conquest's rather than the world's.
    bool marchOrder(const V3& to, bool wholly = false);
    static bool beyondReach(const V3& from, const V3& to);  // more than kLocalReachM off
    void marchArmies(std::vector<uint32_t> armies, const V3& to);  // the one place a march of squads is made
    void submitMarch(const std::vector<uint32_t>& armies, int32_t cx, int32_t cy);
    ManPick manPick(const Camera& camera, float x, float y, float pixelScale) const;
    void applySelection(std::vector<sim::EntityId> units);
    void keepSelectedMen();
    int squadOfArmy(uint32_t army) const;
    void inspect(Inspected what);
    struct Memo {  // what a squad keeps between frames, by army: the squads are made afresh from the conquest each frame
        Formation formation = Formation::DoubleLine;
        V3 facing{1, 0, 0};
    };
    std::map<uint32_t, Memo> armyMemo_;
    std::map<uint32_t, int> squadOfArmy_;
    Shape selShape_;       // a recalled group's arrangement, until the selection changes
    // Where each soldier was last sent in a march (by EntityId - 1), the way
    // he was to face there and the front of the whole, and which order it was.
    struct Slot {
        V3 pos, face, front;
        uint32_t order = 0;  // 0: never sent
    };
    std::vector<Slot> slots_;
    uint32_t marches_ = 0;
    std::array<Group, kGroupSlots> groups_;
    Drag drag_;
    bool mouseScheme_ = false;
    OrderMode orderMode_ = OrderMode::Normal;
    bool stoneAimed_ = false;  // aimHover: the pointer is over ground a click would shell
    V3 stoneAim_;
    bool forceKey_ = false, marchKey_ = false;
    std::vector<uint32_t> orderTick_;  // by EntityId - 1: when the player last ordered him
    std::vector<uint32_t> seenGen_;    // by EntityId - 1: which man of the place the world has given (World::generation) this holds
    std::string notice_;
    uint32_t noticeTick_ = 0;
    std::vector<Event> events_;
    uint32_t eventClock_ = 0, ordersGiven_ = 0;
    struct SquadWatch {
        int alive = -1;  // -1: not seen yet
        bool broke = false;
        uint32_t brokeAt = 0;
    };
    std::map<uint32_t, SquadWatch> squadWatch_;  // by army (a Conquest's) or squad
    bool hadArmy_ = false;  // a Conquest: the local side has had an army standing
    uint32_t quitSeen_ = 0;  // a Conquest: the tick of the last stack told of as quitting a field
    bool paused_ = false;
    int speed_ = 1;
    float slow_ = 1;
    V3 zoneCentre_{};
    float zoneRadius_ = 0;
    double accumulator_ = 0;
    double stepMs_ = 0;
    uint32_t fxSeed_ = 1;

    std::vector<V3> prevUnitPos_;
    std::vector<uint32_t> deathTick_;  // UINT32_MAX while alive
    std::vector<uint32_t> attackTick_, hitTick_;
    bool alphaSet_ = false;
    float alphaOver_ = 0;
    std::vector<std::pair<int32_t, int32_t>> readied_;  // melee cooldown and reload before the tick
    std::vector<std::pair<sim::EntityId, V3>> prevProjectilePos_;  // sorted by id
    std::vector<GroundMark> marks_;
    uint64_t marksAdded_ = 0;
    std::vector<uint32_t> dirtyRemains_;
    std::vector<uint8_t> dirtyFlag_;
    std::vector<Particle> particles_;
    struct Recent {
        V3 pos;
        uint32_t tick;
        float weight;
    };
    std::vector<Recent> recent_;
    sim::PlayerId local_ = kPlayer;  // localSide()
    Voice voice_{kPlayer};
    Sfx sfx_{kPlayer};
    struct Smoulder {
        sim::EntityId unit;
        uint32_t since;
    };
    std::vector<Smoulder> smouldering_;  // the burned, still smoking
    std::vector<WaterRing> rings_;
    uint64_t ringsAdded_ = 0;
    std::vector<WaterDye> dye_;
    struct Bleeder {
        sim::EntityId unit;
        uint32_t until;  // the tick his wound stops dripping
        V3 last;         // where he left his last drop
        float gap;       // metres to the next
    };
    std::vector<Bleeder> bleeders_;  // the wounded leaving a trail (battle_water.cpp)
    CapturePoint capture_;
    // The end (battle_end.cpp): the rout rule, when the battle was decided, and the squads that ever broke.
    sim::RoutRule rout_;
    std::vector<sim::HostSquad> hosts_;
    uint32_t decidedTick_ = UINT32_MAX;
    std::vector<uint8_t> everBroke_;
    std::vector<int32_t> startRank_;  // by EntityId - 1: each man's rank when the battle began (finish)
    bool deploy_ = false;
    void judge();
    void pursue();
    void placeCapturePoint(const sim::Continent::City& town, sim::PlayerId keeper);
    std::unique_ptr<BattleMode> mode_;
    std::vector<uint8_t> foreign_;  // by EntityId - 1: an ally's soldiers (setForeign)
    // The computer's memory of each squad it handles (aiFight): where it was
    // at its last look, how many looks it has stood stuck, and how many more
    // it spends marching round to its goal (a foe across a cliff is no foe);
    // a pack of scavengers, whether it was raiding at the last look and the
    // tick before which it rests (aiRaid).
    struct AiMemo {
        V3 last;
        int still = 0, detour = 0;
        int retreat = 0;  // looks left falling back to mend
        int waited = 0;   // looks spent waiting for the army to close up
        int rested = 0;   // looks before it may wait again (a laggard stuck behind a cliff must not stall the army)
        bool raiding = false;
        uint32_t restUntil = 0;
    };
    AiSkill aiSkill_ = AiSkill::Normal;
    bool aiAssault_ = false;      // the computer's host has gone in (runEnemyAi: the bows first, then all together)
    uint32_t aiDuelSince_ = 0;    // when its bows first loosed (0: not yet)
    int aiWing_ = 0;              // the wing of the foe's line it goes in on (-1, 1; 0: not chosen)
    std::vector<AiMemo> aiMemo_;
    std::map<uint32_t, AiMemo> armyAi_;  // the computer's memory of each squad (aiMemo_), by army, kept across the frames' fresh squads
    // The per-soldier presentation state grown for soldiers raised mid-battle.
    void growUnits();
};

template <typename Fn>
void Battle::forEachProjectile(Fn&& fn) const {
    const float a = alpha();
    size_t prev = 0;
    for (const sim::Projectile& p : world_->projectiles()) {
        const V3 now = toV3(p.pos);
        while (prev < prevProjectilePos_.size() && prevProjectilePos_[prev].first < p.id) ++prev;
        const V3 before =
            prev < prevProjectilePos_.size() && prevProjectilePos_[prev].first == p.id ? prevProjectilePos_[prev].second : now;
        fn(p, lerp(before, now, a), toV3(p.vel));
    }
}

}  // namespace illyria::game
