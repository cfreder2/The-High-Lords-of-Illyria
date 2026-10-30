// A battle as a match's field (battle.h, mode.h): an empty field the mode
// fills, squads raised mid-battle, allies' squads the player cannot order,
// and the computer's orders for the squads of the computer's seats.
#include <algorithm>
#include <climits>
#include <cmath>

#include "illyria/game/battle.h"
#include "illyria/game/mode.h"

namespace illyria::game {

using sim::UnitType;

Battle::Battle(uint64_t seed, const sim::Vec3& centre, std::shared_ptr<const sim::Continent> continent)
    : scenario_(Scenario::Mode) {
    continent_ = continent ? std::move(continent) : std::make_shared<const sim::Continent>(kRegionSeed);
    foeName_.clear();
    build(seed, centre);
    finish();
}

Battle::~Battle() = default;

void Battle::setMode(std::unique_ptr<BattleMode> mode) { mode_ = std::move(mode); }

void Battle::growUnits() {
    const size_t n = world_->units().size();
    if (deathTick_.size() >= n) return;
    const size_t old = prevUnitPos_.size();
    deathTick_.resize(n, UINT32_MAX);
    attackTick_.resize(n, UINT32_MAX);
    hitTick_.resize(n, UINT32_MAX);
    selectedFlag_.resize(n, 0);
    prevUnitPos_.resize(n);
    for (size_t i = old; i < n; ++i) prevUnitPos_[i] = toV3(world_->units()[i].pos);
    if (!foreign_.empty()) foreign_.resize(n, 0);
}

int Battle::addSquad(const std::string& name, sim::PlayerId owner, UnitType type, int count, V3 centre, V3 facing, int rank) {
    spawnSquad(name, owner, type, std::max(1, count), centre, facing.normalized());
    if (rank > 0)
        for (sim::EntityId id : squads_.back().members) world_->setVeterancy(id, rank, 0);
    growUnits();
    return static_cast<int>(squads_.size()) - 1;
}

void Battle::markForeign(size_t squad) {
    if (squad >= squads_.size()) return;
    foreign_.resize(world_->units().size(), 0);
    for (sim::EntityId id : squads_[squad].members) foreign_[id - 1] = 1;
    // Nobody keeps an ally's men selected.
    std::erase_if(selection_, [&](sim::EntityId id) { return foreign_[id - 1] != 0; });
    refreshSelection();
}

void Battle::aiSteer(size_t squad, const V3& to) {
    if (squad < squads_.size()) steerSquad(squads_[squad], to);
}

void Battle::aiForceMarch(size_t squad, const V3& to) {
    if (squad >= squads_.size()) return;
    const Squad& s = squads_[squad];
    std::vector<sim::EntityId> alive;
    for (sim::EntityId id : s.members)
        if (world_->units()[id - 1].alive()) alive.push_back(id);
    s.goal = to;
    s.hasGoal = true;
    moveGroup(s.owner, std::move(alive), s.formation, to, nullptr, s.facing, true);
}

void Battle::aiAttack(size_t squad, sim::EntityId target) {
    if (squad >= squads_.size()) return;
    const sim::Unit* t = world_->findUnit(target);
    if (t == nullptr || !t->alive()) return;
    const Squad& s = squads_[squad];
    s.hasGoal = false;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive() || u.routed) continue;
        // Already after someone still standing his ground: let him finish
        // (one running away is not worth the chase).
        if (u.targetUnit != sim::kNoEntity && u.targetUnit != target) {
            const sim::Unit* cur = world_->findUnit(u.targetUnit);
            if (cur && cur->alive() && !cur->routed) continue;
        }
        if (u.targetUnit == target) continue;
        sim::Command c{s.owner, id, sim::Command::Type::AttackUnit, t->pos};
        c.targetUnit = target;
        pending_.push_back(c);
    }
}

void Battle::aiHalt(size_t squad) {
    if (squad >= squads_.size()) return;
    const Squad& s = squads_[squad];
    s.hasGoal = false;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.alive() && (u.hasMoveTarget || u.targetUnit != sim::kNoEntity)) issue(s.owner, id, sim::Command::Type::Stop, {});
    }
}

sim::EntityId Battle::nearestFoe(sim::PlayerId owner, const V3& at, float reach) const {
    sim::EntityId best = sim::kNoEntity;
    float bestD2 = reach * reach;
    for (const sim::Unit& u : world_->units()) {
        if (!u.alive() || u.owner == owner || u.routed || u.inAir()) continue;
        const V3 d = toV3(u.pos) - at;
        const float d2 = d.x * d.x + d.y * d.y;
        if (d2 < bestD2) {
            bestD2 = d2;
            best = u.id;
        }
    }
    return best;
}

void Battle::aiFight(size_t squad, const V3& goal, const V3& around, float reach) {
    // As runEnemyAi, for any side, from the simulation's state only.
    if (squad >= squads_.size()) return;
    const Squad& s = squads_[squad];
    const int alive = aliveCount(s);
    if (alive == 0 || standing(s).broken == alive) return;  // routed: beyond orders
    const V3 c = squadCentre(s, false);
    const Role role = roleOf(s.type);
    const auto go = [&](const V3& to) {
        if (V3{to.x - c.x, to.y - c.y, 0}.length() > 6.0f) aiSteer(squad, to);
    };
    if (role == Role::Siege) return;  // engines throw from where they stand
    if (role == Role::Healer) {
        // A little behind the nearest friendly fighting squad.
        float best = 1e30f;
        V3 at = goal;
        for (const Squad& f : squads_) {
            if (f.owner != s.owner || &f == &s || aliveCount(f) == 0 || roleOf(f.type) == Role::Healer) continue;
            const V3 fc = squadCentre(f, false);
            const float d = (fc - c).length();
            if (d < best) {
                best = d;
                at = fc;
            }
        }
        if (best > 15.0f) go(at + (c - at).normalized() * 8.0f);
        return;
    }
    const sim::EntityId foe = nearestFoe(s.owner, around, reach);
    if (foe == sim::kNoEntity) {
        go(goal);
        return;
    }
    const V3 fp = toV3(world_->units()[foe - 1].pos);
    const float dist = V3{fp.x - c.x, fp.y - c.y, 0}.length();
    const sim::UnitStats stats = sim::unitStats(s.type);
    const float range = static_cast<float>(stats.ranged.autoRange.toDouble());
    V3 flank;
    if (stats.mounted && role == Role::Melee && flankPoint(s, flank)) {
        aiSteer(squad, flank);  // round a foe locked in melee, into its flank or rear
    } else if (range > 0 && role != Role::Flier) {
        // A good bowshot off, then they shoot on their own.
        const float keep = range * 0.8f;
        if (dist > keep) aiSteer(squad, c + (fp - c) * ((dist - keep + 4) / dist));
        else aiHalt(squad);
    } else if (dist < 30.0f) {
        aiAttack(squad, foe);
    } else {
        aiSteer(squad, fp);
    }
}

}  // namespace illyria::game
