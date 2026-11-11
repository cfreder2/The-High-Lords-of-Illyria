// The minimaps as text and land (game/include/illyria/game/minimap.h): the conquest's, and a battle's, one set of lines.
#include "illyria/game/minimap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <tuple>
#include <vector>

#include "illyria/game/battle.h"
#include "illyria/game/factions.h"

namespace illyria::game {

namespace {

std::string hex(const V3& col) { return palette::hex(col); }

struct Man {
    uint8_t owner;
    float x, y;
};

// The men a viewer is shown, binned `bin` metres square, by side: a bin of more than kApart is a heap (its middle, by how
// many), fewer are points where they stand. An owner past the eight lords is the grey of nobody's (a garrison).
void appendMen(std::string& out, const std::vector<Man>& men, float bin) {
    constexpr int kApart = 2;
    struct Bin {
        int n = 0;
        float x = 0, y = 0;
        std::vector<std::pair<float, float>> men;
    };
    char buf[160];
    std::map<std::pair<int, int>, Bin> bins;  // by cell, a side at a time below
    for (int side = 0; side <= sim::Continent::kFactionCount; ++side) {
        const uint8_t owner = side < sim::Continent::kFactionCount ? static_cast<uint8_t>(side) : sim::Continent::kNeutral;
        bins.clear();
        for (const Man& m : men) {
            if ((m.owner < sim::Continent::kFactionCount ? m.owner : sim::Continent::kNeutral) != owner) continue;
            Bin& b = bins[{static_cast<int>(m.x / bin), static_cast<int>(m.y / bin)}];
            ++b.n;
            b.x += m.x;
            b.y += m.y;
            if (b.n <= kApart) b.men.push_back({m.x, m.y});
        }
        const std::string col = hex(factionColor(owner));
        for (const auto& [cell, b] : bins) {
            if (b.n > kApart) std::snprintf(buf, sizeof buf, "k|%s|%.0f|%.0f|%d\n", col.c_str(), b.x / static_cast<float>(b.n), b.y / static_cast<float>(b.n), b.n), out += buf;
            else
                for (const auto& [x, y] : b.men) std::snprintf(buf, sizeof buf, "d|%s|%.0f|%.0f\n", col.c_str(), x, y), out += buf;
        }
    }
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
                      hex(factionColor(c.knownOwner(viewer, static_cast<int32_t>(i)))).c_str(), town.capital ? 1 : town.walled ? 0 : 2);  // a capital, a walled town, a village
        out += buf;
    }
    for (size_t k = 0; k < c.sites().size(); ++k) {
        const auto& site = c.continent().sites()[k];
        std::snprintf(buf, sizeof buf, "s|%.0f|%.0f|%d\n", site.pos.x.toDouble(), site.pos.y.toDouble(), static_cast<int>(site.kind));
        out += buf;
    }
    // The men the viewer is shown, 40 m bins.
    std::vector<C::Soldier> men;
    c.visibleSoldiers(viewer, 0, men);
    {
        std::vector<Man> shown;
        shown.reserve(men.size());
        for (const C::Soldier& m : men)
            if (m.alive) shown.push_back({m.owner, m.x, m.y});
        appendMen(out, shown, 40.0f);
    }
    // Fights: where men are fighting now, binned 80 m square, a glow each in the page.
    {
        std::map<std::pair<int, int>, std::tuple<int, float, float>> fights;
        const uint32_t now = c.tick();
        for (const C::Soldier& m : men) {
            if (!m.alive || now - m.fightTick > 2 * C::kTicksPerSecond) continue;
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

std::string battleMinimapText(const Battle& b) {
    std::string out;
    char buf[160];
    std::snprintf(buf, sizeof buf, "m|%d|%d\n", static_cast<int>(Battle::kWorldSize), static_cast<int>(Battle::kWorldSize));
    out = buf;
    // Every man alive that the viewer may see (Battle::sees: a field with fog leaves out the foe it cannot see).
    std::vector<Man> shown;
    const auto& units = b.world().units();
    shown.reserve(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        if (!u.present() || !u.alive()) continue;
        const V3 p = b.unitPosition(i);
        if (!b.sees(u.owner, p.x, p.y)) continue;
        shown.push_back({u.owner, p.x, p.y});
    }
    appendMen(out, shown, 8.0f);
    // Fights: where men struck or were struck in the last two seconds, binned 24 m.
    {
        std::map<std::pair<int, int>, std::tuple<int, float, float>> fights;
        const uint32_t now = b.world().tick(), recent = 2 * sim::kTicksPerSecond;
        for (size_t i = 0; i < units.size(); ++i) {
            const sim::Unit& u = units[i];
            if (!u.present() || !u.alive()) continue;
            const uint32_t struck = std::min(b.attackTick(i), b.hitTick(i));
            if (struck == UINT32_MAX || now < struck || now - struck > recent) continue;
            const V3 p = b.unitPosition(i);
            if (!b.sees(u.owner, p.x, p.y)) continue;
            auto& f = fights[{static_cast<int>(p.x / 24), static_cast<int>(p.y / 24)}];
            ++std::get<0>(f);
            std::get<1>(f) += p.x;
            std::get<2>(f) += p.y;
        }
        for (const auto& [cell, f] : fights) {
            const int n = std::get<0>(f);
            std::snprintf(buf, sizeof buf, "f|%.0f|%.0f|%d\n", std::get<1>(f) / static_cast<float>(n), std::get<2>(f) / static_cast<float>(n), n);
            out += buf;
        }
    }
    return out;
}

std::vector<uint8_t> battleMinimapLand(const Battle& b, int w, int h) {
    const sim::Region& region = b.worldRegion();
    const float size = Battle::kWorldSize;
    std::vector<float> height(static_cast<size_t>(w) * static_cast<size_t>(h));
    std::vector<sim::Ground> ground(height.size());
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const float x = size * (static_cast<float>(i) + 0.5f) / static_cast<float>(w), y = size - size * (static_cast<float>(j) + 0.5f) / static_cast<float>(h);
            const size_t at = static_cast<size_t>(j) * static_cast<size_t>(w) + static_cast<size_t>(i);
            height[at] = b.heightAt(x, y);
            ground[at] = region.groundAt(toFixed(x), toFixed(y));
        }
    std::vector<uint8_t> rgba(height.size() * 4, 255);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) {
            const size_t at = static_cast<size_t>(j) * static_cast<size_t>(w) + static_cast<size_t>(i);
            const auto hh = [&](int di, int dj) { return height[static_cast<size_t>(std::clamp(j + dj, 0, h - 1)) * static_cast<size_t>(w) + static_cast<size_t>(std::clamp(i + di, 0, w - 1))]; };
            // Lit from the north-west, as the conquest's land is.
            const float lit = std::clamp(0.5f + (hh(-1, -1) - hh(1, 1)) * 0.12f, 0.0f, 1.0f);
            V3 c;
            switch (ground[at]) {
                case sim::Ground::Road: c = {0.58f, 0.50f, 0.34f}; break;
                case sim::Ground::Forest: c = {0.12f, 0.24f, 0.10f}; break;
                case sim::Ground::Swamp: c = {0.24f, 0.31f, 0.18f}; break;
                case sim::Ground::Shallows: c = {0.28f, 0.50f, 0.58f}; break;
                case sim::Ground::DeepWater: c = {0.10f, 0.22f, 0.34f}; break;
                case sim::Ground::Rock: c = {0.55f, 0.53f, 0.50f}; break;
                case sim::Ground::Bridge: c = {0.50f, 0.40f, 0.28f}; break;
                case sim::Ground::Mud: c = {0.36f, 0.30f, 0.20f}; break;
                case sim::Ground::Sand: c = {0.80f, 0.73f, 0.55f}; break;
                default: c = {0.34f, 0.47f, 0.22f}; break;
            }
            const bool water = ground[at] == sim::Ground::Shallows || ground[at] == sim::Ground::DeepWater;
            if (!water) c = c * (0.7f + 0.6f * lit);
            for (int k = 0; k < 3; ++k) rgba[at * 4 + static_cast<size_t>(k)] = static_cast<uint8_t>(std::clamp((&c.x)[k], 0.0f, 1.0f) * 255);
        }
    return rgba;
}

}  // namespace illyria::game
