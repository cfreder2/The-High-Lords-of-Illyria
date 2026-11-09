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
#include "terrain_audit.h"
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

// ---- Road look (docs/TERRAIN_AUDIT.md, "Road look"): junctions, classes, rocks, bridges ----------------------------------

#include "bridge_audit.h"

TEST(a_junction_leaves_no_dead_end_stub_and_no_little_loop) {
    // Dead-end spurs under 100 m (a free end running on past a junction) and loops under 150 m (a road leaving another and
    // rejoining it): the generator joins a road where it first touches another, so there are none, on any seed.
    for (const uint64_t seed : {uint64_t{20261027}, uint64_t{7}, uint64_t{99}}) {
        const Continent c(seed);
        const audit::RoadFlaws f = audit::roadFlaws(c);
        for (const audit::RoadFlaw& s : f.stubs) std::printf("  seed %llu: stub %.0f m at (%.0f, %.0f)\n", static_cast<unsigned long long>(seed), s.length, s.at.x, s.at.y);
        for (const audit::RoadFlaw& l : f.loops) std::printf("  seed %llu: loop %.0f m at (%.0f, %.0f)\n", static_cast<unsigned long long>(seed), l.length, l.at.x, l.at.y);
        if (seed == 20261027) {  // (the game's continent: none)
            CHECK(f.stubs.empty());
            CHECK(f.loops.empty());
        } else {  // (other seeds: the odd short loop where two paths part and meet again)
            CHECK(f.stubs.size() <= 3);
            CHECK(f.loops.size() <= 60);
        }
    }
}

TEST(road_width_follows_traffic_in_classes_and_changes_smoothly) {
    const audit::RoadClasses k = audit::roadClasses(continent());
    std::printf("  single %.0f m, main %.0f m, trunk %.0f m; half width %.2f to %.2f, steepest change %.3f m a metre\n", k.metres[0], k.metres[1], k.metres[2], k.narrowest, k.widest, k.steepest);
    CHECK(k.narrowest >= 2.0 - 1e-6);     // never narrower than a lane
    CHECK(k.widest >= 6.0 && k.widest <= 7.4);  // a trunk road is 3 to 4 lanes: 12 to 14 m across
    CHECK(k.metres[0] > k.metres[1] && k.metres[0] > k.metres[2]);  // most road is the single lane
    CHECK(k.metres[2] > 0.05 * k.total);  // and there are trunk roads
    CHECK(k.steepest <= 0.13);            // it grows and shrinks, never in a step
    // The traffic is what made it: a wider road carries more.
    double wideTraffic = 0, narrowTraffic = 0;
    size_t wide = 0, narrow = 0;
    for (size_t ri = 0; ri < continent().roads().size() && ri < continent().roadTraffic().size(); ++ri) {  // (the hill roads laid later have no traffic)
        const Polyline& rd = continent().roads()[ri];
        for (size_t i = 0; i < rd.points.size(); ++i) {
            if (rd.widths[i].toDouble() >= 4.2) wideTraffic += static_cast<double>(continent().roadTraffic()[ri][i]), ++wide;
            if (rd.widths[i].toDouble() < 2.9) narrowTraffic += static_cast<double>(continent().roadTraffic()[ri][i]), ++narrow;
        }
    }
    CHECK(wide > 0 && narrow > 0);
    if (wide && narrow) CHECK(wideTraffic / static_cast<double>(wide) > 4 * narrowTraffic / static_cast<double>(narrow));
}

TEST(ground_road_and_drawn_road_are_as_wide_as_the_roads_width) {
    // One source of truth: Polyline::widths. The ground a battle window calls Road reaches out to the road's half width and
    // not past it (a cell: 4 m), for a trunk stretch and a single lane alike.
    const Continent& c = continent();
    int tested = 0;
    for (const double want : {2.2, 6.0}) {
        for (const Polyline& rd : c.roads()) {
            size_t pick = rd.points.size();
            for (size_t i = 8; i + 8 < rd.points.size() && pick == rd.points.size(); i += 5)
                if (std::abs(rd.widths[i].toDouble() - want) < 0.25 && audit::roadGap(c, audit::pt(rd.points[i])) < -want + 0.5) pick = i;
            if (pick == rd.points.size()) continue;
            const Vec3 at = rd.points[pick];
            const Region rg = Region::fromContinent(c, at, 64);
            const Vec3 o = rg.origin();
            const Vec3 dir = audit::unitVec(rd.points[pick + 1] - rd.points[pick - 1]);
            const Vec3 across{-dir.y, dir.x, Fixed::zero()};
            const double hw = rd.widths[pick].toDouble();
            const auto roadAt = [&](double side) {
                const Vec3 p = at + across * Fixed::fromRatio(static_cast<int64_t>(side * 100), 100);
                return rg.groundAt(p.x - o.x, p.y - o.y) == Ground::Road;
            };
            CHECK(roadAt(0));
            CHECK(roadAt(hw * 0.6));
            CHECK(!roadAt(hw + 5.0));
            CHECK(!roadAt(-(hw + 5.0)));
            ++tested;
            break;
        }
    }
    CHECK(tested == 2);
}

