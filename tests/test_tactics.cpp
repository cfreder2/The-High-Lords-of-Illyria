// Position, support and the Rally (engine/sim/src/world_tactics.cpp): what makes a plan pay.
#include <cstdio>
#include <memory>
#include <vector>

#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;

std::shared_ptr<const Region> illyriaRegion() {
    static const auto region = std::make_shared<const Region>(Region::generateIllyria(20261027));
    return region;
}

// A still target: it never fights back or turns, and has no shield.
UnitStats dummy(UnitType type = UnitType::HeavyInfantry) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged.autoRange = Fixed::zero();
    s.shield.blockChance = Fixed::zero();
    s.maxHp = 5000;
    return s;
}

void shake(World& w, EntityId id, int32_t points) { w.shakeMorale(const_cast<Unit&>(*w.findUnit(id)), points); }

bool told(const World& w, TacticEvent::Kind kind, EntityId unit) {
    for (const TacticEvent& e : w.tactics())
        if (e.kind == kind && e.unit == unit) return true;
    return false;
}
}  // namespace

TEST(a_man_wading_strikes_weaker_and_is_struck_harder) {
    WorldConfig cfg;
    cfg.region = illyriaRegion();
    World w(cfg);
    const Vec3 ford = cfg.region->ford();
    const EntityId wet = w.spawnUnit(0, ford, kEast, unitStats(UnitType::HeavyInfantry));
    const EntityId dry = w.spawnUnit(1, {m(500), m(600), m(0)}, kWest, unitStats(UnitType::HeavyInfantry));
    const EntityId giant = w.spawnUnit(0, ford + Vec3{m(2), m(0), m(0)}, kEast, unitStats(UnitType::Giants));
    CHECK(w.wading(*w.findUnit(wet)));
    CHECK(!w.wading(*w.findUnit(dry)));
    CHECK(!w.wading(*w.findUnit(giant)));  // a giant wades as he walks
    // On the level (no height between them): the wader half as strong, the man on the bank half again as strong against him.
    CHECK(w.positionPct(*w.findUnit(wet), *w.findUnit(dry), m(1), m(0)) == -50);
    CHECK(w.positionPct(*w.findUnit(dry), *w.findUnit(wet), m(1), m(0)) == 50);
    CHECK(w.positionPct(*w.findUnit(dry), *w.findUnit(giant), m(1), m(0)) == 0);
    // Missiles: three quarters more on a man in the water.
    CHECK(w.wadeShotPct(*w.findUnit(wet)) == 175);
    CHECK(w.wadeShotPct(*w.findUnit(dry)) == 100);
}

TEST(the_high_ground_tells_in_a_melee_and_a_volley) {
    World w{WorldConfig{}};
    const EntityId a = w.spawnUnit(0, {}, kEast, unitStats(UnitType::HeavyInfantry));
    const EntityId b = w.spawnUnit(1, {m(2), m(0), m(0)}, kWest, unitStats(UnitType::HeavyInfantry));
    const Unit& ua = *w.findUnit(a);
    const Unit& ub = *w.findUnit(b);
    CHECK(w.positionPct(ua, ub, m(10), m(0)) == 0);
    CHECK(w.positionPct(ua, ub, m(10), -m(1)) == 20);   // a 1 in 10 slope, striking down
    CHECK(w.positionPct(ua, ub, m(10), -m(3)) == 30);   // at most
    CHECK(w.positionPct(ua, ub, m(10), Fixed::fromRatio(1, 2)) == -10);  // striking up
    CHECK(w.positionPct(ua, ub, m(10), m(5)) == -20);   // at most
    CHECK(World::heightShotPct(m(0)) == 100);
    CHECK(World::heightShotPct(m(1)) == 100);
    CHECK(World::heightShotPct(m(5)) == 120);
    CHECK(World::heightShotPct(m(30)) == 150);
    CHECK(World::heightShotPct(-m(8)) == 100);
}

