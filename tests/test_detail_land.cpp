#include <chrono>
#include <algorithm>
#include <cmath>
#include <set>

#include "illyria/game/battle.h"
#include "illyria/game/detail_land.h"
#include "illyria/game/far_forest.h"
#include "test.h"

using namespace illyria;

// The close-up land drawn off the battlefield is sampled the way a battlefield
// is cut out of the continent: built over the Ford's own window (with no
// hole), it matches the battlefield's heights and ground away from the things
// only a battle adds (ponds, the ford's berm).
TEST(detail_land_matches_the_battlefield) {
    const game::Battle battle(1, game::Scenario::Ford);
    const sim::Region& rg = battle.region();
    game::DetailLand land;
    land.reset(battle.continentShared(), battle.origin(), 0);
    land.recentre(512, 512);
    land.finishAll();
    const int n = rg.terrain().cells();
    const std::vector<sim::Fixed>& samples = rg.terrain().samples();
    const auto special = [&](float x, float y) {
        for (const sim::Vec3& p : rg.ponds()) {
            const float dx = x - static_cast<float>(p.x.toDouble()), dy = y - static_cast<float>(p.y.toDouble());
            if (std::hypot(dx, dy) < static_cast<float>(p.z.toDouble()) + 16) return true;
        }
        const sim::Vec3 f = rg.ford();
        return std::hypot(x - static_cast<float>(f.x.toDouble()), y - static_cast<float>(f.y.toDouble())) < 120;
    };
    int heights = 0, heightsSame = 0, cells = 0, cellsSame = 0;
    float worst = 0;
    // The window's shown chunks cover 128..896 m.
    for (int j = 34; j < 222; ++j) {
        for (int i = 34; i < 222; ++i) {
            const float x = static_cast<float>(i) * 4, y = static_cast<float>(j) * 4;
            if (special(x, y)) continue;
            float z = 0;
            CHECK(land.heightAt(x, y, z));
            const float want = static_cast<float>(samples[static_cast<size_t>(j) * (n + 1) + i].toDouble());
            ++heights;
            if (std::fabs(z - want) < 1e-3f) ++heightsSame;
            worst = std::max(worst, std::fabs(z - want));
            const int g = land.ground()[static_cast<size_t>(j) * game::DetailLand::kSamples + i] / 16;
            ++cells;
            if (g == static_cast<int>(rg.cellGround(i, j))) ++cellsSame;
        }
    }
    std::printf("detail land: %d/%d heights, %d/%d cells match; worst %.2f m\n", heightsSame, heights, cellsSame, cells, worst);
    // The same trees, undergrowth and rocks in the same places.
    int trees = 0, treesSame = 0;
    for (int s = 0; s < game::DetailLand::kSlots * game::DetailLand::kSlots; ++s) {
        if (!land.shown(s)) continue;
        for (const sim::Obstacle& o : land.scenery(s)) {
            const float x = static_cast<float>(o.pos.x.toDouble()), y = static_cast<float>(o.pos.y.toDouble());
            if (x < 136 || y < 136 || x > 888 || y > 888 || special(x, y)) continue;
            ++trees;
            for (const sim::Obstacle& b : rg.obstacles()) {
                if (b.kind == o.kind && b.pos.x == o.pos.x && b.pos.y == o.pos.y && b.height == o.height) {
                    ++treesSame;
                    break;
                }
            }
        }
    }
    std::printf("detail land: %d/%d trees, bushes and rocks match\n", treesSame, trees);
    CHECK(trees > 2000);
    CHECK(treesSame > trees * 97 / 100);
    CHECK(heights > 20000);
    CHECK(heightsSame > heights * 97 / 100);
    CHECK(cellsSame > cells * 97 / 100);
}

