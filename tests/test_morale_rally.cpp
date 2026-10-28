// Veterans and rallying (world_morale.cpp): elites recover and rally sooner,
// steady the men around them, and call "Hold the line!" to wavering friends.
#include <cstdio>
#include <vector>

#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;

// Tests shake soldiers directly, as a blow or a hero's fall would.
void shake(World& w, EntityId id, int32_t points) { w.shakeMorale(const_cast<Unit&>(*w.findUnit(id)), points); }

EntityId spawnRanked(World& w, PlayerId owner, int x, int y, UnitType type, int rank) {
    const EntityId id = w.spawnUnit(owner, {m(x), m(y), m(0)}, kEast, unitStats(type));
    w.setVeterancy(id, rank, 0);
    return id;
}

// Ticks until a broken soldier, alone, rallies (-1: not within a minute).
int ticksToRally(int rank) {
    World w{WorldConfig{}};
    const EntityId man = spawnRanked(w, 0, 0, 0, UnitType::LightInfantry, rank);
    shake(w, man, 2000);
    w.step({});
    if (!w.findUnit(man)->routed) return -2;
    for (int t = 1; t < 60 * kTicksPerSecond; ++t) {
        w.step({});
        if (!w.findUnit(man)->routed) return t;
    }
    return -1;
}
}  // namespace

TEST(veterans_rally_sooner_than_recruits) {
    const int recruit = ticksToRally(0), veteran = ticksToRally(2), elite = ticksToRally(3);
    std::printf("  rallied alone after: recruit %.1f s, veteran %.1f s, elite %.1f s\n", recruit / 30.0, veteran / 30.0,
                elite / 30.0);
    CHECK(recruit > 0 && veteran > 0 && elite > 0);
    CHECK(veteran < recruit * 3 / 4);
    CHECK(elite < veteran);
}

TEST(a_veteran_near_makes_recruits_hold_longer) {
    // Two files of six recruits under the same steady hail of shocks; one
    // file has an elite among them (he is not shaken himself).
    const auto run = [](bool withElite, int32_t& moraleAfter) {
        World w{WorldConfig{}};
        std::vector<EntityId> men;
        for (int i = 0; i < 6; ++i) men.push_back(spawnRanked(w, 0, 0, i * 2, UnitType::LightInfantry, 0));
        if (withElite) spawnRanked(w, 0, -2, 5, UnitType::HeavyInfantry, 3);
        w.step({});  // the first census sees who stands near whom
        moraleAfter = 0;
        for (int t = 0; t < 60 * kTicksPerSecond; ++t) {
            for (EntityId id : men) shake(w, id, 5);
            w.step({});
            if (t == 3 * kTicksPerSecond) moraleAfter = w.findUnit(men[0])->morale;
            for (EntityId id : men)
                if (w.findUnit(id)->routed) return t;
        }
        return 60 * kTicksPerSecond;
    };
    int32_t aloneMorale = 0, steadiedMorale = 0;
    const int alone = run(false, aloneMorale), steadied = run(true, steadiedMorale);
    std::printf("  first broke after %.1f s alone, %.1f s beside an elite (morale at 3 s: %d vs %d)\n", alone / 30.0,
                steadied / 30.0, aloneMorale, steadiedMorale);
    CHECK(steadiedMorale > aloneMorale);  // smaller shocks, before any call
    CHECK(steadied > alone * 5 / 4);
}

