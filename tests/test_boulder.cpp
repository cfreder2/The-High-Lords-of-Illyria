// The boulder (engine/sim/src/world_boulder.cpp; docs/GAME_DESIGN.md, "The boulder"): a catapult's stone comes down,
// smashes, and rolls on over the ground. How far on the level; farther down a slope than up one; less on soft ground;
// dead in water; the men in its way crushed and thrown, a column torn up and a line hardly touched; a great beast
// stops it; its own side crushed as the foe is, and the aim reckons the whole roll; the forecast is the throw; and
// the same scene twice is the same world.
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {

Fixed m(int64_t v) { return Fixed::fromInt(v); }
Fixed r(int64_t n, int64_t d) { return Fixed::fromRatio(n, d); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;
constexpr int32_t kCells = 96;  // 384 m
constexpr int64_t kMidY = 192;

// A man who stands where he is put and does nothing.
UnitStats stander(UnitType type, int32_t hp = 0) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged = {};
    s.melee.damage = 0;
    s.shield.blockChance = Fixed::zero();
    s.satchels = {};
    if (hp > 0) s.maxHp = hp;
    return s;
}

// A test ground, 4 m cells: `ground` between x0 and x1 (metres) and plains elsewhere, rising `gradePct` metres in a
// hundred to the east.
std::shared_ptr<const Region> field(Ground ground = Ground::Plains, int64_t x0 = 0, int64_t x1 = 0, int64_t gradePct = 0) {
    std::vector<Ground> cells(static_cast<size_t>(kCells) * kCells, Ground::Plains);
    for (int64_t y = 0; y < kCells; ++y)
        for (int64_t x = x0 / 4; x < x1 / 4; ++x) cells[static_cast<size_t>(y * kCells + x)] = ground;
    std::vector<Fixed> heights;
    if (gradePct != 0) {
        heights.resize(static_cast<size_t>(kCells + 1) * (kCells + 1));
        for (int64_t y = 0; y <= kCells; ++y)
            for (int64_t x = 0; x <= kCells; ++x) heights[static_cast<size_t>(y * (kCells + 1) + x)] = r(x * 4 * gradePct, 100) + m(gradePct < 0 ? 40 : 0);
    }
    return std::make_shared<const Region>(Region::fromGround(kCells, cells, heights));
}

World world(std::shared_ptr<const Region> region, uint64_t seed = 1) {
    WorldConfig c;
    c.seed = seed;
    c.region = std::move(region);
    return World(c);
}

const Projectile* stoneOf(const World& w, EntityId id) {
    for (const Projectile& p : w.projectiles())
        if (p.id == id) return &p;
    return nullptr;
}

// A stone as a catapult's comes down from a throw of about 100 m (19 m/s over the ground, 26 down), let go just over
// the ground at x, going east (or west): where it lies when it has stopped, and what it met on the way.
struct Roll {
    Fixed from, to;
    bool lies = false, gone = false;
    int bounces = 0, splashes = 0, struck = 0, crushed = 0, killed = 0;
    double metres() const { return (to - from).toDouble(); }
};
Roll rollIn(World& w, int64_t x, bool east = true, int64_t y = kMidY) {
    Roll out;
    const Fixed gx = m(x), gy = m(y);
    const Vec3 at{gx, gy, w.groundHeight(gx, gy) + r(1, 10)};
    const EntityId id = w.launchStone(0, at, {east ? m(19) : m(-19), m(0), m(-26)});
    out.from = gx;
    for (int i = 0; i < 60 * kTicksPerSecond; ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) {
            out.bounces += e.kind == ImpactEvent::Kind::Bounce;
            out.splashes += e.kind == ImpactEvent::Kind::Splash;
            out.struck += e.kind == ImpactEvent::Kind::Struck;
            if (e.kind == ImpactEvent::Kind::Hit && e.projectile == id) {
                ++out.crushed;
                out.killed += e.killed;
            }
        }
        const Projectile* p = stoneOf(w, id);
        if (p == nullptr) {
            out.gone = true;
            break;
        }
        out.to = p->pos.x;
        if (p->resting) {
            out.lies = true;
            break;
        }
    }
    if (!east) std::swap(out.from, out.to);
    return out;
}

