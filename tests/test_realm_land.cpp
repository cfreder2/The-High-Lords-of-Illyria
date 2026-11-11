// Each realm's own country (edges.h realmLand): its men strike harder and march quicker on it, in the battle
// (World::onOwnLand) and on the campaign map (Campaign::landAt, the march); and the three kinds of place the
// continent has (docs/MAP.md): capitals, walled towns set at the narrow places, and open villages.
#include <algorithm>
#include <cstdio>

#include "illyria/sim/campaign.h"
#include "illyria/sim/edges.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
// How far a light infantryman of this side walks east over a level plain in ten seconds.
Fixed walked(uint8_t land) {
    WorldConfig cfg;
    cfg.seed = 3;
    cfg.sideLand[0] = land;
    World w(cfg);
    const EntityId id = w.spawnUnit(0, {m(100), m(100), Fixed::zero()}, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::LightInfantry));
    const Command go{0, id, Command::Type::Move, {m(400), m(100), Fixed::zero()}};
    w.step(std::span(&go, 1));
    for (int t = 0; t < 10 * kTicksPerSecond; ++t) w.step({});
    return w.findUnit(id)->pos.x - m(100);
}
}  // namespace

TEST(every_realm_has_a_country_of_its_own) {
    for (uint8_t f = 0; f < Continent::kFactionCount; ++f) {
        CHECK(realmLand(f) != 0);
        CHECK(landName(realmLand(f))[0] != 0);
    }
    CHECK(realmLand(Continent::kNeutral) == 0);  // a neutral garrison has none
    // What the ground counts as: level plains are the open country, the same ground on a slope the hills.
    CHECK(landOfGround(Ground::Plains, false) == kLandOpen);
    CHECK(landOfGround(Ground::Plains, true) == kLandHills);
    CHECK(landOfGround(Ground::Rock, false) == kLandHills);
    CHECK(landOfGround(Ground::Forest, false) == kLandForest);
    CHECK(landOfGround(Ground::Swamp, false) == kLandMarsh);
    CHECK(landOfGround(Ground::Shallows, false) == kLandShore);
    CHECK(landOfGround(Ground::DeepWater, false) == 0);
}

TEST(a_man_marches_quicker_and_strikes_harder_on_his_realms_country) {
    const Fixed plain = walked(0), own = walked(kLandOpen), other = walked(kLandForest);
    std::printf("  ten seconds over a level plain: %.1f m with no country, %.1f m in his own, %.1f m in another's\n", plain.toDouble(), own.toDouble(),
                other.toDouble());
    CHECK(other == plain);
    // kRealmPacePct quicker, to the centimetre a tick allows.
    CHECK(abs(own * 100 - plain * (100 + kRealmPacePct)) < m(20));
    WorldConfig cfg;
    cfg.sideLand[0] = kLandOpen;
    cfg.sideLand[1] = kLandForest;
    World w(cfg);
    const EntityId a = w.spawnUnit(0, {m(50), m(50), Fixed::zero()}, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::LightInfantry));
    const EntityId b = w.spawnUnit(1, {m(60), m(50), Fixed::zero()}, {-Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::LightInfantry));
    CHECK(w.edgesOf(*w.findUnit(a)).realm == kEdgeRealmPct);
    CHECK(w.edgesOf(*w.findUnit(b)).realm == 0);
    CHECK(w.edgesOf(*w.findUnit(a)).total() <= kEdgeCapPct);
}

