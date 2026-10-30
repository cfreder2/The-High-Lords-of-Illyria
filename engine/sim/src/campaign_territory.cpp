// Territory (docs/GAME_DESIGN.md §4.2.9), after the little towers with flags
// that Warlords sets by its river crossings: the map's campaign hexes (267 m
// between centres, the grid the map draws) each fly a side's banner, or none.
// Ten times a day (every kTerritoryTicks):
// - a hex where one side's armies stand, and nobody else's, is claimed for it:
//   10 points, and 5 more an army (four at most); another side's claim is
//   worn down first. Two sides there: nothing changes;
// - a city claims its own hex and the six round it as two armies would, and
//   an outpost tower its holder holds (half a claim or more) the six round it
//   as one army; an outpost's own hex is claimed twice as fast;
// - a hex nobody stands in loses a point (a full claim fades in ten days),
//   and is nobody's at zero.
// The land is worth a little gold (Campaign::income: one a day for every
// kHexesPerGold hexes) and the home-land edge in battle (edges.h).
// Deterministic: integers, hexes and stacks in index order.
#include <algorithm>

#include "illyria/sim/campaign.h"
#include "illyria/sim/hash.h"

namespace illyria::sim {

namespace {
constexpr int32_t kClaimBase = 10, kClaimPerArmy = 5, kClaimArmies = 4;
constexpr int32_t kCityPresence = 2, kOutpostPresence = 1;
constexpr int32_t kSides = 8;
}  // namespace

int32_t Campaign::hexOfMetres(int32_t x, int32_t y) {
    // The nearest centre of the two rows either side (in centimetres, exact).
    const int64_t px = int64_t(x) * 100, py = int64_t(y) * 100;
    const int64_t r0 = std::clamp<int64_t>(py / kHexRowX100, 0, kHexRows - 1);
    int64_t best = INT64_MAX;
    int32_t hex = 0;
    for (int64_t r = r0; r <= std::min<int64_t>(r0 + 1, kHexRows - 1); ++r) {
        const int64_t shift = (r & 1) * kHexSpacing * 50;
        const int64_t col = std::clamp<int64_t>((px - shift + kHexSpacing * 50) / (kHexSpacing * 100), 0, kHexCols - 1);
        const int64_t dx = px - (col * kHexSpacing * 100 + shift), dy = py - r * kHexRowX100;
        const int64_t d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            hex = static_cast<int32_t>(r * kHexCols + col);
        }
    }
    return hex;
}

void Campaign::hexCentre(int32_t hex, int32_t& x, int32_t& y) {
    const int32_t r = hex / kHexCols, col = hex % kHexCols;
    x = col * kHexSpacing + (r & 1) * kHexSpacing / 2;
    y = static_cast<int32_t>(int64_t(r) * kHexRowX100 / 100);
}

void Campaign::hexNeighbours(int32_t hex, std::vector<int32_t>& out) {
    // Odd rows sit half a hex east: their diagonal neighbours are this column and the next.
    const int32_t r = hex / kHexCols, col = hex % kHexCols;
    const int32_t odd = r & 1;
    const int32_t around[6][2] = {{-1, 0}, {1, 0}, {odd - 1, -1}, {odd, -1}, {odd - 1, 1}, {odd, 1}};
    for (const auto& d : around) {
        const int32_t c = col + d[0], rr = r + d[1];
        if (c >= 0 && rr >= 0 && c < kHexCols && rr < kHexRows) out.push_back(rr * kHexCols + c);
    }
}