int alive(const World& w, const std::vector<EntityId>& ids) {
    int n = 0;
    for (const EntityId id : ids) n += w.findUnit(id)->alive();
    return n;
}

}  // namespace

TEST(a_boulder_rolls_tens_of_metres_on_level_open_ground_and_lies_where_it_stops) {
    World w = world(field());
    const Roll roll = rollIn(w, 100);
    std::printf("  level plains: %.1f m beyond where it came down (%d bounce)\n", roll.metres(), roll.bounces);
    CHECK(roll.lies && !roll.gone);
    CHECK(roll.metres() > 20 && roll.metres() < 40);
    CHECK(roll.bounces >= 1);
    // It lies there still, a minute on: a thing on the field.
    const Fixed at = roll.to;
    for (int i = 0; i < 60 * kTicksPerSecond; ++i) w.step({});
    CHECK(w.projectiles().size() == 1 && w.projectiles()[0].resting && w.projectiles()[0].pos.x == at);
}

TEST(a_boulder_rolls_far_down_a_slope_and_dies_up_one) {
    // One hillside, 15 m in 100, rising to the east: the same stone thrown up it and down it, and on the level.
    World level = world(field());
    World hill = world(field(Ground::Plains, 0, 0, 15)), hill2 = world(field(Ground::Plains, 0, 0, 15));
    const Roll flat = rollIn(level, 200), upward = rollIn(hill, 200), down = rollIn(hill2, 300, false);
    std::printf("  a 15%% slope: up it %.1f m, level %.1f m, down it %.1f m\n", upward.metres(), flat.metres(), down.metres());
    CHECK(upward.lies && down.lies);
    CHECK(upward.metres() * 4 < flat.metres() * 3);
    CHECK(down.metres() * 2 > flat.metres() * 3);
    // Steeper (35 in 100, more than the grass holds back): it does not stop on the slope at all, and gathers speed
    // to the foot of the hill (here the field's edge, 340 m on).
    World steep = world(field(Ground::Plains, 0, 0, 35));
    const Roll away = rollIn(steep, 340, false);
    std::printf("  a 35%% slope, down: %.1f m\n", away.metres());
    CHECK(away.metres() > 300);
}

TEST(soft_ground_holds_a_boulder_and_a_road_carries_it) {
    double metres[4];
    const Ground kinds[4] = {Ground::Road, Ground::Plains, Ground::Sand, Ground::Mud};
    for (int k = 0; k < 4; ++k) {
        World w = world(field(kinds[k], 0, 384));
        metres[k] = rollIn(w, 100).metres();
    }
    std::printf("  road %.1f m, plains %.1f m, sand %.1f m, mud %.1f m\n", metres[0], metres[1], metres[2], metres[3]);
    CHECK(metres[0] > metres[1] * 1.2);
    CHECK(metres[2] < metres[1] * 0.5);
    CHECK(metres[3] < metres[2]);
}

TEST(a_boulder_stops_in_water_with_a_splash) {
    // A ford 12 m on from where it comes down, 20 m wide: it rolls in and no farther.
    World w = world(field(Ground::Shallows, 112, 132));
    const Roll roll = rollIn(w, 100);
    std::printf("  into a ford 12 m on: stopped at %.1f m, splashes %d\n", roll.metres(), roll.splashes);
    CHECK(roll.splashes == 1);
    CHECK(roll.lies);  // in the shallows it lies where it stopped (in deep water it is gone)
    CHECK(roll.metres() >= 12 && roll.metres() < 14);
    World deep = world(field(Ground::DeepWater, 112, 132));
    const Roll sunk = rollIn(deep, 100);
    CHECK(sunk.splashes == 1 && sunk.gone);
    // Thrown straight into it: the same.
    World in = world(field(Ground::Shallows, 80, 132));
    const Roll wet = rollIn(in, 100);
    CHECK(wet.splashes == 1 && wet.metres() < 1);
}

