// Orders (world_orders.cpp, battle_orders.cpp): going after one soldier,
// force attacks, marching with initiative on a leash, the force march, the
// choice after a kill, the V, and the markers that show where they go.

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/camera.h"
#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;
using illyria::game::Battle;
using illyria::game::Formation;
using illyria::game::FormationSlot;
using illyria::game::V3;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
Vec3 at(int64_t x, int64_t y) { return {m(x), m(y), Fixed::zero()}; }

// Cannot strike back and will not come at anyone.
UnitStats harmless(UnitType type, int32_t hp) {
    UnitStats s = unitStats(type);
    s.melee.damage = 0;
    s.ranged = {};
    s.aggroRange = Fixed::zero();
    s.shield.blockChance = Fixed::zero();
    s.maxHp = hp;
    return s;
}

void steps(World& w, int ticks) {
    for (int i = 0; i < ticks; ++i) w.step({});
}
void order(World& w, const Command& c) { w.step(std::vector<Command>{c}); }

Command move(PlayerId p, EntityId id, const Vec3& to, bool force = false) {
    Command c{p, id, Command::Type::Move, to};
    c.force = force;
    return c;
}
Command attack(PlayerId p, EntityId id, EntityId target, EntityId rally = kNoEntity) {
    Command c{p, id, Command::Type::AttackUnit, {}};
    c.targetUnit = target;
    c.rally = rally;
    return c;
}

int squadNamed(const Battle& b, const char* name) {
    for (size_t i = 0; i < b.squads().size(); ++i)
        if (b.squads()[i].name == name) return static_cast<int>(i);
    return -1;
}
void run(Battle& b, double seconds) {
    for (double t = 0; t < seconds; t += 0.1) b.advance(0.1);
}
}  // namespace

TEST(attack_unit_runs_down_a_moving_target_and_kills_it) {
    World w(WorldConfig{});
    const EntityId hunter = w.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::Berserker));
    const EntityId prey = w.spawnUnit(1, at(20, 0), kEast, harmless(UnitType::LightInfantry, 60));
    // The prey walks off north-east; the berserker is told to get him.
    w.step(std::vector<Command>{move(1, prey, at(70, 40)), attack(0, hunter, prey)});
    int t = 0;
    while (w.findUnit(prey)->alive() && t < 60 * kTicksPerSecond) w.step({}), ++t;
    std::printf("  prey down after %.1f s, %.0f m from the start\n", t / 30.0,
                (w.findUnit(prey)->pos - at(20, 0)).flat().length().toDouble());
    CHECK(!w.findUnit(prey)->alive());
    CHECK(w.findUnit(hunter)->kills == 1);
    CHECK((w.findUnit(prey)->pos - at(20, 0)).flat().length() > m(5));  // he was caught on the move

    // Missile troops keep to a bowshot and shoot him down (a big target: a
    // few recruits' arrows mostly miss a man at 60 m).
    World v(WorldConfig{});
    const EntityId bow = v.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::Archers));
    const EntityId bow2 = v.spawnUnit(0, at(0, 3), kEast, unitStats(UnitType::Archers));
    const EntityId bow3 = v.spawnUnit(0, at(0, -3), kEast, unitStats(UnitType::Archers));
    const EntityId far = v.spawnUnit(1, at(110, 0), kEast, harmless(UnitType::Elephants, 60));
    v.step(std::vector<Command>{move(1, far, at(140, 30)), attack(0, bow, far), attack(0, bow2, far), attack(0, bow3, far)});
    Fixed nearest = m(1000);
    for (int i = 0; i < 90 * kTicksPerSecond && v.findUnit(far)->alive(); ++i) {
        v.step({});
        nearest = min(nearest, (v.findUnit(far)->pos - v.findUnit(bow)->pos).flat().length());
    }
    CHECK(!v.findUnit(far)->alive());
    CHECK(nearest > m(40));  // he never needed to close to sword's reach
    CHECK(v.findUnit(bow)->pos.x > m(20));  // but closed to range
}

