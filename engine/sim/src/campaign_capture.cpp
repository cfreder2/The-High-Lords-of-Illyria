// Raising a banner (docs/GAME_DESIGN.md §4.2.8): a city changes hands only by
// being held. Once its defenders are beaten (the dice, or the 3D battle), or
// when an army walks into a city nobody defends, the victors stand in it and
// raise their banner over a few hours of campaign time:
// - each tick it climbs kBannerBase + one point an army in the city (eight at
//   most): 160 ticks for a lone army, 48 for a full stack, of kBannerFull;
// - armies of any other side in the city or the eight cells round it contest
//   it: it stops while they are as many as the raisers or fewer, and comes
//   down (by kBannerBase + one point each of theirs, eight at most) while they
//   are more;
// - when the raisers leave (or fall), the attempt is abandoned and its
//   progress lost;
// - at the top the city is theirs: the Warlords capture rules (the loser's
//   share of the treasury, half to the victor; the walls lowered by one).
// While an enemy raises its banner in it, a city trains nothing and takes in
// no new armies. Deterministic: integers, cities in index order.
#include <algorithm>

#include "illyria/sim/campaign.h"

namespace illyria::sim {

int32_t Campaign::bannerContest(int32_t city, uint8_t raiser) const {
    const CityState& c = cities_[static_cast<size_t>(city)];
    int32_t n = 0;
    for (const Stack& s : stacks_)
        if (s.owner != raiser && !s.armies.empty() && !s.boat && std::abs(s.cx - c.cx) <= 1 && std::abs(s.cy - c.cy) <= 1)
            n += static_cast<int32_t>(s.armies.size());
    return n;
}

void Campaign::bannerStep() {
    for (size_t i = 0; i < cities_.size(); ++i) {
        CityState& c = cities_[i];
        if (c.banner == kNeutral) continue;
        const std::string& name = continent_.cities()[i].name;
        const int32_t raisers = armiesAt(c.banner, c.cx, c.cy);
        if (raisers == 0 || c.owner == c.banner || c.razed) {
            // They have gone (marched off, or fallen): the attempt is over.
            if (c.owner != c.banner && c.bannerProgress > 0) deed(c.banner, "Our banner over " + name + " was abandoned");
            c.banner = kNeutral;
            c.bannerProgress = 0;
            continue;
        }
        const int32_t foes = bannerContest(static_cast<int32_t>(i), c.banner);
        if (foes == 0) {
            c.bannerProgress += kBannerBase + std::min(raisers, kMaxStack);
        } else if (foes > raisers) {
            c.bannerProgress = std::max(0, c.bannerProgress - (kBannerBase + std::min(foes, kMaxStack)));
        }
        if (c.bannerProgress < kBannerFull) continue;
        transfer(static_cast<int32_t>(i), c.banner);
    }
}

void Campaign::transfer(int32_t city, uint8_t side) {
    CityState& c = cities_[static_cast<size_t>(city)];
    const uint8_t loser = c.owner;
    const std::string& name = continent_.cities()[static_cast<size_t>(city)].name;
    if (loser != kNeutral) {
        const int32_t owned = std::max(1, citiesOwned(loser));
        const int32_t lost = sides_[loser].gold / owned;
        sides_[loser].gold -= lost;
        sides_[side].gold += lost / 2;
        deed(loser, name + " fell to " + sides_[side].name);
    }
    c.owner = side;
    c.banner = kNeutral;
    c.bannerProgress = 0;
    c.producing = -1;
    c.progress = 0;
    c.vectorTo = -1;
    // Each successful siege lowers the walls by one (Warlords 12).
    c.defense = std::max(1, c.defense - 1);
    for (CityState& o : cities_)
        if (o.vectorTo == city) o.vectorTo = -1;
    deed(side, "Our banner flies over " + name);
    // The town's land is ours with it.
    std::vector<int32_t> ring{hexAt(c.cx, c.cy)};
    hexNeighbours(ring.front(), ring);
    for (int32_t h : ring) {
        Hex& hex = hexes_[static_cast<size_t>(h)];
        if (!hex.land) continue;
        hex.owner = side;
        hex.claim = std::max(hex.claim, kClaimFull / 2);
    }
}

void Campaign::fallBack(Stack& s) {
    // To the nearest open cell around (none of another side's there, room for
    // the stack, ground it can walk), in a fixed order; with none, they yield.
    // A neutral garrison has nowhere to go: it lays down its arms.
    if (s.owner == kNeutral) {
        s.armies.clear();
        return;
    }
    static const int32_t kDirs[8][2] = {{0, 1}, {1, 0}, {0, -1}, {-1, 0}, {1, 1}, {1, -1}, {-1, -1}, {-1, 1}};
    for (const auto& d : kDirs) {
        const int32_t x = s.cx + d[0], y = s.cy + d[1];
        if (x < 0 || y < 0 || x >= nx_ || y >= ny_ || !edgeOpen(s.cx, s.cy, x, y)) continue;
        if (stackCost(s, x, y) >= kProhibited || cityAt(x, y) >= 0) continue;
        bool foe = false;
        for (const Stack& o : stacks_) foe = foe || (o.owner != s.owner && o.cx == x && o.cy == y && !o.armies.empty());
        if (foe || armiesAt(s.owner, x, y) + static_cast<int32_t>(s.armies.size()) > cellCapacity(x, y)) continue;
        s.cx = x;
        s.cy = y;
        s.path.clear();
        s.progress = 0;
        return;
    }
    s.armies.clear();
}

}  // namespace illyria::sim