TEST(a_boulder_tears_up_a_column_in_its_line_and_hardly_touches_a_line_across_it) {
    // Sixteen men two metres apart: in file along the roll, and in a line across it, beginning 6 m beyond where it comes down.
    World column = world(field()), line = world(field());
    std::vector<EntityId> file, rank;
    for (int k = 0; k < 16; ++k) {
        file.push_back(column.spawnUnit(1, {m(106 + 2 * k), m(kMidY), m(0)}, kWest, stander(UnitType::LightInfantry)));
        rank.push_back(line.spawnUnit(1, {m(106), m(kMidY - 15 + 2 * k), m(0)}, kWest, stander(UnitType::LightInfantry)));
    }
    const Roll through = rollIn(column, 100), across = rollIn(line, 100);
    std::printf("  a file of 16: %d struck, %d dead, the stone went %.1f m | a rank of 16: %d struck, %d dead\n", through.crushed, through.killed,
                through.metres(), across.crushed, across.killed);
    CHECK(through.killed >= 4);
    CHECK(through.killed == 16 - alive(column, file));
    CHECK(across.crushed <= 2);
    CHECK(through.killed >= across.killed + 3);
    // Each man took some of its speed: it stops short of where it would have gone over empty ground.
    World empty = world(field());
    CHECK(through.metres() < rollIn(empty, 100).metres() - 3);
    // Those it struck and did not kill were thrown, and the file is shaken.
    int shaken = 0;
    for (const EntityId id : file) shaken += column.findUnit(id)->alive() && column.findUnit(id)->morale < 1000;
    CHECK(shaken > 0 || alive(column, file) == 0);
}

TEST(a_great_beast_is_hit_hard_by_a_boulder_and_an_engine_stops_it) {
    World open = world(field()), w = world(field()), e = world(field());
    const double clear = rollIn(open, 100).metres();
    const EntityId beast = w.spawnUnit(1, {m(112), m(kMidY), m(0)}, kWest, stander(UnitType::Elephants));
    const EntityId engine = e.spawnUnit(1, {m(112), m(kMidY), m(0)}, kWest, stander(UnitType::Catapult));
    const int32_t before = w.findUnit(beast)->hp, engineBefore = e.findUnit(engine)->hp;
    const Roll roll = rollIn(w, 100), stopped = rollIn(e, 100);
    std::printf("  12 m on: an elephant lost %d of %d and the stone went %.1f m (%.1f m with nothing there); an engine lost %d of %d and it went %.1f m\n",
                before - w.findUnit(beast)->hp, before, roll.metres(), clear, engineBefore - e.findUnit(engine)->hp, engineBefore, stopped.metres());
    CHECK(w.findUnit(beast)->hp < before - 80);
    CHECK(roll.metres() < clear - 8);
    CHECK(e.findUnit(engine)->hp < engineBefore - 80);
    CHECK(stopped.metres() < 15);
}

TEST(a_boulder_rolls_through_its_own_men_too) {
    World w = world(field());
    std::vector<EntityId> own;
    for (int k = 0; k < 8; ++k) own.push_back(w.spawnUnit(0, {m(108 + 2 * k), m(kMidY), m(0)}, kEast, stander(UnitType::LightInfantry)));
    const Roll roll = rollIn(w, 100);  // (the stone is side 0's, as they are)
    CHECK(roll.killed >= 3);
    CHECK(alive(w, own) == 8 - roll.killed);
}

namespace {
// A catapult 100 m west of a knot of foes (600 hp each, so they last), its own men in a file beyond them, east, from
// `gap` m on: outside the smash, in the way of the roll. 90 s of shelling over `seeds` worlds: the hit points each side lost.
struct Toll {
    int32_t friends = 0, foes = 0;
    uint64_t sum = 0;
};
Toll shell(int64_t gap, int seeds = 8) {
    Toll t;
    for (int seed = 1; seed <= seeds; ++seed) {
        World w = world(field(), static_cast<uint64_t>(seed));
        const EntityId cat = w.spawnUnit(0, {m(100), m(kMidY), m(0)}, kEast, unitStats(UnitType::Catapult));
        std::vector<EntityId> foes, friends;
        for (int k = 0; k < 6; ++k)
            foes.push_back(w.spawnUnit(1, {m(200) + r((k % 3 - 1) * 3, 2), m(kMidY) + r((k / 3) * 3 - 1, 2), m(0)}, kWest, stander(UnitType::LightInfantry, 600)));
        for (int k = 0; gap > 0 && k < 8; ++k)
            friends.push_back(w.spawnUnit(0, {m(200 + gap + 2 * k), m(kMidY), m(0)}, kWest, stander(UnitType::LightInfantry, 600)));
        const Command order{0, cat, Command::Type::AttackGround, {m(200), m(kMidY), m(0)}};
        w.step(std::span(&order, 1));
        for (int i = 0; i < 90 * kTicksPerSecond; ++i) w.step({});
        for (const EntityId id : foes) t.foes += 600 - std::max(0, w.findUnit(id)->hp);
        for (const EntityId id : friends) t.friends += 600 - std::max(0, w.findUnit(id)->hp);
        t.sum = t.sum * 31 + w.checksum();
    }
    return t;
}
}  // namespace

