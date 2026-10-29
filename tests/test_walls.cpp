// The top of a castle wall in battle: a walk with a parapet, one archer to
// each embrasure, defenders exposed by the angle of the shot, bodies lying on
// the walk, and missiles passing a broken gate (docs/GAME_DESIGN.md, castles).
#include <cstdio>
#include <memory>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
using Castle = Continent::Castle;
Fixed m(int64_t v) { return Fixed::fromInt(v); }
Fixed r(int64_t a, int64_t b) { return Fixed::fromRatio(a, b); }

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
// The first neutral town with no other town's walls in its battle window.
const Continent::City& town() {
    static const Continent::City* chosen = [] {
        const auto& cs = continent().cities();
        for (size_t i = Continent::kFactionCount; i < cs.size(); ++i) {
            bool alone = true;
            for (size_t k = 0; k < cs.size(); ++k)
                alone = alone && (k == i || (cs[k].pos - cs[i].pos).flat().length() > Fixed::fromInt(1100));
            if (alone) return &cs[i];
        }
        return &cs[Continent::kFactionCount];
    }();
    return *chosen;
}
std::shared_ptr<const Region> siege() {
    static const auto rg = std::make_shared<const Region>(Region::fromContinent(continent(), town().pos, 256));
    return rg;
}

// A stretch of curtain wall: the longest with embrasures, its way along and out.
struct Wall {
    const WallSeg* seg = nullptr;
    Vec3 dir, out;
    Fixed len;
    Fixed along(const Vec3& p) const { return (p - seg->a).flat().dot(dir); }
    Fixed across(const Vec3& p) const { return (p - seg->a).flat().dot(out); }
    Vec3 at(Fixed s, Fixed v) const {
        Vec3 p = seg->a + dir * s + out * v;
        p.z = Fixed::zero();
        return p;
    }
    // Inside the walk, short of the parapet, for a body of this radius.
    Fixed inner(Fixed radius) const { return seg->halfThick - Castle::kParapetThick - radius; }
};
Wall longestWall() {
    Wall best;
    for (const WallSeg& w : siege()->walls()) {
        if (w.walk <= Fixed::zero() || w.crenels.size() < 3) continue;
        const Fixed len = (w.b - w.a).flat().length();
        if (best.seg && len <= best.len) continue;
        best.seg = &w;
        best.len = len;
        best.dir = (w.b - w.a).flat() / len;
        best.out = w.out;
    }
    return best;
}
// The embrasure nearest the wall's middle: where its archer stands.
Vec3 middleEmbrasure(const Wall& w) {
    Fixed best = m(100000);
    Vec3 spot;
    for (const Fixed c : w.seg->crenels) {
        const Fixed d = abs(c - w.len / 2);
        if (d >= best) continue;
        best = d;
        spot = w.at(c, w.seg->halfThick - Castle::kParapetThick - r(9, 20));
    }
    return spot;
}
// A target that takes a lot of killing and does not shoot back.
UnitStats dummy() {
    UnitStats s = unitStats(UnitType::Archers);
    s.maxHp = 1000000;
    s.ranged = {};
    s.aggroRange = Fixed::zero();
    return s;
}
// Inside a wall's stone (not on its walk, not under the stone over a gate)?
bool inStone(const Unit& u) {
    for (const WallSeg& w : siege()->walls()) {
        if (w.walk <= Fixed::zero()) continue;
        const Vec3 ab = (w.b - w.a).flat();
        const Fixed len = ab.length();
        const Fixed s = (u.pos - w.a).flat().dot(ab) / len, v = (u.pos - w.a).flat().dot(w.out);
        if (s < Fixed::zero() || s > len || abs(v) >= w.halfThick) continue;
        if (u.pos.z + u.stats.height <= w.a.z) continue;  // in the gate passage
        if (u.pos.z < w.walk - r(1, 10)) return true;    // in the slab
        if (u.pos.z < w.walk + Castle::kMerlonTop && v > w.halfThick - Castle::kParapetThick + r(1, 100)) return true;  // in the parapet
    }
    return false;
}
// Fraction of hits on a defender at the middle embrasure, by six archers this far out.
struct Volley {
    int hits = 0, shots = 0;
    uint64_t checksum = 0;
};
Volley shootAtEmbrasure(Fixed distance, uint64_t seed) {
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = seed;
    World w(cfg);
    const Wall wall = longestWall();
    const Vec3 spot = middleEmbrasure(wall);
    const EntityId target = w.spawnUnit(1, spot, wall.out, dummy());
    for (int i = 0; i < 6; ++i)
        w.spawnUnit(0, spot + wall.out * distance + wall.dir * m(i * 2 - 5), -wall.out, unitStats(UnitType::Archers));
    Volley v;
    for (int t = 0; t < 40 * kTicksPerSecond; ++t) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) v.hits += e.kind == ImpactEvent::Kind::Hit && e.hitUnit == target;
        for (const Projectile& p : w.projectiles()) v.shots += p.kind == ProjectileKind::Arrow && p.ageTicks == 1;
    }
    v.checksum = w.checksum();
    return v;
}
}  // namespace

