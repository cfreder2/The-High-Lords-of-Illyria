// Facing, charges, veterans and height in battle (world_combat.cpp).
#include <cstdio>
#include <memory>
#include <tuple>
#include <vector>

#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
Fixed r(int64_t n, int64_t d) { return Fixed::fromRatio(n, d); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;
const Vec3 kNorth{Fixed::zero(), Fixed::one(), Fixed::zero()};

// A tough, still target: it never fights back or turns, and has no shield.
UnitStats dummy(UnitType type = UnitType::HeavyInfantry) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged.autoRange = Fixed::zero();
    s.shield.blockChance = Fixed::zero();
    s.maxHp = 5000;
    return s;
}

// Ground rising `rise` metres per metre eastward, 256 m square.
std::shared_ptr<const Terrain> ramp(Fixed rise) {
    const int32_t cells = 64;
    std::vector<Fixed> samples;
    for (int32_t y = 0; y <= cells; ++y)
        for (int32_t x = 0; x <= cells; ++x) samples.push_back(m(x * 4) * rise);
    return std::make_shared<const Terrain>(Terrain::fromSamples(cells, m(4), std::move(samples)));
}

// Damage dealt by the first blow that lands on `target`, within `ticks`.
int32_t firstBlow(World& w, EntityId target, int ticks = 20 * kTicksPerSecond) {
    const int32_t before = w.findUnit(target)->hp;
    for (int i = 0; i < ticks; ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts())
            if (e.hitUnit == target && e.kind == ImpactEvent::Kind::Hit) return before - w.findUnit(target)->hp;
    }
    return 0;
}
}  // namespace

TEST(hit_sides_follow_the_targets_facing) {
    CHECK(hitSide(kEast, kWest) == HitSide::Front);  // coming at its face
    CHECK(hitSide(kEast, kEast) == HitSide::Rear);   // travelling the way it faces
    CHECK(hitSide(kEast, kNorth) == HitSide::Flank);
}

TEST(blows_in_the_rear_hurt_more_than_in_the_flank_more_than_the_front) {
    // A berserker strikes a still soldier from in front, from the side and from behind.
    const auto blow = [](const Vec3& from) {
        World w(WorldConfig{});
        w.spawnUnit(0, from * m(2), -from, unitStats(UnitType::Berserker));
        const EntityId t = w.spawnUnit(1, {}, kEast, dummy());
        return firstBlow(w, t);
    };
    const int32_t front = blow(kEast), flank = blow(kNorth), rear = blow(kWest);
    std::printf("  berserker blow: front %d, flank %d, rear %d\n", front, flank, rear);
    CHECK(front > 0);
    CHECK(flank > front && rear > flank);
    CHECK_NEAR(static_cast<double>(rear) / front, 2.0, 0.1);
    CHECK_NEAR(static_cast<double>(flank) / front, 1.5, 0.1);
}

TEST(missiles_in_the_back_hurt_more) {
    const auto volley = [](bool behind) {
        World w(WorldConfig{});
        w.spawnUnit(0, {m(behind ? -40 : 40), m(0), m(0)}, behind ? kEast : kWest, unitStats(UnitType::Archers));
        const EntityId t = w.spawnUnit(1, {}, kEast, dummy());
        return firstBlow(w, t);
    };
    const int32_t front = volley(false), rear = volley(true);
    std::printf("  arrow: front %d, rear %d\n", front, rear);
    CHECK(front > 0 && rear > front);
}

TEST(shields_never_block_from_behind_and_a_busy_soldier_is_off_guard) {
    // Two warriors trade blows; a berserker comes up behind the first.
    World w(WorldConfig{});
    const EntityId a = w.spawnUnit(0, {}, kEast, unitStats(UnitType::Warrior));
    w.spawnUnit(1, {r(13, 10), m(0), m(0)}, kWest, unitStats(UnitType::Warrior));
    int fromBehind = 0, blockedBehind = 0, blockedFront = 0;
    for (int i = 0; i < 9 * kTicksPerSecond && w.findUnit(a)->alive(); ++i) {
        if (i == 3 * kTicksPerSecond) w.spawnUnit(1, {-m(4), m(0), m(0)}, kEast, unitStats(UnitType::Berserker));
        w.step({});
        const Unit* u = w.findUnit(a);
        for (const ImpactEvent& e : w.impacts()) {
            if (e.hitUnit != a || e.projectile != kNoEntity) continue;
            const bool behind = e.dir.dot(u->facing) > Fixed::zero();
            fromBehind += behind && e.kind == ImpactEvent::Kind::Hit;
            blockedBehind += behind && e.kind == ImpactEvent::Kind::Blocked;
            blockedFront += !behind && e.kind == ImpactEvent::Kind::Blocked;
        }
    }
    std::printf("  hits from behind %d (blocked %d), blocked in front %d\n", fromBehind, blockedBehind, blockedFront);
    CHECK(fromBehind > 0);
    CHECK(blockedBehind == 0);
    CHECK(blockedFront > 0);
    // Busy with the man in front, he never turned to face the one behind.
    CHECK(!w.findUnit(a)->alive() || w.findUnit(a)->facing.x > Fixed::zero());
}

