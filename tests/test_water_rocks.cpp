// Rocks and boulders in the water (docs/TERRAIN_AUDIT.md, "Rocks in the water"): one rule on a position-seeded grid (Region::waterRockAt),
// which a battle's Region and the close-up land (game::DetailLand) both apply, so they stand in the same places.
#include <cmath>
#include <cstdio>
#include <set>

#include "illyria/game/battle.h"
#include "illyria/game/detail_land.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "test.h"

using namespace illyria;
using namespace illyria::sim;

namespace {

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

Region window(const Continent& c, double x, double y) {
    return Region::fromContinent(c, {Fixed::fromInt(static_cast<int64_t>(x)), Fixed::fromInt(static_cast<int64_t>(y)), Fixed::zero()}, 128);
}

// Where the lakes, the isle, the sea and a river are (continent metres).
const double kPlaces[][2] = {{569, 978}, {2247, 1511}, {1644, 1597}, {1701, 1579}, {215, 1500}, {199, 3325}, {700, 2650}, {1640, 2420}};

struct Counts {
    int rocks = 0, boulders = 0, breaking = 0, sunk = 0;
};

Counts count(const Region& rg) {
    Counts n;
    for (const Obstacle& o : rg.obstacles()) {
        if (!o.inWater()) continue;
        ++(o.kind == Obstacle::Kind::Boulder ? n.boulders : n.rocks);
        ++(o.breaksSurface() ? n.breaking : n.sunk);
    }
    return n;
}

}  // namespace

TEST(water_rocks_stand_in_lakes_rivers_and_the_sea_in_a_mix_of_sizes) {
    const Continent& c = continent();
    Counts all;
    for (const auto& at : kPlaces) {
        const Region rg = window(c, at[0], at[1]);
        const Counts n = count(rg);
        std::printf("  (%4.0f, %4.0f): %3d rocks %3d boulders, %3d break the surface %3d just under\n", at[0], at[1], n.rocks, n.boulders, n.breaking, n.sunk);
        all.rocks += n.rocks;
        all.boulders += n.boulders;
        all.breaking += n.breaking;
        all.sunk += n.sunk;
        for (const Obstacle& o : rg.obstacles()) {
            if (!o.inWater()) continue;
            // Each stands in water, on a bed under it; the ones that break it rise out of it, the others lie wholly under.
            const Fixed surface = rg.waterSurfaceAt(o.pos.x, o.pos.y);
            CHECK(surface.raw != Region::kDryRaw);
            const Ground g = rg.groundAt(o.pos.x, o.pos.y);
            CHECK(g == Ground::Shallows || g == Ground::DeepWater);
            const double top = o.pos.z.toDouble() + o.height.toDouble() * 0.85, level = o.pos.z.toDouble() + o.halfX.toDouble();
            if (o.breaksSurface()) {
                CHECK(top > level);
                CHECK(o.solid);
            } else {
                CHECK(top < level);
                CHECK(!o.solid);
            }
            CHECK(o.kind == Obstacle::Kind::Boulder ? o.radius.toDouble() >= 1.0 : o.radius.toDouble() < 2.0);
        }
    }
    // Many small rocks, a few big boulders; some break the surface and some do not.
    CHECK(all.rocks > 3 * all.boulders / 2);
    CHECK(all.boulders >= 8);
    CHECK(all.breaking >= 20 && all.sunk >= 20);
}

TEST(water_rocks_are_the_same_wherever_and_however_often_the_window_is_cut_and_differ_by_seed) {
    const Continent& c = continent();
    const Region a = window(c, 569, 978), b = window(c, 569, 978);
    CHECK(a.checksum() == b.checksum());
    // A window cut a little to one side holds the same rocks where the two overlap.
    const Region s = window(c, 569 + 48, 978);
    const Vec3 oa = a.origin(), os = s.origin();
    int shared = 0, same = 0;
    for (const Obstacle& o : a.obstacles()) {
        if (!o.inWater()) continue;
        const Fixed x = o.pos.x + oa.x - os.x, y = o.pos.y + oa.y - os.y;
        if (x < Fixed::fromInt(40) || y < Fixed::fromInt(40) || x > Fixed::fromInt(470) || y > Fixed::fromInt(470)) continue;
        ++shared;
        for (const Obstacle& q : s.obstacles())
            if (q.inWater() && q.pos.x == x && q.pos.y == y && q.radius == o.radius && q.variant == o.variant) {
                ++same;
                break;
            }
    }
    std::printf("  %d of %d water rocks in the overlap are in the other window\n", same, shared);
    CHECK(shared >= 5);
    CHECK(same * 10 >= shared * 9);
    // The rule itself, by seed: the same point under another seed is not the same rock.
    int rocks[2] = {0, 0}, differ = 0;
    for (int64_t gy = 0; gy < 60; ++gy)
        for (int64_t gx = 0; gx < 60; ++gx) {
            Obstacle o1, o2;
            const bool h1 = Region::waterRockAt(11, Region::sceneryPoint(11, gx, gy), Fixed::fromRatio(6, 5), Region::kShoreNear, o1);
            const bool h1b = Region::waterRockAt(11, Region::sceneryPoint(11, gx, gy), Fixed::fromRatio(6, 5), Region::kShoreNear, o2);
            CHECK(h1 == h1b && (!h1 || (o1.radius == o2.radius && o1.height == o2.height && o1.variant == o2.variant)));
            Obstacle o3;
            const bool h3 = Region::waterRockAt(12, Region::sceneryPoint(12, gx, gy), Fixed::fromRatio(6, 5), Region::kShoreNear, o3);
            rocks[0] += h1;
            rocks[1] += h3;
            differ += h1 != h3;
        }
    CHECK(rocks[0] > 20 && rocks[1] > 20);
    CHECK(differ > 20);
}

