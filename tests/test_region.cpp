#include <cstdio>
#include <memory>

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
    int ground[8] = {};
    for (int y = 0; y < r.terrain().cells(); ++y)
        for (int x = 0; x < r.terrain().cells(); ++x) ++ground[static_cast<int>(r.cellGround(x, y))];
    for (int g = 0; g < 7; ++g) CHECK(ground[g] > 50);
    int kinds[7] = {};
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
    // Every platform must generate exactly this world.
    constexpr uint64_t kGolden = 0x7763e599ce28c640;
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
    const EntityId dwarf = w.spawnUnit(0, {m(-15), m(0), m(0)}, kEast, unitStats(UnitType::DwarfGrenadier));
    for (int i = 0; i < 4; ++i) w.spawnUnit(1, {m(0), Fixed::fromRatio(i * 9 - 13, 10), m(0)}, kWest, unitStats(UnitType::Archers));
    const Command bomb{0, dwarf, Command::Type::AttackGround, {m(0), m(0), m(0)}};
    w.step(std::span(&bomb, 1));
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
    const Command hold{0, dwarf, Command::Type::Stop, {}};
    w.step(std::span(&hold, 1));
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

TEST(arrows_stick_in_trees) {
    const Region& r = *illyriaRegion();
    const Obstacle* oak = nullptr;
    for (const Obstacle& o : r.obstacles())
        if (o.kind == Obstacle::Kind::Oak && r.groundAt(o.pos.x - m(25), o.pos.y) == Ground::Plains) {
            oak = &o;
            break;
        }
    CHECK(oak != nullptr);
    if (!oak) return;
    World w(regionConfig());
    for (int i = 0; i < 6; ++i)
        w.spawnUnit(0, {oak->pos.x - m(25), oak->pos.y + Fixed::fromRatio(i - 3, 3), m(0)}, kEast, unitStats(UnitType::Archers));
    std::vector<Command> fire;
    // Aim well past the tree so arrows cross it at chest height or above.
    for (EntityId id = 1; id <= 6; ++id) fire.push_back({0, id, Command::Type::AttackGround, {oak->pos.x + m(30), oak->pos.y, m(0)}});
    w.step(fire);
    for (int i = 0; i < 12 * kTicksPerSecond; ++i) w.step({});
    int inTree = 0;
    for (const Remain& rm : w.remains())
        inTree += rm.kind == RemainKind::Arrow && rm.pos.z > w.groundHeight(rm.pos.x, rm.pos.y) + Fixed::fromRatio(1, 2);
    CHECK(inTree > 0);
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
    constexpr uint64_t kGolden = 0x24aee36c35b6375a;
    if (a != kGolden) std::printf("  Crossing checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}
