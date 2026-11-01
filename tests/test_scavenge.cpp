// The hyena-folk (engine/sim/src/world_scavenge.cpp): soldiers like any
// other until ordered to raid; raiding, what they take, whom they throw it at
// (and whom they leave alone), what a thrown head does, how they fall back,
// and the heavies' answer to them.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "illyria/sim/scenario.h"
#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;
const Vec3 kNorth{Fixed::zero(), Fixed::one(), Fixed::zero()};

// A man who stands where he is put: no blows, no bow, no shield, no aggro.
UnitStats standing(UnitType type) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged.autoRange = Fixed::zero();
    s.shield.blockChance = Fixed::zero();
    return s;
}

// A man who stands his ground but fights back (and shoots, if he can) as ever: only no aggro.
UnitStats holding(UnitType type) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
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

// One order to each of them, and a tick.
void order(World& w, const std::vector<EntityId>& ids, Command::Type type, const Vec3& target = {}, const Vec3& face = {}) {
    std::vector<Command> cmds;
    for (EntityId id : ids) {
        Command c{w.findUnit(id)->owner, id, type, target};
        c.face = face;
        cmds.push_back(c);
    }
    w.step(cmds);
}

// A pack of hyena-folk for side 0, two abreast, from (x, y) down the y axis.
std::vector<EntityId> pack(World& w, int count, int64_t x, int64_t y, const Vec3& facing = kEast) {
    std::vector<EntityId> ids;
    for (int i = 0; i < count; ++i) ids.push_back(w.spawnUnit(0, {m(x + (i % 2) * 2), m(y - (i / 2) * 2), m(0)}, facing, unitStats(UnitType::Hyena)));
    return ids;
}

double gap(const Unit& a, const Unit& b) {
    return ((a.pos - b.pos).flat().length() - a.stats.radius - b.stats.radius).toDouble();
}

}  // namespace

TEST(hyena_picks_the_satchel_over_a_nearer_head) {
    World w(WorldConfig{});
    lay(w, RemainKind::Head, 5, 3);  // 5.8 m off
    // A satchel a grenadier laid 14 m the other way (it never goes off by itself).
    w.placeSatchel(1, {m(-14), m(0), m(0)}, unitStats(UnitType::DwarfGrenadier).satchels.charge);
    const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    // Archers huddled together to throw it at (a lone hyena dares no more), out of their own aggro range.
    for (int i = 0; i < 3; ++i) w.spawnUnit(1, {m(-14), m(20 + i * 2), m(0)}, kWest, standing(UnitType::Archers));
    order(w, {hyena}, Command::Type::Raid);
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
    // A raiding hyena takes up what lies at his feet and throws it at a man standing 9 m off.
    const auto throwAt = [](RemainKind kind, bool& routed, int32_t& hurt) {
        World w(WorldConfig{});
        lay(w, kind, 1, 0);
        const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
        UnitStats target = standing(UnitType::LightInfantry);
        target.melee.damage = 0;
        const EntityId man = w.spawnUnit(1, {m(9), m(0), m(0)}, kEast, target);
        order(w, {hyena}, Command::Type::Raid);
        routed = false;
        for (int i = 0; i < 10 * kTicksPerSecond && !routed; ++i) {
            w.step({});
            routed = w.findUnit(man)->routed;
        }
        hurt = target.maxHp - w.findUnit(man)->hp;
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
    // The heavy infantryman is nearer; the archer farther, but in sight.
    const EntityId knight = w.spawnUnit(1, {m(9), m(-7), m(0)}, kWest, holding(UnitType::HeavyInfantry));
    const EntityId archer = w.spawnUnit(1, {m(14), m(10), m(0)}, kWest, standing(UnitType::Archers));
    order(w, {hyena}, Command::Type::Raid);
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
    // Heavy infantry march through a raiding, empty-handed hyena: he never trades blows with them.
    World w(WorldConfig{});
    const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    std::vector<EntityId> wall;
    for (int i = 0; i < 6; ++i) wall.push_back(w.spawnUnit(1, {m(10 + (i % 2) * 2), m(i * 2 - 5), m(0)}, kWest, unitStats(UnitType::HeavyInfantry)));
    std::vector<Command> cmds{{0, hyena, Command::Type::Raid, {}}};
    for (size_t i = 0; i < wall.size(); ++i) cmds.push_back({1, wall[i], Command::Type::Move, {m(-20 + static_cast<int64_t>(i % 2) * 2), m(static_cast<int64_t>(i) * 2 - 5), m(0)}});
    w.step(cmds);
    int blowsTaken = 0, blowsStruck = 0;
    double closest = 1000;
    for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
        w.step({});
        const Unit* h = w.findUnit(hyena);
        for (const ImpactEvent& e : w.impacts()) {
            if (e.projectile != kNoEntity) continue;
            blowsTaken += e.hitUnit == hyena && (e.kind == ImpactEvent::Kind::Hit || e.kind == ImpactEvent::Kind::Blocked);
            blowsStruck += std::find(wall.begin(), wall.end(), e.hitUnit) != wall.end();
        }
        for (EntityId id : wall) closest = std::min(closest, gap(*w.findUnit(id), *h));
    }
    std::printf("  closest gap %.2f m; blows taken %d, struck %d\n", closest, blowsTaken, blowsStruck);
    CHECK(blowsTaken == 0 && blowsStruck == 0);
    CHECK(closest > unitStats(UnitType::HeavyInfantry).melee.reach.toDouble());
    CHECK(w.findUnit(hyena)->hp == unitStats(UnitType::Hyena).maxHp);

    // A lone healer, though, he cuts down.
    World v(WorldConfig{});
    const EntityId h2 = v.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    const EntityId healer = v.spawnUnit(1, {m(8), m(0), m(0)}, kWest, unitStats(UnitType::Journeyman));
    order(v, {h2}, Command::Type::Raid);
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
    UnitStats man = standing(UnitType::LightInfantry);
    man.melee.damage = 0;
    const EntityId target = w.spawnUnit(1, {m(4), m(12), m(0)}, kWest, man);
    order(w, {hyena}, Command::Type::Raid);
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
    std::printf("  the man hit by the blade: %d of %d hp\n", w.findUnit(target)->hp, w.findUnit(target)->stats.maxHp);
}