TEST(a_charge_into_the_back_of_men_fighting_breaks_them_on_the_spot) {
    // Two foot face a friend of the riders' and trade blows with him; light horse rides into their backs. Met head on by
    // the same two foot with nobody else on them, the charge breaks no one.
    const auto run = [](bool fromBehind, int& broken, bool& word) {
        World w(WorldConfig{});
        UnitStats foe = unitStats(UnitType::LightInfantry);
        foe.maxHp = 5000;
        const EntityId f1 = w.spawnUnit(1, {m(0), m(0), m(0)}, kEast, foe);
        const EntityId f2 = w.spawnUnit(1, {m(0), m(2), m(0)}, kEast, foe);
        UnitStats anvil = unitStats(UnitType::HeavyInfantry);
        anvil.maxHp = 5000;
        anvil.melee.damage = 1;
        if (fromBehind) w.spawnUnit(0, {m(2), m(1), m(0)}, kWest, anvil);
        w.spawnUnit(0, {fromBehind ? -m(30) : m(34), m(0), m(0)}, fromBehind ? kEast : kWest, unitStats(UnitType::LightCavalry));
        w.spawnUnit(0, {fromBehind ? -m(30) : m(34), m(2), m(0)}, fromBehind ? kEast : kWest, unitStats(UnitType::LightCavalry));
        broken = 0;
        word = false;
        for (int i = 0; i < 8 * kTicksPerSecond; ++i) {
            w.step({});
            word = word || told(w, TacticEvent::Kind::Broken, f1) || told(w, TacticEvent::Kind::Broken, f2);
        }
        broken = w.findUnit(f1)->routed + w.findUnit(f2)->routed;
    };
    int rear, front;
    bool rearWord, frontWord;
    run(true, rear, rearWord);
    run(false, front, frontWord);
    std::printf("  light horse on two foot busy with another: from behind %d broken, from the front %d\n", rear, front);
    CHECK(rear == 2);
    CHECK(rearWord);
    CHECK(front == 0);
    CHECK(!frontWord);
}

TEST(braced_pikes_stop_horse_cold_and_say_so) {
    World w(WorldConfig{});
    const EntityId knight = w.spawnUnit(0, {m(28), m(0), m(0)}, kWest, unitStats(UnitType::HeavyCavalry));
    UnitStats pike = dummy(UnitType::Pikemen);
    const EntityId p = w.spawnUnit(1, {}, kEast, pike);
    bool braced = false;
    for (int i = 0; i < 8 * kTicksPerSecond && !braced; ++i) {
        w.step({});
        braced = told(w, TacticEvent::Kind::Braced, p);
    }
    CHECK(braced);
    const Unit& k = *w.findUnit(knight);
    std::printf("  heavy horse on braced pikes: loses %d of %d, morale %d\n", k.stats.maxHp - k.hp, k.stats.maxHp, k.morale);
    CHECK(k.stats.maxHp - k.hp >= 60);
    CHECK(k.morale < kMoraleFull);
    CHECK(w.findUnit(p)->hp == pike.maxHp);
}

TEST(a_man_with_no_order_goes_to_the_help_of_a_friend_fighting_near_him) {
    // A friend and a foe trade blows 32 m from a man with no order (beyond his own aggro reach of 30 m at most).
    const auto run = [](bool hold) {
        World w(WorldConfig{});
        UnitStats tough = unitStats(UnitType::HeavyInfantry);
        tough.maxHp = 5000;
        w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, tough);
        const EntityId foe = w.spawnUnit(1, {m(2), m(0), m(0)}, kWest, tough);
        const EntityId idle = w.spawnUnit(0, {-m(34), m(0), m(0)}, kEast, unitStats(UnitType::HeavyInfantry));
        if (hold) {
            Command c{0, idle, Command::Type::SetStance, {}};
            c.hold = true;
            w.step(std::span(&c, 1));
            CHECK(w.findUnit(idle)->hold);
        }
        bool struck = false;
        for (int i = 0; i < 40 * kTicksPerSecond && !struck; ++i) {
            w.step({});
            struck = w.findUnit(idle)->foe == foe;
        }
        return std::pair{struck, w.findUnit(idle)->pos.x};
    };
    const auto [helped, x] = run(false);
    const auto [heldHelped, heldX] = run(true);
    CHECK(helped);
    CHECK(x > -m(6));
    CHECK(!heldHelped);
    CHECK(heldX == -m(34));  // a man told to hold has not moved a step
}

