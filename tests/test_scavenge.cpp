// The hyena-folk (engine/sim/src/world_scavenge.cpp): what they take, whom
// they throw it at, what a thrown head does, and the armoured foes they keep
// clear of.
#include <algorithm>
#include <cstdio>

#include "illyria/sim/scenario.h"
#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;

// A man who stands where he is put: no blows, no bow, no shield, no aggro.
UnitStats standing(UnitType type) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged.autoRange = Fixed::zero();
    s.shield.blockChance = Fixed::zero();
    return s;
}

// A body part lying on the ground at (x, y).
void lay(World& w, RemainKind kind, int64_t x, int64_t y, UnitType from = UnitType::LightInfantry) {
    w.launchPart(kind, 1, {m(x), m(y), Fixed::fromRatio(1, 10)}, {}, static_cast<uint8_t>(from));
    for (int i = 0; i < 3; ++i) w.step({});
}

bool lying(const World& w, RemainKind kind) {
    for (const Remain& r : w.remains())
        if (r.kind == kind) return true;
    return false;
}

const Projectile* thrownBy(const World& w, EntityId thrower) {
    for (const Projectile& p : w.projectiles())
        if (p.thrown && p.shooter == thrower) return &p;
    return nullptr;
}

}  // namespace

TEST(hyena_picks_the_satchel_over_a_nearer_head) {
    World w(WorldConfig{});
    lay(w, RemainKind::Head, 5, 3);  // 5.8 m off
    // A satchel a grenadier laid 14 m the other way (it never goes off by itself).
    w.placeSatchel(1, {m(-14), m(0), m(0)}, unitStats(UnitType::DwarfGrenadier).satchels.charge);
    const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    // Men to throw it at, out of their own aggro range.
    for (int i = 0; i < 3; ++i) w.spawnUnit(1, {m(-14), m(22 + i * 2), m(0)}, kWest, standing(UnitType::LightInfantry));
    enum class First { None, Satchel, Head } first = First::None;
    bool headStillLying = false;
    int explosions = 0;
    for (int i = 0; i < 8 * kTicksPerSecond; ++i) {
        w.step({});
        const Unit* h = w.findUnit(hyena);
        if (first == First::None && h->scav.satchel) {
            first = First::Satchel;
            headStillLying = lying(w, RemainKind::Head);
        }
        if (first == First::None && h->scav.held == RemainKind::Head) first = First::Head;
        for (const ImpactEvent& e : w.impacts()) explosions += e.kind == ImpactEvent::Kind::Explosion;
    }
    const Unit* h = w.findUnit(hyena);
    int hurt = 0;
    for (const Unit& u : w.units()) hurt += u.owner == 1 && u.hp < u.stats.maxHp;
    std::printf("  took the %s first; %d explosion(s); %d of the men hurt; the hyena at %d hp\n",
                first == First::Satchel ? "satchel" : first == First::Head ? "head" : "nothing", explosions, hurt, h->hp);
    CHECK(first == First::Satchel);
    CHECK(headStillLying);
    // Thrown at the men, it burst where it struck, among them, not in his hands.
    CHECK(explosions == 1);
    CHECK(hurt > 0);
    CHECK(h->alive());
}

