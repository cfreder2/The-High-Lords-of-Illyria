// Territory (engine/sim/src/campaign_territory.cpp) and the small edges of
// battle (edges.h): in the campaign's dice (campaign_rules.cpp) and in the 3D
// battle (world_edges.cpp).
#include <cstdio>

#include "illyria/sim/campaign.h"
#include "illyria/sim/edges.h"
#include "illyria/sim/rng.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
Campaign still(uint64_t seed) {
    Campaign c(continent(), seed);
    for (uint8_t s = 0; s < Continent::kFactionCount; ++s) c.setHuman(s, true);
    return c;
}
// A land hex nobody holds, with no town or outpost in it or round it, and
// one of its cells open ground.
int32_t emptyHex(const Campaign& c, int32_t& cx, int32_t& cy) {
    for (int32_t y = 5; y < c.height() - 5; ++y)
        for (int32_t x = 5; x < c.width() - 5; ++x) {
            const int32_t h = c.hexAt(x, y);
            if (c.terrainAt(x, y) != Lay::Plain || c.hexes()[static_cast<size_t>(h)].owner != Campaign::kNeutral) continue;
            std::vector<int32_t> ring{h};
            Campaign::hexNeighbours(h, ring);
            bool quiet = true;
            for (const Campaign::CityState& t : c.cities())
                for (int32_t n : ring) quiet = quiet && c.hexAt(t.cx, t.cy) != n;
            for (int32_t n : ring) quiet = quiet && c.hexes()[static_cast<size_t>(n)].outpost < 0;
            if (!quiet) continue;
            cx = x;
            cy = y;
            return h;
        }
    return -1;
}
void reckon(Campaign& c, int times) {
    for (int t = 0; t < times * Campaign::kTerritoryTicks; ++t) c.step();
}
}  // namespace

TEST(the_hexes_are_the_maps_grid) {
    const Campaign c = still(1);
    CHECK(c.hexes().size() == static_cast<size_t>(Campaign::kHexCols * Campaign::kHexRows));
    int land = 0;
    for (int32_t h = 0; h < static_cast<int32_t>(c.hexes().size()); ++h) {
        int32_t x, y;
        Campaign::hexCentre(h, x, y);
        CHECK(Campaign::hexOfMetres(x, y) == h);
        // Every neighbour's centre is 400 m away (within a metre: sqrt(3) in centimetres).
        std::vector<int32_t> ring;
        Campaign::hexNeighbours(h, ring);
        for (int32_t n : ring) {
            int32_t nx, ny;
            Campaign::hexCentre(n, nx, ny);
            const int64_t d2 = int64_t(nx - x) * (nx - x) + int64_t(ny - y) * (ny - y);
            CHECK(d2 > 399 * 399 && d2 < 401 * 401);
        }
        land += c.hexes()[static_cast<size_t>(h)].land;
    }
    // A point is in the hex whose centre is nearest.
    CHECK(Campaign::hexOfMetres(210, 10) == 0 || Campaign::hexOfMetres(210, 10) == 1);
    CHECK(Campaign::hexOfMetres(100, 330) == Campaign::kHexCols);       // row 1 (shifted east), column 0
    std::printf("  %zu hexes, %d of them land; %zu outpost towers\n", c.hexes().size(), land, c.outposts().size());
    CHECK(land > 600);
    CHECK(c.outposts().size() >= 8);
    // The realms begin holding the land round their capitals.
    for (const Campaign::CityState& t : c.cities())
        if (t.owner != Campaign::kNeutral) CHECK(c.landOwner(t.cx, t.cy) == t.owner);
}

TEST(armies_claim_the_land_they_stand_on_and_it_fades_when_they_go) {
    Campaign c = still(1);
    int32_t cx = 0, cy = 0;
    const int32_t hex = emptyHex(c, cx, cy);
    CHECK(hex >= 0);
    if (hex < 0) return;
    const Campaign::Hex& h = c.hexes()[static_cast<size_t>(hex)];
    const uint32_t id = c.raise(0, cx, cy, {UnitType::LightInfantry, UnitType::LightInfantry});
    reckon(c, 1);
    CHECK(h.owner == 0);
    CHECK(h.claim == 20);  // 10, and 5 an army
    reckon(c, 10);
    CHECK(h.claim == Campaign::kClaimFull);
    CHECK(c.landOwner(cx, cy) == 0);
    // Gone: it fades a point at each reckoning, ten a day.
    for (const Army& a : std::vector<Army>(c.findStack(id)->armies)) c.disband(id, a.id);
    reckon(c, 10);
    CHECK(h.owner == 0 && h.claim == Campaign::kClaimFull - 10);
    reckon(c, Campaign::kClaimFull - 10);
    CHECK(h.owner == Campaign::kNeutral && h.claim == 0);
}

