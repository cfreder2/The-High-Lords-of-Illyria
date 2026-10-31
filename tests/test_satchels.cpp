// Satchel charges and Molotovs (engine/sim/src/world_satchels.cpp): laid
// satchels wait for a spark and chain, a Molotov's fire is one, a grenadier
// killed by fire or a blast goes up with all he carries, and left to himself
// he lays them in the enemy's path.
#include <cstdio>

#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;

const Explosive& charge() {
    static const Explosive c = unitStats(UnitType::DwarfGrenadier).satchels.charge;
    return c;
}

// A man who stands where he is put and does nothing.
UnitStats dummy(UnitType type) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged = {};
    s.melee.damage = 0;
    s.shield.blockChance = Fixed::zero();
    s.satchels = {};
    return s;
}

int count(const World& w, ImpactEvent::Kind kind) {
    int n = 0;
    for (const ImpactEvent& e : w.impacts()) n += e.kind == kind;
    return n;
}

int laidSatchels(const World& w) {
    int n = 0;
    for (const Projectile& p : w.projectiles()) n += p.kind == ProjectileKind::Bomb && p.fuse == 0;
    return n;
}

}  // namespace

TEST(laid_satchels_wait_for_a_spark_and_chain) {
    World w(WorldConfig{});
    // Three in a row, each within the next one's blast; a fourth well apart.
    for (int x : {0, 3, 6}) w.placeSatchel(0, {m(x), m(0), m(0)}, charge());
    w.placeSatchel(0, {m(20), m(0), m(0)}, charge());
    int explosions = 0;
    for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
        w.step({});
        explosions += count(w, ImpactEvent::Kind::Explosion);
    }
    CHECK(explosions == 0 && laidSatchels(w) == 4);  // never by themselves

    // One set off beside the row: the row goes, one after another; the fourth lies on.
    w.placeSatchel(0, {m(-3), m(0), m(0)}, charge(), kTicksPerSecond);
    int first = -1, last = -1;
    for (int i = 0; i < 6 * kTicksPerSecond; ++i) {
        w.step({});
        const int n = count(w, ImpactEvent::Kind::Explosion);
        if (n > 0 && first < 0) first = i;
        if (n > 0) last = i;
        explosions += n;
    }
    std::printf("  %d blasts over %.2f s; %d left lying\n", explosions, (last - first) / static_cast<double>(kTicksPerSecond), laidSatchels(w));
    CHECK(explosions == 4);
    CHECK(last > first);  // a chain, not one blast
    CHECK(laidSatchels(w) == 1);
}

TEST(a_molotov_burst_sets_off_a_satchel_it_reaches) {
    World w(WorldConfig{});
    const EntityId dwarf = w.spawnUnit(0, {m(-14), m(0), m(0)}, kEast, unitStats(UnitType::DwarfGrenadier));
    w.placeSatchel(1, {m(0), m(0), m(0)}, charge());
    const Command fire{0, dwarf, Command::Type::AttackGround, {m(0), m(0), m(0)}};
    w.step(std::span(&fire, 1));
    int burstAt = -1, blastAt = -1;
    bool fiery = false;
    Vec3 blast;
    for (int i = 0; i < 4 * kTicksPerSecond && blastAt < 0; ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) {
            if (e.kind == ImpactEvent::Kind::Burst && burstAt < 0) {
                burstAt = i;
                fiery = e.cause == DamageType::Fire;
            }
            if (e.kind == ImpactEvent::Kind::Explosion && blastAt < 0) {
                blastAt = i;
                blast = e.pos;
            }
        }
    }
    std::printf("  the bottle burst at %.2f s, the satchel went %.2f s later\n", burstAt / static_cast<double>(kTicksPerSecond),
                (blastAt - burstAt) / static_cast<double>(kTicksPerSecond));
    CHECK(burstAt >= 0 && fiery);
    CHECK(blastAt > burstAt);
    CHECK((blast - Vec3{}).flat().length() < m(3));
    CHECK(laidSatchels(w) == 0);
}

