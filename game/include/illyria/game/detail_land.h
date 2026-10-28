#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "illyria/game/gmath.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/fixed.h"
#include "illyria/sim/region.h"

namespace illyria::game {

// The land close around the camera at the battlefield's own resolution (4 m
// cells), anywhere on the continent: heights, ground kinds, road ruts, tree
// shade and water, sampled the way sim::Region::fromContinent cuts a
// battlefield out of the continent, so the ground looks the same as a battle
// fought there later. Presentation only: nothing here feeds the simulation.
//
// The land is built in chunks of 16 x 16 cells, a little at a time (work()
// stops when its time is up), nearest the camera first. Chunks are stored
// toroidally: the chunk at (cx, cy) lives in slot (cx mod kSlots, cy mod
// kSlots), and its samples at (sample index mod kSamples), so moving the
// window only rebuilds the chunks that came into view and the textures made
// from the arrays never move. The outer ring of slots only holds raw samples
// for the shown chunks' edges (roads are smoothed over 5 samples, mud looks
// two samples out); only the inner kShown x kShown can be finished and shown.
class DetailLand {
public:
    static constexpr int kChunk = 16;                  // cells per chunk side
    static constexpr float kCell = 4.0f;               // metres, as Battle::kCellSize
    static constexpr float kChunkSize = kChunk * kCell;
    static constexpr int kSlots = 14;                  // chunks per side held
    static constexpr int kShown = kSlots - 2;          // chunks per side that can be shown
    static constexpr int kSamples = kChunk * kSlots;   // texture period, in samples (and cells)
    static constexpr int kFine = kSamples * 2;         // road and canopy period, in 2 m texels

    // `origin`: continent coordinates of the battlefield's (0, 0); `hole`
    // cells from there are the battlefield itself, never built here.
    void reset(std::shared_ptr<const sim::Continent> continent, V3 origin, int hole);
    bool active() const { return continent_ != nullptr; }
    // Centres the window on battle-local (x, y), with some slack.
    void recentre(float x, float y);
    // Builds for up to `budgetMs` of wall time; returns the slots finished.
    std::vector<int> work(double budgetMs);
    // Runs until everything in the window is finished (tests, tools).
    void finishAll();

    // Which chunk a slot holds, and whether it is shown (finished and inside the window).
    struct Slot {
        int cx = INT32_MIN, cy = INT32_MIN;
        int rawRows = 0;  // rows of raw samples done (kChunk = all)
        int step = 0;     // finishing steps done
        bool finished = false;
        bool wet = false;  // any water in the chunk
    };
    const Slot& slot(int s) const { return slots_[static_cast<size_t>(s)]; }
    bool shown(int s) const;
    static int slotOf(int cx, int cy) { return wrap(cy, kSlots) * kSlots + wrap(cx, kSlots); }
    // The window's chunks: [x0, x0 + kSlots) x [y0, y0 + kSlots); shown ones are inside by one.
    int windowX() const { return wx_; }
    int windowY() const { return wy_; }
    // Bumped whenever the shown set changes.
    uint32_t generation() const { return generation_; }

    // What the renderer uploads, in the same toroidal layout as its textures
    // (index (y mod period) * period + (x mod period); x, y in samples or texels
    // from the battlefield's (0, 0)).
    const std::vector<float>& heights() const { return height_; }    // kSamples^2, shaped ground
    const std::vector<uint8_t>& ground() const { return ground_; }   // kSamples^2, sim::Ground * 16 per cell
    const std::vector<float>& water() const { return water_; }       // kSamples^2, surface or -1e6 where dry
    const std::vector<uint8_t>& flow() const { return flow_; }       // kSamples^2 * 2, downstream direction
    const std::vector<uint8_t>& road() const { return road_; }       // kFine^2 * 2, as the battlefield's road field
    const std::vector<uint8_t>& canopy() const { return canopy_; }   // kFine^2, tree shade
    static int wrap(int v, int n) { return ((v % n) + n) % n; }

    // The ground height at battle-local (x, y), if its chunk is finished.
    bool heightAt(float x, float y, float& z, bool unchecked = false) const;
    // A slot's trees, undergrowth, rocks and logs (battle-local), once finished.
    const std::vector<sim::Obstacle>& scenery(int s) const { return scenery_[static_cast<size_t>(s)]; }
    // Road and tree-shade texels written beyond a chunk's own.
    static constexpr int kFineApron = 4;
    struct Seg {
        float ax, ay, bx, by;
    };

private:
    struct Raw {
        sim::Fixed h, roadDist, mountain;
        sim::Fixed water;  // sim::Region::kDryRaw where dry
    };
    void claim(int cx, int cy);
    void rawRow(int s);
    bool finishStep(int s);  // true when the chunk is finished
    void growScenery(int s, const std::vector<Seg>& roads);
    bool inHole(int cx, int cy) const;
    const Raw& raw(int sx, int sy) const { return raw_[static_cast<size_t>(wrap(sy, kSamples)) * kSamples + wrap(sx, kSamples)]; }
    sim::Fixed shapedAt(int sx, int sy) const;

    std::shared_ptr<const sim::Continent> continent_;
    sim::Fixed ox_, oy_;  // the battlefield's origin, whole metres
    int hole_ = 0;
    uint64_t detailSeed_ = 0, mudSeed_ = 0, scenerySeed_ = 0;
    std::vector<std::vector<sim::Obstacle>> scenery_;  // per slot
    int wx_ = INT32_MIN / 2, wy_ = INT32_MIN / 2;
    uint32_t generation_ = 0;
    std::vector<Slot> slots_;
    std::vector<std::vector<sim::Continent::Sample>> lattice_;  // per slot: landform every 16 m (5 x 5)
    std::vector<Raw> raw_;
    float focusX_ = 0, focusY_ = 0;
    int finishing_ = -1;              // the slot being finished, whose shaped heights are in shaped_
    std::vector<sim::Fixed> shaped_;  // shaped heights of samples -1..17 around that chunk
    std::vector<sim::Deck> decks_;    // bridge decks near it
    std::vector<float> height_, water_;
    std::vector<uint8_t> ground_, flow_, road_, canopy_;
};

}  // namespace illyria::game