void Campaign::initTerritory() {
    hexes_.assign(static_cast<size_t>(kHexCols) * kHexRows, Hex{});
    cellHex_.assign(static_cast<size_t>(nx_) * ny_, 0);
    for (int32_t y = 0; y < ny_; ++y)
        for (int32_t x = 0; x < nx_; ++x) {
            const int32_t h = hexOfMetres(centre(x), centre(y));
            cellHex_[static_cast<size_t>(y) * nx_ + x] = static_cast<int16_t>(h);
            if (terrainAt(x, y) != Lay::Water) hexes_[static_cast<size_t>(h)].land = true;
        }
    // Outpost towers at the river crossings, one to a hex.
    for (const Continent::Crossing& k : continent_.crossings()) {
        const int32_t cx = std::clamp(cellOf(static_cast<int32_t>(k.pos.x.floorToInt())), 0, nx_ - 1);
        const int32_t cy = std::clamp(cellOf(static_cast<int32_t>(k.pos.y.floorToInt())), 0, ny_ - 1);
        Hex& h = hexes_[static_cast<size_t>(hexAt(cx, cy))];
        if (h.outpost >= 0 || !h.land) continue;
        h.outpost = static_cast<int32_t>(outposts_.size());
        outposts_.push_back({hexAt(cx, cy), cx, cy, k.name});
    }
    // The realms begin holding the land round their capitals.
    for (const CityState& c : cities_) {
        if (c.owner == kNeutral) continue;
        std::vector<int32_t> ring{hexAt(c.cx, c.cy)};
        hexNeighbours(ring.front(), ring);
        for (int32_t h : ring) {
            Hex& hex = hexes_[static_cast<size_t>(h)];
            if (!hex.land) continue;
            hex.owner = c.owner;
            hex.claim = kClaimFull;
        }
    }
}

int32_t Campaign::hexesHeld(uint8_t side) const {
    int32_t n = 0;
    for (const Hex& h : hexes_) n += h.owner == side;
    return n;
}

void Campaign::territoryStep() {
    // Who stands in each hex: armies, cities (their own hex and the ring
    // round it) and held outposts (the ring round them).
    std::vector<int16_t> here(hexes_.size() * kSides, 0);
    const auto add = [&](int32_t hex, uint8_t side, int32_t n) {
        int16_t& v = here[static_cast<size_t>(hex) * kSides + side];
        v = static_cast<int16_t>(std::min(1000, v + n));
    };
    for (const Stack& s : stacks_)
        if (s.owner < kSides && !s.armies.empty() && !s.boat) add(hexAt(s.cx, s.cy), s.owner, static_cast<int32_t>(s.armies.size()));
    std::vector<int32_t> ring;
    for (const CityState& c : cities_) {
        if (c.owner >= kSides || c.razed) continue;
        ring.assign(1, hexAt(c.cx, c.cy));
        hexNeighbours(ring.front(), ring);
        for (int32_t h : ring) add(h, c.owner, kCityPresence);
    }
    for (const Outpost& o : outposts_) {
        const Hex& h = hexes_[static_cast<size_t>(o.hex)];
        if (h.owner >= kSides || h.claim < kClaimFull / 2) continue;
        ring.clear();
        hexNeighbours(o.hex, ring);
        for (int32_t n : ring) add(n, h.owner, kOutpostPresence);
    }
    for (size_t k = 0; k < hexes_.size(); ++k) {
        Hex& h = hexes_[k];
        if (!h.land) continue;
        int32_t sides = 0, n = 0;
        uint8_t who = kNeutral;
        for (uint8_t s = 0; s < kSides; ++s) {
            const int16_t v = here[k * kSides + s];
            if (v <= 0) continue;
            ++sides;
            who = s;
            n = v;
        }
        if (sides > 1) continue;  // contested: it stays as it is
        if (sides == 0) {
            if (h.claim > 0 && --h.claim == 0) h.owner = kNeutral;
            continue;
        }
        int32_t gain = kClaimBase + kClaimPerArmy * std::min(n, kClaimArmies);
        if (h.outpost >= 0) gain *= 2;
        if (h.owner == who) {
            h.claim = std::min(kClaimFull, h.claim + gain);
        } else if (h.owner == kNeutral || h.claim <= gain) {
            // Nobody's, or theirs worn away: the banner changes.
            h.claim = h.owner == kNeutral ? gain : std::max(1, gain - h.claim);
            h.owner = who;
        } else {
            h.claim -= gain;
        }
    }
}

void Campaign::hashTerritory(StateHasher& h) const {
    for (const Hex& x : hexes_) {
        h.addU64(x.owner);
        h.addI64(x.claim);
    }
}

}  // namespace illyria::sim