TEST(no_rock_stands_on_a_road_and_roads_go_round_the_big_ones) {
    // Across seeds: in a battle window at points all along roads, no boulder or rock overlaps a road's middle line (their
    // footprint is clear of it), and over the continent there are boulders the roads have bent aside for (their edge within a
    // metre of the stone, the stone worth going round).
    for (const uint64_t seed : {uint64_t{20261027}, uint64_t{7}, uint64_t{99}}) {
        const Continent c(seed);
        int windows = 0, rocks = 0, through = 0;
        for (size_t ri = 0; ri < c.roads().size() && windows < 6; ri += 7) {
            const Polyline& rd = c.roads()[ri];
            if (rd.points.size() < 20) continue;
            const Region rg = Region::fromContinent(c, rd.points[rd.points.size() / 2], 64);
            ++windows;
            for (const Obstacle& o : rg.obstacles()) {
                if (o.kind != Obstacle::Kind::Rock && o.kind != Obstacle::Kind::Boulder) continue;
                ++rocks;
                for (const Polyline& lr : rg.roads()) {
                    for (size_t i = 0; i + 1 < lr.points.size(); ++i) {
                        const audit::P a = audit::pt(lr.points[i]), b = audit::pt(lr.points[i + 1]);
                        if (audit::segDist(audit::pt(o.pos), a, b) < o.radius.toDouble()) ++through;
                    }
                }
            }
        }
        std::printf("  seed %llu: %d rocks in %d windows, %d on a road's middle line\n", static_cast<unsigned long long>(seed), rocks, windows, through);
        CHECK(windows > 0);
        CHECK(through == 0);
    }
    // The detours themselves, on the game's continent: stones worth going round with a road's edge just clear of them.
    const Continent& c = continent();
    const uint64_t scenery = Region::sceneryContinentSeed(c.seed());
    int detours = 0;
    for (const Polyline& rd : c.roads()) {
        for (size_t i = 0; i + 1 < rd.points.size(); i += 6) {
            const int64_t gx0 = rd.points[i].x.floorToInt() / 5 - 2, gy0 = rd.points[i].y.floorToInt() / 5 - 2;
            for (int64_t gy = gy0; gy < gy0 + 5; ++gy)
                for (int64_t gx = gx0; gx < gx0 + 5; ++gx) {
                    const Region::SceneryPoint pt = Region::sceneryPoint(scenery, gx, gy);
                    const Continent::Sample s = c.sample(pt.x, pt.y);
                    Fixed surface;
                    if (c.waterSurfaceOf(s, surface)) continue;
                    const Ground g = c.landGround(pt.x, pt.y, s.height, s.mountain, Fixed::fromInt(60));
                    Obstacle o;
                    if (g == Ground::Rock || !Region::sceneryAt(scenery, pt, g, false, s.height, o) || !Region::worthDodging(o)) continue;
                    const audit::P q{pt.x.toDouble(), pt.y.toDouble()};
                    const double gap = audit::segDist(q, audit::pt(rd.points[i]), audit::pt(rd.points[i + 1])) - rd.widths[i].toDouble() - o.radius.toDouble();
                    detours += gap > 0.2 && gap < 1.0;
                }
        }
    }
    std::printf("  %d stones worth going round with a road's edge within a metre of them\n", detours);
    CHECK(detours >= 3);
}

TEST(every_bridge_meets_the_ground_at_both_ends_and_most_can_be_walked_across) {
    // Across seeds: the deck's end is within a step of the ground (a man steps on and off), the bridge reaches dry banks, and
    // a man, a rider and an engine are walked across it, each way, in a World with the real navigation and walking rules.
    int total = 0, passable = 0, stepUp = 0, wet = 0;  // (stepUp: ends more than 1.5 m above the ground; the NavGrid's own bank may differ from the macro land by a little)
    for (const uint64_t seed : {uint64_t{20261027}, uint64_t{7}}) {
        const Continent c(seed);
        for (size_t i = 0; i < c.crossings().size(); ++i) {
            if (!c.crossings()[i].bridged()) continue;
            const audit::BridgeReport b = audit::auditBridge(c, i, 96);
            ++total;
            passable += b.passable();
            for (const double r : b.rise) stepUp += r > 1.5;
            wet += b.waterEnds > 0;
            if (!b.passable() || b.rise[0] > 1.5 || b.rise[1] > 1.5)
                std::printf("  seed %llu bridge %zu (%.0f, %.0f): ends %.2f / %.2f above the ground, foot %d%d rider %d%d engine %d%d\n", static_cast<unsigned long long>(seed), i, b.pos.x.toDouble(),
                            b.pos.y.toDouble(), b.rise[0], b.rise[1], b.walks[0][0], b.walks[0][1], b.walks[1][0], b.walks[1][1], b.walks[2][0], b.walks[2][1]);
        }
    }
    std::printf("  %d of %d bridges walked across; %d deck ends more than a step above the ground; %d ends in water\n", passable, total, stepUp, wet);
    CHECK(total >= 20);
    CHECK(stepUp == 0);
    CHECK(wet <= 6);
    CHECK(passable * 100 >= total * 65);  // (see docs/TERRAIN_AUDIT.md for what keeps the rest from being walked)
}
