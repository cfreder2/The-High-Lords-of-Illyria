// How a battle ends (docs/GAME_DESIGN.md §4.5.1). After each step of the world: a host whose every squad is broken or
// under a fifth of its strength, or that is worn to a third and outnumbered, quits the field (sim::RoutRule: it runs
// for its edge, the other side's horse and light foot ride it down (pursue), and the battle is called when the pursuit
// is over or the runners have got away); in a siege the defenders lay down their arms when the flag goes up. The moment the outcome
// is known is kept (decidedTick_), the clock slows over the last blow and stops (Battle::advance), and the results
// sheet is read from the squads as they then stand (result()).
//
// A match (mode.h) ends by its mode's own rules and a Conquest's fight is the end of nothing (its beaten stacks march
// off: Conquest::quitFields, with this same squadSpent); neither comes here.
#include <algorithm>

#include "illyria/game/battle.h"

namespace illyria::game {

namespace {

std::string nameOf(const Squad& q) { return q.name.empty() ? std::string(sim::unitTypeName(q.type)) : q.name; }

}  // namespace

void Battle::judge() {
    if (conquest_) return;
    if (capture_.taken()) {
        world_->breakSide(capture_.attacker() == kPlayer ? kEnemy : kPlayer);  // the flag is up: they lay down their arms
    } else if (scenario_ != Scenario::Mode) {  // (an empty field is a test's or a match's: it is fought to the last man)
        if (hosts_.size() != squads_.size()) {
            hosts_.clear();
            // (Its strength is what it brought to this field: a squad carried in at seven of twelve is whole at seven.)
            for (const Squad& q : squads_) hosts_.push_back({q.owner, q.members, static_cast<int32_t>(q.members.size())});
        }
        rout_.step(*world_, hosts_);
        pursue();
    }
    if (world_->tick() % 5 == 0) {
        everBroke_.resize(squads_.size(), 0);
        for (size_t i = 0; i < squads_.size(); ++i) {
            const int alive = aliveCount(squads_[i]);
            if (alive > 0 && standing(squads_[i]).broken * 2 >= alive) everBroke_[i] = 1;
        }
    }
    if (!decided() && outcome() != 0) decidedTick_ = world_->tick();
}

void Battle::pursue() {
    // The rout (docs/GAME_DESIGN.md 4.5.1): while the beaten host runs for its edge, the horse and the light foot of
    // the other side ride it down, each man after the runner nearest him, as an order through the one command list.
    // The player's own go only when they stand idle and are not told to hold: an order of his is never overridden.
    const sim::PlayerId beaten = rout_.quitter();
    if (beaten == sim::RoutRule::kNobody || outcome() != 0 || (world_->tick() - rout_.quitTick()) % kPursueLookTicks != 0) return;
    for (size_t i = 0; i < squads_.size(); ++i) {
        const Squad& q = squads_[i];
        if (q.owner == beaten || q.garrison || !pursues(q.type)) continue;
        if (q.owner == local_) {
            const SquadState st = squadState(q);
            if (!st.idle || st.hold) continue;
        }
        for (sim::EntityId id : q.members) {
            const sim::Unit& u = world_->units()[id - 1];
            if (!u.alive() || u.routed) continue;
            if (const sim::Unit* cur = world_->findUnit(u.targetUnit); cur && cur->alive()) continue;
            const sim::Unit* best = nullptr;
            sim::Fixed bestD;
            for (const sim::Unit& f : world_->units()) {
                if (f.owner != beaten || !f.alive() || f.inAir()) continue;
                const sim::Fixed d = (f.pos - u.pos).flat().lengthSq();
                if (!best || d < bestD) best = &f, bestD = d;
            }
            if (!best) return;
            sim::Command c{q.owner, id, sim::Command::Type::AttackUnit, best->pos};
            c.targetUnit = best->id;
            pending_.push_back(c);
        }
    }
}

bool Battle::pursues(sim::UnitType type) {
    switch (type) {
        case sim::UnitType::LightCavalry:
        case sim::UnitType::HeavyCavalry:
        case sim::UnitType::WolfRiders:
        case sim::UnitType::LightInfantry:
        case sim::UnitType::Warrior:
        case sim::UnitType::Berserker: return true;
        default: return false;
    }
}

int Battle::pursuitSeconds() const {
    if (rout_.quitter() == sim::RoutRule::kNobody || outcome() != 0) return 0;
    const uint32_t end = rout_.quitTick() + sim::RoutRule::kPursuitTicks;
    return end > world_->tick() ? static_cast<int>((end - world_->tick() + sim::kTicksPerSecond - 1) / sim::kTicksPerSecond) : 0;
}

Battle::Result Battle::result() const {
    Result r;
    r.outcome = outcome();
    r.how = capture_.taken() ? Result::How::Flag : rout_.called(*world_) ? Result::How::Rout : Result::How::Wiped;
    r.seconds = static_cast<int>((decided() ? decidedTick_ : world_->tick()) / sim::kTicksPerSecond);
    int bestKills = 0;
    std::vector<std::string> foeFallen;
    for (size_t i = 0; i < squads_.size(); ++i) {
        const Squad& q = squads_[i];
        const int alive = aliveCount(q), total = static_cast<int>(q.members.size());  // (what it brought to this battle)
        const bool ours = q.owner == local_;
        if (q.type == sim::UnitType::Hero && alive == 0) (ours ? r.fallen : foeFallen).push_back(nameOf(q));
        if (!ours) {
            r.killed += total - alive;
            continue;
        }
        r.lost += total - alive;
        const int kills = standing(q).kills;
        int promoted = 0;
        for (sim::EntityId id : q.members) {
            const sim::Unit* u = world_->findUnit(id);
            promoted += u && u->alive() && id <= startRank_.size() && u->rank > startRank_[id - 1];
        }
        r.promoted += promoted;
        r.rows.push_back({nameOf(q), alive, total, kills, i < everBroke_.size() && everBroke_[i] != 0, q.type == sim::UnitType::Hero ? standing(q).level : standing(q).rank, promoted, q.type == sim::UnitType::Hero});
        if (kills > bestKills) {
            bestKills = kills;
            r.best = nameOf(q);
        }
    }
    r.fallen.insert(r.fallen.end(), foeFallen.begin(), foeFallen.end());
    return r;
}

}  // namespace illyria::game