TEST(hyena_squad_ordered_to_a_spot_holds_there) {
    // Not raiding, they are soldiers like any other: sent there facing north,
    // they stand there facing north, whatever lies about or stands near.
    World w(WorldConfig{});
    const std::vector<EntityId> ids = pack(w, 6, 0, 0);
    lay(w, RemainKind::Head, 22, 4);
    lay(w, RemainKind::Blade, 26, -4);
    w.placeSatchel(1, {m(18), m(-6), m(0)}, unitStats(UnitType::DwarfGrenadier).satchels.charge);
    const EntityId archer = w.spawnUnit(1, {m(40), m(0), m(0)}, kWest, standing(UnitType::Archers));
    const EntityId wizard = w.spawnUnit(1, {m(34), m(14), m(0)}, kWest, standing(UnitType::Wizards));
    std::vector<Command> cmds;
    std::vector<Vec3> spots;
    for (size_t i = 0; i < ids.size(); ++i) {
        const Vec3 spot{m(20 + static_cast<int64_t>(i % 2) * 2), m(static_cast<int64_t>(i / 2) * 2), m(0)};
        spots.push_back(spot);
        Command c{0, ids[i], Command::Type::Move, spot};
        c.face = kNorth;
        cmds.push_back(c);
    }
    w.step(cmds);
    int taken = 0, thrown = 0;
    double wander = 0, facing = 1;
    for (int i = 0; i < 25 * kTicksPerSecond; ++i) {
        w.step({});
        for (size_t k = 0; k < ids.size(); ++k) {
            const Unit& u = *w.findUnit(ids[k]);
            taken += u.scav.holding();
            thrown += thrownBy(w, u.id) != nullptr;
            if (i >= 8 * kTicksPerSecond) {
                wander = std::max(wander, (u.pos - spots[k]).flat().length().toDouble());
                facing = std::min(facing, u.facing.dot(kNorth).toDouble());
            }
        }
    }
    std::printf("  farthest from his place %.2f m, least facing north %.2f; took %d, threw %d\n", wander, facing, taken, thrown);
    CHECK(wander < 1.0);
    CHECK(facing > 0.99);
    CHECK(taken == 0 && thrown == 0);
    CHECK(w.findUnit(archer)->hp == w.findUnit(archer)->stats.maxHp && w.findUnit(wizard)->hp == w.findUnit(wizard)->stats.maxHp);
    for (EntityId id : ids) CHECK(!w.findUnit(id)->scav.raiding);
}

