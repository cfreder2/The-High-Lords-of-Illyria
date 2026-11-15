// A castle with two doors (a capital's outer ring, a long town's): both doors are in the plan, the garrison is placed across both
// and kept at both (engine/sim/src/world_keep.cpp, keepDoors), a door not yet attacked keeps a guard, a breach pulls men from the
// other door, and an attacker coming to the walls takes the weaker door (world_gates.cpp, routeToGate).
#include <algorithm>
#include <cstdio>
#include <vector>

#include "illyria/sim/conquest.h"
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
using Ring = Continent::Castle::Ring;

// The computer lord 1's capital, with its garrison placed, and the doors of its outer ring.
struct Case {
    Conquest c;
    World* w = nullptr;
    int32_t town = -1;
    std::vector<size_t> doors;
    std::vector<EntityId> ids;
    Vec3 heart;
    explicit Case(uint8_t side = 1) : c(continent(), 5, Conquest::Setup{0, 0xFF}) {
        c.setHuman(0, true);
        c.prepareWorld();
        for (size_t i = 0; i < c.cities().size(); ++i)
            if (c.cities()[i].owner == side && continent().cities()[i].capital) town = static_cast<int32_t>(i);
        w = c.presentedWorld();
        heart = continent().cities()[static_cast<size_t>(town)].pos - w->region()->origin();
        const size_t gate = w->gateOfTown(heart);
        for (size_t j = 0; j < w->region()->gates().size(); ++j)
            if (w->region()->gates()[j].ring == w->region()->gates()[gate].ring) doors.push_back(j);
        for (const Conquest::Stack& st : c.stacks())
            if (st.owner == side && c.cityAt(st.cx, st.cy) == town && st.path.empty())
                for (const Army& a : st.armies)
                    for (const Man& m : a.men)
                        if (m.unit) ids.push_back(m.unit);
    }
    Vec3 mid(size_t d) const { return (w->region()->gates()[doors[d]].a + w->region()->gates()[doors[d]].b) * Fixed::fromRatio(1, 2); }
    Vec3 out(size_t d) const { return w->region()->gates()[doors[d]].out; }
    const Unit& unit(EntityId id) const { return *w->findUnit(id); }
    void run(int seconds) {
        for (int t = 0; t < seconds * kTps; ++t) w->step({});
    }
    // Giants, `n` of them, `metres` out from the door (outside the walls by a negative number: inside).
    void foes(size_t d, int n, int metres) {
        for (int i = 0; i < n; ++i) {
            Vec3 p = mid(d) + out(d) * Fixed::fromInt(metres) + Vec3{Fixed::fromInt(i * 3 - n), Fixed::zero(), Fixed::zero()};
            p.z = w->groundHeight(p.x, p.y);
            w->spawnUnit(0, p, -out(d), unitStats(UnitType::Giants));
        }
    }
    // The garrison's grounded men of a role within `range` of a door (the one nearer that door than the other).
    int near(size_t d, World::GarrisonRole role, int range) const {
        int n = 0;
        for (const EntityId id : ids) {
            const Unit& u = unit(id);
            if (!u.alive() || u.stats.flyAltitude > Fixed::zero() || World::garrisonRole(u.stats) != role) continue;
            const double mine = (u.pos - mid(d)).flat().length().toDouble();
            bool nearest = true;
            for (size_t e = 0; e < doors.size(); ++e) nearest = nearest && mine <= (u.pos - mid(e)).flat().length().toDouble() + 0.001;
            n += nearest && mine <= range;
        }
        return n;
    }
};
}  // namespace

