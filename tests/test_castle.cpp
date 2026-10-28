// Castles in battle: walls keep soldiers out except at the gate, stairs lead
// up to the wall-walk, and defenders up there shelter behind the merlons.
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
// The first neutral town (not a capital), and a 1 km battle window on it.
const Continent::City& town() { return continent().cities()[Continent::kFactionCount]; }
std::shared_ptr<const Region> siege() {
    static const auto r = std::make_shared<const Region>(Region::fromContinent(continent(), town().pos, 256));
    return r;
}
Vec3 local(const Vec3& p) { return {p.x - siege()->origin().x, p.y - siege()->origin().y, p.z}; }

// Inside any wall slab (below its top)?
bool inWall(const Region& r, const Unit& u) {
    for (const WallSeg& w : r.walls()) {
        if (u.pos.z > w.top - m(2)) continue;
        const Vec3 ab = (w.b - w.a).flat();
        const Fixed len2 = ab.lengthSq();
        const Fixed t = clamp((u.pos - w.a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one());
        if ((u.pos - (w.a + ab * t)).flat().length() < w.halfThick - Fixed::fromRatio(1, 5)) return true;
    }
    return false;
}
}  // namespace

TEST(a_town_has_walls_a_gate_stairs_and_a_walk) {
    const Region& r = *siege();
    CHECK(!r.walls().empty());
    CHECK(r.ramparts().size() == 1);
    CHECK(r.ramparts().front().stairs.size() == 3);
    int towers = 0, houses = 0;
    for (const Obstacle& o : r.obstacles()) {
        towers += o.kind == Obstacle::Kind::Tower;
        houses += o.kind == Obstacle::Kind::House;
    }
    CHECK(towers >= 6);
    CHECK(houses >= 10);
}

TEST(archers_climb_the_stairs_and_man_the_walls) {
    WorldConfig cfg;
    cfg.region = siege();
    World w(cfg);
    const Continent::Castle& k = town().castle;
    const Vec3 yard = local(k.courtyard);
    // A spot on the walk, midway along the wall opposite the gate.
    const Continent::Castle::Ring& ring = k.rings.front();
    const size_t back = (ring.gateEdge + ring.corners.size() / 2) % ring.corners.size();
    const Vec3 a = local(ring.corners[back]), b = local(ring.corners[(back + 1) % ring.corners.size()]);
    const Vec3 post{(a.x + b.x) / 2, (a.y + b.y) / 2, m(0)};
    const EntityId id = w.spawnUnit(0, yard, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::Archers));
    const Command go{0, id, Command::Type::Move, post};
    w.step(std::span(&go, 1));
    bool arrived = false, fell = false;
    for (int t = 0; t < 120 * kTicksPerSecond && !arrived; ++t) {
        w.step({});
        const Unit* u = w.findUnit(id);
        arrived = (u->pos - post).flat().length() < m(2) && u->pos.z > town().pos.z + ring.height - m(2);
        fell = fell || inWall(*cfg.region, *u);
    }
    CHECK(arrived);
    CHECK(!fell);
}

TEST(walls_keep_soldiers_out_but_the_gate_lets_them_in) {
    WorldConfig cfg;
    cfg.region = siege();
    World w(cfg);
    const Continent::Castle& k = town().castle;
    const Vec3 yard = local(k.courtyard), centre = local(town().pos);
    // Start well outside, behind the castle from the gate, and head for the courtyard.
    const Vec3 away = (centre - yard).flat() / (centre - yard).flat().length();
    const Vec3 start = centre + away * (town().radius + m(60));
    const EntityId id = w.spawnUnit(0, start, -away, unitStats(UnitType::LightInfantry));
    const Command go{0, id, Command::Type::Move, yard};
    w.step(std::span(&go, 1));
    bool inside = false, breached = false;
    for (int t = 0; t < 240 * kTicksPerSecond && !inside; ++t) {
        w.step({});
        const Unit* u = w.findUnit(id);
        inside = (u->pos - yard).flat().length() < m(4);
        breached = breached || inWall(*cfg.region, *u);
    }
    CHECK(inside);
    CHECK(!breached);
}

TEST(merlons_shelter_defenders_from_arrows_shot_from_outside) {
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 7;
    World w(cfg);
    const Continent::Castle::Ring& ring = town().castle.rings.front();
    const size_t e = (ring.gateEdge + 1) % ring.corners.size();
    const Vec3 a = local(ring.corners[e]), b = local(ring.corners[(e + 1) % ring.corners.size()]);
    const Vec3 mid = (a + b) * Fixed::fromRatio(1, 2), centre = local(town().pos);
    const Vec3 out = (mid - centre).flat() / (mid - centre).flat().length();
    // A defender placed on the walk (it walks the stairs in play; here it starts there).
    // Archers have no shields: whatever stops a missile here is the stone.
    const EntityId target = w.spawnUnit(1, {mid.x, mid.y, m(0)}, out, unitStats(UnitType::Archers));
    for (int i = 0; i < 6; ++i) w.spawnUnit(0, mid + out * m(45) + Vec3{m(i % 3) * 2, m(i / 3) * 2, m(0)}, -out, unitStats(UnitType::Archers));
    int blocked = 0, hits = 0;
    for (int t = 0; t < 120 * kTicksPerSecond && w.findUnit(target)->alive(); ++t) {
        w.step({});
        for (const ImpactEvent& ev : w.impacts()) {
            if (ev.hitUnit != target) continue;
            blocked += ev.kind == ImpactEvent::Kind::Blocked;
            hits += ev.kind == ImpactEvent::Kind::Hit;
        }
    }
    std::printf("  on the walk: %d arrows stopped by merlons, %d hits\n", blocked, hits);
    CHECK(w.findUnit(target)->pos.z > town().pos.z + ring.height - m(2));  // it really stood on the walk
    CHECK(blocked >= 2 && blocked * 3 >= hits);
}

TEST(a_neutral_garrison_mans_its_walls_and_holds_them) {
    illyria::game::Battle b(1, illyria::game::Scenario::Siege);
    const Continent::City& t = illyria::game::siegeTown(b.continent());
    CHECK(t.owner == Continent::kNeutral);
    const Fixed walk = t.pos.z + t.castle.rings.front().height - m(2);
    const auto onWalls = [&] {
        int n = 0;
        for (const illyria::game::Squad& s : b.squads())
            if (s.name == "Town Watch")
                for (EntityId id : s.members) n += b.world().findUnit(id)->pos.z > walk;
        return n;
    };
    const int start = onWalls();
    CHECK(start >= 8);
    b.advance(30.0);
    CHECK(onWalls() == start);
}
