// The campaign's soldiers on the land (campaign_soldiers.h): from high up each
// is a dot in his side's colour; the soldiers near the camera are the battle's
// own sprites, in their formation and walking. Which is which depends on the
// camera, but only the drawing does: where every man stands comes from the
// campaign's state alone.
#include <algorithm>
#include <cmath>

#include "illyria/game/factions.h"
#include "renderer.h"

namespace illyria::game {

namespace {
constexpr float kDecalShadow = 1, kDecalRing = 2;  // as renderer.cpp
// Within this distance of the camera (and this far of the view's centre, in
// multiples of it) soldiers are sprites; beyond, dots. A man is a few pixels
// tall at this height, so little is lost by the change.
constexpr float kSpriteBelow = 380.0f, kSpriteReach = 1.9f;
// Sprite sheets baked in one frame: baking is the slow part of a first look.
constexpr size_t kBakesPerFrame = 1;
// A dot is about this wide on the ground, between these sizes in CSS pixels.
constexpr float kDotMetres = 1.1f, kDotMinPx = 2.6f, kDotMaxPx = 7.0f;

int sideOf(uint8_t owner) { return owner < sim::Continent::kFactionCount ? owner : 8; }

float hash01(uint32_t v) {
    v *= 2654435761u;
    v ^= v >> 15;
    v *= 2246822519u;
    v ^= v >> 13;
    return static_cast<float>(v & 0xFFFFu) / 65535.0f;
}
}  // namespace

void Renderer::setMapSoldiers(const std::vector<MapSoldier>& soldiers, float seconds) {
    mapSoldiers_ = soldiers;
    mapSeconds_ = seconds;
    mapOn_ = !soldiers.empty();
    if (!mapOn_) {
        mapPlaces_.clear();
        mapSprites_.clear();
        dots_.count = 0;
    }
}

// Where each soldier stands on the ground (battle-local), which are near enough
// to be sprites, and the sprite sheets they need.
void Renderer::placeMapSoldiers(const Battle& battle, const Camera& camera) {
    const V3 o = battle.origin();
    mapPlaces_.resize(mapSoldiers_.size());
    mapShown_.assign(mapSoldiers_.size(), 0);
    mapSprites_.clear();
    const M4 vp = camera.viewProjection();
    // Within the screen and a tenth beyond it (a man's picture reaches past his feet).
    const auto onScreen = [&](const V3& p) {
        const float x = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12], y = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13],
                    w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
        return w > 0 && std::fabs(x) <= w * 1.1f && std::fabs(y) <= w * 1.1f;
    };
    const bool near = camera.distance() < kSpriteBelow;
    const V3 at = camera.target();
    const float reach = camera.distance() * kSpriteReach;
    std::vector<sim::UnitType> need;
    for (size_t i = 0; i < mapSoldiers_.size(); ++i) {
        const MapSoldier& m = mapSoldiers_[i];
        V3 p{m.x - o.x, m.y - o.y, 0};
        p.z = battle.heightAt(p.x, p.y);
        if (m.aloft) p.z += static_cast<float>(sim::unitStats(static_cast<sim::UnitType>(m.type)).flyAltitude.toDouble());
        mapPlaces_[i] = p;
        if (!onScreen({p.x, p.y, p.z + 1.0f})) continue;
        mapShown_[i] = 1;
        if (!near || std::hypot(p.x - at.x, p.y - at.y) > reach) continue;
        const auto sheet = sheets_.find(m.type);
        if (sheet == sheets_.end() || !sheet->second.tex) {
            // Baked (and asked for from the page) a few a frame; a dot until it is there.
            const auto t = static_cast<sim::UnitType>(m.type);
            if (need.size() < kBakesPerFrame && std::find(need.begin(), need.end(), t) == need.end()) need.push_back(t);
            continue;
        }
        mapSprites_.push_back(i);
    }
    bakeTypes(need);
}

