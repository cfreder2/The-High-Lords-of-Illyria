// How a battle ends (docs/GAME_DESIGN.md §4.5.1). After each step of the world: a host whose every squad is broken or
// under a fifth of its strength, or that is worn to a third and outnumbered, quits the field (sim::RoutRule: it runs
// for its edge and the battle is called after a few seconds of pursuit); in a siege the defenders lay down their arms when the flag goes up. The moment the outcome
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
            for (const Squad& q : squads_) hosts_.push_back({q.owner, q.members, totalCount(q)});
        }
        rout_.step(*world_, hosts_);
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

Battle::Result Battle::result() const {
    Result r;
    r.outcome = outcome();
    r.how = capture_.taken() ? Result::How::Flag : rout_.called(*world_) ? Result::How::Rout : Result::How::Wiped;
    r.seconds = static_cast<int>((decided() ? decidedTick_ : world_->tick()) / sim::kTicksPerSecond);
    int bestKills = 0;
    std::vector<std::string> foeFallen;
    for (size_t i = 0; i < squads_.size(); ++i) {
        const Squad& q = squads_[i];
        const int alive = aliveCount(q), total = totalCount(q);
        const bool ours = q.owner == local_;
        if (q.type == sim::UnitType::Hero && alive == 0) (ours ? r.fallen : foeFallen).push_back(nameOf(q));
        if (!ours) {
            r.killed += total - alive;
            continue;
        }
        r.lost += total - alive;
        const int kills = standing(q).kills;
        r.rows.push_back({nameOf(q), alive, total, kills, i < everBroke_.size() && everBroke_[i] != 0});
        if (kills > bestKills) {
            bestKills = kills;
            r.best = nameOf(q);
        }
    }
    r.fallen.insert(r.fallen.end(), foeFallen.begin(), foeFallen.end());
    // The Killed column adds up: the dead no squad is credited with (blasts and fire with no hand behind them, the foe's own
    // missiles, men gone with a quitting host) are a row of their own (`total` 0: no squad).
    int credited = 0;
    for (const Result::Row& row : r.rows) credited += row.kills;
    if (r.killed > credited) r.rows.push_back({"Others (fled the field, blasts, fire)", 0, 0, r.killed - credited, false});
    return r;
}

}  // namespace illyria::game
