// Headless balance harness (docs/BALANCE.md):
//   illyria_balance speeds                      speeds on flat ground and the time to cross a battlefield
//   illyria_balance march                       campaign marches between neighbouring towns
//   illyria_balance matrix [seeds] [gold]       equal-gold armies of every type against every other
//   illyria_balance duel A B [seeds] [gold]     one pairing, seed by seed
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "illyria/sim/campaign.h"
#include "illyria/sim/world.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
Fixed mm(double v) { return Fixed::fromRaw(static_cast<int64_t>(v * 65536.0)); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};

UnitType typeAt(int i) { return static_cast<UnitType>(i); }

// The campaign's price of a soldier, in gold (a squad's price over its size).
double goldPer(UnitType t) { return static_cast<double>(Campaign::price(t)) / std::max(1, Campaign::squadSize(t)); }

// Soldiers of `t` that `gold` buys (at least one).
int soldiersFor(UnitType t, int gold) { return std::max(1, static_cast<int>(std::lround(gold / goldPer(t)))); }

bool ranged(UnitType t) { return unitStats(t).ranged.muzzleSpeed > Fixed::zero(); }

// One army: `n` soldiers of a type in ranks, the front rank at x = front, facing east (dir 1) or west (-1).
struct Force {
    PlayerId side;
    UnitType type;
    int n;
    double front, dir;
};

// Raises both armies, their soldiers interleaved in the order they are spawned (the simulation walks units in
// that order, so a side raised first would strike first), and sends the melee types forward.
void deploy(World& w, const Force& a, const Force& b, bool aFirst) {
    const Force* sides[2] = {aFirst ? &a : &b, aFirst ? &b : &a};
    std::vector<Command> cmds;
    for (int i = 0; i < std::max(a.n, b.n); ++i)
        for (const Force* s : sides) {
            if (i >= s->n) continue;
            const UnitStats st = unitStats(s->type);
            const double gap = 2.0 * st.radius.toDouble() + 0.5;
            const int wide = std::min(s->n, std::max(6, static_cast<int>(40.0 / gap)));
            const int col = i % wide, row = i / wide;
            const double y = (col - (wide - 1) / 2.0) * gap;
            const double x = s->front - s->dir * row * gap;  // later ranks stand behind
            const EntityId id = w.spawnUnit(s->side, {mm(x), mm(y), m(0)}, s->dir > 0 ? kEast : -kEast, st);
            if (ranged(s->type)) continue;  // shooters hold and fire inside their auto range
            Command c;
            c.player = s->side;
            c.unit = id;
            c.type = Command::Type::Move;
            c.target = {mm(0), mm(0), m(0)};
            cmds.push_back(c);
        }
    w.step(cmds);
}

double value(const World& w, PlayerId p) {
    double v = 0;
    for (const Unit& u : w.units())
        if (u.owner == p && u.hp > 0 && !u.routed)
            v += static_cast<double>(u.hp) / u.stats.maxHp * goldPer(typeAt(u.stats.type));
    return v;
}

int alive(const World& w, PlayerId p) {
    int n = 0;
    for (const Unit& u : w.units()) n += u.owner == p && u.hp > 0 && !u.routed;
    return n;
}

struct Result {
    double score;  // side A's share of the value left standing, 0..1
    bool timeout;
    int aLeft, bLeft;
};

// A fights B with `gold` each, 90 m apart. `variant` picks the end of the field A starts at (bit 0) and whether it
// is player 0 or 1 (bit 1): the simulation resolves player 0's orders and blows first, so both are alternated.
Result fight(UnitType a, UnitType b, int gold, uint64_t seed, int variant, int maxSeconds = 150) {
    const bool aWest = variant % 2 == 0;
    const PlayerId pa = static_cast<PlayerId>(variant / 2 % 2), pb = static_cast<PlayerId>(1 - pa);
    WorldConfig cfg;
    cfg.seed = seed;
    World w(cfg);
    const double dist = 45;
    deploy(w, {pa, a, soldiersFor(a, gold), aWest ? -dist : dist, aWest ? 1.0 : -1.0},
           {pb, b, soldiersFor(b, gold), aWest ? dist : -dist, aWest ? -1.0 : 1.0}, variant / 4 % 2 == 0);
    for (int t = 0; t < maxSeconds * kTicksPerSecond; ++t) {
        w.step({});
        if (t % 15 == 0 && (alive(w, pa) == 0 || alive(w, pb) == 0)) break;
    }
    Result r{};
    const double va = value(w, pa), vb = value(w, pb);
    r.timeout = alive(w, pa) > 0 && alive(w, pb) > 0;
    r.aLeft = alive(w, pa);
    r.bLeft = alive(w, pb);
    r.score = va + vb > 0 ? va / (va + vb) : 0.5;
    return r;
}