TEST(raiders_know_nothing_beyond_their_sight) {
    // Let loose with a wizard and a satchel 40 m off (beyond their 30 m sight):
    // they never go for them, and with nothing in sight the raid ends.
    World w(WorldConfig{});
    const std::vector<EntityId> ids = pack(w, 4, 0, 0);
    w.placeSatchel(1, {m(-40), m(0), m(0)}, unitStats(UnitType::DwarfGrenadier).satchels.charge);
    const EntityId wizard = w.spawnUnit(1, {m(42), m(0), m(0)}, kWest, standing(UnitType::Wizards));
    std::vector<Vec3> from;
    for (EntityId id : ids) from.push_back(w.findUnit(id)->pos);
    order(w, ids, Command::Type::Raid);
    double moved = 0;
    int raidingAt14 = 0, raidingAt17 = 0;
    for (int i = 0; i < 17 * kTicksPerSecond; ++i) {
        w.step({});
        for (size_t k = 0; k < ids.size(); ++k) {
            const Unit& u = *w.findUnit(ids[k]);
            moved = std::max(moved, (u.pos - from[k]).flat().length().toDouble());
            if (i == 14 * kTicksPerSecond) raidingAt14 += u.scav.raiding;
            if (i == 17 * kTicksPerSecond - 1) raidingAt17 += u.scav.raiding;
        }
    }
    std::printf("  moved at most %.2f m; raiding at 14 s %d, at 17 s %d\n", moved, raidingAt14, raidingAt17);
    CHECK(moved < 3.0);
    CHECK(raidingAt14 == 4 && raidingAt17 == 0);
    CHECK(w.findUnit(wizard)->hp == w.findUnit(wizard)->stats.maxHp);
    for (EntityId id : ids) CHECK(!w.findUnit(id)->scav.holding());
}

TEST(raiders_take_the_straggler_not_the_shield_wall) {
    // Heads at their feet; a shield wall of heavy infantry 18 m one way, a
    // lone wizard (or archer) 22 m the other: the throws go at the straggler.
    for (UnitType soft : {UnitType::Wizards, UnitType::Archers}) {
        World w(WorldConfig{});
        const std::vector<EntityId> ids = pack(w, 4, 0, 0);
        for (int k = 0; k < 4; ++k) lay(w, RemainKind::Head, (k % 2) * 2 - 1, -(k / 2) * 2 - 1);
        std::vector<EntityId> wall;
        for (int i = 0; i < 8; ++i) wall.push_back(w.spawnUnit(1, {m(18 + (i % 2) * 2), m(i / 2 * 2 - 3), m(0)}, kWest, holding(UnitType::HeavyInfantry)));
        const EntityId straggler = w.spawnUnit(1, {m(-22), m(2), m(0)}, kEast, standing(soft));
        order(w, ids, Command::Type::Raid);
        int atStraggler = 0, atWall = 0;
        for (int i = 0; i < 12 * kTicksPerSecond; ++i) {
            w.step({});
            for (const Projectile& p : w.projectiles()) {
                if (!p.thrown || p.ageTicks != 1) continue;
                const EntityId mk = w.findUnit(p.shooter)->scav.mark;
                atStraggler += mk == straggler;
                atWall += std::find(wall.begin(), wall.end(), mk) != wall.end();
            }
        }
        std::printf("  %s: %d thrown at the straggler, %d at the wall; he has %d of %d hp\n", unitTypeName(soft), atStraggler, atWall,
                    w.findUnit(straggler)->hp, w.findUnit(straggler)->stats.maxHp);
        CHECK(atStraggler > 0 && atWall == 0);
    }
}

