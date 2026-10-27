#include "illyria/sim/scenario.h"

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"

#include <cstddef>
#include <memory>

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

// The Ford is laid out around this point of a 256 m terrain patch.
constexpr int64_t kOx = 80, kOy = 128;
constexpr int64_t kColumnStart = 70;  // metres east of the archers
}  // namespace

UnitStats archer() { return unitStats(UnitType::Archers); }

UnitStats footman() { return unitStats(UnitType::LightInfantry); }

void Ford::setup(World& world) {
    const Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    const Vec3 west = -east;
    for (int i = 0; i < kArcherCount; ++i) {
        // EntityIds 1..6, standing on a 4 m rise.
        world.spawnUnit(kArchers, {m(kOx), m(kOy + i * 3 - 8), m(0)}, east, archer());
    }
    for (int i = 0; i < kFootmanCount; ++i) {
        // EntityIds 7..16, a two-file column.
        world.spawnUnit(kInvaders,
                        {m(kOx + kColumnStart + (i / 2) * 2), m(kOy + (i % 2) * 2 - 1), m(0)},
                        west, footman());
    }
}

Replay Ford::replay(uint64_t seed) {
    Replay r;
    r.config.seed = seed;
    // A stiff northerly crosswind pushing arrows south.
    r.config.wind = {Fixed::zero(), -Fixed::fromRatio(3, 2), Fixed::zero()};
    r.config.terrain = std::make_shared<const Terrain>(
        Terrain::generate(seed, 64, Fixed::fromInt(4), Fixed::fromInt(24)));

    for (EntityId id = kArcherCount + 1; id <= kArcherCount + kFootmanCount; ++id) {
        r.commands.push_back({0, {kInvaders, id, Command::Type::Move, {m(kOx + 10), m(kOy), m(0)}}});
    }
    // For the first 30 seconds the archers re-aim every 3 seconds to lead the
    // advancing column, without correcting for the wind. After that they are
    // left to fight on their own (auto-fire, then melee).
    for (uint32_t t = 0; t < 30 * kTicksPerSecond; t += 3 * kTicksPerSecond) {
        const int64_t columnX = kOx + kColumnStart - static_cast<int64_t>(t) * 3 / 2 / kTicksPerSecond - 4;
        for (EntityId id = 1; id <= kArcherCount; ++id) {
            r.commands.push_back(
                {t, {kArchers, id, Command::Type::AttackGround, {m(columnX), m(kOy), m(0)}}});
        }
    }
    for (EntityId id = 1; id <= kArcherCount; ++id) {
        r.commands.push_back({30 * kTicksPerSecond, {kArchers, id, Command::Type::Stop, {}}});
    }
    return r;
}

void Melee::setup(World& world) {
    const Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    for (PlayerId side = 0; side < 2; ++side) {
        const int64_t x = side == 0 ? 98 : 158;
        const Vec3 facing = side == 0 ? east : -east;
        for (int t = 0; t < kUnitTypeCount; ++t) {
            world.spawnUnit(side, {m(x), m(60 + t * 5), m(0)}, facing, unitStats(static_cast<UnitType>(t)));
        }
        for (int i = 0; i < 4; ++i) {
            world.spawnUnit(side, {m(side == 0 ? x - 6 : x + 6), m(100 + i * 6), m(0)}, facing,
                            unitStats(UnitType::DwarfGrenadier));
        }
    }
}

void Crossing::setup(World& world) {
    const Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    // Ids 1..24: the Red Hand west of the river. 25..36: archers on the far bank,
    // 37..40 grenadiers, 41..43 griffins.
    for (int i = 0; i < 24; ++i) world.spawnUnit(1, {m(480 + (i % 6) * 2), m(560 + (i / 6) * 2), m(0)}, east, unitStats(UnitType::LightInfantry));
    for (int i = 0; i < 12; ++i) world.spawnUnit(0, {m(612), m(500 + i * 2), m(0)}, -east, unitStats(UnitType::Archers));
    for (int i = 0; i < 4; ++i) world.spawnUnit(0, {m(598), m(506 + i * 3), m(0)}, -east, unitStats(UnitType::DwarfGrenadier));
    for (int i = 0; i < 3; ++i) world.spawnUnit(0, {m(700), m(470 + i * 4), m(0)}, -east, unitStats(UnitType::Griffins));
}

Replay Crossing::replay(uint64_t seed) {
    Replay r;
    r.config.seed = seed;
    r.config.region = std::make_shared<const Region>(Region::generateIllyria(20261027));
    for (EntityId id = 1; id <= 24; ++id) {
        r.commands.push_back({0, {1, id, Command::Type::Move, {m(610), m(512), m(0)}}});
    }
    for (EntityId id = 41; id <= 43; ++id) {
        r.commands.push_back({0, {0, id, Command::Type::Move, {m(380), m(600), m(0)}}});
    }
    return r;
}

void Oakhaven::setup(World& world) {
    const Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    // Ids 1..24: the Red Hand east of the ford. 25..36: archers on the west
    // bank, 37..40 grenadiers, 41..43 griffins.
    for (int i = 0; i < 24; ++i) world.spawnUnit(1, {m(620 + (i % 6) * 2), m(500 + (i / 6) * 2), m(0)}, -east, unitStats(UnitType::LightInfantry));
    for (int i = 0; i < 12; ++i) world.spawnUnit(0, {m(440), m(488 + i * 2), m(0)}, east, unitStats(UnitType::Archers));
    for (int i = 0; i < 4; ++i) world.spawnUnit(0, {m(452), m(494 + i * 3), m(0)}, east, unitStats(UnitType::DwarfGrenadier));
    for (int i = 0; i < 3; ++i) world.spawnUnit(0, {m(300), m(520 + i * 4), m(0)}, east, unitStats(UnitType::Griffins));
}

Replay Oakhaven::replay(uint64_t seed) {
    Replay r;
    r.config.seed = seed;
    const Continent continent(20261027);
    r.config.region = std::make_shared<const Region>(Region::fromContinent(continent, continent.crossings().front().pos, 256));
    for (EntityId id = 1; id <= 24; ++id) r.commands.push_back({0, {1, id, Command::Type::Move, {m(440), m(500), m(0)}}});
    for (EntityId id = 41; id <= 43; ++id) r.commands.push_back({0, {0, id, Command::Type::Move, {m(760), m(560), m(0)}}});
    return r;
}

Replay Melee::replay(uint64_t seed) {
    Replay r;
    r.config.seed = seed;
    r.config.terrain = std::make_shared<const Terrain>(
        Terrain::generate(seed, 64, Fixed::fromInt(4), Fixed::fromInt(16)));
    // Everyone advances to the middle of the valley, then fights on their own.
    const uint32_t perSide = kUnitTypeCount + 4;
    for (PlayerId side = 0; side < 2; ++side) {
        for (uint32_t i = 0; i < perSide; ++i) {
            const EntityId id = side * perSide + i + 1;
            const int64_t y = i < static_cast<uint32_t>(kUnitTypeCount) ? 60 + i * 5 : 100 + (i - kUnitTypeCount) * 6;
            r.commands.push_back({0, {side, id, Command::Type::Move, {m(side == 0 ? 120 : 136), m(y), m(0)}}});
        }
    }
    return r;
}

}  // namespace scenarios
}  // namespace illyria::sim
