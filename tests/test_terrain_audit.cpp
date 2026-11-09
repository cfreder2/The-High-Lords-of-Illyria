// The roads to the castles and the towers on their walls (docs/TERRAIN_AUDIT.md; the measuring is
// tools/terrain_audit.h, which tools/terrainaudit.cpp prints): every gate has a road to its mouth,
// every castle's road joins the network (the isle's two, which no road can reach, aside), no road
// runs through a castle's walls, and the towers keep to a rule rather than standing at every corner.
#include <cmath>
#include <cstdio>
#include <map>
#include <numeric>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "shape_audit.h"
#include "terrain_audit.h"
#include "water_audit.h"
#include "test.h"

using namespace illyria::sim;

namespace {

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// The road (index) that starts at this castle's gate: the road laid to it.
size_t roadOf(const Continent::City& city) {
    const audit::Gate g = audit::gateOf(city.castle.rings.front());
    const auto& roads = continent().roads();
    for (size_t ri = 0; ri < roads.size(); ++ri)
        if (!roads[ri].points.empty() && audit::len(audit::pt(roads[ri].points.front()) - g.mid) < 0.5) return ri;
    return roads.size();
}

// Roads that meet: a vertex of one within 6 m of the other.
bool meet(const Polyline& a, const Polyline& b) {
    for (const Polyline* pair[2][2] = {{&a, &b}, {&b, &a}}; const auto& [p, q] : pair)
        for (const Vec3& v : p->points)
            for (size_t i = 0; i + 1 < q->points.size(); ++i)
                if (audit::segDist(audit::pt(v), audit::pt(q->points[i]), audit::pt(q->points[i + 1])) < 6.0) return true;
    return false;
}

// Which road network (a number) each road is in; roads from `first` on are only tested against the ends of their own.
std::vector<size_t> networks(size_t designRoads) {
    const auto& roads = continent().roads();
    std::vector<size_t> net(roads.size());
    std::iota(net.begin(), net.end(), size_t{0});
    const auto find = [&](size_t i) {
        while (net[i] != i) i = net[i];
        return i;
    };
    const auto join = [&](size_t a, size_t b) { net[find(a)] = find(b); };
    for (size_t i = 0; i < designRoads; ++i)
        for (size_t j = i + 1; j < designRoads; ++j)
            if (meet(roads[i], roads[j])) join(i, j);
    // A road laid later ends on the road it joins (each end snapped onto it).
    for (size_t i = designRoads; i < roads.size(); ++i)
        for (size_t j = 0; j < roads.size(); ++j) {
            if (i == j || roads[i].points.empty()) continue;
            for (const Vec3& e : {roads[i].points.front(), roads[i].points.back()}) {
                const audit::P end = audit::pt(e);
                for (size_t k = 0; k + 1 < roads[j].points.size(); ++k)
                    if (audit::segDist(end, audit::pt(roads[j].points[k]), audit::pt(roads[j].points[k + 1])) < 2.0) {
                        join(i, j);
                        break;
                    }
            }
        }
    for (size_t i = 0; i < net.size(); ++i) net[i] = find(i);
    return net;
}

}  // namespace

TEST(every_castle_gate_has_a_road_to_its_mouth) {
    int missing = 0;
    for (const Continent::City& city : continent().cities()) {
        const audit::CastleReport r = audit::inspect(continent(), city);
        if (!r.gateRoad || r.spur >= 4.0 || r.gateWet) {
            std::printf("  %s (%.0f, %.0f): road at the gate %d, %.1f m from the road, water at the gate %d\n", city.name.c_str(), city.pos.x.toDouble(),
                        city.pos.y.toDouble(), r.gateRoad, r.spur, r.gateWet);
            ++missing;
        }
    }
    CHECK(missing == 0);
}

