// Satchel charges and Molotovs (engine/sim/src/world_satchels.cpp): laid
// satchels wait for a spark and chain, a Molotov's fire is one, a grenadier
// killed by fire or a blast goes up with all he carries, and left to himself
// he lays them in the enemy's path. The trap as an order (lay a line, Detonate),
// the stores (nothing that bursts is carried without end, and a man is thrifty
// with his), and water (oil and powder are dead in it, a fireball is steam).
#include <cstdio>
#include <memory>
#include <vector>

#include "conquest_orders.h"
#include "illyria/sim/conquest.h"
#include "illyria/sim/region.h"

#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
Fixed r(int64_t n, int64_t d) { return Fixed::fromRatio(n, d); }
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

// --- The trap as the player's play, the stores, and water --------------------

namespace {

Command order(EntityId unit, Command::Type type, const Vec3& at, bool queue = false) {
    Command c{0, unit, type, at};
    c.queue = queue;
    return c;
}

int alive(const World& w, const std::vector<EntityId>& ids) {
    int n = 0;
    for (EntityId id : ids) n += w.findUnit(id)->alive();
    return n;
}

// A dwarf who does only what he is told.
UnitStats sapper() {
    UnitStats s = unitStats(UnitType::DwarfGrenadier);
    s.aggroRange = Fixed::zero();
    return s;
}

// A column of `n` light foot, three abreast, its head at x = `head`, coming west along y = 0 for `quarry`.
std::vector<EntityId> column(World& w, int n, int head, EntityId quarry) {
    std::vector<EntityId> foes;
    for (int i = 0; i < n; ++i) foes.push_back(w.spawnUnit(1, {m(head + (i / 3) * 2), m((i % 3) * 2 - 2), m(0)}, kWest, unitStats(UnitType::LightInfantry)));
    std::vector<Command> march;
    for (EntityId id : foes) {
        Command c{1, id, Command::Type::AttackUnit, w.findUnit(quarry)->pos};
        c.targetUnit = quarry;
        march.push_back(c);
    }
    w.step(march);
    return foes;
}

// A foe who stands where he is put whatever is done to him.
UnitStats post(int32_t hp = 5000) {
    UnitStats s = dummy(UnitType::HeavyInfantry);
    s.maxHp = hp;
    s.moraleShockPct = 0;
    return s;
}

std::shared_ptr<const Region> fordRegion() {
    static const auto region = std::make_shared<const Region>(Region::generateIllyria(20261027));
    return region;
}
// The middle of the ford (water), and the first dry ground west of it.
void fordPlaces(Vec3& water, Vec3& bank) {
    const Region& r = *fordRegion();
    water = r.ford();
    Fixed x = water.x;
    while (r.groundAt(x, water.y) == Ground::Shallows || r.groundAt(x, water.y) == Ground::DeepWater) x -= Fixed::one();
    bank = {x - m(3), water.y, m(0)};
}

}  // namespace

TEST(explosives_are_carried_in_counts_and_none_is_made_in_battle) {
    // What each carries: the unit sheet's numbers, pinned here as the rule (no thrower without a count).
    CHECK(unitStats(UnitType::DwarfGrenadier).ranged.ammo == 6 && unitStats(UnitType::DwarfGrenadier).satchels.count == 5);
    CHECK(unitStats(UnitType::Wizards).ranged.ammo == 4);
    CHECK(unitStats(UnitType::Dragons).ranged.ammo == 6);
    CHECK(unitStats(UnitType::Catapult).ranged.ammo == 16);
    for (int t = 0; t < static_cast<int>(UnitType::Count); ++t) {
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.ranged.muzzleSpeed > Fixed::zero() && s.ranged.explosive.radius > Fixed::zero()) CHECK(s.ranged.ammo > 0);
    }
    // A wizard ordered to burn a patch of ground throws four fireballs, and then no more for as long as he stands.
    World w(WorldConfig{});
    const EntityId wizard = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Wizards));
    const Command burn = order(wizard, Command::Type::AttackGround, {m(30), m(0), m(0)});
    w.step(std::span(&burn, 1));
    int blasts = 0;
    for (int i = 0; i < 90 * kTicksPerSecond; ++i) {
        w.step({});
        blasts += count(w, ImpactEvent::Kind::Explosion);
    }
    std::printf("  a wizard's store: %d fireballs thrown in 90 s, %d left\n", blasts, w.findUnit(wizard)->ammo);
    CHECK(blasts == 4 && w.findUnit(wizard)->ammo == 0);
    CHECK(!World::armed(*w.findUnit(wizard)));
}

