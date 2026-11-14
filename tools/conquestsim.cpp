// All-computer Conquest, headless: how fast a war is won and whether any side snowballs or the war stalls.
//   illyria_conquestsim [days] [hard|easy|mixed] [seed ...]
// (mixed: side 0 a Hard lord among seven Easy ones, the nearest thing here to a person playing on Easy.)
// Every five days it prints each side's towns (a dot: the lord is out of the war), the men under arms and the towns that
// have changed hands; at the end one line of the pacing docs/BALANCE.md tabulates: the day the first lord fell, the day
// a lord first held more than half the towns, the day the war was decided, and the leader's towns every ten days.
// `ILLYRIA_SIM_REALMS=1` adds each lord's armies, gold, income and upkeep to every row.
//   illyria_conquestsim siege [armies] [seat|town][+engines] [seed ...]
// One assault, to see what the men make of a wall (the computer lords' sum of it is ConquestAi::menOdds): so many
// squads of light infantry (so many of them catapults instead), in stacks of eight, march from two cells off into side 1's seat as it stands on the first
// day, or into the walled neutral town nearest side 0's seat. Nobody else moves. It prints the men on each side every
// quarter of a day and when the banner is up.
// Integers and the seeded Rng only, like the game: the same seed gives the same war.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "illyria/sim/conquest.h"

using namespace illyria::sim;