TEST(a_force_attack_on_a_friend_only_when_ordered) {
    World w(WorldConfig{});
    const EntityId a = w.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::Berserker));
    const EntityId b = w.spawnUnit(0, at(2, 0), -kEast, unitStats(UnitType::LightInfantry));
    steps(w, 10 * kTicksPerSecond);
    CHECK(w.findUnit(b)->hp == w.findUnit(b)->stats.maxHp);  // friends side by side leave each other be
    order(w, attack(0, a, b));
    steps(w, 10 * kTicksPerSecond);
    CHECK(w.findUnit(b)->hp < w.findUnit(b)->stats.maxHp);
    CHECK(w.findUnit(a)->hp == w.findUnit(a)->stats.maxHp);  // and the friend does not answer
    // A kill of one's own earns no rank.
    CHECK(w.findUnit(a)->kills == 0);
    // Nobody may order the other side's men about.
    World x(WorldConfig{});
    const EntityId p = x.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::Berserker));
    const EntityId q = x.spawnUnit(0, at(2, 0), -kEast, unitStats(UnitType::LightInfantry));
    order(x, attack(1, p, q));
    steps(x, 5 * kTicksPerSecond);
    CHECK(x.findUnit(q)->hp == x.findUnit(q)->stats.maxHp);
}

TEST(marching_soldiers_take_on_crossers_then_march_on) {
    // An enemy within reach of the road is engaged and cut down, then the march goes on.
    const auto march = [](int64_t offset, bool force, EntityId* engagedWith) {
        World w(WorldConfig{});
        const EntityId walker = w.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::HeavyInfantry));
        const EntityId foe = w.spawnUnit(1, at(30, offset), -kEast, harmless(UnitType::LightInfantry, 40));
        order(w, move(0, walker, at(80, 0), force));
        *engagedWith = kNoEntity;
        int t = 0;
        while (w.findUnit(walker)->hasMoveTarget && t < 90 * kTicksPerSecond) {
            w.step({});
            ++t;
            if (w.findUnit(walker)->engageFoe != kNoEntity) *engagedWith = w.findUnit(walker)->engageFoe;
        }
        CHECK(!w.findUnit(walker)->hasMoveTarget);
        CHECK((w.findUnit(walker)->pos - at(80, 0)).flat().length() < m(2));  // he got there
        return w.findUnit(foe)->alive();
    };
    EntityId with = kNoEntity;
    CHECK(!march(3, false, &with));  // 3 m off the road: killed on the way
    CHECK(with == 2);
    CHECK(march(14, false, &with));  // 14 m off: not worth leaving the road for
    CHECK(with == kNoEntity);
    CHECK(march(3, true, &with));  // a force march ignores him
    CHECK(with == kNoEntity);
}

TEST(marching_soldiers_fight_back_when_attacked) {
    World w(WorldConfig{});
    const EntityId walker = w.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::Giants));
    // A berserker waits beside the road and goes for him as he passes.
    UnitStats bers = unitStats(UnitType::Berserker);
    bers.aggroRange = m(25);
    const EntityId foe = w.spawnUnit(1, at(40, 12), -kEast, bers);
    order(w, move(0, walker, at(100, 0)));
    bool answered = false;
    for (int t = 0; t < 60 * kTicksPerSecond && w.findUnit(foe)->alive(); ++t) {
        w.step({});
        answered = answered || w.findUnit(walker)->engageFoe == foe;
    }
    CHECK(answered);
    CHECK(!w.findUnit(foe)->alive());
    // Then on to where he was going.
    steps(w, 40 * kTicksPerSecond);
    CHECK((w.findUnit(walker)->pos - at(100, 0)).flat().length() < m(2));
    // Forced, he takes the blows and marches on.
    World f(WorldConfig{});
    const EntityId g = f.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::Giants));
    const EntityId b = f.spawnUnit(1, at(40, 12), -kEast, bers);
    order(f, move(0, g, at(100, 0), true));
    steps(f, 30 * kTicksPerSecond);
    CHECK(f.findUnit(b)->hp == f.findUnit(b)->stats.maxHp);
}

