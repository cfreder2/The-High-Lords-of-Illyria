// Why formations and orders do or do not matter (docs/BALANCE.md, "Why formations did not matter"): set pieces on flat
// open ground through the sim's own World, measured, no graphics.
//   illyria_ordersdiag spacing [seeds]   inter-man spacing (distribution) marching and in melee; men fighting vs standing by
//   illyria_ordersdiag frontage [seeds]  the same 24 heavy foot as 24x1 .. 4x6 against a double line of 24
//   illyria_ordersdiag flank [seeds]     a line fight, then a third squad in at the front, the flank or the back
//   illyria_ordersdiag arrows [seeds]    arrows: hits per arrow by the target's speed, its way (across, closing) and range
//   illyria_ordersdiag charge [seeds]    horse at archers, at braced pikes, at foot standing formed, at foot marching
// Every set piece fights both ways round (player 0 and 1 swapped), the seeds as given.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

using namespace illyria::sim;

namespace {

double D(Fixed f) { return f.toDouble(); }
Fixed F(double v) { return Fixed::fromRaw(static_cast<int64_t>(std::llround(v * 65536.0))); }
Vec3 at(double x, double y) { return {F(x), F(y), Fixed::zero()}; }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kNorth{Fixed::zero(), Fixed::one(), Fixed::zero()};

double pct(std::vector<double> v, double p) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5)];
}

struct Block {
    std::vector<EntityId> ids;
};

// `n` men of `t`, `abreast` to a rank, `gap` m between files and ranks, the front rank's middle at (x, y), facing `face`
// (east or west), of squad `squad` in `form`. `steady`: never breaks (for geometry alone).
Block raise(World& w, PlayerId side, UnitType t, int n, int abreast, double gap, double x, double y, double face, uint32_t squad,
            FormationKind form, bool steady = false, int hp = 0) {
    Block b;
    UnitStats st = unitStats(t);
    if (steady) st.moraleShockPct = 0;
    if (hp > 0) st.maxHp = hp;
    for (int i = 0; i < n; ++i) {
        const int row = i / abreast, col = i % abreast;
        const int inRow = std::min(abreast, n - row * abreast);
        const double yy = y + (col - (inRow - 1) / 2.0) * gap;
        const double xx = x - face * row * gap;
        const EntityId id = w.spawnUnit(side, at(xx, yy), face > 0 ? kEast : -kEast, st);
        w.setSquad(id, squad, form);
        b.ids.push_back(id);
    }
    return b;
}

void march(std::vector<Command>& c, PlayerId side, const Block& b, const World& w, double dx, double dy, bool force = false) {
    for (EntityId id : b.ids) {
        const Unit* u = w.findUnit(id);
        Command m;
        m.player = side;
        m.unit = id;
        m.type = Command::Type::Move;
        m.force = force;
        m.target = u->pos + at(dx, dy);
        c.push_back(m);
    }
}

int living(const World& w, const Block& b, bool standing = true) {
    int n = 0;
    for (EntityId id : b.ids) {
        const Unit* u = w.findUnit(id);
        n += u != nullptr && u->alive() && (!standing || !u->routed);
    }
    return n;
}

// ---------------------------------------------------------------------------------------------------------------- spacing

struct SpacingSample {
    std::vector<double> nnd;        // nearest friend, centre to centre, metres (standing melee men)
    std::vector<double> nndFight;   // the same, men in a melee this tick
    int fighting = 0, behind = 0, men = 0;  // engaged; not engaged with no foe within 3 m of their reach; all standing
    std::map<int, int> onOne;        // attackers on one man -> how many men had that many on them
    int touching = 0;                // nearest friend closer than the two bodies' breadth + 0.2 m
};