TEST(soldiers_turn_at_a_limited_rate) {
    World w(WorldConfig{});
    const EntityId t = w.spawnUnit(1, {}, kEast, unitStats(UnitType::LightInfantry));
    w.spawnUnit(0, {-m(1), m(0), m(0)}, kEast, dummy(UnitType::LightInfantry));
    w.step({});
    // A quarter turn or less in the first tick; facing the foe within a second.
    CHECK(w.findUnit(t)->facing.x > r(9, 10));
    for (int i = 0; i < kTicksPerSecond; ++i) w.step({});
    CHECK(w.findUnit(t)->facing.x < -r(9, 10));
}

TEST(a_charge_downhill_hurts_more_than_on_the_flat_more_than_uphill) {
    // Heavy cavalry charges a still soldier 28 m away down, along and up a 1 in 4 slope.
    const auto chargeBlow = [](int dirSign, bool flat) {
        WorldConfig cfg;
        cfg.terrain = ramp(flat ? Fixed::zero() : r(1, 4));
        World w(cfg);
        const Fixed x0 = m(128) + m(14) * dirSign;
        const Vec3 facing = dirSign > 0 ? kWest : kEast;
        w.spawnUnit(0, {x0, m(128), m(0)}, facing, unitStats(UnitType::HeavyCavalry));
        const EntityId t = w.spawnUnit(1, {x0 - m(28) * dirSign, m(128), m(0)}, -facing, dummy());
        return firstBlow(w, t);
    };
    const int32_t down = chargeBlow(1, false), flat = chargeBlow(1, true), up = chargeBlow(-1, false);
    // A plain blow, for comparison: no run-up.
    World w(WorldConfig{});
    w.spawnUnit(0, {m(2), m(0), m(0)}, kWest, unitStats(UnitType::HeavyCavalry));
    const int32_t standing = firstBlow(w, w.spawnUnit(1, {}, kEast, dummy()));
    std::printf("  heavy cavalry: standing blow %d, charge uphill %d, flat %d, downhill %d\n", standing, up, flat, down);
    CHECK(flat > standing * 3 / 2);
    CHECK(down > flat);
    CHECK(up < flat);
    CHECK(down > up * 2);
}

TEST(a_charge_into_the_rear_hurts_more_than_into_the_front) {
    const auto chargeBlow = [](bool rear) {
        World w(WorldConfig{});
        w.spawnUnit(0, {m(28), m(0), m(0)}, kWest, unitStats(UnitType::LightCavalry));
        const EntityId t = w.spawnUnit(1, {}, rear ? kWest : kEast, dummy());
        return firstBlow(w, t);
    };
    const int32_t front = chargeBlow(false), rear = chargeBlow(true);
    std::printf("  light cavalry charge: front %d, rear %d\n", front, rear);
    CHECK(rear > front * 9 / 5);
}

