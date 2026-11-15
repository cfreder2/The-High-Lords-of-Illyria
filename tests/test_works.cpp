// Timber works in battle (engine/sim/src/world_works.cpp, world_sortie.cpp; docs/GAME_DESIGN.md "Outworks and timber"):
// watchtowers and the men on them, palisades and abatis that bar horse and wheels, timber that breaks into flying beams and
// planks that hurt the men they strike, fire arrows and Molotovs that burn it, a tower's engine that hunts engines, sorties
// against engines, a ditch that horse cannot cross, a barricade behind a battered gate.
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/sim/ballistics.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/nav.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
Fixed r(int64_t n, int64_t d) { return Fixed::fromRatio(n, d); }
constexpr int32_t kCells = 96;  // 384 m
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kNorth{Fixed::zero(), Fixed::one(), Fixed::zero()};

World field(uint64_t seed = 1) {
    std::vector<Ground> cells(static_cast<size_t>(kCells) * kCells, Ground::Plains);
    WorldConfig c;
    c.seed = seed;
    c.region = std::make_shared<const Region>(Region::fromGround(kCells, cells));
    return World(c);
}
Vec3 at(int64_t x, int64_t y) { return {m(x), m(y), Fixed::zero()}; }

// A man who stands where he is put and does nothing.
UnitStats stander(UnitType type) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged = {};
    s.melee.damage = 0;
    s.shield.blockChance = Fixed::zero();
    return s;
}

Work tower(const Vec3& p) {
    Work w;
    w.kind = Work::Kind::Watchtower;
    w.a = w.b = p;
    w.halfThick = r(3, 2);
    w.height = m(6);
    w.out = -kEast;
    w.maxHp = 700;
    return w;
}
// A run of palisade (or abatis) along y at x, from y0 to y1, facing west, in 6 m runs.
void fence(World& w, Work::Kind kind, int64_t x, int64_t y0, int64_t y1, PlayerId holder = 0) {
    for (int64_t y = y0; y < y1; y += 6) {
        Work k;
        k.kind = kind;
        k.a = at(x, y);
        k.b = at(x, y + 6);
        k.halfThick = kind == Work::Kind::Abatis ? r(7, 5) : r(3, 10);
        k.height = kind == Work::Kind::Abatis ? r(8, 5) : r(13, 10);
        k.out = -kEast;
        k.maxHp = kind == Work::Kind::Abatis ? 400 : 500;
        w.placeWork(k, holder);
    }
}
// A catapult's stone thrown from `from` to come down on `to` (its smash and its roll are the World's).
void stoneAt(World& w, const Vec3& from, const Vec3& to) {
    const Vec3 origin = from + Vec3{Fixed::zero(), Fixed::zero(), m(2)};
    const Vec3 vel = solveFixedElevation(origin, to, kStoneElevation, m(40), Fixed::fromRatio(981, 100)).velocity;
    w.launchStone(1, origin, vel);
}
void run(World& w, int seconds, std::vector<Command> first = {}) {
    w.step(first);
    for (int t = 1; t < seconds * kTicksPerSecond; ++t) w.step({});
}
int timberFlying(const World& w) {
    int n = 0;
    for (const Projectile& p : w.projectiles()) n += p.kind == ProjectileKind::Debris && isTimber(p.remain);
    return n;
}

}  // namespace

