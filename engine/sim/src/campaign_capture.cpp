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
// A stack whose middle is this far beyond a town's edge may still have a man inside it.
constexpr int64_t kStragglersMm = 60000;
// A village has no wall to say where it ends: its pad and this much of the ground about it.
constexpr int64_t kVillageEdgeMm = 10000;
int64_t toMmOf(Fixed metres) { return (metres.raw * 1000) >> Fixed::kFracBits; }
}

// How many armies of each side (the last: the neutral) have men in the town.
int32_t Campaign::armiesInTown(int32_t city, int32_t (&in)[9]) const {
    for (int32_t& n : in) n = 0;
    // In the town: inside its outer wall (a walled place: the ring itself, so that nobody at the foot of the wall or
    // before the gate is in it, and a man on the far wall of a capital is), or on a village's pad and the ten metres about it.
    const Continent::City& town = continent_.cities()[static_cast<size_t>(city)];
    const int64_t tx = toMmOf(town.pos.x), ty = toMmOf(town.pos.y);
    const int64_t reach = toMmOf(town.radius) + 30000;
    std::vector<std::pair<int64_t, int64_t>> wall;
    if (town.walled)
        for (const Vec3& q : town.castle.rings.front().corners) wall.push_back({toMmOf(q.x), toMmOf(q.y)});
    const int64_t pad = toMmOf(town.radius) + kVillageEdgeMm;
    const auto inside = [&](int64_t x, int64_t y) {
        if (wall.empty()) return (x - tx) * (x - tx) + (y - ty) * (y - ty) <= pad * pad;
        bool in = false;
        for (size_t i = 0, j = wall.size() - 1; i < wall.size(); j = i++) {
            const auto& [ax, ay] = wall[i];
            const auto& [bx, by] = wall[j];
            // (Crossing test in whole millimetres: which side of the edge the point lies, without a division.)
            if ((ay > y) == (by > y)) continue;
            const int64_t lhs = (x - ax) * (by - ay), rhs = (bx - ax) * (y - ay);
            if (by > ay ? lhs < rhs : lhs > rhs) in = !in;
        }
        return in;
    };
    int32_t total = 0;
    for (const Stack& s : stacks_) {
        if (s.armies.empty() || s.boat) continue;
        int32_t bx, by;
        bodyOf(s, 0, bx, by);
        const int64_t d0 = int64_t{bx} - tx, d1 = int64_t{by} - ty;
        if (d0 * d0 + d1 * d1 > (reach + kStragglersMm) * (reach + kStragglersMm)) continue;
        for (const Army& a : s.armies) {
            if (a.soldiers <= 0) continue;
            int32_t nIn = 0;
            for (const Man& m : a.men) {
                if (!m.alive() || !m.unit) continue;
                nIn = nIn || inside(m.ax, m.ay);
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
            c.bannerProgress += kThinkTicks * (kBannerBase + std::min(raisers, kMaxStack));
        } else if (foes > raisers) {
            c.bannerProgress = std::max(0, c.bannerProgress - kThinkTicks * (kBannerBase + std::min(foes, kMaxStack)));
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