TEST(a_store_is_not_spent_on_one_man_or_a_running_one) {
    // A wizard with no order: one foot soldier standing 30 m off draws no fire; four in a knot do; the same four broken and running do not.
    const auto thrown = [](int foes, bool routed) {
        World w(WorldConfig{});
        const EntityId wizard = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Wizards));
        std::vector<EntityId> ids;
        for (int k = 0; k < foes; ++k) ids.push_back(w.spawnUnit(1, {m(30 + k % 2), m(k / 2), m(0)}, kWest, dummy(UnitType::LightInfantry)));
        if (routed) {
            Command hold = order(wizard, Command::Type::HoldFire, {});  // (until they have turned to run)
            hold.hold = true;
            w.step(std::span(&hold, 1));
            for (EntityId id : ids) w.restoreUnit(id, INT32_MAX, 0, 0);  // no heart left
            for (int i = 0; i < kTicksPerSecond; ++i) w.step({});
            for (EntityId id : ids) CHECK(w.findUnit(id)->routed);
            hold.hold = false;
            w.step(std::span(&hold, 1));
        }
        for (int i = 0; i < 6 * kTicksPerSecond; ++i) w.step({});
        return unitStats(UnitType::Wizards).ranged.ammo - w.findUnit(wizard)->ammo;
    };
    const int one = thrown(1, false), knot = thrown(4, false), running = thrown(4, true);
    std::printf("  fireballs in 6 s: at one man %d, at four together %d, at four running %d\n", one, knot, running);
    CHECK(one == 0);
    CHECK(knot >= 1);
    CHECK(running == 0);
}

TEST(a_grenadier_keeps_his_last_molotov_for_his_satchels) {
    // Two bottles left, no satchels to lay, a knot of foes in reach: he throws one and keeps the other however long they stand.
    World w(WorldConfig{});
    UnitStats dwarf = sapper();
    dwarf.moraleShockPct = 0;  // (alone before four: this is about his bottles, not his nerve)
    const EntityId g = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, dwarf);
    w.restoreLoad(g, 0, 2);
    for (int k = 0; k < 4; ++k) w.spawnUnit(1, {m(13 + k % 2), m(k / 2), m(0)}, kWest, post());
    for (int i = 0; i < 30 * kTicksPerSecond; ++i) w.step({});
    CHECK(w.findUnit(g)->ammo == 1);
    // A satchel with two of them on it is what it was kept for.
    w.placeSatchel(0, {m(13), m(0), m(0)}, charge());
    int blasts = 0;
    for (int i = 0; i < 12 * kTicksPerSecond; ++i) {
        w.step({});

        blasts += count(w, ImpactEvent::Kind::Explosion);
    }
    std::printf("  the last bottle: kept through 30 s of a knot in reach, thrown at the satchel under it (%d blast, %d bottles left)\n", blasts, w.findUnit(g)->ammo);
    CHECK(w.findUnit(g)->ammo == 0 && blasts == 1);
}

TEST(hold_fire_keeps_bottles_and_charges_for_orders) {
    World w(WorldConfig{});
    const EntityId g = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, sapper());
    Command hold = order(g, Command::Type::HoldFire, {});
    hold.hold = true;
    w.step(std::span(&hold, 1));
    for (int k = 0; k < 4; ++k) w.spawnUnit(1, {m(13 + k % 2), m(k / 2), m(0)}, kWest, dummy(UnitType::HeavyInfantry));
    for (int i = 0; i < 15 * kTicksPerSecond; ++i) w.step({});
    const UnitStats s = unitStats(UnitType::DwarfGrenadier);
    CHECK(w.findUnit(g)->ammo == s.ranged.ammo && w.findUnit(g)->satchels == s.satchels.count);
    // An order is still obeyed.
    const Command fire = order(g, Command::Type::Detonate, {m(13), m(0), m(0)});
    w.step(std::span(&fire, 1));
    for (int i = 0; i < 3 * kTicksPerSecond; ++i) w.step({});
    CHECK(w.findUnit(g)->ammo == s.ranged.ammo - 1);
}