TEST(horse_coming_to_help_rides_round_to_the_flank) {
    World w(WorldConfig{});
    UnitStats tough = unitStats(UnitType::HeavyInfantry);
    tough.maxHp = 5000;
    w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, tough);
    const EntityId foe = w.spawnUnit(1, {m(2), m(0), m(0)}, kWest, tough);
    const EntityId horse = w.spawnUnit(0, {-m(30), m(3), m(0)}, kEast, unitStats(UnitType::LightCavalry));
    bool word = false;
    Fixed widest = Fixed::zero();
    for (int i = 0; i < 30 * kTicksPerSecond && !word; ++i) {
        w.step({});
        widest = max(widest, abs(w.findUnit(horse)->pos.y));
        word = told(w, TacticEvent::Kind::Rear, foe) || told(w, TacticEvent::Kind::Flanked, foe) || told(w, TacticEvent::Kind::Broken, foe);
    }
    std::printf("  the horse swung %.1f m wide of the fight\n", widest.toDouble());
    CHECK(widest > m(8));  // out to the wing, not in at the front past his friend
    CHECK(word);           // and his charge landed in the flank or the back
}

TEST(the_lords_rally_brings_back_every_broken_man_in_earshot_and_then_waits) {
    World w{WorldConfig{}};
    const EntityId lord = w.spawnUnit(0, {}, kEast, unitStats(UnitType::Hero));
    std::vector<EntityId> near, far;
    for (int i = 0; i < 4; ++i) near.push_back(w.spawnUnit(0, {m(25), m(i * 2), m(0)}, kEast, unitStats(UnitType::LightInfantry)));
    far.push_back(w.spawnUnit(0, {m(70), m(0), m(0)}, kEast, unitStats(UnitType::LightInfantry)));
    for (EntityId id : near) shake(w, id, 2000);
    shake(w, far[0], 2000);
    w.step({});
    for (EntityId id : near) CHECK(w.findUnit(id)->routed);
    CHECK(w.canRally(*w.findUnit(lord)));
    CHECK(w.rallyHeard(*w.findUnit(lord)) == 4);
    const uint64_t before = w.checksum();
    const Command call{0, lord, Command::Type::Rally, {}};
    // Not another side's to give.
    Command theirs = call;
    theirs.player = 1;
    w.step(std::span(&theirs, 1));
    CHECK(w.findUnit(near[0])->routed);
    (void)before;
    w.step(std::span(&call, 1));
    CHECK(w.rallyCalls().size() == 1);
    CHECK(w.rallyCalls()[0].kind == RallyCall::Kind::Order);
    CHECK(w.rallyCalls()[0].rallied == 4);
    for (EntityId id : near) CHECK(!w.findUnit(id)->routed);
    CHECK(w.findUnit(far[0])->routed);  // out of earshot (40 m)
    CHECK(!w.canRally(*w.findUnit(lord)));
    CHECK(w.findUnit(lord)->rallyCooldown > 50 * kTicksPerSecond);
    // Called again at once: nothing.
    shake(w, near[0], 2000);
    w.step({});
    w.step(std::span(&call, 1));
    CHECK(w.rallyCalls().empty());
    CHECK(w.findUnit(near[0])->routed);
}

