#pragma once

#include <cstdint>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/gmath.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"

namespace illyria::game {

// Every tree of the whole continent, by the one rule that grows trees anywhere: sim::Region::sceneryAt on the
// continent's 5 m scenery grid (a battle's trees, the close-up land's and these are the same trees where they
// stand), for what the camera sees from afar, where the close-up land is not built. The ground under each is the
// continent's own (Continent::landGround at the tree's own point), so what grows where is what the cover rules of
// the simulation call forest. Presentation only: nothing here feeds the simulation.
//
// The trees' crowns are summed into a density map (kDensityCell metres a texel) that the far land shades from: the
// forest read from so high up that a tree is a speck.
class FarForest {
public:
    static constexpr float kDensityCell = 8.0f;

    // Grows the trees of the whole continent, in battle-local coordinates (`origin`: the battlefield's corner), on
    // the ground and heights of `land`, the continent every 16 m (the coarse land's own, which the trees stand on).
    void grow(const sim::Continent& continent, const FarGrid& land, const V3& origin);

    const std::vector<sim::Obstacle>& trees() const { return trees_; }
    V3 min() const { return min_; }
    // Crown cover, 0 to 255, densityX() x densityY() texels from min().
    int densityX() const { return densityX_; }
    int densityY() const { return densityY_; }
    const std::vector<uint8_t>& density() const { return density_; }
    // The cover at battle-local (x, y), 0 to 1.
    float densityAt(float x, float y) const;

private:
    V3 min_;
    int densityX_ = 0, densityY_ = 0;
    std::vector<sim::Obstacle> trees_;
    std::vector<uint8_t> density_;
};

}  // namespace illyria::game