void sample(const World& w, PlayerId side, SpacingSample& s) {
    std::vector<const Unit*> men;
    for (const Unit& u : w.units())
        if (u.owner == side && u.alive() && !u.routed && u.stats.melee.damage > 0) men.push_back(&u);
    std::map<EntityId, int> on;
    for (const Unit& u : w.units())
        if (u.owner != side && u.alive() && u.engaged && u.foe != kNoEntity) ++on[u.foe];
    for (const Unit* a : men) {
        double best = 1e9;
        for (const Unit* b : men)
            if (a != b) best = std::min(best, D((a->pos - b->pos).flat().length()));
        if (men.size() < 2) continue;
        s.nnd.push_back(best);
        if (best < 2 * D(a->stats.radius) + 0.2) ++s.touching;
        ++s.men;
        if (a->engaged) {
            ++s.fighting;
            s.nndFight.push_back(best);
        } else {
            double foe = 1e9;
            for (const Unit& o : w.units())
                if (o.owner != side && o.alive()) foe = std::min(foe, D((o.pos - a->pos).flat().length()) - D(a->stats.radius) - D(o.stats.radius));
            if (foe > D(a->stats.melee.reach) + 3) ++s.behind;
        }
        auto it = on.find(a->id);
        ++s.onOne[it == on.end() ? 0 : std::min(it->second, 5)];
    }
}

void printSpacing(const char* label, const SpacingSample& s) {
    const double men = std::max(1, s.men);
    std::printf("  %-34s nnd p10/p50/p90 %.2f/%.2f/%.2f m (fighting %.2f/%.2f/%.2f)  touching %3.0f%%  fighting %3.0f%%  standing by %3.0f%%",
                label, pct(s.nnd, 0.1), pct(s.nnd, 0.5), pct(s.nnd, 0.9), pct(s.nndFight, 0.1), pct(s.nndFight, 0.5), pct(s.nndFight, 0.9),
                100 * s.touching / men, 100 * s.fighting / men, 100 * s.behind / men);
    int struck = 0;
    for (const auto& [k, v] : s.onOne)
        if (k > 0) struck += v;
    std::printf("  struck by 1/2/3/4/5+:");
    for (int k = 1; k <= 5; ++k) {
        auto it = s.onOne.find(k);
        std::printf(" %2.0f", 100.0 * (it == s.onOne.end() ? 0 : it->second) / std::max(1, struck));
    }
    std::printf("%%\n");
}

void spacing(int seeds) {
    std::printf("Spacing (steady men: nobody breaks), %d seeds x 2 sides; sampled once a second\n", seeds);
    struct Piece {
        const char* name;
        UnitType a, b;
        int na, abA, nb, abB;
        FormationKind fa, fb;
    };
    const Piece pieces[] = {
        {"light foot 12x2 v 12x2", UnitType::LightInfantry, UnitType::LightInfantry, 24, 12, 24, 12, FormationKind::DoubleLine, FormationKind::DoubleLine},
        {"heavy foot 6x4 v 12x2", UnitType::HeavyInfantry, UnitType::HeavyInfantry, 24, 6, 24, 12, FormationKind::DoubleLine, FormationKind::DoubleLine},
        {"light foot 24 (6x4) v heavy 8 (4x2)", UnitType::LightInfantry, UnitType::HeavyInfantry, 24, 6, 8, 4, FormationKind::DoubleLine, FormationKind::DoubleLine},
    };
    {
        // A squad marching in formation, no foe: the spacing it keeps.
        SpacingSample s;
        for (int seed = 1; seed <= seeds; ++seed) {
            WorldConfig cfg;
            cfg.seed = static_cast<uint64_t>(seed);
            World w(cfg);
            std::vector<Command> c;
            const Block b = raise(w, 0, UnitType::LightInfantry, 24, 12, 1.8, 0, 0, 1, 1, FormationKind::DoubleLine, true);
            march(c, 0, b, w, 40, 0);
            w.step(c);
            for (int t = 1; t <= 20 * kTicksPerSecond; ++t) {
                w.step({});
                if (t % kTicksPerSecond == 0 && t >= 5 * kTicksPerSecond) sample(w, 0, s);
            }
        }
        printSpacing("marching 12x2 at 1.8 m, no foe", s);
    }
    for (const Piece& p : pieces) {
        SpacingSample early, late;
        for (int seed = 1; seed <= seeds; ++seed)
            for (int swap = 0; swap < 2; ++swap) {
                WorldConfig cfg;
                cfg.seed = static_cast<uint64_t>(seed);
                World w(cfg);
                std::vector<Command> c;
                const PlayerId pa = static_cast<PlayerId>(swap), pb = static_cast<PlayerId>(1 - swap);
                const Block a = raise(w, pa, p.a, p.na, p.abA, 1.8, -12, 0, 1, 1, p.fa, true);
                const Block b = raise(w, pb, p.b, p.nb, p.abB, 1.8, 12, 0, -1, 2, p.fb, true);
                march(c, pa, a, w, 30, 0);
                march(c, pb, b, w, -30, 0);
                w.step(c);
                int contact = -1;
                for (int t = 1; t <= 40 * kTicksPerSecond; ++t) {
                    w.step({});
                    if (t % kTicksPerSecond != 0) continue;
                    bool any = false;
                    for (const Unit& u : w.units()) any = any || (u.alive() && u.engaged);
                    if (any && contact < 0) contact = t;
                    if (contact < 0) continue;
                    if (living(w, a) == 0 || living(w, b) == 0) break;
                    SpacingSample& s = t - contact <= 8 * kTicksPerSecond ? early : late;
                    sample(w, pa, s);
                }
            }
        std::printf(" %s\n", p.name);
        printSpacing("first 8 s of the melee (side A)", early);
        printSpacing("after that (side A)", late);
    }
}

