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
    CHECK(w.positionPct(ua, ub, m(10), -m(1)) == 30);   // a 1 in 10 slope, striking down (was 20 before the land pass)
    CHECK(w.positionPct(ua, ub, m(10), -m(3)) == 40);   // at most (was 30)
    CHECK(w.positionPct(ua, ub, m(10), Fixed::fromRatio(1, 2)) == -15);  // striking up (was -10)
    CHECK(w.positionPct(ua, ub, m(10), m(5)) == -35);   // at most (was -20)
    CHECK(World::heightShotPct(m(0)) == 100);
    CHECK(World::heightShotPct(m(1)) == 100);
    CHECK(World::heightShotPct(m(5)) == 120);
    CHECK(World::heightShotPct(m(30)) == 150);
    CHECK(World::heightShotPct(-m(8)) == 65);  // shooting up a hill loses as much (was 100: nothing, before the land pass)
    CHECK(World::heightShotPct(-m(30)) == 60);
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

// --- Land, formations, calls (world_formation.cpp, world_help.cpp) ---

namespace {
// A squad of `n` raised in a rank across y at x, of one formation.
std::vector<EntityId> squadAt(World& w, PlayerId owner, uint32_t squad, FormationKind f, const UnitStats& stats, int n, Fixed x, Fixed y0, const Vec3& facing) {
    std::vector<EntityId> ids;
    for (int i = 0; i < n; ++i) {
        ids.push_back(w.spawnUnit(owner, {x, y0 + Fixed::fromRatio(3 * i, 2), m(0)}, facing, stats));
        w.setSquad(ids.back(), squad, f);
    }
    return ids;
}
int toldCount(const World& w, TacticEvent::Kind kind) {
    int n = 0;
    for (const TacticEvent& e : w.tactics()) n += e.kind == kind;
    return n;
}
bool findGround(const Region& r, Ground want, Vec3& at) {
    for (int y = 40; y < 980; y += 4)
        for (int x = 40; x < 980; x += 4) {
            bool all = true;
            for (int dy = -8; dy <= 8 && all; dy += 4)
                for (int dx = -8; dx <= 8 && all; dx += 4) all = r.groundAt(m(x + dx), m(y + dy)) == want;
            if (all) {
                at = {m(x), m(y), m(0)};
                return true;
            }
        }
    return false;
}
}  // namespace

TEST(every_formation_has_a_price) {
    // The table the manual prints: none of them is free, none is useless.
    const auto& line = World::formationRule(FormationKind::Line);
    const auto& two = World::formationRule(FormationKind::DoubleLine);
    const auto& box = World::formationRule(FormationKind::Box);
    const auto& wedge = World::formationRule(FormationKind::Wedge);
    const auto& loose = World::formationRule(FormationKind::Loose);
    CHECK(line.flankShockPct > 0);                       // a line's flanks are weak
    CHECK(two.shockPct < 0 && two.shotPct > 0);          // two ranks hold their nerve, and are a deeper mark
    CHECK(box.allRound && box.blowPct < 0 && box.shotPct > two.shotPct);  // no flank; poor attack; the arrows' best mark
    CHECK(wedge.blowPct > 0 && wedge.struckAsidePct > 0);                 // strikes hard, dies in the flank
    CHECK(loose.sparePct > 0 && loose.blowPct < 0 && loose.shockPct > 0); // arrows pass; fights and stands worse
    CHECK(World::formationRule(FormationKind::None).blowPct == 0);
}

TEST(a_formation_is_its_men_together_and_in_order) {
    World w(WorldConfig{});
    const auto ids = squadAt(w, 0, 1, FormationKind::DoubleLine, dummy(), 4, m(0), m(0), kEast);
    const EntityId alone = w.spawnUnit(0, {m(60), m(0), m(0)}, kEast, dummy());
    w.setSquad(alone, 2, FormationKind::Box);
    for (int i = 0; i < 12; ++i) w.step({});
    CHECK(w.formed(*w.findUnit(ids[0])));
    CHECK(!w.formed(*w.findUnit(alone)));  // a man by himself is no square
    // Struck in the back by a charge, the men there are out of order until they re-form.
    const EntityId rider = w.spawnUnit(1, {-m(30), m(1), m(0)}, kEast, unitStats(UnitType::LightCavalry));
    Command at{1, rider, Command::Type::AttackUnit, {}};
    at.targetUnit = ids[1];
    w.step(std::vector<Command>{at});
    bool disordered = false, lost = false;
    for (int i = 0; i < 6 * kTicksPerSecond; ++i) {
        w.step({});
        disordered = disordered || toldCount(w, TacticEvent::Kind::Disordered) > 0;
        lost = lost || !w.formed(*w.findUnit(ids[0])) || !w.formed(*w.findUnit(ids[1]));
    }
    CHECK(disordered);
    CHECK(lost);
}