TEST(every_castle_is_joined_to_the_road_network_but_the_isles) {
    const auto& cities = continent().cities();
    // The design's roads, from the first to the last before the towns' own.
    size_t spurs = 0;
    for (const Continent::City& city : cities) spurs += roadOf(city) < continent().roads().size();
    CHECK(spurs == cities.size());  // every castle has a road from its gate
    size_t design = continent().roads().size();
    for (const Continent::City& city : cities) design = std::min(design, roadOf(city));
    CHECK(design >= 6);
    const std::vector<size_t> net = networks(design);
    // The road network is the one most castles' roads are in.
    std::map<size_t, int> members;
    for (const Continent::City& city : cities) ++members[net[roadOf(city)]];
    size_t network = 0;
    int most = 0;
    for (const auto& [id, count] : members)
        if (count > most) most = count, network = id;
    int cutOff = 0;
    for (const Continent::City& city : cities) {
        if (net[roadOf(city)] == network) continue;
        // Only a town on the isle off the north-west coast (Isle of the Seer), where no road goes, is on a road of its own.
        const audit::P isle{67.0 * Continent::kWidth / 1000, 892.0 * Continent::kHeight / 1000};
        CHECK(audit::len(audit::pt(city.pos) - isle) < 400.0);
        ++cutOff;
    }
    CHECK(most >= static_cast<int>(cities.size()) - 2);
    CHECK(cutOff <= 2);
}

TEST(no_road_runs_through_a_castles_walls) {
    for (const Continent::City& city : continent().cities()) {
        const audit::CastleReport r = audit::inspect(continent(), city);
        if (r.roadThroughWall) std::printf("  %s (%.0f, %.0f): a road crosses its walls away from the gate\n", city.name.c_str(), city.pos.x.toDouble(), city.pos.y.toDouble());
        CHECK(!r.roadThroughWall);
    }
}

TEST(a_battle_window_shows_the_road_at_the_gate) {
    // Skirmish and Conquest both stand the castle in a window of this continent: the road is ground there.
    const Continent::City& city = continent().cities()[Continent::kFactionCount + 3];
    const Region rg = Region::fromContinent(continent(), city.pos, 128);
    const audit::Gate g = audit::gateOf(city.castle.rings.front());
    const Vec3 o = rg.origin();
    int road = 0;
    for (double d = 4; d <= 12; d += 4) {
        const audit::P p = g.mouth + g.out * d;
        road += rg.groundAt(audit::fx(p.x) - o.x, audit::fx(p.y) - o.y) == Ground::Road;
    }
    CHECK(road == 3);
}

TEST(towers_stand_where_the_wall_turns_and_not_at_every_corner) {
    size_t most = 0;
    for (const Continent::City& city : continent().cities()) {
        const Continent::Castle& k = city.castle;
        most = std::max(most, k.towers.size());
        // A pair at each gate; a corner tower only where the wall turns sharply or kTowerSpacing of wall has passed.
        size_t corners = 0;
        for (const Continent::Castle::Ring& ring : k.rings)
            for (size_t i = 0; i < ring.corners.size(); ++i) corners += ring.towerAt(i);
        CHECK(k.towers.size() == corners + 2 * k.rings.size());
        // A town: at most a tower to each corner of a square (four) and the gate's pair; a capital with its bailey.
        CHECK(k.towers.size() <= (city.capital ? 11u : 6u));
        // No two towers crowded against each other.
        const audit::CastleReport r = audit::inspect(continent(), city);
        CHECK(r.crowded == 0);
    }
    CHECK(most <= 11);
}

TEST(the_walk_round_a_bare_corner_stays_on_the_wall_top) {
    // Where a corner has no tower the walk's way cuts across it: every point of that way (every metre) must lie on the
    // wall-top slab (within half the wall's thickness of an edge) or on a tower's gallery, never over empty air.
    int bare = 0, off = 0;
    for (const Continent::City& city : continent().cities()) {
        for (const Continent::Castle::Ring& ring : city.castle.rings) {
            const std::vector<Vec3> way = ring.way();
            const size_t n = ring.corners.size();
            for (size_t i = 0; i < n; ++i) bare += !ring.towerAt(i);
            const std::vector<Continent::Castle::Gallery> galleries = ring.galleries();
            for (size_t i = 0; i < way.size(); ++i) {
                const audit::P a = audit::pt(way[i]), b = audit::pt(way[(i + 1) % way.size()]);
                const int steps = std::max(1, static_cast<int>(audit::len(b - a)));
                for (int k = 0; k <= steps; ++k) {
                    const audit::P p = a + (b - a) * (static_cast<double>(k) / steps);
                    double best = 1e9;
                    for (size_t e = 0; e < n; ++e) best = std::min(best, audit::segDist(p, audit::pt(ring.corners[e]), audit::pt(ring.corners[(e + 1) % n])));
                    bool onGallery = false;
                    for (const auto& g : galleries)
                        onGallery = onGallery || audit::len(p - audit::pt(g.pos)) < (g.radius + Continent::Castle::kGalleryWidth).toDouble();
                    // (Only near a bare corner: the galleries' own lanes lie a little outside the wall's middle line.)
                    size_t near = 0;
                    for (size_t e = 1; e < n; ++e)
                        if (audit::len(p - audit::pt(ring.corners[e])) < audit::len(p - audit::pt(ring.corners[near]))) near = e;
                    if (!ring.towerAt(near) && best > ring.thick.toDouble() / 2 && !onGallery) {
                        ++off;
                        std::printf("  %s: %.1f m off the wall near bare corner %zu, turn dot %.2f\n", city.name.c_str(), best, near, audit::dot(audit::unit(audit::pt(ring.corners[near]) - audit::pt(ring.corners[(near + n - 1) % n])), audit::unit(audit::pt(ring.corners[(near + 1) % n]) - audit::pt(ring.corners[near]))));
                        k = steps;
                        i = way.size();
                    }
                }
            }
        }
    }
    CHECK(bare > 0);  // the rule leaves corners bare, so this tests something
    CHECK(off == 0);
}