TEST(the_march_keeps_its_leash_and_lets_runners_go) {
    // He engages a crosser; the crosser runs off sideways (ordered, or broken):
    // he follows no farther than the leash, and marches on.
    for (int routed = 0; routed < 2; ++routed) {
        World w(WorldConfig{});
        const EntityId walker = w.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::LightCavalry));
        const EntityId foe = w.spawnUnit(1, at(40, 3), -kEast, harmless(UnitType::WolfRiders, 5000));
        order(w, move(0, walker, at(120, 0)));
        int t = 0;
        while (w.findUnit(walker)->engageFoe != foe && t < 30 * kTicksPerSecond) w.step({}), ++t;
        CHECK(w.findUnit(walker)->engageFoe == foe);
        if (routed) {
            w.shakeMorale(*const_cast<Unit*>(w.findUnit(foe)), 5000);
            steps(w, 1);
        } else {
            order(w, move(1, foe, at(40, 90), true));
        }
        Fixed widest;
        int engagedTicks = 0;
        for (int i = 0; i < 60 * kTicksPerSecond && w.findUnit(walker)->hasMoveTarget; ++i) {
            w.step({});
            widest = max(widest, abs(w.findUnit(walker)->pos.y));
            engagedTicks += w.findUnit(walker)->engageFoe == foe;
        }
        std::printf("  %s: strayed %.1f m from the road, %d ticks after him\n", routed ? "broken foe" : "runner", widest.toDouble(),
                    engagedTicks);
        CHECK(widest <= m(17));
        CHECK(w.findUnit(foe)->alive());
        CHECK((w.findUnit(walker)->pos - at(120, 0)).flat().length() < m(2));
        if (routed) CHECK(engagedTicks < 3 * kTicksPerSecond);
    }
}

TEST(after_a_kill_veterans_choose_the_better_odds_more_often) {
    // His man falls. Beside it stands either a demon (press on: hopeless) or a
    // lone wounded archer (press on: an easy kill); his friend waits 30 m back (his place 20 m back).
    // Over many seeds veterans make the better choice more often than recruits.
    const auto trial = [](uint64_t seed, int rank, bool demon) {
        WorldConfig cfg;
        cfg.seed = seed;
        World w(cfg);
        const EntityId me = w.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::LightInfantry));
        const EntityId friendly = w.spawnUnit(0, at(-30, 0), kEast, harmless(UnitType::LightInfantry, 60));
        const EntityId victim = w.spawnUnit(1, at(1, 0), -kEast, harmless(UnitType::LightInfantry, 1));
        UnitStats next = demon ? unitStats(UnitType::Demons) : unitStats(UnitType::Archers);
        next.aggroRange = Fixed::zero();
        next.ranged = {};
        if (!demon) next.maxHp = 10;
        w.spawnUnit(1, at(6, 4), -kEast, next);
        w.setVeterancy(me, rank, 0);
        order(w, attack(0, me, victim, friendly));
        for (int t = 0; t < 10 * kTicksPerSecond; ++t) {
            w.step({});
            for (const OrderDecision& d : w.orderDecisions())
                if (d.unit == me) return std::pair<bool, int32_t>{d.pressOn, d.odds};
        }
        return std::pair<bool, int32_t>{false, -1};
    };
    const int kSeeds = 200;
    int good[2] = {0, 0}, pressedDemon[2] = {0, 0};
    int32_t oddsDemon = 0, oddsArcher = 0;
    for (int vet = 0; vet < 2; ++vet) {
        for (uint64_t s = 1; s <= kSeeds; ++s) {
            const auto [vsDemon, od] = trial(s, vet ? 3 : 0, true);
            const auto [vsArcher, oa] = trial(s + 1000, vet ? 3 : 0, false);
            CHECK(od >= 0 && oa >= 0);
            oddsDemon = od, oddsArcher = oa;
            good[vet] += !vsDemon;
            good[vet] += vsArcher;
            pressedDemon[vet] += vsDemon;
        }
    }
    std::printf("  odds: demon %d, archer %d per mille; better choices: recruits %d, elites %d of %d; at the demon: %d, %d\n",
                oddsDemon, oddsArcher, good[0], good[1], 2 * kSeeds, pressedDemon[0], pressedDemon[1]);
    CHECK(oddsDemon < 300 && oddsArcher > 700);
    CHECK(good[1] > good[0]);
    CHECK(pressedDemon[0] > 0);  // some green men think one on one with a demon is a fine idea
    CHECK(pressedDemon[1] == 0);  // elites never do
    // Deterministic: the same seed, the same choice.
    CHECK(trial(7, 0, true) == trial(7, 0, true));
}