TEST(a_square_has_no_flank_and_a_line_has) {
    // Light horse rides into the back of four pikemen. In a square the charge meets a front, and its pikes; in a line it
    // lands in their backs.
    const auto run = [](FormationKind f, bool& braced, bool& square, bool& rear) {
        World w(WorldConfig{});
        const auto ids = squadAt(w, 1, 1, f, dummy(UnitType::Pikemen), 4, m(0), m(0), kEast);
        for (int i = 0; i < 12; ++i) w.step({});
        const EntityId rider = w.spawnUnit(0, {-m(30), m(2), m(0)}, kEast, unitStats(UnitType::LightCavalry));
        Command c{0, rider, Command::Type::AttackUnit, {}};
        c.targetUnit = ids[1];
        w.step(std::vector<Command>{c});
        braced = square = rear = false;
        for (int i = 0; i < 8 * kTicksPerSecond; ++i) {
            w.step({});
            braced = braced || toldCount(w, TacticEvent::Kind::Braced) > 0;
            square = square || toldCount(w, TacticEvent::Kind::Square) > 0;
            rear = rear || toldCount(w, TacticEvent::Kind::Rear) + toldCount(w, TacticEvent::Kind::Broken) + toldCount(w, TacticEvent::Kind::Flanked) > 0;
        }
    };
    bool braced, square, rear;
    run(FormationKind::Box, braced, square, rear);
    CHECK(braced && square && !rear);
    run(FormationKind::Line, braced, square, rear);
    CHECK(!braced && !square && rear);
}

TEST(loose_order_lets_arrows_by) {
    // Twelve bowmen loose at six men for twenty seconds: in loose order the six take well under what two ranks take.
    const auto hurt = [](FormationKind f) {
        WorldConfig cfg;
        cfg.seed = 7;
        World w(cfg);
        const auto marks = squadAt(w, 1, 1, f, dummy(), 6, m(45), m(0), kWest);
        for (int i = 0; i < 12; ++i) w.spawnUnit(0, {m(0), Fixed::fromRatio(3 * i, 2), m(0)}, kEast, unitStats(UnitType::Archers));
        for (int i = 0; i < 20 * kTicksPerSecond; ++i) w.step({});
        int lost = 0;
        for (EntityId id : marks) lost += w.findUnit(id)->stats.maxHp - w.findUnit(id)->hp;
        return lost;
    };
    const int loose = hurt(FormationKind::Loose), two = hurt(FormationKind::DoubleLine);
    std::printf("  arrows on six men in 20 s: loose order %d, two ranks %d\n", loose, two);
    CHECK(two > 0);
    CHECK(loose * 100 < two * 80);
}

TEST(a_rider_who_has_charged_wheels_away_to_come_again) {
    World w(WorldConfig{});
    const EntityId foe = w.spawnUnit(1, {m(0), m(0), m(0)}, kEast, dummy());  // (his back to the rider: a charge into a front that stands is fought out where it lands)
    const EntityId rider = w.spawnUnit(0, {-m(30), m(0), m(0)}, kEast, unitStats(UnitType::LightCavalry));
    Command c{0, rider, Command::Type::AttackUnit, {}};
    c.targetUnit = foe;
    w.step(std::vector<Command>{c});
    int charges = 0;
    Fixed farthest;
    bool struck = false;
    for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
        w.step({});
        if (toldCount(w, TacticEvent::Kind::Charge) > 0) ++charges, struck = true;
        if (struck) farthest = max(farthest, (w.findUnit(rider)->pos - w.findUnit(foe)->pos).flat().length());
    }
    std::printf("  one rider on one man in 20 s: %d charges home, wheeled out to %.1f m\n", charges, farthest.toDouble());
    CHECK(charges >= 2);              // not one charge and then a standing melee
    CHECK(farthest > m(8));           // he rides clear between them
    CHECK(w.findUnit(rider)->alive());
}

