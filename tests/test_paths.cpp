// Terrain is tactical: a unit moves at its speed times what the ground does
// for its kind (gait.h), and takes the fastest way, not the shortest: to the
// road and along it rather than straight over the fields.
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>

#include "illyria/sim/campaign.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/nav.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// A field beside a straight stretch of road, found at run time (the roads
// may move): a 1 km window of the continent well away from any town's walls, a
// start out in the open fields and a goal on the road some way along.
struct RoadSite {
    std::shared_ptr<const Region> region;
    Vec3 start, goal;
};
const RoadSite& roadSite() {
    static const RoadSite site = [] {
        const Continent& c = continent();
        std::vector<Vec3> centres;  // along every road, every 400 m
        for (const Polyline& road : c.roads()) {
            for (size_t k = 0; k + 1 < road.points.size(); ++k) {
                const Vec3 a = road.points[k], d = road.points[k + 1] - a;
                const int64_t steps = (d.flat().length() / 400).floorToInt() + 1;
                for (int64_t s = 0; s < steps; ++s) centres.push_back(a + d * Fixed::fromRatio(s, steps));
            }
        }
        {
            for (const Vec3& centre : centres) {
                bool clear = true;
                for (const Continent::City& city : c.cities()) clear = clear && (city.pos - centre).flat().length() > m(450);
                if (!clear) continue;
                auto region = std::make_shared<const Region>(Region::fromContinent(c, centre, 256));
                const Region& rg = *region;
                // The roads here as dense samples, kept well inside the window.
                for (const Polyline& r : rg.roads()) {
                    std::vector<Vec3> dense;
                    for (size_t i = 0; i + 1 < r.points.size(); ++i) {
                        const Vec3 a = r.points[i], d = r.points[i + 1] - a;
                        const int64_t steps = (d.flat().length() / 4).floorToInt() + 1;
                        for (int64_t s = 0; s < steps; ++s) dense.push_back(a + d * Fixed::fromRatio(s, steps));
                    }
                    const auto inside = [](const Vec3& p) { return p.x > m(120) && p.y > m(120) && p.x < m(904) && p.y < m(904); };
                    for (size_t i = 0; i + 60 < dense.size(); i += 5) {
                        const Vec3 a = dense[i], b = dense[i + 60];  // 240 m on
                        if (!inside(a) || !inside(b)) continue;
                        Fixed arc;
                        bool dry = true;
                        for (size_t j = i; j < i + 60; ++j) {
                            arc += (dense[j + 1] - dense[j]).flat().length();
                            dry = dry && rg.groundAt(dense[j].x, dense[j].y) == Ground::Road;
                        }
                        const Vec3 chord = (b - a).flat();
                        if (!dry || chord.length() * 100 < arc * 97) continue;
                        const Vec3 dir = chord / chord.length(), side{dir.y, -dir.x, Fixed::zero()};
                        for (int sgn = -1; sgn <= 1; sgn += 2) {
                            const Vec3 s = a + side * m(30 * sgn);
                            // Open level fields between the start and the road's far end.
                            bool open = true;
                            for (int q = 0; q <= 20 && open; ++q) {
                                const Vec3 p = s + (b - s) * Fixed::fromRatio(q, 20);
                                const Ground g = rg.groundAt(p.x, p.y);
                                // Well clear of a town's walls at the window's edge.
                                for (const Continent::City& city : c.cities())
                                    open = open && (Vec3{p.x + rg.origin().x, p.y + rg.origin().y, Fixed::zero()} - city.pos).flat().length() > city.radius + m(80);
                                open = (g == Ground::Plains || g == Ground::Road) &&
                                       (q == 0 || abs(rg.terrain().heightAt(p.x, p.y) - rg.terrain().heightAt(s.x, s.y)) < m(6));
                            }
                            if (open && rg.groundAt(s.x, s.y) == Ground::Plains) return RoadSite{region, s, b};
                        }
                    }
                }
            }
        }
        return RoadSite{};
    }();
    return site;
}

