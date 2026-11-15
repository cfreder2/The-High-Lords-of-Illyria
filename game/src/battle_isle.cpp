// Oakhaven Ford as the computer plays it (docs/GAME_DESIGN.md §4.1.4). The ground is fair (sim::Continent::FairGround): the
// river runs straight between the hosts, an isle in its ford with a bare pole on it (CapturePoint::placeNeutral), deep water
// above and below the ford, a timber bridge a column wide further up and down. A host that wades is struck harder and
// strikes weaker (World::wading), so the computer fights from dry ground:
//   - two squads of foot go over the ford to the isle and stand on it, on Hold: they cut down who comes onto it and
//     chase nobody into the water;
//   - its pikes stand at its own ends of the bridges, a squad to a bridge, on Hold, facing the deck;
//   - its bowmen stand on its bank over the ford (the berm), its bombers at the water's edge, its lord behind them;
//   - the rest of its foot goes to its own end of one bridge and its horse to the other's (on Easy a squad to each in
//     turn, after its wait), and stands there, closed up, until the other host has come forward to the river (a fifth
//     of its men within 65 m of the middle line, or over it): then they go over and at the men on the far bank's dry
//     ground. A host that sits in its camp is not charged across a bridge: the flag is going up without that. Nor
//     is a bridge whose far end is held: they go by the other if its far end is open, and else keep their own end;
//   - a third squad of foot goes to the isle while the pole is disputed or the other host's flag is on it;
//   - a squad of the line with the foe on its own bank within 50 m turns on him first, and its horse rides for the isle
//     when the other host's flag is going up there unopposed.
// It reads the simulation only, as runEnemyAi does.
#include <algorithm>

#include "illyria/game/battle.h"

namespace illyria::game {

using sim::UnitType;

namespace {

constexpr int kIsleSquads = 2;         // of its foot on the isle (on Easy, one)
constexpr float kForwardM = 65.0f;     // a man of the foe this near the river's middle, or over it, has come forward
constexpr int kHeldMen = 5;            // this many of the foe at the far end of a bridge hold it
constexpr int kCommittedPct = 20;      // this share of the foe's men forward: he has committed, and it goes round at him
constexpr float kPostAt = 6.0f;        // a squad this near its post is at it
constexpr float kBridgeBack = 8.0f;    // the pikes stand this far back from the foot of the ramp
constexpr float kBermM = 54.0f;        // the bowmen's line, from the river's middle (the berm's crest)
constexpr float kHomeAlarmM = 50.0f;   // a foe on its own bank this near a squad of the line is met
constexpr int kCloseUpLooks = 12;      // looks a squad waits at its end of a bridge for the others going that way

float flatDist(const V3& a, const V3& b) { return V3{a.x - b.x, a.y - b.y, 0}.length(); }

}  // namespace

bool Battle::isle(Isle& out) const {
    if (!capture_.active() || !capture_.neutral()) return false;
    const sim::Continent::FairGround& fair = continent_->fairGround();
    const sim::Vec3 o = region_->origin();
    out.pole = toV3(capture_.at());
    out.bank = static_cast<float>(fair.bank.toDouble());
    int n = 0;
    for (const sim::Continent::Crossing& cr : continent_->crossings()) {
        if (!cr.bridged() || n == 2 || !fair.holds(cr.pos.x, cr.pos.y)) continue;
        out.bridges[n++] = {static_cast<float>((cr.pos.y - o.y).toDouble()), static_cast<float>(std::max(cr.reachAhead(), cr.reachBehind()).toDouble())};
    }
    return n == 2;
}

void Battle::aiPost(size_t squad, const V3& post, const V3& face) {
    const Squad& s = squads_[squad];
    std::vector<sim::EntityId> alive;
    bool moving = false, held = true;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive() || u.routed) continue;
        alive.push_back(id);
        moving = moving || u.hasMoveTarget;
        held = held && u.hold;
    }
    if (alive.empty()) return;
    if (!held)
        for (sim::EntityId id : alive) {
            sim::Command c{s.owner, id, sim::Command::Type::SetStance, {}};
            c.hold = true;
            pending_.push_back(c);
        }
    const bool sent = s.hasGoal && flatDist(s.goal, post) < 2.0f;
    if (flatDist(squadCentre(s, false), post) <= kPostAt || (sent && moving)) return;
    s.goal = post;
    s.hasGoal = true;
    moveGroup(s.owner, std::move(alive), s.formation, post, &face, s.facing);
}

