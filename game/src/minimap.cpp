// The conquest's minimap as text (game/include/illyria/game/minimap.h).
#include "illyria/game/minimap.h"

#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

#include "illyria/game/factions.h"

namespace illyria::game {

namespace {

std::string hex(const V3& col) {
    char h[8];
    std::snprintf(h, sizeof h, "#%02x%02x%02x", static_cast<int>(col.x * 255), static_cast<int>(col.y * 255), static_cast<int>(col.z * 255));
    return h;
}

}  // namespace

std::string minimapText(const sim::Campaign& c, uint8_t viewer) {
    using C = sim::Campaign;
    std::string out;
    char buf[160];
    std::snprintf(buf, sizeof buf, "m|%d|%d\n", static_cast<int>(sim::Continent::kWidth), static_cast<int>(sim::Continent::kHeight));
    out = buf;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const auto& town = c.continent().cities()[i];
        std::snprintf(buf, sizeof buf, "t|%.0f|%.0f|%s|%d\n", town.pos.x.toDouble(), town.pos.y.toDouble(),
                      hex(factionColor(c.knownOwner(viewer, static_cast<int32_t>(i)))).c_str(), town.capital ? 1 : 0);
        out += buf;
    }
    for (size_t k = 0; k < c.sites().size(); ++k) {
        const auto& site = c.continent().sites()[k];
        std::snprintf(buf, sizeof buf, "s|%.0f|%.0f|%d\n", site.pos.x.toDouble(), site.pos.y.toDouble(), static_cast<int>(site.kind));
        out += buf;
    }
    // The men the viewer is shown, binned 40 m square, by side: a bin of more than kApart is a heap (its middle, by
    // how many), fewer are points where they stand.
    std::vector<C::Soldier> men;
    c.visibleSoldiers(viewer, 0, men);
    constexpr float kBin = 40.0f;
    constexpr int kApart = 2;
    struct Bin {
        int n = 0;
        float x = 0, y = 0;
        std::vector<std::pair<float, float>> men;
    };
    std::map<std::pair<int, int>, Bin> bins;  // by cell, a side at a time below
    for (int side = 0; side <= sim::Continent::kFactionCount; ++side) {
        const uint8_t owner = side < sim::Continent::kFactionCount ? static_cast<uint8_t>(side) : C::kNeutral;
        bins.clear();
        for (const C::Soldier& m : men) {
            if (m.owner != owner || !m.alive) continue;
            Bin& b = bins[{static_cast<int>(m.x / kBin), static_cast<int>(m.y / kBin)}];
            ++b.n;
            b.x += m.x;
            b.y += m.y;
            if (b.n <= kApart) b.men.push_back({m.x, m.y});
        }
        const std::string col = hex(factionColor(owner));
        for (const auto& [cell, b] : bins) {
            if (b.n > kApart) std::snprintf(buf, sizeof buf, "k|%s|%.0f|%.0f|%d\n", col.c_str(), b.x / b.n, b.y / b.n, b.n), out += buf;
            else
                for (const auto& [x, y] : b.men) std::snprintf(buf, sizeof buf, "d|%s|%.0f|%.0f\n", col.c_str(), x, y), out += buf;
        }
    }
    // Fights: where men are fighting now, binned 80 m square, a glow each in the page.
    {
        std::map<std::pair<int, int>, std::tuple<int, float, float>> fights;
        const uint32_t now = c.tick();
        for (const C::Soldier& m : men) {
            if (!m.awake || !m.alive || now - m.fightTick > 2 * C::kTicksPerSecond) continue;
            auto& f = fights[{static_cast<int>(m.x / 80), static_cast<int>(m.y / 80)}];
            ++std::get<0>(f);
            std::get<1>(f) += m.x;
            std::get<2>(f) += m.y;
        }
        for (const auto& [cell, f] : fights) {
            const int n = std::get<0>(f);
            std::snprintf(buf, sizeof buf, "f|%.0f|%.0f|%d\n", std::get<1>(f) / static_cast<float>(n), std::get<2>(f) / static_cast<float>(n), n);
            out += buf;
        }
    }
    return out;
}

}  // namespace illyria::game