TEST(bowmen_climb_a_watchtower_and_shoot_from_its_platform_out_of_reach_of_swords) {
    World w = field();
    const size_t t = w.placeWork(tower(at(200, 192)));
    std::vector<Command> orders;
    std::vector<EntityId> bows;
    for (int i = 0; i < 3; ++i) {
        bows.push_back(w.spawnUnit(0, at(180, 186 + i * 4), kEast, unitStats(UnitType::Archers)));
        orders.push_back({0, bows.back(), Command::Type::Move, at(200, 192)});
    }
    run(w, 20, orders);
    int up = 0, below = 0;
    for (const EntityId id : bows) {
        const Unit& u = *w.findUnit(id);
        up += u.perch == static_cast<int32_t>(t) && u.pos.z >= m(6);
        below += u.perch == -1;
    }
    std::printf("  %d up the tower, %d at its foot (two places)\n", up, below);
    CHECK(up == World::kPerchPlaces);
    CHECK(below == 1);
    CHECK(w.perchedOn(t) == 2);
    // Swordsmen at its foot cannot reach them (nor they the men hugging its legs: the platform is in the way); the men up
    // there shoot those farther off.
    std::vector<EntityId> foes;
    for (int i = 0; i < 4; ++i) foes.push_back(w.spawnUnit(1, at(203, 188 + i * 3), -kEast, unitStats(UnitType::LightInfantry)));
    for (int i = 0; i < 4; ++i) foes.push_back(w.spawnUnit(1, at(222, 186 + i * 4), -kEast, stander(UnitType::LightInfantry)));
    int shots = 0;
    for (int k = 0; k < 30 * kTicksPerSecond; ++k) {
        w.step({});
        for (const Projectile& p : w.projectiles()) shots += p.ageTicks == 1 && p.kind == ProjectileKind::Arrow;
    }
    std::printf("  arrows loosed %d\n", shots);
    int hurt = 0;
    for (const EntityId id : foes) hurt += w.findUnit(id)->hp < w.findUnit(id)->stats.maxHp;
    for (const EntityId id : bows)
        if (w.findUnit(id)->perch == static_cast<int32_t>(t)) CHECK(w.findUnit(id)->hp == w.findUnit(id)->stats.maxHp);
    CHECK(hurt >= 2);
    // Ordered off it, he climbs down and goes.
    for (const EntityId id : foes) const_cast<Unit&>(*w.findUnit(id)).hp = 0;
    EntityId up1 = kNoEntity;
    for (const EntityId id : bows)
        if (w.findUnit(id)->perch == static_cast<int32_t>(t)) up1 = id;
    run(w, 10, {{0, up1, Command::Type::Move, at(150, 192)}});
    std::printf("  ordered off: perch %d at x %.1f\n", w.findUnit(up1)->perch, w.findUnit(up1)->pos.x.toDouble());
    CHECK(w.findUnit(up1)->perch == -1);
    CHECK(w.findUnit(up1)->pos.x < m(192));
}

TEST(a_boulder_smashes_a_watchtower_its_men_fall_and_its_flying_timber_kills) {
    World w = field(3);
    const size_t t = w.placeWork(tower(at(200, 192)));
    std::vector<Command> orders;
    std::vector<EntityId> bows;
    for (int i = 0; i < 2; ++i) {
        UnitStats bow = unitStats(UnitType::Archers);
        bow.ranged.autoRange = Fixed::zero();  // (they hold their arrows: the timber is what is measured)
        bows.push_back(w.spawnUnit(0, at(194, 190 + i * 4), kEast, bow));
        orders.push_back({0, bows.back(), Command::Type::Move, at(200, 192)});
    }
    run(w, 10, orders);
    CHECK(w.perchedOn(t) == 2);
    // A file of men beyond it, the way the stone goes.
    std::vector<EntityId> men;
    for (int i = 0; i < 12; ++i)
        men.push_back(w.spawnUnit(1, Vec3{m(203) + r(6, 5) * (i % 4), m(190) + r(6, 5) * (i / 4), Fixed::zero()}, -kEast, stander(UnitType::LightInfantry)));
    int stones = 0, maxTimber = 0;
    while (w.works()[t].standing() && stones < 4) {
        stoneAt(w, at(100, 192), at(199, 192) + Vec3{Fixed::zero(), Fixed::zero(), m(1)});  // (at its legs)
        ++stones;
        for (int k = 0; k < 6 * kTicksPerSecond; ++k) {
            w.step({});
            maxTimber = std::max(maxTimber, timberFlying(w));
        }
    }
    run(w, 6);
    int dead = 0, hurt = 0;
    for (const EntityId id : men) {
        const Unit& u = *w.findUnit(id);
        dead += !u.alive();
        hurt += u.alive() && u.hp < u.stats.maxHp;
    }
    int fallen = 0;
    for (const EntityId id : bows) fallen += w.findUnit(id)->perch == -1 && w.findUnit(id)->pos.z < m(1) && w.findUnit(id)->hp < w.findUnit(id)->stats.maxHp;
    int lying = 0;
    for (const Remain& rm : w.remains()) lying += isTimber(rm.kind);
    std::printf("  %d stones; tower %s; %d pieces of timber in the air at most, %d lying; bowmen fallen and hurt %d; men beyond: %d dead, %d hurt\n", stones,
                w.works()[t].standing() ? "standing" : "down", maxTimber, lying, fallen, dead, hurt);
    CHECK(!w.works()[t].standing());
    CHECK(maxTimber >= 6);
    CHECK(lying >= 4);
    CHECK(fallen == 2);
    CHECK(dead + hurt >= 2);
}

