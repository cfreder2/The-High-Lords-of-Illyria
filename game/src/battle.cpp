#include "illyria/game/battle.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>

#include "illyria/sim/scenario.h"
#include "illyria/sim/terrain.h"

namespace illyria::game {

namespace {

constexpr double kTickSeconds = 1.0 / sim::kTicksPerSecond;
constexpr int kMaxTicksPerFrame = 8;
constexpr int kAiIntervalTicks = sim::kTicksPerSecond * 2;
constexpr float kFormationSpacing = 1.8f;
constexpr float kTapRadiusPx = 30.0f;
constexpr float kSquadTapRadiusPx = 48.0f;

// Small deterministic hash for remains variation.
uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

}  // namespace

Battle::Battle(uint64_t seed) {
    sim::WorldConfig cfg;
    cfg.seed = seed;
    cfg.wind = {sim::Fixed::fromRatio(1, 2), -sim::Fixed::fromRatio(1, 2), sim::Fixed::zero()};
    auto terrain = std::make_shared<const sim::Terrain>(sim::Terrain::generate(
        seed, kTerrainCells, sim::Fixed::fromInt(static_cast<int64_t>(kCellSize)), sim::Fixed::fromInt(48)));
    heights_.reserve(terrain->samples().size());
    for (sim::Fixed h : terrain->samples()) heights_.push_back(static_cast<float>(h.toDouble()));
    cfg.terrain = std::move(terrain);
    world_ = std::make_unique<sim::World>(cfg);

    const V3 east{1, 0, 0}, west{-1, 0, 0};
    const float cx = kWorldSize / 2, cy = kWorldSize / 2;
    // Oakhaven holds the western ridge.
    spawnSquad("1st Oakhaven Bowmen", kPlayer, Squad::Kind::Archers, 12, {cx - 70, cy - 30, 0}, east);
    spawnSquad("2nd Oakhaven Bowmen", kPlayer, Squad::Kind::Archers, 12, {cx - 70, cy + 30, 0}, east);
    spawnSquad("The Grey Watch", kPlayer, Squad::Kind::Footmen, 16, {cx - 45, cy, 0}, east);
    spawnSquad("Oakhaven Shieldmen", kPlayer, Squad::Kind::Footmen, 16, {cx - 45, cy - 55, 0}, east);
    spawnSquad("Ferrymen of the Illyr", kPlayer, Squad::Kind::Footmen, 16, {cx - 45, cy + 55, 0}, east);
    // The Red Hand comes over the eastern hills.
    for (int i = 0; i < 5; ++i) {
        spawnSquad("Red Hand Warband " + std::to_string(i + 1), kEnemy, Squad::Kind::Footmen, 16,
                   {cx + 150 + (i % 2) * 25.0f, cy - 90 + i * 45.0f, 0}, west);
    }
    spawnSquad("Red Hand Slingers", kEnemy, Squad::Kind::Archers, 12, {cx + 200, cy - 40, 0}, west);
    spawnSquad("Red Hand Longbows", kEnemy, Squad::Kind::Archers, 12, {cx + 200, cy + 40, 0}, west);

    const size_t n = world_->units().size();
    deathTick_.assign(n, UINT32_MAX);
    prevUnitPos_.resize(n);
    for (size_t i = 0; i < n; ++i) prevUnitPos_[i] = toV3(world_->units()[i].pos);
}

void Battle::spawnSquad(const std::string& name, sim::PlayerId owner, Squad::Kind kind, int count, V3 centre,
                        V3 facing) {
    Squad s{name, owner, kind, {}};
    const sim::UnitStats stats = kind == Squad::Kind::Archers ? sim::scenarios::archer() : sim::scenarios::footman();
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(count * 2.5))));
    const V3 right{-facing.y, facing.x, 0};
    for (int i = 0; i < count; ++i) {
        const float col = static_cast<float>(i % cols) - (cols - 1) / 2.0f;
        const float row = static_cast<float>(i / cols);
        const V3 p = centre + right * (col * kFormationSpacing) - facing * (row * kFormationSpacing);
        const sim::EntityId id = world_->spawnUnit(owner, toSim(p), toSim(facing), stats);
        s.members.push_back(id);
        if (squadOfUnit_.size() < id) squadOfUnit_.resize(id, -1);
        squadOfUnit_[id - 1] = static_cast<int>(squads_.size());
    }
    squads_.push_back(std::move(s));
}

float Battle::heightAt(float x, float y) const {
    const int n = kTerrainCells;
    const float fx = std::clamp(x / kCellSize, 0.0f, static_cast<float>(n));
    const float fy = std::clamp(y / kCellSize, 0.0f, static_cast<float>(n));
    const int ix = std::min(static_cast<int>(fx), n - 1), iy = std::min(static_cast<int>(fy), n - 1);
    const float tx = fx - ix, ty = fy - iy;
    const auto h = [&](int i, int j) { return heights_[static_cast<size_t>(j) * (n + 1) + i]; };
    const float top = h(ix, iy) + (h(ix + 1, iy) - h(ix, iy)) * tx;
    const float bottom = h(ix, iy + 1) + (h(ix + 1, iy + 1) - h(ix, iy + 1)) * tx;
    return top + (bottom - top) * ty;
}

