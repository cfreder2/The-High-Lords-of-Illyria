// Every soldier counts (engine/sim/src/campaign_rare.cpp): cities train only
// common troops, slowly and dearly; rare ones come from the start, levies,
// temples, ruins and heroes for hire, and are never trained or replaced.
#include <algorithm>
#include <cstdio>
#include <string>

#include "illyria/sim/campaign.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

int32_t countType(const Campaign& c, uint8_t side, UnitType t) {
    int32_t n = 0;
    for (const Campaign::Stack& s : c.stacks())
        if (s.owner == side)
            for (const Army& a : s.armies) n += a.type == t;
    return n;
}
}  // namespace

TEST(cities_train_only_common_troops) {
    const Campaign c(continent(), 1);
    int32_t levies = 0, engines = 0;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        for (const Campaign::Option& o : c.cities()[i].options) {
            if (o.left < 0) {
                CHECK(Campaign::trainable(o.type));
            } else {
                // A levy: rare troops, a few, once.
                CHECK(Campaign::rare(o.type));
                CHECK(o.left >= 1 && o.left <= 2);
                ++levies;
            }
            engines += o.type == UnitType::Catapult;
            if (o.type == UnitType::Catapult) CHECK(c.continent().cities()[i].capital);
        }
    }
    std::printf("  %d levies of rare troops wait in the cities; %d capitals build siege engines\n", levies, engines);
    CHECK(levies >= 8 + 4);  // every capital's, and a few towns'
    CHECK(engines == 3);
    // Slow and dear: light infantry two days, heavy cavalry six.
    for (const Campaign::Option& o : c.cities()[0].options)
        if (o.type == UnitType::LightInfantry) CHECK(o.days == 2 && o.cost >= 10);
    // Common: the line troops; rare: heroes, beasts, monsters, allies, specialists.
    for (UnitType t : {UnitType::LightInfantry, UnitType::HeavyInfantry, UnitType::Archers, UnitType::Pikemen, UnitType::LightCavalry,
                       UnitType::HeavyCavalry, UnitType::Catapult})
        CHECK(Campaign::trainable(t));
    for (UnitType t : {UnitType::Hero, UnitType::Dragons, UnitType::Giants, UnitType::Griffins, UnitType::Unicorns, UnitType::DwarfGrenadier,
                       UnitType::Journeyman, UnitType::Berserker})
        CHECK(Campaign::rare(t));
}

TEST(every_rare_type_can_be_had_somewhere) {
    for (int k = 0; k < kUnitTypeCount; ++k) {
        const UnitType t = static_cast<UnitType>(k);
        if (Campaign::rare(t)) {
            if (Campaign::sources(t) == 0) std::printf("  %s: no source\n", unitTypeName(t));
            CHECK(Campaign::sources(t) != 0);
        } else {
            CHECK(Campaign::sources(t) == 0);
        }
    }
    int32_t n = 0;
    CHECK(Campaign::templeGuard(1, n) == UnitType::Archons && n == 1);
}

TEST(each_realm_starts_with_its_rare_troops) {
    const Campaign c(continent(), 1);
    for (uint8_t side = 0; side < 8; ++side) {
        int32_t rare = 0;
        for (const Campaign::Stack& s : c.stacks())
            if (s.owner == side)
                for (const Army& a : s.armies) rare += !a.hero() && Campaign::rare(a.type);
        CHECK(rare >= 1);
    }
    CHECK(countType(c, 1, UnitType::Giants) == 2);  // the Highcrag Giants march with giants
}