// Share of the fights A won (a stand-off counts for whoever holds more value), over both sides of the field.
double winRate(UnitType a, UnitType b, int gold, int seeds) {
    double wins = 0;
    for (int s = 0; s < seeds; ++s) {
        const Result r = fight(a, b, gold, 1000 + static_cast<uint64_t>(s), s % 8);
        wins += r.score > 0.5 ? 1.0 : r.score == 0.5 ? 0.5 : 0.0;
    }
    return wins / seeds;
}

void speeds() {
    std::printf("%-20s %6s %10s %10s %7s\n", "type", "stat", "100 m", "90 m field", "x man");
    for (int i = 0; i < kUnitTypeCount; ++i) {
        const UnitType t = typeAt(i);
        World w{WorldConfig{}};
        const EntityId id = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(t));
        Command c;
        c.player = 0;
        c.unit = id;
        c.type = Command::Type::Move;
        c.force = true;
        c.target = {m(100), m(0), m(0)};
        w.step(std::vector<Command>{c});
        int ticks = 0;
        while (ticks < 400 * kTicksPerSecond && w.findUnit(id)->pos.x < m(99)) {
            w.step({});
            ++ticks;
        }
        const double secs = static_cast<double>(ticks) / kTicksPerSecond;
        std::printf("%-20s %6.2f %9.1fs %9.1fs %7.2f\n", unitTypeName(t), unitStats(t).moveSpeed.toDouble(), secs, secs * 0.9,
                    100.0 / secs / 2.4);
    }
}

void march() {
    const Continent cont(20261027);
    Campaign c(cont, 1);
    const UnitType walkers[] = {UnitType::LightInfantry, UnitType::HeavyInfantry, UnitType::Dwarves, UnitType::LightCavalry,
                                UnitType::Giants,        UnitType::Catapult,      UnitType::Dragons};
    const size_t kinds = sizeof(walkers) / sizeof(walkers[0]);
    std::vector<double> nearest;
    std::vector<std::vector<double>> secs(kinds);
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const auto& a = c.cities()[i];
        std::vector<std::pair<int, size_t>> near;  // the three nearest other towns by straight line
        for (size_t j = 0; j < c.cities().size(); ++j) {
            if (j == i) continue;
            const auto& b = c.cities()[j];
            near.push_back({std::max(std::abs(a.cx - b.cx), std::abs(a.cy - b.cy)), j});
        }
        std::sort(near.begin(), near.end());
        nearest.push_back(near[0].first);
        for (size_t k = 0; k < 3; ++k) {
            const auto& b = c.cities()[near[k].second];
            for (size_t w = 0; w < kinds; ++w) {
                const uint32_t id = c.raise(7, a.cx, a.cy, {walkers[w]});
                if (!c.move(id, b.cx, b.cy)) continue;
                const Campaign::Stack* st = nullptr;
                for (const auto& s : c.stacks())
                    if (s.id == id) st = &s;
                if (!st) continue;
                double mp = 0;
                int px = st->cx, py = st->cy;
                for (const auto& [x, y] : st->path) {
                    mp += (x != px && y != py ? 1.5 : 1.0) * Campaign::moveCost(walkers[w], c.terrainAt(x, y));
                    px = x, py = y;
                }
                secs[w].push_back(mp / Campaign::movesPerDay(walkers[w]) * 30.0);
            }
        }
    }
    const auto q = [](std::vector<double> v, double p) {
        std::sort(v.begin(), v.end());
        return v.empty() ? 0.0 : v[static_cast<size_t>(p * (v.size() - 1))];
    };
    std::printf("%d towns; nearest neighbour: median %.0f cells (%.0f m)\n", static_cast<int>(c.cities().size()), q(nearest, 0.5),
                q(nearest, 0.5) * Campaign::kCell);
    std::printf("seconds to the three nearest towns (a day is 30 s):\n%-18s %6s %6s %6s\n", "stack", "p10", "median", "p90");
    for (size_t w = 0; w < kinds; ++w)
        std::printf("%-18s %6.0f %6.0f %6.0f\n", unitTypeName(walkers[w]), q(secs[w], 0.1), q(secs[w], 0.5), q(secs[w], 0.9));
}

