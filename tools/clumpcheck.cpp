// Measures how melee men bunch on open ground (flat plane, the sim's own World, no graphics).
//   illyria_clumpcheck [scenario|all] [seed]     tables every 5 s of game time for the first 60 s
//   illyria_clumpcheck agg [seeds]              one line a scenario, means over the seeds
//   illyria_clumpcheck mirror [seeds]            24 v 24 light infantry, win rate and time to the kill
//   CLUMP_STEADY=1 in the environment: morale shocks are off, so every fight is fought out
//   scenarios: inf24, line12, pikes, archers, heavy24, inf48, outnumber (a third argument: print every n-th second)
// Every figure is sampled each second; the tables show every fifth, the summary line is the mean over the seconds both sides
// still have men in the fight. Lengths in "r" are body radii (two men touching stand 2.0 r apart centre to centre).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

using namespace illyria::sim;

namespace {
double D(Fixed f) { return f.toDouble(); }

int gStep = 5;
bool gDump = false;
bool gSteady = false;  // CLUMP_STEADY=1: nobody breaks, so the fight runs to the last man (morale ends fights in seconds otherwise)
std::vector<int> gDumpAt;

struct Row {
    int sec = 0;
    int alive[2] = {};
    double nnd[2] = {};     // mean nearest-friend distance / radius, all melee men
    double reach[2] = {};   // fraction of living melee men that struck or could strike this tick (u.engaged)
    int maxOn[2] = {};      // most men (of this side) hitting one single foe
    double width[2] = {};   // frontage (p10..p90 of the line axis, scaled to the full range) / starting frontage
    double depth[2] = {};   // p10..p90 along the advance axis, metres
    double engNnd[2] = {};  // mean nearest-friend distance / radius, engaged men only
};

bool meleeMan(const Unit& u) { return u.alive() && u.stats.melee.damage > 0 && u.stats.ranged.muzzleSpeed == Fixed::zero(); }

double span(std::vector<double> v) {
    if (v.size() < 3) return 0;
    std::sort(v.begin(), v.end());
    const double lo = v[v.size() / 10], hi = v[(v.size() * 9 + 9) / 10 - 1];
    return (hi - lo) * 1.25;
}

uint32_t jitterState = 1;
double jit() {
    jitterState = jitterState * 1664525u + 1013904223u;
    return (static_cast<double>(jitterState >> 8) / 16777216.0 - 0.5) * 0.6;
}
Vec3 at(double x, double y) {
    return {Fixed::fromRatio(static_cast<int64_t>(x * 100), 100), Fixed::fromRatio(static_cast<int64_t>(y * 100), 100), Fixed::zero()};
}

struct Scenario {
    const char* name;
    void (*build)(World&, std::vector<Command>&);
};

void place(World& w, std::vector<Command>& cmds, PlayerId side, UnitType t, double x, double y, double tx, double ty) {
    const Vec3 face = side == 0 ? Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()} : Vec3{-Fixed::one(), Fixed::zero(), Fixed::zero()};
    UnitStats st = unitStats(t);
    if (gSteady) st.moraleShockPct = 0;
    const EntityId id = w.spawnUnit(side, at(x + jit(), y + jit()), face, st);
    cmds.push_back({side, id, Command::Type::Move, at(tx, ty)});
}

// `ranks_` ranks of `n` abreast, `gap` metres between files, ranks 2 m apart; the order is to march to `goalX` (the place
// where the lines meet), file for file, and there they fight as they stand.
void ranks(World& w, std::vector<Command>& c, PlayerId side, UnitType t, int n, int ranks_, double gap, double frontX, double cy,
           double goalX) {
    for (int r = 0; r < ranks_; ++r)
        for (int i = 0; i < n; ++i) {
            const double x = side == 0 ? frontX - 2.0 * r : frontX + 2.0 * r;
            const double y = cy + (i - (n - 1) / 2.0) * gap;
            place(w, c, side, t, x, y, goalX, y);
        }
}

