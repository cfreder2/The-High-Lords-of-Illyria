// Tactics told and commanded (the world's rules: engine/sim/src/world_tactics.cpp). The words floated over a squad
// when position tells ("Flanked", "In the ford", "Braced"), the stance of the selection (Hold, or support), the squads
// standing idle and the key that steps through them, the white flag of a broken squad with its rally countdown, and the
// lord's Rally, which the computer's lords call by the same command. One path for a Skirmish and a Conquest: all of it
// reads the squads and the world the Battle presents.
#include <algorithm>

#include "illyria/game/battle.h"

namespace illyria::game {

namespace {

// The same word over the same ground is said once in this long (ticks), and stays this long.
constexpr uint32_t kFloatAgain = 6 * sim::kTicksPerSecond;
constexpr float kFloatCell = 24.0f;
constexpr size_t kMaxFloats = 24;
// The computer's lord calls the Rally with this many broken men in earshot.
constexpr int kAiRallyMen = 4;

const char* wordOf(sim::TacticEvent::Kind k) {
    switch (k) {
        case sim::TacticEvent::Kind::Flanked: return "Flanked";
        case sim::TacticEvent::Kind::Rear: return "Hit from behind";
        case sim::TacticEvent::Kind::Broken: return "Broken by the charge";
        case sim::TacticEvent::Kind::Braced: return "Braced";
        case sim::TacticEvent::Kind::Ford: return "In the ford";
        case sim::TacticEvent::Kind::HighGround: return "High ground";
        case sim::TacticEvent::Kind::Volley: return "Shot in the water";
    }
    return "";
}

}  // namespace

void Battle::collectTactics() {
    const uint32_t now = world_->tick();
    std::erase_if(floats_, [&](const Float& f) { return now - f.tick > kFloatShown; });
    for (const sim::TacticEvent& e : world_->tactics()) {
        const V3 at = toV3(e.pos);
        // Fog: a word over men the viewer cannot see would tell him of them.
        if (e.owner != local_ && !sees(e.owner, at.x, at.y)) continue;
        const uint64_t key = (static_cast<uint64_t>(e.kind) << 56) ^ (static_cast<uint64_t>(e.owner) << 48) ^
                             (static_cast<uint64_t>(static_cast<uint32_t>(static_cast<int32_t>(at.x / kFloatCell)) & 0xFFFFFF) << 24) ^
                             (static_cast<uint32_t>(static_cast<int32_t>(at.y / kFloatCell)) & 0xFFFFFF);
        const auto it = floatSaid_.find(key);
        if (it != floatSaid_.end() && now - it->second < kFloatAgain) continue;
        floatSaid_[key] = now;
        // Good news for the viewer: his pikes stood, or it happened to the other side.
        const bool good = (e.kind == sim::TacticEvent::Kind::Braced) == (e.owner == local_);
        floats_.push_back({now, e.unit, at, wordOf(e.kind), good});
        if (floats_.size() > kMaxFloats) floats_.erase(floats_.begin());
    }
    if (now % (20 * sim::kTicksPerSecond) == 0) std::erase_if(floatSaid_, [&](const auto& kv) { return now - kv.second > kFloatAgain; });
}

bool Battle::selectionHolds() const {
    const std::vector<sim::EntityId> alive = selectedAlive();
    return !alive.empty() && std::all_of(alive.begin(), alive.end(), [&](sim::EntityId id) { return world_->units()[id - 1].hold; });
}

void Battle::holdSelected() {
    // Hold, or (already holding) back to support.
    const bool hold = !selectionHolds();
    const std::vector<sim::EntityId> alive = selectedAlive();
    if (alive.empty()) return;
    for (sim::EntityId id : alive) {
        sim::Command c{local_, id, sim::Command::Type::SetStance, {}};
        c.hold = hold;
        pending_.push_back(c);
    }
    sfx_.ui(Sound::Toggle);
    say(hold ? "Hold: they keep their place" : "Support: they go to the help of friends fighting near them");
}

Battle::SquadState Battle::squadState(const Squad& s) const {
    SquadState st;
    int alive = 0, idle = 0, hold = 0, broken = 0, longest = 0;
    bool pressed = false;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive()) continue;
        ++alive;
        idle += sim::World::standsIdle(u);
        hold += u.hold;
        if (u.routed) {
            ++broken;
            const int32_t t = world_->rallyTicks(u);
            if (t < 0) pressed = true;
            else longest = std::max(longest, t);
        }
    }
    // (Not a garrison at its stations, nor the lord, who is kept out of the front rank and goes to nobody's help.)
    st.idle = alive > 0 && idle == alive && !s.garrison && s.type != sim::UnitType::Hero;
    st.hold = alive > 0 && hold == alive;
    if (alive > 0 && broken * 2 >= alive) st.rally = pressed ? -1 : std::max(1, (longest + sim::kTicksPerSecond - 1) / sim::kTicksPerSecond);
    return st;
}