TEST(a_wall_walk_holds_one_archer_to_each_embrasure) {
    const Region& rg = *siege();
    const Continent::Castle::Ring& ring = town().castle.rings.front();
    Fixed perimeter;
    for (size_t e = 0; e < ring.corners.size(); ++e) {
        perimeter += (ring.corners[(e + 1) % ring.corners.size()] - ring.corners[e]).flat().length();
        const std::vector<Fixed> c = ring.crenels(e);
        for (size_t k = 1; k < c.size(); ++k) CHECK(c[k] - c[k - 1] >= Castle::kCrenelPitch - r(1, 100));
    }
    const size_t places = rg.ramparts().front().embrasures.size();
    std::printf("  %zu embrasures round %.0f m of wall\n", places, perimeter.toDouble());
    // A modest number: no more than one every pitch, but not a bare wall either.
    CHECK(Fixed::fromInt(static_cast<int64_t>(places)) * Castle::kCrenelPitch <= perimeter);
    CHECK(Fixed::fromInt(static_cast<int64_t>(places)) * Castle::kCrenelPitch * 3 >= perimeter);

    // Archers all along one wall's walk, a foe out in front: only those at
    // an embrasure can shoot; the rest stand behind merlons.
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 5;
    World w(cfg);
    const Wall wall = longestWall();
    std::vector<EntityId> line;
    for (Fixed s = m(6); s < wall.len - m(6); s += r(3, 2)) line.push_back(w.spawnUnit(1, wall.at(s, Fixed::zero()), wall.out, unitStats(UnitType::Archers)));
    for (int i = 0; i < 4; ++i) w.spawnUnit(0, wall.at(wall.len / 2 + m(i * 3), m(40)), -wall.out, dummy());
    std::vector<EntityId> shooters;
    for (int t = 0; t < 20 * kTicksPerSecond; ++t) {
        w.step({});
        for (const Projectile& p : w.projectiles()) {
            if (p.ageTicks != 1 || p.kind != ProjectileKind::Arrow) continue;
            bool seen = false;
            for (EntityId id : shooters) seen = seen || id == p.shooter;
            if (!seen) shooters.push_back(p.shooter);
        }
    }
    std::printf("  %zu archers on a %.0f m wall with %zu embrasures: %zu shot\n", line.size(), wall.len.toDouble(), wall.seg->crenels.size(), shooters.size());
    CHECK(!shooters.empty());
    CHECK(shooters.size() <= wall.seg->crenels.size());
    CHECK(shooters.size() * 2 < line.size());
}

