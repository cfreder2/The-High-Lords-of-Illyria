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
        case sim::TacticEvent::Kind::Mire: return "In the mud";
        case sim::TacticEvent::Kind::Uphill: return "Uphill";
        case sim::TacticEvent::Kind::Cover: return "Cover";
        case sim::TacticEvent::Kind::Ambush: return "Ambush";
        case sim::TacticEvent::Kind::Disordered: return "Disordered";
        case sim::TacticEvent::Kind::Square: return "Square holds";
        case sim::TacticEvent::Kind::Downhill: return "Downhill charge";
        case sim::TacticEvent::Kind::Charge: return "";  // (counted, not said: the flank and the back have their own words)
    }
    return "";
}

// A word that is good news for the men it is over (the rest are bad news for them).
bool goodFor(sim::TacticEvent::Kind k) {
    return k == sim::TacticEvent::Kind::Braced || k == sim::TacticEvent::Kind::Cover || k == sim::TacticEvent::Kind::Square;
}

const char* whyOf(sim::HelpCall::Why why) {
    switch (why) {
        case sim::HelpCall::Why::Outnumbered: return "outnumbered";
        case sim::HelpCall::Why::Flanked: return "flanked";
        case sim::HelpCall::Why::Wavering: return "wavering";
        case sim::HelpCall::Why::Shot: return "under arrows";
        case sim::HelpCall::Why::HeroDown: return "their lord is down";
        case sim::HelpCall::Why::Hunted: return "hunted";
    }
    return "";
}

}  // namespace

void Battle::collectTactics() {
    const uint32_t now = world_->tick();
    std::erase_if(floats_, [&](const Float& f) { return now - f.tick > kFloatShown; });
    for (const sim::TacticEvent& e : world_->tactics()) {
        if (wordOf(e.kind)[0] == 0) continue;
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
        const bool good = goodFor(e.kind) == (e.owner == local_);
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

const char* Battle::groundTag(const Squad& s) const {
    // What most of its men stand on; else how it stands to the nearest foe it can see within a bowshot.
    int alive = 0, ford = 0, mire = 0, woods = 0, hidden = 0, road = 0;
    float height = 0;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive()) continue;
        ++alive;
        const sim::World::Footing f = world_->footing(u);
        ford += f == sim::World::Footing::Ford;
        mire += f == sim::World::Footing::Mire;
        if (world_->inWoods(u)) ++woods, hidden += u.seenBy == (1u << u.owner);
        road += worldRegion().groundAt(u.pos.x, u.pos.y) == sim::Ground::Road;
        height += static_cast<float>(u.pos.z.toDouble());
    }
    if (alive == 0) return "";
    if (ford * 2 >= alive) return "in the ford";
    if (mire * 2 >= alive) return "in the mud";
    if (woods * 2 >= alive) return hidden == woods ? "hidden in woods" : "in woods";
    height /= static_cast<float>(alive);
    const V3 c = squadCentre(s, false);
    const sim::EntityId foe = nearestFoe(s.owner, c, 80.0f);
    if (foe != sim::kNoEntity) {
        const sim::Unit& f = world_->units()[foe - 1];
        const V3 fp = toV3(f.pos);
        if (f.owner == local_ || sees(f.owner, fp.x, fp.y)) {
            const float dz = height - fp.z;
            if (dz >= 2.0f) return "high ground";
            if (dz <= -2.0f) return "low ground";
        }
    }
    return road * 2 >= alive ? "on the road" : "";
}

const char* Battle::groundAt(const V3& at) const {
    const sim::Vec3 p = toSim(at);
    switch (worldRegion().groundAt(p.x, p.y)) {
        case sim::Ground::Shallows: return "ford";
        case sim::Ground::DeepWater: return "deep water";
        case sim::Ground::Swamp:
        case sim::Ground::Mud: return "mud";
        case sim::Ground::Forest: return "woods";
        case sim::Ground::Bridge: return "bridge";
        default: break;
    }
    const std::vector<sim::EntityId> alive = selectedAlive();
    if (!alive.empty()) {
        const V3 c = centreOf(alive);
        const float dz = static_cast<float>((world_->groundHeight(p.x, p.y) - world_->groundHeight(toSim(c).x, toSim(c).y)).toDouble());
        if (dz >= 2.0f) return "higher ground";
        if (dz <= -2.0f) return "lower ground";
    }
    return worldRegion().groundAt(p.x, p.y) == sim::Ground::Road ? "road" : "";
}