void Battle::advance(double seconds) {
    if (paused_) return;
    accumulator_ += std::min(seconds, 0.25) * speed_;
    int ticks = 0;
    while (accumulator_ >= kTickSeconds && ticks < kMaxTicksPerFrame) {
        step();
        accumulator_ -= kTickSeconds;
        ++ticks;
    }
    // Drop time we could not catch up on rather than spiralling.
    if (accumulator_ >= kTickSeconds) accumulator_ = 0;
}

void Battle::step() {
    const auto& units = world_->units();
    for (size_t i = 0; i < units.size(); ++i) prevUnitPos_[i] = toV3(units[i].pos);
    prevProjectilePos_.clear();
    for (const sim::Projectile& p : world_->projectiles()) prevProjectilePos_.push_back({p.id, toV3(p.pos)});

    if (world_->tick() % kAiIntervalTicks == 0) runEnemyAi();

    const auto start = std::chrono::steady_clock::now();
    world_->step(pending_);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stepMs_ = stepMs_ * 0.95 + ms * 0.05;
    pending_.clear();
    collectRemains();
}

void Battle::collectRemains() {
    const uint32_t tick = world_->tick();
    for (const sim::ImpactEvent& e : world_->impacts()) {
        const V3 pos = toV3(e.pos);
        V3 dir = toV3(e.dir).normalized();
        const uint32_t seed = mix(e.projectile ? e.projectile : e.hitUnit, tick);
        if (e.hitUnit == sim::kNoEntity) {
            StuckArrow a{pos, dir, tick};
            // Prototype cap; the full game consolidates old remains instead (§5.5).
            if (stuckArrows_.size() < kMaxStuckArrows) {
                stuckArrows_.push_back(a);
            } else {
                stuckArrows_[arrowsAdded_ % kMaxStuckArrows] = a;
            }
            ++arrowsAdded_;
            continue;
        }
        const size_t index = e.hitUnit - 1;
        const V3 body = toV3(world_->units()[index].pos);
        if (e.killed) deathTick_[index] = tick;
        // Spatter thrown in the direction of the blow, plus a pool for a kill.
        const int splats = e.killed ? 3 : 1;
        for (int i = 0; i < splats; ++i) {
            const uint32_t s = mix(seed, static_cast<uint32_t>(i));
            const float throwDist = 0.3f + static_cast<float>(s % 100) / 100.0f * (e.killed ? 1.6f : 1.0f);
            const float side = (static_cast<float>((s >> 8) % 100) / 100.0f - 0.5f) * 0.8f;
            V3 p = body + V3{dir.x, dir.y, 0} * throwDist + V3{-dir.y, dir.x, 0} * side;
            p.z = heightAt(p.x, p.y);
            const float size = (e.killed && i == 0) ? 1.3f : 0.45f + static_cast<float>((s >> 16) % 100) / 200.0f;
            BloodStain b{p, static_cast<float>(s % 628) / 100.0f, size, tick, s};
            if (blood_.size() < kMaxBloodStains) {
                blood_.push_back(b);
            } else {
                blood_[bloodAdded_ % kMaxBloodStains] = b;
            }
            ++bloodAdded_;
        }
    }
}

void Battle::issue(sim::PlayerId player, sim::EntityId unit, sim::Command::Type type, const V3& target) {
    pending_.push_back({player, unit, type, toSim(target)});
}

int Battle::aliveCount(const Squad& s) const {
    int n = 0;
    for (sim::EntityId id : s.members) n += world_->units()[id - 1].alive();
    return n;
}

V3 Battle::squadCentre(const Squad& s, bool interpolated) const {
    const auto pos = [&](size_t i) { return interpolated ? unitPosition(i) : toV3(world_->units()[i].pos); };
    V3 sum;
    int n = 0;
    for (sim::EntityId id : s.members) {
        if (!world_->units()[id - 1].alive()) continue;
        sum += pos(id - 1);
        ++n;
    }
    if (n == 0) return pos(s.members.front() - 1);
    return sum * (1.0f / static_cast<float>(n));
}

void Battle::moveSquad(const Squad& squad, const V3& to) {
    const V3 from = squadCentre(squad, false);
    V3 facing = V3{to.x - from.x, to.y - from.y, 0}.normalized();
    if (facing.length() == 0) facing = {1, 0, 0};
    const V3 right{-facing.y, facing.x, 0};
    const int count = aliveCount(squad);
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(count * 2.5))));
    int i = 0;
    for (sim::EntityId id : squad.members) {
        if (!world_->units()[id - 1].alive()) continue;
        const float col = static_cast<float>(i % cols) - (cols - 1) / 2.0f;
        const float row = static_cast<float>(i / cols);
        issue(squad.owner, id, sim::Command::Type::Move,
              to + right * (col * kFormationSpacing) - facing * (row * kFormationSpacing));
        ++i;
    }
}