// Walks a unit from `from` to `to`; returns the ticks taken (-1: never
// arrived) and counts the ticks spent on each ground.
int walkTo(World& w, EntityId id, const Vec3& to, int limitSeconds, int* onGround = nullptr) {
    const Command go{0, id, Command::Type::Move, to};
    w.step(std::span(&go, 1));
    for (int t = 1; t < limitSeconds * kTicksPerSecond; ++t) {
        w.step({});
        const Unit* u = w.findUnit(id);
        if (onGround && w.region()) ++onGround[static_cast<int>(w.region()->groundAt(u->pos.x, u->pos.y))];
        if (!u->hasMoveTarget) return t;
    }
    return -1;
}

// A level test ground, 4 m cells, with rectangles of other ground on plains.
struct Patch {
    int64_t x0, y0, x1, y1;  // metres
    Ground g;
};
std::shared_ptr<const Region> testGround(int32_t cells, std::initializer_list<Patch> patches) {
    std::vector<Ground> ground(static_cast<size_t>(cells) * cells, Ground::Plains);
    for (const Patch& p : patches)
        for (int64_t y = p.y0 / 4; y < p.y1 / 4; ++y)
            for (int64_t x = p.x0 / 4; x < p.x1 / 4; ++x) ground[static_cast<size_t>(y * cells + x)] = p.g;
    return std::make_shared<const Region>(Region::fromGround(cells, ground));
}
}  // namespace

TEST(a_soldier_in_the_fields_makes_for_the_road_and_arrives_sooner) {
    const RoadSite& site = roadSite();
    CHECK(site.region != nullptr);
    if (!site.region) return;
    const NavGrid nav(*site.region);
    const UnitStats stats = unitStats(UnitType::LightInfantry);
    // Straight over the fields would take this long.
    const Fixed straight = nav.travelTime(site.start, site.goal, Gait::Foot) / stats.moveSpeed;
    std::vector<Vec3> path;
    CHECK(nav.findPath(site.start, site.goal, path));
    CHECK(path.size() > 1);  // not straight
    WorldConfig cfg;
    cfg.region = site.region;
    World w(cfg);
    const EntityId id = w.spawnUnit(0, site.start, kEast, stats);
    int ground[kGroundCount] = {};
    const int ticks = walkTo(w, id, site.goal, 300, ground);
    const double secs = static_cast<double>(ticks) / kTicksPerSecond;
    std::printf("  to the road: %.0f s by road (%d%% of it on the road), %.0f s straight over the fields\n", secs,
                ticks > 0 ? ground[static_cast<int>(Ground::Road)] * 100 / ticks : 0, straight.toDouble());
    CHECK(ticks > 0);
    CHECK(secs < straight.toDouble() * 0.97);
    CHECK(ground[static_cast<int>(Ground::Road)] * 2 > ticks);
}

TEST(a_squad_marching_to_the_road_keeps_together_on_it) {
    const RoadSite& site = roadSite();
    if (!site.region) return;
    WorldConfig cfg;
    cfg.region = site.region;
    World w(cfg);
    const UnitStats stats = unitStats(UnitType::HeavyInfantry);
    const Vec3 dir = (site.goal - site.start).flat() / (site.goal - site.start).flat().length();
    const Vec3 right{dir.y, -dir.x, Fixed::zero()};
    std::vector<EntityId> ids;
    std::vector<Command> orders;
    // Twelve men in three ranks of four, ordered to form a column at the goal.
    for (int i = 0; i < 12; ++i) {
        const Fixed col = Fixed::fromRatio(2 * (i % 4) - 3, 1), row = Fixed::fromInt(-2 * (i / 4));
        const EntityId id = w.spawnUnit(0, site.start + right * col + dir * row, dir, stats);
        ids.push_back(id);
        Command c{0, id, Command::Type::Move, site.goal + right * Fixed::fromInt(i % 2 == 0 ? -1 : 1) - dir * Fixed::fromInt(2 * (i / 2))};
        c.speed = stats.moveSpeed * Fixed::fromRatio(95, 100);
        c.march = true;
        c.marchFrom = site.start;
        c.marchTo = site.goal;
        orders.push_back(c);
    }
    w.step(orders);
    int samples = 0, onRoad = 0;
    for (int t = 0; t < 200 * kTicksPerSecond; ++t) {
        w.step({});
        // Well under way: count who is on the road.
        if (t > 60 * kTicksPerSecond && t < 140 * kTicksPerSecond && t % kTicksPerSecond == 0) {
            for (EntityId id : ids) {
                const Unit* u = w.findUnit(id);
                ++samples;
                onRoad += site.region->groundAt(u->pos.x, u->pos.y) == Ground::Road;
            }
        }
    }
    std::printf("  squad on the march: %d%% of the time on the road\n", samples ? onRoad * 100 / samples : 0);
    CHECK(samples > 0 && onRoad * 100 >= samples * 70);
    int there = 0;
    for (EntityId id : ids) there += !w.findUnit(id)->hasMoveTarget;
    CHECK(there >= 11);
}

