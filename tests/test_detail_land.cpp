#include <chrono>
#include <cmath>

#include "illyria/game/battle.h"
#include "illyria/game/detail_land.h"
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