TEST(a_queue_of_lay_orders_makes_a_line_and_he_moves_on) {
    World w(WorldConfig{});
    const EntityId g = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, sapper());
    std::vector<Command> orders;
    for (int k = 0; k < 3; ++k) orders.push_back(order(g, Command::Type::LaySatchel, {m(20), m(k * 4 - 4), m(0)}, k > 0));
    orders.push_back(order(g, Command::Type::Move, {m(0), m(0), m(0)}, true));
    w.step(orders);
    for (int i = 0; i < 60 * kTicksPerSecond; ++i) w.step({});
    int inLine = 0;
    for (const Projectile& p : w.projectiles())
        if (p.kind == ProjectileKind::Bomb)
            for (int k = 0; k < 3; ++k) inLine += (p.pos - Vec3{m(20), m(k * 4 - 4), m(0)}).flat().length() < m(2);
    const Unit& u = *w.findUnit(g);
    std::printf("  %d of 3 laid in the line, %d carried; he stands %.1f m from the nearest\n", inLine, u.satchels, (u.pos - Vec3{m(20), m(0), m(0)}).flat().length().toDouble());
    CHECK(inLine == 3 && laidSatchels(w) == 3);
    CHECK(u.satchels == unitStats(UnitType::DwarfGrenadier).satchels.count - 3);
    CHECK((u.pos - Vec3{}).flat().length() < m(2));  // gone on to his next order, not standing on his own trap
}

TEST(detonate_fires_at_once_with_a_friend_in_the_blast_and_the_line_goes_in_sequence) {
    // Three satchels 4 m apart with foes among them and one of his own men standing in the blast: left to himself the
    // dwarf waits on his friend (tests/test_friendly_fire.cpp: 5 s); ordered, he throws at once, and the friend pays.
    World w(WorldConfig{});
    const EntityId g = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, sapper());
    for (int k = 0; k < 3; ++k) w.placeSatchel(0, {m(14), m(k * 4 - 4), m(0)}, charge());
    std::vector<EntityId> foes;
    for (int k = 0; k < 6; ++k) foes.push_back(w.spawnUnit(1, {m(15 + k % 2), m((k / 2) * 4 - 4), m(0)}, kWest, dummy(UnitType::LightInfantry)));
    const EntityId friendly = w.spawnUnit(0, {m(12), m(1), m(0)}, kEast, dummy(UnitType::LightInfantry));
    const Command fire = order(g, Command::Type::Detonate, {m(15), m(1), m(0)});  // (beside the middle one: it is taken)
    w.step(std::span(&fire, 1));
    int first = -1, last = -1, blasts = 0;
    for (int i = 0; i < 6 * kTicksPerSecond; ++i) {
        w.step({});
        const int n = count(w, ImpactEvent::Kind::Explosion);
        if (n > 0 && first < 0) first = i;
        if (n > 0) last = i;
        blasts += n;
    }
    std::printf("  ordered: first blast at %.2f s, the line over %.2f s, %d blasts; %d of 6 foes down, the friend %s\n", first / 30.0,
                (last - first) / 30.0, blasts, 6 - alive(w, foes), w.findUnit(friendly)->alive() ? "lives" : "dead");
    CHECK(first >= 0 && first < 3 * kTicksPerSecond);  // no five-second wait
    CHECK(blasts == 3 && last > first);                // a ripple, not one bang
    CHECK(alive(w, foes) <= 1);
    CHECK(!w.findUnit(friendly)->alive());             // friendly fire is real
    CHECK(w.findUnit(g)->ammo == unitStats(UnitType::DwarfGrenadier).ranged.ammo - 1);  // one bottle for the whole line
    CHECK(!w.findUnit(g)->hasAttackTarget);            // one shot, and the order is done
}

