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
#include "illyria/game/sfx.h"
#include "illyria/game/voice.h"
#include "illyria/sim/campaign.h"
#include "illyria/sim/continent.h"
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
// The picker's tenth entry, after the nine: the selection as it stands
// (Battle::keepShape), its own arrangement kept as it moves.
constexpr int kKeepShapeId = kFormationCount;
constexpr const char* kKeepShapeName = "As they stand";
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
};

struct Squad {
    std::string name;
    sim::PlayerId owner;
    sim::UnitType type;
    std::vector<sim::EntityId> members;
    Formation formation = Formation::DoubleLine;
    mutable V3 facing{1, 0, 0};  // the way it last marched (presentation of orders, not sim state)
    // A garrison holds its town: it fights whoever comes within reach of its
    // post, then goes back to it, and never marches out.
    bool garrison = false;
    V3 post{};
    // Where it was last sent (the computer re-plans a chase only when that moves enough).
    mutable V3 goal{};
    mutable bool hasGoal = false;
    // In Conquest (battle_conquest.cpp) a squad is an army of a stack: its men awake in the world are `members`, those
    // asleep in their stack on the march or in a town are `sleeping` (their ids as Campaign::Man has them), and what its
    // card shows that no unit holds is told by the army (`told`, `arms`, `rare`, `command`, `hired`).
    std::vector<uint32_t> sleeping{};
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
    Mode,      // a match: Horde or capture the flag (mode.h), on a field it fills itself
};

// A battle fought from the campaign: the armies of each side, as they stand.
const sim::Continent::City& siegeTown(const sim::Continent& c);

// What a squad does on the battlefield, for orders, the AI and HUD icons.
// Scouts (the hyena-folk) are let loose to raid (sim Command::Raid,
// world_scavenge.cpp): the computer brings its packs near the foe and lets
// them loose (Battle::aiRaid); the player does with T or the Raid button.
enum class Role { Melee, Ranged, Bomber, Caster, Healer, Flier, Siege, Scout };
Role roleOf(sim::UnitType type);

// Decals derived only from simulation events, so every peer accumulates the
// same ones (docs/GAME_DESIGN.md §5.5).
struct GroundMark {
    enum class Kind : uint8_t { Blood, Crater, Dust };
    Kind kind;
    V3 pos;
    float yaw;
    float size;
    uint32_t tick;
    uint32_t seed;
};

// Purely cosmetic, per client: fire, smoke, sparks, dirt, healing light.
struct Particle {
    enum class Kind : uint8_t { Fire, Smoke, Spark, Dirt, Heal, Flash };
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
    static constexpr size_t kMaxParticles = 4000;

    explicit Battle(uint64_t seed, Scenario scenario = Scenario::Ford);
    // An empty field for a match (Scenario::Mode): the mode raises the armies.
    // `continent` may be one already built (it is the same for every battle).
    // With `farReachM`, only the continent within that many metres of the
    // field is loaded for the horizon (a match's camera never leaves its
    // field, setZone), else all of it.
    Battle(uint64_t seed, const sim::Vec3& centre, std::shared_ptr<const sim::Continent> continent = {},
           float farReachM = 0);
    ~Battle();
    // For a campaign battle: what became of every army.
    Scenario scenario() const { return scenario_; }
    // Who the player is fighting, for the HUD.
    const std::string& foeName() const { return foeName_; }

    // Runs as many fixed ticks as `seconds` of wall time covers (times the game speed).
    void advance(double seconds);
    // Fraction of the way from the previous tick to the current one, for interpolation.
    float alpha() const { return alphaSet_ ? alphaOver_ : static_cast<float>(accumulator_ * sim::kTicksPerSecond); }