TEST(a_palisade_bars_horse_and_slows_foot_and_abatis_falls_to_stones_and_the_way_is_open) {
    World w = field();
    fence(w, Work::Kind::Palisade, 150, 60, 324);
    const EntityId horse = w.spawnUnit(0, at(130, 192), kEast, stander(UnitType::LightCavalry));
    const EntityId foot = w.spawnUnit(0, at(130, 196), kEast, stander(UnitType::LightInfantry));
    run(w, 25, {{0, horse, Command::Type::Move, at(170, 192)}, {0, foot, Command::Type::Move, at(170, 196)}});
    const Unit& h = *w.findUnit(horse);
    const Unit& f = *w.findUnit(foot);
    std::printf("  horse at x %.1f, foot at x %.1f after 25 s\n", h.pos.x.toDouble(), f.pos.x.toDouble());
    CHECK(h.pos.x < m(150) || abs(h.pos.y - m(192)) > m(100));  // (round the end of it, if at all)
    CHECK(f.pos.x > m(165));

    // Abatis before it: two stones on a run of it, and the horse goes through the gap.
    World a = field(2);
    fence(a, Work::Kind::Abatis, 150, 60, 324);
    size_t run0 = 0;
    for (size_t i = 0; i < a.works().size(); ++i)
        if (a.works()[i].def.a.y == m(186)) run0 = i;
    for (int s = 0; s < 3 && a.works()[run0].standing(); ++s) {
        stoneAt(a, at(60, 189), at(150, 189));
        run(a, 6);
    }
    std::printf("  the abatis at y 186-192: %s\n", a.works()[run0].standing() ? "standing" : "broken");
    CHECK(!a.works()[run0].standing());
    const EntityId rider = a.spawnUnit(0, at(130, 189), kEast, stander(UnitType::LightCavalry));
    run(a, 15, {{0, rider, Command::Type::Move, at(170, 189)}});
    CHECK(a.findUnit(rider)->pos.x > m(160));
}

TEST(fire_arrows_set_a_palisade_alight_and_it_burns_down) {
    World w = field();
    fence(w, Work::Kind::Palisade, 160, 180, 204, 1);
    const EntityId bow = w.spawnUnit(0, at(120, 192), kEast, unitStats(UnitType::Archers));
    Command c{0, bow, Command::Type::AttackObject, at(160, 192)};
    c.object = Command::Object::Work;
    c.objectIndex = 1;
    run(w, 20, {c});
    const Unit& u = *w.findUnit(bow);
    bool burning = false;
    for (const WorkState& ws : w.works()) burning = burning || ws.fire > 0 || !ws.standing();
    std::printf("  fire arrows left %d; a run alight or down: %d\n", u.fireArrows, burning);
    CHECK(u.fireArrows < unitStats(UnitType::Archers).fireArrows);
    CHECK(burning);
    run(w, 90);
    int down = 0;
    for (const WorkState& ws : w.works()) down += !ws.standing();
    std::printf("  %d of %zu runs burned down\n", down, w.works().size());
    CHECK(down >= 2);  // (it spread)
    // Plain arrows do nothing to it: a bowman with none is refused.
    World x = field();
    fence(x, Work::Kind::Palisade, 160, 180, 204, 1);
    UnitStats noFire = unitStats(UnitType::Archers);
    noFire.fireArrows = 0;
    const EntityId plain = x.spawnUnit(0, at(120, 192), kEast, noFire);
    Command p{0, plain, Command::Type::AttackObject, at(160, 192)};
    p.object = Command::Object::Work;
    p.objectIndex = 1;
    run(x, 10, {p});
    CHECK(x.findUnit(plain)->attackWork == -1);
}

TEST(a_molotov_burns_a_palisade) {
    World w = field();
    fence(w, Work::Kind::Palisade, 160, 186, 198, 1);
    const EntityId rider = w.spawnUnit(0, at(140, 192), kEast, unitStats(UnitType::LightCavalry));
    CHECK(w.findUnit(rider)->ammo == 1);
    Command c{0, rider, Command::Type::AttackGround, at(160, 192)};
    run(w, 6, {c});
    bool lit = false;
    for (const WorkState& ws : w.works()) lit = lit || ws.fire > 0;
    CHECK(lit);
    CHECK(w.findUnit(rider)->ammo == 0);
}

