#include <algorithm>
#include <cstdio>
#include <memory>
#include <utility>
#include <vector>

#include "illyria/sim/nav.h"
#include "illyria/sim/region.h"
#include "illyria/sim/scenario.h"
#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;

std::shared_ptr<const Region> illyriaRegion() {
    static const auto region = std::make_shared<const Region>(Region::generateIllyria(20261027));
    return region;
}
WorldConfig regionConfig() {
    WorldConfig cfg;
    cfg.region = illyriaRegion();
    return cfg;
}
}  // namespace

TEST(region_has_every_kind_of_ground_and_scenery) {
    const Region& r = *illyriaRegion();
    int ground[10] = {};
    for (int y = 0; y < r.terrain().cells(); ++y)
        for (int x = 0; x < r.terrain().cells(); ++x) ++ground[static_cast<int>(r.cellGround(x, y))];
    for (int g = 0; g < 7; ++g) CHECK(ground[g] > 50);
    int kinds[8] = {};
    for (const Obstacle& o : r.obstacles()) ++kinds[static_cast<int>(o.kind)];
    CHECK(kinds[0] > 500 && kinds[1] > 500 && kinds[2] > 500);  // pines, oaks, undergrowth
    CHECK(kinds[3] > 100 && kinds[4] > 100);                      // rocks, boulders
    CHECK(kinds[5] >= 10 && kinds[6] == 8);                       // village, temple pillars
    // The ford is shallow enough to wade; the river beside it is not.
    CHECK(r.groundAt(r.ford().x, r.ford().y) == Ground::Shallows);
    CHECK(r.groundAt(m(552), m(600)) == Ground::DeepWater);
}

TEST(region_is_deterministic_and_pinned) {
    CHECK(Region::generateIllyria(20261027).checksum() == illyriaRegion()->checksum());
    CHECK(Region::generateIllyria(1).checksum() != illyriaRegion()->checksum());
    // Every platform must generate exactly this world (its temple pillars as wide as their drawn feet).
    constexpr uint64_t kGolden = 0x01e3e4745061101b;  // (was 0xdd6cd009aa0f3da1 before roads of classes by traffic (a lane to four wide, `Polyline::widths` read by the ground and the drawing), clean junctions (no spurs or loops), bends round boulders, bridges with ramps to the ground and no bridge under a castle's wall)
    if (illyriaRegion()->checksum() != kGolden) std::printf("  region checksum: 0x%016llx\n", static_cast<unsigned long long>(illyriaRegion()->checksum()));
    CHECK(illyriaRegion()->checksum() == kGolden);
}

TEST(soldiers_cross_the_river_by_the_ford) {
    World w(regionConfig());
    // West bank, north of the ford, ordered straight across to the east bank.
    const EntityId u = w.spawnUnit(0, {m(500), m(600), m(0)}, kEast, unitStats(UnitType::LightInfantry));
    const Vec3 goal{m(620), m(600), m(0)};
    const Command go{0, u, Command::Type::Move, goal};
    w.step(std::span(&go, 1));
    CHECK(w.findUnit(u)->path.size() > 1);  // not a straight line
    bool waded = false;
    for (int i = 0; i < 150 * kTicksPerSecond && w.findUnit(u)->hasMoveTarget; ++i) {
        w.step({});
        const Vec3 p = w.findUnit(u)->pos;
        CHECK(w.region()->groundAt(p.x, p.y) != Ground::DeepWater);
        waded = waded || w.region()->groundAt(p.x, p.y) == Ground::Shallows;
    }
    CHECK(waded);
    CHECK((w.findUnit(u)->pos - goal).flat().length() < m(2));
}