TEST(cavalry_rides_round_a_wood_that_infantry_cuts_through) {
    // A wood across the way: 96 m deep, 120 m wide, in a 400 m field.
    const auto rg = testGround(100, {{152, 140, 248, 260, Ground::Forest}});
    WorldConfig cfg;
    cfg.region = rg;
    const Vec3 from{m(100), m(200), m(0)}, to{m(300), m(200), m(0)};
    int foot[kGroundCount] = {}, horse[kGroundCount] = {};
    World a(cfg);
    const int footTicks = walkTo(a, a.spawnUnit(0, from, kEast, unitStats(UnitType::LightInfantry)), to, 200, foot);
    World b(cfg);
    const UnitStats cav = unitStats(UnitType::LightCavalry);
    const int horseTicks = walkTo(b, b.spawnUnit(0, from, kEast, cav), to, 200, horse);
    const NavGrid nav(*rg);
    const double straight = (nav.travelTime(from, to, Gait::Horse) / cav.moveSpeed).toDouble();
    std::printf("  a wood: men %d ticks in it, horse %d (%.1f s round it, %.1f s through)\n", foot[static_cast<int>(Ground::Forest)],
                horse[static_cast<int>(Ground::Forest)], static_cast<double>(horseTicks) / kTicksPerSecond, straight);
    CHECK(footTicks > 0 && horseTicks > 0);
    CHECK(foot[static_cast<int>(Ground::Forest)] > 0);
    CHECK(horse[static_cast<int>(Ground::Forest)] == 0);
    CHECK(static_cast<double>(horseTicks) / kTicksPerSecond < straight);
}

TEST(a_dwarf_crosses_rock_faster_than_a_man) {
    const auto rg = testGround(64, {{0, 0, 256, 256, Ground::Rock}});
    WorldConfig cfg;
    cfg.region = rg;
    const Vec3 from{m(20), m(128), m(0)}, to{m(220), m(128), m(0)};
    World a(cfg), b(cfg);
    const int man = walkTo(a, a.spawnUnit(0, from, kEast, unitStats(UnitType::LightInfantry)), to, 300);
    const int dwarf = walkTo(b, b.spawnUnit(0, from, kEast, unitStats(UnitType::Dwarves)), to, 300);
    std::printf("  200 m of rock: man %.0f s, dwarf %.0f s\n", static_cast<double>(man) / kTicksPerSecond,
                static_cast<double>(dwarf) / kTicksPerSecond);
    CHECK(man > 0 && dwarf > 0 && dwarf < man);
    // On plains the man is the quicker.
    const auto plains = testGround(64, {});
    WorldConfig pc;
    pc.region = plains;
    World c(pc), d(pc);
    CHECK(walkTo(c, c.spawnUnit(0, from, kEast, unitStats(UnitType::LightInfantry)), to, 300) <
          walkTo(d, d.spawnUnit(0, from, kEast, unitStats(UnitType::Dwarves)), to, 300));
}