TEST(a_lone_hyena_leaves_a_squad_of_heavies_alone) {
    // A head in hand, a squad of heavy infantry 16 m off: one hyena is no
    // match for them, and does not try; he keeps out of their reach.
    World w(WorldConfig{});
    lay(w, RemainKind::Head, 1, 0);
    const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    std::vector<EntityId> squad;
    for (int i = 0; i < 6; ++i) squad.push_back(w.spawnUnit(1, {m(16 + (i % 2) * 2), m(i / 2 * 2 - 2), m(0)}, kWest, holding(UnitType::HeavyInfantry)));
    order(w, {hyena}, Command::Type::Raid);
    int thrown = 0;
    bool took = false;
    double closest = 1000;
    for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
        w.step({});
        thrown += thrownBy(w, hyena) != nullptr && thrownBy(w, hyena)->ageTicks == 1;
        took = took || w.findUnit(hyena)->scav.holding();
        for (EntityId id : squad) closest = std::min(closest, gap(*w.findUnit(id), *w.findUnit(hyena)));
    }
    std::printf("  took the head: %s; thrown %d; closest gap %.1f m; still raiding: %s\n", took ? "yes" : "no", thrown, closest,
                w.findUnit(hyena)->scav.raiding ? "yes" : "no");
    CHECK(took);
    CHECK(thrown == 0);
    CHECK(closest > unitStats(UnitType::HeavyInfantry).melee.reach.toDouble() + 2.5);
    for (EntityId id : squad) CHECK(w.findUnit(id)->hp == w.findUnit(id)->stats.maxHp);
}

TEST(raiders_keep_out_of_archers_reach_while_other_prey_is_about) {
    // Short bows (24 m on their own, so their reach is within the pack's
    // sight): a squad of archers 28 m north, a wizard 13 m in front of them;
    // a lone swordsman 22 m south-east. The wizard is the softer mark, but
    // to throw at him they would stand in the archers' reach: the pack goes
    // for the swordsman, and never comes within the archers' bowshot.
    World w(WorldConfig{});
    const std::vector<EntityId> ids = pack(w, 4, 0, 0);
    for (int k = 0; k < 4; ++k) lay(w, RemainKind::Head, (k % 2) * 2 - 1, -(k / 2) * 2 - 1);
    UnitStats bow = holding(UnitType::Archers);
    bow.ranged.autoRange = m(24);
    std::vector<EntityId> archers;
    for (int i = 0; i < 6; ++i) archers.push_back(w.spawnUnit(1, {m(-5 + i * 2), m(28), m(0)}, -kNorth, bow));
    const EntityId wizard = w.spawnUnit(1, {m(0), m(15), m(0)}, -kNorth, standing(UnitType::Wizards));
    // (He stays about: he never breaks, and takes a lot of killing.)
    UnitStats sword = standing(UnitType::LightInfantry);
    sword.melee.damage = 0;
    sword.maxHp = 2000;
    sword.moraleShockPct = 0;
    const EntityId swordsman = w.spawnUnit(1, {m(16), m(-15), m(0)}, kWest, sword);
    order(w, ids, Command::Type::Raid);
    int atWizard = 0, atSwordsman = 0;
    double nearest = 1000;  // the nearest any came to an archer
    for (int i = 0; i < 15 * kTicksPerSecond; ++i) {
        w.step({});
        for (const Projectile& p : w.projectiles()) {
            if (!p.thrown || p.ageTicks != 1) continue;
            const EntityId mk = w.findUnit(p.shooter)->scav.mark;
            atWizard += mk == wizard;
            atSwordsman += mk == swordsman;
        }
        for (EntityId h : ids)
            for (EntityId a : archers) nearest = std::min(nearest, (w.findUnit(h)->pos - w.findUnit(a)->pos).flat().length().toDouble());
    }
    std::printf("  thrown at the swordsman %d, the wizard %d; nearest an archer %.1f m (their reach %.0f m)\n", atSwordsman, atWizard, nearest,
                bow.ranged.autoRange.toDouble());
    CHECK(atSwordsman > 0 && atWizard == 0);
    CHECK(nearest > bow.ranged.autoRange.toDouble());
}

