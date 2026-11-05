// The campaign's soldiers on the land (campaign_soldiers.h): one continuous level
// of detail for every man on the island. From high up each is a dot in his side's
// colour; lower down the same man is the battle's sprite, standing where his dot
// was, and the dots fade out as the sprites come in, so that nothing pops. A man
// who is fighting (striking, struck, or locked in melee) pulses as a dot, rapidly
// and at random in strength, and swings and flinches as a sprite. Which is
// which depends on the camera, but only the drawing does: where every man stands,
// and whether he is in a fight, comes from the campaign's state alone.
#include <algorithm>
#include <cmath>

#include "illyria/game/factions.h"
#include "renderer.h"

namespace illyria::game {

namespace {
constexpr float kDecalShadow = 1, kDecalRing = 2;  // as renderer.cpp
// Within this distance of the camera (and this far of the view's centre, in
// multiples of it) soldiers are sprites; beyond, dots. A man is a few pixels
// tall at this height. The dots fade out over the way in to kDotsGone, so that
// a dot becomes its sprite in place (the sprite is drawn over it from kSpriteBelow).
constexpr float kSpriteBelow = 380.0f, kSpriteReach = 1.9f, kDotsGone = 170.0f;
// Sprite sheets baked in one frame: baking is the slow part of a first look.
constexpr size_t kBakesPerFrame = 1;
// A dot is about this wide on the ground, between these sizes in CSS pixels.
constexpr float kDotMetres = 1.1f, kDotMinPx = 2.6f, kDotMaxPx = 7.0f;
// A fight shows for this long after the last blow, struck or given.
constexpr float kFightSeconds = 1.0f;

int sideOf(uint8_t owner) { return owner < sim::Continent::kFactionCount ? owner : 8; }

float hash01(uint32_t v) {
    v *= 2654435761u;
    v ^= v >> 15;
    v *= 2246822519u;
    v ^= v >> 13;
    return static_cast<float>(v & 0xFFFFu) / 65535.0f;
}

// How hard a man in a fight is flashing, 0..1: a random strength every 1/16 s
// (a different one for every man), eased from one to the next, and swelling
// with the bout. A function of his number and the game's time only.
float pulseOf(uint32_t id, float seconds) {
    const float t = seconds * 16.0f;
    const uint32_t n = static_cast<uint32_t>(t);
    const float f = t - static_cast<float>(n), e = f * f * (3.0f - 2.0f * f);
    const float a = hash01(id * 977u + n), b = hash01(id * 977u + n + 1);
    return 0.15f + 0.85f * (a + (b - a) * e);
}
}  // namespace

void Renderer::setMapSoldiers(const std::vector<MapSoldier>& soldiers, float seconds, float dt) {
    mapSoldiers_ = soldiers;
    mapSeconds_ = seconds;
    mapDt_ = dt;
    mapOn_ = !soldiers.empty();
    if (!mapOn_) {
        mapPlaces_.clear();
        mapSprites_.clear();
        mapMemory_.clear();
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
        if (!near || !m.alive || std::hypot(p.x - at.x, p.y - at.y) > reach) continue;
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
// the rest the dots (all of them far off, fading as the sprites come in), and a
// ring on the selected stack.
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
    // The dot of a man who is also a sprite fades out as the camera comes in.
    const float spriteDot = std::clamp((dist - kDotsGone) / (kSpriteBelow - kDotsGone), 0.0f, 1.0f);
    std::vector<float> dots;
    dots.reserve(mapSoldiers_.size() * 9);
    const float lift = 0.9f + dist * 0.0015f;
    const float tps = static_cast<float>(sim::kTicksPerSecond);
    float sx = 0, sy = 0, sz = 0;
    int selected = 0;
    for (size_t i = 0; i < mapSoldiers_.size(); ++i) {
        const MapSoldier& m = mapSoldiers_[i];
        const V3& p = mapPlaces_[i];
        if (m.selected && m.alive) {
            sx += p.x, sy += p.y, sz += p.z;
            ++selected;
        }
        if (!mapShown_[i] || !m.alive) continue;
        const float alpha = sprite[i] ? spriteDot : 1.0f;
        if (alpha <= 0.0f) continue;
        // In a fight if he struck, was struck, or fought at all within the last second.
        const bool fighting = m.awake && mapSeconds_ - static_cast<float>(m.fightTick) / tps < kFightSeconds;
        const float pulse = fighting ? pulseOf(m.id, mapSeconds_) : 0.0f;
        const V3 colour = factionColor(m.owner);
        dots.insert(dots.end(), {p.x, p.y, p.z + lift, colour.x, colour.y, colour.z, m.selected ? 1.0f : 0.0f, pulse, alpha});
    }
    uploadDynamic(dots_, dots);

    std::map<uint32_t, sprites::ClipMemory> memory;  // the sprite men's, kept while they stay in sight
    for (size_t i : mapSprites_) {
        const MapSoldier& m = mapSoldiers_[i];
        const V3& p = mapPlaces_[i];
        const auto type = static_cast<sim::UnitType>(m.type);
        const bool moving = m.speed > 0.05f;
        // Metres walked, from the campaign's clock and his pace, so that his stride
        // keeps to the ground; each man begins at his own point of it.
        const float walked = m.speed * mapSeconds_ + hash01(m.id) * 10.0f;
        // What he has done lately, in seconds ago (-1: nothing yet).
        const auto since = [&](uint32_t tick) { return m.awake && tick > 0 ? mapSeconds_ - static_cast<float>(tick) / tps : -1.0f; };
        const float struck = since(m.struckTick), hurt = since(m.hurtTick);
        int frame = 0;
        if (const sprites::SheetClips* clips = clipsFor(m.type)) {
            sprites::ClipMemory mem;
            if (const auto known = mapMemory_.find(m.id); known != mapMemory_.end()) mem = known->second;
            sprites::Deeds d;
            d.now = mapSeconds_;
            d.sinceAttack = struck;
            d.sinceHit = hurt;
            d.moving = moving;
            d.inAir = m.aloft;
            d.idlePhase = static_cast<float>(m.id % 13) * 0.37f;
            d.attackSeconds = figures::attackSeconds(type);
            d.flinchSeconds = figures::kFlinchSeconds;
            d.stride = figures::stride(type);
            const float dt = std::max(mapDt_, 1e-3f);
            d.stepMetres = m.speed * dt;
            mem.speed = sprites::easeSpeed(mem.speed, m.speed, dt);
            frame = sprites::chooseFrame(*clips, d, mem).frame;
            memory[m.id] = mem;
        } else {
            const auto sheet = sheets_.find(m.type);
            const bool prerenderedFlier = m.aloft && sheet != sheets_.end() && sheet->second.prerendered;
            frame = sprites::fixedLayoutFrame(struck < 0 ? 1e9f : struck, figures::attackSeconds(type), hurt < 0 ? 1e9f : hurt, moving, m.aloft, walked,
                                              figures::stride(type), prerenderedFlier, mapSeconds_, m.id);
        }
        std::vector<float>& g = groups[kMapKeyBase + m.type * kMapSides + sideOf(m.owner)];
        g.insert(g.end(), {p.x, p.y, p.z, m.yaw, static_cast<float>(frame), m.selected ? 1.0f : 0.0f, 0.0f});
        // His contact shadow, so that he stands on the ground.
        decals.insert(decals.end(), {p.x, p.y, p.z, m.yaw, static_cast<float>(sim::unitStats(type).radius.toDouble()) * 1.3f, 0, kDecalShadow, 0, 1, 0, 0});
    }
    mapMemory_ = std::move(memory);

    // The selected stack, ringed: wide enough to see from the height of the dots (close in, each of its
    // sprites is marked itself, and a ring across the field would only hide it).
    if (selected > 0 && dist > 120.0f) {
        const V3 centre{sx / selected, sy / selected, sz / selected};
        float radius = 0;
        for (size_t i = 0; i < mapSoldiers_.size(); ++i)
            if (mapSoldiers_[i].selected && mapSoldiers_[i].alive) radius = std::max(radius, std::hypot(mapPlaces_[i].x - centre.x, mapPlaces_[i].y - centre.y));
        const float size = std::max(std::min(radius + 3.0f, 45.0f), dist * 0.012f);  // (a spread-out stack is ringed about its middle, not all over the field)
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(dots_.vao);
    for (const float rim : {mapDotPx_ * 0.22f, 0.0f}) {
        glUniform1f(dot_.rim, rim);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, dots_.count);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (!tested) glEnable(GL_DEPTH_TEST);
}

}  // namespace illyria::game