TEST(a_squad_in_trouble_calls_and_the_nearest_free_squad_answers) {
    // Three foot beset by nine, a squad of six friends 45 m off (beyond the 40 m at which a man goes of himself): the
    // three call, the six answer and march. Told to hold, the six stay, and the call goes unanswered.
    const auto run = [](bool hold, int& calls, int& answered, int& fought, uint64_t& sum) {
        World w(WorldConfig{});
        UnitStats tough = unitStats(UnitType::HeavyInfantry);
        tough.maxHp = 4000;
        tough.aggroRange = m(3);  // (they stand where they are and take what comes)
        squadAt(w, 0, 1, FormationKind::DoubleLine, tough, 3, m(0), m(0), kEast);
        const auto friends = squadAt(w, 0, 2, FormationKind::DoubleLine, unitStats(UnitType::HeavyInfantry), 6, -m(45), m(0), kEast);
        std::vector<Command> cmds;
        for (int i = 0; i < 9; ++i) {
            const EntityId f = w.spawnUnit(1, {m(22), Fixed::fromRatio(3 * i, 2) - m(5), m(0)}, kWest, unitStats(UnitType::LightInfantry));
            w.setSquad(f, 1, FormationKind::Line);
            cmds.push_back({1, f, Command::Type::Move, {m(0), m(1), m(0)}});
        }
        if (hold)
            for (EntityId id : friends) {
                Command c{0, id, Command::Type::SetStance, {}};
                c.hold = true;
                cmds.push_back(c);
            }
        w.step(cmds);
        calls = answered = fought = 0;
        for (int i = 0; i < 75 * kTicksPerSecond; ++i) {
            w.step({});
            for (const HelpCall& c : w.helpCalls())
                if (c.owner == 0) {
                    calls += c.squad == 1;
                    if (c.answer != 0) {
                        ++answered;
                        CHECK(c.answer == 2);
                    }
                }
            for (EntityId id : friends) fought += w.findUnit(id)->engaged;
        }
        sum = w.checksum();
    };
    int calls, answered, fought;
    uint64_t sum, again;
    run(false, calls, answered, fought, sum);
    std::printf("  three beset for 75 s: %d calls, %d answered, the six fought %d man-ticks\n", calls, answered, fought);
    CHECK(calls >= 1 && calls <= 3);  // it calls, and not every second (kCallAgainTicks)
    CHECK(answered >= 1);
    CHECK(fought > 0);
    run(false, calls, answered, fought, again);
    CHECK(sum == again);  // the same every time: no clock, no chance outside the world's dice
    run(true, calls, answered, fought, sum);
    CHECK(calls >= 1);
    CHECK(answered == 0);
    CHECK(fought == 0);  // hold means hold
}

TEST(horse_that_answer_take_the_flank) {
    // Twelve foot hold twenty-four; eight light horse stand 90 m behind their own line with no order. They are called,
    // and come round: their charges land in flanks and backs, not on the front.
    World w(WorldConfig{});
    UnitStats line = unitStats(UnitType::HeavyInfantry);
    line.maxHp = 600;  // (they stand long enough to be helped)
    squadAt(w, 0, 1, FormationKind::DoubleLine, line, 12, m(0), -m(9), kEast);
    const auto horse = squadAt(w, 0, 2, FormationKind::Wedge, unitStats(UnitType::LightCavalry), 8, -m(90), -m(6), kEast);
    std::vector<Command> cmds;
    for (int i = 0; i < 24; ++i) {
        const EntityId f = w.spawnUnit(1, {m(40 + (i / 12) * 2), Fixed::fromRatio(3 * (i % 12), 2) - m(9), m(0)}, kWest, unitStats(UnitType::LightInfantry));
        w.setSquad(f, 1, FormationKind::DoubleLine);
        cmds.push_back({1, f, Command::Type::Move, {m(0), Fixed::fromRatio(3 * (i % 12), 2) - m(9), m(0)}});
    }
    w.step(cmds);
    int front = 0, aside = 0, answers = 0;
    for (int i = 0; i < 60 * kTicksPerSecond; ++i) {
        w.step({});
        // (The commander sends them: of themselves they would ride straight to their friends; the flank is an order.)
        std::vector<Command> send;
        for (const HelpCall& c : w.helpCalls()) {
            if (c.owner != 0 || c.squad != 1) continue;
            ++answers;
            for (EntityId id : horse) {
                Command a{0, id, Command::Type::Answer, {}};
                a.targetUnit = c.caller;
                send.push_back(a);
            }
        }
        if (!send.empty()) w.step(send);
        for (const TacticEvent& e : w.tactics())
            if (e.kind == TacticEvent::Kind::Charge && e.owner == 1) (e.detail == 0 ? front : aside) += 1;
    }
    int left = 0;
    for (EntityId id : horse) left += w.findUnit(id)->alive();
    std::printf("  horse called from 90 m: %d answers, charges %d in the flank or back, %d on the front, %d of 8 riders left\n", answers, aside, front, left);
    CHECK(answers >= 1);
    CHECK(aside >= 4);
    CHECK(left >= 5);
}

