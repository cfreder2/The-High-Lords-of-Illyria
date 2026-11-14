// All-computer Conquest, headless: how fast a war is won and whether any side snowballs or the war stalls.
//   illyria_conquestsim [days] [hard|easy] [seed ...]
// Every five days it prints each side's towns; at the end the day the war was decided (or that it was not), the
// largest lead anyone held, and the fights fought. Integers and the seeded Rng only, like the game: the same seed
// gives the same war.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "illyria/sim/conquest.h"

using namespace illyria::sim;

int main(int argc, char** argv) {
    int days = argc > 1 ? std::atoi(argv[1]) : 30;
    const bool hard = argc > 2 && std::strcmp(argv[2], "easy") != 0;
    std::vector<uint64_t> seeds;
    for (int i = 3; i < argc; ++i) seeds.push_back(std::strtoull(argv[i], nullptr, 10));
    if (seeds.empty()) seeds = {1, 2, 3};
    static const Continent continent(20261027);
    for (uint64_t seed : seeds) {
        Conquest c(continent, seed);
        for (uint8_t s = 0; s < 8; ++s) c.setDifficulty(s, hard ? Difficulty::Hard : Difficulty::Easy);
        const auto start = std::chrono::steady_clock::now();
        int decided = -1, lead = 0, mostFirst = 0;
        std::printf("seed %llu (%s lords)\n day   towns by side                        men   fights\n", static_cast<unsigned long long>(seed), hard ? "hard" : "easy");
        for (int day = 1; day <= days && decided < 0; ++day) {
            for (int t = 0; t < Conquest::kTicksPerDay && !c.winner(); ++t) c.step();
            int most = 0;
            for (uint8_t s = 0; s < 8; ++s) most = std::max(most, c.citiesOwned(s));
            lead = std::max(lead, most);
            if (c.winner() && decided < 0) decided = day;
            if (day % 5 == 0 || decided >= 0) {
                int men = 0;
                for (const Conquest::Stack& st : c.stacks())
                    for (const Army& a : st.armies) men += a.soldiers;
                std::printf(" %3d  ", day);
                for (uint8_t s = 0; s < 8; ++s) std::printf("%3d", c.citiesOwned(s));
                std::printf("   %6d   %zu\n", men, c.fights().size());
            }
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        int alive = 0;
        for (uint8_t s = 0; s < 8; ++s) alive += c.citiesOwned(s) > 0;
        (void)mostFirst;
        std::printf(" -> %s; most towns held by one side %d of 80; sides with a town %d; %.0f s\n", decided >= 0 ? "decided" : "undecided", lead, alive, secs);
        if (decided >= 0) std::printf("    decided on day %d, winner side %d\n", decided, c.winner());
    }
    return 0;
}