void inf24(World& w, std::vector<Command>& c) {
    ranks(w, c, 0, UnitType::LightInfantry, 12, 2, 2.0, 110, 128, 130);
    ranks(w, c, 1, UnitType::LightInfantry, 12, 2, 2.0, 150, 128, 130);
}
void line12(World& w, std::vector<Command>& c) {
    ranks(w, c, 0, UnitType::HeavyInfantry, 12, 1, 2.5, 110, 128, 130);
    ranks(w, c, 1, UnitType::HeavyInfantry, 12, 1, 2.5, 150, 128, 130);
}
void pikes(World& w, std::vector<Command>& c) {
    ranks(w, c, 0, UnitType::Pikemen, 8, 2, 2.0, 120, 128, 120);
    ranks(w, c, 1, UnitType::HeavyCavalry, 10, 1, 2.5, 190, 128, 121);
}
void archers(World& w, std::vector<Command>& c) {
    ranks(w, c, 0, UnitType::LightInfantry, 4, 4, 1.6, 100, 128, 146);  // a block of 16
    ranks(w, c, 1, UnitType::Archers, 8, 2, 2.0, 150, 128, 150);
}
// Two blocks of 24 heavy infantry (6 abreast, 4 deep, 1.6 m between files), the fight that lasts: a press of men.
void heavy24(World& w, std::vector<Command>& c) {
    ranks(w, c, 0, UnitType::HeavyInfantry, 6, 4, 1.6, 110, 128, 130);
    ranks(w, c, 1, UnitType::HeavyInfantry, 6, 4, 1.6, 150, 128, 130);
}
// 48 against 48 light infantry, two ranks of 24 at 1.6 m: a long line meeting a long line.
void inf48(World& w, std::vector<Command>& c) {
    ranks(w, c, 0, UnitType::LightInfantry, 24, 2, 1.6, 110, 128, 130);
    ranks(w, c, 1, UnitType::LightInfantry, 24, 2, 1.6, 150, 128, 130);
}
// 24 against 8 (heavy infantry, so the fight runs): the many must find room round the few.
void outnumber(World& w, std::vector<Command>& c) {
    ranks(w, c, 0, UnitType::LightInfantry, 6, 4, 1.6, 110, 128, 130);
    ranks(w, c, 1, UnitType::HeavyInfantry, 4, 2, 1.6, 150, 128, 130);
}
const Scenario kScenarios[] = {{"inf24", inf24}, {"line12", line12}, {"pikes", pikes}, {"archers", archers}, {"heavy24", heavy24}, {"inf48", inf48}, {"outnumber", outnumber}};

struct Result {
    std::vector<Row> rows;
    int start[2] = {};
    int last[2] = {};
    int halfDeadSec = -1;
};

