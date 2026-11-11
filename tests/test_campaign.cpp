// The campaign, after Warlords: a capital and a hero to start, neutral towns
// with small garrisons, gold from cities, armies that cost upkeep.
#include <algorithm>
#include <cstdio>

#include "illyria/sim/campaign.h"
#include "campaign_orders.h"
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
    CHECK(CampaignOrders::produce(c, capital, 0));  // light infantry: two days, 12 gold
    for (int t = 0; t < Campaign::kTicksPerDay * 3; ++t) c.step();
    int armies = 0;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) armies += static_cast<int>(st.armies.size());
    CHECK(armies >= 4);  // hero, escort, the realm's rare troops and a recruit
    CHECK(c.sides()[0].gold > before);  // the capital's 30 a day outpaces a few recruits
}

namespace {
// The player's stack and the nearest neutral town to it (of at most the given defence).
struct Siege {
    uint32_t stack = 0;
    int32_t town = -1;
};
Siege nearestNeutralTown(const Campaign& c, int32_t defence) {
    Siege out;
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) home = &st;
    if (!home) return out;
    out.stack = home->id;
    int32_t best = 1 << 30;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& city = c.cities()[i];
        if (city.owner != Campaign::kNeutral || city.defense > defence) continue;
        const int32_t d = std::abs(city.cx - home->cx) + std::abs(city.cy - home->cy);
        if (d < best) {
            best = d;
            out.town = static_cast<int32_t>(i);
        }
    }
    return out;
}
int32_t menOf(const Campaign& c, uint8_t owner) {
    int32_t n = 0;
    for (const Campaign::Stack& s : c.stacks())
        if (s.owner == owner)
            for (const Army& a : s.armies) n += a.soldiers;
    return n;
}
}  // namespace

TEST(a_stack_marches_on_a_neutral_town_and_takes_it_by_fighting_there) {
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    for (uint8_t side = 1; side < Continent::kFactionCount; ++side) c.setHuman(side, true);  // (nobody else moves)
    const Siege s = nearestNeutralTown(c, 4);
    CHECK(s.stack != 0 && s.town >= 0);
    if (!s.stack || s.town < 0) return;
    const Campaign::CityState& town = c.cities()[static_cast<size_t>(s.town)];
    // A strong force to take it: heavy foot and archers beside the hero, raised where the hero stands.
    const Campaign::Stack* home = c.findStack(s.stack);
    c.raise(0, home->cx, home->cy, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers, UnitType::Archers});
    const int32_t before = menOf(c, Campaign::kNeutral);
    for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
        if (st.owner == 0 && !st.garrison) CHECK(CampaignOrders::move(c, st.id, town.cx, town.cy));
    bool fought = false;
    for (int t = 0; t < Campaign::kTicksPerDay * 15 && c.cities()[static_cast<size_t>(s.town)].owner != 0; ++t) {
        c.step();
        fought = fought || menOf(c, Campaign::kNeutral) < before;
    }
    // Their men are in the world while they fight, and the town is ours when its defenders are dead and our banner is up.
    CHECK(fought);
    CHECK(c.cities()[static_cast<size_t>(s.town)].owner == 0);
    CHECK(!c.fights().empty());
    if (c.fights().empty()) return;
    const Campaign::Fight& f = c.fights().front();
    std::printf("  day %u: %s fell; the neutral garrisons lost %d men\n", f.day, continent().cities()[static_cast<size_t>(f.city)].name.c_str(),
                before - menOf(c, Campaign::kNeutral));
    CHECK(f.city == s.town);
    CHECK(menOf(c, Campaign::kNeutral) < before);  // men died: nothing was settled by a roll
}

TEST(men_who_fought_come_out_of_it_with_their_wounds_kills_and_ranks) {
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    for (uint8_t side = 1; side < Continent::kFactionCount; ++side) c.setHuman(side, true);
    const Siege s = nearestNeutralTown(c, 4);
    if (!s.stack || s.town < 0) return;
    const Campaign::CityState& town = c.cities()[static_cast<size_t>(s.town)];
    const Campaign::Stack* home = c.findStack(s.stack);
    c.raise(0, home->cx, home->cy, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry});
    for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
        if (st.owner == 0 && !st.garrison) CampaignOrders::move(c, st.id, town.cx, town.cy);
    int32_t kills = 0;
    for (int t = 0; t < Campaign::kTicksPerDay * 15 && c.cities()[static_cast<size_t>(s.town)].owner != 0; ++t) c.step();
    for (uint32_t t = 0; t < 3 * 10 * Campaign::kTicksPerSecond; ++t) c.step();  // and the fighting is over
    int32_t wounded = 0, men = 0;
    for (const Campaign::Stack& st : c.stacks())
        for (const Army& a : st.armies) {
            if (st.owner != 0) continue;
            kills += a.kills;
            for (const Man& m : a.men) {
                men += m.alive();
                wounded += m.alive() && m.hp > 0;  // hurt, not whole (-1)
            }
            CHECK(a.soldiers == static_cast<int32_t>(std::count_if(a.men.begin(), a.men.end(), [](const Man& m) { return m.alive(); })));
        }
    std::printf("  %d men stand, %d wounded, %d kills between them\n", men, wounded, kills);
    CHECK(kills > 0);
    CHECK(men > 0);
}

