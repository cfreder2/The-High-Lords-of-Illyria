#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "illyria/game/camera.h"
#include "illyria/game/gmath.h"
#include "illyria/sim/world.h"

namespace illyria::game {

struct Squad {
    enum class Kind { Archers, Footmen };
    std::string name;
    sim::PlayerId owner;
    Kind kind;
    std::vector<sim::EntityId> members;
};

// Remains are derived only from simulation events, so every peer
// accumulates the same ones (docs/GAME_DESIGN.md §5.5).
struct StuckArrow {
    V3 pos;
    V3 dir;
    uint32_t tick;
};

struct BloodStain {
    V3 pos;
    float yaw;
    float size;
    uint32_t tick;
    uint32_t seed;
};

// One playable battle: owns the deterministic world, turns player input into
// commands, drives the enemy, and keeps presentation state (interpolation,
// remains) for the renderer.
class Battle {
public:
    static constexpr sim::PlayerId kPlayer = 0;
    static constexpr sim::PlayerId kEnemy = 1;
    static constexpr int32_t kTerrainCells = 256;
    static constexpr float kCellSize = 4.0f;
    static constexpr float kWorldSize = kTerrainCells * kCellSize;
    static constexpr size_t kMaxStuckArrows = 40000;
    static constexpr size_t kMaxBloodStains = 30000;

    explicit Battle(uint64_t seed);

    // Runs as many fixed ticks as `seconds` of wall time covers (times the game speed).
    void advance(double seconds);
    // Fraction of the way from the previous tick to the current one, for interpolation.
    float alpha() const { return static_cast<float>(accumulator_ * sim::kTicksPerSecond); }

    // Input.
    void tap(const Camera& camera, float x, float y, float pixelScale);
    void selectSquad(int index);
    void stopSelected();
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

    float heightAt(float x, float y) const;
    const std::vector<float>& heights() const { return heights_; }

    // Interpolated presentation positions.
    V3 unitPosition(size_t index) const;
    bool unitMoving(size_t index) const;
    uint32_t deathTick(size_t index) const { return deathTick_[index]; }
    template <typename Fn>
    void forEachProjectile(Fn&& fn) const;

    // Remains are stored in fixed-capacity rings: the k-th one ever added
    // lives at index k % capacity.
    const std::vector<StuckArrow>& stuckArrows() const { return stuckArrows_; }
    const std::vector<BloodStain>& bloodStains() const { return blood_; }
    uint64_t stuckArrowsAdded() const { return arrowsAdded_; }
    uint64_t bloodStainsAdded() const { return bloodAdded_; }
    double stepMs() const { return stepMs_; }

private:
    void spawnSquad(const std::string& name, sim::PlayerId owner, Squad::Kind kind, int count, V3 centre,
                    V3 facing);
    void step();
    void runEnemyAi();
    void collectRemains();
    void moveSquad(const Squad& squad, const V3& to);
    void issue(sim::PlayerId player, sim::EntityId unit, sim::Command::Type type, const V3& target);

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

    std::vector<V3> prevUnitPos_;
    std::vector<uint32_t> deathTick_;  // UINT32_MAX while alive
    std::vector<std::pair<sim::EntityId, V3>> prevProjectilePos_;  // sorted by id
    std::vector<StuckArrow> stuckArrows_;
    std::vector<BloodStain> blood_;
    uint64_t arrowsAdded_ = 0, bloodAdded_ = 0;
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
        fn(lerp(before, now, a), toV3(p.vel).normalized());
    }
}

}  // namespace illyria::game