Result simulate(const Scenario& sc, uint64_t seed, int seconds) {
    jitterState = static_cast<uint32_t>(seed * 2654435761u + 7);
    WorldConfig cfg;
    cfg.seed = seed;
    World w(cfg);
    std::vector<Command> orders;
    sc.build(w, orders);
    Result res;
    double frontage[2] = {1, 1};
    for (int s = 0; s < 2; ++s) {
        std::vector<double> ys;
        int all = 0;
        for (const Unit& u : w.units()) {
            if (u.owner != s) continue;
            ++all;
            if (meleeMan(u)) ys.push_back(D(u.pos.y));
        }
        frontage[s] = std::max(1.0, span(ys));
        res.start[s] = all;
    }
    const int total = res.start[0] + res.start[1];
    w.step(orders);
    for (int sec = 0; sec <= seconds; ++sec) {
        if (sec > 0)
            for (int i = 1; i < kTicksPerSecond; ++i) w.step({});
        if (gDump && std::find(gDumpAt.begin(), gDumpAt.end(), sec) != gDumpAt.end())
            for (const Unit& u : w.units())
                if (u.alive())
                    std::printf("U %d %d %.2f %.2f %.2f %d %d %.2f %.2f\n", sec, static_cast<int>(u.owner), D(u.pos.x), D(u.pos.y), D(u.stats.radius),
                                u.engaged ? 1 : 0, u.routed ? 1 : 0, D(u.facing.x), D(u.facing.y));
        Row row;
        row.sec = sec;
        int dead = 0;
        for (int s = 0; s < 2; ++s) {
            std::vector<const Unit*> men;
            int all = 0;
            for (const Unit& u : w.units())
                if (u.owner == s && u.alive()) {
                    ++all;
                    if (meleeMan(u) && !u.routed) men.push_back(&u);
                }
            row.alive[s] = all;
            dead += res.start[s] - all;
            std::vector<double> ys, xs;
            double sumN = 0, sumE = 0, eng = 0;
            std::map<EntityId, int> on;
            for (const Unit* a : men) {
                double best = 1e9;
                for (const Unit* b : men)
                    if (a != b) best = std::min(best, D((a->pos - b->pos).flat().length()));
                const double nn = men.size() > 1 ? best / D(a->stats.radius) : 0;
                sumN += nn;
                ys.push_back(D(a->pos.y));
                xs.push_back(D(a->pos.x));
                if (a->engaged) {
                    sumE += nn;
                    eng += 1;
                    if (a->foe != kNoEntity) ++on[a->foe];
                }
            }
            const double n = std::max<double>(1, static_cast<double>(men.size()));
            row.nnd[s] = sumN / n;
            row.reach[s] = eng / n;
            row.engNnd[s] = eng > 0 ? sumE / eng : 0;
            for (const auto& [id, k] : on) row.maxOn[s] = std::max(row.maxOn[s], k);
            row.width[s] = span(ys) / frontage[s];
            row.depth[s] = span(xs) / 1.25;
        }
        if (res.halfDeadSec < 0 && dead * 2 >= total) res.halfDeadSec = sec;
        res.rows.push_back(row);
    }
    for (int s = 0; s < 2; ++s) res.last[s] = res.rows.back().alive[s];
    return res;
}

void table(const Scenario& sc, uint64_t seed) {
    const Result r = simulate(sc, seed, 60);
    std::printf("== %s  seed %llu  (side 0 | side 1)  start %d v %d\n", sc.name, static_cast<unsigned long long>(seed), r.start[0], r.start[1]);
    std::printf("  t  alive   nnd/r      inReach    maxOnOne  width(x start)  depth m   engagedNnd/r\n");
    double sum[6] = {};
    int cnt = 0;
    for (const Row& w : r.rows) {
        const bool fighting = w.alive[0] > 0 && w.alive[1] > 0 && (w.reach[0] > 0 || w.reach[1] > 0);
        if (fighting) {
            ++cnt;
            sum[0] += (w.nnd[0] + w.nnd[1]) / 2;
            sum[1] += (w.reach[0] + w.reach[1]) / 2;
            sum[2] += std::max(w.maxOn[0], w.maxOn[1]);
            sum[3] += (w.width[0] + w.width[1]) / 2;
            sum[4] += (w.engNnd[0] + w.engNnd[1]) / 2;
            sum[5] += (w.depth[0] + w.depth[1]) / 2;
        }
        if (w.sec % gStep == 0)
            std::printf("%3d %3d|%-3d %5.2f|%-5.2f %4.2f|%-4.2f %3d|%-3d    %4.2f|%-4.2f     %4.1f|%-4.1f  %5.2f|%-5.2f\n", w.sec, w.alive[0],
                        w.alive[1], w.nnd[0], w.nnd[1], w.reach[0], w.reach[1], w.maxOn[0], w.maxOn[1], w.width[0], w.width[1], w.depth[0],
                        w.depth[1], w.engNnd[0], w.engNnd[1]);
    }
    if (cnt > 0)
        std::printf("  MEAN over %d fighting seconds: nnd %.2f r, in reach %.2f, max on one %.1f, width %.2f x start, depth %.1f m, engaged nnd %.2f r; "
                    "half dead at %d s, left at 60 s %d v %d\n",
                    cnt, sum[0] / cnt, sum[1] / cnt, sum[2] / cnt, sum[3] / cnt, sum[5] / cnt, sum[4] / cnt, r.halfDeadSec, r.last[0], r.last[1]);
}