TEST(a_retreat_keeps_one_place) {
    // After a throw at a man beside a squad of heavies, the hyena falls back
    // to one place and runs straight there: no stutter, no turning back.
    World w(WorldConfig{});
    lay(w, RemainKind::Arm, 1, 0);
    const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    UnitStats sword = standing(UnitType::LightInfantry);
    sword.melee.damage = 0;
    const EntityId man = w.spawnUnit(1, {m(20), m(0), m(0)}, kWest, sword);
    order(w, {hyena}, Command::Type::Raid);
    bool retreating = false;
    int places = 0, ticks = 0, reversals = 0;
    Vec3 place, last, lastStep;
    for (int i = 0; i < 12 * kTicksPerSecond; ++i) {
        w.step({});
        const Unit& h = *w.findUnit(hyena);
        const bool now = h.scav.mind == Scavenger::Mind::Retreat;
        if (now && !retreating) {
            ++places;
            place = h.scav.goal;
            last = h.pos;
            lastStep = {};
        } else if (now) {
            ++ticks;
            if (!(h.scav.goal == place)) {
                ++places;
                place = h.scav.goal;
            }
            const Vec3 stepNow = (h.pos - last).flat();
            if (stepNow.dot(lastStep) < Fixed::zero()) ++reversals;
            if (stepNow.lengthSq() > Fixed::zero()) lastStep = stepNow;
            last = h.pos;
        } else if (retreating) {
            break;  // the first retreat is done
        }
        retreating = now;
    }
    std::printf("  fell back %d ticks to %d place(s), %d reversal(s); the man %d of %d hp\n", ticks, places, reversals, w.findUnit(man)->hp,
                w.findUnit(man)->stats.maxHp);
    CHECK(places == 1);
    CHECK(ticks > kTicksPerSecond);
    CHECK(reversals == 0);
}

TEST(heavies_turn_their_shields_to_a_thrower) {
    // Heavy infantry standing with their backs to a hyena ordered to throw at
    // one of them: the struck man turns his shield to the thrower, none goes
    // chasing what he cannot catch, and his shield takes some of the throws.
    World w(WorldConfig{});
    const EntityId hyena = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::Hyena));
    std::vector<EntityId> squad;
    for (int i = 0; i < 4; ++i) squad.push_back(w.spawnUnit(1, {m(22 + (i % 2) * 2), m(i / 2 * 2 - 1), m(0)}, kEast, unitStats(UnitType::HeavyInfantry)));
    const EntityId mark = squad[0];
    std::vector<Vec3> places;
    for (EntityId id : squad) places.push_back(w.findUnit(id)->pos);
    int thrown = 0, blocked = 0, struck = 0;
    double turned = -1, wander = 0;
    for (int round = 0; round < 8; ++round) {
        // An arm at his feet (a head would break whom it struck), taken up raiding, then the order to throw it at him.
        const Vec3 at = w.findUnit(hyena)->pos;
        lay(w, RemainKind::Arm, at.x.floorToInt() + 1, at.y.floorToInt());
        order(w, {hyena}, Command::Type::Raid);
        for (int i = 0; i < 3 * kTicksPerSecond && !w.findUnit(hyena)->scav.holding(); ++i) w.step({});
        if (!w.findUnit(hyena)->scav.holding()) break;
        Command c{0, hyena, Command::Type::AttackUnit, w.findUnit(mark)->pos};
        c.targetUnit = mark;
        w.step(std::vector<Command>{c});
        for (int i = 0; i < 4 * kTicksPerSecond; ++i) {
            w.step({});
            if (const Projectile* p = thrownBy(w, hyena); p && p->ageTicks == 1) ++thrown;
            for (const ImpactEvent& e : w.impacts()) {
                if (e.hitUnit != mark || e.projectile == kNoEntity) continue;
                blocked += e.kind == ImpactEvent::Kind::Blocked;
                struck += e.kind == ImpactEvent::Kind::Hit;
            }
            for (size_t k = 0; k < squad.size(); ++k) wander = std::max(wander, (w.findUnit(squad[k])->pos - places[k]).flat().length().toDouble());
        }
        if (round == 0) {
            const Unit& k = *w.findUnit(mark);
            turned = k.facing.dot((w.findUnit(hyena)->pos - k.pos).flat() / (w.findUnit(hyena)->pos - k.pos).flat().length()).toDouble();
        }
    }
    std::printf("  thrown %d: blocked %d, struck %d; after the first, the man faced the thrower %.2f; the squad strayed %.2f m\n", thrown, blocked,
                struck, turned, wander);
    CHECK(thrown >= 6);
    CHECK(turned > 0.9);
    CHECK(blocked > 0);
    CHECK(wander < 1.0);
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
    constexpr uint64_t kGolden = 0xb1f804967fd1aac0;
    if (a != kGolden) std::printf("  Raid checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}