int Battle::squadOfNumber(uint32_t number) const {
    if (number == 0) return -1;
    if (conquest_) return squadOfArmy(number);
    return number <= squads_.size() ? static_cast<int>(number) - 1 : -1;
}

void Battle::collectCalls() {
    const uint32_t now = world_->tick();
    std::erase_if(calls_, [&](const Call& c) { return now - c.tick > sim::World::kCallShownTicks; });
    for (const sim::HelpCall& c : world_->helpCalls()) {
        if (c.owner != local_) {
            // Another side's: nothing of it is shown or told (it would give away men the viewer may not see). The computer
            // answers its own with the same march the world gives a free squad.
            aiAnswer(c);
            continue;
        }
        const int q = squadOfNumber(c.squad), a = squadOfNumber(c.answer);
        if (q < 0) continue;
        calls_.push_back({now, toV3(c.pos), q, a, c.caller, c.why});
        // Over the squad that calls, and over the one that goes (the words floated, as position's are).
        floats_.push_back({now, c.caller, toV3(c.pos), "Help!", false});
        if (a >= 0) floats_.push_back({now, c.answerer, toV3(c.answerPos), "Answering", true});
        std::string line = squads_[static_cast<size_t>(q)].name + " call for help: " + whyOf(c.why);
        if (a >= 0) line += ". " + squads_[static_cast<size_t>(a)].name + " answer";
        announce(line);
    }
}

void Battle::aiAnswer(const sim::HelpCall& call) {
    // Only a side the computer plays here (a skirmish's foe; a Conquest's lords are theirs to run: conquestAi).
    if (conquest_ || call.owner != kEnemy || aiSkill_ == AiSkill::Easy) return;
    const sim::Unit* caller = world_->findUnit(call.caller);
    if (caller == nullptr) return;
    const auto send = [&](const Squad& s) {
        for (sim::EntityId id : s.members) {
            if (!world_->units()[id - 1].alive()) continue;
            sim::Command c{call.owner, id, sim::Command::Type::Answer, {}};
            c.targetUnit = call.caller;
            pending_.push_back(c);
        }
    };
    // A squad of its horse that the world has already sent goes by the flank: that ride is a commander's (Command::Answer).
    if (call.answer != 0) {
        const int a = squadOfNumber(call.answer);
        if (a >= 0 && sim::unitStats(squads_[static_cast<size_t>(a)].type).mounted) send(squads_[static_cast<size_t>(a)]);
        return;
    }
    // The nearest squad that is not itself fighting, horse first (they take the flank).
    int best = -1;
    float bestScore = 140.0f;
    const V3 at = toV3(call.pos);
    for (size_t i = 0; i < squads_.size(); ++i) {
        const Squad& s = squads_[i];
        if (s.owner != call.owner || s.garrison || s.type == sim::UnitType::Hero || squadOfNumber(call.squad) == static_cast<int>(i)) continue;
        const Role r = roleOf(s.type);
        if (r != Role::Melee && r != Role::Ranged) continue;
        int alive = 0, busy = 0;
        for (sim::EntityId id : s.members) {
            const sim::Unit& u = world_->units()[id - 1];
            alive += u.alive();
            busy += u.alive() && (u.engaged || u.foe != sim::kNoEntity || u.routed || u.helping != sim::kNoEntity);
        }
        if (alive == 0 || busy * 2 > alive) continue;
        const float d = (squadCentre(s, false) - at).length();
        const float score = sim::unitStats(s.type).mounted ? d * 0.5f : d;
        if (score < bestScore) bestScore = score, best = static_cast<int>(i);
    }
    if (best < 0) return;
    send(squads_[static_cast<size_t>(best)]);
    squads_[static_cast<size_t>(best)].hasGoal = false;
    if (aiAnswering_.size() < squads_.size()) aiAnswering_.resize(squads_.size(), 0);
    aiAnswering_[static_cast<size_t>(best)] = world_->tick() + 20 * sim::kTicksPerSecond;
}