void Battle::runEnemyAi() {
    // Depends only on simulation state (never on interpolated positions), so in
    // multiplayer the relay or every peer can run it and issue identical
    // commands. TODO(multiplayer): move to fixed point before lockstep play.
    for (const Squad& s : squads_) {
        if (s.owner != kEnemy || aliveCount(s) == 0) continue;
        const V3 c = squadCentre(s, false);
        float best = 1e30f;
        V3 goal;
        for (const Squad& t : squads_) {
            if (t.owner != kPlayer || aliveCount(t) == 0) continue;
            const V3 tc = squadCentre(t, false);
            const float d = (tc - c).length();
            if (d < best) {
                best = d;
                goal = tc;
            }
        }
        if (best == 1e30f) return;
        const float engage = s.kind == Squad::Kind::Archers ? 90.0f : 15.0f;
        if (best > engage) {
            moveSquad(s, s.kind == Squad::Kind::Archers ? c + (goal - c) * ((best - engage + 5) / best) : goal);
        } else {
            for (sim::EntityId id : s.members) issue(kEnemy, id, sim::Command::Type::Stop, {});
        }
    }
}

int Battle::outcome() const {
    bool playerAlive = false, enemyAlive = false;
    for (const sim::Unit& u : world_->units()) {
        if (!u.alive()) continue;
        (u.owner == kPlayer ? playerAlive : enemyAlive) = true;
    }
    if (!enemyAlive) return 1;
    if (!playerAlive) return 2;
    return 0;
}

V3 Battle::unitPosition(size_t index) const {
    return lerp(prevUnitPos_[index], toV3(world_->units()[index].pos), alpha());
}

bool Battle::unitMoving(size_t index) const {
    const V3 d = toV3(world_->units()[index].pos) - prevUnitPos_[index];
    return d.x * d.x + d.y * d.y > 1e-6f;
}

void Battle::selectSquad(int index) {
    if (index >= 0 && index < static_cast<int>(squads_.size()) && squads_[index].owner == kPlayer) selected_ = index;
}

void Battle::stopSelected() {
    if (selected_ < 0) return;
    for (sim::EntityId id : squads_[selected_].members) issue(kPlayer, id, sim::Command::Type::Stop, {});
}

void Battle::tap(const Camera& camera, float x, float y, float pixelScale) {
    // Nearest living soldier under the finger.
    int bestUnit = -1;
    float bestD2 = kTapRadiusPx * pixelScale * kTapRadiusPx * pixelScale;
    const auto& units = world_->units();
    for (size_t i = 0; i < units.size(); ++i) {
        if (!units[i].alive()) continue;
        float px, py;
        const V3 p = unitPosition(i) + V3{0, 0, 0.9f};
        if (!camera.project(p, px, py)) continue;
        const float d2 = (px - x) * (px - x) + (py - y) * (py - y);
        if (d2 < bestD2) {
            bestD2 = d2;
            bestUnit = static_cast<int>(i);
        }
    }
    // Zoomed far out, soldiers are specks: fall back to the nearest squad centre.
    int bestSquad = bestUnit >= 0 ? squadOfUnit_[bestUnit] : -1;
    if (bestSquad < 0) {
        float bestS = kSquadTapRadiusPx * pixelScale * kSquadTapRadiusPx * pixelScale;
        for (size_t s = 0; s < squads_.size(); ++s) {
            if (aliveCount(squads_[s]) == 0) continue;
            float px, py;
            if (!camera.project(squadCentre(squads_[s]), px, py)) continue;
            const float d2 = (px - x) * (px - x) + (py - y) * (py - y);
            if (d2 < bestS) {
                bestS = d2;
                bestSquad = static_cast<int>(s);
            }
        }
    }

    if (bestSquad >= 0 && squads_[bestSquad].owner == kPlayer) {
        selected_ = bestSquad;
        return;
    }
    if (selected_ < 0 || aliveCount(squads_[selected_]) == 0) return;
    const Squad& mine = squads_[selected_];

    if (bestSquad >= 0) {
        // Tapped an enemy: archers loose on that spot, footmen charge it.
        const V3 at = bestUnit >= 0 ? unitPosition(bestUnit) : squadCentre(squads_[bestSquad]);
        if (mine.kind == Squad::Kind::Archers) {
            for (sim::EntityId id : mine.members) issue(kPlayer, id, sim::Command::Type::AttackGround, at);
        } else {
            moveSquad(mine, at);
        }
        return;
    }
    V3 ground;
    if (camera.pickGround(x, y, ground)) moveSquad(mine, ground);
}

}  // namespace illyria::game