bool Battle::aiIsle(size_t q) {
    Isle g;
    if (!isle(g)) return false;
    const Squad& s = squads_[q];
    const Role role = roleOf(s.type);
    if (role == Role::Healer || role == Role::Scout || role == Role::Flier || role == Role::Siege) return false;  // as on any field
    const sim::UnitStats stats = sim::unitStats(s.type);
    // Its own bank: the second seat's host stands east of the river, the first's west.
    const float side = s.owner == kEnemy ? 1.0f : -1.0f;
    const V3 c = squadCentre(s, false);
    const auto east = [&](const V3& p) { return (p.x - g.pole.x) * side; };  // metres onto its own bank from the middle line
    const V3 toRiver{-side, 0, 0};
    if (aiMemo_.size() < squads_.size()) aiMemo_.resize(squads_.size());
    AiMemo& memo = aiMemo_[q];
    const auto release = [&] {  // off Hold: it goes to the help of friends again
        for (sim::EntityId id : s.members) {
            const sim::Unit& u = world_->units()[id - 1];
            if (!u.alive() || !u.hold) continue;
            sim::Command cmd{s.owner, id, sim::Command::Type::SetStance, {}};
            cmd.hold = false;
            pending_.push_back(cmd);
        }
    };
    // The bowmen, the bombers and the lord: on their own bank over the ford, and what comes in reach they shoot.
    if (role == Role::Ranged || role == Role::Caster || role == Role::Bomber || s.type == UnitType::Hero) {
        int nth = 0;
        for (size_t k = 0; k < q; ++k) nth += squads_[k].owner == s.owner && aliveCount(squads_[k]) > 0 && roleOf(squads_[k].type) == role && squads_[k].type != UnitType::Hero;
        const float along = static_cast<float>((nth + 1) / 2) * 30.0f * (nth % 2 ? -1.0f : 1.0f);
        const float back = s.type == UnitType::Hero ? kBermM + 18.0f : role == Role::Bomber ? g.bank + 4.0f : kBermM;
        const V3 post{g.pole.x + side * back, g.pole.y + (s.type == UnitType::Hero ? 0.0f : along), 0};
        const float range = static_cast<float>(stats.ranged.autoRange.toDouble());
        aiFight(q, post, post, std::max(range, 30.0f));
        return true;
    }
    if (role != Role::Melee) return false;
    // Whoever its foot and horse fight, they fight on dry ground: a man in the water is left to the bowmen.
    struct Dry {
        bool& flag;
        explicit Dry(bool& f) : flag(f) { flag = true; }
        ~Dry() { flag = false; }
    } dry(aiDryFoes_);

    // The foe on its own bank, near: met before anything else (its bowmen and its camp are behind it).
    if (s.type != UnitType::Pikemen && east(c) > g.bank) {
        const sim::EntityId near = pickFoe(s.owner, c, kHomeAlarmM, stats.mounted);
        if (near != sim::kNoEntity && east(toV3(world_->units()[near - 1].pos)) > g.bank) {
            release();
            aiFight(q, c, c, kHomeAlarmM);
            return true;
        }
    }

    // The other host's flag going up with nobody of its own at the pole: its horse rides for the isle at once (the foot
    // for the isle is on its way, and slower).
    if (stats.mounted && memo.flank < 2 && capture_.attacker() != s.owner && capture_.attackersIn() > 0 && capture_.defendersIn() == 0) {
        release();
        memo.bridge = -1;
        const sim::EntityId foe = pickFoe(s.owner, g.pole, static_cast<float>(CapturePoint::kNeutralRadiusM) + 3.0f, true);
        if (foe != sim::kNoEntity && flatDist(c, g.pole) < 30.0f) aiAttack(q, foe);
        else aiSteer(q, g.pole);
        return true;
    }

    // The pikes: a squad to each bridge, at its own end, facing the deck.
    if (s.type == UnitType::Pikemen) {
        int nth = 0;
        for (size_t k = 0; k < q; ++k) nth += squads_[k].owner == s.owner && squads_[k].type == UnitType::Pikemen && aliveCount(squads_[k]) > 0;
        const Isle::Bridge& b = g.bridges[nth % 2];
        aiPost(q, {g.pole.x + side * (b.reach + kBridgeBack), b.y, 0}, toRiver);
        return true;
    }

    // The foot for the isle: the kIsleSquads squads of the line nearest the pole that are whole and have not gone round.
    const auto forIsle = [&](size_t k) {
        const Squad& f = squads_[k];
        if (f.owner != s.owner || aliveCount(f) == 0 || f.type == UnitType::Pikemen || f.type == UnitType::Hero || roleOf(f.type) != Role::Melee) return false;
        if (sim::unitStats(f.type).mounted || (aiSkill_ != AiSkill::Easy && aiShattered(f)) || (k < aiMemo_.size() && aiMemo_[k].flank > 0)) return false;
        return east(squadCentre(f, false)) > -g.bank;
    };
    if (forIsle(q)) {
        const float mine = flatDist(c, g.pole);
        int nearer = 0;
        for (size_t k = 0; k < squads_.size(); ++k) {
            if (k == q || !forIsle(k)) continue;
            const float d = flatDist(squadCentre(squads_[k], false), g.pole);
            nearer += d < mine || (d == mine && k < q);
        }
        // (One more while the pole is disputed or the other host's flag is on it.)
        const bool pressed = capture_.blocked() || (capture_.attacker() != s.owner && (capture_.attackersIn() > 0 || capture_.progress() > 0));
        if (nearer < (aiSkill_ == AiSkill::Easy ? 1 : kIsleSquads) + (pressed ? 1 : 0)) {
            memo.bridge = -1;
            // On the isle with the foe on it too: cut him down. Else stand at the pole, each squad on its half of the isle.
            const sim::EntityId foe = pickFoe(s.owner, g.pole, static_cast<float>(CapturePoint::kNeutralRadiusM) + 3.0f, false);
            if (foe != sim::kNoEntity && mine < 22.0f) aiAttack(q, foe);
            else aiPost(q, {g.pole.x, g.pole.y + (nearer == 0 ? 5.0f : -5.0f), 0}, toRiver);
            return true;
        }
    }

    // A squad cut to pieces falls back behind its bowmen (not on Easy, where the broken fight on).
    if (aiSkill_ != AiSkill::Easy && aiShattered(s) && memo.flank < 2) {
        release();
        aiSteer(q, {g.pole.x + side * (kBermM + 26.0f), g.pole.y, 0});
        return true;
    }
    // On Easy the rest stand a while first (Battle::kEasyWaitTicks: the player has time to set his line).
    if (aiSkill_ == AiSkill::Easy && world_->tick() < kEasyWaitTicks && memo.flank == 0) return true;
    // The rest go round. Which bridge: on Easy a squad to each in turn; else the foot by the one whose far end has the fewer
    // of the foe about it (the upstream one when they are alike), the horse by the other. Once it has set off it keeps to it.
    if (memo.bridge < 0) {
        int nth = 0;
        for (size_t k = 0; k < q; ++k) nth += squads_[k].owner == s.owner && aliveCount(squads_[k]) > 0 && roleOf(squads_[k].type) == Role::Melee && squads_[k].type != UnitType::Pikemen;
        int quiet = 0;
        if (aiSkill_ == AiSkill::Easy) {
            quiet = nth % 2;
        } else {
            int about[2] = {0, 0};
            for (const sim::Unit& u : world_->units()) {
                if (!u.alive() || u.owner == s.owner || u.routed) continue;
                for (int b = 0; b < 2; ++b) about[b] += flatDist(toV3(u.pos), {g.pole.x - side * (g.bridges[b].reach + kBridgeBack), g.bridges[b].y, 0}) < 45.0f;
            }
            quiet = about[1] < about[0] ? 1 : 0;
            if (stats.mounted) quiet = 1 - quiet;
        }
        memo.bridge = quiet;
        memo.flank = 0;
        memo.waited = 0;
    }
    const Isle::Bridge& b = g.bridges[memo.bridge];
    const V3 ownEnd{g.pole.x + side * (b.reach + kBridgeBack), b.y, 0}, farEnd{g.pole.x - side * (b.reach + kBridgeBack + 6.0f), b.y, 0};
    if (memo.flank == 0) {
        if (flatDist(c, ownEnd) > 14.0f) {
            release();
            aiSteer(q, ownEnd);
            return true;
        }
        // At its end of the bridge: wait for the others going this way (not on Easy), then over.
        bool strung = false;
        for (size_t k = 0; k < squads_.size() && aiSkill_ != AiSkill::Easy; ++k) {
            const Squad& f = squads_[k];
            if (k == q || f.owner != s.owner || aliveCount(f) == 0 || k >= aiMemo_.size() || aiMemo_[k].bridge != memo.bridge || aiMemo_[k].flank != 0) continue;
            strung = strung || (!aiShattered(f) && flatDist(squadCentre(f, false), ownEnd) > 40.0f);
        }
        if (strung && ++memo.waited <= kCloseUpLooks) {
            aiHalt(q);
            return true;
        }
        // And for the other host to come forward: over a bridge alone at a host standing whole in its camp is a squad thrown away.
        int foes = 0, forward = 0;
        for (const sim::Unit& u : world_->units()) {
            if (!u.alive() || u.owner == s.owner || u.routed) continue;
            ++foes;
            forward += east(toV3(u.pos)) > -kForwardM;
        }
        if (forward * 100 < foes * kCommittedPct) {
            aiPost(q, ownEnd + V3{side * 10.0f, 0, 0}, toRiver);  // behind the pikes' place, on Hold
            return true;
        }
        // Nor is a far end that is held stormed over 6 m of deck (kHeldMen of the foe standing within 25 m of it): it goes
        // by the other bridge if that one's far end is open, and else keeps its own end.
        const auto held = [&](int bridge) {
            const V3 end{g.pole.x - side * (g.bridges[bridge].reach + kBridgeBack), g.bridges[bridge].y, 0};
            int men = 0;
            for (const sim::Unit& u : world_->units()) men += u.alive() && u.owner != s.owner && !u.routed && flatDist(toV3(u.pos), end) < 25.0f;
            return men >= kHeldMen;
        };
        if (held(memo.bridge)) {
            if (!held(1 - memo.bridge)) {
                memo.bridge = 1 - memo.bridge;
                memo.waited = 0;
                release();
                aiSteer(q, {g.pole.x + side * (g.bridges[memo.bridge].reach + kBridgeBack), g.bridges[memo.bridge].y, 0});
            } else {
                aiPost(q, ownEnd + V3{side * 10.0f, 0, 0}, toRiver);
            }
            return true;
        }
        memo.flank = 1;
    }
    if (memo.flank == 1) {
        if (east(c) > -(b.reach + 2.0f)) {
            aiSteer(q, farEnd);  // (a march, not a forced one: who bars the deck is fought)
            return true;
        }
        memo.flank = 2;
    }
    // Across: at the nearest of the foe's squads standing on his bank's dry ground, fighting what is within 45 m on the way.
    V3 goal = {g.pole.x - side * kIsleCampM, g.pole.y, 0};
    float best = 1e30f;
    for (const Squad& t : squads_) {
        if (t.owner == s.owner || aliveCount(t) == 0) continue;
        const V3 tc = squadCentre(t, false);
        const float d = flatDist(tc, c);
        if (east(tc) < -g.bank && d < best) best = d, goal = tc;
    }
    release();
    aiFight(q, goal, c, 45.0f);
    return true;
}

}  // namespace illyria::game
