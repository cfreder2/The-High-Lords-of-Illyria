#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "illyria/game/battle_capture.h"
#include "illyria/game/camera.h"
#include "illyria/game/gmath.h"
#include "illyria/game/sfx.h"
#include "illyria/game/voice.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::game {

class BattleMode;  // mode.h

// How a squad arranges itself, as in Myth: each soldier keeps his place as
// the squad marches, and the whole squad moves at its slowest member's pace
// (slower still in the tight formations: see formationPace).
//   Line        two ranks: the shield wall
//   ShortLine   three ranks, narrower and deeper
//   Staggered   a loose checkerboard of two ranks: missile troops, out of
//               reach of one stray blast
//   Box, Wedge, Column, Loose
//   Encircle    a wide crescent around the point, everyone facing outward
//   Vee         a V, the point at the back and the arms reaching forward:
//               whatever comes at the point is taken in both flanks
enum class Formation : uint8_t { Line, Box, Wedge, Loose, Column, Staggered, ShortLine, Encircle, Vee };
constexpr int kFormationCount = 9;
// The picker's order, which is also the desktop keys 1-9 (0 is Scatter).
constexpr Formation kFormationOrder[kFormationCount] = {Formation::Line,  Formation::ShortLine, Formation::Staggered,
                                                        Formation::Box,   Formation::Wedge,     Formation::Column,
                                                        Formation::Loose, Formation::Encircle,  Formation::Vee};
const char* formationName(Formation f);
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

struct Squad {
    std::string name;
    sim::PlayerId owner;
    sim::UnitType type;
    std::vector<sim::EntityId> members;
    Formation formation = Formation::Box;
    mutable V3 facing{1, 0, 0};  // the way it last marched (presentation of orders, not sim state)
    // A garrison holds its town: it fights whoever comes within reach of its
    // post, then goes back to it, and never marches out.
    bool garrison = false;
    V3 post{};
    uint32_t armyId = 0;  // the campaign army it is, when fought from the campaign
    // Where it was last sent (the computer re-plans a chase only when that moves enough).
    mutable V3 goal{};
    mutable bool hasGoal = false;
};

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

// Which battle to fight.
enum class Scenario {
    Ford,      // Oakhaven Ford: the Red Hand comes over the river
    Siege,     // a neutral town near Oakhaven, held by its own small garrison
    Campaign,  // where two campaign stacks met (see BattlePlan)
    Mode,      // a match: Horde or capture the flag (mode.h), on a field it fills itself
};

// A battle fought from the campaign: the armies of each side, as they stand.
struct BattleArmy {
    uint32_t id = 0;
    sim::UnitType type = sim::UnitType::LightInfantry;
    std::string name;
    int soldiers = 1;
    int rank = 0;  // heroes: level - 1
    int blessings = 0;
};
struct BattlePlan {
    sim::Vec3 centre;      // continent coordinates: the town, or the field
    int city = -1;         // the town fought over (continent index), or -1
    int homeSide = -1;     // whose land it is: 0 the player's, 1 the foe's, -1 nobody's (the home-land edge)
    std::vector<BattleArmy> player, foe;
    std::string foeName;
};
// How each campaign army came out of the battle.
struct ArmyResult {
    uint32_t id = 0;
    int soldiers = 0, kills = 0, rank = 0;
};
// The town besieged in Scenario::Siege: the neutral town nearest Oakhaven Ford.
const sim::Continent::City& siegeTown(const sim::Continent& c);

// What a squad does on the battlefield, for orders, the AI and HUD icons.
enum class Role { Melee, Ranged, Bomber, Caster, Healer, Flier, Siege };
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
// the battlefield and for the camera: the whole continent every 64 m, and a
// finer ring every 16 m around the battlefield.
struct FarGrid {
    static constexpr float kCell = 32.0f;
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
    static constexpr size_t kMaxParticles = 4000;

    explicit Battle(uint64_t seed, Scenario scenario = Scenario::Ford);
    Battle(uint64_t seed, const BattlePlan& plan);
    // An empty field for a match (Scenario::Mode): the mode raises the armies.
    // `continent` may be one already built (it is the same for every battle).
    Battle(uint64_t seed, const sim::Vec3& centre, std::shared_ptr<const sim::Continent> continent = {});
    ~Battle();
    // For a campaign battle: what became of every army.
    std::vector<ArmyResult> results() const;
    Scenario scenario() const { return scenario_; }
    // Who the player is fighting, for the HUD.
    const std::string& foeName() const { return foeName_; }

    // Runs as many fixed ticks as `seconds` of wall time covers (times the game speed).
    void advance(double seconds);
    // Fraction of the way from the previous tick to the current one, for interpolation.
    float alpha() const { return static_cast<float>(accumulator_ * sim::kTicksPerSecond); }

    // Input. Selection is as in Myth (game/src/battle_command.cpp): tapping
    // one of ours selects him alone, tapping him again everyone of his type
    // in view; `additive` (shift-click) adds or removes him instead.
    void tap(const Camera& camera, float x, float y, float pixelScale, bool additive = false);
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
    struct Group {
        std::vector<sim::EntityId> units;
        Formation formation = Formation::Line;
    };
    static constexpr int kGroupSlots = 10;
    void assignGroup(int slot);
    // The first free slot, or -1 when all are taken.
    int saveGroup();
    bool recallGroup(int slot);
    void clearGroup(int slot);
    const Group& group(int slot) const { return groups_[static_cast<size_t>(slot)]; }

