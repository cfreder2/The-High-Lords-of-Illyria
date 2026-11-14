// A garrison standing in a walled town is stationed as one (engine/sim/src/world_keep.cpp, garrisonPosts): bowmen up on the
// wall-walk at the embrasures, one to each, spread round the ring; foot in the keepers' horseshoe behind the doors, facing
// them; casters, healers, engines and the lord in the ranks behind the foot. The men are ordinary men on the land: they
// stand where they are put, with no order but the player's.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

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

// The side's capital, and the stack that stands in it.
struct Seat {
    Conquest c;
    int32_t town = -1;
    const Conquest::Stack* stack = nullptr;
    size_t gate = World::kNoGate;
};
Seat seatOf(uint8_t player) {
    Seat s{Conquest(continent(), 5, Conquest::Setup{player, 0xFF}), -1, nullptr, World::kNoGate};
    s.c.setHuman(0, true);
    s.c.prepareWorld();
    for (size_t i = 0; i < s.c.cities().size(); ++i)
        if (s.c.cities()[i].owner == 0 && continent().cities()[i].capital) s.town = static_cast<int32_t>(i);
    for (const Conquest::Stack& st : s.c.stacks())
        if (st.owner == 0 && s.c.cityAt(st.cx, st.cy) == s.town && st.path.empty()) s.stack = &st;
    const World& w = *s.c.world();
    s.gate = w.gateOfTown(continent().cities()[static_cast<size_t>(s.town)].pos - w.region()->origin());
    return s;
}

const Unit* unitOf(const World& w, const Man& m) { return m.unit ? w.findUnit(m.unit) : nullptr; }
}  // namespace

namespace {
struct Layout {
    int wall = 0, foot = 0, rear = 0;
    std::set<size_t> embrasures;
    bool allOnGrid = true, noneInStone = true, wallOnWalk = true, groundInside = true, facing = true, rearBehind = true, wallPitch = true;
    double footDepth = 0, rearDepth = 1e9;
};
// Where each man of the stack stands, against the ring and gate of his town.
Layout inspect(const Seat& s) {
    const World& w = *s.c.world();
    const Gate& g = w.region()->gates()[s.gate];
    const Rampart& ring = w.region()->ramparts()[static_cast<size_t>(g.ring)];
    const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
    Layout l;
    std::vector<Vec3> walls;
    for (const Army& a : s.stack->armies)
        for (const Man& m : a.men) {
            const Unit* u = unitOf(w, m);
            if (!u || !u->alive()) continue;
            if (u->stats.flyAltitude > Fixed::zero()) continue;  // (a flier stands as it did: it wants room to take wing)
            const World::GarrisonRole role = World::garrisonRole(u->stats);
            // (A bowman stands on the walk, at the height of its deck: the ground nav is the stone under him.)
            if (role != World::GarrisonRole::Wall) l.allOnGrid = l.allOnGrid && w.standable(u->pos);
            if (role == World::GarrisonRole::Wall) {
                ++l.wall;
                bool at = false;
                for (size_t e = 0; e < ring.embrasures.size(); ++e)
                    if ((u->pos - ring.embrasures[e].pos).flat().length() < Fixed::fromRatio(6, 5) && abs(u->pos.z - ring.embrasures[e].pos.z) < Fixed::fromRatio(3, 2)) {
                        at = true;
                        l.embrasures.insert(e);
                    }
                l.wallOnWalk = l.wallOnWalk && at;
                for (const Vec3& q : walls) l.wallPitch = l.wallPitch && (q - u->pos).flat().length() >= Fixed::fromInt(3);
                walls.push_back(u->pos);
                continue;
            }
            const double depth = (mid - u->pos).flat().dot(g.out).toDouble();
            if (role == World::GarrisonRole::Foot) ++l.foot, l.footDepth = std::max(l.footDepth, depth);
            else ++l.rear, l.rearDepth = std::min(l.rearDepth, depth);
            l.groundInside = l.groundInside && Gate::inside(ring, u->pos) && abs(u->pos.z - w.groundHeight(u->pos.x, u->pos.y)) < Fixed::fromRatio(1, 4);
            const Vec3 toGate = (mid - u->pos).flat();
            l.facing = l.facing && u->facing.flat().dot(toGate) > toGate.length() * Fixed::fromRatio(9, 10);
        }
    l.rearBehind = l.rear == 0 || l.foot == 0 || l.rearDepth > l.footDepth;
    return l;
}
}  // namespace