TEST(a_rally_call_lifts_wavering_friends_once_per_cooldown) {
    World w{WorldConfig{}};
    std::vector<EntityId> men;
    for (int i = 0; i < 4; ++i) men.push_back(spawnRanked(w, 0, 0, i * 2, UnitType::LightInfantry, 0));
    const EntityId vet = spawnRanked(w, 0, -3, 3, UnitType::HeavyInfantry, 2);
    // Someone else's veteran far off hears nothing.
    spawnRanked(w, 0, 200, 0, UnitType::HeavyInfantry, 2);
    for (EntityId id : men) shake(w, id, 600);  // to 340: wavering
    CHECK(w.findUnit(men[0])->wavering());
    std::vector<uint32_t> callTicks;
    int32_t liftedTo = 0;
    for (int t = 0; t < 32 * kTicksPerSecond; ++t) {
        // Keep them shaken, so he would call again as soon as he may.
        if (t % kTicksPerSecond == 0 && t > 0)
            for (EntityId id : men)
                if (w.findUnit(id)->morale > 300) shake(w, id, 150);
        w.step({});
        for (const RallyCall& c : w.rallyCalls()) {
            CHECK(c.caller == vet);
            CHECK(c.kind == RallyCall::Kind::Veteran);
            CHECK(c.heard == 4);
            if (callTicks.empty()) liftedTo = w.findUnit(men[0])->morale;
            callTicks.push_back(w.tick());
        }
    }
    std::printf("  calls at ticks:");
    for (uint32_t t : callTicks) std::printf(" %u", t);
    std::printf(" (wavering lifted to %d)\n", liftedTo);
    CHECK(!callTicks.empty() && callTicks.front() <= 10);  // at the first census
    CHECK(liftedTo >= 340 + 150 - 5);
    CHECK(callTicks.size() >= 2 && callTicks.size() <= 3);
    for (size_t i = 1; i < callTicks.size(); ++i) CHECK(callTicks[i] - callTicks[i - 1] >= 15 * kTicksPerSecond);
}

TEST(a_heros_rally_turns_back_the_broken) {
    World w{WorldConfig{}};
    const EntityId man = spawnRanked(w, 0, 0, 0, UnitType::LightInfantry, 0);
    const EntityId hero = w.spawnUnit(0, {m(-5), m(0), m(0)}, kEast, unitStats(UnitType::Hero));
    // A foe near enough that he could never rally on his own.
    UnitStats still = unitStats(UnitType::HeavyInfantry);
    still.aggroRange = Fixed::zero();
    w.spawnUnit(1, {m(18), m(0), m(0)}, kWest, still);
    shake(w, man, 746);  // to 180: broken
    bool broke = false, back = false;
    for (int t = 0; t < kTicksPerSecond && !back; ++t) {
        w.step({});
        broke = broke || w.findUnit(man)->routed || !w.rallyCalls().empty();
        for (const RallyCall& c : w.rallyCalls()) {
            CHECK(c.caller == hero);
            CHECK(c.kind == RallyCall::Kind::Hero);
            CHECK(c.rallied == 1);
            back = true;
        }
    }
    CHECK(broke);
    CHECK(back);
    CHECK(!w.findUnit(man)->routed);
    CHECK(w.findUnit(man)->morale >= 450);
}

TEST(recruits_never_call_and_the_rally_is_deterministic) {
    const auto run = [](int vetRank, int& calls) {
        World w{WorldConfig{}};
        for (int i = 0; i < 10; ++i) {
            spawnRanked(w, 0, 50, 100 + i * 3, i % 3 ? UnitType::LightInfantry : UnitType::HeavyInfantry, i % 3 ? 0 : vetRank);
            spawnRanked(w, 1, 75, 100 + i * 3, i % 2 ? UnitType::Berserker : UnitType::Warrior, i % 4 == 0 ? vetRank : 0);
        }
        w.spawnUnit(1, {m(80), m(115), m(0)}, kWest, unitStats(UnitType::Hero));
        calls = 0;
        for (int i = 0; i < 40 * kTicksPerSecond; ++i) {
            w.step({});
            calls += static_cast<int>(w.rallyCalls().size());
        }
        return w.checksum();
    };
    int calls = 0, again = 0, green = 0;
    const uint64_t a = run(3, calls);
    CHECK(a == run(3, again));
    CHECK(calls == again);
    run(0, green);
    std::printf("  rally checksum 0x%016llx, %d calls (all-recruit battle: %d, the hero's)\n",
                static_cast<unsigned long long>(a), calls, green);
    CHECK(calls > green);
}