    // Orders to the selection (for tests and hooks as well as the input).
    // At a gate, as tapping it.
    void orderAtGate(int gate);
    // March there, as tapping the ground: the formation faces the way it marched.
    void orderMove(const V3& to);
    // March there and face that way on arrival, the formation laid out across
    // it (Myth's gesture rotation).
    void orderMoveFacing(const V3& to, const V3& facing);
    void stopSelected();
    // Attacks (game/src/battle_orders.cpp). Everyone selected after one
    // soldier, of either side (a force attack on a friend): missile troops
    // keep to a bowshot and shoot him, the rest run him down. When he falls
    // each decides for himself: another foe, or back to his squad.
    void orderAttackUnit(sim::EntityId target);
    // Missile troops shoot at the spot; the rest march there, fighting
    // whoever they meet.
    void orderAttackGround(const V3& at);
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
    // At a gate, a wall or a house or tree: who can, attacks it (missile
    // troops shoot a gate and the rest go to hack it; only engines,
    // grenadiers and wizards harm anything at a wall, house or tree).
    void orderAttackObject(const Pick& pick);
    // How the next tap is read: as usual, as an attack on whatever is tapped
    // (the phone's attack button, Ctrl-click or A on the desktop), or as a
    // force march (a long press on the attack button, Alt-click or M).
    // Attack and force-march modes last for one order.
    enum class OrderMode : uint8_t { Normal, Attack, ForceMarch };
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
    // The selection's formation; it re-forms where it stands.
    void setFormation(Formation f);
    void cycleFormation();

    // A press held on the battlefield (Gestures' hold): drawing a selection
    // box, or the direction line of a move with facing. `box` forces a box
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
        V3 anchor, facing{1, 0, 0};             // the line: from the destination, the way to face
        bool aimed = false;                     // dragged far enough to set a facing
        V3 tip;                                 // where the finger is, on the ground
        std::vector<FormationSlot> ghost;       // the formation there, in battle coordinates
    };
    const Drag& drag() const { return drag_; }
    void togglePause() { paused_ = !paused_; }
    void cycleSpeed() { speed_ = speed_ >= 4 ? 1 : speed_ * 2; }

    // Matches (mode.h, game/src/battle_modes.cpp). The mode runs every tick:
    // it gathers each seat's orders, plays the computer's seats and keeps the
    // score; while one runs, the built-in enemy stands down.
    void setMode(std::unique_ptr<BattleMode> mode);
    BattleMode* mode() const { return mode_.get(); }
    // A squad raised during the battle (a wave, reinforcements); its index.
    int addSquad(const std::string& name, sim::PlayerId owner, sim::UnitType type, int count, V3 centre, V3 facing,
                 int rank = 0);
    // A squad of the local player's side seated by someone else (an ally):
    // seen and fought beside, but not his to order.
    void markForeign(size_t squad);
    bool controllable(const sim::Unit& u) const {
        return u.owner == kPlayer && !(u.id <= foreign_.size() && foreign_[u.id - 1]);
    }
    // The computer's orders to a squad. Steer: march there with initiative
    // (a squad already on its way to about the same place keeps its route).
    // ForceMarch: deaf to the enemy. Attack: every soldier after one foe.
    void aiSteer(size_t squad, const V3& to);
    void aiForceMarch(size_t squad, const V3& to);
    void aiAttack(size_t squad, sim::EntityId target);
    void aiHalt(size_t squad);
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
    bool inBattlefield(float x, float y) const { return x >= 0 && y >= 0 && x <= kWorldSize && y <= kWorldSize; }
    const FarGrid& farGrid() const { return far_; }
    const FarGrid& nearGrid() const { return near_; }
    const sim::Region& region() const { return *region_; }
    const sim::Continent& continent() const { return *continent_; }
    std::shared_ptr<const sim::Continent> continentShared() const { return continent_; }
    // Continent coordinates of this battlefield's (0, 0).
    V3 origin() const { return toV3(region_->origin()); }
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
    void setUpCampaign(const BattlePlan& plan);
    void build(uint64_t seed, const sim::Vec3& centre, int homeSide = -1);
    void finish();
    void spawnSquad(const std::string& name, sim::PlayerId owner, sim::UnitType type, int count, V3 centre,
                    V3 facing);
    void step();
    void runEnemyAi();
    void collectEvents();
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
    // The standing gate under the finger, or -1.
    int gateAt(const Camera& camera, float x, float y, float pixelScale) const;
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
    std::unique_ptr<sim::World> world_;
    std::vector<float> heights_;
    std::vector<Squad> squads_;
    std::vector<int> squadOfUnit_;  // indexed by EntityId - 1
    std::vector<sim::Command> pending_;
    std::vector<sim::EntityId> selection_;  // ascending, so orders go out in one order everywhere
    std::vector<uint8_t> selectedFlag_;     // by EntityId - 1
    Formation selFormation_ = Formation::Line;
    std::array<Group, kGroupSlots> groups_;
    Drag drag_;
    OrderMode orderMode_ = OrderMode::Normal;
    bool forceKey_ = false, marchKey_ = false;
    std::vector<uint32_t> orderTick_;  // by EntityId - 1: when the player last ordered him
    std::string notice_;
    uint32_t noticeTick_ = 0;
    bool paused_ = false;
    int speed_ = 1;
    double accumulator_ = 0;
    double stepMs_ = 0;
    uint32_t fxSeed_ = 1;

    std::vector<V3> prevUnitPos_;
    std::vector<uint32_t> deathTick_;  // UINT32_MAX while alive
    std::vector<uint32_t> attackTick_, hitTick_;
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
    std::vector<uint8_t> foreign_;  // by EntityId - 1: an ally's soldiers (markForeign)
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