TEST(a_charge_onto_braced_pikes_goes_badly) {
    // A pikeman who stands his ground (still, never turning) meets a charge head on or from behind.
    const auto run = [](bool facingIt) {
        World w(WorldConfig{});
        const EntityId knight = w.spawnUnit(0, {m(28), m(0), m(0)}, kWest, unitStats(UnitType::HeavyCavalry));
        UnitStats pike = dummy(UnitType::Pikemen);
        const EntityId p = w.spawnUnit(1, {}, facingIt ? kEast : kWest, pike);
        bool thrown = false;
        for (int i = 0; i < 8 * kTicksPerSecond; ++i) {
            w.step({});
            thrown = thrown || w.findUnit(knight)->airborne;
            bool struck = false;
            for (const ImpactEvent& e : w.impacts()) struck = struck || e.kind == ImpactEvent::Kind::Hit;
            if (struck) break;
        }
        const int32_t knightLoss = unitStats(UnitType::HeavyCavalry).maxHp - w.findUnit(knight)->hp;
        const int32_t pikeLoss = pike.maxHp - w.findUnit(p)->hp;
        return std::tuple{knightLoss, pikeLoss, thrown};
    };
    const auto [kFront, pFront, thrownFront] = run(true);
    const auto [kRear, pRear, thrownRear] = run(false);
    std::printf("  charge on pikes' front: knight loses %d, pikeman %d; on their rear: knight %d, pikeman %d\n", kFront, pFront,
                kRear, pRear);
    CHECK(kFront >= 40);
    CHECK(pFront == 0);
    CHECK(thrownFront);
    CHECK(kRear == 0);
    CHECK(pRear > 50);
    (void)thrownRear;
}

TEST(momentum_is_lost_when_stopped) {
    World w(WorldConfig{});
    const EntityId k = w.spawnUnit(0, {}, kEast, unitStats(UnitType::LightCavalry));
    const Command go{0, k, Command::Type::Move, {m(80), m(0), m(0)}};
    w.step(std::span(&go, 1));
    for (int i = 0; i < 3 * kTicksPerSecond; ++i) w.step({});
    CHECK(w.findUnit(k)->momentum > m(5));
    const Command halt{0, k, Command::Type::Stop, {}};
    w.step(std::span(&halt, 1));
    CHECK(w.findUnit(k)->momentum == Fixed::zero());
}

TEST(veterans_strike_faster_and_take_less_damage) {
    const auto blows = [](int32_t rank) {
        World w(WorldConfig{});
        const EntityId b = w.spawnUnit(0, {m(2), m(0), m(0)}, kWest, unitStats(UnitType::Berserker));
        w.setVeterancy(b, rank, 0);
        const EntityId t = w.spawnUnit(1, {}, kEast, dummy());
        int n = 0;
        for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
            w.step({});
            for (const ImpactEvent& e : w.impacts()) n += e.hitUnit == t && e.kind == ImpactEvent::Kind::Hit;
        }
        return n;
    };
    const int recruit = blows(0), elite = blows(3);
    // The same blow on a recruit and on an elite soldier.
    const auto taken = [](int32_t rank) {
        World w(WorldConfig{});
        w.spawnUnit(0, {m(2), m(0), m(0)}, kWest, unitStats(UnitType::Berserker));
        UnitStats s = dummy();
        const EntityId t = w.spawnUnit(1, {}, kEast, s);
        w.setVeterancy(t, rank, 0);
        return firstBlow(w, t);
    };
    const int32_t onRecruit = taken(0), onElite = taken(3);
    std::printf("  blows in 20 s: recruit %d, elite %d; a blow on a recruit %d, on an elite %d\n", recruit, elite, onRecruit,
                onElite);
    CHECK(elite > recruit * 6 / 5);
    CHECK(onElite < onRecruit);
    CHECK_NEAR(static_cast<double>(onElite) / onRecruit, 0.82, 0.04);
}

TEST(veterans_get_up_sooner) {
    // A giant throws a recruit and an elite; the elite is back on his feet first.
    const auto downTicks = [](int32_t rank) {
        World w(WorldConfig{});
        w.spawnUnit(0, {m(3), m(0), m(0)}, kWest, unitStats(UnitType::Giants));
        UnitStats s = dummy(UnitType::LightInfantry);
        const EntityId t = w.spawnUnit(1, {}, kEast, s);
        w.setVeterancy(t, rank, 0);
        bool thrown = false;
        for (int i = 0; i < 10 * kTicksPerSecond; ++i) {
            w.step({});
            const Unit* u = w.findUnit(t);
            if (u->airborne) thrown = true;
            else if (thrown) return u->meleeCooldown;
        }
        return -1;
    };
    const int32_t recruit = downTicks(0), elite = downTicks(3);
    CHECK(recruit > 0 && elite > 0);
    CHECK(elite < recruit);
}