TEST(a_defender_at_an_embrasure_is_hit_more_from_afar_than_from_below) {
    const Volley far = shootAtEmbrasure(m(45), 3), near = shootAtEmbrasure(m(5), 3);
    std::printf("  from 45 m: %d hits of %d arrows; from under the wall: %d of %d\n", far.hits, far.shots, near.hits, near.shots);
    CHECK(far.hits >= 3);
    CHECK(far.hits > near.hits * 2);
}

TEST(arrows_that_strike_the_wall_fall_to_its_foot) {
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 9;
    World w(cfg);
    const Wall wall = longestWall();
    const Vec3 spot = middleEmbrasure(wall);
    w.spawnUnit(1, spot, wall.out, dummy());
    for (int i = 0; i < 8; ++i) w.spawnUnit(0, spot + wall.out * m(30) + wall.dir * m(i * 2 - 7), -wall.out, unitStats(UnitType::Archers));
    for (int t = 0; t < 60 * kTicksPerSecond; ++t) w.step({});
    // Nothing hangs on the wall's face or sits in its stone: what struck it dropped.
    int onFace = 0, atFoot = 0;
    const Terrain& land = siege()->terrain();
    for (const Remain& rm : w.remains()) {
        if (rm.kind != RemainKind::Arrow) continue;
        const Fixed v = wall.across(rm.pos), s = wall.along(rm.pos);
        if (s < Fixed::zero() || s > wall.len) continue;
        const Fixed ground = land.heightAt(rm.pos.x, rm.pos.y);
        if (v > wall.seg->halfThick - r(1, 10) && v < wall.seg->halfThick + r(3, 10) && rm.pos.z > ground + r(1, 2) && rm.pos.z < wall.seg->walk) ++onFace;
        if (v > wall.seg->halfThick && v < wall.seg->halfThick + m(4) && rm.pos.z < ground + r(1, 2)) ++atFoot;
    }
    std::printf("  arrows on the wall's face %d, at its foot %d\n", onFace, atFoot);
    CHECK(onFace == 0);
    CHECK(atFoot >= 1);
}

TEST(defenders_walk_the_wall_walk_inside_the_parapet) {
    WorldConfig cfg;
    cfg.region = siege();
    World w(cfg);
    const Wall wall = longestWall();
    const Fixed walk = wall.seg->walk;
    const EntityId id = w.spawnUnit(1, wall.at(m(7), Fixed::zero()), wall.dir, unitStats(UnitType::Archers));
    const Vec3 end = wall.at(wall.len - m(7), wall.inner(r(2, 5)));  // right up against the parapet
    const Command go{1, id, Command::Type::Move, end};
    w.step(std::span(&go, 1));
    bool onTop = true, inside = true, arrived = false;
    for (int t = 0; t < 60 * kTicksPerSecond && !arrived; ++t) {
        w.step({});
        const Unit& u = *w.findUnit(id);
        onTop = onTop && abs(u.pos.z - walk) < r(1, 10);
        inside = inside && wall.across(u.pos) <= wall.inner(u.stats.radius) + r(1, 100) && !inStone(u);
        arrived = (u.pos - end).flat().length() < m(1);
    }
    CHECK(arrived);
    CHECK(onTop);
    CHECK(inside);
}

