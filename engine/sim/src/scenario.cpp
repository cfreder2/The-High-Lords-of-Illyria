#include "illyria/sim/scenario.h"

#include <cstddef>

namespace illyria::sim {

void runReplay(World& world, const Replay& replay, uint32_t ticks,
               const std::function<void(const World&)>& onTick) {
    std::vector<Command> batch;
    std::size_t next = 0;
    for (uint32_t i = 0; i < ticks; ++i) {
        batch.clear();
        while (next < replay.commands.size() && replay.commands[next].tick == world.tick()) {
            batch.push_back(replay.commands[next++].command);
        }
        world.step(batch);
        if (onTick) onTick(world);
    }
}

namespace scenarios {

namespace {
constexpr PlayerId kArchers = 0;
constexpr PlayerId kInvaders = 1;
constexpr int kArcherCount = 6;
constexpr int kFootmanCount = 10;

Fixed m(int64_t metres) { return Fixed::fromInt(metres); }
}  // namespace

UnitStats archer() {
    UnitStats s;
    s.radius = Fixed::fromRatio(2, 5);
    s.height = Fixed::fromRatio(9, 5);
    s.moveSpeed = Fixed::fromRatio(5, 2);
    s.maxHp = 40;
    s.ranged.muzzleSpeed = m(55);
    s.ranged.reloadTicks = 2 * kTicksPerSecond;
    s.ranged.damage = 18;
    s.ranged.dispersionAt100m = m(4);
    return s;
}

UnitStats footman() {
    UnitStats s;
    s.radius = Fixed::fromRatio(1, 2);
    s.height = Fixed::fromRatio(19, 10);
    s.moveSpeed = Fixed::fromRatio(3, 2);
    s.maxHp = 60;
    return s;
}

void Ford::setup(World& world) {
    const Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    const Vec3 west = -east;
    for (int i = 0; i < kArcherCount; ++i) {
        // EntityIds 1..6, standing on a 4 m rise.
        world.spawnUnit(kArchers, {m(0), m(i * 3 - 8), m(4)}, east, archer());
    }
    for (int i = 0; i < kFootmanCount; ++i) {
        // EntityIds 7..16, a two-file column 110 m away.
        world.spawnUnit(kInvaders, {m(110 + (i / 2) * 2), m((i % 2) * 2 - 1), m(0)}, west,
                        footman());
    }
}

Replay Ford::replay(uint64_t seed) {
    Replay r;
    r.config.seed = seed;
    // A stiff northerly crosswind pushing arrows south.
    r.config.wind = {Fixed::zero(), -Fixed::fromRatio(3, 2), Fixed::zero()};

    for (EntityId id = kArcherCount + 1; id <= kArcherCount + kFootmanCount; ++id) {
        r.commands.push_back({0, {kInvaders, id, Command::Type::Move, {m(0), m(0), m(0)}}});
    }
    // The archers re-aim every 3 seconds to lead the advancing column, but do
    // not correct for the wind.
    for (uint32_t t = 0; t < Ford::kTicks; t += 3 * kTicksPerSecond) {
        const int64_t columnX = 110 - static_cast<int64_t>(t) * 3 / 2 / kTicksPerSecond - 4;
        for (EntityId id = 1; id <= kArcherCount; ++id) {
            r.commands.push_back(
                {t, {kArchers, id, Command::Type::AttackGround, {m(columnX), m(0), m(0)}}});
        }
    }
    return r;
}

}  // namespace scenarios
}  // namespace illyria::sim