TEST(a_levy_is_mustered_once) {
    Campaign c(continent(), 1);
    c.setHuman(1, true);
    int32_t capital = -1;
    for (size_t i = 0; i < c.cities().size(); ++i)
        if (c.continent().cities()[i].capital && c.continent().cities()[i].owner == 1) capital = static_cast<int32_t>(i);
    CHECK(capital >= 0);
    if (capital < 0) return;
    int32_t option = -1;
    const auto& options = c.cities()[static_cast<size_t>(capital)].options;
    for (size_t k = 0; k < options.size(); ++k)
        if (options[k].left > 0) option = static_cast<int32_t>(k);
    CHECK(option >= 0);
    if (option < 0) return;
    const Campaign::Option levy = options[static_cast<size_t>(option)];
    const int32_t before = countType(c, 1, levy.type);
    const size_t n = options.size();
    CHECK(c.produce(capital, option));
    for (int t = 0; t < Campaign::kTicksPerDay * (levy.days * levy.left + 2); ++t) c.step();
    std::printf("  %d %s mustered at %s\n", levy.left, unitTypeName(levy.type), c.continent().cities()[static_cast<size_t>(capital)].name.c_str());
    CHECK(countType(c, 1, levy.type) == before + levy.left);
    // Spent: the option is gone and the city stands idle.
    CHECK(c.cities()[static_cast<size_t>(capital)].options.size() == n - 1);
    CHECK(c.cities()[static_cast<size_t>(capital)].producing == -1);
}

TEST(a_temples_guard_follows_the_first_hero) {
    Campaign c(continent(), 2);
    for (size_t s = 0; s < c.sides().size(); ++s) c.setHuman(static_cast<uint8_t>(s), true);
    // A hero raised in the city nearest a temple marches there.
    uint32_t id = 0;
    int32_t temple = -1;
    for (size_t k = 0; k < c.sites().size() && temple < 0; ++k) {
        const Campaign::SiteState st = c.sites()[k];
        if (st.kind != Continent::Site::Kind::Temple) continue;
        int32_t best = 1 << 30, city = -1;
        for (size_t i = 0; i < c.cities().size(); ++i) {
            const int32_t d = std::max(std::abs(c.cities()[i].cx - st.cx), std::abs(c.cities()[i].cy - st.cy));
            if (d < best) best = d, city = static_cast<int32_t>(i);
        }
        c.grant(city, 0, UnitType::LightInfantry, 1);
        id = c.raise(0, c.cities()[static_cast<size_t>(city)].cx, c.cities()[static_cast<size_t>(city)].cy, {UnitType::Hero, UnitType::LightInfantry});
        if (!c.move(id, st.cx, st.cy)) continue;
        for (int t = 0; t < Campaign::kTicksPerDay * 30 && !c.findStack(id)->path.empty(); ++t) c.step();
        if (c.findStack(id)->cx == st.cx && c.findStack(id)->cy == st.cy) temple = static_cast<int32_t>(k);
    }
    CHECK(temple >= 0);
    if (temple < 0) return;
    const size_t before = c.findStack(id)->armies.size();
    CHECK(c.search(id));
    const Campaign::Stack* s = c.findStack(id);
    int32_t count = 0;
    const UnitType guard = Campaign::templeGuard(c.sites()[static_cast<size_t>(temple)].bit, count);
    CHECK(s->armies.size() == before + static_cast<size_t>(count));
    CHECK(c.sites()[static_cast<size_t>(temple)].gifted);
    for (size_t k = before; k < s->armies.size(); ++k) CHECK(s->armies[k].type == guard && s->armies[k].ally);
    // Only once: the next visit has nothing left to give.
    CHECK(!c.canSearch(id));
    std::printf("  %s: %d %s joined the hero\n", c.continent().sites()[static_cast<size_t>(temple)].name.c_str(), count, unitTypeName(guard));
}

TEST(veterans_and_rare_troops_are_precious) {
    Army a;
    a.type = UnitType::LightInfantry;
    CHECK(!Campaign::precious(a));
    a.rank = 1;
    CHECK(!Campaign::precious(a));  // a regular is not yet a veteran
    a.rank = 2;
    CHECK(Campaign::precious(a));
    Army g;
    g.type = UnitType::Griffins;
    CHECK(Campaign::precious(g));
    // Rare troops cost upkeep unless they are allies, and dearer common troops cost more.
    CHECK(Campaign::upkeep(UnitType::HeavyCavalry) > Campaign::upkeep(UnitType::LightInfantry));
    CHECK(Campaign::upkeep(UnitType::Griffins) > 0);
}