TEST(a_defender_killed_on_the_walk_lies_on_it) {
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 4;
    World w(cfg);
    const Wall wall = longestWall();
    const EntityId id = w.spawnUnit(1, middleEmbrasure(wall), wall.out, unitStats(UnitType::Archers));
    for (int i = 0; i < 12; ++i) w.spawnUnit(0, middleEmbrasure(wall) + wall.out * m(35) + wall.dir * m(i * 2 - 11), -wall.out, unitStats(UnitType::Archers));
    int t = 0;
    for (; t < 180 * kTicksPerSecond && w.findUnit(id)->alive(); ++t) w.step({});
    CHECK(!w.findUnit(id)->alive());
    for (int k = 0; k < 10 * kTicksPerSecond; ++k) w.step({});
    const Unit& u = *w.findUnit(id);
    std::printf("  fell after %d s: %.2f m above the walk, %.2f m in from the parapet, lying %.2f across the wall\n", t / kTicksPerSecond,
                (u.pos.z - wall.seg->walk).toDouble(), (wall.inner(Fixed::zero()) - wall.across(u.pos)).toDouble(), u.facing.dot(wall.out).toDouble());
    CHECK(abs(u.pos.z - wall.seg->walk) < r(1, 10));
    CHECK(wall.across(u.pos) <= wall.inner(u.stats.radius) + r(1, 100));
    CHECK(abs(u.facing.dot(wall.out)) < r(1, 10));  // along the walk, not across it into the stone
    CHECK(!inStone(u));
    // Shot dead, he keeps the arrow (the death matrix), and the way the blow
    // laid him down (which the body is drawn by) is along the walk too.
    CHECK(u.death.kind == DeathKind::Pierced);
    CHECK(u.death.arrowPinned);
    CHECK(u.death.arrowHeight > Fixed::zero() && u.death.arrowHeight < u.stats.height);
    CHECK(abs(u.death.dir.dot(wall.out)) < r(1, 10));
}

TEST(a_defender_cut_down_on_the_walk_loses_his_limb_onto_the_walk) {
    // A hero's sword on the walk beside him: the part cut off lands on the
    // walk (or goes over the parapet), never in the stone or down the hill under it.
    WorldConfig cfg;
    cfg.region = siege();
    int severed = 0, onWalk = 0, inSlab = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        cfg.seed = seed;
        World w(cfg);
        const Wall wall = longestWall();
        const Fixed s = wall.len / 2 + m(static_cast<int64_t>(seed));
        w.spawnUnit(0, wall.at(s, Fixed::zero()), wall.dir, unitStats(UnitType::Hero));
        const EntityId victim = w.spawnUnit(1, wall.at(s + r(7, 5), Fixed::zero()), -wall.dir, unitStats(UnitType::Archers));
        bool cut = false;
        for (int t = 0; t < 30 * kTicksPerSecond && w.findUnit(victim)->alive(); ++t) w.step({});
        cut = w.findUnit(victim)->death.kind == DeathKind::Severed;
        for (int t = 0; t < 10 * kTicksPerSecond; ++t) w.step({});
        if (!cut) continue;
        ++severed;
        for (const Remain& rm : w.remains()) {
            if (rm.kind != RemainKind::Head && rm.kind != RemainKind::Arm && rm.kind != RemainKind::Leg) continue;
            const Fixed v = wall.across(rm.pos), a = wall.along(rm.pos);
            if (a < Fixed::zero() || a > wall.len || abs(v) >= wall.seg->halfThick) continue;
            if (abs(rm.pos.z - wall.seg->walk) < r(3, 10)) ++onWalk;
            else ++inSlab;
        }
        const Unit& u = *w.findUnit(victim);
        CHECK(abs(u.pos.z - wall.seg->walk) < r(1, 10));
        CHECK(abs(u.death.dir.dot(wall.out)) < r(1, 10));
    }
    std::printf("  %d of 6 cut down on the walk: %d parts on it, %d in its stone\n", severed, onWalk, inSlab);
    CHECK(severed >= 1);
    CHECK(onWalk >= 1);
    CHECK(inSlab == 0);
}

TEST(bodies_thrown_by_stones_on_the_walls_never_rest_in_the_stone) {
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 11;
    World w(cfg);
    const Wall wall = longestWall();
    for (int i = 0; i < 6; ++i) w.spawnUnit(1, wall.at(wall.len / 2 + m(i * 2 - 5), Fixed::zero()), wall.out, unitStats(UnitType::Archers));
    w.spawnUnit(0, wall.at(wall.len / 2, m(120)), -wall.out, unitStats(UnitType::Catapult));
    int dead = 0, bad = 0;
    for (int t = 0; t < 150 * kTicksPerSecond; ++t) w.step({});
    for (const Unit& u : w.units()) {
        if (u.owner != 1 || u.airborne) continue;
        dead += !u.alive();
        bad += inStone(u);
    }
    std::printf("  %d of 6 on the wall dead, %d in the stone\n", dead, bad);
    CHECK(dead >= 1);
    CHECK(bad == 0);
}

