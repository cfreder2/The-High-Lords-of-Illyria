// A garrison at its stations (engine/sim/src/world_keep.cpp, keepGates) in a capital of the campaign: the player's idle men take
// their fighting stations when a foe comes near, as a computer lord's do; a man with an order of his own is never pulled to a post
// (released when ordered, a garrison man again when he is idle); the lord keeps his place behind the foot; and the player's gate
// is still his hand's: his men bound through it go through.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "illyria/sim/campaign.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
constexpr int kTps = static_cast<int>(kTicksPerSecond);

// A capital of `side` (side 0 is the player's, human; the others are computer lords), the stack that stands in it.
struct Case {
    Campaign c;
    int32_t town = -1;
    size_t gate = World::kNoGate;
    std::vector<EntityId> ids;
    Vec3 mid, out;
    World* w = nullptr;
    explicit Case(uint8_t side) : c(continent(), 5, Campaign::Setup{0, 0xFF}) {
        c.setHuman(0, true);
        c.prepareWorld();
        for (size_t i = 0; i < c.cities().size(); ++i)
            if (c.cities()[i].owner == side && continent().cities()[i].capital) town = static_cast<int32_t>(i);
        w = c.presentedWorld();
        gate = w->gateOfTown(continent().cities()[static_cast<size_t>(town)].pos - w->region()->origin());
        const Gate& g = w->region()->gates()[gate];
        mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
        out = g.out;
        for (const Campaign::Stack& st : c.stacks())
            if (st.owner == side && c.cityAt(st.cx, st.cy) == town && st.path.empty())
                for (const Army& a : st.armies)
                    for (const Man& m : a.men)
                        if (m.unit) ids.push_back(m.unit);
    }
    const Rampart& ring() const { return w->region()->ramparts()[static_cast<size_t>(w->region()->gates()[gate].ring)]; }
    const Unit& unit(EntityId id) const { return *w->findUnit(id); }
    void run(int seconds) {
        for (int t = 0; t < seconds * kTps; ++t) w->step({});
    }
    // Four giants at 60 m from the doors, outside.
    void foe() {
        for (int i = 0; i < 4; ++i) {
            Vec3 p = mid + out * Fixed::fromInt(60) + Vec3{Fixed::fromInt(i * 3 - 4), Fixed::zero(), Fixed::zero()};
            p.z = w->groundHeight(p.x, p.y);
            w->spawnUnit(1, p, -out, unitStats(UnitType::Giants));
        }
    }
    std::vector<EntityId> grounded(World::GarrisonRole role) const {
        std::vector<EntityId> r;
        for (const EntityId id : ids) {
            const Unit& u = unit(id);
            if (u.alive() && u.stats.flyAltitude == Fixed::zero() && World::garrisonRole(u.stats) == role) r.push_back(id);
        }
        return r;
    }
    // A short walk toward the doors from where he stands now (open ground: the scattered men stand in the yard).
    Vec3 step(EntityId id, int metres) const { return unit(id).pos + out * Fixed::fromInt(metres); }
    Vec3 spot(EntityId id, int depth) const {
        return mid - out * Fixed::fromInt(depth) + Vec3{Fixed::fromInt(static_cast<int64_t>(id % 9)), Fixed::zero(), Fixed::zero()};
    }
    // Everyone sent off across the courtyard by the player (an order each), then left to stand there.
    void scatter() {
        std::vector<Command> away;
        for (const EntityId id : ids)
            if (unit(id).stats.flyAltitude == Fixed::zero()) away.push_back({0, id, Command::Type::Move, spot(id, 40)});
        w->step(away);
        run(120);
    }
    bool atEmbrasure(const Unit& u) const {
        for (const Rampart::Embrasure& e : ring().embrasures)
            if ((u.pos - e.pos).flat().length() < Fixed::fromRatio(6, 5) && abs(u.pos.z - e.pos.z) < Fixed::fromRatio(3, 2)) return true;
        return false;
    }
    double depthOf(const Unit& u) const { return (mid - u.pos).flat().dot(out).toDouble(); }
};

double moved(const Vec3& a, const Vec3& b) { return (a - b).flat().length().toDouble(); }
}  // namespace