TEST(another_lord_wears_the_claim_away_and_two_lords_stall_it) {
    Campaign c = still(1);
    int32_t cx = 0, cy = 0;
    const int32_t hex = emptyHex(c, cx, cy);
    CHECK(hex >= 0);
    if (hex < 0) return;
    const Campaign::Hex& h = c.hexes()[static_cast<size_t>(hex)];
    const uint32_t ours = c.raise(0, cx, cy, {UnitType::LightInfantry});
    reckon(c, 8);
    const int32_t held = h.claim;
    CHECK(h.owner == 0 && held >= 100);
    // A rival stack in another cell of the same hex: contested, nothing changes.
    int32_t ox = -1, oy = -1;
    for (int32_t y = cy - 1; y <= cy + 1 && ox < 0; ++y)
        for (int32_t x = cx - 1; x <= cx + 1 && ox < 0; ++x)
            if ((x != cx || y != cy) && c.hexAt(x, y) == hex && c.terrainAt(x, y) != Lay::Water) ox = x, oy = y;
    CHECK(ox >= 0);
    if (ox < 0) return;
    const uint32_t theirs = c.raise(3, ox, oy, {UnitType::LightInfantry, UnitType::LightInfantry, UnitType::LightInfantry});
    reckon(c, 5);
    CHECK(h.owner == 0 && h.claim == held);
    // Ours gone: theirs wear ours down (25 a reckoning), then claim it.
    for (const Army& a : std::vector<Army>(c.findStack(ours)->armies)) c.disband(ours, a.id);
    reckon(c, 4);
    CHECK(h.owner == 3);
    (void)theirs;
}

TEST(outposts_are_claimed_twice_as_fast_and_reach_round_them) {
    Campaign c = still(1);
    const Campaign::Outpost* op = nullptr;
    for (const Campaign::Outpost& o : c.outposts())
        if (c.hexes()[static_cast<size_t>(o.hex)].owner == Campaign::kNeutral && c.terrainAt(o.cx, o.cy) != Lay::Water) op = &o;
    CHECK(op != nullptr);
    if (!op) return;
    std::printf("  %zu outposts; taking the tower at %s\n", c.outposts().size(), op->name.c_str());
    c.raise(0, op->cx, op->cy, {UnitType::LightInfantry});
    reckon(c, 1);
    CHECK(c.hexes()[static_cast<size_t>(op->hex)].claim == 30);  // (10 + 5) x 2
    reckon(c, 3);
    // Held: it claims the hexes round it as one army would.
    std::vector<int32_t> ring;
    Campaign::hexNeighbours(op->hex, ring);
    int32_t reached = 0;
    for (int32_t n : ring) reached += c.hexes()[static_cast<size_t>(n)].owner == 0;
    CHECK(reached >= 3);
}

TEST(the_land_pays_a_little_gold) {
    Campaign c = still(1);
    const int32_t before = c.income(0);
    const int32_t held = c.hexesHeld(0);
    CHECK(held >= 5);
    int32_t cities = 0;
    for (const Campaign::CityState& t : c.cities())
        if (t.owner == 0) cities += t.income;
    CHECK(before == cities + held / Campaign::kHexesPerGold);
    std::printf("  side 0: %d hexes, %d gold a day from them, %d from its city\n", held, held / Campaign::kHexesPerGold, cities);
}

TEST(territory_is_deterministic) {
    const auto run = [] {
        Campaign c(continent(), 6);
        for (int t = 0; t < Campaign::kTicksPerDay * 6; ++t) c.step();
        int32_t held = 0;
        for (const Campaign::Hex& h : c.hexes()) held += h.owner != Campaign::kNeutral;
        return std::make_pair(c.checksum(), held);
    };
    const auto a = run();
    CHECK(a == run());
    std::printf("  six days of war: %d hexes held\n", a.second);
    CHECK(a.second > 50);
}