TEST(answering_is_a_march_of_his_own_that_any_order_ends) {
    World w(WorldConfig{});
    const EntityId caller = w.spawnUnit(0, {m(60), m(0), m(0)}, kEast, dummy());
    const EntityId man = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::HeavyInfantry));
    CHECK(w.freeToAnswer(*w.findUnit(man)));
    Command c{0, man, Command::Type::Answer, {}};
    c.targetUnit = caller;
    w.step(std::vector<Command>{c});
    CHECK(w.findUnit(man)->helping == caller);
    CHECK(!w.freeToAnswer(*w.findUnit(man)));
    for (int i = 0; i < 2 * kTicksPerSecond; ++i) w.step({});
    CHECK(w.findUnit(man)->pos.x > m(2));  // on his way
    w.step(std::vector<Command>{{0, man, Command::Type::Stop, {}}});
    CHECK(w.findUnit(man)->helping == kNoEntity);
    // ... and he is there when he arrives: free again.
    w.step(std::vector<Command>{c});
    for (int i = 0; i < 40 * kTicksPerSecond && w.findUnit(man)->helping != kNoEntity; ++i) w.step({});
    CHECK(w.findUnit(man)->helping == kNoEntity);
    CHECK((w.findUnit(man)->pos - w.findUnit(caller)->pos).flat().length() < m(6));
}

TEST(woods_hide_men_and_stop_arrows_and_mud_mires) {
    WorldConfig cfg;
    cfg.region = illyriaRegion();
    Vec3 wood, mud;
    if (!findGround(*cfg.region, Ground::Forest, wood)) {
        std::printf("  (no wood on this ground: not tested)\n");
        return;
    }
    {
        World w(cfg);
        const EntityId hidden = w.spawnUnit(0, wood, kEast, dummy());
        const EntityId far = w.spawnUnit(1, wood + Vec3{m(50), m(0), m(0)}, kWest, unitStats(UnitType::Archers));
        for (int i = 0; i < 3 * kTicksPerSecond; ++i) w.step({});
        CHECK(w.inWoods(*w.findUnit(hidden)));
        CHECK(w.concealed(*w.findUnit(hidden), 1));
        CHECK(!w.concealed(*w.findUnit(hidden), 0));     // never from his own side
        CHECK(w.findUnit(far)->reloadRemaining == 0);    // the bowman has not loosed at a man he cannot see
        w.spawnUnit(1, wood + Vec3{m(12), m(0), m(0)}, kWest, dummy());
        for (int i = 0; i < kTicksPerSecond; ++i) w.step({});
        CHECK(!w.concealed(*w.findUnit(hidden), 1));     // a foe within 25 m sees him
    }
    if (findGround(*cfg.region, Ground::Mud, mud) || findGround(*cfg.region, Ground::Swamp, mud)) {
        World w(cfg);
        const EntityId stuck = w.spawnUnit(0, mud, kEast, unitStats(UnitType::HeavyInfantry));
        const EntityId dry = w.spawnUnit(1, {m(500), m(600), m(0)}, kWest, unitStats(UnitType::HeavyInfantry));
        CHECK(w.footing(*w.findUnit(stuck)) == World::Footing::Mire);
        CHECK(w.positionPct(*w.findUnit(stuck), *w.findUnit(dry), m(1), m(0)) == -25);
        CHECK(w.positionPct(*w.findUnit(dry), *w.findUnit(stuck), m(1), m(0)) == 25);
        CHECK(w.wadeShotPct(*w.findUnit(stuck)) == 130);
    }
}

TEST(a_squad_keeps_the_ground_it_was_ordered_to) {
    // A man marched to a place by an order is posted there: a friend fighting 30 m off does not draw him away (with no
    // order he would go). Stop frees him, and he goes.
    World w(WorldConfig{});
    UnitStats tough = unitStats(UnitType::HeavyInfantry);
    tough.maxHp = 5000;
    const EntityId posted = w.spawnUnit(0, {m(0), m(8), m(0)}, kEast, unitStats(UnitType::HeavyInfantry));
    w.step(std::vector<Command>{{0, posted, Command::Type::Move, {m(0), m(0), m(0)}}});
    for (int i = 0; i < 8 * kTicksPerSecond; ++i) w.step({});
    CHECK(w.findUnit(posted)->posted);
    CHECK(!w.freeToAnswer(*w.findUnit(posted)));
    w.spawnUnit(0, {m(30), m(0), m(0)}, kEast, tough);
    UnitStats weak = tough;
    weak.melee.damage = 1;
    const EntityId foe = w.spawnUnit(1, {m(32), m(0), m(0)}, kWest, weak);
    for (int i = 0; i < 8 * kTicksPerSecond; ++i) w.step({});
    CHECK((w.findUnit(posted)->pos - Vec3{m(0), m(0), m(0)}).flat().length() < m(3));  // he kept his ground
    w.step(std::vector<Command>{{0, posted, Command::Type::Stop, {}}});
    CHECK(!w.findUnit(posted)->posted);
    for (int i = 0; i < 14 * kTicksPerSecond; ++i) w.step({});
    CHECK((w.findUnit(posted)->pos - w.findUnit(foe)->pos).flat().length() < m(6));
}
