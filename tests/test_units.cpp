#include <string>
#include <utility>
#include <vector>
#include "illyria/sim/scenario.h"
#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;

int count(const World& w, ImpactEvent::Kind kind) {
    int n = 0;
    for (const ImpactEvent& e : w.impacts()) n += e.kind == kind;
    return n;
}
}  // namespace

TEST(every_unit_type_has_sane_stats) {
    for (int t = 0; t < kUnitTypeCount; ++t) {
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        CHECK(s.type == t);
        CHECK(s.maxHp > 0);
        CHECK(s.radius > Fixed::zero() && s.height > Fixed::zero() && s.moveSpeed > Fixed::zero());
        CHECK(s.melee.damage > 0 || s.healing.amount > 0 || s.ranged.muzzleSpeed > Fixed::zero());
        CHECK(std::string(unitTypeName(static_cast<UnitType>(t))) != "?");
    }
}

TEST(shields_block_from_the_front_but_not_the_flank) {
    // A warrior facing east is shot at from the east, another (facing east) from the west.
    const auto blockedFrom = [](bool front) {
        World w(WorldConfig{});
        w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Warrior));
        for (int i = 0; i < 8; ++i) {
            w.spawnUnit(1, {m(front ? 30 : -30), m(i - 4), m(0)}, front ? kWest : kEast, unitStats(UnitType::Archers));
        }
        int blocked = 0, hits = 0;
        for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
            w.step({});
            blocked += count(w, ImpactEvent::Kind::Blocked);
            hits += count(w, ImpactEvent::Kind::Hit);
        }
        return std::pair{blocked, hits};
    };
    const auto [frontBlocked, frontHits] = blockedFrom(true);
    const auto [rearBlocked, rearHits] = blockedFrom(false);
    CHECK(frontBlocked > 0);
    CHECK(rearBlocked == 0);
    CHECK(rearHits > 0);
}

TEST(healers_mend_wounded_friends_with_limited_charges) {
    World w(WorldConfig{});
    const EntityId patient = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Warrior));
    // Behind the patient, so the archers shoot the warrior rather than the healer.
    const EntityId healer = w.spawnUnit(0, {m(10), m(0), m(0)}, kWest, unitStats(UnitType::Journeyman));
    // Archers wound the warrior from out of his aggro range.
    for (int i = 0; i < 6; ++i) w.spawnUnit(1, {m(-40), m(i * 2 - 5), m(0)}, kEast, unitStats(UnitType::Archers));
    int heals = 0;
    for (int i = 0; i < 40 * kTicksPerSecond; ++i) {
        w.step({});
        heals += count(w, ImpactEvent::Kind::Heal);
    }
    CHECK(heals > 0);
    CHECK(heals <= unitStats(UnitType::Journeyman).healing.charges);
    CHECK(w.findUnit(healer)->healCharges == unitStats(UnitType::Journeyman).healing.charges - heals);
    (void)patient;
}

TEST(fliers_cruise_high_and_swoop_to_fight) {
    World w(WorldConfig{});
    const EntityId griffin = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Griffins));
    CHECK(w.findUnit(griffin)->pos.z == m(12));
    const EntityId footman = w.spawnUnit(1, {m(25), m(0), m(0)}, kWest, unitStats(UnitType::LightInfantry));
    for (int i = 0; i < 30 * kTicksPerSecond && w.findUnit(footman)->alive(); ++i) w.step({});
    CHECK(!w.findUnit(footman)->alive());
    CHECK(w.findUnit(griffin)->pos.z < m(4));
}

TEST(giants_knock_soldiers_flying) {
    World w(WorldConfig{});
    w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Giants));
    const EntityId f = w.spawnUnit(1, {m(6), m(0), m(0)}, kWest, unitStats(UnitType::LightInfantry));
    bool thrown = false;
    for (int i = 0; i < 10 * kTicksPerSecond; ++i) {
        w.step({});
        const Unit* u = w.findUnit(f);
        thrown = thrown || (u->airborne && u->pos.z > Fixed::one());
    }
    CHECK(thrown);
}

TEST(satchel_charges_bounce_explode_gib_and_throw_remains) {
    WorldConfig cfg;
    World w(cfg);
    // Archers first litter the ground with arrows around the target spot.
    for (int i = 0; i < 6; ++i) w.spawnUnit(0, {m(-40), m(i - 3), m(0)}, kEast, unitStats(UnitType::Archers));
    const EntityId dwarf = w.spawnUnit(0, {m(-15), m(0), m(0)}, kEast, unitStats(UnitType::DwarfGrenadier));
    std::vector<Command> volley;
    for (EntityId id = 1; id <= 6; ++id) volley.push_back({0, id, Command::Type::AttackGround, {m(5), m(0), m(0)}});
    w.step(volley);
    for (int i = 0; i < 8 * kTicksPerSecond; ++i) w.step({});
    const std::vector<Command> hold = [] {
        std::vector<Command> c;
        for (EntityId id = 1; id <= 6; ++id) c.push_back({0, id, Command::Type::Stop, {}});
        return c;
    }();
    w.step(hold);
    int stuck = 0;
    for (const Remain& r : w.remains()) stuck += r.kind == RemainKind::Arrow;
    CHECK(stuck > 5);

    // Enemies walk into the arrow field; the dwarf lobs a satchel among them.
    for (int i = 0; i < 4; ++i) w.spawnUnit(1, {m(5), m(i * 2 - 3), m(0)}, kWest, unitStats(UnitType::LightInfantry));
    const Command bomb{0, dwarf, Command::Type::AttackGround, {m(5), m(0), m(0)}};
    w.step(std::span(&bomb, 1));
    int explosions = 0, relaunched = 0, kills = 0;
    bool bounced = false;
    for (int i = 0; i < 6 * kTicksPerSecond; ++i) {
        w.step({});
        explosions += count(w, ImpactEvent::Kind::Explosion);
        for (const ImpactEvent& e : w.impacts()) kills += e.killed;
        for (const Projectile& p : w.projectiles()) {
            relaunched += p.kind == ProjectileKind::Debris && p.remain == RemainKind::Arrow && p.ageTicks == 1;
            const bool nearGround = p.pos.z - w.groundHeight(p.pos.x, p.pos.y) < Fixed::fromRatio(1, 2);
            bounced = bounced || (p.kind == ProjectileKind::Bomb && nearGround && p.vel.z > Fixed::zero());
        }
        if (explosions > 0 && i > 3 * kTicksPerSecond) break;
    }
    CHECK(explosions > 0);
    CHECK(relaunched > 0);
    CHECK(bounced);
    int parts = 0;
    for (int i = 0; i < 10 * kTicksPerSecond; ++i) w.step({});
    for (const Remain& r : w.remains()) parts += r.kind == RemainKind::Head || r.kind == RemainKind::Arm || r.kind == RemainKind::Leg;
    CHECK(kills == 0 || parts > 0);
    std::printf("  stuck %d, relaunched %d, kills %d, body parts %d\n", stuck, relaunched, kills, parts);
}

TEST(melee_scenario_is_deterministic) {
    const auto run = [](uint64_t seed) {
        const Replay r = scenarios::Melee::replay(seed);
        World w(r.config);
        scenarios::Melee::setup(w);
        runReplay(w, r, scenarios::Melee::kTicks);
        return w.checksum();
    };
    const uint64_t a = run(1234);
    CHECK(a == run(1234));
    constexpr uint64_t kGolden = 0x1ee54ac1dfd96778;
    if (a != kGolden) std::printf("  Melee checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}