TEST(a_capital_and_a_long_town_have_a_second_door_in_a_side_wall) {
    int capitals = 0, longTowns = 0;
    for (const Continent::City& city : continent().cities()) {
        if (!city.walled) continue;
        const Ring& outer = city.castle.rings.front();
        const bool two = outer.gateEdge2 != Ring::kNoEdge;
        CHECK(two == (city.capital || city.castle.shape == 1));
        if (!two) continue;
        capitals += city.capital;
        longTowns += !city.capital;
        const size_t n = outer.corners.size();
        CHECK(outer.gateEdge2 == (outer.gateEdge + n / 4) % n);
        CHECK(outer.gateOn(outer.gateEdge) && outer.gateOn(outer.gateEdge2) && outer.gateEdge2 != outer.gateEdge && !outer.gateOn((outer.gateEdge + n / 2) % n));  // (a four-sided town's side wall is the next edge)
        // The pair of towers flanking each door: four gate towers on the outer ring, and the walls' crenels stay clear of them.
        CHECK(outer.galleries().size() == n + 4);
        CHECK(city.castle.rings.size() == (city.capital ? 2u : 1u));
    }
    std::printf("  %d capitals and %d long towns with two doors\n", capitals, longTowns);
    CHECK(capitals == Continent::kFactionCount);
    CHECK(longTowns >= 1);
}

TEST(a_capitals_outer_ring_has_two_gates_in_the_region_and_its_garrison_stands_at_both) {
    Case k;
    CHECK(k.doors.size() == 2);
    if (k.doors.size() != 2) return;
    CHECK(k.w->gates()[k.doors[0]].garrisoned && k.w->gates()[k.doors[1]].garrisoned);
    // The doors face different ways (a quarter turn apart).
    CHECK(k.out(0).flat().dot(k.out(1).flat()) < Fixed::fromRatio(1, 2));
    int foot[2], wall[2];
    for (size_t d = 0; d < 2; ++d) {
        foot[d] = k.near(d, World::GarrisonRole::Foot, 45);
        wall[d] = k.near(d, World::GarrisonRole::Wall, 300);
    }
    std::printf("  foot %d / %d, bowmen %d / %d\n", foot[0], foot[1], wall[0], wall[1]);
    for (size_t d = 0; d < 2; ++d) {
        CHECK(foot[d] >= 6);
        CHECK(wall[d] >= 3);
    }
    // Neither door has more than twice the other's.
    CHECK(foot[0] <= foot[1] * 2 + 2 && foot[1] <= foot[0] * 2 + 2);
    CHECK(wall[0] <= wall[1] * 2 + 2 && wall[1] <= wall[0] * 2 + 2);
}

TEST(foes_at_one_door_draw_the_garrison_there_and_the_other_door_keeps_a_guard) {
    Case k;
    if (k.doors.size() != 2) { CHECK(false); return; }
    const int before[2] = {k.near(0, World::GarrisonRole::Foot, 60), k.near(1, World::GarrisonRole::Foot, 60)};
    k.foes(0, 6, 40);
    k.run(40);
    const int foot[2] = {k.near(0, World::GarrisonRole::Foot, 60), k.near(1, World::GarrisonRole::Foot, 60)};
    int manned[2] = {0, 0};
    const Rampart& ring = k.w->region()->ramparts()[static_cast<size_t>(k.w->region()->gates()[k.doors[0]].ring)];
    for (const EntityId id : k.ids) {
        const Unit& u = k.unit(id);
        if (!u.alive() || World::garrisonRole(u.stats) != World::GarrisonRole::Wall || u.stats.flyAltitude > Fixed::zero()) continue;
        for (const Rampart::Embrasure& e : ring.embrasures)
            if ((u.pos - e.pos).flat().length() < Fixed::fromRatio(6, 5) && abs(u.pos.z - e.pos.z) < Fixed::fromRatio(3, 2)) {
                ++manned[(u.pos - k.mid(0)).flat().lengthSq() < (u.pos - k.mid(1)).flat().lengthSq() ? 0 : 1];
                break;
            }
    }
    std::printf("  foot at the attacked door %d -> %d, at the other %d -> %d; bowmen on the walk %d / %d\n", before[0], foot[0], before[1], foot[1], manned[0], manned[1]);
    CHECK(foot[0] > before[0]);   // drawn to the door the foes are at
    CHECK(foot[1] >= 3);          // the other is not left bare
    CHECK(foot[1] * 3 >= foot[0]);  // ... nor stripped: at least a quarter of the garrison there
    CHECK(manned[0] >= 3 && manned[1] >= 1);  // bowmen on the walls at both doors
}