TEST(a_capitals_garrison_stands_as_one_bowmen_on_the_walk_foot_in_ranks_the_rest_behind) {
    for (const uint8_t lord : {0, 6}) {
        Seat s = seatOf(lord);
        CHECK(s.stack != nullptr);
        CHECK(s.gate != World::kNoGate);
        const Layout l = inspect(s);
        std::printf("  lord %d: %d bowmen on %zu embrasures, %d foot, %d behind\n", lord, l.wall, l.embrasures.size(), l.foot, l.rear);
        CHECK(l.wall >= 12 && l.foot >= 24 && l.rear >= 1);
        CHECK(l.wallOnWalk);  // every bowman at an embrasure of the walk
        CHECK(static_cast<int>(l.embrasures.size()) == l.wall);  // one to an embrasure
        CHECK(l.wallPitch);  // none bunched
        CHECK(l.allOnGrid);
        CHECK(l.groundInside);  // the rest on the courtyard's ground, inside the ring
        CHECK(l.facing);  // facing the doors
        CHECK(l.rearBehind);  // the lord, casters and healers behind the foot
    }
}

TEST(bowmen_are_spread_round_the_whole_ring_with_the_first_beside_the_doors) {
    Seat s = seatOf(6);
    const World& w = *s.c.world();
    const Gate& g = w.region()->gates()[s.gate];
    const Rampart& ring = w.region()->ramparts()[static_cast<size_t>(g.ring)];
    const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
    std::vector<double> nearGate;
    double sx = 0, sy = 0;
    int n = 0;
    for (const Army& a : s.stack->armies)
        for (const Man& m : a.men) {
            const Unit* u = unitOf(w, m);
            if (!u || World::garrisonRole(u->stats) != World::GarrisonRole::Wall) continue;
            nearGate.push_back((u->pos - mid).flat().length().toDouble());
            sx += u->pos.x.toDouble(), sy += u->pos.y.toDouble(), ++n;
        }
    std::sort(nearGate.begin(), nearGate.end());
    CHECK(n >= 12);
    CHECK(nearGate.front() < 6.0);  // the walk over the doors first
    // Spread: their middle is the ring's, not a wall's (a clump on one side would sit well off it).
    CHECK(std::hypot(sx / n - ring.centre.x.toDouble(), sy / n - ring.centre.y.toDouble()) < 20.0);
    // ... and at least a quarter of them are on the far half of the ring.
    int far = 0;
    for (const Army& a : s.stack->armies)
        for (const Man& m : a.men) {
            const Unit* u = unitOf(w, m);
            far += u && World::garrisonRole(u->stats) == World::GarrisonRole::Wall && (u->pos - mid).flat().dot(g.out) < -(ring.centre - mid).flat().length() * Fixed::fromRatio(6, 5);
        }
    CHECK(far * 4 >= n);
}

TEST(the_same_garrison_is_put_in_the_same_places_twice) {
    Seat a = seatOf(6), b = seatOf(6);
    CHECK(a.c.world()->checksum() == b.c.world()->checksum());
    std::vector<UnitStats> kinds;
    for (const UnitType t : {UnitType::Hero, UnitType::LightInfantry, UnitType::Archers, UnitType::Wizards, UnitType::Journeyman, UnitType::HeavyInfantry})
        for (int i = 0; i < 9; ++i) kinds.push_back(unitStats(t));
    std::vector<const UnitStats*> ptrs;
    for (const UnitStats& k : kinds) ptrs.push_back(&k);
    const auto one = a.c.world()->garrisonPosts(a.gate, ptrs);
    const auto two = a.c.world()->garrisonPosts(a.gate, ptrs);
    CHECK(one.size() == ptrs.size());
    CHECK(one.size() == two.size());
    for (size_t i = 0; i < one.size() && i < two.size(); ++i) CHECK(one[i].pos == two[i].pos && one[i].face == two[i].face && one[i].role == two[i].role);
}

TEST(men_sent_out_of_their_stations_go_back_to_them_when_stationed_again) {
    Seat s = seatOf(6);
    World& w = *s.c.presentedWorld();
    const Gate& g = w.region()->gates()[s.gate];
    const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
    std::vector<EntityId> ids;
    for (const Army& a : s.stack->armies)
        for (const Man& m : a.men)
            if (m.unit) ids.push_back(m.unit);
    // Sent off across the courtyard (the way a stack left standing by a battle is), the bowmen down off the walk ...
    std::vector<Command> away;
    for (const EntityId id : ids) away.push_back({0, id, Command::Type::Move, mid - g.out * Fixed::fromInt(40) + Vec3{Fixed::fromInt(static_cast<int64_t>(id % 9)), Fixed::zero(), Fixed::zero()}});
    w.step(away);
    for (int t = 0; t < 120 * static_cast<int>(kTicksPerSecond); ++t) w.step({});
    CHECK(inspect(s).wallOnWalk == false);
    // ... then told to take their stations.
    w.stationMen(s.gate, ids);
    for (int t = 0; t < 150 * static_cast<int>(kTicksPerSecond); ++t) w.step({});
    const Layout l = inspect(s);
    CHECK(l.wall >= 12);
    CHECK(l.wallOnWalk);
    CHECK(static_cast<int>(l.embrasures.size()) == l.wall);
    CHECK(l.groundInside);
}