int Battle::selectNextIdle() {
    const int n = static_cast<int>(squads_.size());
    for (int k = 1; k <= n; ++k) {
        const int i = (idleCursor_ + k) % n;
        const Squad& s = squads_[static_cast<size_t>(i)];
        if (!controllable(s) || aliveCount(s) == 0 || !squadState(s).idle) continue;
        idleCursor_ = i;
        select(s.members);
        return i;
    }
    refuse("No squad is standing idle");
    return -1;
}

sim::EntityId Battle::lordOf(sim::PlayerId owner, bool controllableOnly) const {
    // The lord who can call it now and has the most broken men in earshot; else any living lord (for his cooldown).
    sim::EntityId best = sim::kNoEntity;
    int bestHeard = -2;
    for (const sim::Unit& u : world_->units()) {
        if (!u.alive() || u.owner != owner || u.stats.type != static_cast<uint8_t>(sim::UnitType::Hero)) continue;
        if (controllableOnly && !controllable(u)) continue;
        const int heard = world_->canRally(u) ? world_->rallyHeard(u) : -1;
        if (heard > bestHeard) bestHeard = heard, best = u.id;
    }
    return best;
}

Battle::RallyState Battle::rallyState() const {
    RallyState r;
    const sim::EntityId lord = lordOf(local_, true);
    if (lord == sim::kNoEntity) return r;
    const sim::Unit& u = world_->units()[lord - 1];
    r.lord = true;
    r.cooldown = (u.rallyCooldown + sim::kTicksPerSecond - 1) / sim::kTicksPerSecond;
    r.heard = world_->rallyHeard(u);
    return r;
}

bool Battle::orderRally() {
    const sim::EntityId lord = lordOf(local_, true);
    if (lord == sim::kNoEntity) {
        refuse("No lord is on the field to rally them");
        return false;
    }
    const sim::Unit& u = world_->units()[lord - 1];
    if (!world_->canRally(u)) {
        refuse("The lord has just called: " + std::to_string((u.rallyCooldown + sim::kTicksPerSecond - 1) / sim::kTicksPerSecond) + " s until he can again");
        return false;
    }
    if (world_->rallyHeard(u) == 0) {
        refuse("No broken men are within the lord's call (40 m)");
        return false;
    }
    pending_.push_back({local_, lord, sim::Command::Type::Rally, {}});
    sfx_.ui(Sound::RallyHorn);
    announce("The lord rallies them: " + std::to_string(world_->rallyHeard(u)) + " men turn back");
    return true;
}

void Battle::aiRally(size_t squad) {
    if (squad >= squads_.size() || squads_[squad].type != sim::UnitType::Hero) return;
    for (sim::EntityId id : squads_[squad].members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (world_->canRally(u) && world_->rallyHeard(u) >= kAiRallyMen) pending_.push_back({u.owner, id, sim::Command::Type::Rally, {}});
    }
}

}  // namespace illyria::game