TEST(a_broken_mans_rally_countdown_is_halved_by_his_lord_within_20_m) {
    const auto ticks = [](int lordAt, int& real) {
        World w{WorldConfig{}};
        const EntityId man = w.spawnUnit(0, {}, kEast, unitStats(UnitType::LightInfantry));
        if (lordAt > 0) {
            UnitStats lord = unitStats(UnitType::Hero);
            w.spawnUnit(0, {m(lordAt), m(0), m(0)}, kEast, lord);
        }
        const_cast<Unit&>(*w.findUnit(man)).callCooldown = 0;
        shake(w, man, 2000);
        w.step({});
        const int32_t told = w.rallyTicks(*w.findUnit(man));
        real = -1;
        for (int t = 1; t < 90 * kTicksPerSecond; ++t) {
            // (The lord's own "Hold the line!" would bring him back at once: this is the countdown without it.)
            for (const Unit& u : w.units()) const_cast<Unit&>(u).callCooldown = 100;
            w.step({});
            if (!w.findUnit(man)->routed) {
                real = t;
                break;
            }
        }
        return told;
    };
    int aloneReal, closeReal, farReal;
    const int32_t alone = ticks(0, aloneReal), close = ticks(15, closeReal), far = ticks(28, farReal);
    std::printf("  rally countdown: alone %d ticks (took %d), lord at 15 m %d (took %d), lord at 28 m %d (took %d)\n", alone, aloneReal, close, closeReal, far,
                farReal);
    CHECK(alone > 0 && aloneReal > 0);
    CHECK(std::abs(alone - aloneReal) <= 12);  // the card's countdown is the truth, to a census
    CHECK(std::abs(close - closeReal) <= 12);
    CHECK(close * 2 <= far + 12);              // within 20 m: half the time of a lord who only leads from 28 m
    CHECK(close < alone);
    // A foe within his safe reach: no countdown at all.
    World w{WorldConfig{}};
    const EntityId man = w.spawnUnit(0, {}, kEast, unitStats(UnitType::LightInfantry));
    w.spawnUnit(1, {m(10), m(0), m(0)}, kWest, dummy());
    shake(w, man, 2000);
    w.step({});
    CHECK(w.rallyTicks(*w.findUnit(man)) == -1);
}

TEST(idle_means_no_order_nothing_to_fight_and_not_holding) {
    World w{WorldConfig{}};
    const EntityId a = w.spawnUnit(0, {}, kEast, unitStats(UnitType::HeavyInfantry));
    w.step({});
    CHECK(World::standsIdle(*w.findUnit(a)));
    const Command go{0, a, Command::Type::Move, {m(30), m(0), m(0)}};
    w.step(std::span(&go, 1));
    CHECK(!World::standsIdle(*w.findUnit(a)));
    Command hold{0, a, Command::Type::SetStance, {}};
    hold.hold = true;
    w.step(std::span(&hold, 1));
    CHECK(w.findUnit(a)->hasMoveTarget);  // a stance touches no order in hand
    const Command stop{0, a, Command::Type::Stop, {}};
    w.step(std::span(&stop, 1));
    CHECK(!World::standsIdle(*w.findUnit(a)));  // holding is meant, not idle
    hold.hold = false;
    w.step(std::span(&hold, 1));
    CHECK(World::standsIdle(*w.findUnit(a)));
}

TEST(a_man_with_no_order_keeps_the_bank_and_an_ordered_one_wades_in) {
    WorldConfig cfg;
    cfg.region = illyriaRegion();
    const Region& r = *cfg.region;
    const Vec3 ford = r.ford();
    // The first dry ground west of the ford, and a place 4 m back from the water's edge.
    Fixed x = ford.x;
    const auto wet = [&](Fixed at) { return r.groundAt(at, ford.y) == Ground::Shallows || r.groundAt(at, ford.y) == Ground::DeepWater; };
    while (wet(x)) x -= Fixed::one();
    const Vec3 bank{x - m(4), ford.y, m(0)};
    const auto run = [&](bool ordered) {
        World w(cfg);
        const EntityId foe = w.spawnUnit(1, {x + m(6), ford.y, m(0)}, kWest, dummy());  // standing in the ford
        CHECK(w.wading(*w.findUnit(foe)));
        const EntityId man = w.spawnUnit(0, bank, kEast, unitStats(UnitType::HeavyInfantry));
        CHECK(!w.wading(*w.findUnit(man)));
        if (ordered) {
            Command c{0, man, Command::Type::AttackUnit, w.findUnit(foe)->pos};
            c.targetUnit = foe;
            w.step(std::span(&c, 1));
        }
        bool waded = false;
        for (int t = 0; t < 15 * kTicksPerSecond; ++t) {
            w.step({});
            waded = waded || w.wading(*w.findUnit(man));
        }
        return waded;
    };
    CHECK(!run(false));  // on his own he lets the foe come to the bank
    CHECK(run(true));    // an order takes him across
}