TEST(a_fight_is_the_same_on_every_run) {
    const auto run = [](int32_t ticks) {
        Campaign c(continent(), 3);
        for (uint8_t side = 0; side < Continent::kFactionCount; ++side) c.setHuman(side, true);
        const Siege s = nearestNeutralTown(c, 4);
        const Campaign::CityState& town = c.cities()[static_cast<size_t>(s.town)];
        const Campaign::Stack* home = c.findStack(s.stack);
        c.raise(0, home->cx, home->cy, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers});
        for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
            if (st.owner == 0 && !st.garrison) CampaignOrders::move(c, st.id, town.cx, town.cy);
        for (int32_t t = 0; t < ticks; ++t) c.step();
        return std::make_pair(c.checksum(), c.menInWorld());
    };
    // In the middle of a fight, and well after it.
    const auto a = run(2400), b = run(2400);
    CHECK(a == b);
    CHECK(a.second > 0);  // and its men are in the world
    const auto late = run(Campaign::kTicksPerDay * 3), again = run(Campaign::kTicksPerDay * 3);
    CHECK(late == again);
}

TEST(campaign_is_deterministic) {
    const auto run = [](uint64_t seed) {
        Campaign c(continent(), seed);
        for (size_t i = 0; i < c.cities().size(); ++i)
            if (c.cities()[i].owner != Campaign::kNeutral) CampaignOrders::produce(c, static_cast<int32_t>(i), 0);
        for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
            if (!st.garrison) CampaignOrders::move(c, st.id, std::min(c.width() - 1, st.cx + 10), st.cy);
        for (int t = 0; t < Campaign::kTicksPerDay * 10; ++t) c.step();
        return c.checksum();
    };
    const uint64_t a = run(5);
    CHECK(a == run(5));
    constexpr uint64_t kGolden = 0xc5a69ae56dd59646;  // (was 0x55aae7c458e0fb8e before the player's idle garrison fought from its stations and a lord stood behind the foot, and a man with an order was no keeper; was 0xf137a04f0fe52458 before a garrison at rest was stationed as one: bowmen on the walk at the embrasures, foot in ranks behind the gate, the rest behind them; was 0x6026df4dee138226 before master (the realm layout, the land painted as Myth's) was merged with the roads, lakes, water and underwater work; was 0x1307c679b04d87a8 before the home-land edge was felt in the fight (a man on land his side holds), a march could be given quicker than a man's pace (the Winged Sandals), and a lord with no hope of winning a town back went in to hold its banner up; was 0x21d6e65de67b519c before every realm was given the same share of the land (three walled towns and six villages drafted from its own country: docs/MAP.md); was 0x9fd766763f6404bf before the land was opened (walled towns and villages, the lords' seats moved: docs/MAP.md), the garrisons, the capitals' guard and pace, the realms' own country; was 0x4660436c60562524 before gates raised for giants: the lintel at least 5.8 m, neutral walls at least 8 m; was 0x2cd385e8e22a2982 before the roads (classes, junctions, bridges that meet the ground) and the lakes (natural shores, level river surfaces) were merged, with the castles' houses no longer walling their ramps off; was 0xebab373fbcb96054 before roads of classes by traffic (a lane to four wide, `Polyline::widths` read by the ground and the drawing), clean junctions (no spurs or loops), bends round boulders, bridges with ramps to the ground and no bridge under a castle's wall; was 0x901f7b381440612a before broken men who stand (cornered, or none near to run from) no longer being passed over by their foes; 0x617fc67865b3b931 before the men snapped to the millimetre where a blocked slot moved them, and the glade round ruins; was 0xd724054dae09f288 before roads laid to every castle's gate, the gates facing their roads, no town astride a road and a rule for towers; was 0xfa8f2c062fbfab2f before every gate's fields were hashed always and the keepers met and recalled men; was 0x45f28a2ecb24c218 before gates swung (three seconds, blocking) and garrisons kept them; was 0xcb1630541ad9f9db before the capitals courtyards moved beyond their inner baileys (step 3d) on top of one mode (step 3e); was 0xdc2856c0768135f5 before every man became a unit of the world at all times: no sleeping, no waking, the march the world's own steps)
    if (a != kGolden) std::printf("  campaign checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}