TEST(each_kind_has_its_ground) {
    CHECK(gaitSpeed(Gait::Horse, Ground::Road) > gaitSpeed(Gait::Foot, Ground::Road));
    CHECK(gaitSpeed(Gait::Horse, Ground::Forest) < gaitSpeed(Gait::Foot, Ground::Forest));
    CHECK(gaitSpeed(Gait::Horse, Ground::Swamp) < gaitSpeed(Gait::Foot, Ground::Swamp));
    CHECK(gaitSpeed(Gait::Wolf, Ground::Forest) > gaitSpeed(Gait::Foot, Ground::Forest));
    CHECK(gaitSpeed(Gait::Woodland, Ground::Forest) > gaitSpeed(Gait::Foot, Ground::Forest));
    CHECK(gaitSpeed(Gait::Dwarf, Ground::Rock) > gaitSpeed(Gait::Foot, Ground::Rock));
    CHECK(gaitClimbPct(Gait::Dwarf) < gaitClimbPct(Gait::Foot));
    CHECK(gaitSpeed(Gait::Giant, Ground::Mud) > gaitSpeed(Gait::Foot, Ground::Mud));
    CHECK(gaitSpeed(Gait::Undead, Ground::Swamp) == Fixed::one());
    CHECK(unitStats(UnitType::Archers).gait == Gait::Woodland);
    CHECK(unitStats(UnitType::WolfRiders).gait == Gait::Wolf);
    CHECK(unitStats(UnitType::Minotaurs).gait == Gait::Giant);
    CHECK(unitStats(UnitType::Griffins).gait == Gait::Foot);  // fliers walk like men
    for (int g = 0; g < kGaitCount; ++g) CHECK(gaitSpeed(static_cast<Gait>(g), Ground::DeepWater) == Fixed::zero());
}

TEST(paths_never_cut_a_corner_through_a_swamp) {
    // A swamp from the south edge: the way round its north end is quicker.
    const auto rg = testGround(100, {{152, 0, 248, 160, Ground::Swamp}});
    const NavGrid nav(*rg);
    for (const auto& [fx, fy, tx, ty] : std::initializer_list<std::array<int64_t, 4>>{{60, 100, 340, 100}, {60, 40, 340, 150}, {120, 150, 300, 20}}) {
        const Vec3 from{m(fx), m(fy), m(0)}, to{m(tx), m(ty), m(0)};
        std::vector<Vec3> path;
        CHECK(nav.findPath(from, to, path));
        CHECK(path.size() > 1);
        Vec3 at = from;
        int wet = 0;
        for (const Vec3& p : path) {
            const int64_t steps = ((p - at).flat().length() * 2).floorToInt() + 1;
            for (int64_t s = 0; s <= steps; ++s) {
                const Vec3 q = at + (p - at) * Fixed::fromRatio(s, steps);
                wet += rg->groundAt(q.x, q.y) == Ground::Swamp && !(q == from) && !(q == to);
            }
            at = p;
        }
        CHECK(wet == 0);
        // And a walker following it stays dry too.
        WorldConfig cfg;
        cfg.region = rg;
        World w(cfg);
        int ground[kGroundCount] = {};
        CHECK(walkTo(w, w.spawnUnit(0, from, kEast, unitStats(UnitType::LightInfantry)), to, 300, ground) > 0);
        CHECK(ground[static_cast<int>(Ground::Swamp)] == 0);
    }
    // The dead do not mind the swamp: straight through.
    std::vector<Vec3> path;
    CHECK(nav.findPath({m(60), m(100), m(0)}, {m(340), m(100), m(0)}, path, Gait::Undead));
    CHECK(path.size() == 1);
}