TEST(a_catapult_reckons_the_whole_roll_of_its_boulder_before_it_throws_at_its_own_men) {
    // Its own file stands 9 m beyond the foe and on: clear of the smash (3 m, and 4 m of scatter), square in the roll.
    // Measured with the roll left out of the aim (World::rollToll returning nothing): the friends lost 8905 hit points
    // over the eight worlds (and the foe 6068). Reckoned, the engine drops the stone to one side of the file, or waits:
    // 824 (and the foe 7092).
    const Toll alone = shell(0), behind = shell(9);
    std::printf("  no friends: foes lost %d | a file of its own 9 m beyond the foe: friends lost %d, foes %d\n", alone.foes, behind.friends, behind.foes);
    CHECK(behind.friends * 6 <= 8905);
    CHECK(behind.foes * 2 >= alone.foes);  // (and it still shells the foe)
}

TEST(the_forecast_of_a_boulders_roll_is_the_throw) {
    // What the player is shown before the order (World::stonePath) against a stone really thrown at the same mark.
    World w = world(field(Ground::Plains, 0, 0, 4));
    const EntityId cat = w.spawnUnit(0, {m(100), m(kMidY), m(0)}, kEast, unitStats(UnitType::Catapult));
    const Command order{0, cat, Command::Type::AttackGround, {m(200), m(kMidY), w.groundHeight(m(200), m(kMidY))}};
    w.step(std::span(&order, 1));
    Vec3 smash, rest;
    bool smashed = false, rested = false;
    for (int i = 0; i < 60 * kTicksPerSecond && !rested; ++i) {
        w.step({});
        for (const ImpactEvent& e : w.impacts())
            if (e.kind == ImpactEvent::Kind::Smash && !smashed) smash = e.pos, smashed = true;
        for (const Projectile& p : w.projectiles())
            if (p.kind == ProjectileKind::Stone && p.resting) rest = p.pos, rested = true;
    }
    CHECK(smashed && rested);
    const StonePath path = w.stonePath(*w.findUnit(cat), smash);
    CHECK(path.roll.size() > 30);
    const double off = (path.roll.back().pos - rest).flat().length().toDouble(), went = (rest - smash).flat().length().toDouble();
    std::printf("  thrown: came down and rolled %.1f m; the forecast's end is %.2f m from where it lay\n", went, off);
    CHECK(went > 10);
    CHECK(off < 1.5);
    // The forecast falls off in speed to nothing, and begins where it comes down.
    CHECK(path.roll.front().speed > path.roll[path.roll.size() / 2].speed);
    CHECK((path.landing - smash).flat().length() < Fixed::one());
}

TEST(a_boulder_is_the_same_on_every_run_and_the_checksum_sees_it_roll) {
    const auto run = [](bool stone, int ticks) {
        World w = world(field(Ground::Mud, 150, 170, 3), 5);
        for (int k = 0; k < 10; ++k) w.spawnUnit(1, {m(108 + 2 * k), m(kMidY + (k % 3) - 1), m(0)}, kWest, stander(UnitType::LightInfantry));
        if (stone) w.launchStone(0, {m(100), m(kMidY), w.groundHeight(m(100), m(kMidY)) + r(1, 10)}, {m(19), m(0), m(-26)});
        for (int i = 0; i < ticks; ++i) w.step({});
        return w.checksum();
    };
    CHECK(run(true, 400) == run(true, 400));
    CHECK(run(true, 40) != run(false, 40));
    CHECK(run(true, 39) != run(true, 40));  // (rolling: the state moves every tick)
    CHECK(shell(9, 2).sum == shell(9, 2).sum);
}