TEST(a_line_on_the_bank_is_not_cowed_by_the_numbers_wading_at_it) {
    // One man, told to hold, with six foes standing within 15 m of him: in the water they count as three (he is shaken
    // by two more than his own number, not five), on open ground as six.
    const auto moraleAfter = [](bool ford) {
        WorldConfig cfg;
        if (ford) cfg.region = illyriaRegion();
        World w(cfg);
        Vec3 bank{}, first{m(5), m(0), m(0)};
        if (ford) {
            const Region& r = *cfg.region;
            Fixed x = r.ford().x;
            while (r.groundAt(x, r.ford().y) == Ground::Shallows || r.groundAt(x, r.ford().y) == Ground::DeepWater) x -= Fixed::one();
            bank = {x - m(2), r.ford().y, m(0)};
            first = {x + m(3), r.ford().y, m(0)};
        }
        const EntityId man = w.spawnUnit(0, bank, kEast, unitStats(UnitType::HeavyInfantry));
        Command hold{0, man, Command::Type::SetStance, {}};
        hold.hold = true;
        int wet = 0;
        for (int k = 0; k < 6; ++k) {
            const EntityId foe = w.spawnUnit(1, first + Vec3{m(k / 2), m(k % 2 ? 2 : -2), m(0)}, kWest, dummy());
            wet += w.wading(*w.findUnit(foe));
        }
        CHECK(wet == (ford ? 6 : 0));
        w.step(std::span(&hold, 1));
        for (int t = 0; t < 3 * kTicksPerSecond; ++t) w.step({});
        return w.findUnit(man)->morale;
    };
    const int32_t bank = moraleAfter(true), open = moraleAfter(false);
    std::printf("  morale after 3 s before six foes: on the bank, they in the water %d; on open ground %d\n", bank, open);
    CHECK(bank > open + 150);
    CHECK(open < kMoraleFull - 300);
}

TEST(tactics_replay_to_the_same_checksum) {
    const auto run = [] {
        World w(WorldConfig{});
        std::vector<EntityId> mine;
        for (int i = 0; i < 6; ++i) mine.push_back(w.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, unitStats(UnitType::HeavyInfantry)));
        for (int i = 0; i < 6; ++i) w.spawnUnit(1, {m(20), m(i * 2), m(0)}, kWest, unitStats(UnitType::LightInfantry));
        for (int i = 0; i < 4; ++i) w.spawnUnit(0, {-m(30), m(i * 2), m(0)}, kEast, unitStats(UnitType::LightCavalry));
        for (int i = 0; i < 4; ++i) w.spawnUnit(0, {-m(20), m(20 + i * 2), m(0)}, kEast, unitStats(UnitType::Pikemen));
        const EntityId lord = w.spawnUnit(0, {-m(10), m(4), m(0)}, kEast, unitStats(UnitType::Hero));
        for (int t = 0; t < 20 * kTicksPerSecond; ++t) {
            std::vector<Command> cmds;
            if (t == 5) {
                Command c{0, mine[0], Command::Type::SetStance, {}};
                c.hold = true;
                cmds.push_back(c);
            }
            if (t == 300) cmds.push_back({0, lord, Command::Type::Rally, {}});
            w.step(cmds);
        }
        return w.checksum();
    };
    CHECK(run() == run());
}
