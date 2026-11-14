// How a Conquest is won (docs/BALANCE.md, "A Conquest that ends"): a lord's seat is his realm, a sacked town pays,
// more than half the towns is the war, and a Warlord's stacks go at a prize together.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "illyria/sim/conquest.h"
#include "conquest_orders.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// The town nearest a cell that `fits`, or -1.
template <typename Fits>
int32_t nearestTown(const Conquest& c, int32_t cx, int32_t cy, Fits fits) {
    int32_t best = -1, bestD = INT32_MAX;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Conquest::CityState& t = c.cities()[i];
        const int32_t d = std::max(std::abs(t.cx - cx), std::abs(t.cy - cy));
        if (d > 0 && d < bestD && fits(static_cast<int32_t>(i))) bestD = d, best = static_cast<int32_t>(i);
    }
    return best;
}
}  // namespace

TEST(a_lords_seat_is_his_realm) {
    Conquest c(continent(), 3);
    const int32_t seat = c.seatOf(1);
    CHECK(seat >= 0 && c.cities()[static_cast<size_t>(seat)].owner == 1 && c.continent().cities()[static_cast<size_t>(seat)].capital);
    for (uint8_t s = 0; s < 8; ++s) CHECK(c.seatOf(s) >= 0 && c.cities()[static_cast<size_t>(c.seatOf(s))].owner == s);
    const Conquest::CityState& home = c.cities()[static_cast<size_t>(seat)];
    // Side 1 holds a second town with a garrison of two squads, and has a stack in the field.
    const int32_t town = nearestTown(c, home.cx, home.cy, [&](int32_t i) { return !c.continent().cities()[static_cast<size_t>(i)].capital; });
    const uint32_t garrison = c.grant(town, 1, UnitType::LightInfantry, 2);
    const Conquest::CityState& t = c.cities()[static_cast<size_t>(town)];
    const uint32_t field = c.raise(1, (home.cx + t.cx) / 2 + 1, (home.cy + t.cy) / 2 + 1, {UnitType::LightInfantry});
    std::vector<uint32_t> squads;
    for (const Army& a : c.findStack(garrison)->armies) squads.push_back(a.id);
    for (int k = 0; k < 30; ++k) c.step();  // (the men are in the world, as side 1's)
    const int32_t chest0 = c.sides()[0].gold, chest1 = c.sides()[1].gold;
    const int32_t sack = home.income * Conquest::kSackDays;
    CHECK(chest1 > 0);

    ConquestOrders::transfer(c, seat, 0);

    // The lord is out of the war, and all he had is the victor's: his chest (and the sack of the seat), his towns, the
    // garrisons of his towns man for man; his men in the field and his heroes are gone.
    CHECK(!c.sides()[1].alive);
    CHECK(c.citiesOwned(1) == 0);
    CHECK(c.cities()[static_cast<size_t>(seat)].owner == 0 && c.cities()[static_cast<size_t>(town)].owner == 0);
    CHECK(c.sides()[1].gold == 0);
    CHECK(c.sides()[0].gold == chest0 + chest1 / 2 / 2 + sack + (chest1 - chest1 / 2));  // (half his seat's share of two towns, then the rest)
    for (const Conquest::Stack& s : c.stacks()) CHECK(s.owner != 1);
    CHECK(c.findStack(field) == nullptr);
    const Conquest::Stack* sworn = c.findStack(garrison);
    CHECK(sworn && sworn->owner == 0 && sworn->armies.size() == squads.size());
    if (sworn)
        for (size_t k = 0; k < sworn->armies.size(); ++k) CHECK(sworn->armies[k].id == squads[k] && !sworn->armies[k].hero());
    CHECK(c.winner() == 0);  // six lords are left to beat
    // They stand in the world as the victor's men, where they stood.
    for (int k = 0; k < 30; ++k) c.step();
    sworn = c.findStack(garrison);
    CHECK(sworn != nullptr);
    if (sworn) {
        int men = 0;
        for (const Army& a : sworn->armies)
            for (const Man& m : a.men) {
                const Unit* u = m.unit ? c.world()->findUnit(m.unit) : nullptr;
                CHECK(u && u->owner == 0);
                ++men;
            }
        CHECK(men == 2 * Conquest::squadSize(UnitType::LightInfantry));
    }
    // A day on, the fallen lord is still out, and nobody plays him.
    for (int k = 0; k < Conquest::kTicksPerDay; ++k) c.step();
    CHECK(!c.sides()[1].alive);
    for (const Conquest::Stack& s : c.stacks()) CHECK(s.owner != 1);
    // A saved game carries on the same.
    const std::string text = c.save();
    const auto loaded = Conquest::load(continent(), text);
    CHECK(loaded && loaded->checksum() == c.checksum());
}