namespace {
void siege(const Continent& continent, int armies, int engines, bool seat, uint64_t seed) {
    Conquest c(continent, seed);
    for (uint8_t s = 0; s < 8; ++s) c.setHuman(s, true);  // nobody marches or trains but by our orders
    int32_t target = c.seatOf(1);
    if (!seat) {
        const Conquest::CityState& home = c.cities()[static_cast<size_t>(c.seatOf(0))];
        int32_t bestD = INT32_MAX;
        for (size_t i = 0; i < c.cities().size(); ++i) {
            const Conquest::CityState& t = c.cities()[i];
            const int32_t d = std::max(std::abs(t.cx - home.cx), std::abs(t.cy - home.cy));
            if (t.owner == Conquest::kNeutral && continent.cities()[i].walled && d < bestD) bestD = d, target = static_cast<int32_t>(i);
        }
    }
    const Conquest::CityState& t = c.cities()[static_cast<size_t>(target)];
    int32_t fx = -1, fy = -1;
    for (int32_t r = 2; r <= 4 && fx < 0; ++r)
        for (int32_t dy = -r; dy <= r && fx < 0; ++dy)
            for (int32_t dx = -r; dx <= r && fx < 0; ++dx) {
                const int32_t x = t.cx + dx, y = t.cy + dy;
                if (std::max(std::abs(dx), std::abs(dy)) != r || x < 0 || y < 0 || x >= c.width() || y >= c.height() || c.cityAt(x, y) >= 0) continue;
                if (c.terrainAt(x, y) == Lay::Plain || c.terrainAt(x, y) == Lay::Road) fx = x, fy = y;
            }
    for (int left = armies; left > 0; left -= Conquest::kMaxStack) {
        Conquest::Order o;
        o.kind = Conquest::Order::Kind::Move;
        o.side = 0;
        std::vector<UnitType> types(static_cast<size_t>(std::min(left, Conquest::kMaxStack)), UnitType::LightInfantry);
        for (size_t k = 0; k < types.size() && engines > 0; ++k, --engines) types[k] = UnitType::Catapult;  // (of the squads, not more)
        o.stack = c.raise(0, fx, fy, types);
        o.a = t.cx, o.b = t.cy;
        c.submit(o);
    }
    const auto count = [&](int& ours, int& theirs) {
        ours = theirs = 0;
        for (const Conquest::Stack& s : c.stacks())
            for (const Army& a : s.armies) {
                if (s.owner == 0 && std::max(std::abs(s.cx - t.cx), std::abs(s.cy - t.cy)) <= 4) ours += a.soldiers;
                else if (s.owner != 0 && s.cx == t.cx && s.cy == t.cy) theirs += a.soldiers;
            }
    };
    int ours, theirs;
    count(ours, theirs);
    std::printf("seed %llu: %d squads (%d men) at %s (%s, walls %d), held by %d men\n", static_cast<unsigned long long>(seed), armies, ours,
                continent.cities()[static_cast<size_t>(target)].name.c_str(), seat ? "a seat" : "a walled town", t.defense, theirs);
    const int was = theirs;
    const int sent = ours;
    for (int q = 1; q <= 16 && t.owner != 0 && ours > 0; ++q) {
        for (int k = 0; k < Conquest::kTicksPerDay / 4 && t.owner != 0; ++k) c.step();
        count(ours, theirs);
        std::printf("  day %.2f: %d of ours, %d of theirs%s\n", c.tick() / double(Conquest::kTicksPerDay), ours, theirs, t.owner == 0 ? "; the banner is up" : "");
        std::fflush(stdout);
    }
    std::printf("  -> | %s | %d | %d | %d | %s | %d | %d |\n", seat ? "seat" : "town", sent, was, sent * 100 / std::max(1, was), t.owner == 0 ? "taken" : "held", ours, theirs);
}
}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "siege") == 0) {
        static const Continent land(20261027);
        const int armies = argc > 2 ? std::atoi(argv[2]) : 8;
        const bool seat = argc <= 3 || std::strncmp(argv[3], "town", 4) != 0;
        const char* plus = argc > 3 ? std::strchr(argv[3], '+') : nullptr;  // seat+2, town+2: so many of the squads are catapults
        const int engines = plus ? std::atoi(plus + 1) : 0;
        if (argc <= 4) siege(land, armies, engines, seat, 1);
        for (int i = 4; i < argc; ++i) siege(land, armies, engines, seat, std::strtoull(argv[i], nullptr, 10));
        return 0;
    }
    int days = argc > 1 ? std::atoi(argv[1]) : 30;
    const bool mixed = argc > 2 && std::strcmp(argv[2], "mixed") == 0;
    const bool hard = argc > 2 && !mixed && std::strcmp(argv[2], "easy") != 0;
    const bool realms = std::getenv("ILLYRIA_SIM_REALMS") != nullptr;
    std::vector<uint64_t> seeds;
    for (int i = 3; i < argc; ++i) seeds.push_back(std::strtoull(argv[i], nullptr, 10));
    if (seeds.empty()) seeds = {1, 2, 3};
    static const Continent continent(20261027);
    for (uint64_t seed : seeds) {
        Conquest c(continent, seed);
        for (uint8_t s = 0; s < 8; ++s) c.setDifficulty(s, hard || (mixed && s == 0) ? Difficulty::Hard : Difficulty::Easy);
        const auto start = std::chrono::steady_clock::now();
        int decided = -1, lead = 0, firstOut = -1, half = -1, out = 0, lastDay = 0, seats = 0;
        int fell[8];
        for (int& f : fell) f = -1;
        std::string curve;
        std::printf("seed %llu (%s lords)\n day   towns by side                        men   fights\n", static_cast<unsigned long long>(seed), hard ? "hard" : mixed ? "one hard, seven easy" : "easy");
        for (int day = 1; day <= days && decided < 0; ++day) {
            for (int t = 0; t < Conquest::kTicksPerDay && !c.winner(); ++t) c.step();
            lastDay = day;
            int most = 0;
            for (uint8_t s = 0; s < 8; ++s) {
                most = std::max(most, c.citiesOwned(s));
                if (fell[s] < 0 && !c.sides()[s].alive) {
                    fell[s] = day, ++out;
                    if (firstOut < 0) firstOut = day;
                    bool stormed = false;
                    for (const Conquest::Fight& f : c.fights())
                        if (f.city == c.seatOf(s) && f.defender == s) {
                            stormed = true;
                            ++seats;
                            std::printf("      day %d: the seat of side %d fell to side %d, who now holds %d towns\n", day, s, f.attacker, c.citiesOwned(f.attacker));
                        }
                    if (!stormed) {
                        const uint8_t lord = c.cities()[static_cast<size_t>(c.seatOf(s))].owner;
                        std::printf("      day %d: side %d bent the knee to side %d, who now holds %d towns\n", day, s, lord, lord < 8 ? c.citiesOwned(lord) : 0);
                    }
                }
            }
            lead = std::max(lead, most);
            if (half < 0 && most * 2 > static_cast<int>(c.cities().size())) half = day;
            if (c.winner() && decided < 0) decided = day;
            if (day % 10 == 0) curve += (curve.empty() ? "" : " ") + std::to_string(most);
            if (day % 5 == 0 || decided >= 0) {
                int men = 0;
                for (const Conquest::Stack& st : c.stacks())
                    for (const Army& a : st.armies) men += a.soldiers;
                std::printf(" %3d  ", day);
                for (uint8_t s = 0; s < 8; ++s) {
                    if (c.sides()[s].alive) std::printf("%3d", c.citiesOwned(s));
                    else std::printf("  .");
                }
                std::printf("   %6d   %zu\n", men, c.fights().size());
                if (realms) {
                    for (uint8_t s = 0; s < 8; ++s) {
                        // (at his seat: the armies standing in it; host: the most he has together in any other one place)
                        if (!c.sides()[s].alive) continue;
                        const Conquest::CityState& seat = c.cities()[static_cast<size_t>(c.seatOf(s))];
                        int armies = 0, home = 0, host = 0;
                        for (const Conquest::Stack& st : c.stacks()) {
                            if (st.owner != s) continue;
                            armies += static_cast<int>(st.armies.size());
                            if (st.cx == seat.cx && st.cy == seat.cy) {
                                home += static_cast<int>(st.armies.size());
                                continue;
                            }
                            int here = 0;
                            for (const Conquest::Stack& o : c.stacks())
                                if (o.owner == s && o.cx == st.cx && o.cy == st.cy) here += static_cast<int>(o.armies.size());
                            host = std::max(host, here);
                        }
                        std::printf("        side %d: armies %3d (at his seat %2d, host %2d) gold %5d income %4d upkeep %4d\n", s, armies, home, host, c.sides()[s].gold,
                                    c.income(s), c.upkeepOf(s));
                    }
                }
                std::fflush(stdout);
            }
        }
        const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf(" -> %s; most towns held by one side %d of 80; lords out %d; %.0f s\n", decided >= 0 ? "decided" : "undecided", lead, out, secs);
        if (decided >= 0) std::printf("    decided on day %d, winner side %d\n", decided, c.winner() - 1);
        std::printf("    lords fell on days:");
        for (int f : fell) f < 0 ? std::printf(" -") : std::printf(" %d", f);
        std::printf("\n");
        // (the table's row)
        const auto dayOr = [](int d) { return d < 0 ? std::string("-") : std::to_string(d); };
        // seed | first lord out | a lord holds 41 | decided | lords out (seats stormed) | the leader's towns every ten days
        std::printf("    | %llu | %s | %s | %s | %d (%d) | %s |\n", static_cast<unsigned long long>(seed), dayOr(firstOut).c_str(), dayOr(half).c_str(),
                    decided >= 0 ? std::to_string(decided).c_str() : ("- (" + std::to_string(lastDay) + ")").c_str(), out, seats, curve.c_str());
        std::fflush(stdout);
    }
    return 0;
}