void Battle::aiFormation(size_t squad) {
    if (squad >= squads_.size() || aiSkill_ == AiSkill::Easy) return;
    Squad& s = squads_[squad];
    if (s.type == sim::UnitType::Hero || roleOf(s.type) == Role::Siege) return;
    const sim::UnitStats stats = sim::unitStats(s.type);
    const V3 c = squadCentre(s, false);
    const uint32_t now = world_->tick();
    std::vector<sim::EntityId> alive;
    int shot = 0, fighting = 0;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive()) continue;
        alive.push_back(id);
        shot += u.shotTick != 0 && now + 1 - u.shotTick <= 4 * sim::kTicksPerSecond;
        fighting += u.engaged || u.foe != sim::kNoEntity;
    }
    if (alive.size() < 4) return;
    // Horse coming at foot that has no pikes' answer: the square. Arrows falling on men with nobody at their throats: loose.
    bool horse = false;
    if (!stats.mounted && roleOf(s.type) == Role::Melee)
        for (const sim::Unit& u : world_->units()) {
            if (!u.alive() || u.owner == s.owner || !u.stats.mounted || u.routed) continue;
            const V3 p = toV3(u.pos);
            if (!sees(u.owner, p.x, p.y) || world_->concealed(u, s.owner)) continue;
            if ((p - c).length() < 45.0f && u.foe == sim::kNoEntity) horse = true;
        }
    Formation want = defaultFormation(s.type);
    if (horse && fighting == 0) want = Formation::Box;
    else if (shot * 4 >= static_cast<int>(alive.size()) && fighting == 0) want = Formation::Loose;
    if (want == s.formation) return;
    s.formation = want;
    tellFormation(s.owner, alive, want);
}

bool Battle::answerCall(V3& at) {
    if (calls_.empty()) {
        say("Nobody is calling for help");
        return false;
    }
    const Call& call = calls_.back();
    at = call.pos;
    // The nearest of the player's squads with nothing in hand (not the caller, not the lord), by kind as the world chooses
    // (World::callForHelp): to a melee, foot or horse, bowmen only when none is free; under missiles, horse or bowmen.
    int best = -1;
    float bestD = 1e30f;
    const bool melee = call.why != sim::HelpCall::Why::Shot;
    for (int pass = 0; pass < 2 && best < 0; ++pass)
    for (size_t i = 0; i < squads_.size(); ++i) {
        const Squad& s = squads_[i];
        if (static_cast<int>(i) == call.squad || !controllable(s) || aliveCount(s) == 0 || s.garrison || s.type == sim::UnitType::Hero) continue;
        const sim::UnitStats& st = sim::unitStats(s.type);
        const bool bows = st.ranged.muzzleSpeed > sim::Fixed::zero();
        if (melee && bows && pass == 0) continue;
        if (!melee && !bows && !st.mounted && st.flyAltitude == sim::Fixed::zero()) continue;
        int alive = 0, freeMen = 0;
        for (sim::EntityId id : s.members) {
            const sim::Unit& u = world_->units()[id - 1];
            alive += u.alive();
            freeMen += u.alive() && !u.routed && !u.engaged && u.foe == sim::kNoEntity && !sim::World::hasOrder(u);
        }
        if (freeMen * 2 < alive) continue;
        const float d = (squadCentre(s, false) - call.pos).length();
        if (d < bestD) bestD = d, best = static_cast<int>(i);
    }
    if (best < 0) {
        say("No squad is free to answer");
        return true;
    }
    // Sent: the same march the world gives a free squad, and for horse the ride round the flank that only a command gives.
    select(squads_[static_cast<size_t>(best)].members);
    for (sim::EntityId id : squads_[static_cast<size_t>(best)].members) {
        if (!world_->units()[id - 1].alive()) continue;
        sim::Command c{local_, id, sim::Command::Type::Answer, {}};
        c.targetUnit = call.caller;
        pending_.push_back(c);
    }
    say(squads_[static_cast<size_t>(best)].name + " answer the call");
    return true;
}

Battle::SquadState Battle::squadState(const Squad& s, bool ground) const {
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
    const uint32_t now = world_->tick();
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive()) continue;
        st.calling = st.calling || (u.called != 0 && now + 1 - u.called <= sim::World::kCallShownTicks);
        st.answering = st.answering || u.helping != sim::kNoEntity;
    }
    if (ground) st.ground = groundTag(s);
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
