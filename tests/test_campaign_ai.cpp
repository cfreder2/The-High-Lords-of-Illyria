// The computer lords (engine/sim/src/campaign_ai.cpp) and the Warlords rules
// they reckon with (campaign_rules.cpp): exact dice odds, the Hard lord
// outplaying the Easy one, a capital saved, a treasury that never runs dry.
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "illyria/sim/campaign.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

void runDays(Campaign& c, int days) {
    for (int t = 0; t < days * Campaign::kTicksPerDay && !c.winner(); ++t) c.step();
}

int32_t capitalOf(const Campaign& c, uint8_t side) {
    for (size_t i = 0; i < c.cities().size(); ++i)
        if (c.continent().cities()[i].capital && c.continent().cities()[i].owner == side) return static_cast<int32_t>(i);
    return -1;
}
}  // namespace

TEST(warlords_dice_odds_are_exact) {
    // Equal strengths: a coin toss. One army against nobody: certain.
    CHECK(Campaign::winChance({5}, {5}) == 500);
    CHECK(Campaign::winChance({3}, {}) == 1000);
    CHECK(Campaign::winChance({}, {3}) == 0);
    // Strength 6 against 4: the next hit lands on the 4 with chance
    // 6*6 / (6*6 + 4*4) = 36/52; two hits before two: p^2 (3 - 2p) = 0.774.
    CHECK(std::abs(Campaign::winChance({6}, {4}) - 774) <= 1);
    // Quality over quantity, as the manual warns: a dragon (9) beats four light infantry (3).
    CHECK(Campaign::winChance({9}, {3, 3, 3, 3}) > 500);
    // More armies, better odds; a stronger defence, worse.
    CHECK(Campaign::winChance({4, 4, 4}, {5, 5}) > Campaign::winChance({4, 4}, {5, 5}));
    CHECK(Campaign::winChance({4, 4}, {6, 6}) < Campaign::winChance({4, 4}, {5, 5}));
}

TEST(terrain_and_walls_change_the_odds) {
    Campaign c(continent(), 1);
    // A neutral town's walls (defence 2-6 gives +1) make its garrison harder to beat.
    const int32_t capital = capitalOf(c, 0);
    CHECK(capital >= 0);
    int32_t town = -1;
    for (size_t i = 0; i < c.cities().size(); ++i)
        if (c.cities()[i].owner == Campaign::kNeutral) town = static_cast<int32_t>(i);
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(town)];
    std::vector<const Army*> garrison = c.defendersAt(0, t.cx, t.cy);
    CHECK(!garrison.empty());
    const std::vector<int32_t> walled = c.lineup(garrison, Campaign::kNeutral, t.cx, t.cy, true);
    const std::vector<int32_t> open = c.lineup(garrison, Campaign::kNeutral, t.cx, t.cy, false);
    for (size_t k = 0; k < walled.size(); ++k) CHECK(walled[k] == std::min(9, open[k] + 1));
}

TEST(hard_lords_outplay_easy_ones) {
    // Seat by seat: the same lord played Hard among Easy ones, against the
    // same seat in an all-Easy game, so neither the seat's position nor its
    // neighbours decide it. Forty days: the race for the neutral towns and the
    // first wars between neighbours.
    int32_t hard = 0, easy = 0, better = 0;
    for (uint64_t seed : {2, 3}) {
        const auto play = [&](int hardSeat) {
            Campaign c(continent(), seed);
            for (uint8_t s = 0; s < 8; ++s) c.setDifficulty(s, s == hardSeat ? Difficulty::Hard : Difficulty::Easy);
            runDays(c, 40);
            return c;
        };
        const Campaign base = play(-1);
        for (int seat = 0; seat < 8; ++seat) {
            const int32_t h = play(seat).citiesOwned(static_cast<uint8_t>(seat)), e = base.citiesOwned(static_cast<uint8_t>(seat));
            hard += h, easy += e, better += h > e;
        }
    }
    std::printf("  seat for seat: Hard %d cities, Easy %d; Hard ahead in %d of 16\n", hard, easy, better);
    CHECK(hard > easy * 11 / 10);
}