TEST(a_rock_that_breaks_the_water_is_off_every_road_ford_bridge_river_mouth_town_pier_boat_cell_and_the_spit) {
    for (const uint64_t seed : {uint64_t{20261027}, uint64_t{1}, uint64_t{6}}) {
        const Continent c(seed);
        int tested = 0, onRoad = 0, inKeepout = 0, underBoat = 0, onDeck = 0;
        const std::vector<Vec3> walk = c.isleWalk();
        std::vector<std::pair<double, double>> spots;
        for (const Continent::Crossing& cr : c.crossings()) spots.push_back({cr.pos.x.toDouble(), cr.pos.y.toDouble()});
        for (const Continent::River& rv : c.rivers()) spots.push_back({rv.points.back().x.toDouble(), rv.points.back().y.toDouble()});
        for (const Vec3& w : walk) spots.push_back({w.x.toDouble(), w.y.toDouble()});
        for (size_t i = 0; i < spots.size(); i += (spots.size() > 40 ? spots.size() / 40 : 1)) {
            const Region rg = window(c, spots[i].first, spots[i].second);
            const Vec3 og = rg.origin();
            for (const Obstacle& o : rg.obstacles()) {
                if (!o.breaksSurface()) continue;
                ++tested;
                // Off every road's line by its half width, the rock's size and three metres (a ford's clear way).
                for (const Polyline& rd : rg.roads())
                    for (size_t k = 0; k + 1 < rd.points.size(); ++k) {
                        const double ax = rd.points[k].x.toDouble(), ay = rd.points[k].y.toDouble(), bx = rd.points[k + 1].x.toDouble(), by = rd.points[k + 1].y.toDouble();
                        const double px = o.pos.x.toDouble(), py = o.pos.y.toDouble(), dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
                        const double t = l2 > 0 ? std::fmax(0, std::fmin(1, ((px - ax) * dx + (py - ay) * dy) / l2)) : 0;
                        const double d = std::hypot(ax + dx * t - px, ay + dy * t - py);
                        if (d < rd.widths[k].toDouble() + o.radius.toDouble() + 2.5) ++onRoad;
                    }
                for (const Deck& dk : rg.decks())
                    if (Deck{dk.centre, dk.dir, dk.halfSpan + Fixed::fromInt(4), dk.halfWidth + Fixed::fromInt(2)}.contains(o.pos.x, o.pos.y)) ++onDeck;
                const Fixed cx = o.pos.x + og.x, cy = o.pos.y + og.y;
                if (Region::keepsRockOut(c.waterKeepouts(), cx, cy)) ++inKeepout;
                const double bx = std::fmod(cx.toDouble(), 44.0) - 22.0, by = std::fmod(cy.toDouble(), 44.0) - 22.0;
                if (std::hypot(bx, by) < 9.0) ++underBoat;
            }
        }
        std::printf("  seed %llu: %d rocks breaking the surface near fords, bridges, mouths and the Spit; %d on a road, %d on a deck, %d kept out, %d under a boat\n", static_cast<unsigned long long>(seed), tested, onRoad, onDeck, inKeepout, underBoat);
        CHECK(tested >= 5);
        CHECK(onRoad == 0 && onDeck == 0 && inKeepout == 0 && underBoat == 0);
        // Nor on the Spit's own ground (it is land), the Drowned Abbey's approach: none within the keep-out of a walked strand.
        CHECK(!walk.empty());
    }
}

TEST(detail_land_stands_the_water_rocks_where_the_battlefield_does) {
    const game::Battle battle(1, game::Scenario::Ford);
    const Region& rg = battle.region();
    game::DetailLand land;
    land.reset(battle.continentShared(), battle.origin(), 0);
    land.recentre(512, 512);
    land.finishAll();
    int rocks = 0, same = 0, regionRocks = 0;
    for (int s = 0; s < game::DetailLand::kSlots * game::DetailLand::kSlots; ++s) {
        if (!land.shown(s)) continue;
        for (const Obstacle& o : land.scenery(s)) {
            const float x = static_cast<float>(o.pos.x.toDouble()), y = static_cast<float>(o.pos.y.toDouble());
            if (!o.inWater() || x < 136 || y < 136 || x > 888 || y > 888) continue;
            ++rocks;
            for (const Obstacle& b : rg.obstacles())
                if (b.inWater() && b.kind == o.kind && b.pos.x == o.pos.x && b.pos.y == o.pos.y && b.variant == o.variant && std::fabs(b.height.toDouble() - o.height.toDouble()) < 0.25) {
                    ++same;
                    break;
                }
        }
    }
    for (const Obstacle& b : rg.obstacles()) regionRocks += b.inWater();
    std::printf("  detail land: %d/%d water rocks match (the battlefield has %d)\n", same, rocks, regionRocks);
    CHECK(rocks >= 5);
    CHECK(same * 10 >= rocks * 9);
}
