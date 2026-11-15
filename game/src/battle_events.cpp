// The strip of events for the player: what happened to his own men, in a few short lines that fade. A squad that
// broke, a squad wiped out, a hero fallen, and (in a Conquest) the last army destroyed. It reads the squads once a
// sixth of a second, after the world's step, so a skirmish and a Conquest share it (docs/ARCHITECTURE_RULES.md, 1).
// Presentation only: nothing here touches the world, and nothing is read back into it.
#include <algorithm>

#include "illyria/game/battle.h"

namespace illyria::game {

namespace {

// A squad that broke and rallied and broke again says so once in this long.
constexpr uint32_t kRebreakTicks = 12 * sim::kTicksPerSecond;
constexpr size_t kMaxEvents = 8;

// "A", "A and B", "3 squads".
std::string listOf(const std::vector<std::string>& names, const char* plural) {
    if (names.size() == 1) return names[0];
    if (names.size() == 2) return names[0] + " and " + names[1];
    return std::to_string(names.size()) + " " + plural;
}

}  // namespace

void Battle::announce(const std::string& text) {
    // The same words twice running (a squad's sibling breaking a moment later) is one line.
    if (!events_.empty() && events_.back().text == text && eventClock_ - events_.back().at < kEventShown) return;
    events_.push_back({eventClock_, text});
    events_.erase(std::remove_if(events_.begin(), events_.end(), [this](const Event& e) { return eventClock_ - e.at > kEventKeep; }), events_.end());
    if (events_.size() > kMaxEvents) events_.erase(events_.begin(), events_.end() - static_cast<std::ptrdiff_t>(kMaxEvents));
}

void Battle::observeFight() {
    const uint32_t now = conquest_->tick();
    const uint8_t me = conquest_->localSide();
    const auto armyOf = [&](uint32_t id) -> const sim::Army* {
        for (const sim::Conquest::Stack& st : conquest_->stacks())
            if (st.owner == me)
                for (const sim::Army& a : st.armies)
                    if (a.id == id) return &a;
        return nullptr;
    };
    bool fighting = false;
    for (const sim::Conquest::Stack& st : conquest_->stacks()) {
        if (st.owner != me) continue;
        for (const sim::Army& a : st.armies) {
            bool fresh = false;
            for (const sim::Man& m : a.men) fresh = fresh || (m.alive() && m.unit && m.fightTick != 0 && now - m.fightTick < kFightFreshTicks);
            if (!fresh) continue;
            fighting = true;
            if (std::none_of(fight_.armies.begin(), fight_.armies.end(), [&](const FightWatch::Entry& e) { return e.army == a.id; })) {
                if (fight_.armies.empty()) fight_.began = eventClock_;
                fight_.armies.push_back({a.id, a.name.empty() ? std::string(sim::unitTypeName(a.type)) : a.name, a.soldiers, a.kills, a.rank, a.hero()});
            }
        }
    }
    if (fighting) {
        fight_.last = eventClock_;
        return;
    }
    if (fight_.armies.empty() || eventClock_ - fight_.last < kFightOverTicks) return;
    // It is over: what each squad that fought has left and did.
    Result r;
    r.conquest = true;
    r.seconds = static_cast<int>((fight_.last - fight_.began) / sim::kTicksPerSecond);
    int best = 0, left = 0;
    for (const FightWatch::Entry& e : fight_.armies) {
        const sim::Army* a = armyOf(e.army);  // (nullptr: wiped out, and taken off the roll)
        Result::Row row{e.name, a ? a->soldiers : 0, e.alive, a ? std::max(0, a->kills - e.kills) : 0, false, a ? a->rank : e.rank, a && a->rank > e.rank ? 1 : 0, e.hero};
        if (e.hero) row.rank += 1;  // (a hero's level)
        left += row.alive;
        r.lost += row.total - row.alive;
        r.killed += row.kills;
        r.promoted += row.promoted;
        if (e.hero && row.alive == 0) r.fallen.push_back(e.name);
        if (row.kills > best) best = row.kills, r.best = e.name;
        r.rows.push_back(std::move(row));
    }
    r.outcome = fight_.weQuit || left == 0 ? 2 : 1;
    r.how = fight_.weQuit || fight_.foeQuit ? Result::How::Rout : Result::How::Wiped;
    fight_ = {};
    if (r.killed + r.lost < kFightWorth) return;
    fightReport_ = std::move(r);
    fightTold_ = true;
}

void Battle::observeSquads() {
    ++eventClock_;
    if (eventClock_ % 5 != 0) return;
    std::vector<std::string> broke, wiped;
    for (size_t i = 0; i < squads_.size(); ++i) {
        const Squad& q = squads_[i];
        if (!controllable(q) || q.garrison) continue;
        const uint32_t key = q.army ? q.army : 0x80000000u + static_cast<uint32_t>(i);
        SquadWatch& w = squadWatch_[key];
        const int alive = aliveCount(q);
        const bool breaking = alive > 0 && standing(q).broken * 2 >= alive;
        if (w.alive > 0 && alive == 0) {
            if (q.type == sim::UnitType::Hero) announce(q.name + " has fallen");
            else wiped.push_back(q.name.empty() ? std::string(sim::unitTypeName(q.type)) : q.name);
        } else if (breaking && !w.broke && (w.brokeAt == 0 || eventClock_ - w.brokeAt >= kRebreakTicks)) {
            if (w.alive > 0) {
                broke.push_back(q.name.empty() ? std::string(sim::unitTypeName(q.type)) : q.name);
                w.brokeAt = eventClock_;
            }
        }
        w.alive = alive;
        w.broke = breaking;
    }
    if (!wiped.empty()) announce(listOf(wiped, "squads") + (wiped.size() == 1 ? " was wiped out" : " were wiped out"));
    if (!broke.empty()) announce(listOf(broke, "squads") + (broke.size() == 1 ? " broke and is running" : " broke and are running"));
    // The isle's pole (Oakhaven Ford): the other host's flag starting up it unopposed is news, with the alarm; so is ours.
    if (capture_.active() && capture_.neutral() && !capture_.taken()) {
        const int rising = capture_.attackersIn() > 0 && capture_.defendersIn() == 0 ? (capture_.attacker() == local_ ? 1 : 2) : 0;
        if (rising != poleRising_) {
            if (rising == 2) {
                announce("Their flag is going up on the isle");
                sfx_.alert(Sound::Alarm);
            } else if (rising == 1) {
                announce("Your flag is going up on the isle");
            }
            poleRising_ = rising;
        }
    }
    // A Conquest: a stack beaten in a fight has quit the field (Conquest::quitFields): the local side's own, or
    // another's that the viewer may see.
    if (conquest_) {
        const V3 o = origin();
        for (const sim::Conquest::Quit& q : conquest_->quits()) {
            if (q.tick <= quitSeen_) continue;
            quitSeen_ = q.tick;
            if (q.owner == conquest_->localSide()) {
                announce("Your host has quit the field");
                fight_.weQuit = !fight_.armies.empty();
            } else if (sees(q.owner, static_cast<float>(q.px) / 1000.0f - o.x, static_cast<float>(q.py) / 1000.0f - o.y)) {
                announce("The enemy has quit the field");
                fight_.foeQuit = !fight_.armies.empty();
            }
        }
        observeFight();
    }
    // A Conquest: the last army of the local side gone. The map's banner stays while it is so; this is the moment.
    if (conquest_ && eventClock_ % 30 == 0) {
        bool any = false;
        for (const sim::Conquest::Stack& st : conquest_->stacks()) any = any || (st.owner == conquest_->localSide() && !st.armies.empty());
        if (hadArmy_ && !any) {
            announce("Your army has been destroyed. Train new armies in a town you hold.");
            say("Your army has been destroyed");
        }
        hadArmy_ = any;
    }
}

}  // namespace illyria::game