TEST(bodies_do_not_overlap_and_heavier_ones_push_harder) {
    World w(WorldConfig{});
    const EntityId small = w.spawnUnit(0, {m(0), m(0), m(0)}, kEast, unitStats(UnitType::LightInfantry));
    const EntityId big = w.spawnUnit(0, {Fixed::fromRatio(1, 2), m(0), m(0)}, kWest, unitStats(UnitType::Elephants));
    w.step({});
    const Unit* a = w.findUnit(small);
    const Unit* b = w.findUnit(big);
    const Fixed gap = (b->pos - a->pos).flat().length();
    CHECK(gap >= a->stats.radius + b->stats.radius - Fixed::fromRatio(1, 100));
    // The man was shoved much farther than the elephant.
    CHECK(abs(a->pos.x) > abs(b->pos.x - Fixed::fromRatio(1, 2)) * 3);
}

TEST(scenery_blocks_soldiers) {
    const Region& r = *illyriaRegion();
    const Obstacle* boulder = nullptr;
    for (const Obstacle& o : r.obstacles())
        if (o.kind == Obstacle::Kind::Boulder && o.radius > m(2) && r.groundAt(o.pos.x - m(10), o.pos.y) != Ground::DeepWater) {
            boulder = &o;
            break;
        }
    CHECK(boulder != nullptr);
    if (!boulder) return;
    World w(regionConfig());
    const EntityId u = w.spawnUnit(0, {boulder->pos.x - m(10), boulder->pos.y, m(0)}, kEast, unitStats(UnitType::LightInfantry));
    const Command go{0, u, Command::Type::Move, {boulder->pos.x + m(10), boulder->pos.y, m(0)}};
    w.step(std::span(&go, 1));
    Fixed closest = m(100);
    for (int i = 0; i < 30 * kTicksPerSecond; ++i) {
        w.step({});
        closest = min(closest, (w.findUnit(u)->pos - boulder->pos).flat().length());
    }
    CHECK(closest >= boulder->radius + w.findUnit(u)->stats.radius - Fixed::fromRatio(1, 10));
}

TEST(walking_through_body_parts_kicks_them_along) {
    World w(WorldConfig{});
    // A satchel kills a soldier, scattering parts; then someone walks through them.
    for (int i = 0; i < 4; ++i) w.spawnUnit(1, {m(0), Fixed::fromRatio(i * 9 - 13, 10), m(0)}, kWest, unitStats(UnitType::Archers));
    w.placeSatchel(0, {m(0), m(0), m(0)}, unitStats(UnitType::DwarfGrenadier).satchels.charge, kTicksPerSecond);
    Vec3 part;
    bool found = false;
    for (int i = 0; i < 40 * kTicksPerSecond && !found; ++i) {
        w.step({});
        if (!w.projectiles().empty()) continue;  // wait until everything has settled
        for (const Remain& rm : w.remains())
            if (rm.kind == RemainKind::Head || rm.kind == RemainKind::Arm || rm.kind == RemainKind::Leg) {
                part = rm.pos;
                found = true;
            }
    }
    CHECK(found);
    if (!found) return;
    const EntityId walker = w.spawnUnit(0, {part.x - m(6), part.y, m(0)}, kEast, unitStats(UnitType::HeavyInfantry));
    const Command go{0, walker, Command::Type::Move, {part.x + m(6), part.y, m(0)}};
    w.step(std::span(&go, 1));
    int kicked = 0;
    for (int i = 0; i < 8 * kTicksPerSecond; ++i) {
        w.step({});
        for (const Projectile& p : w.projectiles()) kicked += p.kind == ProjectileKind::Debris && p.ageTicks == 1;
    }
    CHECK(kicked > 0);
}