TEST(a_thrown_head_breaks_the_man_it_hits) {
    // A hyena takes up what lies at his feet and throws it at a man standing 9 m off.
    const auto throwAt = [](RemainKind kind, bool& routed, int32_t& hurt) {
        World w(WorldConfig{});
        lay(w, kind, 1, 0);
        const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
        UnitStats target = standing(UnitType::LightInfantry);
        target.melee.damage = 0;
        const EntityId man = w.spawnUnit(1, {m(9), m(0), m(0)}, kEast, target);
        routed = false;
        for (int i = 0; i < 10 * kTicksPerSecond && !routed; ++i) {
            w.step({});
            routed = w.findUnit(man)->routed;
        }
        hurt = target.maxHp - w.findUnit(man)->hp;
        (void)hyena;
    };
    bool headRouted = false, armRouted = false;
    int32_t headHurt = 0, armHurt = 0;
    throwAt(RemainKind::Head, headRouted, headHurt);
    throwAt(RemainKind::Arm, armRouted, armHurt);
    std::printf("  a head: %s, %d hp; an arm: %s, %d hp\n", headRouted ? "broke him" : "did not break him", headHurt,
                armRouted ? "broke him" : "did not break him", armHurt);
    CHECK(headRouted && headHurt > 0);
    CHECK(!armRouted && armHurt > 0);

    // Only a hand's throw: a head blown at him by a blast hurts, and no more.
    World w(WorldConfig{});
    const EntityId man = w.spawnUnit(1, {m(0), m(0), m(0)}, kWest, standing(UnitType::LightInfantry));
    w.launchPart(RemainKind::Head, 0, {m(-5), m(0), Fixed::fromRatio(6, 5)}, {m(14), m(0), Fixed::one()});
    for (int i = 0; i < kTicksPerSecond; ++i) w.step({});
    CHECK(w.findUnit(man)->hp < w.findUnit(man)->stats.maxHp);
    CHECK(!w.findUnit(man)->routed && w.findUnit(man)->morale > kMoraleWaver);
}

TEST(hyena_prefers_an_archer_target_over_heavy_infantry) {
    World w(WorldConfig{});
    lay(w, RemainKind::Arm, 1, 0);
    const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    // The heavy infantryman is nearer; the archer farther, but in range.
    UnitStats hi = unitStats(UnitType::HeavyInfantry);
    hi.aggroRange = Fixed::zero();
    const EntityId knight = w.spawnUnit(1, {m(9), m(-7), m(0)}, kWest, hi);
    const EntityId archer = w.spawnUnit(1, {m(14), m(10), m(0)}, kWest, standing(UnitType::Archers));
    const Projectile* first = nullptr;
    EntityId markAtThrow = kNoEntity;
    Vec3 from;
    for (int i = 0; i < 6 * kTicksPerSecond && !first; ++i) {
        w.step({});
        first = thrownBy(w, hyena);
        if (first) {
            markAtThrow = w.findUnit(hyena)->scav.mark;
            from = w.findUnit(hyena)->pos;
        }
    }
    CHECK(first != nullptr);
    if (!first) return;
    const Vec3 toArcher = (w.findUnit(archer)->pos - from).flat(), toKnight = (w.findUnit(knight)->pos - from).flat();
    const Vec3 v = first->vel.flat();
    const double cosArcher = (v.dot(toArcher) / (v.length() * toArcher.length())).toDouble();
    const double cosKnight = (v.dot(toKnight) / (v.length() * toKnight.length())).toDouble();
    std::printf("  thrown at %s (toward the archer %.2f, the knight %.2f)\n", markAtThrow == archer ? "the archer" : "someone else", cosArcher,
                cosKnight);
    CHECK(markAtThrow == archer);
    CHECK(cosArcher > 0.95 && cosArcher > cosKnight);
}

TEST(hyena_keeps_out_of_melee_with_heavy_infantry) {
    // Heavy infantry come at an empty-handed hyena: he never trades blows with them.
    World w(WorldConfig{});
    const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    std::vector<EntityId> wall;
    for (int i = 0; i < 6; ++i) wall.push_back(w.spawnUnit(1, {m(10 + (i % 2) * 2), m(i * 2 - 5), m(0)}, kWest, unitStats(UnitType::HeavyInfantry)));
    int blowsTaken = 0, blowsStruck = 0;
    Fixed closest = m(1000);
    for (int i = 0; i < 30 * kTicksPerSecond; ++i) {
        w.step({});
        const Unit* h = w.findUnit(hyena);
        for (const ImpactEvent& e : w.impacts()) {
            if (e.projectile != kNoEntity) continue;
            blowsTaken += e.hitUnit == hyena && (e.kind == ImpactEvent::Kind::Hit || e.kind == ImpactEvent::Kind::Blocked);
            blowsStruck += std::find(wall.begin(), wall.end(), e.hitUnit) != wall.end();
        }
        for (EntityId id : wall) {
            const Unit* k = w.findUnit(id);
            closest = min(closest, (k->pos - h->pos).flat().length() - k->stats.radius - h->stats.radius);
        }
    }
    std::printf("  closest gap %.2f m; blows taken %d, struck %d\n", closest.toDouble(), blowsTaken, blowsStruck);
    CHECK(blowsTaken == 0 && blowsStruck == 0);
    CHECK(closest > unitStats(UnitType::HeavyInfantry).melee.reach);
    CHECK(w.findUnit(hyena)->hp == unitStats(UnitType::Hyena).maxHp);

    // A healer, though, he cuts down.
    World v(WorldConfig{});
    const EntityId h2 = v.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    const EntityId healer = v.spawnUnit(1, {m(8), m(0), m(0)}, kWest, unitStats(UnitType::Journeyman));
    for (int i = 0; i < 20 * kTicksPerSecond && v.findUnit(healer)->alive(); ++i) v.step({});
    CHECK(!v.findUnit(healer)->alive());
    CHECK(v.findUnit(h2)->kills == 1);
}

