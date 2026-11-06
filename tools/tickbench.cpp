// Times the Conquest tick with the whole population in one World (docs/ONE_WORLD.md, "What it costs"): an all-computer
// campaign from the start, then the same with stacks added in every town until the island holds about 3,000 and
// 6,800 men. Prints ms a tick for each size and the checksum of the world after the timed ticks, so an optimisation
// can be shown to change speed and not the result.
//   illyria_tickbench [ticks=200] [men ...]        (default sizes: 0 3000 6800)
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "illyria/sim/campaign.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/units.h"

using namespace illyria::sim;

int main(int argc, char** argv) {
    const int ticks = argc > 1 ? std::atoi(argv[1]) : 200;
    std::vector<int32_t> sizes;
    for (int i = 2; i < argc; ++i) sizes.push_back(std::atoi(argv[i]));
    if (sizes.empty()) sizes = {0, 3000, 6800};
    const Continent continent(20261027);
    Campaign c(continent, 7);
    c.prepareWorld();
    c.step();
    size_t town = 0;
    for (const int32_t want : sizes) {
        while (c.menInWorld() < want) {
            const Campaign::CityState& cs = c.cities()[town++ % c.cities().size()];
            c.raise(cs.owner == Campaign::kNeutral ? 0 : cs.owner, cs.cx, cs.cy, std::vector<UnitType>(8, UnitType::LightInfantry));
            if (town % 8 == 0) c.step();
        }
        c.step();
        const int32_t men = c.menInWorld();
        // In batches of 20 ticks: the mean, and the quickest batch (a machine shared with other work is noisy).
        double total = 0, best = 1e30;
        int done = 0;
        while (done < ticks) {
            const int n = std::min(20, ticks - done);
            const auto t0 = std::chrono::steady_clock::now();
            for (int k = 0; k < n; ++k) c.step();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            total += ms;
            best = std::min(best, ms / n);
            done += n;
        }
        const double ms = total / ticks;
        std::printf("%d men: %.3f ms a tick (best batch %.3f), %.2f us a man, checksum %016" PRIx64 "\n", men, ms, best,
                    ms * 1000.0 / std::max(1, men), c.checksum());
        std::fflush(stdout);
    }
    return 0;
}