TEST(walkers_leave_arrows_where_they_fell) {
    World w(WorldConfig{});
    // A volley into open ground, then a heavy soldier marches through it.
    std::vector<EntityId> archers;
    for (int i = 0; i < 6; ++i) archers.push_back(w.spawnUnit(0, {m(-40), m(i * 2 - 5), m(0)}, kEast, unitStats(UnitType::Archers)));
    std::vector<Command> fire;
    for (EntityId a : archers) fire.push_back({0, a, Command::Type::AttackGround, {m(0), m(0), m(0)}});
    w.step(fire);
    for (int i = 0; i < 12 * kTicksPerSecond; ++i) w.step({});
    std::vector<Command> stop;
    for (EntityId a : archers) stop.push_back({0, a, Command::Type::Stop, {}});
    w.step(stop);
    for (int i = 0; i < 5 * kTicksPerSecond; ++i) w.step({});
    std::vector<Remain> before;
    for (const Remain& r : w.remains())
        if (r.kind == RemainKind::Arrow) before.push_back(r);
    CHECK(before.size() > 5);
    const EntityId walker = w.spawnUnit(0, {m(-12), m(0), m(0)}, kEast, unitStats(UnitType::HeavyInfantry));
    for (int pass = 0; pass < 3; ++pass) {
        const Command go{0, walker, Command::Type::Move, {pass % 2 ? m(-12) : m(12), m(pass - 1), m(0)}};
        w.step(std::span(&go, 1));
        for (int i = 0; i < 8 * kTicksPerSecond; ++i) {
            w.step({});
            for (const Projectile& p : w.projectiles()) CHECK(!(p.kind == ProjectileKind::Debris && p.remain == RemainKind::Arrow));
        }
    }
    size_t same = 0;
    for (const Remain& r : w.remains())
        for (const Remain& b : before) same += r.kind == RemainKind::Arrow && r.pos == b.pos && r.dir == b.dir;
    CHECK(same == before.size());
}

TEST(walkers_leave_bodies_where_they_fell_but_blasts_throw_them) {
    World w(WorldConfig{});
    // A volley into a field of unarmed men: shot dead, each where he stood.
    std::vector<EntityId> archers;
    for (int i = 0; i < 6; ++i) archers.push_back(w.spawnUnit(0, {m(-20), m(i * 2 - 5), m(0)}, kEast, unitStats(UnitType::Archers)));
    UnitStats victim = unitStats(UnitType::LightInfantry);
    victim.maxHp = 1;
    victim.melee.damage = 0;
    victim.shield.blockChance = Fixed::zero();
    victim.aggroRange = Fixed::zero();
    for (int x = 0; x < 4; ++x)
        for (int y = 0; y < 5; ++y) w.spawnUnit(1, {m(x * 2 - 3), m(y * 2 - 4), m(0)}, kWest, victim);
    const auto anyLeft = [&] {
        for (const Unit& u : w.units())
            if (u.owner == 1 && u.alive()) return true;
        return false;
    };
    for (int i = 0; i < 150 * kTicksPerSecond && anyLeft(); ++i) w.step({});
    CHECK(!anyLeft());
    std::vector<Command> stop;
    for (EntityId a : archers) stop.push_back({0, a, Command::Type::Stop, {}});
    w.step(stop);
    for (int i = 0; i < 3 * kTicksPerSecond; ++i) w.step({});
    std::vector<std::pair<EntityId, Vec3>> bodies;
    for (const Unit& u : w.units())
        if (!u.alive() && !u.inAir()) bodies.push_back({u.id, u.pos});
    CHECK(bodies.size() == 20);
    const auto stillThere = [&] {
        size_t n = 0;
        for (const auto& [id, pos] : bodies) n += w.findUnit(id)->pos == pos;
        return n;
    };

    // A rank of heavy infantry marches back and forth through them.
    std::vector<EntityId> walkers;
    for (int i = 0; i < 6; ++i) walkers.push_back(w.spawnUnit(0, {m(-12), m(i * 2 - 5), m(0)}, kEast, unitStats(UnitType::HeavyInfantry)));
    // Bodies a walker came near enough to have shoved aside, before.
    std::vector<bool> trodden(bodies.size());
    size_t fewest = bodies.size();
    for (int pass = 0; pass < 3; ++pass) {
        std::vector<Command> go;
        for (size_t i = 0; i < walkers.size(); ++i)
            go.push_back({0, walkers[i], Command::Type::Move, {pass % 2 ? m(-12) : m(12), m(static_cast<int64_t>(i) * 2 - 5 + pass), m(0)}});
        w.step(go);
        for (int t = 0; t < 18 * kTicksPerSecond; ++t) {
            w.step({});
            fewest = std::min(fewest, stillThere());
            for (size_t b = 0; b < bodies.size(); ++b)
                for (EntityId id : walkers) {
                    const Unit& u = *w.findUnit(id);
                    const Fixed reach = u.stats.radius + w.findUnit(bodies[b].first)->stats.radius * 3 / 5;
                    trodden[b] = trodden[b] || (u.pos - bodies[b].second).flat().lengthSq() < reach * reach;
                }
        }
    }
    int stepped = 0;
    for (bool t : trodden) stepped += t;
    std::printf("  %d of %zu bodies walked over, %zu always where they fell\n", stepped, bodies.size(), fewest);
    CHECK(stepped >= 15);
    CHECK(fewest == bodies.size());
    // Every walker got where he was sent: the dead are no obstacle.
    for (EntityId id : walkers) CHECK(!w.findUnit(id)->hasMoveTarget);

    // A satchel in their midst throws them.
    w.placeSatchel(0, {m(0), m(0), m(0)}, unitStats(UnitType::DwarfGrenadier).satchels.charge, kTicksPerSecond);
    bool blown = false;
    int thrown = 0;
    for (int i = 0; i < 20 * kTicksPerSecond && !blown; ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) blown = blown || e.kind == ImpactEvent::Kind::Explosion;
        for (const auto& [id, pos] : bodies) thrown += w.findUnit(id)->airborne;
    }
    for (int i = 0; i < 5 * kTicksPerSecond; ++i) w.step({});
    CHECK(blown);
    CHECK(thrown > 0);
    std::printf("  %zu of %zu moved by the blast\n", bodies.size() - stillThere(), bodies.size());
    CHECK(bodies.size() - stillThere() >= 3);
}