TEST(a_wizard_can_spring_the_trap_and_walks_into_reach_to_do_it) {
    World w(WorldConfig{});
    UnitStats wiz = unitStats(UnitType::Wizards);
    const EntityId wizard = w.spawnUnit(0, {m(-60), m(0), m(0)}, kEast, wiz);
    w.placeSatchel(0, {m(20), m(0), m(0)}, charge());
    const Command fire = order(wizard, Command::Type::Detonate, {m(20), m(0), m(0)});
    w.step(std::span(&fire, 1));
    int blasts = 0;
    for (int i = 0; i < 40 * kTicksPerSecond; ++i) {
        w.step({});
        blasts += count(w, ImpactEvent::Kind::Explosion);
    }
    CHECK(blasts == 2);  // his fireball, and the satchel
    CHECK(w.findUnit(wizard)->ammo == wiz.ranged.ammo - 1);
    CHECK(w.findUnit(wizard)->pos.x > m(-45));  // he came up into his reach (50 m)
}

TEST(a_blast_shakes_the_men_it_leaves_standing) {
    World w(WorldConfig{});
    w.placeSatchel(0, {m(0), m(0), m(0)}, charge(), 2);
    const EntityId near = w.spawnUnit(1, {m(8), m(0), m(0)}, kWest, dummy(UnitType::LightInfantry));   // outside the blast, inside twice it
    const EntityId far = w.spawnUnit(1, {m(30), m(0), m(0)}, kWest, dummy(UnitType::LightInfantry));
    for (int i = 0; i < 6; ++i) w.step({});
    CHECK(w.findUnit(near)->hp == w.findUnit(near)->stats.maxHp);
    CHECK(w.findUnit(near)->morale < w.findUnit(far)->morale);
}

TEST(a_mined_road_one_dwarf_and_a_column) {
    // Twelve light foot come down a road for a lone dwarf. Used well: three satchels laid along it beforehand, the dwarf
    // 15 m back, one Molotov at the nearest when the head of the column is on the line. Left alone (no charges laid, no
    // order): he does what he can by himself. And with no dwarf at all the column arrives whole.
    const auto run = [](int mode, int& satchelsUsed, int& bottles, bool& dwarfLives, int& broken) {
        World w(WorldConfig{});
        const EntityId g = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::DwarfGrenadier));
        if (mode == 0)
            for (int k = 0; k < 3; ++k) w.placeSatchel(0, {m(15 + k * 3), m(0), m(0)}, charge());  // along the road, 15 to 21 m out
        if (mode == 0) {
            Command hold = order(g, Command::Type::HoldFire, {});
            hold.hold = true;
            w.step(std::span(&hold, 1));
        }
        const std::vector<EntityId> foes = column(w, 12, 45, g);
        bool sprung = false;
        for (int i = 0; i < 90 * kTicksPerSecond; ++i) {
            std::vector<Command> cmds;
            if (mode == 0 && !sprung) {
                int on = 0;
                for (EntityId id : foes) on += w.findUnit(id)->pos.x < m(19);
                // (The bottle flies 15 m in a second and a bit: he throws as the head of the column passes the middle charge.)
                if (on >= 1) cmds.push_back(order(g, Command::Type::Detonate, {m(15), m(0), m(0)})), sprung = true;
            }
            w.step(cmds);
        }
        const UnitStats s = unitStats(UnitType::DwarfGrenadier);
        satchelsUsed = mode == 0 ? 3 : s.satchels.count - w.findUnit(g)->satchels;
        bottles = s.ranged.ammo - w.findUnit(g)->ammo;
        dwarfLives = w.findUnit(g)->alive();
        broken = 0;
        for (EntityId id : foes) broken += w.findUnit(id)->alive() && w.findUnit(id)->routed;
        return 12 - alive(w, foes);
    };
    int usedTrap = 0, usedAlone = 0, bottlesTrap = 0, bottlesAlone = 0, brokenTrap = 0, brokenAlone = 0;
    bool livesTrap = false, livesAlone = false;
    const int trap = run(0, usedTrap, bottlesTrap, livesTrap, brokenTrap);
    const int alone = run(1, usedAlone, bottlesAlone, livesAlone, brokenAlone);
    std::printf("  the trap: %d of 12 down (%d more broken) for 3 satchels and %d bottle: %.1f a satchel; the dwarf %s\n", trap, brokenTrap, bottlesTrap, trap / 3.0,
                livesTrap ? "lives" : "dead");
    std::printf("  left alone: %d of 12 down (%d broken) for %d satchels and %d bottles; the dwarf %s\n", alone, brokenAlone, usedAlone, bottlesAlone,
                livesAlone ? "lives" : "dead");
    CHECK(trap >= 8);         // most of a squad
    CHECK(bottlesTrap == 1);
    CHECK(livesTrap);
    CHECK(trap > alone);
    CHECK(!livesAlone);       // twelve men on one dwarf with no trap laid: he is dead
}