// One line a scenario over `seeds` seeds: the means of the per-second figures over the seconds both sides were in the fight, the
// peak men on one foe, and how the fights ended.
void aggregate(int seeds) {
    std::printf("%-10s %6s %7s %8s %6s %6s %8s %7s %7s %8s\n", "scenario", "nnd/r", "engNnd", "inReach", "maxOn", "peak", "width", "depth", "halfDead",
                "side0win");
    for (const Scenario& sc : kScenarios) {
        double nnd = 0, eng = 0, reach = 0, maxOn = 0, width = 0, depth = 0, half = 0, wins = 0;
        int secs = 0, peak = 0;
        for (int s = 1; s <= seeds; ++s) {
            const Result r = simulate(sc, static_cast<uint64_t>(s), 60);
            half += r.halfDeadSec < 0 ? 60 : r.halfDeadSec;
            wins += r.last[0] > r.last[1] ? 1.0 : r.last[0] == r.last[1] ? 0.5 : 0.0;
            for (const Row& w : r.rows) {
                if (!(w.alive[0] > 0 && w.alive[1] > 0 && (w.reach[0] > 0 || w.reach[1] > 0))) continue;
                ++secs;
                nnd += (w.nnd[0] + w.nnd[1]) / 2;
                reach += (w.reach[0] + w.reach[1]) / 2;
                maxOn += std::max(w.maxOn[0], w.maxOn[1]);
                peak = std::max(peak, std::max(w.maxOn[0], w.maxOn[1]));
                width += (w.width[0] + w.width[1]) / 2;
                depth += (w.depth[0] + w.depth[1]) / 2;
                eng += (w.engNnd[0] + w.engNnd[1]) / 2;
            }
        }
        const double n = std::max(1, secs);
        std::printf("%-10s %6.2f %7.2f %8.2f %6.2f %6d %8.2f %7.1f %7.1f s %7.2f\n", sc.name, nnd / n, eng / n, reach / n, maxOn / n, peak, width / n, depth / n,
                    half / seeds, wins / seeds);
    }
}

void mirror(int seeds) {
    int w0 = 0, w1 = 0, draws = 0, unfinished = 0;
    double sumSec = 0, sumLeft = 0;
    for (int s = 1; s <= seeds; ++s) {
        const Result r = simulate(kScenarios[0], static_cast<uint64_t>(s), 90);
        if (r.last[0] > r.last[1]) ++w0;
        else if (r.last[1] > r.last[0]) ++w1;
        else ++draws;
        if (r.last[0] > 0 && r.last[1] > 0) ++unfinished;
        sumSec += r.halfDeadSec < 0 ? 90 : r.halfDeadSec;
        sumLeft += r.last[0] + r.last[1];
    }
    std::printf("mirror 24v24 over %d seeds, at 90 s: side 0 wins %d, side 1 wins %d, level %d; both still standing %d; "
                "mean second of half dead %.1f; mean survivors %.1f\n",
                seeds, w0, w1, draws, unfinished, sumSec / seeds, sumLeft / seeds);
}
}  // namespace

int main(int argc, char** argv) {
    gSteady = std::getenv("CLUMP_STEADY") != nullptr;
    const std::string which = argc > 1 ? argv[1] : "all";
    if (which == "agg") {
        aggregate(argc > 2 ? std::atoi(argv[2]) : 20);
        return 0;
    }
    if (which == "mirror") {
        mirror(argc > 2 ? std::atoi(argv[2]) : 40);
        return 0;
    }
    if (which == "dump") {  // dump SCENARIO SEED sec,sec,...: one line a living man, for the pictures (tools/clumpcheck_plot.py)
        gDump = true;
        for (const char* q = argc > 4 ? argv[4] : ""; *q != 0;) {
            gDumpAt.push_back(std::atoi(q));
            while (*q != 0 && *q != ',') ++q;
            if (*q == ',') ++q;
        }
        for (const Scenario& sc : kScenarios)
            if (argc > 2 && std::string(argv[2]) == sc.name) simulate(sc, argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 1, gDumpAt.empty() ? 0 : gDumpAt.back());
        return 0;
    }
    const uint64_t seed = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 1;
    if (argc > 3) gStep = std::atoi(argv[3]);
    for (const Scenario& sc : kScenarios)
        if (which == "all" || which == sc.name) table(sc, seed);
    return 0;
}