// ---------------------------------------------------------------- the small edges

TEST(each_edge_is_small_and_together_they_are_capped) {
    for (const int32_t pct : {kEdgeHomePct, kEdgeGroundPct, kEdgeRankPct * 3, kEdgeHeightPct, kEdgeHoldPct, heroLeadPct(9)}) CHECK(pct > 0 && pct < 10);
    Edges all{kEdgeHomePct, kEdgeGroundMaxPct, kEdgeRankPct * 3, heroLeadPct(9), kEdgeHeightPct, kEdgeHoldPct};
    CHECK(all.total() == kEdgeCapPct);
    std::printf("  every edge: %s\n", all.describe().c_str());
    CHECK(Edges{}.describe().empty());
    const Edges two{3, 0, 0, 0, 4, 0};
    CHECK(two.describe() == "home land +3% \xC2\xB7 high ground +4%");
    // Ground a kind of soldier knows.
    CHECK(familiarGround(Gait::Woodland, Ground::Forest));
    CHECK(familiarGround(Gait::Dwarf, Ground::Rock));
    CHECK(familiarGround(Gait::Undead, Ground::Swamp));
    CHECK(!familiarGround(Gait::Foot, Ground::Forest));
    CHECK(!familiarGround(Gait::Horse, Ground::Plains));
}

TEST(the_edges_tip_an_even_dice_battle_but_do_not_decide_it) {
    // Four heavy infantry (4) against four: a coin toss. Each edge alone
    // moves it a little; all of them together (capped at +15%) to about
    // seven in ten. Exact (winChance) and rolled (the campaign's d100).
    const auto odds = [](int32_t pct) {
        const int32_t s = 4 * (100 + pct);  // hundredths
        return Campaign::winChance({s, s, s, s}, {400, 400, 400, 400});
    };
    const auto rolled = [](int32_t pct, uint64_t seed) {
        Rng rng(seed, 7);
        const int32_t as = 4 * (100 + pct), ds = 400;
        int wins = 0;
        for (int b = 0; b < 2000; ++b) {
            int a = 4, d = 4, ha = 0, hd = 0;
            while (a > 0 && d > 0) {
                const bool aLow = static_cast<int32_t>(rng.nextBelow(1000)) + 1 <= ds, dLow = static_cast<int32_t>(rng.nextBelow(1000)) + 1 <= as;
                if (aLow == dLow) continue;
                if (aLow && ++ha == 2) --a, ha = 0;
                if (dLow && ++hd == 2) --d, hd = 0;
            }
            wins += d == 0;
        }
        return wins * 1000 / 2000;
    };
    std::printf("  even stacks: none %d, home land %d, veterans %d, everything %d (rolled %d)\n", odds(0), odds(kEdgeHomePct),
                odds(kEdgeRankPct * 2), odds(kEdgeCapPct), rolled(kEdgeCapPct, 3));
    CHECK(odds(0) == 500);
    for (const int32_t pct : {kEdgeHomePct, kEdgeGroundPct, kEdgeHeightPct, heroLeadPct(5)}) CHECK(odds(pct) > 500 && odds(pct) < 600);
    CHECK(odds(kEdgeCapPct) >= 650 && odds(kEdgeCapPct) <= 780);
    CHECK(std::abs(rolled(kEdgeCapPct, 3) - odds(kEdgeCapPct)) < 40);
    // A weaker stack (3 against 4) with every edge is still the weaker stack.
    CHECK(Campaign::winChance({345, 345, 345, 345}, {400, 400, 400, 400}) < 500);
}