TEST(water_puts_out_molotovs_and_satchels_and_turns_a_fireball_to_steam) {
    Vec3 water, bank;
    fordPlaces(water, bank);
    WorldConfig cfg;
    cfg.region = fordRegion();
    // A Molotov thrown into the ford: doused, no burst; the men standing there unhurt.
    {
        World w(cfg);
        const EntityId g = w.spawnUnit(0, bank, kEast, sapper());
        const EntityId foe = w.spawnUnit(1, water, kWest, dummy(UnitType::LightInfantry));
        CHECK(w.douses(water) && !w.douses(bank));
        const Command fire = order(g, Command::Type::Detonate, water);
        w.step(std::span(&fire, 1));
        int doused = 0, bursts = 0;
        for (int i = 0; i < 4 * kTicksPerSecond; ++i) {
            w.step({});
            doused += count(w, ImpactEvent::Kind::Doused);
            bursts += count(w, ImpactEvent::Kind::Burst);
        }
        CHECK(doused == 1 && bursts == 0);
        CHECK(w.findUnit(foe)->hp == w.findUnit(foe)->stats.maxHp);
    }
    // A satchel is not laid in water (the order is refused), and one lying there is dead: a blast beside it does not set it off.
    {
        World w(cfg);
        const EntityId g = w.spawnUnit(0, bank, kEast, sapper());
        const Command lay = order(g, Command::Type::LaySatchel, water);
        w.step(std::span(&lay, 1));
        for (int i = 0; i < 20 * kTicksPerSecond; ++i) w.step({});
        CHECK(laidSatchels(w) == 0 && w.findUnit(g)->satchels == unitStats(UnitType::DwarfGrenadier).satchels.count);
        CHECK(!w.wading(*w.findUnit(g)));
        const Vec3 wetSpot = bank + Vec3{m(5), m(0), m(0)};
        CHECK(w.douses(wetSpot));
        w.placeSatchel(0, wetSpot, charge());                           // one that ended up in the water
        w.placeSatchel(0, bank + Vec3{m(2), m(0), m(0)}, charge(), 5);  // and one burning on the bank, 3 m from it
        int blasts = 0;
        for (int i = 0; i < 5 * kTicksPerSecond; ++i) {
            w.step({});
            blasts += count(w, ImpactEvent::Kind::Explosion);
        }
        CHECK(blasts == 1 && laidSatchels(w) == 1);
    }
    // A fireball on a man standing in the ford: steam, a third as wide and a third as hard.
    {
        const auto hurt = [&](const Vec3& at, int& steam) {
            World w(cfg);
            const EntityId wizard = w.spawnUnit(0, at - Vec3{m(25), m(0), m(0)}, kEast, unitStats(UnitType::Wizards));
            const EntityId foe = w.spawnUnit(1, at, kWest, post());
            const Command fire = order(wizard, Command::Type::Detonate, at);
            w.step(std::span(&fire, 1));
            steam = 0;
            Fixed radius;
            for (int i = 0; i < 4 * kTicksPerSecond; ++i) {
                w.step({});
                for (const ImpactEvent& e : w.impacts())
                    if (e.kind == ImpactEvent::Kind::Steam) ++steam, radius = e.radius;
            }
            if (steam) CHECK(radius == unitStats(UnitType::Wizards).ranged.explosive.radius / 3);
            return 5000 - w.findUnit(foe)->hp;
        };
        int steamWet = 0, steamDry = 0;
        const int wet = hurt(water, steamWet), dry = hurt(bank - Vec3{m(10), m(0), m(0)}, steamDry);
        std::printf("  a fireball on a man: in the ford %d hp (steam), on the bank %d hp\n", wet, dry);
        CHECK(steamWet == 1 && steamDry == 0);
        CHECK(wet > 0 && wet * 5 <= dry * 2);  // a third of the blast, and nearer its edge
    }
}