TEST(a_gate_still_has_archer_stations_beside_it_when_corners_are_bare) {
    // The garrison's archers take the embrasures nearest the doors within 60 m: there are always some (Rampart::embrasures).
    for (size_t ci = 0; ci < continent().cities().size(); ci += 5) {
        const Continent::City& city = continent().cities()[ci];
        const Region rg = Region::fromContinent(continent(), city.pos, 128);
        const audit::Gate g = audit::gateOf(city.castle.rings.front());
        const Vec3 o = rg.origin();
        const audit::P mid = g.mid - audit::P{o.x.toDouble(), o.y.toDouble()};
        int stations = 0;
        for (const Rampart& rp : rg.ramparts())
            for (const Rampart::Embrasure& e : rp.embrasures) stations += audit::len(audit::pt(e.pos) - mid) <= 60.0;
        CHECK(stations >= 4);
    }
}

// ---- Water: rivers below their banks, lakes and islands that are not ovals (docs/TERRAIN_AUDIT.md) ----

namespace {
// The seeds the water and the shapes are checked on: the game's own (its baked relief) and two designs.
const Continent& continentOf(uint64_t seed) {
    static const Continent a(20261027), b(7), c(424242);
    return seed == 20261027 ? a : seed == 7 ? b : c;
}
const uint64_t kSeeds[] = {20261027, 7, 424242};
}  // namespace

TEST(no_river_stands_as_a_wall_of_water_above_its_banks) {
    // Before: 48 of 292 sections of the game's rivers ended in a cliff of water (up to 69 m: the surface was the
    // centreline's ground less 1.5 m, the banks fell away from it, a town's pad or a creek's gully cut them lower).
    for (const uint64_t seed : kSeeds) {
        int sections = 0, walls = 0;
        double tallest = 0;
        for (const wateraudit::RiverReport& r : wateraudit::rivers(continentOf(seed))) {
            sections += r.sections;
            walls += r.walls;
            tallest = std::max(tallest, r.tallestWall);
        }
        std::printf("  seed %llu: %d of %d sections walled, tallest %.2f m\n", static_cast<unsigned long long>(seed), walls, sections, tallest);
        CHECK(sections > 150);
        CHECK(walls <= 8);
        CHECK(tallest < 7.0);  // (a bank the levee could not reach: a creek or a pad beside the river)
    }
}

TEST(a_rivers_surface_falls_gently_along_its_course) {
    // Before: falls of 1 in 1.7 (12 m between two vertices 20 m apart), a curtain of water down the bank.
    for (const uint64_t seed : kSeeds) {
        const Continent& c = continentOf(seed);
        for (const Continent::River& rv : c.rivers()) {
            for (size_t i = 0; i + 1 < rv.points.size(); ++i) {
                const double run = (rv.points[i + 1] - rv.points[i]).flat().length().toDouble();
                const double fall = rv.points[i].z.toDouble() - rv.points[i + 1].z.toDouble();
                if (run < 1e-6) continue;
                CHECK(fall > -0.001);                           // never uphill
                CHECK(fall / run <= wateraudit::kMaxFall);      // never steep
            }
        }
    }
}