TEST(going_back_takes_him_to_his_old_place_beside_his_friend) {
    WorldConfig cfg;
    cfg.seed = 3;
    World w(cfg);
    const EntityId me = w.spawnUnit(0, at(0, 0), kEast, unitStats(UnitType::LightCavalry));
    const EntityId friendly = w.spawnUnit(0, at(-4, 3), kEast, harmless(UnitType::LightInfantry, 60));
    const EntityId victim = w.spawnUnit(1, at(30, 10), -kEast, harmless(UnitType::LightInfantry, 30));
    order(w, attack(0, me, victim, friendly));
    steps(w, 40 * kTicksPerSecond);
    CHECK(!w.findUnit(victim)->alive());
    CHECK(w.findUnit(me)->targetUnit == kNoEntity);
    // Nobody else to fight: back beside his friend where he stood.
    CHECK((w.findUnit(me)->pos - at(0, 0)).flat().length() < m(2));
}

TEST(attacks_on_gates_walls_and_scenery_go_to_those_who_can_harm_them) {
    CHECK(World::canBatter(unitStats(UnitType::Catapult)));
    CHECK(World::canBatter(unitStats(UnitType::DwarfGrenadier)));
    CHECK(World::canBatter(unitStats(UnitType::Wizards)));
    CHECK(!World::canBatter(unitStats(UnitType::Archers)));
    CHECK(!World::canBatter(unitStats(UnitType::HeavyInfantry)));
}

TEST(the_v_has_its_point_at_the_back_and_its_arms_forward) {
    const float spacing = 2.0f;
    const std::vector<FormationSlot> v = illyria::game::formationLayout(Formation::Vee, 9, spacing);
    const std::vector<FormationSlot> wedge = illyria::game::formationLayout(Formation::Wedge, 9, spacing);
    CHECK(v.size() == 9);
    const auto back = [](const std::vector<FormationSlot>& s) {
        return std::min_element(s.begin(), s.end(), [](const FormationSlot& a, const FormationSlot& b) { return a.pos.y < b.pos.y; });
    };
    const auto front = [](const std::vector<FormationSlot>& s) {
        return std::max_element(s.begin(), s.end(), [](const FormationSlot& a, const FormationSlot& b) { return a.pos.y < b.pos.y; });
    };
    // One man alone at the back, in the middle; the arms widen as they go forward.
    CHECK_NEAR(back(v)->pos.x, 0.0, 1e-4);
    int atBack = 0;
    for (const FormationSlot& s : v) atBack += s.pos.y < back(v)->pos.y + 0.01f;
    CHECK(atBack == 1);
    for (const FormationSlot& s : v) CHECK(std::abs(s.pos.x) - (s.pos.y - back(v)->pos.y) < 0.01f);
    CHECK(std::abs(front(v)->pos.x) > spacing * 2);  // the front is the arms' tips, wide apart
    // The wedge is the other way round: its lone man is at the front.
    CHECK_NEAR(front(wedge)->pos.x, 0.0, 1e-4);
    float minGap = 1e9f;
    for (size_t i = 0; i < v.size(); ++i)
        for (size_t j = i + 1; j < v.size(); ++j) minGap = std::min(minGap, (v[i].pos - v[j].pos).length());
    CHECK(minGap >= spacing * 0.99f);
    CHECK(std::string(illyria::game::formationName(Formation::Vee)) == "V");
    CHECK(illyria::game::kFormationOrder[illyria::game::kFormationCount - 1] == Formation::Vee);
}

TEST(the_destination_markers_are_where_they_end_up) {
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    b.setFormation(Formation::Vee);
    run(b, 1);
    const V3 to = b.squadCentre(b.squads()[static_cast<size_t>(watch)], false) + V3{-25, 6, 0};
    b.orderMove(to);
    b.advance(0.05);
    std::vector<Battle::OrderMark> slots;
    for (const Battle::OrderMark& mk : b.orderMarks())
        if (mk.kind == Battle::OrderMark::Kind::Slot) slots.push_back(mk);
    CHECK(slots.size() == b.selectedAlive().size());
    run(b, 45);
    float worst = 0;
    for (const Battle::OrderMark& mk : slots) {
        const V3 p = illyria::game::toV3(b.world().units()[mk.unit - 1].pos);
        worst = std::max(worst, V3{p.x - mk.pos.x, p.y - mk.pos.y, 0}.length());
        CHECK(illyria::game::toV3(b.world().units()[mk.unit - 1].facing).dot(mk.face) > 0.95f);
    }
    std::printf("  %zu soldiers, at most %.2f m from their markers\n", slots.size(), worst);
    CHECK(worst < 1.0f);
    // Arrived: the markers are gone.
    for (const Battle::OrderMark& mk : b.orderMarks()) CHECK(mk.kind != Battle::OrderMark::Kind::Slot);
    // Shown while selected, and for a moment after an order when not.
    b.orderMove(to + V3{-25, 0, 0});
    b.clearSelection();
    b.advance(0.05);
    CHECK(!b.orderMarks().empty());
    run(b, 4);
    bool any = false;
    for (const Battle::OrderMark& mk : b.orderMarks()) any = any || mk.kind == Battle::OrderMark::Kind::Slot;
    CHECK(!any);
    b.selectSquad(watch);
    for (const Battle::OrderMark& mk : b.orderMarks()) any = any || mk.kind == Battle::OrderMark::Kind::Slot;
    CHECK(any);
}