TEST(a_sacked_town_pays_and_a_neutral_one_does_not) {
    Conquest c(continent(), 3);
    const Conquest::CityState& home = c.cities()[static_cast<size_t>(c.seatOf(1))];
    const int32_t town = nearestTown(c, home.cx, home.cy, [&](int32_t i) { return !c.continent().cities()[static_cast<size_t>(i)].capital; });
    const int32_t other = nearestTown(c, home.cx, home.cy, [&](int32_t i) { return i != town && !c.continent().cities()[static_cast<size_t>(i)].capital; });
    c.grant(town, 1, UnitType::LightInfantry, 1);
    int32_t chest0 = c.sides()[0].gold;
    const int32_t chest1 = c.sides()[1].gold;
    ConquestOrders::transfer(c, town, 0);  // a lord's town: half his chest's share for it, and the sack
    CHECK(c.sides()[1].alive);
    CHECK(c.sides()[1].gold == chest1 - chest1 / 2);
    CHECK(c.sides()[0].gold == chest0 + chest1 / 2 / 2 + c.cities()[static_cast<size_t>(town)].income * Conquest::kSackDays);
    chest0 = c.sides()[0].gold;
    ConquestOrders::transfer(c, other, 0);  // a neutral town: nothing but the town
    CHECK(c.sides()[0].gold == chest0);
}

TEST(more_than_half_the_towns_is_the_war_and_a_person_out_of_it_has_lost) {
    Conquest c(continent(), 3);
    c.setHuman(0, true);
    const int32_t half = static_cast<int32_t>(c.cities().size()) / 2;
    // Side 2 takes neutral towns one by one: at half of them the war goes on, with one more it is won.
    for (size_t i = 0; i < c.cities().size() && c.citiesOwned(2) < half; ++i)
        if (c.cities()[i].owner == Conquest::kNeutral) ConquestOrders::transfer(c, static_cast<int32_t>(i), 2);
    CHECK(c.citiesOwned(2) == half && c.winner() == 0);
    for (size_t i = 0; i < c.cities().size() && c.citiesOwned(2) == half; ++i)
        if (c.cities()[i].owner == Conquest::kNeutral) ConquestOrders::transfer(c, static_cast<int32_t>(i), 2);
    CHECK(c.winner() == 3);

    // The one person in the war loses his seat: the war is over for him, and the lord with the most towns has it.
    Conquest d(continent(), 3);
    d.setHuman(0, true);
    CHECK(d.winner() == 0);
    ConquestOrders::transfer(d, d.seatOf(0), 5);
    CHECK(!d.sides()[0].alive);
    CHECK(d.winner() == 6);
}

