// The far land's field texture: what the 16 m mesh cannot say about the shore and the relief, sampled smoothly per pixel.
// The coarse land used to take its waterline from the mesh's vertices (a value interpolated across each triangle, so a coast
// stepped in 16 m blocks and 45 degree diagonals). The same samples, read through a cubic filter in the shader (farField in
// kCommon), put the waterline where the shore really lies between them and give the lakes a depth that shelves. Deterministic:
// built from the FarGrid the sim's continent fills at load, nothing here feeds back.
//   R  the water, as Renderer::buildLand's vertices carry it: 1 + depth / 12 where wet, 1 less the bank's height over the water
//      next door / 12 on dry land beside it (the waterline is 1)
//   G  the ground's height in metres (the bed where wet): the cubic filter's smooth height, for the snow line and the
//      rock's strata, which the mesh's triangles faceted
//   B  the ground's convexity: its height over the average of its neighbours two cells away, / 12 m, -1 to 1 (ridges
//      positive, ravines negative), for the hillshade's relief
//   A  rock less swamp (the mesh's kinds, which the cubic filter smooths: a ground is one or the other)
#include <algorithm>
#include <cmath>
#include <vector>

#include "renderer.h"

namespace illyria::game {

void Renderer::uploadFarField(const Battle& battle) {
    const FarGrid& g = battle.farGrid();
    const int nx = g.nx, ny = g.ny;
    if (nx < 3 || ny < 3) return;
    const auto at = [&](int i, int j) { return static_cast<size_t>(std::clamp(j, 0, ny - 1)) * nx + static_cast<size_t>(std::clamp(i, 0, nx - 1)); };
    const auto wetAt = [&](int i, int j) {
        const auto gr = static_cast<sim::Ground>(g.ground[at(i, j)]);
        return gr == sim::Ground::DeepWater || gr == sim::Ground::Shallows;
    };
    const auto groundAt = [&](int i, int j) { return g.height[at(i, j)] - (wetAt(i, j) ? g.depth[at(i, j)] : 0.0f); };
    std::vector<float> px(static_cast<size_t>(nx) * ny * 4, 0.0f);
    for (int j = 0; j < ny; ++j)
        for (int i = 0; i < nx; ++i) {
            float w;
            if (wetAt(i, j)) {
                w = 1.0f + std::clamp(g.depth[at(i, j)] / 12.0f, 0.0f, 1.0f);
            } else {
                float surface = -1e6f;
                for (int dj = -1; dj <= 1; ++dj)
                    for (int di = -1; di <= 1; ++di) {
                        const int a = i + di, b = j + dj;
                        if (a >= 0 && b >= 0 && a < nx && b < ny && wetAt(a, b)) surface = std::max(surface, g.height[at(a, b)]);
                    }
                w = surface < -1e5f ? 0.0f : std::clamp(1.0f - (g.height[at(i, j)] - surface) / 12.0f, 0.0f, 0.999f);
            }
            const float h = groundAt(i, j);
            const float around = (groundAt(i - 2, j) + groundAt(i + 2, j) + groundAt(i, j - 2) + groundAt(i, j + 2)) * 0.25f;
            float* o = &px[at(i, j) * 4];
            o[0] = w;
            o[1] = h;
            o[2] = std::clamp((h - around) / 12.0f, -1.0f, 1.0f);
            const auto gr = static_cast<sim::Ground>(g.ground[at(i, j)]);
            o[3] = gr == sim::Ground::Rock ? 1.0f : gr == sim::Ground::Swamp ? -1.0f : 0.0f;
        }
    if (!farFieldTex_) glGenTextures(1, &farFieldTex_);
    glBindTexture(GL_TEXTURE_2D, farFieldTex_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, nx, ny, 0, GL_RGBA, GL_FLOAT, px.data());
    // The rectangle the field covers (sample 0 .. sample n - 1), one cell inside: min xy, 1 / cell, and the cell.
    farFieldRect_[0] = g.min.x;
    farFieldRect_[1] = g.min.y;
    farFieldRect_[2] = 1.0f / g.cell;
    farFieldRect_[3] = g.cell;
}

}  // namespace illyria::game
