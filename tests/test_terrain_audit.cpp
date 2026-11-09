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