TEST(blasted_parts_skid_away_leaving_blood_trails) {
    World w(WorldConfig{});
    for (int i = 0; i < 4; ++i) w.spawnUnit(1, {m(0), Fixed::fromRatio(i * 9 - 13, 10), m(0)}, kWest, unitStats(UnitType::Archers));
    w.placeSatchel(0, {m(0), m(0), m(0)}, unitStats(UnitType::DwarfGrenadier).satchels.charge, kTicksPerSecond);
    int bleeds = 0;
    Vec3 blast;
    bool exploded = false;
    for (int i = 0; i < 20 * kTicksPerSecond; ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) {
            bleeds += e.kind == ImpactEvent::Kind::Bleed;
            if (e.kind == ImpactEvent::Kind::Explosion && !exploded) {
                blast = e.pos;
                exploded = true;
            }
        }
    }
    CHECK(exploded);
    // Every part leaves a dotted trail, not one splat...
    int parts = 0;
    Fixed farthest;
    for (const Remain& r : w.remains()) {
        if (r.kind != RemainKind::Head && r.kind != RemainKind::Arm && r.kind != RemainKind::Leg) continue;
        ++parts;
        farthest = max(farthest, (r.pos - blast).flat().length());
    }
    CHECK(parts >= 3);
    CHECK(bleeds >= parts * 3);
    // ...and some are thrown well clear of the blast.
    CHECK(farthest > m(8));
}