TEST(veteran_grades_for_the_health_bar) {
    World w(WorldConfig{});
    const EntityId s = w.spawnUnit(0, {}, kEast, unitStats(UnitType::Warrior));
    const EntityId h = w.spawnUnit(0, {m(5), m(0), m(0)}, kEast, unitStats(UnitType::Hero));
    CHECK(veteranGrade(*w.findUnit(s)) == 0);
    w.setVeterancy(s, 2, 0);
    CHECK(veteranGrade(*w.findUnit(s)) == 2);
    w.setVeterancy(h, 8, 0);  // level 9
    CHECK(veteranGrade(*w.findUnit(h)) == 3);
}

TEST(archers_on_high_ground_outrange_archers_below) {
    // Archers 16 m up a 1 in 4 slope and archers below them, 64 m apart:
    // beyond the flat reach of a bow (55 m), within reach from above.
    WorldConfig cfg;
    cfg.terrain = ramp(r(1, 4));
    World w(cfg);
    const EntityId high = w.spawnUnit(0, {m(144), m(128), m(0)}, kWest, unitStats(UnitType::Archers));
    const EntityId low = w.spawnUnit(1, {m(80), m(128), m(0)}, kEast, dummy(UnitType::Archers));
    UnitStats lowBow = unitStats(UnitType::Archers);
    lowBow.aggroRange = Fixed::zero();
    const EntityId lowShooter = w.spawnUnit(1, {m(80), m(124), m(0)}, kEast, lowBow);
    int fromHigh = 0, fromLow = 0;
    for (int i = 0; i < 10 * kTicksPerSecond; ++i) {
        w.step({});
        for (const Projectile& p : w.projectiles()) {
            if (p.ageTicks != 1) continue;
            fromHigh += p.shooter == high;
            fromLow += p.shooter == lowShooter;
        }
    }
    std::printf("  shots from the hill %d, from below %d (height %.0f m)\n", fromHigh, fromLow,
                (w.findUnit(high)->pos.z - w.findUnit(low)->pos.z).toDouble());
    CHECK(fromHigh > 0);
    CHECK(fromLow == 0);
}

TEST(plunging_arrows_hurt_more) {
    CHECK(World::plungePct({m(40), m(0), -m(4)}) == 100);  // a long flat shot
    CHECK(World::plungePct({m(40), m(0), -m(20)}) > 115);   // shot down from a hill
    CHECK(World::plungePct({m(1), m(0), -m(40)}) == 125);   // straight down
}

TEST(a_falling_hero_is_reported) {
    World w(WorldConfig{});
    const EntityId hero = w.spawnUnit(0, {}, kEast, unitStats(UnitType::Hero));
    for (int i = 0; i < 6; ++i) w.spawnUnit(1, {m(i < 3 ? 3 : -3), m(i % 3 * 2 - 2), m(0)}, i < 3 ? kWest : kEast, unitStats(UnitType::Berserker));
    bool reported = false;
    for (int i = 0; i < 60 * kTicksPerSecond && !reported; ++i) {
        w.step({});
        for (const HeroDeath& d : w.heroDeaths()) {
            reported = d.hero == hero && d.owner == 0 && d.killer != kNoEntity;
            CHECK(!w.findUnit(hero)->alive());
        }
    }
    CHECK(reported);
}

TEST(combat_is_deterministic) {
    // Riders charge a line of pikes and infantry on rolling hills, archers shoot.
    const auto run = [] {
        WorldConfig cfg;
        cfg.seed = 99;
        cfg.terrain = std::make_shared<const Terrain>(Terrain::generate(3, 64, m(4), m(20)));
        World w(cfg);
        for (int i = 0; i < 6; ++i) {
            w.spawnUnit(0, {m(60), m(100 + i * 4), m(0)}, kEast, unitStats(UnitType::HeavyCavalry));
            w.spawnUnit(0, {m(50), m(100 + i * 4), m(0)}, kEast, unitStats(UnitType::Archers));
            w.spawnUnit(1, {m(110), m(100 + i * 4), m(0)}, kWest, unitStats(i % 2 ? UnitType::Pikemen : UnitType::Warrior));
            w.spawnUnit(1, {m(125), m(100 + i * 4), m(0)}, kWest, unitStats(UnitType::LightCavalry));
        }
        for (int i = 0; i < 40 * kTicksPerSecond; ++i) w.step({});
        return w.checksum();
    };
    const uint64_t a = run();
    CHECK(a == run());
    // Some broke and ran in that fight, and the same ones every time.
    std::printf("  combat checksum 0x%016llx\n", static_cast<unsigned long long>(a));
}

