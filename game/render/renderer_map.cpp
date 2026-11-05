// Men at every distance: one level of detail for the soldiers of any battle, in the renderer's one pass over them
// (Renderer::draw). From high up each is a dot in his side's colour; lower down the same man is the battle's
// sprite, standing where his dot was, and the dots fade out as the sprites come in, so that nothing pops. A man
// in a fight pulses as a dot, rapidly and at random in strength, and swings and flinches as a sprite. Which is
// which depends on the camera, but only the drawing does.
//
// The men are the presented world's own units (Battle::borrow: the campaign's fights), and the sleepers: the
// campaign's soldiers asleep in their squads on the march, who are not units of any world (Battle::sleepers: Campaign::Soldier).
#include <algorithm>
#include <cmath>

#include "illyria/game/factions.h"
#include "renderer.h"

namespace illyria::game {

namespace {
constexpr float kDecalRing = 2;  // as renderer.cpp
// Within this distance of the camera (and this far of the view's centre, in
// multiples of it) soldiers are sprites; beyond, dots. A man is a few pixels
// tall at this height. The dots fade out over the way in to kDotsGone, so that
// a dot becomes its sprite in place (the sprite is drawn over it from kSpriteBelow).
constexpr float kSpriteBelow = 380.0f, kSpriteReach = 1.9f, kDotsGone = 170.0f;
// Sprite sheets baked in one frame: baking is the slow part of a first look.
constexpr size_t kBakesPerFrame = 1;
// A dot is about this wide on the ground, between these sizes in CSS pixels. From the whole island (kDotFarFrom and
// beyond, to the greatest height) it is never smaller than kDotFarPx: a man is a dot to read at a glance, in his side's colour,
// whatever the ground (a ring round the colour and an edge round the ring do the rest, rims 40% of the width either side).
constexpr float kDotMetres = 1.1f, kDotMinPx = 4.4f, kDotMaxPx = 9.0f, kDotFarPx = 8.0f, kDotFarFrom = 700.0f, kDotFarTo = 3000.0f;

float hash01(uint32_t v) {
    v *= 2654435761u;
    v ^= v >> 15;
    v *= 2246822519u;
    v ^= v >> 13;
    return static_cast<float>(v & 0xFFFFu) / 65535.0f;
}
}  // namespace

// How hard a man in a fight is flashing, 0..1: a random strength every 1/16 s
// (a different one for every man), eased from one to the next, and swelling
// with the bout. A function of his number and the game's time only.
float manPulse(uint32_t id, float seconds) {
    const float t = seconds * 16.0f;
    const uint32_t n = static_cast<uint32_t>(t);
    const float f = t - static_cast<float>(n), e = f * f * (3.0f - 2.0f * f);
    const float a = hash01(id * 977u + n), b = hash01(id * 977u + n + 1);
    return 0.15f + 0.85f * (a + (b - a) * e);
}

int manSide(uint8_t owner) { return owner < sim::Continent::kFactionCount ? owner : 8; }

// The frame's level of detail: how near the camera is, which men are near it, how wide a dot is.
void Renderer::beginMen(const Battle& battle, const Camera& camera, float seconds) {
    MenLod& m = men_;
    m.seconds = seconds;
    m.dots.clear();
    m.need.clear();
    m.selected.clear();
    m.looked = false;
    m.seen.clear();
    m.sprites = 0;
    const float dist = camera.distance();
    m.vp = camera.viewProjection();
    m.near = dist < kSpriteBelow;
    m.at = camera.target();
    m.reach = dist * kSpriteReach;
    // Dots: as wide as a man's shoulders where that is visible, never a speck or a blot.
    const float screen = closeupScreen_ > 0 ? closeupScreen_ : camera.height();
    const float devicePerCss = camera.height() / std::max(screen, 1.0f);
    const float pxPerMetre = 1.0f / (2.0f * std::tan(Camera::kFovY / 2) / camera.height() * std::max(dist, 1.0f));
    const float far = std::clamp((dist - kDotFarFrom) / (kDotFarTo - kDotFarFrom), 0.0f, 1.0f);
    const float minPx = kDotMinPx + (kDotFarPx - kDotMinPx) * far * far * (3.0f - 2.0f * far);
    mapDotPx_ = std::clamp(kDotMetres * pxPerMetre / devicePerCss, minPx, kDotMaxPx) * devicePerCss;
    // The dot of a man who is also a sprite fades out as the camera comes in.
    m.spriteDot = std::clamp((dist - kDotsGone) / (kSpriteBelow - kDotsGone), 0.0f, 1.0f);
    m.lift = 0.9f + dist * 0.0015f;
}

// Within the screen and a tenth beyond it (a man's picture reaches past his feet).
bool Renderer::manOnScreen(const V3& p) const {
    const M4& vp = men_.vp;
    const float x = vp.m[0] * p.x + vp.m[4] * p.y + vp.m[8] * p.z + vp.m[12], y = vp.m[1] * p.x + vp.m[5] * p.y + vp.m[9] * p.z + vp.m[13],
                w = vp.m[3] * p.x + vp.m[7] * p.y + vp.m[11] * p.z + vp.m[15];
    return w > 0 && std::fabs(x) <= w * 1.1f && std::fabs(y) <= w * 1.1f;
}

// Whether a man at p is the battle's sprite (else only a dot): near enough, on the screen, and his sheet baked
// (asked for, a few a frame, until it is).
bool Renderer::manIsSprite(const V3& p, int type) {
    MenLod& m = men_;
    if (!m.near || !manOnScreen(p) || std::hypot(p.x - m.at.x, p.y - m.at.y) > m.reach) return false;
    const auto sheet = sheets_.find(type);
    if (sheet == sheets_.end() || !sheet->second.tex) {
        const auto t = static_cast<sim::UnitType>(type);
        if (m.need.size() < kBakesPerFrame && std::find(m.need.begin(), m.need.end(), t) == m.need.end()) m.need.push_back(t);
        return false;
    }
    ++m.sprites;
    m.seen.push_back({type, p});
    return true;
}

// A man's dot: in his side's colour (a town's own garrison hollow), pulsing if he is in a fight, fading if he is
// also a sprite; ringed pale cyan when he is selected, grey when he is only looked at.
void Renderer::manDot(const V3& p, const V3& colour, bool owned, bool selected, bool inspected, float pulse, bool alsoSprite) {
    MenLod& m = men_;
    if (selected || inspected) {
        m.selected.push_back(p);
        m.looked = m.looked || inspected;
    }
    const float alpha = alsoSprite ? m.spriteDot : 1.0f;
    if (alpha <= 0.0f || !manOnScreen(p)) return;
    m.dots.insert(m.dots.end(), {p.x, p.y, p.z + m.lift, colour.x, colour.y, colour.z, inspected ? 2.0f : selected ? 1.0f : 0.0f, pulse,
                                 owned ? alpha : -alpha});
}

// The frame's men done with: the selected ringed, the dots uploaded, the sheets asked for baked.
void Renderer::endMen(const Battle& battle, const Camera& camera, std::vector<float>& decals) {
    MenLod& m = men_;
    // The selected, ringed: wide enough to see from the height of the dots (close in, each of the sprites is
    // marked itself, and a ring across the field would only hide it).
    if (!m.selected.empty() && camera.distance() > 120.0f) {
        V3 centre{0, 0, 0};
        for (const V3& p : m.selected) centre = centre + p;
        centre = centre * (1.0f / static_cast<float>(m.selected.size()));
        float radius = 0;
        for (const V3& p : m.selected) radius = std::max(radius, std::hypot(p.x - centre.x, p.y - centre.y));
        const float size = std::max(std::min(radius + 3.0f, 45.0f), camera.distance() * 0.012f);  // (a spread-out stack is ringed about its middle)
        const float grey = 0.62f;
        if (m.looked) decals.insert(decals.end(), {centre.x, centre.y, battle.floorAt(centre), 0, size, 0, kDecalRing, grey, grey, grey, 0});
        else decals.insert(decals.end(), {centre.x, centre.y, battle.floorAt(centre), 0, size, 0, kDecalRing, 0.4f, 0.9f, 1.0f, 0});
    }
    uploadDynamic(dots_, m.dots);
    bakeTypes(m.need);
}

void Renderer::drawDots(const Camera& camera, double timeSeconds, float gameSeconds) {
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
    for (const float rim : {mapDotPx_ * 0.40f, mapDotPx_ * 0.20f, 0.0f}) {
        glUniform1f(dot_.rim, rim);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, dots_.count);
    }
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    if (!tested) glEnable(GL_DEPTH_TEST);
}

}  // namespace illyria::game
