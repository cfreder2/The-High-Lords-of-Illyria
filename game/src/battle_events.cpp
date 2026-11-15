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
            if (q.owner == conquest_->localSide()) announce("Your host has quit the field");
            else if (sees(q.owner, static_cast<float>(q.px) / 1000.0f - o.x, static_cast<float>(q.py) / 1000.0f - o.y)) announce("The enemy has quit the field");
        }
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