void matrix(int seeds, int gold) {
    std::printf("equal gold (%d) a side, %d seeds, both ends of the field; cell = %% of fights the row won\n", gold, seeds);
    std::printf("%-7s", "");
    for (int j = 0; j < kUnitTypeCount; ++j) std::printf("%4.3s", unitTypeName(typeAt(j)));
    std::printf("  mean  men\n");
    static double done[kUnitTypeCount][kUnitTypeCount];
    for (int i = 0; i < kUnitTypeCount; ++i) {
        std::printf("%-7.6s", unitTypeName(typeAt(i)));
        double sum = 0;
        int n = 0;
        for (int j = 0; j < kUnitTypeCount; ++j) {
            if (i == j) {
                std::printf("%4s", ".");
                continue;
            }
            // The fights are the same read from either side, so each pair is fought once.
            const double wr = i < j ? (done[i][j] = winRate(typeAt(i), typeAt(j), gold, seeds)) : 1.0 - done[j][i];
            sum += wr, ++n;
            std::printf("%4d", static_cast<int>(std::lround(wr * 100)));
        }
        std::printf("  %4.0f  %d\n", 100 * sum / n, soldiersFor(typeAt(i), gold));
        std::fflush(stdout);
    }
}

void duel(UnitType a, UnitType b, int seeds, int gold) {
    std::printf("%s (%d) vs %s (%d), %d gold\n", unitTypeName(a), soldiersFor(a, gold), unitTypeName(b), soldiersFor(b, gold), gold);
    for (int s = 0; s < seeds; ++s) {
        const Result r = fight(a, b, gold, 1000 + static_cast<uint64_t>(s), s % 8);
        std::printf("  seed %d: A %.2f  left %d vs %d%s\n", s, r.score, r.aLeft, r.bLeft, r.timeout ? "  (timeout)" : "");
    }
}

// One fight, every few seconds: men standing, routed, and hit points left on each side.
void trace(UnitType a, UnitType b, int gold) {
    WorldConfig cfg;
    cfg.seed = 1000;
    World w(cfg);
    deploy(w, {0, a, soldiersFor(a, gold), -45, 1}, {1, b, soldiersFor(b, gold), 45, -1}, true);
    for (int t = 0; t <= 120 * kTicksPerSecond; ++t) {
        if (t % (5 * kTicksPerSecond) == 0) {
            int n[2] = {0, 0}, r[2] = {0, 0};
            double hp[2] = {0, 0}, x[2] = {0, 0};
            for (const Unit& u : w.units())
                if (u.hp > 0) {
                    n[u.owner]++, r[u.owner] += u.routed, hp[u.owner] += u.hp, x[u.owner] += u.pos.x.toDouble();
                }
            std::printf("t=%3d  men %3d/%3d  routed %3d/%3d  hp %5.0f/%5.0f  mean x %5.1f/%5.1f\n", t / kTicksPerSecond, n[0], n[1],
                        r[0], r[1], hp[0], hp[1], n[0] ? x[0] / n[0] : 0.0, n[1] ? x[1] / n[1] : 0.0);
        }
        w.step({});
    }
}

int byName(const char* s) {
    for (int i = 0; i < kUnitTypeCount; ++i)
        if (std::strncmp(unitTypeName(typeAt(i)), s, std::strlen(s)) == 0) return i;
    return 0;
}
}  // namespace

int main(int argc, char** argv) {
    const char* mode = argc > 1 ? argv[1] : "matrix";
    if (!std::strcmp(mode, "speeds")) speeds();
    else if (!std::strcmp(mode, "march")) march();
    else if (!std::strcmp(mode, "trace") && argc > 3)
        trace(typeAt(byName(argv[2])), typeAt(byName(argv[3])), argc > 4 ? std::atoi(argv[4]) : 60);
    else if (!std::strcmp(mode, "duel") && argc > 3)
        duel(typeAt(byName(argv[2])), typeAt(byName(argv[3])), argc > 4 ? std::atoi(argv[4]) : 6, argc > 5 ? std::atoi(argv[5]) : 60);
    else matrix(argc > 2 ? std::atoi(argv[2]) : 6, argc > 3 ? std::atoi(argv[3]) : 60);
    return 0;
}
