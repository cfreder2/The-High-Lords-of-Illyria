// What each tick's deaths leave to see and hear (the death matrix, sim
// world_death.cpp): blood rain around a body torn apart, smoke rising from the
// burned, and the battle's sound effects (sfx.h). Presentation only.
#include <algorithm>
#include <cmath>

#include "illyria/game/battle.h"

namespace illyria::game {

namespace {

uint32_t hashMix(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}
float unitOf(uint32_t h) { return static_cast<float>(h & 0xFFFF) / 65535.0f; }

// The burned smoulder this long, smoking less as they cool.
constexpr uint32_t kSmoulderTicks = 25 * sim::kTicksPerSecond;
constexpr size_t kMaxSmouldering = 64;

}  // namespace

void Battle::presentDeaths() {
    const uint32_t tick = world_->tick();
    for (const sim::DeathEvent& d : world_->deathEvents()) {
        const V3 pos = toV3(d.pos);
        const uint32_t seed = hashMix(d.unit, tick);
        const sim::UnitStats& s = world_->units()[d.unit - 1].stats;
        if (d.kind == sim::DeathKind::Gibbed && s.bleeds) {
            // The blood rain: drops and bits pattering down all round.
            for (int i = 0; i < 14; ++i) {
                const uint32_t h = hashMix(seed, static_cast<uint32_t>(i));
                const float a = unitOf(h) * 6.2832f, r = 0.5f + unitOf(h >> 8) * 3.5f;
                addMark(GroundMark::Kind::Blood, pos + V3{std::cos(a) * r, std::sin(a) * r, 0}, a, 0.12f + unitOf(h >> 16) * 0.25f, h);
            }
        } else if (d.kind == sim::DeathKind::Charred && smouldering_.size() < kMaxSmouldering) {
            smouldering_.push_back({d.unit, tick});
        }
    }
    // Smoke from the burned, a puff every few ticks; flames for the first seconds.
    std::erase_if(smouldering_, [&](const Smoulder& s) { return tick - s.since > kSmoulderTicks; });
    for (const Smoulder& s : smouldering_) {
        const uint32_t age = tick - s.since;
        const uint32_t every = age < 5 * sim::kTicksPerSecond ? 3 : age < 15 * sim::kTicksPerSecond ? 6 : 12;
        if ((tick + s.unit) % every != 0) continue;
        const sim::Unit& u = world_->units()[s.unit - 1];
        const uint32_t h = hashMix(s.unit, tick);
        const V3 at = toV3(u.pos) + V3{unitOf(h) - 0.5f, unitOf(h >> 8) - 0.5f, 0.2f};
        emit(Particle::Kind::Smoke, at, {(unitOf(h >> 4) - 0.5f) * 0.4f, (unitOf(h >> 12) - 0.5f) * 0.4f, 0.8f + unitOf(h >> 16) * 0.6f},
             2.5f + unitOf(h >> 20) * 1.5f, 0.35f + unitOf(h >> 24) * 0.3f);
        if (age < 4 * sim::kTicksPerSecond)
            emit(Particle::Kind::Fire, at, {0, 0, 0.6f}, 0.35f, 0.25f + unitOf(h >> 6) * 0.15f);
    }
    sfx_.observe(*world_, outcome(), *continent_, origin(), scenario_ == Scenario::Siege);
}

}  // namespace illyria::game