// ---------------------------------------------------------------------------------------------------------------- outcome

struct Outcome {
    int killed = 0, lost = 0;  // by side A
    int aWins = 0, fights = 0;
    double brokeAt = 0;        // mean second the loser broke (half its men routed)
    int broke = 0;
    double engagedFirst10 = 0; // share of side A's men that struck a blow in the first 10 s of the melee
};

void print(const char* name, const Outcome& o) {
    std::printf("  %-44s A won %2d of %2d  killed %4d lost %4d  exchange %4.2f  loser broke at %5.1f s  A's men fighting in first 10 s %3.0f%%\n",
                name, o.aWins, o.fights, o.killed, o.lost, static_cast<double>(o.killed) / std::max(1, o.lost), o.broke ? o.brokeAt / o.broke : 0.0,
                100 * o.engagedFirst10 / std::max(1, o.fights));
}

// Fight to the end (or `secs`), A and B blocks; count.
void run(World& w, const Block& a, const Block& b, Outcome& o, int secs, const std::vector<std::pair<int, std::vector<Command>>>& later = {}) {
    const int a0 = static_cast<int>(a.ids.size()), b0 = static_cast<int>(b.ids.size());
    int contact = -1;
    std::vector<uint8_t> struck(a.ids.size(), 0);
    int brokeTick = -1;
    for (int t = 1; t <= secs * kTicksPerSecond; ++t) {
        std::vector<Command> now;
        for (const auto& [when, cmds] : later)
            if (when == t) now.insert(now.end(), cmds.begin(), cmds.end());
        w.step(now);
        for (size_t i = 0; i < a.ids.size(); ++i) {
            const Unit* u = w.findUnit(a.ids[i]);
            if (u->engaged) {
                if (contact < 0) contact = t;
                if (t - contact <= 10 * kTicksPerSecond) struck[i] = 1;
            }
        }
        if (t % 15 == 0) {
            const int la = living(w, a), lb = living(w, b);
            const int aliveA = living(w, a, false), aliveB = living(w, b, false);
            if (brokeTick < 0 && ((aliveA > 0 && la * 2 <= aliveA) || (aliveB > 0 && lb * 2 <= aliveB))) brokeTick = t;
            if (la == 0 || lb == 0) break;
        }
    }
    const int aliveA = living(w, a, false), aliveB = living(w, b, false);
    o.killed += b0 - aliveB;
    o.lost += a0 - aliveA;
    o.aWins += living(w, a) * b0 > living(w, b) * a0 ? 1 : 0;
    ++o.fights;
    if (brokeTick >= 0) o.brokeAt += static_cast<double>(brokeTick) / kTicksPerSecond, ++o.broke;
    int s = 0;
    for (uint8_t k : struck) s += k;
    o.engagedFirst10 += static_cast<double>(s) / std::max(1, a0);
}

