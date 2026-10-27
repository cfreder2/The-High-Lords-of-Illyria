#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "illyria/sim/scenario.h"

using namespace illyria::sim;

int main(int argc, char** argv) {
    const uint64_t seed = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 1234;
    const Replay replay = scenarios::Ford::replay(seed);
    World world(replay.config);
    scenarios::Ford::setup(world);
    runReplay(world, replay, scenarios::Ford::kTicks, [](const World& w) {
        if (w.tick() % kTicksPerSecond == 0) {
            std::printf("tick %4" PRIu32 "  projectiles %3zu  checksum %016" PRIx64 "\n", w.tick(),
                        w.projectiles().size(), w.checksum());
        }
    });
    return 0;
}
