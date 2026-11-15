// `illyria_balance tactics`, the whole battles (docs/BALANCE.md, "Land, formations, calls"): a Skirmish as the game sets
// it up (the open field at 450 gold, the Ford's classic muster), the computer on Normal, played headless through the
// Battle's own orders by four players: one who gives no order, one who plans a defence, one who plans and then commands
// it, and one who sends everyone at the foe at once. The same code the desktop and the page run; the seed is the world's.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"

using namespace illyria;
using namespace illyria::game;

namespace {

enum class Plan { None, Defend, Micro, Charge };
const char* planName(Plan p) {
    switch (p) {
        case Plan::None: return "no orders";
        case Plan::Defend: return "planned defence";
        case Plan::Micro: return "plan plus micro";
        case Plan::Charge: return "reckless charge";
    }
    return "";
}

sim::Fixed fx(float v) { return sim::Fixed::fromRatio(static_cast<int64_t>(std::lround(v * 256.0f)), 256); }

struct Count {
    int mine = 0, foe = 0;
};
Count living(const Battle& b) {
    Count c;
    for (const sim::Unit& u : b.world().units())
        if (u.present() && u.alive()) (u.owner == Battle::kPlayer ? c.mine : c.foe) += 1;
    return c;
}

bool mounted(const Squad& s) { return sim::unitStats(s.type).mounted; }
bool flier(const Squad& s) { return roleOf(s.type) == Role::Flier; }
bool isFoot(const Squad& s) { return roleOf(s.type) == Role::Melee && !mounted(s) && s.type != sim::UnitType::Hero; }
bool isPike(const Squad& s) { return sim::unitStats(s.type).melee.bonusVsMountedPct > 0 && !mounted(s); }

V3 centreOf(const Battle& b, sim::PlayerId owner) {
    V3 sum;
    int n = 0;
    for (const sim::Unit& u : b.world().units())
        if (u.present() && u.alive() && u.owner == owner) sum += V3{static_cast<float>(u.pos.x.toDouble()), static_cast<float>(u.pos.y.toDouble()), 0}, ++n;
    return n ? sum * (1.0f / static_cast<float>(n)) : V3{};
}

sim::Ground groundAt(const Battle& b, const V3& p) {
    const sim::Region* r = b.world().region();
    return r ? r->groundAt(fx(p.x), fx(p.y)) : sim::Ground::Plains;
}
float heightAt(const Battle& b, const V3& p) { return static_cast<float>(b.world().groundHeight(fx(p.x), fx(p.y)).toDouble()); }
bool wet(sim::Ground g) { return g == sim::Ground::Shallows || g == sim::Ground::DeepWater; }

// The line a defence is set on: the near bank of the water between the armies (a few strides back from it), or with no
// water the best height within a bowshot of where the army stands; and the way to face there.
struct Ground {
    V3 line, front;
    bool bank = false;
    bool bridge = false;
    V3 bridgeEnd;
};
Ground readGround(const Battle& b) {
    Ground g;
    const V3 mine = centreOf(b, Battle::kPlayer), foe = centreOf(b, Battle::kEnemy);
    g.front = V3{foe.x - mine.x, foe.y - mine.y, 0}.normalized();
    const float span = V3{foe.x - mine.x, foe.y - mine.y, 0}.length();
    g.line = mine;
    for (float d = 0; d < span; d += 2.0f) {
        const V3 p = mine + g.front * d;
        if (wet(groundAt(b, p))) {
            g.line = mine + g.front * std::max(0.0f, d - 7.0f);
            g.bank = true;
            break;
        }
    }
    const V3 side{-g.front.y, g.front.x, 0};
    if (g.bank) {
        // A bridge within reach of the line: its near end is the pikes' post.
        float best = 1e9f;
        for (float across = -160.0f; across <= 160.0f; across += 4.0f)
            for (float along = -10.0f; along <= 40.0f; along += 2.0f) {
                const V3 p = g.line + side * across + g.front * along;
                const sim::Region* r = b.world().region();
                if (groundAt(b, p) != sim::Ground::Bridge || !r->underWater(fx(p.x), fx(p.y))) continue;
                const float score = std::fabs(across) + along;
                if (score < best) best = score, g.bridge = true, g.bridgeEnd = p - g.front * 9.0f;
            }
    } else {
        // No water: the highest ground near, a little toward the foe.
        float best = heightAt(b, mine);
        for (float along = -30.0f; along <= 60.0f; along += 6.0f)
            for (float across = -60.0f; across <= 60.0f; across += 6.0f) {
                const V3 p = mine + g.front * along + side * across;
                if (const float h = heightAt(b, p); h > best + 0.5f && h < heightAt(b, mine) + 7.0f && groundAt(b, p) == sim::Ground::Plains) best = h, g.line = p;
            }
        // The line faces down the slope it stands on (the foe must climb to it), if there is a slope to speak of.
        const V3 fall{heightAt(b, g.line - V3{6, 0, 0}) - heightAt(b, g.line + V3{6, 0, 0}), heightAt(b, g.line - V3{0, 6, 0}) - heightAt(b, g.line + V3{0, 6, 0}), 0};
        if (fall.length() > 0.6f) g.front = fall.normalized();
    }
    return g;
}

void send(Battle& b, int squad, const V3& to, const V3& face) {
    b.selectSquad(squad);
    b.orderMoveFacing(to, face);
}

// The planned defence: foot in a line on the ground chosen, pikes at the bridge's end, bowmen and casters a rank behind,
// healers and the lord behind them, horse and fliers on the wings with no order (they answer whoever calls).
void deployDefence(Battle& b, const Ground& g) {
    const V3 side{-g.front.y, g.front.x, 0};
    std::vector<int> foot, pikes, shooters, rear, wings;
    bool atBridge = false;
    for (size_t i = 0; i < b.squads().size(); ++i) {
        const Squad& s = b.squads()[i];
        if (s.owner != Battle::kPlayer || s.garrison) continue;
        const int q = static_cast<int>(i);
        const Role r = roleOf(s.type);
        if (g.bridge && isPike(s) && !atBridge) send(b, q, g.bridgeEnd, g.front), atBridge = true;
        else if (isFoot(s) && isPike(s)) pikes.push_back(q);
        else if (isFoot(s)) foot.push_back(q);
        else if (r == Role::Ranged || r == Role::Caster || r == Role::Bomber) shooters.push_back(q);
        else if (mounted(s) || flier(s) || r == Role::Scout) wings.push_back(q);
        else if (r != Role::Siege) rear.push_back(q);
    }
    const auto rank = [&](const std::vector<int>& squads, float back, float gap) {
        for (size_t k = 0; k < squads.size(); ++k) {
            const float across = (static_cast<float>(k) - (static_cast<float>(squads.size()) - 1) / 2) * gap;
            send(b, squads[k], g.line + side * across - g.front * back, g.front);
        }
    };
    // (The pikes braced on the ends of the line, where horse comes.)
    for (size_t k = 0; k < pikes.size(); ++k) foot.insert(k % 2 ? foot.end() : foot.begin(), pikes[k]);
    rank(foot, 0, 15.0f);
    rank(shooters, 13.0f, 15.0f);
    rank(rear, 24.0f, 10.0f);
    const float half = (static_cast<float>(std::max<size_t>(foot.size(), 1)) - 1) / 2 * 15.0f + 24.0f;
    for (size_t k = 0; k < wings.size(); ++k) {
        const float across = (k % 2 ? -1.0f : 1.0f) * (half + static_cast<float>(k / 2) * 12.0f);
        send(b, wings[k], g.line + side * across - g.front * 16.0f, g.front);
    }
    b.select({});
}

// The formations of the plan, looked at every second: the foot in loose order while arrows are all that reaches it, two
// ranks closed up when a foe comes within a charge of it (the rules of world_formation.cpp, used as the manual says).
void formUp(Battle& b) {
    for (size_t i = 0; i < b.squads().size(); ++i) {
        const Squad& s = b.squads()[i];
        if (s.owner != Battle::kPlayer || s.garrison || !isFoot(s) || b.aliveCount(s) < 4) continue;
        const bool near = b.nearestFoe(Battle::kPlayer, b.squadCentre(s, false), 34.0f) != sim::kNoEntity;
        const Formation want = near ? Formation::DoubleLine : Formation::Loose;
        if (s.formation == want) continue;
        b.selectSquad(static_cast<int>(i));
        b.setFormation(want);
    }
    b.select({});
}

// Command on top of the plan, every few seconds: a squad cut to a third is pulled out behind the line; the lord's Rally
// when broken men can hear it.
void micro(Battle& b, const Ground& g, uint32_t& lastCall) {
    for (size_t i = 0; i < b.squads().size(); ++i) {
        const Squad& s = b.squads()[i];
        if (s.owner != Battle::kPlayer || s.garrison || s.type == sim::UnitType::Hero) continue;
        int alive = 0, engaged = 0;
        for (sim::EntityId id : s.members) {
            const sim::Unit& u = b.world().units()[id - 1];
            alive += u.alive();
            engaged += u.alive() && u.engaged;
        }
        if (alive == 0 || engaged == 0 || alive * 3 > static_cast<int>(s.members.size())) continue;
        (void)g;
    }
    // A call for help: the nearest squad with nothing in hand is sent (horse held on the wing go by the flank).
    if (!b.calls().empty() && b.calls().back().tick != lastCall) {
        lastCall = b.calls().back().tick;
        V3 at;
        b.answerCall(at);
    }
    b.select({});
    if (b.rallyState().lord && b.rallyState().cooldown == 0 && b.rallyState().heard >= 4) b.orderRally();
}

struct Tally {
    int killed = 0, lost = 0;  // when the battle was decided (or at kEndSeconds)
    int outcome = 0;
    int seconds = 0;
    std::vector<std::string> untouched;  // the player's squads that never struck, shot or lost a man
    int charges[3] = {0, 0, 0};          // the player's charges home: front, flank, rear
    int foeCharges[3] = {0, 0, 0};
    int calls = 0, answers = 0;
    // TAC_DIAG: what the battle is made of (docs/BALANCE.md, "Why formations did not matter").
    int arrows[2] = {0, 0}, arrowFoe[2] = {0, 0}, arrowFriend[2] = {0, 0};  // by the shooter's side: 0 the player
    int flankBlows[2] = {0, 0}, rearBlows[2] = {0, 0}, frontBlows[2] = {0, 0};  // melee blows landed on side s's men
    int bowAnswers = 0, bowAnswersMelee = 0;  // the player's answers by bow squads, and of those to a call that was not "shot"
    std::vector<std::string> breaks;          // "t side squad lost% why" as each squad first breaks (half its men routed)
    double engagedShare = 0;                  // of the men of squads in a melee, the share striking, sampled each second
    int engagedSamples = 0;
};

constexpr int kEndSeconds = 360;

Tally fight(Scenario scenario, int budget, Plan plan, uint64_t seed) {
    SkirmishSetup setup;
    setup.budget = budget;
    setup.hard = std::getenv("TAC_SKILL") && std::atoi(std::getenv("TAC_SKILL")) == 2;
    setup.easy = std::getenv("TAC_SKILL") && std::atoi(std::getenv("TAC_SKILL")) == 0;
    Battle b(seed, scenario, setup);
    b.setAiSkill(std::getenv("TAC_SKILL") ? static_cast<Battle::AiSkill>(std::atoi(std::getenv("TAC_SKILL"))) : Battle::AiSkill::Normal);
    const Count start = living(b);
    const Ground g = readGround(b);
    if (plan == Plan::Defend || plan == Plan::Micro) deployDefence(b, g);
    uint32_t lastCall = 0;
    if (plan == Plan::Charge) {
        std::vector<sim::EntityId> all;
        for (const sim::Unit& u : b.world().units())
            if (u.present() && u.alive() && u.owner == Battle::kPlayer) all.push_back(u.id);
        b.select(all);
        b.orderMove(centreOf(b, Battle::kEnemy));
        b.select({});
    }
    std::vector<uint8_t> fought(b.squads().size(), 0);
    Tally t;
    const bool diag = std::getenv("TAC_DIAG") != nullptr;
    // (TAC_SHIFT=n delays every order the scripted player gives by n ticks: the same plan, its timing moved.)
    const int shift = std::getenv("TAC_SHIFT") ? std::atoi(std::getenv("TAC_SHIFT")) : 0;
    std::map<sim::EntityId, sim::PlayerId> arrowOwner;
    std::vector<uint8_t> broke(b.squads().size(), 0);
    std::vector<int> squadStart(b.squads().size(), 0);
    std::vector<uint32_t> lastFlanked(b.squads().size(), 0);
    for (size_t i = 0; i < b.squads().size(); ++i) squadStart[i] = b.aliveCount(b.squads()[i]);
    for (int tick = 0; tick < kEndSeconds * sim::kTicksPerSecond; ++tick) {
        b.advance(1.0 / sim::kTicksPerSecond + 1e-6);
        if (plan == Plan::Micro && (tick - shift) % sim::kTicksPerSecond == 0 && tick > shift) micro(b, g, lastCall);
        if (diag) {
            const sim::World& w = b.world();
            for (const sim::Projectile& p : w.projectiles())
                if (p.kind == sim::ProjectileKind::Arrow && !p.thrown && !arrowOwner.count(p.id)) arrowOwner[p.id] = p.owner, ++t.arrows[p.owner == Battle::kPlayer ? 0 : 1];
            for (const sim::ImpactEvent& e : w.impacts()) {
                if (e.kind != sim::ImpactEvent::Kind::Hit && e.kind != sim::ImpactEvent::Kind::Blocked) continue;
                const sim::Unit* v = e.hitUnit != sim::kNoEntity ? &w.units()[e.hitUnit - 1] : nullptr;
                if (v == nullptr) continue;
                const int vs = v->owner == Battle::kPlayer ? 0 : 1;
                if (e.projectile != sim::kNoEntity) {
                    auto it = arrowOwner.find(e.projectile);
                    if (it == arrowOwner.end()) continue;
                    const int ss = it->second == Battle::kPlayer ? 0 : 1;
                    (ss == vs ? t.arrowFriend : t.arrowFoe)[ss] += 1;
                } else if (e.kind == sim::ImpactEvent::Kind::Hit) {
                    const sim::HitSide side = sim::hitSide(v->facing, e.dir);
                    (side == sim::HitSide::Front ? t.frontBlows : side == sim::HitSide::Flank ? t.flankBlows : t.rearBlows)[vs] += 1;
                }
            }
            for (const sim::TacticEvent& e : w.tactics())
                if (e.kind == sim::TacticEvent::Kind::Flanked || e.kind == sim::TacticEvent::Kind::Rear || e.kind == sim::TacticEvent::Kind::Broken) {
                    const int q = b.squadOfNumber(w.units()[e.unit - 1].squad);
                    if (q >= 0) lastFlanked[static_cast<size_t>(q)] = static_cast<uint32_t>(tick);
                }
            for (const sim::HelpCall& c : w.helpCalls())
                if (c.owner == Battle::kPlayer && c.answer != 0) {
                    const int q = b.squadOfNumber(c.answer);
                    if (q >= 0 && sim::unitStats(b.squads()[static_cast<size_t>(q)].type).ranged.muzzleSpeed > sim::Fixed::zero()) {
                        ++t.bowAnswers;
                        t.bowAnswersMelee += c.why != sim::HelpCall::Why::Shot;
                    }
                }
            if (tick % sim::kTicksPerSecond == 0) {
                int inMelee = 0, striking = 0;
                for (size_t i = 0; i < b.squads().size(); ++i) {
                    const Squad& s = b.squads()[i];
                    int alive = 0, routed = 0, eng = 0;
                    for (sim::EntityId id : s.members) {
                        const sim::Unit& u = w.units()[id - 1];
                        alive += u.alive(), routed += u.alive() && u.routed, eng += u.alive() && u.engaged;
                    }
                    if (eng > 0) inMelee += alive - routed, striking += eng;
                    if (!broke[i] && alive > 0 && routed * 2 >= alive && squadStart[i] > 0) {
                        broke[i] = 1;
                        const int lostPct = 100 - 100 * alive / squadStart[i];
                        // Why: flanked in the last 5 s; a friend of its side broke within 10 s before it; else its losses.
                        std::string why = tick - static_cast<int>(lastFlanked[i]) <= 5 * sim::kTicksPerSecond && lastFlanked[i] != 0 ? "flanked" : "";
                        char line[160];
                        std::snprintf(line, sizeof line, "%3d %s %s %d%%%s", tick / sim::kTicksPerSecond, s.owner == Battle::kPlayer ? "mine" : "foe ",
                                      s.name.c_str(), lostPct, why.empty() ? "" : (" " + why).c_str());
                        t.breaks.push_back(line);
                    }
                }
                if (inMelee > 0) t.engagedShare += static_cast<double>(striking) / inMelee, ++t.engagedSamples;
            }
        }
        if (tick % 5 == 0)
            for (size_t i = 0; i < b.squads().size() && i < fought.size(); ++i)
                for (sim::EntityId id : b.squads()[i].members) {
                    const sim::Unit& u = b.world().units()[id - 1];
                    fought[i] = fought[i] || !u.alive() || u.engaged || u.reloadRemaining > 0 || u.hp < u.stats.maxHp;
                }
        for (const sim::TacticEvent& e : b.world().tactics()) {
            if (e.kind != sim::TacticEvent::Kind::Charge) continue;
            (e.owner == Battle::kPlayer ? t.foeCharges : t.charges)[std::min(2, e.detail)] += 1;  // (owner: the man struck)
        }
        for (const sim::HelpCall& c : b.world().helpCalls())
            if (c.owner == Battle::kPlayer) ++t.calls, t.answers += c.answer != 0;
        t.seconds = (tick + 1) / sim::kTicksPerSecond;
        if (b.outcome() != 0) break;  // decided (a rout is the end: the game stops the clock there)
    }
    const Count end = living(b);
    t.killed = start.foe - end.foe, t.lost = start.mine - end.mine;
    t.outcome = b.outcome();
    for (size_t i = 0; i < b.squads().size() && i < fought.size(); ++i)
        if (b.squads()[i].owner == Battle::kPlayer && !fought[i] && roleOf(b.squads()[i].type) != Role::Siege) t.untouched.push_back(b.squads()[i].name);
    return t;
}

double ratio(int killed, int lost) { return static_cast<double>(killed) / std::max(1, lost); }

}  // namespace