TEST(the_campaign_reckons_each_armys_edges) {
    Campaign c = still(1);
    int32_t cap = -1;
    for (size_t i = 0; i < c.cities().size(); ++i)
        if (c.cities()[i].owner == 0) cap = static_cast<int32_t>(i);
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(cap)];
    Army vet;
    vet.type = UnitType::HeavyInfantry;
    vet.rank = 2;
    Army hero;
    hero.type = UnitType::Hero;
    hero.rank = 4;  // level 5: strength 8, two leadership points
    const std::vector<const Army*> side{&vet, &hero};
    const Edges e = c.armyEdges(vet, side, 0, t.cx, t.cy, true);
    std::printf("  a veteran beside a level 5 hero at home: %s\n", e.describe().c_str());
    CHECK(e.home == kEdgeHomePct);  // the capital's own hex
    CHECK(e.seniority == 2 * kEdgeRankPct);
    CHECK(e.hero == heroLeadPct(Campaign::strength(hero)));
    CHECK(e.total() <= kEdgeCapPct);
    // A soldier's rank is an edge, not strength; a hero's level is his strength.
    Army green = vet;
    green.rank = 0;
    CHECK(Campaign::strength(vet) == Campaign::strength(green));
    const std::vector<int32_t> a = c.lineup({&vet}, 0, t.cx, t.cy, false), b = c.lineup({&green}, 0, t.cx, t.cy, false);
    CHECK(a[0] > b[0] && a[0] < b[0] + 100);  // more, but less than a whole point
    // Another lord standing on our land has no home edge there.
    CHECK(c.armyEdges(vet, side, 3, t.cx, t.cy, false).home == 0);
}

TEST(the_edges_give_a_clear_but_not_decisive_lead_in_3d) {
    // Six light infantry against six on flat ground, a little scattered, each
    // seed fought twice with the sides swapped (the first side to act has a
    // slight edge of its own). The one side veterans and at home (+9%): it
    // should win most, not all.
    int wins = 0, losses = 0;
    for (uint64_t run = 0; run < 40; ++run) {
        const PlayerId edged = static_cast<PlayerId>(run % 2);
        WorldConfig cfg;
        cfg.seed = run / 2 + 1;
        cfg.homeSide = edged;
        World w(cfg);
        Rng jitter(run / 2 + 1, 99);
        std::vector<EntityId> a, b;
        const Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
        for (int k = 0; k < 6; ++k) {
            const Fixed ja = Fixed::fromRatio(static_cast<int64_t>(jitter.nextBelow(200)), 100);
            const Fixed jb = Fixed::fromRatio(static_cast<int64_t>(jitter.nextBelow(200)), 100);
            a.push_back(w.spawnUnit(0, {Fixed::fromInt(120) - ja, Fixed::fromInt(110 + 2 * k) + jb, Fixed::zero()}, east, unitStats(UnitType::LightInfantry)));
            b.push_back(w.spawnUnit(1, {Fixed::fromInt(124) + jb, Fixed::fromInt(110 + 2 * k) + ja, Fixed::zero()}, -east, unitStats(UnitType::LightInfantry)));
            w.setVeterancy(edged == 0 ? a.back() : b.back(), 2, 0);
        }
        const std::vector<EntityId>& mine = edged == 0 ? a : b;
        const std::vector<EntityId>& theirs = edged == 0 ? b : a;
        CHECK(w.edgesOf(*w.findUnit(mine[0])).total() == kEdgeHomePct + 2 * kEdgeRankPct);
        CHECK(w.edgesOf(*w.findUnit(theirs[0])).total() == 0);
        for (int t = 0; t < 240 * kTicksPerSecond; ++t) {
            w.step({});
            int im = 0, it = 0;
            for (EntityId id : mine) im += w.findUnit(id)->alive() && !w.findUnit(id)->routed;
            for (EntityId id : theirs) it += w.findUnit(id)->alive() && !w.findUnit(id)->routed;
            if (im == 0 || it == 0) {
                wins += it == 0 && im > 0;
                losses += im == 0 && it > 0;
                break;
            }
        }
    }
    std::printf("  six against six, veterans at home (+9%%): won %d, lost %d of 40\n", wins, losses);
    CHECK(wins > losses * 3 / 2);
    // Not a foregone thing. (Light infantry sit on a kill breakpoint: 11 x 109%
    // is nearly always 12, five blows to kill instead of six, so the two edges
    // together count for more than each alone, home 19-21, veterans 23-17. With
    // the orders and the death matrix merged this lands at 39-1, the branch alone 37-3.)
    CHECK(losses >= 1);
}
