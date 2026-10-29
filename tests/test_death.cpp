// The death matrix (engine/sim/src/world_death.cpp): how the killing blow
// decides the body left behind.
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

#include "illyria/sim/terrain.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest{-Fixed::one(), Fixed::zero(), Fixed::zero()};

struct Outcome {
    std::vector<DeathEvent> deaths;
    uint64_t checksum = 0;
    int count(DeathKind k) const {
        int n = 0;
        for (const DeathEvent& d : deaths) n += d.kind == k;
        return n;
    }
};

// Pairs (or groups) far enough apart to fight separately, run to the end.
Outcome run(World& w, int seconds, const std::vector<Command>& first = {}) {
    Outcome o;
    w.step(first);
    for (int i = 0; i < seconds * kTicksPerSecond; ++i) {
        w.step({});
        for (const DeathEvent& d : w.deathEvents()) o.deaths.push_back(d);
    }
    o.checksum = w.checksum();
    return o;
}

// Each of `a` against each foe in turn: blows land in whole numbers, so the
// overkill depends on the foe's health.
Outcome duels(UnitType a, std::vector<UnitType> foes, int pairs, int seconds, uint64_t seed = 1) {
    WorldConfig cfg;
    cfg.seed = seed;
    World w(cfg);
    for (int i = 0; i < pairs; ++i) {
        w.spawnUnit(0, {m(0), m(i * 120), m(0)}, kEast, unitStats(a));
        w.spawnUnit(1, {m(3), m(i * 120), m(0)}, kWest, unitStats(foes[static_cast<size_t>(i) % foes.size()]));
    }
    return run(w, seconds);
}

// What the matrix should have made of each death.
DeathKind expected(const DeathEvent& d, bool arrow) {
    const UnitStats s = unitStats(static_cast<UnitType>(d.type));
    if (d.type == static_cast<uint8_t>(UnitType::Catapult)) return d.cause == DamageType::Fire ? DeathKind::Charred : DeathKind::Fell;
    if (d.cause == DamageType::Fire) return DeathKind::Charred;
    if (d.cause == DamageType::Pierce && arrow) return DeathKind::Pierced;
    if (d.overkill >= kObliterateOverkill) return DeathKind::Gibbed;
    switch (d.cause) {
        case DamageType::Explosive: return d.overkill >= kGibOverkillBlast ? DeathKind::Gibbed : DeathKind::Fell;
        case DamageType::Slash: return s.mass < m(4) && d.overkill >= kSeverOverkillSlash ? DeathKind::Severed : DeathKind::Fell;
        case DamageType::Cleave: return s.mass < m(4) && d.overkill >= kSeverOverkillCleave ? DeathKind::Severed : DeathKind::Fell;
        default: return DeathKind::Fell;
    }
}

int mismatches(const Outcome& o, bool arrows) {
    int bad = 0;
    for (const DeathEvent& d : o.deaths)
        if (d.kind != expected(d, arrows)) {
            std::printf("  unexpected: %s by %s, overkill %d\n", deathKindName(d.kind), damageTypeName(d.cause), d.overkill);
            ++bad;
        }
    return bad;
}
}  // namespace

TEST(weapons_have_the_damage_types_of_their_kind) {
    CHECK(unitStats(UnitType::Warrior).melee.damageType == DamageType::Slash);
    CHECK(unitStats(UnitType::Berserker).melee.damageType == DamageType::Cleave);
    CHECK(unitStats(UnitType::Dwarves).melee.damageType == DamageType::Cleave);
    CHECK(unitStats(UnitType::Giants).melee.damageType == DamageType::Blunt);
    CHECK(unitStats(UnitType::Pikemen).melee.damageType == DamageType::Pierce);
    CHECK(unitStats(UnitType::Archers).ranged.damageType == DamageType::Pierce);
    CHECK(unitStats(UnitType::DwarfGrenadier).ranged.damageType == DamageType::Explosive);
    CHECK(unitStats(UnitType::Wizards).ranged.damageType == DamageType::Fire);
    CHECK(unitStats(UnitType::Catapult).ranged.damageType == DamageType::Blunt);
}