void frontage(int seeds) {
    std::printf("Frontage: 24 heavy infantry (A) as 24x1 .. 4x6 against 24 heavy infantry in a double line (12x2); both march on; %d seeds x 2\n", seeds);
    const int abreast[] = {24, 12, 8, 6, 4};
    for (int ab : abreast) {
        Outcome o;
        for (int seed = 1; seed <= seeds; ++seed)
            for (int swap = 0; swap < 2; ++swap) {
                WorldConfig cfg;
                cfg.seed = static_cast<uint64_t>(seed);
                World w(cfg);
                const PlayerId pa = static_cast<PlayerId>(swap), pb = static_cast<PlayerId>(1 - swap);
                std::vector<Command> c;
                const FormationKind f = ab == 24 ? FormationKind::Line : FormationKind::DoubleLine;
                const Block a = raise(w, pa, UnitType::HeavyInfantry, 24, ab, 1.8, -12, 0, 1, 1, f);
                const Block b = raise(w, pb, UnitType::HeavyInfantry, 24, 12, 1.8, 12, 0, -1, 2, FormationKind::DoubleLine);
                march(c, pa, a, w, 30, 0);
                march(c, pb, b, w, -30, 0);
                w.step(c);
                run(w, a, b, o, 120);
            }
        char name[64];
        std::snprintf(name, sizeof name, "A %dx%d", ab, (24 + ab - 1) / ab);
        print(name, o);
    }
}