TEST(an_idle_human_garrison_takes_its_stations_when_a_foe_is_at_60_m) {
    Case k(0);
    CHECK(k.gate != World::kNoGate);
    CHECK(k.w->gates()[k.gate].garrisoned);
    k.scatter();
    // The rest (healers, casters, engines): one of each, standing behind the foot, nothing like a station.
    std::vector<EntityId> rest;
    for (const UnitType t : {UnitType::Journeyman, UnitType::Wizards, UnitType::Catapult}) {
        Vec3 p = k.mid - k.out * Fixed::fromInt(30) + Vec3{Fixed::fromInt(static_cast<int>(rest.size()) * 4 - 4), Fixed::zero(), Fixed::zero()};
        p.z = k.w->groundHeight(p.x, p.y);
        rest.push_back(k.w->spawnUnit(0, p, -k.out, unitStats(t)));
    }
    const auto wall = k.grounded(World::GarrisonRole::Wall), foot = k.grounded(World::GarrisonRole::Foot);
    CHECK(wall.size() >= 12 && foot.size() >= 24);
    for (const EntityId id : wall) CHECK(!k.atEmbrasure(k.unit(id)));  // (they were sent down off the walk)
    std::vector<Vec3> footBefore;
    for (const EntityId id : foot) footBefore.push_back(k.unit(id).pos);
    k.foe();
    int given = 0;
    for (int t = 0; t < 150 * kTps; ++t) {
        k.w->step({});
        // (a crowd may nudge them; the gate's stations never give them a move)
        for (const EntityId id : rest) given += k.unit(id).stationed || k.unit(id).hasMoveTarget;
    }
    int manned = 0, formed = 0, forward = 0;
    for (const EntityId id : wall) manned += k.atEmbrasure(k.unit(id));
    for (size_t i = 0; i < foot.size(); ++i) {
        const Unit& u = k.unit(foot[i]);
        if (!u.alive()) continue;
        const double d = k.depthOf(u);
        formed += d >= 8.0 && d <= 34.0 && Gate::inside(k.ring(), u.pos) && u.facing.flat().dot(k.out) > Fixed::zero();
        forward += moved(u.pos, footBefore[i]) > 3.0;
    }
    std::printf("  %d of %zu bowmen at embrasures, %d of %zu foot in the horseshoe (%d moved)\n", manned, wall.size(), formed, foot.size(), forward);
    CHECK(manned * 10 >= static_cast<int>(wall.size()) * 9);
    CHECK(formed * 10 >= static_cast<int>(foot.size()) * 9);
    CHECK(forward * 2 >= static_cast<int>(foot.size()));
    CHECK(given == 0);  // healers, casters and engines were never sent anywhere
}

TEST(the_same_men_with_an_order_are_not_pulled_to_posts_and_go_back_when_idle) {
    Case k(0);
    k.scatter();
    const auto wall = k.grounded(World::GarrisonRole::Wall);
    // Ordered a short way across the courtyard: each has a walk to make.
    std::vector<Command> orders;
    for (const EntityId id : k.ids)
        if (k.unit(id).stats.flyAltitude == Fixed::zero()) orders.push_back({0, id, Command::Type::Move, k.step(id, 4)});
    k.foe();
    k.w->step(orders);
    for (const EntityId id : k.ids) CHECK(!k.unit(id).stationed);  // the orders are the player's: nobody is stationed yet
    std::vector<Vec3> goal;
    for (const EntityId id : k.ids) goal.push_back(k.step(id, 4));
    std::vector<uint8_t> arrived(k.ids.size(), 0);
    int stationedEarly = 0, steered = 0, walking = 0;
    for (int t = 0; t < 40 * kTps; ++t) {
        k.w->step({});
        for (size_t i = 0; i < k.ids.size(); ++i) {
            const Unit& u = k.unit(k.ids[i]);
            if (!u.alive() || u.stats.flyAltitude > Fixed::zero()) continue;
            if (!u.hasMoveTarget) arrived[i] = 1;
            if (u.stationed && !arrived[i]) ++stationedEarly;  // taken for a post before his own walk was done
            if (u.hasMoveTarget && !u.stationed) ++walking, steered += (u.moveTarget - goal[i]).flat().length() > Fixed::fromInt(2);
        }
    }
    std::printf("  %d man-ticks walking under orders, %d steered elsewhere, %d taken early\n", walking, steered, stationedEarly);
    CHECK(walking > 0);
    CHECK(steered == 0);
    CHECK(stationedEarly == 0);
    // Idle again, they are the garrison's: back at the stations (the bowmen on the walk; the stair takes them a while).
    k.run(200);
    int manned = 0;
    for (const EntityId id : wall) manned += k.atEmbrasure(k.unit(id));
    int foes = 0, ord = 0;
    for (const Unit& u : k.w->units()) foes += u.owner == 1 && u.alive() && (u.pos - k.mid).flat().length() < Fixed::fromInt(70);
    for (const EntityId id : wall) ord += k.unit(id).hasMoveTarget;
    std::printf("  %d of %zu bowmen back at embrasures; %d foes near, %d bowmen still walking\n", manned, wall.size(), foes, ord);
    CHECK(manned * 10 >= static_cast<int>(wall.size()) * 9);
}

