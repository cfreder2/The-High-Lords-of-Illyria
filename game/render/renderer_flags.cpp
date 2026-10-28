// Flags: the poles with banners on them (a siege's capture point, the
// banners rising over the campaign's towns), the castles' tower banners in
// their holders' colours, and the campaign's territory texture.
#include <algorithm>
#include <cmath>

#include "renderer.h"

namespace illyria::game {

namespace {
// The banners fly downwind (the battle's wind blows from the north-west).
constexpr float kFlyYaw = -0.785398f;

void push(std::vector<float>& out, const V3& pos, float yaw, const V3& scale, const V3& tint) {
    out.insert(out.end(), {pos.x, pos.y, pos.z, 0.0f, 0.0f, std::sin(yaw / 2), std::cos(yaw / 2), scale.x, scale.y, scale.z, tint.x, tint.y, tint.z});
}
}  // namespace

void Renderer::setFlags(const std::vector<Flag>& flags) {
    std::vector<float> poles, cloths;
    for (const Flag& f : flags) {
        const float r = std::max(0.08f, f.height * 0.012f);
        push(poles, f.base, 0, {r, r, f.height}, {1, 1, 1});
        // The cloth hangs from its top edge: at the finial when fully raised.
        const float drop = f.size * 0.64f;
        const float z = drop + (f.height - drop) * std::clamp(f.raised, 0.0f, 1.0f);
        push(cloths, f.base + V3{0, 0, z}, kFlyYaw, {f.size, f.size, f.size}, f.colour);
    }
    uploadMeshInstances(poles_, poles);
    uploadMeshInstances(cloths_, cloths);
}

void Renderer::setCityColours(const std::vector<V3>& colours) {
    // Each banner instance is 13 floats, its tint the last three.
    for (size_t k = 0; k < bannerCity_.size() && (k + 1) * 13 <= bannerData_.size(); ++k) {
        if (bannerCity_[k] >= colours.size()) continue;
        const V3& c = colours[bannerCity_[k]];
        bannerData_[k * 13 + 10] = c.x;
        bannerData_[k * 13 + 11] = c.y;
        bannerData_[k * 13 + 12] = c.z;
    }
    uploadMeshInstances(banners_, bannerData_);
}

void Renderer::setTerritory(const std::vector<uint8_t>& rgba, int cols, int rows, float strength) {
    territoryInfo_[2] = strength;
    if (rgba.size() < static_cast<size_t>(cols) * rows * 4 || cols <= 0 || rows <= 0) return;
    if (!territoryTex_) {
        glGenTextures(1, &territoryTex_);
        glBindTexture(GL_TEXTURE_2D, territoryTex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, territoryTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cols, rows, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
}

}  // namespace illyria::game
