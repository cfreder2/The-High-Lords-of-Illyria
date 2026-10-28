// The lie of the land matters: climbing is slow, cliffs are walls, and the
// side holding the high ground hits harder.
#include <cstdio>
#include <memory>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWest = -kEast;
constexpr int32_t kCells = 64;  // 4 m cells: a 256 m square

// Ground whose height is f(x) in metres, along x only.
template <typename F>
WorldConfig shaped(F f) {
    std::vector<Fixed> h;
    for (int32_t j = 0; j <= kCells; ++j)
        for (int32_t i = 0; i <= kCells; ++i) h.push_back(f(i * 4));
    WorldConfig cfg;
    cfg.terrain = std::make_shared<const Terrain>(Terrain::fromSamples(kCells, m(4), std::move(h)));
    return cfg;
}

// Ticks for a soldier to walk from x0 to x1 along y = 128.
int ticksToWalk(const WorldConfig& cfg, int64_t x0, int64_t x1) {
    World w(cfg);
    const EntityId id = w.spawnUnit(0, {m(x0), m(128), m(0)}, kEast, unitStats(UnitType::LightInfantry));
    const Command go{0, id, Command::Type::Move, {m(x1), m(128), m(0)}};
    w.step(std::span(&go, 1));
    for (int t = 1; t < 120 * kTicksPerSecond; ++t) {
        w.step({});
        if (abs(w.findUnit(id)->pos.x - m(x1)) < m(2)) return t;
    }
    return -1;
}
}  // namespace

TEST(climbing_is_slow_and_going_down_is_quick) {
    // A steady 1 in 4 slope rising eastward.
    const WorldConfig hill = shaped([](int64_t x) { return Fixed::fromInt(x) / 4; });
    const WorldConfig flat = shaped([](int64_t) { return Fixed::zero(); });
    const int level = ticksToWalk(flat, 60, 160), up = ticksToWalk(hill, 60, 160), down = ticksToWalk(hill, 160, 60);
    CHECK(level > 0 && up > 0 && down > 0);
    CHECK(up > level * 3 / 2);
    CHECK(down < level);
}

TEST(cliffs_are_walls) {
    // Flat ground, then a sheer 12 m face at x = 128.
    const WorldConfig cliff = shaped([](int64_t x) { return x > 128 ? m(12) : Fixed::zero(); });
    World w(cliff);
    const EntityId id = w.spawnUnit(0, {m(100), m(128), m(0)}, kEast, unitStats(UnitType::LightInfantry));
    const Command go{0, id, Command::Type::Move, {m(160), m(128), m(0)}};
    w.step(std::span(&go, 1));
    for (int t = 0; t < 40 * kTicksPerSecond; ++t) w.step({});
    CHECK(w.findUnit(id)->pos.x < m(129));
}

TEST(the_high_ground_wins) {
    // Equal skirmishers meet on a slope; the one standing above should win
    // most duels (each seed shuffles the dice).
    const WorldConfig slope = shaped([](int64_t x) { return Fixed::fromInt(x) / 3; });
    int upperWins = 0, lowerWins = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        WorldConfig cfg = slope;
        cfg.seed = seed;
        World w(cfg);
        const EntityId lower = w.spawnUnit(0, {m(120), m(128), m(0)}, kEast, unitStats(UnitType::LightInfantry));
        const EntityId upper = w.spawnUnit(1, {m(122), m(128), m(0)}, kWest, unitStats(UnitType::LightInfantry));
        for (int t = 0; t < 90 * kTicksPerSecond; ++t) {
            w.step({});
            if (!w.findUnit(lower)->alive() || !w.findUnit(upper)->alive()) break;
        }
        upperWins += w.findUnit(upper)->alive() && !w.findUnit(lower)->alive();
        lowerWins += w.findUnit(lower)->alive() && !w.findUnit(upper)->alive();
    }
    std::printf("  high ground duels: upper %d, lower %d\n", upperWins, lowerWins);
    CHECK(upperWins > lowerWins * 2);
}

TEST(soldiers_cross_a_footbridge_without_wading) {
    // The Heron Footbridge spans the Aldwater upstream of Oakhaven Ford.
    const Continent continent(20261027);
    WorldConfig cfg;
    cfg.region = std::make_shared<const Region>(Region::fromContinent(continent, continent.crossings().front().pos, 256));
    CHECK(cfg.region->decks().size() == 1);
    const Deck& d = cfg.region->decks().front();
    CHECK(d.foot);
    const Vec3 west = d.centre - d.dir * (d.halfSpan + m(12)), east = d.centre + d.dir * (d.halfSpan + m(12));
    World w(cfg);
    const EntityId id = w.spawnUnit(0, west, d.dir, unitStats(UnitType::LightInfantry));
    const Command go{0, id, Command::Type::Move, {east.x, east.y, m(0)}};
    w.step(std::span(&go, 1));
    bool wet = false, crossed = false, onDeck = false;
    for (int t = 0; t < 60 * kTicksPerSecond && !crossed; ++t) {
        w.step({});
        const Unit* u = w.findUnit(id);
        const Ground g = cfg.region->groundAt(u->pos.x, u->pos.y);
        wet = wet || g == Ground::DeepWater || g == Ground::Shallows;
        onDeck = onDeck || (d.contains(u->pos.x, u->pos.y) && u->pos.z >= d.centre.z - Fixed::fromRatio(1, 10));
        crossed = (u->pos - east).flat().length() < m(3);
    }
    CHECK(crossed);
    CHECK(onDeck);
    CHECK(!wet);
}