TEST(a_soldier_on_a_bridge_deck_finds_his_way_off_it) {
    const Continent& c = continent();
    WorldConfig cfg;
    cfg.region = std::make_shared<const Region>(Region::fromContinent(c, c.crossings().front().pos, 256));
    const Deck* deck = nullptr;
    for (const Deck& k : cfg.region->decks())
        if (!k.raised) deck = &k;
    CHECK(deck != nullptr);
    if (!deck) return;
    // From the middle of the deck to the bank, a way along it from the bridge's end.
    const Vec3 side{deck->dir.y, -deck->dir.x, Fixed::zero()};
    const Vec3 bank = deck->centre - deck->dir * (deck->halfSpan + m(15));
    Vec3 goal = bank;
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        const Vec3 g = bank + side * m(60 * sgn);
        const Ground gr = cfg.region->groundAt(g.x, g.y);
        if (gr != Ground::DeepWater && gr != Ground::Shallows) goal = g;
    }
    World w(cfg);
    const EntityId id = w.spawnUnit(0, {deck->centre.x, deck->centre.y, m(0)}, deck->dir, unitStats(UnitType::LightInfantry));
    CHECK(walkTo(w, id, {goal.x, goal.y, m(0)}, 120) > 0);
    CHECK((w.findUnit(id)->pos - goal).flat().length() < m(3));
}

TEST(marching_on_roads_is_deterministic) {
    const RoadSite& site = roadSite();
    if (!site.region) return;
    const auto run = [&] {
        WorldConfig cfg;
        cfg.region = site.region;
        World w(cfg);
        std::vector<Command> orders;
        for (int i = 0; i < 8; ++i) {
            const UnitType type = i < 4 ? UnitType::LightCavalry : UnitType::Dwarves;
            const EntityId id = w.spawnUnit(0, site.start + Vec3{m(i * 2), m(0), m(0)}, kEast, unitStats(type));
            Command c{0, id, Command::Type::Move, site.goal + Vec3{m(i), m(0), m(0)}};
            c.march = i % 2 == 0;
            c.marchFrom = site.start;
            c.marchTo = site.goal;
            orders.push_back(c);
        }
        w.step(orders);
        for (int t = 0; t < 60 * kTicksPerSecond; ++t) w.step({});
        return w.checksum();
    };
    CHECK(run() == run());
}