TEST(a_warlords_stacks_go_at_a_seat_as_a_host) {
    // Side 1 holds a realm of nine towns, and side 0 a town beside its seat. The seat's guard (sixty-one men) is too
    // many for one stack (the men say so: behind a wall they count three times over), and the dice would send no
    // number of armies at such walls. Of three stacks standing together two are enough; they go as one, and the third
    // with them when the prize is only then worth more than the villages each could take alone.
    const auto war = [](int stacks, int32_t& cx, int32_t& cy) {
        auto c = std::make_unique<Conquest>(continent(), 3);
        const Conquest::CityState& home = c->cities()[static_cast<size_t>(c->seatOf(1))];
        cx = home.cx, cy = home.cy;
        const int32_t camp = nearestTown(*c, cx, cy, [&](int32_t i) { return !c->continent().cities()[static_cast<size_t>(i)].capital; });
        const Conquest::CityState& at = c->cities()[static_cast<size_t>(camp)];
        c->raise(1, cx, cy, std::vector<UnitType>(2, UnitType::HeavyInfantry));
        for (size_t i = c->cities().size(); i-- > 0 && c->citiesOwned(1) < 9;)
            if (static_cast<int32_t>(i) != camp && c->cities()[i].owner == Conquest::kNeutral) c->grant(static_cast<int32_t>(i), 1, UnitType::LightInfantry, 1);
        c->grant(camp, 0, UnitType::HeavyInfantry, 8);
        for (int k = 1; k < stacks; ++k) c->raise(0, at.cx, at.cy, std::vector<UnitType>(8, UnitType::HeavyInfantry));
        ConquestOrders::runAi(*c, 0);
        return c;
    };
    const auto marchingOnSeat = [](const Conquest& c, int32_t cx, int32_t cy) {
        int n = 0;
        for (const Conquest::Stack& s : c.stacks()) n += s.owner == 0 && !s.path.empty() && s.path.back() == std::make_pair(cx, cy);
        return n;
    };
    int32_t cx, cy;
    const auto one = war(1, cx, cy);
    CHECK(marchingOnSeat(*one, cx, cy) == 0);
    const auto three = war(3, cx, cy);
    std::printf("  stacks of the host marching on the seat: %d\n", marchingOnSeat(*three, cx, cy));
    CHECK(marchingOnSeat(*three, cx, cy) >= 2);
}

TEST(an_outmatched_computer_lord_bends_the_knee_at_the_council_and_a_person_never) {
    // Everyone holds his seat; one lord (`rich`) is given neutral towns. A lord yields to twice his towns: a Warlord to
    // whoever holds them, a Knight only to a person. The weakest goes (of equals, the first), one at a council, and
    // never the person (side 0, as weak as any).
    const auto war = [](Difficulty d, uint8_t rich, int towns) {
        auto c = std::make_unique<Conquest>(continent(), 3);
        c->setHuman(0, true);
        c->setComputerDifficulty(d);
        for (size_t i = 0; i < c->cities().size() && c->citiesOwned(rich) < towns; ++i)
            if (c->cities()[i].owner == Conquest::kNeutral) ConquestOrders::transfer(*c, static_cast<int32_t>(i), rich);
        for (int k = 0; k < 30; ++k) c->step();  // (the men are in the world)
        ConquestOrders::council(*c);
        return c;
    };
    const auto out = [](const Conquest& c) {
        int n = 0;
        for (const Conquest::Side& s : c.sides()) n += !s.alive;
        return n;
    };
    CHECK(Conquest::kCouncilFrom == 30 && Conquest::kCouncilEvery == 10 && Conquest::kYieldTimes == 2);
    CHECK(out(*war(Difficulty::Hard, 2, 1)) == 0);  // equals: nobody yields
    CHECK(out(*war(Difficulty::Easy, 2, 6)) == 0);  // a Knight does not yield to a computer lord, however strong
    // A Warlord to the computer lord of side 2, a Knight to the person: side 1 in both, the first computer lord of the weakest.
    for (const auto& [d, rich] : {std::make_pair(Difficulty::Hard, uint8_t{2}), std::make_pair(Difficulty::Easy, uint8_t{0})}) {
        const auto c = war(d, rich, 2);
        CHECK(out(*c) == 1);
        CHECK(c->sides()[0].alive && !c->sides()[1].alive);
        CHECK(c->citiesOwned(1) == 0 && c->citiesOwned(rich) == 3);
        const int32_t seat = c->seatOf(1);
        CHECK(c->cities()[static_cast<size_t>(seat)].owner == rich);
        // His seat's guard is the victor's now, his lord gone; nothing of his is left anywhere.
        int sworn = 0, heroes = 0;
        for (const Conquest::Stack& s : c->stacks()) {
            CHECK(s.owner != 1);
            if (s.owner == rich && c->cityAt(s.cx, s.cy) == seat)
                for (const Army& a : s.armies) ++sworn, heroes += a.hero();
        }
        CHECK(sworn >= 3 && heroes == 0);  // the light infantry, the two companies of the guard and the realm's rare troops
        CHECK(c->winner() == 0);
        // The next council takes the next of the weakest, one at a time.
        ConquestOrders::council(*c);
        CHECK(out(*c) == 2 && !c->sides()[rich == 2 ? 3 : 2].alive);
    }
}