TEST(a_grenadier_killed_by_a_blast_goes_up_with_all_his_satchels) {
    const auto run = [](bool byBlast, Fixed& biggest, int& spilled, int32_t& bystanderHurt) {
        World w(WorldConfig{});
        UnitStats dwarf = unitStats(UnitType::DwarfGrenadier);
        dwarf.maxHp = 1;
        const EntityId g = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, dwarf);
        // Friends standing 6 m off: beyond one satchel's blast, inside five's.
        std::vector<EntityId> near;
        for (int k = 0; k < 4; ++k)
            near.push_back(w.spawnUnit(0, {m(k == 0 ? 6 : k == 1 ? -6 : 0), m(k == 2 ? 6 : k == 3 ? -6 : 0), m(0)}, kEast, dummy(UnitType::HeavyInfantry)));
        if (byBlast) {
            w.placeSatchel(1, {m(1), m(0), m(0)}, charge(), 5);  // a burning satchel at his feet
        } else {
            w.launchPart(RemainKind::Head, 1, {m(-4), m(0), Fixed::one()}, {m(14), m(0), Fixed::one()});  // a blow, not a blast
        }
        biggest = Fixed::zero();
        for (int i = 0; i < 3 * kTicksPerSecond; ++i) {
            w.step({});
            for (const ImpactEvent& e : w.impacts())
                if (e.kind == ImpactEvent::Kind::Explosion) biggest = max(biggest, e.radius);
        }
        CHECK(!w.findUnit(g)->alive());
        CHECK(w.findUnit(g)->satchels == 0);
        spilled = laidSatchels(w);
        bystanderHurt = 0;
        for (EntityId id : near) bystanderHurt += w.findUnit(id)->stats.maxHp - w.findUnit(id)->hp;
    };
    Fixed blastBig, blowBig;
    int blastSpilled = 0, blowSpilled = 0;
    int32_t blastHurt = 0, blowHurt = 0;
    run(true, blastBig, blastSpilled, blastHurt);
    run(false, blowBig, blowSpilled, blowHurt);
    const int n = unitStats(UnitType::DwarfGrenadier).satchels.count;
    std::printf("  blown up: a %.1f m blast, %d hp off the men 6 m away; struck down: %d satchels spilled, no blast (%.1f m)\n",
                blastBig.toDouble(), blastHurt, blowSpilled, blowBig.toDouble());
    CHECK(blastBig == charge().radius * (3 + n) / 4);
    CHECK(blastHurt > 0 && blastSpilled == 0);
    CHECK(blowBig == Fixed::zero() && blowSpilled == n && blowHurt == 0);
}

TEST(grenadiers_lay_satchels_in_the_enemys_path) {
    World w(WorldConfig{});
    const EntityId g = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::DwarfGrenadier));
    std::vector<EntityId> foes;
    for (int i = 0; i < 6; ++i) foes.push_back(w.spawnUnit(1, {m(40 + (i / 3) * 2), m((i % 3) * 2 - 2), m(0)}, kWest, unitStats(UnitType::LightInfantry)));
    std::vector<Command> march;
    for (EntityId id : foes) march.push_back({1, id, Command::Type::Move, {m(-30), m(0), m(0)}});
    w.step(march);
    int laidMost = 0, explosions = 0, bursts = 0;
    for (int i = 0; i < 25 * kTicksPerSecond; ++i) {
        w.step({});
        laidMost = std::max(laidMost, laidSatchels(w));
        explosions += count(w, ImpactEvent::Kind::Explosion);
        bursts += count(w, ImpactEvent::Kind::Burst);
    }
    int dead = 0;
    for (EntityId id : foes) dead += !w.findUnit(id)->alive();
    std::printf("  %d satchel(s) laid, %d left to carry; %d Molotov bursts, %d blasts; %d of 6 dead\n",
                unitStats(UnitType::DwarfGrenadier).satchels.count - w.findUnit(g)->satchels, w.findUnit(g)->satchels, bursts, explosions, dead);
    CHECK(laidMost >= 1);
    CHECK(w.findUnit(g)->satchels < unitStats(UnitType::DwarfGrenadier).satchels.count);
    CHECK(explosions >= 1);  // set off under them by his own fire
}
