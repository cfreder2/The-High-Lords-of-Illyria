// The Skirmish setup: armies bought at the Conquest's prices for a purse, and the four maps (the Ford, a village's siege, a
// capital's siege of two doors, open ground) made by the one setup code (Battle's constructor, muster and setUpSiege).
#include <algorithm>

#include "illyria/game/battle.h"
#include "illyria/sim/conquest.h"
#include "test.h"

using namespace illyria::game;
using illyria::sim::UnitType;

namespace {

// Men of `owner` within `r` metres of a point (battle metres).
int menNear(const Battle& b, uint8_t owner, const V3& p, float r) {
    int n = 0;
    for (const illyria::sim::Unit& u : b.world().units()) {
        if (!u.alive() || u.owner != owner) continue;
        const V3 q = toV3(u.pos);
        n += (V3{q.x - p.x, q.y - p.y, 0}).length() <= r;
    }
    return n;
}

V3 doorOf(const Battle& b, size_t gate) {
    const illyria::sim::Gate& g = b.worldRegion().gates()[gate];
    return toV3(g.a + g.b) * 0.5f;
}

}  // namespace

TEST(an_army_is_bought_within_its_purse_at_the_conquests_prices) {
    for (int purse : {100, 300, 900}) {
        const std::vector<Buy> army = buyArmy(purse, false);
        CHECK(armyCost(army) <= purse);
        CHECK(purse - armyCost(army) < illyria::sim::Conquest::price(UnitType::LightInfantry));  // (nothing a squad could still buy)
        for (const Buy& b : army) CHECK(b.type != UnitType::Catapult);
    }
    bool engine = false;
    for (const Buy& b : buyArmy(600, true)) engine = engine || b.type == UnitType::Catapult;
    CHECK(engine);
    for (UnitType t : skirmishTypes()) CHECK(illyria::sim::Conquest::trainable(t));
}

TEST(a_capital_siege_has_two_doors_each_kept_and_each_besieged) {
    SkirmishSetup setup;
    setup.budget = 450;
    Battle b(1, Scenario::Capital, setup);
    CHECK(b.world().gates().size() >= 2);
    const V3 d0 = doorOf(b, 0), d1 = doorOf(b, 1);
    CHECK((V3{d0.x - d1.x, d0.y - d1.y, 0}).length() > 40.0f);
    // Keepers at both doors (inside the walls, within 45 m of each) and none starved.
    const int in0 = menNear(b, Battle::kEnemy, d0, 45.0f), in1 = menNear(b, Battle::kEnemy, d1, 45.0f);
    CHECK(in0 >= 10);
    CHECK(in1 >= 10);
    // The player's army comes to both: some of it nearer each door than the other.
    int toFirst = 0, toSecond = 0;
    for (const Squad& s : b.squads()) {
        if (s.owner != Battle::kPlayer) continue;
        const V3 c = b.squadCentre(s, false);
        const float a = (V3{c.x - d0.x, c.y - d0.y, 0}).length(), z = (V3{c.x - d1.x, c.y - d1.y, 0}).length();
        (a < z ? toFirst : toSecond) += 1;
    }
    CHECK(toFirst >= 2);
    CHECK(toSecond >= 2);
    // The camera frames the whole castle and both camps.
    V3 at, out;
    float distance = 0;
    CHECK(b.siegeFocus(at, distance, out));
    CHECK(distance > 150.0f);
    // The player's engines are among those bought (the first door's company has them).
    bool engines = false;
    for (const Squad& s : b.squads()) engines = engines || (s.owner == Battle::kPlayer && s.type == UnitType::Catapult);
    CHECK(engines);
}

TEST(holding_the_walls_puts_the_player_in_the_garrison_and_the_foe_outside) {
    SkirmishSetup setup;
    setup.budget = 300;
    setup.defend = true;
    Battle b(1, Scenario::Siege, setup);
    CHECK(b.defending());
    int mine = 0, theirs = 0;
    for (const Squad& s : b.squads()) {
        if (s.owner == Battle::kPlayer) {
            CHECK(s.garrison);
            ++mine;
        } else {
            CHECK(!s.garrison);
            ++theirs;
        }
    }
    CHECK(mine > 0);
    CHECK(theirs > 0);
    CHECK(b.capturePoint().active());
    b.advance(1.0);
    CHECK(b.capturePoint().defendersIn() > 0);  // (the player's men hold the courtyard)
}

TEST(open_ground_has_two_armies_facing_each_other) {
    SkirmishSetup setup;
    setup.budget = 300;
    Battle b(1, Scenario::Field, setup);
    int mine = 0, theirs = 0;
    V3 sum[2]{};
    for (const illyria::sim::Unit& u : b.world().units()) {
        if (!u.alive()) continue;
        (u.owner == Battle::kPlayer ? mine : theirs) += 1;
        const V3 p = toV3(u.pos);
        sum[u.owner == Battle::kPlayer ? 0 : 1] = sum[u.owner == Battle::kPlayer ? 0 : 1] + p;
    }
    CHECK(mine > 40);
    CHECK(theirs > 40);
    const float apart = (V3{sum[0].x / static_cast<float>(mine) - sum[1].x / static_cast<float>(theirs), sum[0].y / static_cast<float>(mine) - sum[1].y / static_cast<float>(theirs), 0}).length();
    CHECK(apart > 100.0f && apart < 260.0f);
}

TEST(what_the_player_bought_is_what_he_fields) {
    SkirmishSetup setup;
    setup.budget = 300;
    setup.army = {{UnitType::HeavyInfantry, 3}, {UnitType::Archers, 2}};
    Battle b(1, Scenario::Ford, setup);
    int heavy = 0, archers = 0;
    for (const Squad& s : b.squads()) {
        if (s.owner != Battle::kPlayer) continue;
        heavy += s.type == UnitType::HeavyInfantry;
        archers += s.type == UnitType::Archers;
    }
    CHECK(heavy == 3);
    CHECK(archers == 2);
}