TEST(a_breach_at_one_door_pulls_reserves_from_the_other) {
    Case k;
    if (k.doors.size() != 2) { CHECK(false); return; }
    k.run(1);
    // The door stove in, and the foes inside it.
    k.w->setGateHp(k.doors[0], 0);
    k.foes(0, 5, -6);
    const int before = k.near(0, World::GarrisonRole::Foot, 60);
    k.run(30);
    const int after = k.near(0, World::GarrisonRole::Foot, 60);
    int met = 0;
    for (const Unit& u : k.w->units()) met += u.owner == 0 && !u.alive();
    std::printf("  foot at the breach %d -> %d, foes down %d\n", before, after, met);
    CHECK(after >= before);
    CHECK(met > 0 || after > before);  // the foes inside are met, not ignored
    // The other door still has its keepers (two men over their share go, not all of them).
    CHECK(k.near(1, World::GarrisonRole::Foot, 60) >= 3);
}

TEST(a_capitals_garrison_overrun_in_its_outer_ward_falls_back_to_the_bailey) {
    // The last stand: a door down and more foes inside the walls than the foot left to hold them.
    Case k;
    if (k.doors.size() != 2) { CHECK(false); return; }
    k.run(1);
    const size_t keep = [&] {
        for (size_t j = 0; j < k.w->region()->gates().size(); ++j) {
            const Gate& g = k.w->region()->gates()[j];
            if (g.ring != k.w->region()->gates()[k.doors[0]].ring && ((g.a + g.b) * Fixed::fromRatio(1, 2) - k.heart).flat().length() < Fixed::fromInt(80)) return j;
        }
        return World::kNoGate;
    }();
    CHECK(keep != World::kNoGate);
    if (keep == World::kNoGate) return;
    const Rampart& bailey = k.w->region()->ramparts()[static_cast<size_t>(k.w->region()->gates()[keep].ring)];
    const auto inBailey = [&] {
        int n = 0;
        for (const EntityId id : k.ids) n += k.unit(id).alive() && Gate::inside(bailey, k.unit(id).pos);
        return n;
    };
    CHECK(inBailey() == 0);
    k.w->setGateHp(k.doors[0], 0);
    // (Foes who only stand there, so that what the garrison does is its own choice and not a fight's.)
    UnitStats still = unitStats(UnitType::LightInfantry);
    still.aggroRange = Fixed::zero();
    const int crowd = static_cast<int>(k.ids.size()) + 6;
    for (int i = 0; i < crowd; ++i) {
        Vec3 p = k.mid(0) - k.out(0) * Fixed::fromInt(5 + i / 12 * 2) + Vec3{Fixed::fromInt(i % 12 - 6), Fixed::zero(), Fixed::zero()};
        p.z = k.w->groundHeight(p.x, p.y);
        k.w->spawnUnit(0, p, -k.out(0), still);
    }
    k.run(5);
    int bound = 0;
    for (const EntityId id : k.ids) bound += k.unit(id).alive() && k.unit(id).hasMoveTarget && Gate::inside(bailey, k.unit(id).moveTarget);
    std::printf("  after 5 s %d are bound for the bailey; its gate holder %d, the door's %d, gate mode %d\n", bound, k.w->gates()[keep].holder, k.w->gates()[k.doors[0]].holder, static_cast<int>(k.w->gates()[keep].mode));
    for (int s = 0; s < 85; s += 5) {
        k.run(5);
        if (s == 40) {
            const Unit* stuck = nullptr;
            const Vec3 gm0 = (k.w->region()->gates()[keep].a + k.w->region()->gates()[keep].b) * Fixed::fromRatio(1, 2);
            for (const EntityId id : k.ids)
                if (k.unit(id).alive() && k.unit(id).hasMoveTarget && (k.unit(id).pos - gm0).flat().length() < Fixed::fromInt(12)) stuck = &k.unit(id);
            if (stuck) {
                const Vec3 o = k.w->region()->gates()[keep].out;
                std::printf("  one stuck: %.1f m out from the doors (along out), %.1f m across; target %.1f m out; path %zu at %u; standable target %d; pos z %.1f target z %.1f\n",
                            (stuck->pos - gm0).flat().dot(o).toDouble(), (stuck->pos - gm0).flat().dot(Vec3{o.y, -o.x, Fixed::zero()}).toDouble(),
                            (stuck->moveTarget - gm0).flat().dot(o).toDouble(), stuck->path.size(), stuck->pathIndex, k.w->standable(stuck->moveTarget) ? 1 : 0,
                            stuck->pos.z.toDouble(), stuck->moveTarget.z.toDouble());
            }
        }
        int b2 = 0, near = 0, st = 0;
        const Vec3 gm = (k.w->region()->gates()[keep].a + k.w->region()->gates()[keep].b) * Fixed::fromRatio(1, 2);
        for (const EntityId id : k.ids) {
            const Unit& u = k.unit(id);
            b2 += u.alive() && u.hasMoveTarget && Gate::inside(bailey, u.moveTarget);
            near += u.alive() && (u.pos - gm).flat().length() < Fixed::fromInt(12);
            st += u.alive() && u.stationed;
        }
        std::printf("  t=%d bound %d, within 12 m of the keep gate %d, stationed %d, in %d, gate mode %d hp %d\n", s + 10, b2, near, st, inBailey(), static_cast<int>(k.w->gates()[keep].mode), k.w->gates()[keep].hp);
    }
    int alive = 0;
    for (const EntityId id : k.ids) alive += k.unit(id).alive();
    std::printf("  %d of the garrison's %d living men stand in the bailey\n", inBailey(), alive);
    CHECK(inBailey() >= 3);
}