    // Conquest (game/src/battle_conquest.cpp): the whole island's war is a battle too, presented by this one Battle:
    // it shows the conquest's world exactly as it shows its own, from the same places. The world must be on a region
    // with this battle's origin (the field is made at the island's: Campaign::islandOrigin), so that the world's
    // places are the battle's. Whoever steps the conquest calls beforeWorldStep() and afterWorldStep() round each
    // step of its world (Campaign::onWorldStep), says how far between ticks it is, and calls presentConquest() once a
    // frame, after the steps, and flushOrders() once before them.
    void borrow(sim::Campaign* conquest);  // nullptr: back to its own world
    bool borrowing() const { return conquest_ != nullptr; }
    sim::Campaign* conquest() const { return conquest_; }
    void beforeWorldStep();
    void afterWorldStep();
    void setAlpha(float a) { alphaSet_ = true, alphaOver_ = a; }
    // The fight in the borrowed world was cleared away (its men are gone, new ones are not those): forget them.
    void forgetMen();
    // Every man as the conquest says he stands this frame (Campaign::visibleSoldiers): the squads of the armies he is
    // of (their men awake in the world are its units, those asleep are sleepers, one picture each in the renderer's
    // one pass), who is selected or looked at, and the selection kept through a man's waking and sleeping (it is of
    // men, not of the world's units, which come and go).
    void presentConquest(const std::vector<sim::Campaign::Soldier>& men);
    // The player's orders for the conquest, out as Orders (Campaign::submit): the commands to men in the world, as a
    // skirmish's step would take them; the marches are submitted as they are given (marchOrder).
    void flushOrders();
    // Every man of a stack of the player's, asleep or awake, selected (the page's, at the start: over the capital).
    void selectStack(uint32_t stack);
    // The stack the selection is of (all its men in one), or 0: the stack panel's, the voyage's, the search's.
    uint32_t selectedStack() const { return selStack_; }
    // The army (squad) the selection is exactly (every man of it and no other), or 0.
    uint32_t selectedArmy() const;

    // Men who are not units of the world presented: the conquest's soldiers asleep in their squads on the march or in a
    // town (their bodies are stepped as a whole, not as men). A man has one record for presentation, Campaign::Soldier,
    // which says where he stands whether he is awake (and then his unit is to hand) or asleep; the sleepers are those of
    // them with no unit, each the same picture as a unit's, in the same pass of the renderer (a dot from far off, the
    // battle's sprite near). x, y are continent metres.
    // What the viewer may see of the others (Campaign::canSee, through the conquest this field presents): a man of
    // `owner` at battle-local x, y that it refuses is not drawn, picked or looked at. A skirmish's field sees all.
    bool sees(uint8_t owner, float x, float y) const;
    const std::vector<sim::Campaign::Soldier>& sleepers() const { return sleepers_; }
    // Whether a man asleep (by his id) is selected, or looked at.
    bool isSelectedMan(uint32_t man) const { return std::binary_search(selSleeping_.begin(), selSleeping_.end(), man); }
    bool isInspectedMan(uint32_t man) const { return inspected_.kind == Inspected::Kind::Man && inspected_.id == man; }
    // A man asleep (by his id) where he stands, battle-local on the ground (the origin if he is not asleep).
    V3 sleeperPositionOf(uint32_t man) const;
    // The man nearest the finger on the screen within `tapPx` pixels, at any height and any distance (a dot is as
    // much a man as a sprite): one of the world's (unit: his id) or a sleeper (index into sleepers()).
    struct ManPick {
        enum class Kind : uint8_t { None, Unit, Sleeper } kind = Kind::None;
        uint32_t id = 0;
        size_t index = 0;
        float px = 1e30f;  // how far off the finger he is, in pixels
    };
    ManPick manAt(const Camera& camera, float x, float y, float tapPx) const;
    // The standing gate under the finger, or -1.
    int gateAt(const Camera& camera, float x, float y, float pixelScale) const;
    // What the player looks at and cannot order: view-only, grey. One man not his (a unit of the world, or a sleeper by
    // his man id) or a gate; a tap on any of them with nothing selected looks at it, and any other tap lets it go.
    struct Inspected {
        enum class Kind : uint8_t { None, Unit, Man, Gate } kind = Kind::None;  // Man: in Conquest, by his id (Campaign::Man)
        uint32_t id = 0;
    };
    const Inspected& inspected() const { return inspected_; }
    bool isInspected(sim::EntityId id) const { return inspectedUnit_ == id && id != 0; }
    // What the card of the one looked at shows (the page prints it).
    struct Glance {
        bool any = false, gate = false, awake = false, broken = false;
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
    void orderTap(const Camera& camera, float x, float y, float pixelScale);
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
    struct Group {
        std::vector<sim::EntityId> units;
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
    const Group& group(int slot) const { return groups_[static_cast<size_t>(slot)]; }

