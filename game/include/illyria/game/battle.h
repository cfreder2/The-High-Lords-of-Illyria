#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "illyria/game/camera.h"
#include "illyria/game/gmath.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::game {

// How a squad arranges itself, as in Myth: each soldier keeps his place as
// the squad marches, and the whole squad moves at its slowest member's pace.
enum class Formation : uint8_t { Line, Box, Wedge, Loose, Column };
const char* formationName(Formation f);
// Slots relative to the squad's centre: x to the right, y forward.
std::vector<V3> formationSlots(Formation f, int count, float spacing);

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
    // For a campaign battle: what became of every army.
    std::vector<ArmyResult> results() const;
    Scenario scenario() const { return scenario_; }
    // Who the player is fighting, for the HUD.
    const std::string& foeName() const { return foeName_; }

    // Runs as many fixed ticks as `seconds` of wall time covers (times the game speed).
    void advance(double seconds);
    // Fraction of the way from the previous tick to the current one, for interpolation.
    float alpha() const { return static_cast<float>(accumulator_ * sim::kTicksPerSecond); }

    // Input.
    void tap(const Camera& camera, float x, float y, float pixelScale);
    void selectSquad(int index);
    // The selected squad's orders at a gate (as tapping it), for tests and hooks.
    void orderAtGate(int gate);
    // The selected squad marched to a spot (as tapping the ground), for tests and hooks.
    void orderMove(const V3& to);
    void stopSelected();
    // Every one of the player's squads at the nearest enemy: the foot charge,
    // missile troops close to a long bowshot.
    void chargeAll();
    // Next formation for the selected squad; it re-forms where it stands.
    void cycleFormation();
    void togglePause() { paused_ = !paused_; }
    void cycleSpeed() { speed_ = speed_ >= 4 ? 1 : speed_ * 2; }

    // Queries.
    const sim::World& world() const { return *world_; }
    const std::vector<Squad>& squads() const { return squads_; }
    // Test hook: every soldier of a squad raised to this rank (shield pips on the health bars).
    void promoteSquad(size_t squad, int rank) {
        if (squad < squads_.size())
            for (sim::EntityId id : squads_[squad].members) world_->setVeterancy(id, rank, 0);
    }
    int selectedSquad() const { return selected_; }
    bool paused() const { return paused_; }
    int speed() const { return speed_; }
    // 0 in progress, 1 victory, 2 defeat.
    int outcome() const;
    int aliveCount(const Squad& s) const;
    Standing standing(const Squad& s) const;
    V3 squadCentre(const Squad& s, bool interpolated = true) const;
    int squadOf(sim::EntityId id) const { return squadOfUnit_[id - 1]; }

    // Ground height anywhere: the battlefield exactly, the rest of the continent coarsely.
    float heightAt(float x, float y) const;
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

private:
    Scenario scenario_ = Scenario::Ford;
    std::string foeName_ = "Red Hand";
    void setUpFord();
    void setUpSiege();
    void setUpCampaign(const BattlePlan& plan);
    void build(uint64_t seed, const sim::Vec3& centre);
    void finish();
    void spawnSquad(const std::string& name, sim::PlayerId owner, sim::UnitType type, int count, V3 centre,
                    V3 facing);
    void step();
    void runEnemyAi();
    void collectEvents();
    void addMark(GroundMark::Kind kind, V3 pos, float yaw, float size, uint32_t seed);
    void emit(Particle::Kind kind, V3 pos, V3 vel, float life, float size);
    void updateParticles(float seconds);
    void moveSquad(const Squad& squad, const V3& to);
    // The computer's orders: the same as moveSquad, but a squad already on
    // its way to about the same place keeps its route (routes cost time).
    void steerSquad(const Squad& squad, const V3& to);
    // Where riders should ride to take a foe locked in melee in the flank or
    // rear; false when there is none (or they are already on its back).
    bool flankPoint(const Squad& riders, V3& out) const;
    void issue(sim::PlayerId player, sim::EntityId unit, sim::Command::Type type, const V3& target);
    // Sieges: the town's gates belong to its defenders.
    void holdGates(sim::PlayerId holder);
    // The standing gate under the finger, or -1.
    int gateAt(const Camera& camera, float x, float y, float pixelScale) const;
    // The squad's orders at a gate: engines and bombers shoot at it, the rest go through it.
    void attackGate(const Squad& squad, int gate);
    // Enemy catapults: the player's gate when they besiege, else the nearest foe in reach.
    void aimEngines(const Squad& s);

    std::shared_ptr<const sim::Continent> continent_;
    std::shared_ptr<const sim::Region> region_;
    FarGrid far_, near_;
    std::unique_ptr<sim::World> world_;
    std::vector<float> heights_;
    std::vector<Squad> squads_;
    std::vector<int> squadOfUnit_;  // indexed by EntityId - 1
    std::vector<sim::Command> pending_;
    int selected_ = -1;
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