// Building happens a slice at a time, and only what the window shows is kept.
TEST(detail_land_builds_incrementally) {
    const game::Battle battle(1, game::Scenario::Ford);
    game::DetailLand land;
    land.reset(battle.continentShared(), battle.origin(), game::Battle::kTerrainCells);
    land.recentre(-2400, 500);
    int frames = 0, finished = 0;
    const auto start = std::chrono::steady_clock::now();
    while (finished < game::DetailLand::kShown * game::DetailLand::kShown && frames < 100000) {
        finished += static_cast<int>(land.work(1.0).size());
        ++frames;
    }
    std::printf("detail land: %d chunks in %d slices of 1 ms (%.0f ms)\n", finished, frames,
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    CHECK(finished == game::DetailLand::kShown * game::DetailLand::kShown);
    CHECK(frames > 20);  // a little each frame, not all at once
    int shown = 0;
    for (int s = 0; s < game::DetailLand::kSlots * game::DetailLand::kSlots; ++s) shown += land.shown(s) ? 1 : 0;
    CHECK(shown == finished);
    // Moving a few chunks over keeps what is still in view.
    land.recentre(-2400 + 3 * game::DetailLand::kChunkSize, 500);
    shown = 0;
    for (int s = 0; s < game::DetailLand::kSlots * game::DetailLand::kSlots; ++s) shown += land.shown(s) ? 1 : 0;
    CHECK(shown >= (game::DetailLand::kShown - 4) * game::DetailLand::kShown);
    // Next to the battlefield nothing is built inside it.
    land.recentre(0, 0);
    land.finishAll();
    for (int s = 0; s < game::DetailLand::kSlots * game::DetailLand::kSlots; ++s) {
        const game::DetailLand::Slot& sl = land.slot(s);
        if (land.shown(s)) CHECK(!(sl.cx >= 0 && sl.cy >= 0 && sl.cx < 16 && sl.cy < 16));
    }
}

// What you see is what the rules use: the trees drawn from afar (FarForest) are grown by the rule that grows a
// battle's trees, so forest ground (the ground the simulation's cover, gait and archery rules call forest) is
// crowded with them and plains have none to speak of, the crown cover map the land is shaded from follows them,
// and the battlefield's own trees are among them.
TEST(far_forest_is_the_cover_the_rules_use) {
    const game::Battle battle(1, game::Scenario::Ford);
    const game::FarGrid& land = battle.farGrid();
    game::FarForest forest;
    forest.grow(battle.continent(), land, battle.origin());
    CHECK(forest.trees().size() > 5000);
    std::vector<int> perCell(land.ground.size(), 0);
    for (const sim::Obstacle& o : forest.trees()) {
        CHECK(o.kind == sim::Obstacle::Kind::Pine || o.kind == sim::Obstacle::Kind::Oak);
        const int i = static_cast<int>(std::lround((o.pos.x.toDouble() - land.min.x) / land.cell)), j = static_cast<int>(std::lround((o.pos.y.toDouble() - land.min.y) / land.cell));
        const sim::Ground g = static_cast<sim::Ground>(land.ground[static_cast<size_t>(j) * land.nx + i]);
        CHECK(g != sim::Ground::DeepWater && g != sim::Ground::Shallows && g != sim::Ground::Road && g != sim::Ground::Bridge);
        ++perCell[static_cast<size_t>(j) * land.nx + i];
    }
    double forestTrees = 0, plainTrees = 0, forestCover = 0, plainCover = 0;
    int forestCells = 0, plainCells = 0;
    for (int j = 1; j < land.ny - 1; ++j) {
        for (int i = 1; i < land.nx - 1; ++i) {
            const size_t k = static_cast<size_t>(j) * land.nx + i;
            const sim::Ground g = static_cast<sim::Ground>(land.ground[k]);
            const float cover = forest.densityAt(land.min.x + static_cast<float>(i) * land.cell, land.min.y + static_cast<float>(j) * land.cell);
            if (g == sim::Ground::Forest) ++forestCells, forestTrees += perCell[k], forestCover += cover;
            if (g == sim::Ground::Plains) ++plainCells, plainTrees += perCell[k], plainCover += cover;
        }
    }
    CHECK(forestCells > 100 && plainCells > 1000);
    std::printf("far forest: %zu trees; %.2f a forest cell, %.2f a plains cell; cover %.2f against %.2f\n", forest.trees().size(), forestTrees / forestCells,
                plainTrees / plainCells, forestCover / forestCells, plainCover / plainCells);
    CHECK(forestTrees / forestCells > 2.0);
    CHECK(plainTrees / plainCells < forestTrees / forestCells / 5);
    CHECK(forestCover / forestCells > 4 * plainCover / plainCells);
    // The battlefield's own trees (the sim's obstacles) are among the continent's, but where the coarse ground
    // and the battle's own differ (roads, shores).
    int own = 0, shared = 0;
    std::set<std::pair<int64_t, int64_t>> far;
    for (const sim::Obstacle& o : forest.trees()) far.insert({o.pos.x.raw, o.pos.y.raw});
    for (const sim::Obstacle& o : battle.region().obstacles()) {
        if (o.kind != sim::Obstacle::Kind::Pine && o.kind != sim::Obstacle::Kind::Oak) continue;
        ++own;
        shared += far.count({o.pos.x.raw, o.pos.y.raw}) ? 1 : 0;
    }
    std::printf("far forest: %d of the battlefield's %d trees are in it\n", shared, own);
    CHECK(own > 100);
    CHECK(shared * 10 > own * 8);
}