TEST(a_river_reaches_the_sea_at_the_seas_level_and_opens_out) {
    const Continent& c = continent();
    int mouths = 0;
    for (const Continent::River& rv : c.rivers()) {
        const Vec3 end = rv.points.back();
        if (c.landform(end.x, end.y).land >= Fixed::zero()) continue;  // on another river or a lake
        ++mouths;
        CHECK(end.z == Fixed::zero());
        // Wider at the mouth than upstream (the estuary), and no drop in the last 100 m: the fall is gentle to the end.
        CHECK(rv.widths.back() > rv.widths[rv.widths.size() / 2]);
        for (size_t i = rv.points.size() - 1; i > 0 && (end - rv.points[i]).flat().length() < Fixed::fromInt(100); --i)
            CHECK((rv.points[i - 1].z - rv.points[i].z).toDouble() < 3.0);  // 1 in 10 over a vertex about 20 m on
    }
    CHECK(mouths >= 3);
}

TEST(no_lake_or_island_is_a_drawn_oval) {
    // Before: Mirrormere, Silverlake and Longmere deviated 0.06, 0.05 and 0.06 from the ellipse of their own moments
    // (solidity 1.00, 0.99, 0.96); Abbey Isle 0.105 (0.97); the Isle of the Seer 0.04 (1.00): ovals.
    for (const uint64_t seed : kSeeds) {
        for (const shapeaudit::Shape& sh : shapeaudit::bodies(continentOf(seed), 3.0)) {
            if (sh.area < 1000) continue;  // a pond or a rock
            std::printf("  seed %llu %-18s deviation %.3f solidity %.3f\n", static_cast<unsigned long long>(seed), sh.name.c_str(), sh.deviation, sh.solidity);
            if (sh.name == "the Spit") {
                CHECK(sh.solidity < 0.8);  // it wanders: nowhere near convex
                continue;
            }
            CHECK(sh.deviation >= (sh.name.find("Isle") == std::string::npos ? 0.12 : 0.06));  // a small isle is measured coarsely
            CHECK(sh.solidity <= 0.97);
        }
    }
}

TEST(abbey_isle_is_reached_on_foot_by_a_spit_that_wanders) {
    const Continent& c = continent();
    const Continent::Area heart = c.lakes()[3];  // the Heartmere
    const Vec3 abbey = c.isles().front().centre;
    // Flood the dry ground from the isle's middle (2 m cells): it reaches the shore beyond the lake, by land alone.
    const int n = 220;
    const double x0 = heart.centre.x.toDouble() - 1.3 * heart.rx.toDouble(), y0 = heart.centre.y.toDouble() - 1.3 * heart.ry.toDouble(), cell = 2.5 * 1.3 * heart.rx.toDouble() / n;
    const int ny = static_cast<int>(2.6 * heart.ry.toDouble() / cell);
    std::vector<char> seen(static_cast<size_t>(n) * ny, 0);
    const auto dry = [&](int i, int j) {
        Fixed w;
        const Continent::Sample s = c.sample(Fixed::fromRatio(static_cast<int64_t>((x0 + i * cell) * 100), 100), Fixed::fromRatio(static_cast<int64_t>((y0 + j * cell) * 100), 100));
        return !c.waterSurfaceOf(s, w);
    };
    std::vector<std::pair<int, int>> stack{{static_cast<int>((abbey.x.toDouble() - x0) / cell), static_cast<int>((abbey.y.toDouble() - y0) / cell)}};
    bool ashore = false;
    int land = 0;
    while (!stack.empty()) {
        const auto [i, j] = stack.back();
        stack.pop_back();
        if (i < 0 || j < 0 || i >= n || j >= ny || seen[static_cast<size_t>(j) * n + i] || !dry(i, j)) continue;
        seen[static_cast<size_t>(j) * n + i] = 1;
        ++land;
        const double dx = (x0 + i * cell - heart.centre.x.toDouble()) / heart.rx.toDouble(), dy = (y0 + j * cell - heart.centre.y.toDouble()) / heart.ry.toDouble();
        if (dx * dx + dy * dy > 1.5 * 1.5) ashore = true;
        for (const auto& d : {std::pair{1, 0}, {-1, 0}, {0, 1}, {0, -1}}) stack.push_back({i + d.first, j + d.second});
    }
    CHECK(ashore);
    CHECK(land > 400);
    // The ruin stands on dry ground, and a walker's grid has the spit's cells as land.
    Fixed w;
    CHECK(!c.waterSurface(abbey.x, abbey.y, w));
    CHECK(c.isleWalk().size() > 40);
}