TEST(the_computer_saves_a_threatened_capital) {
    // Side 1 (played by hand here) marches a strong stack on side 0's
    // capital. Side 0 has a stack of its own out in the field, nearer home
    // than the enemy: the computer must bring it back in time, and hold.
    Campaign c(continent(), 7);
    c.setHuman(1, true);
    const int32_t capital = capitalOf(c, 0);
    const Campaign::CityState home = c.cities()[static_cast<size_t>(capital)];
    // A road out of the capital: the way to the furthest city within reach.
    const uint32_t probe = c.raise(0, home.cx, home.cy, {UnitType::LightInfantry});
    std::vector<std::pair<int32_t, int32_t>> road;
    for (size_t i = 0; i < c.cities().size() && road.size() < 14; ++i) {
        if (c.cities()[i].owner != Campaign::kNeutral) continue;
        if (!c.move(probe, c.cities()[i].cx, c.cities()[i].cy)) continue;
        if (c.findStack(probe)->path.size() >= 14) road = c.findStack(probe)->path;
    }
    CHECK(road.size() >= 14);
    if (road.size() < 14) return;
    c.disband(probe, c.findStack(probe)->armies.front().id);
    const auto [fx, fy] = road[4];    // ours: half a day out
    const auto [ex, ey] = road[12];   // theirs: further along the same road
    const uint32_t field = c.raise(0, fx, fy, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry});
    std::vector<uint32_t> ours;
    for (const Army& a : c.findStack(field)->armies) ours.push_back(a.id);
    const uint32_t foe = c.raise(1, ex, ey, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Giants, UnitType::Giants});
    CHECK(c.move(foe, home.cx, home.cy));
    // Before they march: the capital alone would likely fall.
    std::vector<const Army*> attackers;
    for (const Army& a : c.findStack(foe)->armies) attackers.push_back(&a);
    const int32_t before = c.attackOdds(attackers, 1, home.cx, home.cy);
    std::printf("  the enemy's odds on the capital as it stands: %d/1000\n", before);
    CHECK(before > 300);
    // Run until the enemy attacks (or is destroyed on the way).
    bool atHome = false;
    for (int t = 0; t < 10 * Campaign::kTicksPerDay; ++t) {
        const Campaign::Stack* f = c.findStack(foe);
        if (!f || f->armies.empty()) break;
        if (f->path.size() == 1) {  // one step from the gates
            int32_t inside = 0;
            for (const Campaign::Stack& s : c.stacks())
                if (s.owner == 0 && s.cx == home.cx && s.cy == home.cy)
                    for (const Army& a : s.armies) inside += std::find(ours.begin(), ours.end(), a.id) != ours.end();
            atHome = inside == static_cast<int32_t>(ours.size());
        }
        c.step();
        if (c.pending()) c.resolvePending();
    }
    CHECK(atHome);
    CHECK(c.cities()[static_cast<size_t>(capital)].owner == 0);
}

TEST(computer_lords_keep_out_of_debt) {
    // Sixty days of all-computer war: nobody's armies desert for want of pay
    // while their side still holds a realm (a side down to its last city can
    // lose it in a day; then nothing pays anyone).
    Campaign c(continent(), 5);
    c.setComputerDifficulty(Difficulty::Easy);
    for (uint8_t s = 0; s < 8; s += 2) c.setDifficulty(s, Difficulty::Hard);
    std::vector<int32_t> cities(8), deserted(8, 0);
    bool ok = true;
    for (int d = 0; d < 60 && !c.winner(); ++d) {
        for (uint8_t s = 0; s < 8; ++s) cities[s] = c.citiesOwned(s);
        runDays(c, 1);
        for (uint8_t s = 0; s < 8; ++s) {
            if (cities[s] >= 2 && c.citiesOwned(s) >= 2 && c.sides()[s].deserted != deserted[s]) {
                std::printf("  day %d: side %d lost %d armies for want of pay\n", d, s, c.sides()[s].deserted - deserted[s]);
                ok = false;
            }
            deserted[s] = c.sides()[s].deserted;
            CHECK(c.sides()[s].gold >= 0);
        }
    }
    CHECK(ok);
}
