#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "illyria/sim/scenario.h"
#include "test.h"

using namespace illyria::sim;

namespace {

uint64_t runFord(uint64_t seed) {
    const Replay replay = scenarios::Ford::replay(seed);
    World world(replay.config);
    scenarios::Ford::setup(world);
    runReplay(world, replay, scenarios::Ford::kTicks);
    return world.checksum();
}

}  // namespace

TEST(world_is_deterministic) {
    CHECK(runFord(1234) == runFord(1234));
    CHECK(runFord(1234) != runFord(1235));
}

TEST(world_matches_golden_checksum) {
    // Pinned result of the Ford scenario. Every platform and compiler (native
    // x86-64/ARM, WebAssembly) must reproduce it bit for bit; if a deliberate
    // simulation change alters it, update the constant in the same commit.
    const uint64_t actual = runFord(1234);
    constexpr uint64_t kGolden = 0x740ce730add205be;
    if (actual != kGolden) std::printf("  Ford checksum: 0x%016llx\n", static_cast<unsigned long long>(actual));
    CHECK(actual == kGolden);
}

TEST(world_command_order_between_players_does_not_matter) {
    Replay a = scenarios::Ford::replay(99);
    Replay b = a;
    // Reverse the order commands from different players arrive within each tick.
    std::stable_sort(b.commands.begin(), b.commands.end(), [](const auto& x, const auto& y) {
        return x.tick != y.tick ? x.tick < y.tick : x.command.player > y.command.player;
    });
    World wa(a.config), wb(b.config);
    scenarios::Ford::setup(wa);
    scenarios::Ford::setup(wb);
    runReplay(wa, a, 300);
    runReplay(wb, b, 300);
    CHECK(wa.checksum() == wb.checksum());
}

TEST(world_arrows_kill_and_wind_pushes_them) {
    const Replay replay = scenarios::Ford::replay(1234);
    World world(replay.config);
    scenarios::Ford::setup(world);
    int ground = 0, hits = 0, aimedMisses = 0;
    double southDrift = 0;
    runReplay(world, replay, scenarios::Ford::kTicks, [&](const World& w) {
        for (const ImpactEvent& e : w.impacts()) {
            if (e.hitUnit != kNoEntity) {
                ++hits;
            } else {
                ++ground;
                // For the first 30 s the archers aim at the column's centre line, y = 128.
                if (w.tick() < 30 * kTicksPerSecond) {
                    ++aimedMisses;
                    southDrift += 128.0 - e.pos.y.toDouble();
                }
            }
        }
    });
    int dead = 0;
    for (const Unit& u : world.units()) dead += u.owner == 1 && !u.alive();
    std::printf("  %d hits, %d misses, %d footmen dead, mean miss %.1f m south\n", hits, ground,
                dead, aimedMisses ? southDrift / aimedMisses : 0.0);
    CHECK(hits > 0);
    CHECK(dead > 0);
    CHECK(ground > 0);
    CHECK(aimedMisses > 0);
    // The uncorrected crosswind blows arrows south by ~0.5 * 1.5 m/s^2 * flight_time^2.
    CHECK(southDrift / aimedMisses > 0.2);
}

TEST(world_rejects_commands_for_other_players_units) {
    World world(WorldConfig{});
    const EntityId id = world.spawnUnit(0, {}, {Fixed::one(), {}, {}}, scenarios::footman());
    const Command cmd{1, id, Command::Type::Move, {Fixed::fromInt(10), {}, {}}};
    for (int i = 0; i < 30; ++i) world.step(i == 0 ? std::span(&cmd, 1) : std::span<const Command>{});
    CHECK(world.findUnit(id)->pos == Vec3{});
}