namespace {
// A frail, shieldless hero standing 12 m from an archer who shoots only that
// far, his men 20 m behind him: an arrow soon kills him in front of them.
struct HeroFalls {
    World w{WorldConfig{}};
    std::vector<EntityId> men;
    explicit HeroFalls(UnitType type) {
        UnitStats hero = unitStats(UnitType::Hero);
        hero.maxHp = 1;
        hero.aggroRange = Fixed::zero();
        hero.shield.blockChance = Fixed::zero();
        w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, hero);
        for (int i = 0; i < 8; ++i) men.push_back(w.spawnUnit(0, {m(-20 - i % 2 * 2), m(i - 4), m(0)}, kEast, unitStats(type)));
        UnitStats bow = unitStats(UnitType::Archers);
        bow.aggroRange = Fixed::zero();
        bow.ranged.autoRange = m(12);
        w.spawnUnit(1, {m(12), m(0), m(0)}, kWest, bow);
    }
    int routed() const {
        int n = 0;
        for (EntityId id : men) n += w.findUnit(id)->routed;
        return n;
    }
    // Steps until the hero falls; false if he never does.
    bool untilHeroFalls() {
        for (int i = 0; i < 30 * kTicksPerSecond; ++i) {
            w.step({});
            if (!w.heroDeaths().empty()) return true;
        }
        return false;
    }
};
}  // namespace

TEST(a_heros_death_breaks_nearby_green_troops) {
    HeroFalls s(UnitType::LightInfantry);
    CHECK(s.routed() == 0);
    CHECK(s.untilHeroFalls());
    CHECK(s.routed() == 8);
    // Broken, they heed no orders.
    const Command go{0, s.men[0], Command::Type::Move, {m(-80), m(0), m(0)}};
    s.w.step(std::span(&go, 1));
    CHECK(!s.w.findUnit(s.men[0])->hasMoveTarget);
}

TEST(undead_never_break) {
    HeroFalls s(UnitType::Undead);
    CHECK(s.untilHeroFalls());
    CHECK(s.routed() == 0);
    CHECK(s.w.findUnit(s.men[0])->morale == kMoraleFull);
}

TEST(elite_heavy_troops_waver_but_hold) {
    HeroFalls s(UnitType::HeavyInfantry);
    for (EntityId id : s.men) s.w.setVeterancy(id, 3, 0);
    CHECK(s.untilHeroFalls());
    CHECK(s.routed() == 0);
    CHECK(s.w.findUnit(s.men[0])->morale < kMoraleFull / 2);  // badly shaken
}

TEST(broken_troops_rally_when_safe_and_take_orders_again) {
    HeroFalls s(UnitType::LightInfantry);
    CHECK(s.untilHeroFalls());
    CHECK(s.routed() == 8);
    int ticks = 0;
    while (s.routed() > 0 && ticks < 40 * kTicksPerSecond) {
        s.w.step({});
        ++ticks;
    }
    std::printf("  rallied after %.1f s\n", static_cast<double>(ticks) / kTicksPerSecond);
    CHECK(s.routed() == 0);
    const Command go{0, s.men[0], Command::Type::Move, {m(-80), m(0), m(0)}};
    s.w.step(std::span(&go, 1));
    CHECK(s.w.findUnit(s.men[0])->hasMoveTarget);
}

TEST(broken_troops_flee_from_the_enemy) {
    // A green soldier sees his hero cut down by berserkers, and runs.
    World w(WorldConfig{});
    const EntityId man = w.spawnUnit(0, {}, kEast, unitStats(UnitType::LightInfantry));
    UnitStats frail = unitStats(UnitType::Hero);
    frail.maxHp = 1;
    const EntityId hero = w.spawnUnit(0, {m(2), m(0), m(0)}, kEast, frail);
    for (int i = 0; i < 3; ++i) w.spawnUnit(1, {m(18), m(i * 2 - 2), m(0)}, kWest, unitStats(UnitType::Berserker));
    bool broke = false;
    for (int i = 0; i < 10 * kTicksPerSecond && !broke; ++i) {
        w.step({});
        broke = w.findUnit(man)->routed;
    }
    CHECK(!w.findUnit(hero)->alive());
    CHECK(broke);
    const Fixed x0 = w.findUnit(man)->pos.x;
    for (int i = 0; i < kTicksPerSecond; ++i) w.step({});
    CHECK(!w.findUnit(man)->alive() || w.findUnit(man)->pos.x < x0 - Fixed::one());  // running west, away from them
}
