// Raising a banner (docs/GAME_DESIGN.md §4.2.8): a city changes hands only by
// being held. When the men of another side stand in it with none of the
// town's own or any other side's near, they raise their banner over a few
// hours of campaign time. Who stands in it is who is there: the men of an
// army asleep in its courtyard, or the men of one fighting in it.
// - every half second the banner climbs by (kBannerBase + one point an army
//   in the town, eight at most) times the ticks since: 160 ticks for a lone
//   army, 48 for a full group, of kBannerFull;
// - men of any other side in the town contest it: it stops while their armies
//   are as many as the raisers or fewer, and comes down while they are more;
// - when the raisers leave (or fall), the attempt is abandoned and its
//   progress lost;
// - at the top the city is theirs: the Warlords capture rules (the loser's
//   share of the treasury, half to the victor; the walls lowered by one).
// While an enemy raises its banner in it, a city trains nothing and takes in
// no new armies. Deterministic: integers, cities in index order.
#include <algorithm>

#include "illyria/sim/campaign.h"

namespace illyria::sim {

namespace {
constexpr int64_t kInTownMm = 60000;  // inside the walls: within this of the courtyard
}

// How many armies of each side (the last: the neutral) have men in the town.
int32_t Campaign::armiesInTown(int32_t city, int32_t (&in)[9]) const {
    const CityState& c = cities_[static_cast<size_t>(city)];
    for (int32_t& n : in) n = 0;
    int32_t cx, cy;
    anchorOf(c.cx, c.cy, cx, cy);
    int32_t total = 0;
    for (const Stack& s : stacks_) {
        if (s.armies.empty() || s.boat) continue;
        int32_t bx, by;
        bodyOf(s, 0, bx, by);
        const int64_t d0 = int64_t{bx} - cx, d1 = int64_t{by} - cy;
        if (d0 * d0 + d1 * d1 > (kInTownMm + 30000) * (kInTownMm + 30000)) continue;
        for (const Army& a : s.armies) {
            if (a.soldiers <= 0) continue;
            int32_t nIn = 0;
            if (!s.awake) {
                nIn = d0 * d0 + d1 * d1 <= kInTownMm * kInTownMm;
            } else {
                for (const Man& m : a.men) {
                    if (!m.alive() || !m.unit) continue;
                    const int64_t e0 = int64_t{m.ax} - cx, e1 = int64_t{m.ay} - cy;
                    nIn = nIn || e0 * e0 + e1 * e1 <= kInTownMm * kInTownMm;
                }
            }
            in[s.owner < 8 ? s.owner : 8] += nIn;
            total += nIn;
        }
    }
    return total;
}

int32_t Campaign::bannerContest(int32_t city, uint8_t raiser) const {
    int32_t in[9];
    armiesInTown(city, in);
    int32_t n = 0;
    for (int32_t side = 0; side < 9; ++side) n += side != raiser ? in[side] : 0;
    return n;
}

void Campaign::bannerStep() {
    for (size_t i = 0; i < cities_.size(); ++i) {
        CityState& c = cities_[i];
        if (c.razed) continue;
        int32_t in[9];
        const bool any = armiesInTown(static_cast<int32_t>(i), in) > 0;
        const std::string& name = continent_.cities()[i].name;
        if (c.banner == kNeutral) {
            if (!any) continue;
            // The first to stand in it with none of the town's own men: a banner goes up.
            const size_t owner = c.owner < 8 ? c.owner : 8;
            if (in[owner]) continue;
            int32_t raiser = -1;
            for (int32_t side = 0; side < 8; ++side) {
                if (side == c.owner || !in[side]) continue;
                int32_t others = 0;
                for (int32_t o = 0; o < 9; ++o) others += o != side ? in[o] : 0;
                if (others == 0) {
                    raiser = side;
                    break;
                }
            }
            if (raiser < 0) continue;
            c.banner = static_cast<uint8_t>(raiser);
            c.bannerProgress = 0;
            deed(c.banner, "Our banner goes up over " + name);
            if (c.owner != kNeutral) deed(c.owner, sides_[c.banner].name + " raise their banner over " + name + "!");
            bannerAlarm(static_cast<int32_t>(i));  // its lord may send help at once (campaign_ai.cpp)
            continue;
        }
        const size_t raiserSide = c.banner;
        const int32_t raisers = in[raiserSide];
        if (raisers == 0 || c.owner == c.banner) {
            // They have gone (marched off, or fallen): the attempt is over.
            if (c.owner != c.banner && c.bannerProgress > 0) deed(c.banner, "Our banner over " + name + " was abandoned");
            c.banner = kNeutral;
            c.bannerProgress = 0;
            continue;
        }
        int32_t foes = 0;
        for (size_t o = 0; o < 9; ++o)
            if (o != raiserSide) foes += in[o];
        if (foes == 0) {
            c.bannerProgress += kContactTicks * (kBannerBase + std::min(raisers, kMaxStack));
        } else if (foes > raisers) {
            c.bannerProgress = std::max(0, c.bannerProgress - kContactTicks * (kBannerBase + std::min(foes, kMaxStack)));
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
    fights_.push_back({side, loser, city, day()});
    c.owner = side;
    c.banner = kNeutral;
    c.bannerProgress = 0;
    c.producing = -1;
    c.progress = 0;
    c.vectorTo = -1;
    // Each successful siege lowers the walls by one (Warlords 12).
    c.defense = std::max(1, c.defense - 1);
    holdGates(city);  // its gates are the new lord's now: his men do not hack at them
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

}  // namespace illyria::sim