    // Orders to the selection (for tests and hooks as well as the input).
    // At a gate, as tapping it.
    void orderAtGate(int gate);
    // March there, as tapping the ground, facing as they do now: as they stand
    // (keepShape), their own arrangement moved there; in a chosen formation,
    // re-formed there.
    void orderMove(const V3& to);
    // March there and face that way on arrival (pressed there and dragged
    // the way to face: pressEnd), the whole arrangement turned to it.
    void orderMoveFacing(const V3& to, const V3& facing);
    void stopSelected();
    // Attacks (game/src/battle_orders.cpp). Everyone selected after one
    // soldier, of either side (a force attack on a friend): missile troops
    // keep to a bowshot and shoot him, the rest run him down. When he falls
    // each decides for himself: another foe, or back to his squad.
    void orderAttackUnit(sim::EntityId target);
    // Conquest: at a man asleep (his stack on the march, or in a town); the squads march to where he stands.
    void orderAttackSleeper(size_t sleeper);
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
        int sleeper = -1;  // Conquest: a man asleep (index into sleepers()), who is no unit (index -1)
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
    enum class Intent : uint8_t { None, Select, Attack, AttackObject, AttackGround, Cannot, Move, ForceMarch };
    Intent intentAt(const Camera& camera, float x, float y, float pixelScale) const;
    // The last order refused or explained ("Only engines ... can harm walls"), and when (tick).
    const std::string& notice() const { return notice_; }
    uint32_t noticeTick() const { return noticeTick_; }
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
    // How strongly a soldier's order markers show (0: not at all).
    float markOpacity(size_t index) const;
    // Tests and hooks: a gate battered down at once; every archer of ours
    // looses at a spot on the ground.
    void breakGate(int gate);
    void testShootAt(const V3& at);
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
    // How many a squad's card counts: its men that are alive (awake or asleep), and the squad's full size.
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
    // Selected men asleep (their ids as Campaign::Man has them) and every man selected, awake or asleep.
    const std::vector<uint32_t>& selectedSleepers() const { return selSleeping_; }
    size_t selectionSize() const { return selectedAlive().size() + selSleeping_.size(); }
    // The types of those selected, one name for them all ("Mixed" when they are not).
    const char* selectionLabel() const;
    bool selectionBatters() const;
    // 0 none of the squad selected, 1 some, 2 all its living soldiers.
    int squadSelection(const Squad& s) const;
    int groupSelection(int slot) const;
    bool paused() const { return paused_; }
    int speed() const { return speed_; }
    // 0 in progress, 1 victory, 2 defeat.
    int outcome() const;
    // A town's capture point (battle_capture.h); inactive in the field.
    const CapturePoint& capturePoint() const { return capture_; }
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
    std::string foeName_ = "Red Hand";
    void setUpFord();
    void setUpSiege();
    void build(uint64_t seed, const sim::Vec3& centre, int homeSide = -1, float farReachM = 0);
    void finish();
    void spawnSquad(const std::string& name, sim::PlayerId owner, sim::UnitType type, int count, V3 centre,
                    V3 facing);
    void step();
    void runEnemyAi();
    void collectEvents();
    void applyImpacts(const std::vector<sim::ImpactEvent>& events, const sim::World& source, V3 shift, bool own);
    // Each tick's deaths, seen and heard (battle_deaths.cpp): blood rain, smoke, sound effects.
    void presentDeaths();
    void addMark(GroundMark::Kind kind, V3 pos, float yaw, float size, uint32_t seed);
    void emit(Particle::Kind kind, V3 pos, V3 vel, float life, float size);
    void updateParticles(float seconds);
    void moveSquad(const Squad& squad, const V3& to);
    // Soldiers marching as one formation to `to`: facing `face` there when
    // given, else the way they march.
    // `force`: a force march, deaf to the enemy on the way.
    void moveGroup(sim::PlayerId owner, std::vector<sim::EntityId> units, Formation formation, const V3& to,
                   const V3* face, const V3& fallbackFacing, bool force = false);
    // Who goes where, and faces which way.
    struct MarchOrder {
        sim::EntityId id;
        V3 target, face;
    };
    // A formation at `to`: the soldiers nearest the front take the front
    // ranks, keeping their order left to right, so no one crosses another.
    std::vector<MarchOrder> planFormation(const std::vector<sim::EntityId>& units, Formation formation, const V3& to,
                                          const V3& facing) const;
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
    std::vector<MarchOrder> planSelection(const std::vector<sim::EntityId>& units, const V3& to, const V3& facing) const;
    void marchSelection(const std::vector<sim::EntityId>& units, const V3& to, const V3* facing, bool force);
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
    // Of these, the one nearest p.
    sim::EntityId nearestOf(const std::vector<sim::EntityId>& ids, const V3& p) const;
    // Sieges: the town's gates belong to its defenders.
    void holdGates(sim::PlayerId holder);
    // The squad's orders at a gate: engines and bombers shoot at it, the rest go through it.
    void attackGate(const Squad& squad, int gate);
    void attackGateWith(sim::PlayerId owner, const std::vector<sim::EntityId>& units, Formation formation, int gate);
    // Enemy catapults: the player's gate when they besiege, else the nearest foe in reach.
    void aimEngines(const Squad& s);
    // A garrison's archers: those not at an embrasure go up to an empty one.
    void manTheWalls(const Squad& s);