TEST(a_man_with_orders_in_hand_is_left_alone_while_his_idle_neighbours_are_stationed) {
    Case k(0);
    k.scatter();
    const auto foot = k.grounded(World::GarrisonRole::Foot);
    CHECK(foot.size() >= 24);
    // Half the foot are ordered on a walk with a second leg waiting; the others are idle.
    std::vector<Command> orders;
    std::vector<EntityId> busy, free;
    for (size_t i = 0; i < foot.size(); ++i) (i % 2 ? busy : free).push_back(foot[i]);
    for (const EntityId id : busy) {
        orders.push_back({0, id, Command::Type::Move, k.step(id, 8)});
        Command leg{0, id, Command::Type::Move, k.step(id, 16)};
        leg.queue = true;
        orders.push_back(leg);
    }
    k.foe();
    k.w->step(orders);
    k.run(2);
    int moving = 0, posted = 0;
    for (const EntityId id : busy) moving += k.unit(id).hasMoveTarget && !k.unit(id).stationed;
    for (const EntityId id : free) posted += k.unit(id).stationed;
    CHECK(moving * 10 >= static_cast<int>(busy.size()) * 9);
    CHECK(posted * 2 >= static_cast<int>(free.size()));
}

TEST(a_lord_stays_behind_the_foot_for_a_computer_side_and_the_player) {
    for (const uint8_t side : {1, 0}) {
        Case k(side);
        CHECK(k.gate != World::kNoGate);
        EntityId lord = kNoEntity;
        for (const EntityId id : k.ids)
            if (k.unit(id).stats.type == static_cast<uint8_t>(UnitType::Hero)) lord = id;
        CHECK(lord != kNoEntity);
        if (lord == kNoEntity) continue;
        const Vec3 at = k.unit(lord).pos;
        k.foe();
        k.run(150);
        const Unit& u = k.unit(lord);
        double footDepth = 0;
        for (const EntityId id : k.grounded(World::GarrisonRole::Foot)) footDepth = std::max(footDepth, k.depthOf(k.unit(id)));
        std::printf("  side %d: lord moved %.2f m, at depth %.1f, foot to %.1f\n", side, moved(u.pos, at), k.depthOf(u), footDepth);
        CHECK(u.alive());
        CHECK(moved(u.pos, at) < 0.5);  // not called into the horseshoe
        CHECK(k.depthOf(u) > footDepth);  // behind the foot
    }
}

TEST(the_players_men_bound_through_his_own_gate_go_through_it_with_a_foe_about) {
    for (const bool foe : {false, true}) {
        Case k(0);
        k.scatter();
        if (foe) k.foe();
        // A footman in the courtyard sent out through the doors to a spot 20 m outside them.
        EntityId man = kNoEntity;
        for (const EntityId id : k.grounded(World::GarrisonRole::Foot)) man = id;
        CHECK(man != kNoEntity);
        Vec3 to = k.mid + k.out * Fixed::fromInt(20);
        to.z = k.w->groundHeight(to.x, to.y);
        bool opened = false;
        double best = -1e9;
        const std::vector<Command> go = {{0, man, Command::Type::Move, to}};
        k.w->step(go);
        for (int t = 0; t < 60 * kTps; ++t) {
            k.w->step({});
            opened = opened || k.w->gates()[k.gate].mode != GateState::Mode::Shut;
            best = std::max(best, -k.depthOf(k.unit(man)));
        }
        std::printf("  foe %d: gate %s, furthest out %.1f m\n", foe, opened ? "swung open" : "stayed shut", best);
        CHECK(opened);
        CHECK(best > 3.0);  // through the doors and beyond them
    }
}

TEST(the_same_stationing_run_twice_has_the_same_checksum) {
    uint64_t sum[2] = {0, 0};
    for (int run = 0; run < 2; ++run) {
        Case k(0);
        k.scatter();
        std::vector<Command> some;
        for (size_t i = 0; i < k.ids.size(); i += 3)
            if (k.unit(k.ids[i]).stats.flyAltitude == Fixed::zero()) some.push_back({0, k.ids[i], Command::Type::Move, k.spot(k.ids[i], 50)});
        k.foe();
        k.w->step(some);
        k.run(60);
        sum[run] = k.w->checksum();
    }
    CHECK(sum[0] == sum[1]);
}
