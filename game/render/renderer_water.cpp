// The water in a fight (docs/DEATH_AUDIT.md, "In the water"). The battle keeps the rings spreading on it and the
// blood in it (game/src/battle_water.cpp); here the ones nearest the view are packed for the water's shader (kWaterFs:
// uRipple, uDye), and the dead the water bears are drawn riding in its surface: rising to it as they fall, bobbing,
// turned slowly by the stream, and going down to the bed when it lets them go. Where they are is the simulation's
// (engine/sim/src/world_drift.cpp); only the height they are drawn at and the turning are made here.

#include <algorithm>
#include <cmath>

#include "renderer.h"

namespace illyria::game {

namespace {

float hashUnit(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return static_cast<float>(v & 0xFFFF) / 65535.0f;
}

// As kWaterFs `ripples`: how long a ring of this size spreads.
float ringLife(float size) { return 1.4f + 1.3f * size; }

}  // namespace

void Renderer::gatherWater(const Battle& battle, const Camera& camera, float gameSeconds) {
    waterRings_ = waterDyes_ = 0;
    if (hidden_ >> static_cast<int>(Part::Wake) & 1) return;  // (timing what they cost: --hide, --cycle)
    const V3 focus = camera.target();
    // From high up a foot's ring is less than a pixel: only the big ones are worth the shader's while.
    const float least = camera.distance() > 320.0f ? 1e9f : camera.distance() > 150.0f ? 1.0f : camera.distance() > 80.0f ? 0.45f : 0.0f;
    struct Pick {
        float score;
        const WaterRing* ring;
    };
    std::vector<Pick> picks;
    const float reach = camera.distance() * 1.6f + 30.0f;
    for (const WaterRing& r : battle.waterRings()) {
        const float age = gameSeconds - r.born;
        if (age < 0 || age >= ringLife(r.size) || r.size < least) continue;
        const float dx = r.pos.x - focus.x, dy = r.pos.y - focus.y, d = std::sqrt(dx * dx + dy * dy);
        if (d > reach) continue;
        // The nearest, the biggest and the youngest first.
        picks.push_back({d / (0.4f + r.size) * (0.6f + age / ringLife(r.size)), &r});
    }
    if (picks.size() > static_cast<size_t>(kWaterRings)) {
        std::nth_element(picks.begin(), picks.begin() + kWaterRings, picks.end(), [](const Pick& a, const Pick& b) { return a.score < b.score; });
        picks.resize(kWaterRings);
    }
    const auto grow = [](float* box, bool first, float x, float y, float r) {
        if (first) box[0] = x - r, box[1] = y - r, box[2] = x + r, box[3] = y + r;
        box[0] = std::min(box[0], x - r), box[1] = std::min(box[1], y - r), box[2] = std::max(box[2], x + r), box[3] = std::max(box[3], y + r);
    };
    for (const Pick& p : picks) {
        float* out = &waterRing_[waterRings_ * 4];
        out[0] = p.ring->pos.x, out[1] = p.ring->pos.y, out[2] = gameSeconds - p.ring->born, out[3] = p.ring->size;
        // (As kWaterFs `ripples`: how far its front has run, and the margin it is tested with.)
        grow(ringBox_, waterRings_++ == 0, out[0], out[1], 0.1f * out[3] + (0.8f + 0.3f * out[3]) * out[2] + 0.4f);
    }
    for (const WaterDye& d : battle.waterDye()) {
        if (waterDyes_ >= kWaterDyes) break;
        // Thinner as it spreads (the same blood in more water) and as it ages; a moment to bloom.
        const float bloom = std::min(1.0f, 0.25f + d.age * 1.5f);
        const float strength = d.strength() * bloom * std::pow(0.6f / std::max(d.radius, 0.6f), 0.55f);
        if (strength < 0.01f) continue;
        float* out = &waterDye_[waterDyes_ * 4];
        out[0] = d.pos.x, out[1] = d.pos.y, out[2] = d.radius * std::min(1.0f, 0.5f + d.age), out[3] = strength;
        // (As kWaterFs `bloodIn`: a cloud reaches root-five radii, drawn out 1.9 times along the stream, pulled 1.6 m by the eddies.)
        grow(dyeBox_, waterDyes_++ == 0, out[0], out[1], out[2] * 2.24f * 1.9f + 1.7f);
    }
}

V3 Renderer::bodyInWater(const Battle& battle, size_t index, const sim::Unit& u, V3 pos, float dt, float gameSeconds, float& turn) {
    if (adrift_.size() <= index) adrift_.resize(battle.world().units().size());
    Adrift& a = adrift_[index];
    float surface = 0, depth = 0;
    if (u.inAir() || !battle.waterAt(pos.x, pos.y, surface, depth)) {
        a.set = false;  // on land, or thrown: where the simulation has him
        turn = a.turn;
        return pos;
    }
    // In the surface with most of him under it; let go, he goes down to the bed where the simulation laid him.
    const float want = u.afloat ? surface - 0.1f : pos.z;
    if (!a.set) {
        a.set = true;
        a.z = u.afloat ? surface - depth : want;  // he fell standing on the bed
    }
    const float rate = want > a.z ? 1.3f : 0.4f;  // up as he falls, down slowly
    a.z += std::clamp(want - a.z, -rate * dt, rate * dt);
    if (u.afloat) a.turn += (hashUnit(u.id * 13u + 5u) - 0.5f) * 0.45f * dt;
    turn = a.turn;
    const float riding = std::clamp(1.0f - (surface - a.z) / 0.5f, 0.0f, 1.0f);  // (no bobbing once he is going down)
    pos.z = a.z + std::sin(gameSeconds * 1.7f + static_cast<float>(u.id) * 1.3f) * 0.03f * riding;
    return pos;
}

}  // namespace illyria::game
