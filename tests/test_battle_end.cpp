// How a battle ends (engine/sim/src/mode_rules.cpp: squadSpent, hostBeaten, RoutRule; World::quitField;
// game/src/battle_end.cpp; Conquest::quitFields): a beaten host quits the field and the battle is called, the fight
// stops on the last blow, the results count what happened; and the deployment that holds the clock at the start.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "conquest_orders.h"
#include "illyria/game/battle.h"
#include "illyria/sim/conquest.h"
#include "illyria/sim/mode_rules.h"
#include "test.h"

using namespace illyria::sim;
using illyria::game::Battle;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};

// A squad of `count` men of `owner` in a file at x, from y up, two metres apart.
HostSquad squad(World& w, PlayerId owner, int x, int y, int count, UnitType type = UnitType::LightInfantry) {
    HostSquad s;
    s.owner = owner;
    for (int i = 0; i < count; ++i) s.members.push_back(w.spawnUnit(owner, {m(x), m(y + i * 2), m(0)}, kEast, unitStats(type)));
    return s;
}

void breakMan(World& w, EntityId id) { w.shakeMorale(const_cast<Unit&>(*w.findUnit(id)), 5000); }

// Two hosts of two squads of six, 60 m apart; the eastern one (side 1) broken to a man. The world after `ticks` steps
// under the rule.
struct Rout {
    World w{WorldConfig{}};
    std::vector<HostSquad> squads;
    RoutRule rule;
    Rout() {
        squads = {squad(w, 0, 0, 0, 6), squad(w, 0, 0, 20, 6), squad(w, 1, 60, 0, 6), squad(w, 1, 60, 20, 6)};
        w.step({});
        for (size_t q = 2; q < 4; ++q)
            for (EntityId id : squads[q].members) breakMan(w, id);
    }
    void run(int ticks) {
        for (int t = 0; t < ticks; ++t) {
            w.step({});
            rule.step(w, squads);
        }
    }
};

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

}  // namespace

TEST(battle_end_a_squad_is_spent_when_broken_or_under_a_fifth) {
    World w{WorldConfig{}};
    HostSquad s = squad(w, 0, 0, 0, 10);
    w.step({});
    CHECK(!squadSpent(w, s.members, 10));
    for (int i = 0; i < 8; ++i) w.removeUnit(s.members[static_cast<size_t>(i)]);
    CHECK(!squadSpent(w, s.members, 10));  // two of ten: a fifth, not under it
    w.removeUnit(s.members[8]);
    CHECK(squadSpent(w, s.members, 10));  // one of ten
    // Whole, and every man of it broken.
    HostSquad t = squad(w, 0, 40, 0, 6);
    for (EntityId id : t.members) breakMan(w, id);
    w.step({});
    CHECK(squadSpent(w, t.members, 6));
    // A squad's full strength is what it was raised with, when it says so (a Conquest army's).
    HostSquad u = squad(w, 0, 80, 0, 3);
    CHECK(!squadSpent(w, u.members, 0) && squadSpent(w, u.members, 16));
}

TEST(battle_end_a_host_with_every_squad_spent_quits_for_its_edge_and_the_battle_is_called) {
    Rout r;
    std::vector<Fixed> before;
    r.run(1);
    CHECK(r.rule.quitter() == 1 && r.w.sideBroken(1) && !r.w.sideBroken(0));
    for (EntityId id : r.squads[2].members) before.push_back(r.w.findUnit(id)->pos.x);
    CHECK(!r.rule.called(r.w));
    r.run(RoutRule::kPursuitTicks - 2);
    CHECK(!r.rule.called(r.w));  // the pursuit: a few seconds of chase
    r.run(2);
    CHECK(r.rule.called(r.w));
    // They ran east, away from the other host, and none rallied or took an order.
    for (size_t i = 0; i < before.size(); ++i) {
        const Unit* u = r.w.findUnit(r.squads[2].members[i]);
        CHECK(u->routed && u->pos.x > before[i] + m(8));
    }
    const Vec3 was = r.w.findUnit(r.squads[2].members[0])->pos;
    const std::vector<Command> order = {Command{1, r.squads[2].members[0], Command::Type::Move, {m(0), m(0), m(0)}}};
    r.w.step(order);
    CHECK(r.w.findUnit(r.squads[2].members[0])->pos.x >= was.x);
    // The same on every machine.
    Rout again;
    again.run(RoutRule::kPursuitTicks + 1);
    Rout third;
    third.run(RoutRule::kPursuitTicks + 1);
    CHECK(again.w.checksum() == third.w.checksum() && again.rule.quitTick() == third.rule.quitTick());
}

TEST(battle_end_a_host_worn_to_a_third_and_outnumbered_quits) {
    World w{WorldConfig{}};
    std::vector<HostSquad> squads = {squad(w, 0, 0, 0, 9), squad(w, 0, 0, 30, 9), squad(w, 1, 300, 0, 6), squad(w, 1, 300, 20, 6), squad(w, 1, 300, 40, 6)};
    w.step({});
    CHECK(!hostBeaten(w, squads, 0) && !hostBeaten(w, squads, 1));
    // Side 1: two men left in each squad of six (no squad spent), six of eighteen: a third, not under it.
    for (size_t q = 2; q < 5; ++q)
        for (size_t i = 2; i < 6; ++i) w.removeUnit(squads[q].members[i]);
    CHECK(!hostBeaten(w, squads, 1));
    w.removeUnit(squads[2].members[1]);  // five of eighteen, eighteen against them
    CHECK(!squadSpent(w, squads[3].members, 6) && hostBeaten(w, squads, 1));
    CHECK(!hostBeaten(w, squads, 0));
    // Not when those against it are as few: seven against five is under three to two.
    for (size_t i = 0; i < 9; ++i) w.removeUnit(squads[0].members[i]);
    for (size_t i = 0; i < 2; ++i) w.removeUnit(squads[1].members[i]);
    CHECK(!hostBeaten(w, squads, 1));
}

