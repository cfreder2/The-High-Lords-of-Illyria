// The campaign, after Warlords: a capital and a hero to start, neutral towns
// with small garrisons, gold from cities, armies that cost upkeep.
#include <cstdio>

#include "illyria/sim/campaign.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
}  // namespace

TEST(each_side_starts_with_a_capital_a_hero_and_gold) {
    const Campaign c(continent(), 1);
    for (size_t s = 0; s < c.sides().size(); ++s) {
        CHECK(c.citiesOwned(static_cast<uint8_t>(s)) == 1);
        CHECK(c.sides()[s].gold > 0);
        int heroes = 0;
        for (const Campaign::Stack& st : c.stacks())
            if (st.owner == s)
                for (const Army& a : st.armies) heroes += a.hero();
        CHECK(heroes == 1);
    }
    // The other 72 are neutral, each with a garrison.
    int neutral = 0, garrisons = 0;
    for (const Campaign::CityState& city : c.cities()) neutral += city.owner == Campaign::kNeutral;
    for (const Campaign::Stack& st : c.stacks()) garrisons += st.garrison && !st.armies.empty();
    CHECK(neutral == 72);
    CHECK(garrisons == 72);
}

TEST(cities_pay_gold_and_train_armies_that_cost_upkeep) {
    Campaign c(continent(), 1);
    c.setHuman(0, true);  // the hero stays home rather than going off exploring
    const int32_t capital = [&] {
        for (size_t i = 0; i < c.cities().size(); ++i)
            if (c.cities()[i].owner == 0) return static_cast<int32_t>(i);
        return -1;
    }();
    const int32_t before = c.sides()[0].gold;
    CHECK(c.produce(capital, 0));  // light infantry: a day, 4 gold
    for (int t = 0; t < Campaign::kTicksPerDay * 3; ++t) c.step();
    int armies = 0;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) armies += static_cast<int>(st.armies.size());
    CHECK(armies >= 4);  // hero, escort and new recruits
    CHECK(c.sides()[0].gold > before);  // the capital's 30 a day outpaces a few recruits
}

TEST(a_heros_stack_marches_and_takes_a_neutral_town) {
    Campaign c(continent(), 3);
    // The player's stack (side 0) and the nearest neutral town to its capital.
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) home = &st;
    CHECK(home != nullptr);
    if (!home) return;
    const uint32_t id = home->id;
    int32_t target = -1, best = 1 << 30;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& city = c.cities()[i];
        if (city.owner != Campaign::kNeutral || city.defense > 4) continue;
        const int32_t d = std::abs(city.cx - home->cx) + std::abs(city.cy - home->cy);
        if (d < best) {
            best = d;
            target = static_cast<int32_t>(i);
        }
    }
    CHECK(target >= 0);
    // Reinforce the stack so the attack is fair, then march.
    for (int k = 0; k < 4; ++k) c.produce(static_cast<int32_t>(0), 0);
    CHECK(c.move(id, c.cities()[static_cast<size_t>(target)].cx, c.cities()[static_cast<size_t>(target)].cy));
    for (int t = 0; t < Campaign::kTicksPerDay * 20 && c.fights().empty(); ++t) c.step();
    CHECK(!c.fights().empty());
    if (c.fights().empty()) return;
    const Campaign::Fight& f = c.fights().front();
    std::printf("  day %u: attacked %s, %s (lost %d armies, killed %d)\n", f.day, continent().cities()[static_cast<size_t>(f.city)].name.c_str(),
                f.captured ? "captured" : "repulsed", f.attackerLost, f.defenderLost);
    CHECK(f.city == target);
}

TEST(a_players_battle_waits_for_them_and_survivors_keep_their_ranks) {
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) home = &st;
    CHECK(home != nullptr);
    if (!home) return;
    const uint32_t id = home->id;
    int32_t target = -1, best = 1 << 30;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& city = c.cities()[i];
        if (city.owner != Campaign::kNeutral) continue;
        const int32_t d = std::abs(city.cx - home->cx) + std::abs(city.cy - home->cy);
        if (d < best) {
            best = d;
            target = static_cast<int32_t>(i);
        }
    }
    CHECK(c.move(id, c.cities()[static_cast<size_t>(target)].cx, c.cities()[static_cast<size_t>(target)].cy));
    for (int t = 0; t < Campaign::kTicksPerDay * 20 && !c.pending(); ++t) c.step();
    CHECK(c.pending() != nullptr);
    if (!c.pending()) return;
    // The campaign holds still while the player fights.
    const uint32_t tick = c.tick();
    c.step();
    CHECK(c.tick() == tick);
    CHECK(c.fights().empty());
    const std::vector<const Army*> attackers = c.pendingAttackers(), defenders = c.pendingDefenders();
    CHECK(!attackers.empty() && !defenders.empty());
    // The 3D battle: every defender falls; the first attacker loses a man and rises a rank.
    std::vector<Campaign::Outcome> out;
    for (const Army* a : attackers) out.push_back({a->id, a->soldiers, 3, a->rank});
    out.front().soldiers = std::max(1, out.front().soldiers - 1);
    out.front().rank += 1;
    const Campaign::Outcome first = out.front();
    for (const Army* a : defenders) out.push_back({a->id, 0, 0, a->rank});
    c.applyBattle(out, true);
    CHECK(!c.pending());
    CHECK(c.cities()[static_cast<size_t>(target)].owner == 0);
    const Campaign::Stack* after = c.findStack(id);
    CHECK(after != nullptr);
    if (!after) return;
    bool found = false;
    for (const Army& a : after->armies)
        if (a.id == first.army) {
            found = true;
            CHECK(a.soldiers == first.soldiers);
            CHECK(a.rank == first.rank);
            CHECK(a.kills >= 3);
        }
    CHECK(found);
}

TEST(campaign_is_deterministic) {
    const auto run = [](uint64_t seed) {
        Campaign c(continent(), seed);
        for (size_t i = 0; i < c.cities().size(); ++i)
            if (c.cities()[i].owner != Campaign::kNeutral) c.produce(static_cast<int32_t>(i), 0);
        for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
            if (!st.garrison) c.move(st.id, std::min(c.width() - 1, st.cx + 10), st.cy);
        for (int t = 0; t < Campaign::kTicksPerDay * 10; ++t) c.step();
        return c.checksum();
    };
    const uint64_t a = run(5);
    CHECK(a == run(5));
    constexpr uint64_t kGolden = 0xfa371369b4f3f3d4;
    if (a != kGolden) std::printf("  campaign checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}
