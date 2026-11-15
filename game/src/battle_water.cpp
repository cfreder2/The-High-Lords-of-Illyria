// What the water shows of a fight (docs/DEATH_AUDIT.md, "In the water"): rings spreading from whatever wades through
// it or falls into it, spray thrown up, a wake behind a man or a horse on the move, the blood of the wounded and the
// dead blooming in it and going down with the current, and the drops the wounded leave on dry ground. Presentation
// only: the rings and the dye are a short list the water's shader reads (renderer_water.cpp), spray is particles. What
// floats, where, and how the river carries it is the simulation's (engine/sim/src/world_drift.cpp).
#include <algorithm>
#include <cmath>

#include "illyria/game/battle.h"

namespace illyria::game {

namespace {

constexpr float kTick = 1.0f / sim::kTicksPerSecond;
// A man is looked at for his wading every this many ticks (a third of the men each tick).
constexpr uint32_t kWadeStride = 3;
// Blood in the water: a cloud thins to nothing in about this long, spreading as it goes.
constexpr float kDyeSeconds = 48.0f;
constexpr float kDyeSpread = 0.11f;   // m/s its radius grows
constexpr float kDyeCarry = 0.85f;    // of the current's pace (it lags the surface a little)
constexpr float kDyeMergeM = 1.3f;    // fresh blood this near a young cloud feeds it
// The wounded: a man hurt below this share of his health drips for this long after the blow.
constexpr float kBleedBelow = 0.6f;
constexpr uint32_t kBleedTicks = 22 * sim::kTicksPerSecond;
constexpr size_t kMaxBleeders = 96;

uint32_t hashOf(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}
float unitOf(uint32_t h) { return static_cast<float>(h & 0xFFFF) / 65535.0f; }

}  // namespace

bool Battle::waterAt(float x, float y, float& surface, float& depth) const {
    if (!inWorld(x, y)) return false;
    const sim::Fixed fx = toFixed(x), fy = toFixed(y);
    const sim::Fixed d = world_->waterDepth(fx, fy);
    if (d <= sim::Fixed::zero()) return false;
    depth = static_cast<float>(d.toDouble());
    surface = static_cast<float>(worldRegion().waterSurfaceAt(fx, fy).toDouble());
    return true;
}

void Battle::ring(const V3& at, float size, float delay) {
    const WaterRing r{at, static_cast<float>(world_->tick()) * kTick + delay, size};
    if (rings_.size() < kMaxWaterRings) rings_.push_back(r);
    else rings_[ringsAdded_ % kMaxWaterRings] = r;
    ++ringsAdded_;
}

void Battle::splash(const V3& at, float size, const V3& travel) {
    const uint32_t tick = world_->tick();
    const auto rnd = [&] { return unitOf(fxSeed_ = hashOf(fxSeed_, tick)); };
    ring(at, size);
    if (size > 0.7f) ring(at, size * 0.6f, 0.35f);  // the second swell of something heavy
    // Spray: a crown thrown up and out, more of it and higher the bigger the splash.
    const int drops = std::clamp(static_cast<int>(size * 9.0f), 2, 26);
    const V3 along = flat(travel) * 0.25f;
    for (int i = 0; i < drops; ++i) {
        const float a = rnd() * 6.2832f, out = (0.5f + rnd() * 1.3f) * (0.6f + size * 0.5f);
        emit(Particle::Kind::Spray, at + V3{std::cos(a) * size * 0.2f, std::sin(a) * size * 0.2f, 0.03f},
             V3{std::cos(a) * out + along.x, std::sin(a) * out + along.y, (1.4f + rnd() * 2.2f) * (0.6f + size * 0.55f)}, 0.45f + rnd() * 0.35f + size * 0.12f,
             0.025f + rnd() * 0.03f + size * 0.012f);
    }
}

void Battle::waterColumn(const V3& at, float reach) {
    const uint32_t tick = world_->tick();
    const auto rnd = [&] { return unitOf(fxSeed_ = hashOf(fxSeed_, tick)); };
    ring(at, 2.6f);
    ring(at, 1.8f, 0.4f);
    ring(at, 1.2f, 0.9f);
    // The column: a jet straight up, a crown round it, and the mist it leaves.
    for (int i = 0; i < 46; ++i) {
        const float a = rnd() * 6.2832f, out = rnd() * rnd() * 2.2f;
        emit(Particle::Kind::Spray, at + V3{std::cos(a) * 0.3f, std::sin(a) * 0.3f, 0.05f}, V3{std::cos(a) * out, std::sin(a) * out, 5.0f + rnd() * 6.5f},
             0.9f + rnd() * 0.7f, 0.05f + rnd() * 0.07f);
    }
    for (int i = 0; i < 22; ++i) {
        const float a = rnd() * 6.2832f, out = 2.0f + rnd() * reach * 1.4f;
        emit(Particle::Kind::Spray, at, V3{std::cos(a) * out, std::sin(a) * out, 2.0f + rnd() * 3.0f}, 0.7f + rnd() * 0.5f, 0.04f + rnd() * 0.05f);
    }
    for (int i = 0; i < 6; ++i)
        emit(Particle::Kind::Steam, at + V3{(rnd() - 0.5f) * 1.2f, (rnd() - 0.5f) * 1.2f, 0.6f + rnd() * 1.5f}, V3{(rnd() - 0.5f) * 0.8f, (rnd() - 0.5f) * 0.8f, 0.6f + rnd()},
             1.0f + rnd() * 0.8f, 0.4f + rnd() * 0.4f);
}

void Battle::steam(const V3& at, float reach) {
    const uint32_t tick = world_->tick();
    const auto rnd = [&] { return unitOf(fxSeed_ = hashOf(fxSeed_, tick)); };
    ring(at, 1.7f);
    ring(at, 1.0f, 0.5f);
    // Fire on water: it flashes to steam, a white cloud boiling up and drifting off, and the surface spits.
    for (int i = 0; i < 12; ++i) {
        const float a = rnd() * 6.2832f, d = rnd() * reach * 0.5f;
        emit(Particle::Kind::Steam, at + V3{std::cos(a) * d, std::sin(a) * d, 0.2f + rnd() * 0.5f}, V3{std::cos(a) * (0.3f + rnd() * 0.8f), std::sin(a) * (0.3f + rnd() * 0.8f), 1.2f + rnd() * 2.2f},
             1.5f + rnd() * 1.6f, 0.35f + rnd() * 0.5f);
    }
    for (int i = 0; i < 18; ++i) {
        const float a = rnd() * 6.2832f, out = 1.0f + rnd() * 3.0f;
        emit(Particle::Kind::Spray, at, V3{std::cos(a) * out, std::sin(a) * out, 2.0f + rnd() * 3.5f}, 0.5f + rnd() * 0.4f, 0.03f + rnd() * 0.03f);
    }
}

void Battle::dyeWater(const V3& at, float amount) {
    // Into a young cloud close by, if there is one: a fight in one place makes one deepening stain, not a hundred.
    WaterDye* near = nullptr;
    float best = kDyeMergeM * kDyeMergeM;
    for (WaterDye& d : dye_) {
        const float dx = d.pos.x - at.x, dy = d.pos.y - at.y, r = dx * dx + dy * dy;
        if (d.age < 4.0f && r < best) best = r, near = &d;
    }
    if (near != nullptr) {
        const float share = amount / (near->amount + amount);
        near->pos = lerp(near->pos, at, share);
        near->amount = std::min(near->amount + amount, 3.0f);
        return;
    }
    const WaterDye fresh{at, amount, 0.35f + 0.25f * std::min(amount, 2.0f), 0.0f};
    if (dye_.size() < kMaxWaterDye) {
        dye_.push_back(fresh);
        return;
    }
    // Full: the faintest cloud gives its place.
    auto faint = std::min_element(dye_.begin(), dye_.end(), [](const WaterDye& a, const WaterDye& b) { return a.strength() < b.strength(); });
    if (faint->strength() < amount) *faint = fresh;
}

float WaterDye::strength() const { return amount * std::max(0.0f, 1.0f - age / kDyeSeconds); }
float WaterDye::life() { return kDyeSeconds; }

bool Battle::presentInWater(const sim::ImpactEvent& e, const V3& pos, const sim::Unit* victim, const V3& body) {
    using K = sim::ImpactEvent::Kind;
    float surface = 0, depth = 0;
    if (e.kind == K::Hit) {
        if (victim == nullptr || !waterAt(body.x, body.y, surface, depth) || body.z > surface + 0.3f) return false;
        const float scale = static_cast<float>(victim->stats.height.toDouble()) / 1.8f;
        const V3 at{body.x, body.y, surface};
        if (victim->stats.bleeds) dyeWater(at, (e.killed ? 1.0f : 0.3f) * std::min(scale, 2.5f));
        // He goes down into it: the splash of the fall, as he lands.
        if (e.killed && depth > 0.08f) {
            const V3 fall = toV3(e.dir).normalized();
            splash(at + flat(fall) * (0.5f * scale), std::clamp(0.9f * scale, 0.7f, 2.4f), fall * 2.0f);
        } else if (depth > 0.05f) {
            ring(at, 0.3f);
        }
        return true;
    }
    if (!waterAt(pos.x, pos.y, surface, depth) || pos.z > surface + 0.6f) return false;
    const V3 at{pos.x, pos.y, surface};
    switch (e.kind) {
        case K::Ground: splash(at, 0.22f, toV3(e.dir)); break;                                  // an arrow: a plop and a ring
        case K::Splash: splash(at, static_cast<float>(e.radius.toDouble()), toV3(e.dir)); break;  // a part of a body
        case K::Smash: waterColumn(at, static_cast<float>(e.radius.toDouble())); break;           // a stone: a column of water
        case K::Explosion:
        case K::Steam:
        case K::Burst: steam(at, static_cast<float>(e.radius.toDouble())); break;
        case K::Doused: steam(at, static_cast<float>(e.radius.toDouble()) * 0.4f); break;  // a bottle or a satchel gone out
        default: return false;
    }
    return true;
}

void Battle::stepWater() {
    const uint32_t tick = world_->tick();
    const auto& units = world_->units();

    // The waders: rings at the feet of whoever moves through water, a wake behind him as they spread, spray at a run.
    // Sized by the creature and its pace; a man standing to fight in it stirs it as he strikes.
    for (size_t i = tick % kWadeStride; i < units.size() && i < prevUnitPos_.size(); i += kWadeStride) {
        const sim::Unit& u = units[i];
        if (!u.alive() || u.inAir()) continue;
        const V3 pos = toV3(u.pos);
        float surface = 0, depth = 0;
        if (!waterAt(pos.x, pos.y, surface, depth) || depth < 0.04f || pos.z > surface + 0.2f) continue;
        if (!sees(u.owner, pos.x, pos.y)) continue;
        const V3 step = flat(pos - prevUnitPos_[i]);
        const float speed = step.length() / kTick;
        const bool struck = attackTick_[i] == tick || hitTick_[i] == tick;
        if (speed < 0.25f && !struck) continue;
        const uint32_t h = hashOf(static_cast<uint32_t>(i), tick);
        const uint32_t beat = tick / kWadeStride + static_cast<uint32_t>(i) * 5u;
        const uint32_t every = speed > 3.5f ? 1u : speed > 1.5f ? 2u : 4u;
        if (!struck && beat % every != 0) continue;
        const float girth = std::clamp(static_cast<float>(u.stats.radius.toDouble()) * 1.9f, 0.5f, 2.6f);
        const float size = girth * std::clamp(0.35f + speed * 0.12f, 0.35f, 1.0f) * std::min(1.0f, 0.4f + depth * 2.0f);
        const V3 dir = speed > 0.01f ? step * (1.0f / step.length()) : V3{};
        const V3 foot = V3{pos.x, pos.y, surface} + V3{-dir.y, dir.x, 0} * ((unitOf(h) - 0.5f) * girth * 0.45f);
        ring(foot, size);
        // Spray off the shins, thrown forward: none at a careful wade, a sheet of it at a gallop.
        const int drops = std::min(6, static_cast<int>((speed - 0.8f) * girth * 0.9f) + (struck ? 2 : 0));
        for (int k = 0; k < drops; ++k) {
            const uint32_t s = hashOf(h, static_cast<uint32_t>(k + 1));
            const V3 side = V3{-dir.y, dir.x, 0} * ((unitOf(s) - 0.5f) * 1.6f);
            emit(Particle::Kind::Spray, foot + V3{0, 0, 0.04f}, dir * (speed * (0.3f + unitOf(s >> 8) * 0.5f)) + side + V3{0, 0, 0.9f + unitOf(s >> 16) * (0.8f + speed * 0.35f)},
                 0.3f + unitOf(s >> 4) * 0.3f, 0.02f + unitOf(s >> 12) * 0.025f * girth);
        }
    }

    // The wounded drip: drops along the way they go on dry ground, a thread of red where they stand in water.
    std::erase_if(bleeders_, [&](const Bleeder& b) { return tick >= b.until || b.unit < 1 || b.unit > units.size() || !units[b.unit - 1].alive(); });
    for (Bleeder& b : bleeders_) {
        const sim::Unit& u = units[b.unit - 1];
        const V3 pos = toV3(u.pos);
        const uint32_t h = hashOf(b.unit, tick);
        float surface = 0, depth = 0;
        if (waterAt(pos.x, pos.y, surface, depth) && pos.z < surface + 0.2f) {
            if ((tick + b.unit * 7u) % 24u == 0) dyeWater({pos.x, pos.y, surface}, 0.14f);
            b.last = pos;
            continue;
        }
        const V3 gone = flat(pos - b.last);
        if (u.inAir() || gone.length() < b.gap) continue;
        b.last = pos;
        b.gap = 0.55f + unitOf(h) * 1.1f;
        // Fewer and smaller as the wound closes.
        const float left = static_cast<float>(b.until - tick) / static_cast<float>(kBleedTicks);
        addMark(GroundMark::Kind::Blood, pos + V3{(unitOf(h >> 8) - 0.5f) * 0.5f, (unitOf(h >> 16) - 0.5f) * 0.5f, 0}, unitOf(h >> 4) * 6.2832f,
                (0.07f + unitOf(h >> 12) * 0.12f) * (0.6f + 0.4f * left), h);
    }

    // The dye: carried by the current, spreading, thinning.
    for (WaterDye& d : dye_) {
        d.age += kTick;
        d.radius += kDyeSpread * kTick;
        if (!inWorld(d.pos.x, d.pos.y)) {
            d.age = kDyeSeconds;
            continue;
        }
        const sim::Vec3 flow = world_->flowAt(toFixed(d.pos.x), toFixed(d.pos.y));
        const V3 to = d.pos + V3{static_cast<float>(flow.x.toDouble()), static_cast<float>(flow.y.toDouble()), 0} * (kDyeCarry * kTick);
        float surface = 0, depth = 0;
        if (waterAt(to.x, to.y, surface, depth)) d.pos = {to.x, to.y, surface};  // (against a bank it stays and fades)
    }
    std::erase_if(dye_, [](const WaterDye& d) { return d.age >= kDyeSeconds; });
}

void Battle::noteWound(const sim::Unit& victim) {
    if (!victim.stats.bleeds || !victim.alive() || victim.hp * 10 > victim.stats.maxHp * static_cast<int32_t>(kBleedBelow * 10.0f)) return;
    const uint32_t until = world_->tick() + kBleedTicks;
    for (Bleeder& b : bleeders_)
        if (b.unit == victim.id) {
            b.until = until;
            return;
        }
    if (bleeders_.size() < kMaxBleeders) bleeders_.push_back({victim.id, until, toV3(victim.pos), 0.5f});
}

}  // namespace illyria::game