void flank(int seeds) {
    std::printf("Flank: 16 light infantry (A, 8x2) and 16 (B, 8x2) meet head on; at 6 s of the melee 8 more of A come in at B's front (behind A's\n"
                "line), flank or back. And a flanked squad against a smaller one: 12 v 12 head on, then 6 of A at B's back. %d seeds x 2\n", seeds);
    struct Piece {
        const char* name;
        int n0, nb, extra;
        double ex, ey;  // where the extra men start (B's line meets at x = 0)
    };
    const Piece pieces[] = {{"no extra men", 16, 16, 0, 0, 0},
                            {"8 more in at the front (behind A)", 16, 16, 8, -14, 0},
                            {"8 more in at B's flank", 16, 16, 8, 6, 22},
                            {"8 more in at B's back", 16, 16, 8, 20, 0},
                            {"12 v 12, then 6 of A at B's back", 12, 12, 6, 20, 0},
                            {"12 v 12, 6 of A head on, alone (control)", 0, 12, 6, -10, 0}};
    for (const Piece& p : pieces) {
        Outcome o;
        int flankBlows = 0, rearBlows = 0, flankedTold = 0;
        double moraleDrop = 0;
        int samples = 0;
        for (int seed = 1; seed <= seeds; ++seed)
            for (int swap = 0; swap < 2; ++swap) {
                WorldConfig cfg;
                cfg.seed = static_cast<uint64_t>(seed);
                World w(cfg);
                const PlayerId pa = static_cast<PlayerId>(swap), pb = static_cast<PlayerId>(1 - swap);
                std::vector<Command> c;
                Block a = p.n0 > 0 ? raise(w, pa, UnitType::LightInfantry, p.n0, p.n0 / 2, 1.8, -6, 0, 1, 1, FormationKind::DoubleLine) : Block{};
                const Block b = raise(w, pb, UnitType::LightInfantry, p.nb, p.nb / 2, 1.8, 6, 0, -1, 2, FormationKind::DoubleLine);
                if (!a.ids.empty()) march(c, pa, a, w, 10, 0);
                march(c, pb, b, w, -10, 0);
                Block x;
                if (p.extra > 0) x = raise(w, pa, UnitType::LightInfantry, p.extra, p.extra / 2, 1.8, p.ex, p.ey, p.ex > 10 ? -1 : 1, 3, FormationKind::DoubleLine);
                if (p.n0 == 0) march(c, pa, x, w, 16, 0);  // the control: alone, head on, at once
                w.step(c);
                // The extra men are sent at B's middle 6 s after the lines meet (about 8 s in), as a march: they fight whom they meet.
                std::vector<std::pair<int, std::vector<Command>>> later;
                if (p.extra > 0 && p.n0 > 0) {
                    std::vector<Command> go;
                    for (EntityId id : x.ids) {
                        Command m;
                        m.player = pa;
                        m.unit = id;
                        m.type = Command::Type::Move;
                        m.target = at(2, 0);
                        go.push_back(m);
                    }
                    later.push_back({8 * kTicksPerSecond, go});
                }
                Block all = a;
                all.ids.insert(all.ids.end(), x.ids.begin(), x.ids.end());
                // Watch B's men for 3 s after the extra men strike their first blow.
                // (run, with a look at each tick for the tactic events and B's morale)
                const int a0 = static_cast<int>(all.ids.size());
                int firstExtra = -1;
                double before = 0;
                bool measured = false;
                int brokeTick = -1;
                for (int t = 1; t <= 90 * kTicksPerSecond; ++t) {
                    std::vector<Command> now;
                    for (const auto& [when, cmds] : later)
                        if (when == t) now.insert(now.end(), cmds.begin(), cmds.end());
                    w.step(now);
                    for (const TacticEvent& e : w.tactics())
                        if (e.owner == pb && (e.kind == TacticEvent::Kind::Flanked || e.kind == TacticEvent::Kind::Rear)) ++flankedTold;
                    for (const ImpactEvent& e : w.impacts()) {
                        if (e.kind != ImpactEvent::Kind::Hit || e.projectile != kNoEntity) continue;
                        const Unit* v = w.findUnit(e.hitUnit);
                        if (v == nullptr || v->owner != pb) continue;
                        const HitSide side = hitSide(v->facing, e.dir);
                        flankBlows += side == HitSide::Flank, rearBlows += side == HitSide::Rear;
                    }
                    const auto meanMorale = [&] {
                        double m = 0;
                        int n = 0;
                        for (EntityId id : b.ids)
                            if (const Unit* u = w.findUnit(id); u->alive()) m += u->morale, ++n;
                        return n ? m / n : 0.0;
                    };
                    if (firstExtra < 0 && p.extra > 0)
                        for (EntityId id : x.ids)
                            if (w.findUnit(id)->engaged) {
                                firstExtra = t;
                                before = meanMorale();
                            }
                    if (firstExtra >= 0 && !measured && t == firstExtra + 3 * kTicksPerSecond) {
                        moraleDrop += before - meanMorale();
                        ++samples;
                        measured = true;
                    }
                    if (t % 15 == 0) {
                        const int la = living(w, all), lb = living(w, b);
                        const int aliveB = living(w, b, false);
                        if (brokeTick < 0 && aliveB > 0 && lb * 2 <= aliveB) brokeTick = t;
                        if (la == 0 || lb == 0) break;
                    }
                }
                const int aliveA = living(w, all, false), aliveB = living(w, b, false);
                o.killed += static_cast<int>(b.ids.size()) - aliveB;
                o.lost += a0 - aliveA;
                o.aWins += living(w, all) > living(w, b) ? 1 : 0;
                ++o.fights;
                if (brokeTick >= 0) o.brokeAt += static_cast<double>(brokeTick) / kTicksPerSecond, ++o.broke;
            }
        print(p.name, o);
        std::printf("      B struck in the flank %d, in the back %d (blows); 'Flanked'/'Hit from behind' told %d; B's mean morale lost in the 3 s after the extra men struck: %.0f\n",
                    flankBlows, rearBlows, flankedTold, samples ? moraleDrop / samples : 0.0);
    }
}

