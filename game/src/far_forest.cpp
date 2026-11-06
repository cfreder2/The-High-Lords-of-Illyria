#include "illyria/game/far_forest.h"

#include <algorithm>
#include <cmath>

namespace illyria::game {

namespace {
using sim::Fixed;
using sim::Ground;
}  // namespace

void FarForest::grow(const sim::Continent& c, const FarGrid& land, const V3& origin) {
    const uint64_t seed = sim::Region::sceneryContinentSeed(c.seed());
    const int64_t step = sim::Region::kSceneryStep;
    const int64_t width = sim::Continent::kWidth, height = sim::Continent::kHeight;
    const Fixed ox = Fixed::fromInt(static_cast<int64_t>(origin.x)), oy = Fixed::fromInt(static_cast<int64_t>(origin.y));
    min_ = {-origin.x, -origin.y, 0};
    densityX_ = static_cast<int>(std::ceil(static_cast<float>(width) / kDensityCell));
    densityY_ = static_cast<int>(std::ceil(static_cast<float>(height) / kDensityCell));
    trees_.clear();
    std::vector<float> cover(static_cast<size_t>(densityX_) * densityY_, 0.0f);
    // No wood grows inside a town's walls.
    struct Town {
        float x, y, reach;
    };
    std::vector<Town> towns;
    for (const sim::Continent::City& city : c.cities())
        towns.push_back({static_cast<float>(city.pos.x.toDouble()), static_cast<float>(city.pos.y.toDouble()), static_cast<float>(city.radius.toDouble()) * 1.25f});
    for (int64_t gy = 0; gy * step < height; ++gy) {
        for (int64_t gx = 0; gx * step < width; ++gx) {
            const sim::Region::SceneryPoint pt = sim::Region::sceneryPoint(seed, gx, gy);
            if (pt.x < Fixed::zero() || pt.y < Fixed::zero() || pt.x >= Fixed::fromInt(width) || pt.y >= Fixed::fromInt(height)) continue;
            const float lx = static_cast<float>((pt.x - ox).toDouble()), ly = static_cast<float>((pt.y - oy).toDouble());
            const int ci = std::clamp(static_cast<int>(std::lround((lx - land.min.x) / land.cell)), 0, land.nx - 1);
            const int cj = std::clamp(static_cast<int>(std::lround((ly - land.min.y) / land.cell)), 0, land.ny - 1);
            const Ground g = static_cast<Ground>(land.ground[static_cast<size_t>(cj) * land.nx + ci]);
            if (g == Ground::Road || g == Ground::Bridge || g == Ground::Shallows || g == Ground::DeepWater) continue;
            const Fixed z = Fixed::fromRaw(static_cast<int64_t>(std::llround(static_cast<double>(land.heightAt(lx, ly)) * Fixed::kOneRaw)));
            sim::Obstacle o;
            if (!sim::Region::sceneryAt(seed, pt, g, false, z, o)) continue;
            if (o.kind != sim::Obstacle::Kind::Pine && o.kind != sim::Obstacle::Kind::Oak) continue;
            o.pos = {pt.x - ox, pt.y - oy, z};
            const float x = static_cast<float>(pt.x.toDouble()), y = static_cast<float>(pt.y.toDouble());
            bool inTown = false;
            for (const Town& t : towns) inTown = inTown || std::hypot(x - t.x, y - t.y) < t.reach;
            if (inTown) continue;
            trees_.push_back(o);
            // The crown's cover of the 8 m texels round it.
            const float crown = o.kind == sim::Obstacle::Kind::Pine ? 2.6f : 3.6f, area = 3.14159f * crown * crown / (kDensityCell * kDensityCell);
            const int dx = std::min(densityX_ - 1, static_cast<int>(x / kDensityCell)), dy = std::min(densityY_ - 1, static_cast<int>(y / kDensityCell));
            cover[static_cast<size_t>(dy) * densityX_ + dx] += area;
        }
    }
    // Softened over the 3 x 3 texels round each (a wood's cover is a mass, not a texel's luck of the draw).
    density_.resize(cover.size());
    for (int y = 0; y < densityY_; ++y) {
        for (int x = 0; x < densityX_; ++x) {
            float sum = 0;
            for (int k = 0; k < 9; ++k) sum += cover[static_cast<size_t>(std::clamp(y + k / 3 - 1, 0, densityY_ - 1)) * densityX_ + std::clamp(x + k % 3 - 1, 0, densityX_ - 1)];
            density_[static_cast<size_t>(y) * densityX_ + x] = static_cast<uint8_t>(std::min(1.0f, sum / 9.0f * 1.5f) * 255.0f + 0.5f);
        }
    }
}

float FarForest::densityAt(float x, float y) const {
    const int ix = static_cast<int>(std::floor((x - min_.x) / kDensityCell)), iy = static_cast<int>(std::floor((y - min_.y) / kDensityCell));
    if (ix < 0 || iy < 0 || ix >= densityX_ || iy >= densityY_) return 0;
    return static_cast<float>(density_[static_cast<size_t>(iy) * densityX_ + ix]) / 255.0f;
}

}  // namespace illyria::game