TEST(the_campaign_reckons_a_realms_country_as_the_battle_does) {
    Campaign c(continent(), 5);
    // The Sylvaran Elves (faction 4) in a wood, and on a plain; the Aurelian Throne (0) the other way about.
    int32_t wood[2] = {-1, -1}, plain[2] = {-1, -1};
    for (int32_t y = 0; y < c.height(); ++y)
        for (int32_t x = 0; x < c.width(); ++x) {
            // (A wood with more wood east of it, to march through.)
            if (c.terrainAt(x, y) == Lay::Forest && wood[0] < 0 && x + 1 < c.width() && c.terrainAt(x + 1, y) == Lay::Forest) wood[0] = x, wood[1] = y;
            if (c.terrainAt(x, y) == Lay::Plain && plain[0] < 0) plain[0] = x, plain[1] = y;
        }
    CHECK(wood[0] >= 0 && plain[0] >= 0);
    const uint32_t elves = c.raise(4, wood[0], wood[1], {UnitType::LightInfantry});
    const uint32_t knights = c.raise(0, wood[0], wood[1], {UnitType::LightInfantry});
    const auto edge = [&](uint32_t stack, uint8_t owner, const int32_t at[2]) {
        const Campaign::Stack* s = c.findStack(stack);
        std::vector<const Army*> armies;
        for (const Army& a : s->armies) armies.push_back(&a);
        return c.stackEdges(armies, owner, at[0], at[1], false).realm;
    };
    CHECK(edge(elves, 4, wood) == kEdgeRealmPct);
    CHECK(edge(elves, 4, plain) == 0);
    CHECK(edge(knights, 0, wood) == 0);
    CHECK(edge(knights, 0, plain) == kEdgeRealmPct);
    // And they march it quicker: the same step into a wood takes the elves less time.
    const Campaign::Stack& e = *c.findStack(elves);
    const Campaign::Stack& k = *c.findStack(knights);
    int32_t quicker = 0, steps = 0;
    for (int32_t dy = -1; dy <= 1; ++dy)
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const int32_t x = wood[0] + dx, y = wood[1] + dy;
            if ((!dx && !dy) || c.terrainAt(x, y) != Lay::Forest) continue;
            const int32_t te = c.stepTime(e, wood[0], wood[1], x, y), tk = c.stepTime(k, wood[0], wood[1], x, y);
            if (te == Campaign::kClosed || tk == Campaign::kClosed) continue;
            ++steps;
            quicker += te < tk;
        }
    std::printf("  %d of %d steps through the wood are quicker for the elves\n", quicker, steps);
    CHECK(steps > 0 && quicker == steps);
}

TEST(the_land_has_capitals_walled_towns_and_open_villages) {
    const auto& cs = continent().cities();
    CHECK(cs.size() == 80);
    size_t capitals = 0, towns = 0, villages = 0;
    for (const Continent::City& c : cs) {
        if (c.capital) ++capitals;
        else if (c.walled) ++towns;
        else ++villages;
        CHECK(c.walled == !c.castle.rings.empty());
        if (!c.walled) {
            // A village: a tower house, a green and houses; nothing to shoot from or to break.
            CHECK(c.castle.towers.empty() && c.castle.ramps.empty());
            CHECK(c.castle.houses.size() >= 3);
            CHECK(c.castle.courtyardRadius > Fixed::zero());
            CHECK(c.radius < m(40));
        }
    }
    std::printf("  %zu capitals, %zu walled towns, %zu villages\n", capitals, towns, villages);
    CHECK(capitals == 8);
    CHECK(towns >= 16 && towns <= 24);
    CHECK(villages >= 48);
    // Open country between the walls: no two walled places within three bowshots, no village under a wall's bows.
    Fixed closestWalls = m(100000), closestVillage = m(100000);
    for (const Continent::City& a : cs)
        for (const Continent::City& b : cs) {
            if (&a == &b || !b.walled) continue;
            const Fixed gap = (a.pos - b.pos).flat().length() - a.radius - b.radius;
            if (a.walled) closestWalls = min(closestWalls, gap);
            else closestVillage = min(closestVillage, gap);
        }
    std::printf("  the closest two walls stand %.0f m apart; the nearest village to a wall %.0f m\n", closestWalls.toDouble(), closestVillage.toDouble());
    CHECK(closestWalls >= m(165));
    CHECK(closestVillage >= m(80));
    // A walled place holds every named crossing but the open ford of the set battle.
    for (size_t i = 1; i < continent().crossings().size(); ++i) {
        const Continent::Crossing& cr = continent().crossings()[i];
        if (cr.name.empty() || cr.kind == Continent::Crossing::Kind::Footbridge) continue;
        Fixed nearest = m(100000);
        for (const Continent::City& c : cs)
            if (c.walled) nearest = min(nearest, (c.pos - cr.pos).flat().length() - c.radius);
        if (nearest > m(260)) std::printf("  %s: the nearest wall is %.0f m off\n", cr.name.c_str(), nearest.toDouble());
        CHECK(nearest <= m(260));
    }
}