TEST(cuts_past_the_threshold_sever_a_limb_or_the_head) {
    // Heroes' swords and berserkers' great blades against light infantry.
    const std::vector<UnitType> foes = {UnitType::Archers, UnitType::Journeyman, UnitType::Pikemen, UnitType::LightInfantry,
                                        UnitType::Undead, UnitType::Warrior};
    const Outcome slash = duels(UnitType::Hero, foes, 18, 40);
    const Outcome cleave = duels(UnitType::Berserker, foes, 18, 40);
    std::printf("  slash: %zu deaths, %d severed; cleave: %zu deaths, %d severed\n", slash.deaths.size(),
                slash.count(DeathKind::Severed), cleave.deaths.size(), cleave.count(DeathKind::Severed));
    CHECK(slash.count(DeathKind::Severed) > 0);
    CHECK(slash.count(DeathKind::Fell) > 0);  // a blow just enough leaves him whole
    CHECK(cleave.count(DeathKind::Severed) > 0);
    CHECK(mismatches(slash, false) == 0);
    CHECK(mismatches(cleave, false) == 0);
    int heads = 0, limbs = 0;
    for (const Outcome* o : {&slash, &cleave})
        for (const DeathEvent& d : o->deaths) {
            if (d.kind != DeathKind::Severed) continue;
            const int32_t threshold = d.cause == DamageType::Cleave ? kSeverOverkillCleave : kSeverOverkillSlash;
            if (d.severed == RemainKind::Head) {
                ++heads;
                CHECK(d.overkill >= threshold * 2);
            } else {
                ++limbs;
                CHECK(d.severed == RemainKind::Arm || d.severed == RemainKind::Leg);
                CHECK(d.overkill < threshold * 2);
            }
        }
    CHECK(heads > 0 && limbs > 0);
}

TEST(a_severed_part_flies_on_along_the_blow_and_bleeds) {
    WorldConfig cfg;
    World w(cfg);
    const EntityId berserker = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hero));
    // An archer dies to a hero's second blow, well past the threshold.
    w.spawnUnit(1, {m(2), m(0), m(0)}, kWest, unitStats(UnitType::Archers));
    bool severed = false, flying = false, pool = false;
    for (int i = 0; i < 20 * kTicksPerSecond && !severed; ++i) {
        w.step({});
        for (const DeathEvent& d : w.deathEvents()) severed = severed || d.kind == DeathKind::Severed;
        for (const ImpactEvent& e : w.impacts()) pool = pool || (e.kind == ImpactEvent::Kind::Bleed && e.hitUnit != kNoEntity);
        if (!severed) continue;
        for (const Projectile& p : w.projectiles())
            if (p.kind == ProjectileKind::Debris && p.bleeding && p.vel.x > Fixed::zero()) flying = true;
    }
    (void)berserker;
    CHECK(severed);
    CHECK(flying);  // thrown away from the berserker, east
    CHECK(pool);
    const Unit* dead = w.findUnit(2);
    CHECK(dead && !dead->alive() && !dead->gibbed && dead->death.severed != RemainKind::None);
}

