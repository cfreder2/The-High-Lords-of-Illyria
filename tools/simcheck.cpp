#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "illyria/sim/scenario.h"

using namespace illyria::sim;

template <typename Scenario>
void run(const char* name, uint64_t seed) {
    const Replay replay = Scenario::replay(seed);
    World world(replay.config);
    Scenario::setup(world);
    runReplay(world, replay, Scenario::kTicks, [&](const World& w) {
        if (w.tick() % kTicksPerSecond == 0) {
            std::printf("%s tick %4" PRIu32 "  projectiles %3zu  checksum %016" PRIx64 "\n", name, w.tick(),
                        w.projectiles().size(), w.checksum());
        }
    });
}

int main(int argc, char** argv) {
    const uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1234;
    run<scenarios::Ford>("ford", seed);
    run<scenarios::Melee>("melee", seed);
    run<scenarios::Crossing>("crossing", seed);
    return 0;
}