TEST(an_attacker_coming_to_the_walls_takes_the_door_that_is_weaker) {
    for (const size_t weak : {size_t{0}, size_t{1}}) {
        Case k;
        if (k.doors.size() != 2) { CHECK(false); return; }
        k.w->setGateHp(k.doors[weak], k.w->gates()[k.doors[weak]].hp / 8);
        // A man at the side of the town, as far from one door as from the other, bound for the middle of it.
        const Vec3 across = (k.mid(1) - k.mid(0)).flat();
        const Vec3 side = Vec3{-across.y, across.x, Fixed::zero()} / max(Fixed::one(), across.length());
        const Fixed reach = across.length() / 2 + Fixed::fromInt(30);
        Vec3 from = k.heart + side * reach;
        from.z = k.w->groundHeight(from.x, from.y);
        Vec3 to = k.heart;
        to.z = k.w->groundHeight(to.x, to.y);
        const EntityId id = k.w->spawnUnit(0, from, -side, unitStats(UnitType::HeavyInfantry));
        const std::vector<Command> go = {{0, id, Command::Type::Move, to}};
        k.w->step(go);
        const Unit& u = k.unit(id);
        size_t through = 2;
        for (size_t d = 0; d < 2; ++d)
            for (const Vec3& p : u.path)
                if ((p - k.mid(d)).flat().length() < Fixed::fromRatio(1, 2)) through = d;
        std::printf("  door %zu nearly down: the man goes through door %zu\n", weak, through);
        CHECK(through == weak);
    }
}

TEST(a_two_door_garrison_run_twice_has_the_same_checksum) {
    uint64_t sum[2] = {0, 0};
    for (int run = 0; run < 2; ++run) {
        Case k;
        if (k.doors.size() != 2) { CHECK(false); return; }
        k.foes(0, 6, 40);
        k.run(20);
        k.w->setGateHp(k.doors[0], 0);
        k.foes(0, 3, -6);
        k.run(20);
        sum[run] = k.w->checksum();
    }
    CHECK(sum[0] == sum[1]);
}