TEST(tapping_an_enemy_sends_the_selection_after_him) {
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    // The enemy soldier nearest them.
    const V3 c = b.squadCentre(b.squads()[static_cast<size_t>(watch)], false);
    EntityId foe = kNoEntity;
    float best = 1e9f;
    for (const Unit& u : b.world().units()) {
        if (u.owner != Battle::kEnemy || !u.alive()) continue;
        const float d = (illyria::game::toV3(u.pos) - c).length();
        if (d < best) best = d, foe = u.id;
    }
    illyria::game::Camera cam;
    cam.setBounds(Battle::kWorldSize);
    cam.setViewport(800, 600);
    cam.setHeightFn([&b](float x, float y) { return b.heightAt(x, y); });
    const V3 fp = b.unitPosition(foe - 1);
    cam.lookAt(fp.x, fp.y, 60);
    float px, py;
    CHECK(cam.project(fp + V3{0, 0, 0.9f}, px, py));
    CHECK(b.intentAt(cam, px, py, 1.0f) == Battle::Intent::Attack);
    b.tap(cam, px, py, 1.0f);
    b.advance(0.05);
    for (EntityId id : b.selectedAlive()) CHECK(b.world().units()[id - 1].targetUnit == foe);
    int rings = 0;
    for (const Battle::OrderMark& mk : b.orderMarks()) rings += mk.kind == Battle::OrderMark::Kind::Target;
    CHECK(rings == static_cast<int>(b.selectedAlive().size()));
    // Attack mode: the ground is attack-ground, and it lasts one order.
    b.setOrderMode(Battle::OrderMode::Attack);
    V3 ground;
    CHECK(cam.pickGround(400, 560, ground));
    CHECK(b.intentAt(cam, 400, 560, 1.0f) == Battle::Intent::AttackGround);
    b.tap(cam, 400, 560, 1.0f);
    CHECK(b.orderMode() == Battle::OrderMode::Normal);
    // Ctrl over one of ours: a force attack.
    b.setKeys(true, false);
    const EntityId other = b.squads()[static_cast<size_t>(squadNamed(b, "Ferry Pikes"))].members.front();
    const V3 op = b.unitPosition(other - 1);
    cam.lookAt(op.x, op.y, 60);
    CHECK(cam.project(op + V3{0, 0, 0.9f}, px, py));
    CHECK(b.intentAt(cam, px, py, 1.0f) == Battle::Intent::Attack);
    b.setKeys(false, false);
    CHECK(b.intentAt(cam, px, py, 1.0f) == Battle::Intent::Select);
}

TEST(orders_replay_identically) {
    const auto play = [](uint64_t seed) {
        WorldConfig cfg;
        cfg.seed = seed;
        World w(cfg);
        std::vector<EntityId> red, blue;
        for (int i = 0; i < 6; ++i) {
            blue.push_back(w.spawnUnit(0, at(0, i * 3), kEast, unitStats(i % 2 ? UnitType::LightCavalry : UnitType::LightInfantry)));
            red.push_back(w.spawnUnit(1, at(50, i * 3 + 10), -kEast, unitStats(i % 2 ? UnitType::Archers : UnitType::Warrior)));
        }
        std::vector<Command> cmds;
        for (int i = 0; i < 6; ++i) {
            if (i < 2) cmds.push_back(attack(0, blue[static_cast<size_t>(i)], red[5], blue[5]));
            else cmds.push_back(move(0, blue[static_cast<size_t>(i)], at(80, i * 3), i == 4));
            cmds.push_back(move(1, red[static_cast<size_t>(i)], at(-20, i * 3)));
        }
        w.step(cmds);
        steps(w, 40 * kTicksPerSecond);
        return w.checksum();
    };
    CHECK(play(11) == play(11));
    CHECK(play(11) != play(12));
}