TEST(arrows_fly_through_a_broken_gate_and_fall_to_the_ground) {
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 6;
    World w(cfg);
    const Region& rg = *siege();
    // The town's own gate: the one nearest its middle.
    const Vec3 middle{town().pos.x - rg.origin().x, town().pos.y - rg.origin().y, Fixed::zero()};
    size_t gate = 0;
    for (size_t i = 1; i < rg.gates().size(); ++i)
        if (((rg.gates()[i].a + rg.gates()[i].b) * r(1, 2) - middle).flat().length() < ((rg.gates()[gate].a + rg.gates()[gate].b) * r(1, 2) - middle).flat().length()) gate = i;
    const Gate& g = rg.gates()[gate];
    const Vec3 centre = (g.a + g.b) * r(1, 2);
    w.setGateHolder(gate, 1);
    // First at the shut doors: arrows stick in the oak.
    std::vector<Command> orders;
    for (int i = 0; i < 8; ++i) {
        const EntityId a = w.spawnUnit(0, centre + g.out * m(30) + Vec3{m(i % 4 - 2), m(i / 4), m(0)}, -g.out, unitStats(UnitType::Archers));
        orders.push_back({0, a, Command::Type::AttackGate, centre});
    }
    w.step(orders);
    for (int t = 0; t < 20 * kTicksPerSecond; ++t) w.step({});
    const Terrain& land = rg.terrain();
    const auto hanging = [&] {
        // Arrows above the ground in the gateway, in or before the doors.
        int n = 0;
        const Vec3 ab = (g.b - g.a).flat();
        for (const Remain& rm : w.remains()) {
            if (rm.kind != RemainKind::Arrow) continue;
            const Vec3 rel = (rm.pos - g.a).flat();
            if (rel.dot(ab) < Fixed::zero() || rel.dot(ab) > ab.lengthSq() || abs(rel.dot(g.out)) > m(3)) continue;
            n += rm.pos.z > land.heightAt(rm.pos.x, rm.pos.y) + r(3, 10);
        }
        return n;
    };
    const int stuck = hanging();
    // Broken: what was stuck in it falls, and what is shot through flies on into the town.
    w.setGateHp(gate, 0);
    orders.clear();
    for (const Unit& u : w.units()) orders.push_back({0, u.id, Command::Type::AttackGround, centre - g.out * m(25)});
    w.step(orders);
    for (int t = 0; t < 30 * kTicksPerSecond; ++t) w.step({});
    int through = 0;
    for (const Remain& rm : w.remains()) {
        if (rm.kind != RemainKind::Arrow || (rm.pos - g.a).flat().dot(g.out) > -m(6)) continue;
        through += rm.pos.z < land.heightAt(rm.pos.x, rm.pos.y) + r(3, 10);
    }
    std::printf("  stuck in the shut gate %d; after it broke: %d hanging, %d flew through into the town\n", stuck, hanging(), through);
    CHECK(stuck >= 3);
    CHECK(hanging() == 0);
    CHECK(through >= 10);
}

TEST(walls_and_their_embrasures_are_deterministic) {
    const Volley a = shootAtEmbrasure(m(30), 21), b = shootAtEmbrasure(m(30), 21), c = shootAtEmbrasure(m(30), 22);
    CHECK(a.checksum == b.checksum);
    CHECK(a.checksum != c.checksum);
    CHECK(siege()->checksum() == Region::fromContinent(continent(), town().pos, 256).checksum());
}
