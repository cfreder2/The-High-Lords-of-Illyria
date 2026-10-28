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
};

// What a squad does on the battlefield, for orders, the AI and HUD icons.
enum class Role { Melee, Ranged, Bomber, Caster, Healer, Flier };
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

    explicit Battle(uint64_t seed);

    // Runs as many fixed ticks as `seconds` of wall time covers (times the game speed).
    void advance(double seconds);
    // Fraction of the way from the previous tick to the current one, for interpolation.
    float alpha() const { return static_cast<float>(accumulator_ * sim::kTicksPerSecond); }

    // Input.
    void tap(const Camera& camera, float x, float y, float pixelScale);
    void selectSquad(int index);
    void stopSelected();
    // Next formation for the selected squad; it re-forms where it stands.
    void cycleFormation();
    void togglePause() { paused_ = !paused_; }
    void cycleSpeed() { speed_ = speed_ >= 4 ? 1 : speed_ * 2; }

    // Queries.
    const sim::World& world() const { return *world_; }
    const std::vector<Squad>& squads() const { return squads_; }
    int selectedSquad() const { return selected_; }
    bool paused() const { return paused_; }
    int speed() const { return speed_; }
    // 0 in progress, 1 victory, 2 defeat.
    int outcome() const;
    int aliveCount(const Squad& s) const;
    V3 squadCentre(const Squad& s, bool interpolated = true) const;
    int squadOf(sim::EntityId id) const { return squadOfUnit_[id - 1]; }

    // Ground height anywhere: the battlefield exactly, the rest of the continent coarsely.
    float heightAt(float x, float y) const;
    bool inBattlefield(float x, float y) const { return x >= 0 && y >= 0 && x <= kWorldSize && y <= kWorldSize; }
    const FarGrid& farGrid() const { return far_; }
    const FarGrid& nearGrid() const { return near_; }
    const sim::Region& region() const { return *region_; }
    const sim::Continent& continent() const { return *continent_; }
    // Continent coordinates of this battlefield's (0, 0).
    V3 origin() const { return toV3(region_->origin()); }
    const std::vector<float>& heights() const { return heights_; }

    // Interpolated presentation positions.
    V3 unitPosition(size_t index) const;
    bool unitMoving(size_t index) const;
    uint32_t deathTick(size_t index) const { return deathTick_[index]; }
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
    void spawnSquad(const std::string& name, sim::PlayerId owner, sim::UnitType type, int count, V3 centre,
                    V3 facing);
    void step();
    void runEnemyAi();
    void collectEvents();
    void addMark(GroundMark::Kind kind, V3 pos, float yaw, float size, uint32_t seed);
    void emit(Particle::Kind kind, V3 pos, V3 vel, float life, float size);
    void updateParticles(float seconds);
    void moveSquad(const Squad& squad, const V3& to);
    void issue(sim::PlayerId player, sim::EntityId unit, sim::Command::Type type, const V3& target);

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