TEST(hyena_takes_a_blade_from_the_dead_and_throws_it) {
    World w(WorldConfig{});
    // A swordsman falls (a head flung at a man with one hit point left).
    UnitStats frail = standing(UnitType::LightInfantry);
    frail.maxHp = 1;
    const EntityId dead = w.spawnUnit(1, {m(4), m(0), m(0)}, kWest, frail);
    w.launchPart(RemainKind::Head, 0, {m(-1), m(0), Fixed::fromRatio(6, 5)}, {m(14), m(0), Fixed::one()});
    for (int i = 0; i < 2 * kTicksPerSecond; ++i) w.step({});
    CHECK(!w.findUnit(dead)->alive() && !w.findUnit(dead)->gibbed);
    // The head lies nearer, but the blade is worth more.
    const EntityId hyena = w.spawnUnit(0, {m(0), m(-1), m(0)}, kEast, unitStats(UnitType::Hyena));
    const EntityId man = w.spawnUnit(1, {m(4), m(12), m(0)}, kWest, standing(UnitType::LightInfantry));
    bool tookBlade = false, threwBlade = false;
    for (int i = 0; i < 6 * kTicksPerSecond && !threwBlade; ++i) {
        w.step({});
        tookBlade = tookBlade || w.findUnit(hyena)->scav.held == RemainKind::Blade;
        const Projectile* p = thrownBy(w, hyena);
        threwBlade = p && p->remain == RemainKind::Blade && p->source == static_cast<uint8_t>(UnitType::LightInfantry);
    }
    CHECK(w.findUnit(dead)->looted);
    CHECK(tookBlade && threwBlade);
    for (int i = 0; i < 3 * kTicksPerSecond; ++i) w.step({});
    std::printf("  the man hit by the blade: %d of %d hp\n", w.findUnit(man)->hp, w.findUnit(man)->stats.maxHp);
}

TEST(raid_scenario_is_deterministic) {
    const auto run = [](uint64_t seed, int* thrown, int* heads, int* kills) {
        const Replay r = scenarios::Raid::replay(seed);
        World w(r.config);
        scenarios::Raid::setup(w);
        runReplay(w, r, scenarios::Raid::kTicks, [&](const World& world) {
            for (const Projectile& p : world.projectiles()) {
                if (!p.thrown || p.ageTicks != 1) continue;
                if (thrown) ++*thrown;
                if (heads && p.remain == RemainKind::Head) ++*heads;
            }
        });
        if (kills)
            for (const Unit& u : w.units()) *kills += u.stats.type == static_cast<uint8_t>(UnitType::Hyena) ? u.kills : 0;
        return w.checksum();
    };
    int thrown = 0, heads = 0, kills = 0;
    const uint64_t a = run(1234, &thrown, &heads, &kills);
    std::printf("  raid: %d things thrown (%d heads), %d killed by hyena-folk\n", thrown, heads, kills);
    CHECK(a == run(1234, nullptr, nullptr, nullptr));
    CHECK(thrown > 0);
    constexpr uint64_t kGolden = 0x2830d44ad3349ec1;
    if (a != kGolden) std::printf("  Raid checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}