// ---------------------------------------------------------------------------------------------------------------- arrows

void arrows(int seeds) {
    std::printf("Arrows: 12 archers standing in two ranks shoot at 8 men who cannot die: standing, walking or galloping across the line of\n"
                "fire at a fixed range, or coming straight at them from 75 m; hits (wounding or caught on a shield) per arrow loosed, by the\n"
                "range to the nearest of them when it was loosed; %d seeds\n", seeds);
    struct Way {
        const char* name;
        UnitType t;
        bool across, still;
        bool block;  // a close block (else a loose file)
    };
    const Way ways[] = {{"foot standing still, close block", UnitType::LightInfantry, true, true, true},
                        {"foot standing still, loose file", UnitType::LightInfantry, true, true, false},
                        {"foot walking across (2.4 m/s)", UnitType::LightInfantry, true, false, false},
                        {"foot walking at them", UnitType::LightInfantry, false, false, true},
                        {"horse standing", UnitType::LightCavalry, true, true, false},
                        {"horse galloping across (7 m/s)", UnitType::LightCavalry, true, false, false},
                        {"horse galloping at them", UnitType::LightCavalry, false, false, true}};
    const int ranges[] = {20, 35, 50, 65};
    std::printf("  %-36s", "range:");
    for (int r : ranges) std::printf("   %3d m", r);
    std::printf("\n");
    for (const Way& wy : ways) {
        std::printf("  %-36s", wy.name);
        int loosed[4] = {}, hits[4] = {};
        for (int ri = 0; ri < (wy.across ? 4 : 1); ++ri)
            for (int seed = 1; seed <= seeds; ++seed) {
                WorldConfig cfg;
                cfg.seed = static_cast<uint64_t>(seed);
                World w(cfg);
                const Block arch = raise(w, 0, UnitType::Archers, 12, 6, 1.8, 0, 0, 1, 1, FormationKind::DoubleLine, true);
                std::vector<Command> c;
                Block tgt;
                const double speed = D(unitStats(wy.t).moveSpeed), range = ranges[ri];
                const double gap = wy.block ? 2.0 : 4.0;
                const int secs = wy.across ? 16 : 24;
                for (int i = 0; i < 8; ++i) {
                    UnitStats st = unitStats(wy.t);
                    st.maxHp = 100000;
                    st.moraleShockPct = 0;
                    st.melee.damage = 0;
                    double x, y;
                    // Across: a file along the way they go, which passes the middle of the archers' line at half time.
                    if (wy.across) x = range + (i % 2) * gap, y = -speed * secs / 2 + (i / 2) * gap;
                    else x = 75 + (i / 4) * gap, y = (i % 4) * gap - 1.5 * gap;
                    const EntityId id = w.spawnUnit(1, at(x, y), wy.across ? kNorth : -kEast, st);
                    tgt.ids.push_back(id);
                    if (!wy.still) {
                        Command m;
                        m.player = 1;
                        m.unit = id;
                        m.type = Command::Type::Move;
                        m.force = true;
                        m.target = wy.across ? at(x, y + speed * secs * 2) : at(8 + (i / 4) * gap, y);
                        c.push_back(m);
                    }
                }
                w.step(c);
                std::map<EntityId, int> bin;  // arrow -> range bin when loosed
                for (int t = 1; t <= secs * kTicksPerSecond; ++t) {
                    w.step({});
                    for (const Projectile& p : w.projectiles()) {
                        if (p.kind != ProjectileKind::Arrow || bin.count(p.id)) continue;
                        double near = 1e9;
                        for (EntityId id : tgt.ids) near = std::min(near, D((w.findUnit(id)->pos - p.pos).flat().length()));
                        const int k = wy.across ? ri : near < 27.5 ? 0 : near < 42.5 ? 1 : near < 57.5 ? 2 : near < 72.5 ? 3 : -1;
                        bin[p.id] = k;
                        if (k >= 0) ++loosed[k];
                    }
                    for (const ImpactEvent& e : w.impacts()) {
                        if ((e.kind != ImpactEvent::Kind::Hit && e.kind != ImpactEvent::Kind::Blocked) || e.projectile == kNoEntity) continue;
                        auto it = bin.find(e.projectile);
                        if (it == bin.end() || it->second < 0) continue;
                        if (std::find(tgt.ids.begin(), tgt.ids.end(), e.hitUnit) != tgt.ids.end()) ++hits[it->second];
                    }
                }
                (void)arch;
            }
        for (int k = 0; k < 4; ++k) {
            if (loosed[k] == 0) std::printf("      --");
            else std::printf("   %3.0f%% ", 100.0 * hits[k] / loosed[k]);
        }
        int all = 0, hit = 0;
        for (int k = 0; k < 4; ++k) all += loosed[k], hit += hits[k];
        std::printf("  (%d arrows, %.0f%%)\n", all, 100.0 * hit / std::max(1, all));
    }
}