TEST(arrows_leave_whole_bodies_with_the_arrow_pinned) {
    WorldConfig cfg;
    World w(cfg);
    for (int i = 0; i < 8; ++i) w.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, unitStats(UnitType::Archers));
    // Beyond their own reach to charge, they stand and die.
    for (int i = 0; i < 6; ++i) w.spawnUnit(1, {m(50), m(i * 2), m(0)}, kWest, unitStats(UnitType::LightInfantry));
    const Outcome o = run(w, 60);
    std::printf("  %zu deaths, %d pierced\n", o.deaths.size(), o.count(DeathKind::Pierced));
    CHECK(o.count(DeathKind::Pierced) > 0);
    CHECK(mismatches(o, true) == 0);
    for (const Unit& u : w.units()) {
        if (u.alive() || u.death.kind != DeathKind::Pierced) continue;
        CHECK(u.death.arrowPinned);
        CHECK(!u.gibbed);
        // Shot from the west, the arrow points east, and he falls the way it flew.
        CHECK(u.death.arrowDir.x > Fixed::zero());
        CHECK(u.death.dir.x > Fixed::fromRatio(1, 2));
        CHECK(u.death.arrowHeight > Fixed::zero() && u.death.arrowHeight < u.stats.height);
    }
}

TEST(arrows_stick_in_shields_that_turn_them) {
    WorldConfig cfg;
    World w(cfg);
    for (int i = 0; i < 6; ++i) w.spawnUnit(0, {m(0), m(i * 2), m(0)}, kEast, unitStats(UnitType::Archers));
    const EntityId wall = w.spawnUnit(1, {m(45), m(5), m(0)}, kWest, unitStats(UnitType::Warrior));
    const Command hold{1, wall, Command::Type::Stop, {}};
    w.step(std::span(&hold, 1));
    int blocked = 0;
    for (int i = 0; i < 30 * kTicksPerSecond; ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) blocked += e.kind == ImpactEvent::Kind::Blocked;
    }
    const Unit* u = w.findUnit(wall);
    std::printf("  %d blocked, %d stuck in the shield\n", blocked, u->shieldArrows);
    CHECK(blocked > 0);
    CHECK(u->shieldArrows > 0 && u->shieldArrows <= kMaxShieldArrows);
}

TEST(fire_chars_the_dead) {
    WorldConfig cfg;
    World w(cfg);
    for (int i = 0; i < 3; ++i) w.spawnUnit(0, {m(0), m(i * 3), m(0)}, kEast, unitStats(UnitType::Wizards));
    for (int i = 0; i < 6; ++i) w.spawnUnit(1, {m(35), m(i * 2), m(0)}, kWest, unitStats(UnitType::Journeyman));
    const Outcome o = run(w, 90);
    CHECK(o.count(DeathKind::Charred) > 0);
    CHECK(o.count(DeathKind::Gibbed) == 0);
    CHECK(mismatches(o, false) == 0);
}

TEST(blasts_gib_past_the_threshold_and_throw_the_rest_whole) {
    int gibbed = 0, whole = 0;
    for (uint64_t seed = 1; seed <= 4; ++seed) {
        WorldConfig cfg;
        cfg.seed = seed;
        World w(cfg);
        const EntityId dwarf = w.spawnUnit(0, {m(-14), m(0), m(0)}, kEast, unitStats(UnitType::DwarfGrenadier));
        for (int i = 0; i < 6; ++i) w.spawnUnit(1, {m(i % 3), m(i / 3 * 2 - 1), m(0)}, kWest, unitStats(UnitType::Archers));
        const Outcome o = run(w, 30, {{0, dwarf, Command::Type::AttackGround, {m(1), m(0), m(0)}}});
        CHECK(mismatches(o, true) == 0);  // (the archers shoot back)
        gibbed += o.count(DeathKind::Gibbed);
        whole += o.count(DeathKind::Fell);
        for (const Unit& u : w.units())
            if (!u.alive() && u.death.kind == DeathKind::Gibbed) CHECK(u.gibbed);
    }
    std::printf("  %d gibbed, %d thrown whole\n", gibbed, whole);
    CHECK(gibbed > 0);
}

