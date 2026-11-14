#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "illyria/sim/conquest.h"
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
    run<scenarios::Oakhaven>("oakhaven", seed);
    run<scenarios::Raid>("raid", seed);
    // The garrisons of the continent's castles, put in their stations at the start of a conquest (World::garrisonPosts): the
    // world's checksum after seating every town's men, for each lord.
    static const Continent continent(20261027);
    for (uint8_t lord = 0; lord < 8; lord += 3) {
        Conquest c(continent, seed, Conquest::Setup{lord, 0xFF});
        c.setHuman(0, true);
        c.prepareWorld();
        std::printf("garrisons lord %u  men %d  checksum %016" PRIx64 "\n", lord, c.menInWorld(), c.world()->checksum());
    }
    return 0;
}