    std::shared_ptr<const sim::Continent> continent_;
    std::shared_ptr<const sim::Region> region_;
    FarGrid far_, near_;
    std::unique_ptr<sim::World> ownWorld_;  // the battle's own world, if it has one
    sim::World* world_ = nullptr;           // the world it presents: its own, or one it borrows (borrow)
    std::vector<float> heights_;
    std::vector<Squad> squads_;
    std::vector<int> squadOfUnit_;  // indexed by EntityId - 1
    std::vector<sim::Command> pending_;
    std::vector<sim::EntityId> selection_;  // ascending, so orders go out in one order everywhere
    std::vector<float> spriteBoxes_;  // setSpriteBoxes
    std::vector<uint8_t> selectedFlag_;     // by EntityId - 1
    Formation selFormation_ = Formation::Line;
    bool selKeep_ = true;  // as they stand (keepShape), not selFormation_
    Inspected inspected_;
    sim::EntityId inspectedUnit_ = 0;  // the unit of the world he is, awake (Conquest), or the unit looked at
    // Conquest (battle_conquest.cpp). The selection there is of men (their ids), so that it holds through a man's
    // waking and sleeping; selection_ and selSleeping_ are what it is of the world's units and of the sleepers now.
    sim::Campaign* conquest_ = nullptr;
    std::vector<uint32_t> selMen_, selSleeping_;
    std::vector<uint32_t> manOfUnit_;      // by EntityId - 1: the man a unit is (0: none)
    uint32_t selStack_ = 0;
    // Who an order is for, and where it goes as a march of squads (marchOrder): the one place an order of the
    // selection becomes the conquest's rather than the world's.
    bool marchOrder(const V3& to, bool wholly = false);
    void haltOrder();
    static bool beyondReach(const V3& from, const V3& to);  // more than kLocalReachM off
    void marchArmies(std::vector<uint32_t> armies, const V3& to);  // the one place a march of squads is made
    void submitMarch(const std::vector<uint32_t>& armies, bool halt, int32_t cx, int32_t cy);
    ManPick manPick(const Camera& camera, float x, float y, float pixelScale) const;
    void selectMen(std::vector<sim::EntityId> units, std::vector<uint32_t> sleepers);
    void applySelection(std::vector<sim::EntityId> units);
    void keepSelectedMen();
    static constexpr size_t kNoSleeper = static_cast<size_t>(-1);
    size_t sleeperIndex(uint32_t man) const;  // into sleepers_, or kNoSleeper
    V3 sleeperPosition(const sim::Campaign::Soldier& m) const;  // battle-local, on the ground
    int squadOfArmy(uint32_t army) const;
    void inspect(Inspected what);
    struct Memo {  // what a squad keeps between frames, by army: the squads are made afresh from the conquest each frame
        Formation formation = Formation::DoubleLine;
        V3 facing{1, 0, 0};
    };
    std::map<uint32_t, Memo> armyMemo_;
    std::vector<std::pair<uint32_t, size_t>> sleeperIx_;  // (man, index into sleepers_), by man
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
    OrderMode orderMode_ = OrderMode::Normal;
    bool forceKey_ = false, marchKey_ = false;
    std::vector<uint32_t> orderTick_;  // by EntityId - 1: when the player last ordered him
    std::vector<uint32_t> seenGen_;    // by EntityId - 1: which man of the place the world has given (World::generation) this holds
    std::string notice_;
    uint32_t noticeTick_ = 0;
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
    std::vector<sim::Campaign::Soldier> sleepers_;
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
    CapturePoint capture_;
    void placeCapturePoint(const sim::Continent::City& town);
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
        bool raiding = false;
        uint32_t restUntil = 0;
    };
    std::vector<AiMemo> aiMemo_;
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