TEST(arrows_go_over_a_lone_tree_but_stick_in_one_by_the_mark) {
    const Region& r = *illyriaRegion();
    const Obstacle* oak = nullptr;
    for (const Obstacle& o : r.obstacles())
        if (o.kind == Obstacle::Kind::Oak && r.groundAt(o.pos.x - m(25), o.pos.y) == Ground::Plains) {
            oak = &o;
            break;
        }
    CHECK(oak != nullptr);
    if (!oak) return;
    // Six archers 25 m short of the oak, shooting at a spot `past` beyond it:
    // the arrows left stuck up in that tree.
    const auto stuck = [&](Fixed past) {
        World w(regionConfig());
        for (int i = 0; i < 6; ++i)
            w.spawnUnit(0, {oak->pos.x - m(25), oak->pos.y + Fixed::fromRatio(i - 3, 3), m(0)}, kEast, unitStats(UnitType::Archers));
        std::vector<Command> fire;
        for (EntityId id = 1; id <= 6; ++id) fire.push_back({0, id, Command::Type::AttackGround, {oak->pos.x + past, oak->pos.y, m(0)}});
        w.step(fire);
        for (int i = 0; i < 12 * kTicksPerSecond; ++i) w.step({});
        int inTree = 0;
        for (const Remain& rm : w.remains())
            inTree += rm.kind == RemainKind::Arrow && rm.pos.z > w.groundHeight(rm.pos.x, rm.pos.y) + Fixed::fromRatio(1, 2) &&
                      (rm.pos - oak->pos).flat().length() < Fixed::fromRatio(3, 2);
        return inTree;
    };
    // A lone tree well short of the mark is shot over; one just in front of it shelters him.
    CHECK(stuck(m(30)) == 0);
    CHECK(stuck(m(3)) > 0);
}

TEST(crossing_scenario_is_deterministic_and_pinned) {
    const auto run = [](uint64_t seed) {
        const Replay r = scenarios::Crossing::replay(seed);
        World w(r.config);
        scenarios::Crossing::setup(w);
        runReplay(w, r, scenarios::Crossing::kTicks);
        return w.checksum();
    };
    const uint64_t a = run(1234);
    CHECK(a == run(1234));
    constexpr uint64_t kGolden = 0x698eb5b5e3e3bbad;  // (was 0x76618e3a101c1b5e before roads of classes by traffic (a lane to four wide, `Polyline::widths` read by the ground and the drawing), clean junctions (no spurs or loops), bends round boulders, bridges with ramps to the ground and no bridge under a castle's wall; was 0x95f17af2982566da before broken men who stand (cornered, or none near to run from) no longer being passed over by their foes)
    if (a != kGolden) std::printf("  Crossing checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}

// A body part flying fast hurts the first man it strikes, a little and once;
// one kicked along (slow) hurts no one.
TEST(flying_body_parts_hurt_once_and_kicks_never) {
    World w(WorldConfig{});
    UnitStats dummy = unitStats(UnitType::LightInfantry);
    dummy.melee.damage = 0;
    dummy.shield.blockChance = Fixed::zero();
    dummy.aggroRange = Fixed::zero();
    const EntityId front = w.spawnUnit(1, {m(0), m(0), m(0)}, kWest, dummy);
    const EntityId behind = w.spawnUnit(1, {m(2), m(0), m(0)}, kWest, dummy);
    const EntityId kicked = w.spawnUnit(1, {m(0), m(10), m(0)}, kWest, dummy);
    w.launchPart(RemainKind::Head, 0, {m(-5), m(0), Fixed::fromRatio(6, 5)}, {m(14), m(0), Fixed::one()}, static_cast<uint8_t>(UnitType::HeavyInfantry));
    w.launchPart(RemainKind::Arm, 0, {-Fixed::fromRatio(6, 5), m(10), Fixed::fromRatio(1, 2)}, {m(3), m(0), Fixed::fromRatio(1, 2)});
    for (int i = 0; i < 2 * kTicksPerSecond; ++i) w.step({});
    const int32_t hurt = dummy.maxHp - w.findUnit(front)->hp;
    std::printf("  struck: %d hp; the man behind %d; the kicked-at %d\n", hurt, dummy.maxHp - w.findUnit(behind)->hp,
                dummy.maxHp - w.findUnit(kicked)->hp);
    CHECK(hurt >= 2 && hurt <= 8);
    CHECK(w.findUnit(behind)->hp == dummy.maxHp);
    CHECK(w.findUnit(kicked)->hp == dummy.maxHp);
    // It came to rest as a piece of the soldier it came from.
    bool kept = false;
    for (const Remain& r : w.remains()) kept = kept || (r.kind == RemainKind::Head && r.source == static_cast<uint8_t>(UnitType::HeavyInfantry));
    CHECK(kept);
}