TEST(no_bottle_is_thrown_at_men_in_the_ford_of_his_own_accord) {
    Vec3 water, bank;
    fordPlaces(water, bank);
    WorldConfig cfg;
    cfg.region = fordRegion();
    const auto thrown = [&](UnitType who, const Vec3& from, const Vec3& knot) {
        World w(cfg);
        UnitStats s = unitStats(who);
        s.satchels = {};
        s.aggroRange = Fixed::zero();
        const EntityId t = w.spawnUnit(0, from, kEast, s);
        for (int k = 0; k < 4; ++k) w.spawnUnit(1, knot + Vec3{m(k % 2) * 2, m(k / 2) * 2, m(0)}, kWest, dummy(UnitType::LightInfantry));
        for (int i = 0; i < 12 * kTicksPerSecond; ++i) w.step({});
        return s.ranged.ammo - w.findUnit(t)->ammo;
    };
    const Vec3 dry = bank - Vec3{m(14), m(0), m(0)};
    const int dwarfWet = thrown(UnitType::DwarfGrenadier, water - Vec3{m(13), m(0), m(0)}, water);
    const int dwarfDry = thrown(UnitType::DwarfGrenadier, dry - Vec3{m(13), m(0), m(0)}, dry);
    const int wizardWet = thrown(UnitType::Wizards, water - Vec3{m(30), m(0), m(0)}, water);
    const int wizardDry = thrown(UnitType::Wizards, dry - Vec3{m(30), m(0), m(0)}, dry);
    std::printf("  at four men in the ford / on dry ground: Molotovs %d / %d, fireballs %d / %d\n", dwarfWet, dwarfDry, wizardWet, wizardDry);
    CHECK(dwarfWet == 0 && dwarfDry >= 1);
    CHECK(wizardWet == 0 && wizardDry >= 1);
}

// --- Precious: kept out of the melee, guarded, hunted, and few --------------------------------------------------

TEST(a_dwarf_gives_ground_behind_the_foot_and_the_foot_comes_to_him) {
    // A dwarf with nothing left to throw, a swordsman of his side 8 m behind him, one foe coming for him. Left to himself
    // he gives ground toward the swordsman, who comes to the foe (and has him before the dwarf has gone far); told to
    // hold, he stands where he is.
    const auto run = [](bool hold, Fixed& dwarfX, bool& friendFought, bool& lives) {
        World w(WorldConfig{});
        const EntityId g = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::DwarfGrenadier));
        w.restoreLoad(g, 0, 0);
        const EntityId guard = w.spawnUnit(0, {m(-8), m(0), m(0)}, kEast, unitStats(UnitType::HeavyInfantry));
        const EntityId foe = w.spawnUnit(1, {m(22), m(0), m(0)}, kWest, unitStats(UnitType::LightInfantry));
        std::vector<Command> cmds;
        Command hunt{1, foe, Command::Type::AttackUnit, {}};
        hunt.targetUnit = g;
        cmds.push_back(hunt);
        if (hold) {
            Command stand{0, g, Command::Type::SetStance, {}};
            stand.hold = true;
            cmds.push_back(stand);
        }
        w.step(cmds);
        friendFought = false;
        Fixed least = m(100);
        for (int i = 0; i < 12 * kTicksPerSecond; ++i) {
            w.step({});
            friendFought = friendFought || w.findUnit(guard)->engaged || w.findUnit(guard)->foe == foe;
            least = min(least, w.findUnit(g)->pos.x);
        }
        dwarfX = least;
        lives = w.findUnit(g)->alive();
    };
    Fixed shyX, heldX;
    bool shyFought = false, heldFought = false, shyLives = false, heldLives = false;
    run(false, shyX, shyFought, shyLives);
    run(true, heldX, heldFought, heldLives);
    std::printf("  left to himself he gave ground to x = %.1f (the guard at -8) and %s; told to hold he stood at %.1f\n", shyX.toDouble(), shyLives ? "lives" : "died",
                heldX.toDouble());
    CHECK(shyX < m(-2));   // back toward the swordsman
    CHECK(shyFought);      // who came to the foe
    CHECK(shyLives);
    CHECK(heldX > m(-1));  // a man told to hold keeps his place
}