TEST(battle_end_engines_alone_hold_no_field) {
    World w{WorldConfig{}};
    std::vector<HostSquad> squads = {squad(w, 0, 0, 0, 6), squad(w, 1, 300, 0, 6), squad(w, 1, 320, 0, 2, UnitType::Catapult)};
    w.step({});
    CHECK(!hostBeaten(w, squads, 1));
    for (EntityId id : squads[1].members) w.removeUnit(id);
    CHECK(hostBeaten(w, squads, 1));  // its foot gone, the engines' crews do not fight on
    CHECK(!hostBeaten(w, squads, 0) && !hostBeaten(w, squads, 7));  // (a side with no squad is no host)
}

TEST(battle_end_the_fight_stops_on_the_last_blow_and_the_result_counts_it) {
    illyria::game::SkirmishSetup setup;
    setup.budget = 100;
    Battle b(42, illyria::game::Scenario::Field, setup);
    // All but one man of the other host taken off the field: every squad of it is spent.
    int removed = 0, mine = 0;
    bool kept = false;
    for (const illyria::game::Squad& q : b.squads()) {
        if (q.owner != Battle::kEnemy) {
            ++mine;
            continue;
        }
        for (EntityId id : q.members) {
            if (!kept && q.members.size() >= 6) {
                kept = true;
                continue;
            }
            b.testRemove(id);
            ++removed;
        }
    }
    CHECK(kept && removed > 0);
    b.advance(0.1);
    CHECK(b.routedSide() == Battle::kEnemy && b.outcome() == 0 && !b.decided());
    for (int k = 0; k < 400 && !b.over(); ++k) b.advance(0.1);
    CHECK(b.over() && b.outcome() == 1);
    const uint32_t stopped = b.world().tick();
    CHECK(stopped == 1 + static_cast<uint32_t>(RoutRule::kPursuitTicks) + Battle::kLastBlowTicks);  // the rout on the first tick, the pursuit, the last blow
    b.advance(1.0);
    CHECK(b.world().tick() == stopped);  // the fight has stopped for good
    const Battle::Result r = b.result();
    CHECK(r.outcome == 1 && r.how == Battle::Result::How::Rout);
    CHECK(r.killed == removed && r.lost == 0 && static_cast<int>(r.rows.size()) == mine);
    CHECK(r.seconds == static_cast<int>((stopped - Battle::kLastBlowTicks) / kTicksPerSecond));
}

TEST(battle_end_a_deployment_holds_the_clock_and_takes_orders) {
    illyria::game::SkirmishSetup setup;
    setup.budget = 100;
    Battle b(42, illyria::game::Scenario::Field, setup);
    b.setDeploying(true);
    for (int k = 0; k < 20; ++k) b.advance(0.1);
    CHECK(b.world().tick() == 0 && b.deploying());
    b.selectSquad(0);
    const illyria::game::V3 c = b.squadCentre(b.squads()[0], false);
    b.orderMove({c.x, c.y + 20, 0});
    CHECK(b.world().tick() == 0);
    b.beginBattle();
    b.advance(0.1);
    CHECK(b.world().tick() > 0);
    CHECK(b.world().findUnit(b.squads()[0].members.front())->hasMoveTarget);  // the order given while it waited
}

TEST(battle_end_a_beaten_stack_in_the_conquest_marches_for_its_nearest_town) {
    Conquest c(continent(), 3);
    for (uint8_t s = 0; s < 8; ++s) c.setHuman(s, true);  // (nobody moves but by our orders)
    const Conquest::Stack* home = nullptr;
    for (const Conquest::Stack& st : c.stacks())
        if (st.owner == 0) home = &st;
    CHECK(home != nullptr);
    if (!home) return;
    const int32_t x = std::min(home->cx + 8, c.width() - 12), y = home->cy;
    const uint32_t mine = c.raise(0, x, y, {UnitType::HeavyInfantry, UnitType::Archers});
    const uint32_t theirs = c.raise(1, x + 4, y, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers});
    ConquestOrders::move(c, mine, x + 3, y);
    ConquestOrders::move(c, theirs, x + 1, y);
    const auto fought = [&] {
        const Conquest::Stack* s = c.findStack(mine);
        if (!s) return true;
        for (const Army& a : s->armies)
            for (const Man& man : a.men)
                if (man.fightTick != 0) return true;
        return false;
    };
    int guard = 0;
    while (!fought() && guard++ < 6000) c.step();
    const Conquest::Stack* s = c.findStack(mine);
    CHECK(s != nullptr && fought());
    if (!s) return;
    CHECK(s->forceUntil <= c.tick());  // fighting, not beaten: it stands
    // Every man of it breaks.
    World& w = const_cast<World&>(*c.world());
    for (const Army& a : s->armies)
        for (const Man& man : a.men)
            if (man.alive() && man.unit) w.shakeMorale(const_cast<Unit&>(*w.findUnit(man.unit)), 5000);
    for (int k = 0; k < 2 * Conquest::kThinkTicks + 2; ++k) c.step();
    s = c.findStack(mine);
    CHECK(s != nullptr);
    if (!s) return;
    CHECK(!s->path.empty() && s->forceUntil > c.tick());
    if (s->path.empty()) return;
    const int32_t town = c.cityAt(s->path.back().first, s->path.back().second);
    CHECK(town >= 0 && c.cities()[static_cast<size_t>(town)].owner == 0);
    std::printf("  the beaten stack marches %zu cells for town %d\n", s->path.size(), town);
}