TEST(no_two_lords_start_side_by_side_and_none_starts_poor) {
    // Each neutral place counted for the capital nearest it (as the crow flies): every lord has a handful, and
    // the richest start is no more than three times the poorest.
    const auto& cs = continent().cities();
    Campaign c(continent(), 5);
    int32_t places[8] = {}, gold[8] = {};
    Fixed closest = m(100000);
    for (size_t i = 0; i < cs.size(); ++i) {
        if (cs[i].capital) {
            for (size_t j = 0; j < i; ++j)
                if (cs[j].capital) closest = min(closest, (cs[i].pos - cs[j].pos).flat().length());
            continue;
        }
        size_t best = 0;
        for (size_t k = 0; k < 8; ++k)
            if ((cs[k].pos - cs[i].pos).flat().lengthSq() < (cs[best].pos - cs[i].pos).flat().lengthSq()) best = k;
        ++places[cs[best].owner];
        gold[cs[best].owner] += c.cities()[i].income;
    }
    for (int f = 0; f < 8; ++f) std::printf("  %-22s %2d places, %3d gold a day\n", Continent::factions()[static_cast<size_t>(f)].name.c_str(), places[f], gold[f]);
    std::printf("  the nearest two capitals are %.0f m apart\n", closest.toDouble());
    CHECK(closest >= m(450));
    CHECK(*std::min_element(places, places + 8) >= 5);
    CHECK(*std::max_element(gold, gold + 8) <= *std::min_element(gold, gold + 8) * 3);
}

TEST(a_man_on_his_own_sides_land_has_the_home_edge_wherever_the_land_is_held) {
    // The campaign tells the world whose land each part of the field is (World::setLandHolders): the west half
    // side 0's here, the east half side 1's, and a strip nobody's.
    WorldConfig cfg;
    World w(cfg);
    std::vector<uint8_t> holders(4 * 4, 255);
    for (int y = 0; y < 4; ++y) holders[static_cast<size_t>(y) * 4 + 0] = 0, holders[static_cast<size_t>(y) * 4 + 1] = 0, holders[static_cast<size_t>(y) * 4 + 3] = 1;
    w.setLandHolders(holders, 4, m(50), {Fixed::zero(), Fixed::zero(), Fixed::zero()});
    const Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    const auto home = [&](PlayerId side, int64_t x) {
        const EntityId id = w.spawnUnit(side, {m(x), m(60), Fixed::zero()}, east, unitStats(UnitType::LightInfantry));
        return w.edgesOf(*w.findUnit(id)).home;
    };
    CHECK(home(0, 30) == kEdgeHomePct);   // on his own
    CHECK(home(1, 30) == 0);              // on the other's
    CHECK(home(0, 120) == 0);             // on nobody's
    CHECK(home(1, 170) == kEdgeHomePct);
    CHECK(home(0, 170) == 0);
    CHECK(w.landHolderAt(m(500), m(60)) == -1);  // off the land that was told
}

TEST(a_march_given_quicker_than_a_mans_own_pace_carries_him_with_it) {
    // A hero's Winged Sandals: the campaign gives the stack's march a third quicker than its slowest man walks.
    const auto walkedAt = [](int64_t pct) {
        WorldConfig cfg;
        World w(cfg);
        const UnitStats stats = unitStats(UnitType::HeavyInfantry);
        const EntityId id = w.spawnUnit(0, {m(100), m(100), Fixed::zero()}, {Fixed::one(), Fixed::zero(), Fixed::zero()}, stats);
        Command go{0, id, Command::Type::Move, {m(400), m(100), Fixed::zero()}};
        go.speed = stats.moveSpeed * pct / 100;
        w.step(std::span(&go, 1));
        for (int t = 0; t < 10 * kTicksPerSecond; ++t) w.step({});
        return w.findUnit(id)->pos.x - m(100);
    };
    const Fixed own = walkedAt(100), sandals = walkedAt(133), absurd = walkedAt(400);
    std::printf("  ten seconds: %.1f m at his own pace, %.1f m with the sandals, %.1f m at most\n", own.toDouble(), sandals.toDouble(), absurd.toDouble());
    CHECK(abs(sandals * 100 - own * 133) < m(30));
    CHECK(abs(absurd * 2 - own * 3) < m(1));  // half as fast again at most
}

TEST(a_capital_whose_lord_is_not_in_the_war_trains_its_own_realms_kinds) {
    // The player is the Aurelian Throne; the Ironhold Dwarves (faction 2) are left out: Deepforge stands neutral, and
    // what it would train is the dwarves' (grenadiers), not the player's.
    Campaign c(continent(), 5, Campaign::Setup{0, static_cast<uint8_t>(0xFF & ~(1u << 2))});
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Continent::City& city = continent().cities()[i];
        if (!city.capital || city.owner != 2) continue;
        CHECK(c.cities()[i].owner == Campaign::kNeutral);
        bool grenadiers = false, knights = false;
        for (const Campaign::Option& o : c.cities()[i].options) grenadiers = grenadiers || o.type == UnitType::DwarfGrenadier, knights = knights || o.type == UnitType::HeavyCavalry;
        CHECK(grenadiers && !knights);
    }
}