// The ground of a map as the plans read it, in letters (4 m a letter), with each side's squads: for setting a plan.
void tacticsMap(bool ford, int plan, int seconds) {
    SkirmishSetup setup;
    setup.budget = ford ? 0 : 450;
    Battle b(5, ford ? Scenario::Ford : Scenario::Field, setup);
    const Ground g = readGround(b);
    if (plan == 1) deployDefence(b, g);
    for (int t = 0; t < seconds * sim::kTicksPerSecond; ++t) b.advance(1.0 / sim::kTicksPerSecond + 1e-6);
    const V3 mine = centreOf(b, Battle::kPlayer), foe = centreOf(b, Battle::kEnemy);
    const V3 mid = (mine + foe) * 0.5f;
    std::printf("mine %.0f,%.0f foe %.0f,%.0f line %.0f,%.0f bank %d bridge %d at %.0f,%.0f\n", mine.x, mine.y, foe.x, foe.y, g.line.x, g.line.y, g.bank, g.bridge,
                g.bridgeEnd.x, g.bridgeEnd.y);
    const int half = 40;
    std::vector<std::string> rows(static_cast<size_t>(2 * half + 1), std::string(static_cast<size_t>(2 * half + 1), ' '));
    for (int j = -half; j <= half; ++j)
        for (int i = -half; i <= half; ++i) {
            const V3 p = mid + V3{static_cast<float>(i) * 4.0f, static_cast<float>(j) * 4.0f, 0};
            static const char kLetters[] = ".=T%~#^Hm:";
            const int h = static_cast<int>(heightAt(b, p));
            char c = kLetters[static_cast<int>(groundAt(b, p))];
            if (c == '.') c = static_cast<char>('0' + std::clamp((h - static_cast<int>(heightAt(b, g.line))) / 2 + 1, 0, 9));  // (1: level with the line; each step 2 m)
            rows[static_cast<size_t>(half - j)][static_cast<size_t>(i + half)] = c;
        }
    for (size_t q = 0; q < b.squads().size(); ++q) {
        const Squad& s = b.squads()[q];
        if (b.aliveCount(s) == 0) continue;
        const V3 c = b.squadCentre(s, false);
        const int i = static_cast<int>(std::lround((c.x - mid.x) / 4.0f)), j = static_cast<int>(std::lround((c.y - mid.y) / 4.0f));
        if (std::abs(i) > half || std::abs(j) > half) continue;
        rows[static_cast<size_t>(half - j)][static_cast<size_t>(i + half)] = static_cast<char>((s.owner == Battle::kPlayer ? 'A' : 'a') + static_cast<int>(q % 26));
        int freeMen = 0, helping = 0, posted = 0, supporting = 0, fighting = 0;
        for (sim::EntityId id : s.members) {
            const sim::Unit& u = b.world().units()[id - 1];
            if (!u.alive()) continue;
            freeMen += b.world().freeToAnswer(u), helping += u.helping != sim::kNoEntity, posted += u.posted, supporting += u.support != sim::kNoEntity, fighting += u.engaged;
        }
        std::printf("%c %s (%s) %d at %.0f,%.0f h %.1f  free %d answering %d posted %d supporting %d fighting %d\n", (s.owner == Battle::kPlayer ? 'A' : 'a') + static_cast<int>(q % 26),
                    s.name.c_str(), sim::unitTypeName(s.type), b.aliveCount(s), c.x, c.y, heightAt(b, c), freeMen, helping, posted, supporting, fighting);
    }
    for (const std::string& r : rows) std::printf("%s\n", r.c_str());
}