TEST(bowmen_shoot_the_foes_dwarf_before_his_line) {
    // The foe's line 30 m off, his dwarf farther (42 m) and out to one side of it, where no body stands between. (Straight
    // behind the line he is covered: an arrow strikes the first man in its way.)
    World w(WorldConfig{});
    for (int k = 0; k < 6; ++k) w.spawnUnit(0, {m(0), m(k * 2 - 5), m(0)}, kEast, unitStats(UnitType::Archers));
    std::vector<EntityId> line;
    for (int k = 0; k < 6; ++k) line.push_back(w.spawnUnit(1, {m(30), m(k * 2 - 5), m(0)}, kWest, post()));
    UnitStats idle = dummy(UnitType::DwarfGrenadier);
    idle.satchels = unitStats(UnitType::DwarfGrenadier).satchels;  // (still a dwarf to the eye; he lays nothing: his load is taken)
    idle.moraleShockPct = 0;
    const EntityId g = w.spawnUnit(1, {m(38), m(18), m(0)}, kWest, idle);
    w.restoreLoad(g, 0, 0);
    int dwarfDied = -1;
    for (int i = 0; i < 40 * kTicksPerSecond && dwarfDied < 0; ++i) {
        w.step({});
        if (!w.findUnit(g)->alive()) dwarfDied = i;
    }
    int lineHurt = 0;
    for (EntityId id : line) lineHurt += 5000 - w.findUnit(id)->hp;
    std::printf("  six bowmen: the dwarf beyond his line dead at %.1f s; the line had taken %d hp by then\n", dwarfDied / 30.0, lineHurt);
    CHECK(dwarfDied >= 0);
    CHECK(lineHurt < 110);  // less than what killed him: the arrows went at the dwarf
}

TEST(those_who_throw_a_blast_come_alone_and_a_realm_keeps_few) {
    CHECK(Conquest::squadSize(UnitType::DwarfGrenadier) == 1);
    CHECK(Conquest::squadSize(UnitType::Wizards) == 1);
    CHECK(Conquest::squadSize(UnitType::Dragons) == 1);
    CHECK(Conquest::keepCap(UnitType::DwarfGrenadier) == 2);
    CHECK(Conquest::keepCap(UnitType::Wizards) == 1 && Conquest::keepCap(UnitType::Dragons) == 1);
    CHECK(Conquest::keepCap(UnitType::Catapult) == 3);
    CHECK(Conquest::keepCap(UnitType::LightInfantry) == 0);
    // One dwarf costs as much as three squads of heavy foot and more, and takes the longest of all to train.
    CHECK(Conquest::price(UnitType::DwarfGrenadier) >= 3 * Conquest::price(UnitType::HeavyInfantry));
    CHECK(Conquest::price(UnitType::Wizards) > Conquest::price(UnitType::DwarfGrenadier));
    CHECK(!Conquest::trainable(UnitType::Wizards));
    // The Moonward Order begins with its one wizard, and is at its cap; no other realm has one.
    static const Continent continent(20261027);
    Conquest c(continent, 1);
    for (uint8_t side = 0; side < 8; ++side) {
        CHECK(c.kept(side, UnitType::Wizards) == (side == 6 ? 1 : 0));
        CHECK(c.kept(side, UnitType::DwarfGrenadier) <= Conquest::keepCap(UnitType::DwarfGrenadier));
    }
    CHECK(c.atCap(6, UnitType::Wizards));
    for (const Conquest::Stack& s : c.stacks())
        for (const Army& a : s.armies)
            if (a.type == UnitType::Wizards || a.type == UnitType::DwarfGrenadier) CHECK(a.men.size() == 1);
    // Ironhold may set a grenadier training (none kept yet); a city of a realm at its cap may not begin another.
    for (size_t i = 0; i < c.cities().size(); ++i) {
        if (c.cities()[i].owner != 2) continue;
        for (size_t o = 0; o < c.cities()[i].options.size(); ++o)
            if (c.cities()[i].options[o].type == UnitType::DwarfGrenadier) CHECK(ConquestOrders::produce(c, static_cast<int32_t>(i), static_cast<int32_t>(o)));
    }
}
