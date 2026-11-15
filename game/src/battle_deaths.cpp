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
    // Burning ground where a Molotov fell (world_fire.cpp): low flames over the patch while it burns, and smoke; timber
    // alight (an engine) flames and smokes where it stands.
    for (const sim::GroundFire& f : world_->fires()) {
        if (tick % 2 != 0) continue;
        const V3 c = toV3(f.at);
        const float r = static_cast<float>(f.radius.toDouble());
        const uint32_t seed = hashMix(static_cast<uint32_t>(f.at.x.raw ^ f.at.y.raw), tick);
        for (int i = 0; i < 3; ++i) {
            const uint32_t h = hashMix(seed, static_cast<uint32_t>(i));
            const float a = unitOf(h) * 6.2832f, d = r * std::sqrt(unitOf(h >> 8));
            emit(Particle::Kind::Fire, c + V3{std::cos(a) * d, std::sin(a) * d, 0.1f}, {0, 0, 0.5f + unitOf(h >> 16) * 0.8f}, 0.4f + unitOf(h >> 20) * 0.4f,
                 0.3f + unitOf(h >> 24) * 0.3f);
        }
        if (tick % 8 == 0) emit(Particle::Kind::Smoke, c + V3{0, 0, 0.8f}, {0, 0, 1.2f}, 3.0f, 0.8f);
    }
    // Timber works alight (sim world_works.cpp): flames along them, rising as they catch, and smoke.
    for (size_t i = 0; i < world_->works().size(); ++i) {
        const sim::WorkState& w = world_->works()[i];
        if (w.fire <= 0 || !w.standing() || (tick + i) % 2 != 0) continue;
        const V3 a = toV3(w.def.a), b = toV3(w.def.b);
        const float h = static_cast<float>(w.def.height.toDouble()) * (w.def.kind == sim::Work::Kind::Watchtower ? 1.15f : 1.0f);
        const uint32_t seed = hashMix(static_cast<uint32_t>(i) * 977u, tick);
        for (int k = 0; k < 2; ++k) {
            const uint32_t hh = hashMix(seed, static_cast<uint32_t>(k));
            const V3 at = a + (b - a) * unitOf(hh) + V3{unitOf(hh >> 4) - 0.5f, unitOf(hh >> 12) - 0.5f, h * unitOf(hh >> 20) * std::min(1.0f, static_cast<float>(w.fire) / 90.0f)};
            emit(Particle::Kind::Fire, at, {0, 0, 0.9f + unitOf(hh >> 8)}, 0.5f + unitOf(hh >> 16) * 0.5f, 0.35f + unitOf(hh >> 24) * 0.3f);
        }
        if ((tick + i) % 10 == 0) emit(Particle::Kind::Smoke, (a + b) * 0.5f + V3{0, 0, h + 0.5f}, {0, 0, 1.4f}, 4.0f, 0.9f);
    }
    for (const sim::Unit& u : world_->units()) {
        if (u.burning <= 0 || !u.alive() || (tick + u.id) % 3 != 0) continue;
        const uint32_t h = hashMix(u.id, tick);
        const V3 at = toV3(u.pos) + V3{unitOf(h) - 0.5f, unitOf(h >> 8) - 0.5f, static_cast<float>(u.stats.height.toDouble()) * unitOf(h >> 16)};
        emit(Particle::Kind::Fire, at, {0, 0, 0.8f}, 0.5f, 0.3f + unitOf(h >> 6) * 0.2f);
        if ((tick + u.id) % 12 == 0) emit(Particle::Kind::Smoke, at + V3{0, 0, 0.5f}, {0, 0, 1.4f}, 3.0f, 0.7f);
    }
    {
        std::vector<V3> fires;
        for (const Smoulder& s : smouldering_)
            if (tick - s.since < 15 * sim::kTicksPerSecond) fires.push_back(toV3(world_->units()[s.unit - 1].pos));
        for (const sim::GroundFire& f : world_->fires()) fires.push_back(toV3(f.at));
        sfx_.setFires(fires);
        // Who struck this tick (their attack clips begin now): the blows' sounds wait for the frame that shows them.
        std::vector<Sfx::Striker> strikers;
        const auto& units = world_->units();
        for (size_t i = 0; i < units.size() && i < attackTick_.size(); ++i)
            if (attackTick_[i] == tick) strikers.push_back({toV3(units[i].pos), static_cast<uint8_t>(units[i].stats.type), units[i].owner});
        sfx_.setStrikers(std::move(strikers));
    }
    sfx_.observe(*world_, outcome(), *continent_, origin(), scenario_ == Scenario::Siege || scenario_ == Scenario::Capital);
}

}  // namespace illyria::game