// ---------------------------------------------------------------------------------------------------------------- charge

void charge(int seeds) {
    std::printf("Horse: 8 light or heavy cavalry ride from 70 m at a standing body; %d seeds x 2 sides\n", seeds);
    struct Piece {
        const char* name;
        UnitType horse, foot;
        int nh, nf;
        FormationKind form;
        bool footMarches;
    };
    const Piece pieces[] = {{"light horse 8 at 12 archers (holding, shooting)", UnitType::LightCavalry, UnitType::Archers, 8, 12, FormationKind::DoubleLine, false},
                            {"light horse 8 at 12 pikes standing (braced)", UnitType::LightCavalry, UnitType::Pikemen, 8, 12, FormationKind::DoubleLine, false},
                            {"heavy horse 8 at 12 pikes standing (braced)", UnitType::HeavyCavalry, UnitType::Pikemen, 8, 12, FormationKind::DoubleLine, false},
                            {"heavy horse 8 at 16 heavy foot standing, double line", UnitType::HeavyCavalry, UnitType::HeavyInfantry, 8, 16, FormationKind::DoubleLine, false},
                            {"heavy horse 8 at 16 heavy foot marching at them", UnitType::HeavyCavalry, UnitType::HeavyInfantry, 8, 16, FormationKind::DoubleLine, true},
                            {"heavy horse 8 at 16 heavy foot in a box", UnitType::HeavyCavalry, UnitType::HeavyInfantry, 8, 16, FormationKind::Box, false}};
    for (const Piece& p : pieces) {
        Outcome o;
        for (int seed = 1; seed <= seeds; ++seed)
            for (int swap = 0; swap < 2; ++swap) {
                WorldConfig cfg;
                cfg.seed = static_cast<uint64_t>(seed);
                World w(cfg);
                const PlayerId pa = static_cast<PlayerId>(swap), pb = static_cast<PlayerId>(1 - swap);
                std::vector<Command> c;
                const Block h = raise(w, pa, p.horse, p.nh, 4, 2.6, -35, 0, 1, 1, FormationKind::Line);
                const Block f = raise(w, pb, p.foot, p.nf, p.nf / 2, 1.8, 35, 0, -1, 2, p.form);
                march(c, pa, h, w, 80, 0);
                if (p.footMarches) march(c, pb, f, w, -40, 0);
                w.step(c);
                run(w, h, f, o, 90);
            }
        print(p.name, o);
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string mode = argc > 1 ? argv[1] : "all";
    const int seeds = argc > 2 ? std::atoi(argv[2]) : 4;
    if (mode == "spacing" || mode == "all") spacing(seeds);
    if (mode == "frontage" || mode == "all") frontage(seeds);
    if (mode == "flank" || mode == "all") flank(seeds);
    if (mode == "arrows" || mode == "all") arrows(seeds);
    if (mode == "charge" || mode == "all") charge(seeds);
    return 0;
}