TEST(massive_overkill_obliterates_whatever_the_weapon) {
    // A catapult's stone lands on a knot of healers: crushed to pieces.
    WorldConfig cfg;
    World w(cfg);
    w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Catapult));
    for (int i = 0; i < 9; ++i) w.spawnUnit(1, {m(60 + i % 3), m(i / 3), m(0)}, kWest, unitStats(UnitType::Archers));
    const Outcome o = run(w, 90);
    int crushed = 0;
    for (const DeathEvent& d : o.deaths) crushed += d.cause == DamageType::Blunt && d.kind == DeathKind::Gibbed;
    std::printf("  %zu dead, %d obliterated\n", o.deaths.size(), crushed);
    CHECK(crushed > 0);
    CHECK(mismatches(o, true) == 0);
}

TEST(body_parts_roll_down_slopes) {
    // A steep ramp, falling to the west: heights rise 0.6 m per metre east.
    const int32_t cells = 32;
    std::vector<Fixed> samples;
    for (int y = 0; y <= cells; ++y)
        for (int x = 0; x <= cells; ++x) samples.push_back(Fixed::fromRatio(x * 4 * 6, 10));
    WorldConfig cfg;
    cfg.terrain = std::make_shared<const Terrain>(Terrain::fromSamples(cells, m(4), samples));
    Fixed downhill, across;
    int parts = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        cfg.seed = seed;
        World w(cfg);
        // The grenadier throws across the slope (north), so the blast scatters parts evenly east and west.
        const EntityId dwarf = w.spawnUnit(0, {m(64), m(40), m(0)}, {Fixed::zero(), Fixed::one(), Fixed::zero()}, unitStats(UnitType::DwarfGrenadier));
        for (int i = 0; i < 4; ++i) w.spawnUnit(1, {m(63 + i % 2 * 2), m(52 + i / 2 * 2), m(0)}, kWest, unitStats(UnitType::Journeyman));
        run(w, 40, {{0, dwarf, Command::Type::AttackGround, {m(64), m(54), m(0)}}});
        for (const Remain& r : w.remains()) {
            if (r.kind != RemainKind::Head && r.kind != RemainKind::Arm && r.kind != RemainKind::Leg && r.kind != RemainKind::Chunk &&
                r.kind != RemainKind::Torso)
                continue;
            ++parts;
            downhill += m(64) - r.pos.x;
            across += abs(r.pos.y - m(53));
        }
    }
    std::printf("  %d parts, mean %.1f m downhill, %.1f m across\n", parts, parts ? downhill.toDouble() / parts : 0.0,
                parts ? across.toDouble() / parts : 0.0);
    CHECK(parts > 0);
    CHECK(downhill > m(3) * parts);
}

TEST(the_death_matrix_is_deterministic) {
    const auto battle = [](uint64_t seed) {
        WorldConfig cfg;
        cfg.seed = seed;
        World w(cfg);
        const UnitType ours[] = {UnitType::Berserker, UnitType::Archers, UnitType::Wizards, UnitType::DwarfGrenadier, UnitType::Hero};
        for (int i = 0; i < 10; ++i) w.spawnUnit(0, {m(0), m(i * 3), m(0)}, kEast, unitStats(ours[i % 5]));
        for (int i = 0; i < 14; ++i) w.spawnUnit(1, {m(30), m(i * 2), m(0)}, kWest, unitStats(UnitType::LightInfantry));
        return run(w, 60);
    };
    const Outcome a = battle(5), b = battle(5);
    CHECK(a.checksum == b.checksum);
    CHECK(a.deaths.size() == b.deaths.size());
    std::map<DeathKind, int> kinds;
    for (size_t i = 0; i < a.deaths.size() && i < b.deaths.size(); ++i) {
        CHECK(a.deaths[i].unit == b.deaths[i].unit && a.deaths[i].kind == b.deaths[i].kind && a.deaths[i].overkill == b.deaths[i].overkill);
        ++kinds[a.deaths[i].kind];
    }
    std::printf("  %zu deaths:", a.deaths.size());
    for (const auto& [k, n] : kinds) std::printf(" %s %d", deathKindName(k), n);
    std::printf("\n");
    CHECK(kinds.size() >= 3);
    CHECK(battle(6).checksum != a.checksum);
}