TEST(long_routes_are_cheap_enough_for_phones) {
    const RoadSite& site = roadSite();
    if (!site.region) return;
    const NavGrid nav(*site.region);
    std::vector<Vec3> path;
    const auto t0 = std::chrono::steady_clock::now();
    int found = 0, tried = 0;
    for (int i = 0; i < 40; ++i) {
        // From the field by the road out to all sides, half a kilometre (on
        // open land: the window may hold a lake, a crag or a town's walls).
        const Fixed a = Fixed::fromRatio(i * 6283, 40 * 1000);
        const Vec3 from = site.start, to = from + Vec3{cos(a), sin(a), Fixed::zero()} * m(450);
        if (to.x < m(8) || to.y < m(8) || to.x > m(1016) || to.y > m(1016) || nav.blocked(to)) continue;
        ++tried;
        found += nav.findPath(from, to, path, static_cast<Gait>(i % kGaitCount));
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::printf("  %d routes of 450 m: %.1f ms each, %d found\n", tried, ms / std::max(1, tried), found);
    CHECK(tried >= 10);
    CHECK(found * 2 > tried);
}

TEST(a_campaign_stack_takes_the_road_to_a_far_city) {
    const Continent& c = continent();
    Campaign camp(c, 3);
    // The road cells follow the roads.
    int samples = 0, onRoad = 0;
    for (const Polyline& r : c.roads()) {
        for (size_t k = 0; k + 1 < r.points.size(); ++k) {
            const Vec3 a = r.points[k], d = r.points[k + 1] - a;
            const int64_t steps = (d.flat().length() / 20).floorToInt() + 1;
            for (int64_t s = 0; s < steps; ++s) {
                const Vec3 p = a + d * Fixed::fromRatio(s, steps);
                const int32_t x = Campaign::cellOf(static_cast<int32_t>(p.x.floorToInt())), y = Campaign::cellOf(static_cast<int32_t>(p.y.floorToInt()));
                if (x < 0 || y < 0 || x >= camp.width() || y >= camp.height()) continue;
                const Lay l = camp.terrainAt(x, y);
                ++samples;
                onRoad += l == Lay::Road || l == Lay::Bridge || l == Lay::City;
            }
        }
    }
    std::printf("  campaign road cells: %d%% of %d road samples\n", samples ? onRoad * 100 / samples : 0, samples);
    CHECK(samples > 0 && onRoad * 100 >= samples * 97);
    CHECK(Campaign::moveCost(UnitType::LightInfantry, Lay::Road) < Campaign::moveCost(UnitType::LightInfantry, Lay::Plain));

    // A stack sent from a city to another far along a road keeps to the road,
    // and its way costs less than straight over the country.
    int tested = 0, roadWins = 0;
    for (const Polyline& r : c.roads()) {
        if (tested >= 6) break;
        // The towns the road passes, in order: from the first to the last.
        std::vector<int32_t> towns;
        for (const Vec3& p : r.points) {
            for (size_t i = 0; i < c.cities().size(); ++i)
                if ((c.cities()[i].pos - p).flat().length() < m(600) && (towns.empty() || towns.back() != static_cast<int32_t>(i)))
                    towns.push_back(static_cast<int32_t>(i));
        }
        if (towns.size() < 2) continue;
        const int32_t from = towns.front(), to = towns.back();
        if (from == to) continue;
        const Campaign::CityState& cs = camp.cities()[static_cast<size_t>(from)];
        const Campaign::CityState& ct = camp.cities()[static_cast<size_t>(to)];
        if (std::max(std::abs(cs.cx - ct.cx), std::abs(cs.cy - ct.cy)) < 12) continue;  // 1.6 km or more
        const uint32_t id = camp.grant(from, 0, UnitType::LightInfantry, 1);
        if (!camp.move(id, ct.cx, ct.cy)) continue;
        const Campaign::Stack* st = camp.findStack(id);
        if (!st || st->path.empty()) continue;
        ++tested;
        int cost = 0, road = 0;
        // Costed as the campaign does: diagonal steps cost half as much again.
        int32_t px = cs.cx, py = cs.cy;
        for (const auto& [x, y] : st->path) {
            const Lay l = camp.terrainAt(x, y);
            const int step = Campaign::moveCost(UnitType::LightInfantry, l);
            cost += x != px && y != py ? step * 3 / 2 : step;
            road += l == Lay::Road || l == Lay::Bridge || l == Lay::City;
            px = x;
            py = y;
        }
        // Straight over the country, cell by cell (if no river is in the way).
        int straight = 0;
        bool wet = false;
        for (int q = 1; q < 100 && !wet; ++q) {
            const Vec3 p = c.cities()[static_cast<size_t>(from)].pos + (c.cities()[static_cast<size_t>(to)].pos - c.cities()[static_cast<size_t>(from)].pos) * Fixed::fromRatio(q, 100);
            const Continent::Sample s = c.sample(p.x, p.y);
            wet = s.riverDist < s.riverHalfWidth && !s.atFord;
        }
        const int32_t n = std::max(std::abs(cs.cx - ct.cx), std::abs(cs.cy - ct.cy));
        px = cs.cx;
        py = cs.cy;
        for (int32_t s = 1; s <= n; ++s) {
            const int32_t x = cs.cx + (ct.cx - cs.cx) * s / n, y = cs.cy + (ct.cy - cs.cy) * s / n;
            const int step = std::min(Campaign::moveCost(UnitType::LightInfantry, camp.terrainAt(x, y)), Campaign::kProhibited);
            straight += x != px && y != py ? step * 3 / 2 : step;
            px = x;
            py = y;
        }
        std::printf("  %s to %s: %zu cells, %d on the road, cost %d (straight %d)\n", c.cities()[static_cast<size_t>(from)].name.c_str(),
                    c.cities()[static_cast<size_t>(to)].name.c_str(), st->path.size(), road, cost, straight);
        // (no dearer, within the creeks between cells this count does not see)
        if (!wet) CHECK(cost * 10 <= straight * 11);
        // Most of the way on the road (the towns stand a little off it).
        roadWins += road * 100 >= static_cast<int>(st->path.size()) * 55;
    }
    CHECK(tested >= 2);
    CHECK(roadWins == tested);
}