// The soldiers into this frame's drawing: sprites joining the battle's groups,
// the rest the dots, and a ring on the selected stack.
void Renderer::pushMapSprites(const Battle& battle, const Camera& camera, std::map<int, std::vector<float>>& groups,
                              std::vector<float>& decals) {
    const float dist = camera.distance();
    // Dots: as wide as a man's shoulders where that is visible, never a speck or a blot.
    const float screen = closeupScreen_ > 0 ? closeupScreen_ : camera.height();
    const float devicePerCss = camera.height() / std::max(screen, 1.0f);
    const float pxPerMetre = 1.0f / (2.0f * std::tan(Camera::kFovY / 2) / camera.height() * std::max(dist, 1.0f));
    mapDotPx_ = std::clamp(kDotMetres * pxPerMetre / devicePerCss, kDotMinPx, kDotMaxPx) * devicePerCss;
    std::vector<char> sprite(mapSoldiers_.size(), 0);
    for (size_t i : mapSprites_) sprite[i] = 1;
    std::vector<float> dots;
    dots.reserve(mapSoldiers_.size() * 7);
    const float lift = 0.9f + dist * 0.0015f;
    float sx = 0, sy = 0, sz = 0;
    int selected = 0;
    for (size_t i = 0; i < mapSoldiers_.size(); ++i) {
        const MapSoldier& m = mapSoldiers_[i];
        const V3& p = mapPlaces_[i];
        if (m.selected) {
            sx += p.x, sy += p.y, sz += p.z;
            ++selected;
        }
        if (sprite[i] || !mapShown_[i]) continue;
        const V3 colour = factionColor(m.owner);
        dots.insert(dots.end(), {p.x, p.y, p.z + lift, colour.x, colour.y, colour.z, m.selected ? 1.0f : 0.0f});
    }
    uploadDynamic(dots_, dots);

    for (size_t i : mapSprites_) {
        const MapSoldier& m = mapSoldiers_[i];
        const V3& p = mapPlaces_[i];
        const auto type = static_cast<sim::UnitType>(m.type);
        const bool moving = m.speed > 0.05f;
        // Metres walked, from the campaign's clock and his stack's pace, so that his stride
        // keeps to the ground; each man begins at his own point of it.
        const float walked = m.speed * mapSeconds_ + hash01(m.id) * 10.0f;
        int frame = 0;
        if (const sprites::SheetClips* clips = clipsFor(m.type)) {
            const sprites::SheetClip* walk = clips->play("walk");
            const sprites::SheetClip* idle = clips->play("idle");
            if (moving && walk) {
                const sprites::SheetClip* run = clips->play("run");
                const sprites::SheetClip* clip = run && run != walk && m.speed > sprites::runThreshold(*walk, *run) ? run : walk;
                const float stride = clip->stride > 0 ? clip->stride : figures::stride(type);
                frame = clip->first + clip->frameAtPhase(std::fmod(walked / stride, 1.0f));
            } else if (idle) {
                frame = idle->first + idle->frameAt(mapSeconds_ + static_cast<float>(m.id % 13) * 0.37f, idle->length(2.0f), true);
            }
        } else {
            const auto sheet = sheets_.find(m.type);
            const bool prerenderedFlier = m.aloft && sheet != sheets_.end() && sheet->second.prerendered;
            frame = sprites::fixedLayoutFrame(1e9f, figures::attackSeconds(type), 1e9f, moving, m.aloft, walked, figures::stride(type),
                                              prerenderedFlier, mapSeconds_, m.id);
        }
        std::vector<float>& g = groups[kMapKeyBase + m.type * kMapSides + sideOf(m.owner)];
        g.insert(g.end(), {p.x, p.y, p.z, m.yaw, static_cast<float>(frame), m.selected ? 1.0f : 0.0f, 0.0f});
        // His contact shadow, so that he stands on the ground.
        decals.insert(decals.end(), {p.x, p.y, p.z, m.yaw, static_cast<float>(sim::unitStats(type).radius.toDouble()) * 1.3f, 0, kDecalShadow, 0, 1, 0, 0});
    }

    // The selected stack, ringed: wide enough to see from the height of the dots.
    if (selected > 0) {
        const V3 centre{sx / selected, sy / selected, sz / selected};
        float radius = 0;
        for (size_t i = 0; i < mapSoldiers_.size(); ++i)
            if (mapSoldiers_[i].selected) radius = std::max(radius, std::hypot(mapPlaces_[i].x - centre.x, mapPlaces_[i].y - centre.y));
        const float size = std::max(radius + 3.0f, dist * 0.012f);
        decals.insert(decals.end(), {centre.x, centre.y, battle.floorAt(centre), 0, size, 0, kDecalRing, 0.4f, 0.9f, 1.0f, 0});
    }
}

void Renderer::drawMapDots(const Camera& camera, double timeSeconds, float gameSeconds) {
    if (dots_.count == 0) return;
    setCommon(dot_, camera, timeSeconds, gameSeconds);
    glUniform2f(dot_.viewport, camera.width(), camera.height());
    glUniform1f(dot_.dotPx, mapDotPx_);
    // Seen through hills from high up, where one is only a speck: depth-tested once the land can hide a man.
    const bool tested = camera.distance() < 1500.0f;
    if (!tested) glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(dots_.vao);
    for (const float rim : {mapDotPx_ * 0.22f, 0.0f}) {
        glUniform1f(dot_.rim, rim);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, dots_.count);
    }
    glDepthMask(GL_TRUE);
    if (!tested) glEnable(GL_DEPTH_TEST);
}

}  // namespace illyria::game