void tacticsBattles(int seeds) {
    struct Map {
        const char* name;
        Scenario scenario;
        int budget;
    };
    const Map maps[] = {{"the open field, 450 gold", Scenario::Field, 450}, {"the Ford, classic muster", Scenario::Ford, 0}};
    const Plan plans[] = {Plan::None, Plan::Defend, Plan::Micro, Plan::Charge};
    // (TAC_MAP=field|ford and TAC_PLAN=0..3 in the environment run one of them.)
    const char* onlyMap = std::getenv("TAC_MAP");
    const char* onlyPlan = std::getenv("TAC_PLAN");
    for (const Map& map : maps) {
        if (onlyMap && (map.scenario == Scenario::Ford) != (onlyMap[1] == 'o')) continue;
        std::printf("\n%s, Normal, %d seeds: kills per loss when the battle is decided (%d s at most)\n", map.name, seeds, kEndSeconds);
        for (const Plan plan : plans) {
            if (onlyPlan && static_cast<int>(plan) != std::atoi(onlyPlan)) continue;
            int k = 0, l = 0, secs = 0, won = 0, lostBattles = 0, ch[3] = {0, 0, 0}, fch[3] = {0, 0, 0}, calls = 0, answers = 0;
            std::string idle;
            for (int s = 0; s < seeds; ++s) {
                const Tally t = fight(map.scenario, map.budget, plan, 5 + static_cast<uint64_t>(s));
                if (std::getenv("TAC_DIAG")) {
                    std::printf("    seed %d: %s %d for %d at %d s; arrows mine %d (foe hit %d, own hit %d), theirs %d (%d, %d); blows on mine front/flank/rear %d/%d/%d, on theirs %d/%d/%d;"
                                " bow answers %d (to melee calls %d); striking share in a melee %.0f%%\n",
                                s, t.outcome == 1 ? "won" : t.outcome == 2 ? "lost" : "undecided", t.killed, t.lost, t.seconds, t.arrows[0], t.arrowFoe[0], t.arrowFriend[0], t.arrows[1],
                                t.arrowFoe[1], t.arrowFriend[1], t.frontBlows[0], t.flankBlows[0], t.rearBlows[0], t.frontBlows[1], t.flankBlows[1], t.rearBlows[1], t.bowAnswers,
                                t.bowAnswersMelee, 100 * t.engagedShare / std::max(1, t.engagedSamples));
                    std::string line;
                    for (const std::string& br : t.breaks) line += "[" + br + "] ";
                    std::printf("      breaks: %s\n", line.c_str());
                }
                k += t.killed, l += t.lost, secs += t.seconds;
                won += t.outcome == 1, lostBattles += t.outcome == 2;
                calls += t.calls, answers += t.answers;
                for (int i = 0; i < 3; ++i) ch[i] += t.charges[i], fch[i] += t.foeCharges[i];
                if (s == 0)
                    for (const std::string& n : t.untouched) idle += (idle.empty() ? "" : ", ") + n;
                std::fflush(stdout);
            }
            std::printf("  %-16s %4.1f (%d for %d)   won %d lost %d of %d, %d s\n", planName(plan), ratio(k, l), k, l, won, lostBattles, seeds, secs / seeds);
            std::printf("      charges home front/flank/rear: mine %d/%d/%d, the foe's %d/%d/%d; calls %d, answered %d\n", ch[0], ch[1], ch[2], fch[0], fch[1], fch[2], calls,
                        answers);
            std::printf("      squads that never fought (first seed): %s\n", idle.empty() ? "none" : idle.c_str());
        }
    }
}