TEST(a_tower_engine_outranges_a_ground_engine_and_shoots_engines_before_men) {
    World w = field();
    Work e;
    e.kind = Work::Kind::Engine;
    e.a = e.b = at(40, 192) + Vec3{Fixed::zero(), Fixed::zero(), m(14)};
    e.halfThick = r(6, 5);
    e.height = m(14);
    e.out = kEast;
    e.maxHp = 260;
    const size_t eng = w.placeWork(e, 0);
    // A foe's catapult 165 m off (beyond its own 150 m throw at the tower), and a man nearer.
    UnitStats cat = unitStats(UnitType::Catapult);
    const EntityId catapult = w.spawnUnit(1, at(205, 192), -kEast, cat);
    const EntityId man = w.spawnUnit(1, at(120, 210), -kEast, stander(UnitType::LightInfantry));
    run(w, 45);
    const Unit& c = *w.findUnit(catapult);
    std::printf("  catapult %d of %d, the man %d of %d, bolts left %d of %d\n", c.hp, c.stats.maxHp, w.findUnit(man)->hp, w.findUnit(man)->stats.maxHp,
                w.works()[eng].ammo, World::kEngineBolts);
    CHECK(c.hp < c.stats.maxHp);
    CHECK(w.findUnit(man)->hp == w.findUnit(man)->stats.maxHp);
    CHECK(w.works()[eng].ammo < World::kEngineBolts);
    CHECK(w.works()[eng].standing());  // (it was out of the catapult's reach)
    // A catapult brought within its throw answers the tower first, and stones break it.
    World x = field(4);
    const size_t e2 = x.placeWork(e, 0);
    x.spawnUnit(1, at(175, 192), -kEast, cat);
    run(x, 120);
    std::printf("  a catapult at 135 m: the tower engine %s (%d hp)\n", x.works()[e2].standing() ? "standing" : "broken", x.works()[e2].hp);
    CHECK(x.works()[e2].hp < 260);
}

TEST(a_sortie_burns_an_unescorted_engine_and_rides_home_and_an_escort_turns_it_back) {
    for (const bool escorted : {false, true}) {
        World w = field(escorted ? 6 : 5);
        UnitStats cat = unitStats(UnitType::Catapult);
        cat.ranged.autoRange = Fixed::zero();
        const EntityId engine = w.spawnUnit(1, at(230, 192), -kEast, cat);
        if (escorted)
            for (int i = 0; i < 16; ++i) w.spawnUnit(1, at(218 + (i % 4) * 2, 182 + (i / 4) * 6), -kEast, unitStats(UnitType::Pikemen));
        std::vector<Command> orders;
        std::vector<EntityId> riders;
        for (int i = 0; i < 4; ++i) {
            riders.push_back(w.spawnUnit(0, at(100, 186 + i * 4), kEast, unitStats(UnitType::LightCavalry)));
            Command c{0, riders.back(), Command::Type::Sortie, at(230, 192)};
            c.targetUnit = engine;
            orders.push_back(c);
        }
        run(w, 70, orders);
        int home = 0, alive = 0, bottles = 0;
        for (const EntityId id : riders) {
            const Unit& u = *w.findUnit(id);
            alive += u.alive();
            home += u.alive() && (u.pos - at(100, 192)).flat().length() < m(20);
            bottles += u.ammo;
        }
        const Unit& e = *w.findUnit(engine);
        std::printf("  %s: engine %s (%d hp), riders alive %d, home %d, bottles left %d\n", escorted ? "escorted" : "alone", e.alive() ? "standing" : "destroyed", e.hp, alive, home, bottles);
        if (!escorted) {
            CHECK(!e.alive());
            CHECK(home >= 3);
        } else {
            CHECK(e.alive());
            CHECK(alive >= 2);
        }
    }
}

TEST(a_fall_from_a_height_hurts) {
    World w = field();
    const EntityId id = w.spawnUnit(0, at(100, 100), kEast, stander(UnitType::LightInfantry));
    Unit& up = const_cast<Unit&>(*w.findUnit(id));
    up.pos.z = m(8);
    up.airborne = true;
    run(w, 3);
    const Unit& u = *w.findUnit(id);
    std::printf("  from 8 m: %d of %d\n", u.hp, u.stats.maxHp);
    CHECK(u.hp < u.stats.maxHp && u.alive());
}
