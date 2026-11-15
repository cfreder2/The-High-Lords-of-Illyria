// Raising a banner (docs/GAME_DESIGN.md §4.2.8): a city changes hands only by
// being held. When the men of another side stand in it with none of the
// town's own or any other side's near, they raise their banner over a few
// hours of conquest time. Who stands in it is who is there: the men of an
// army asleep in its courtyard, or the men of one fighting in it.
// - every half second the banner climbs by (kBannerBase + one point an army
//   in the town, eight at most) times the ticks since: 160 ticks for a lone
//   army, 48 for a full group, of kBannerFull;
// - over a lord's seat it climbs at a third of that pace (kSeatBannerSlow);
// - men of any other side in the town contest it: it stops while their armies
//   are as many as the raisers or fewer, and comes down while they are more;
// - when the raisers leave (or fall), the attempt is abandoned and its
//   progress lost;
// - at the top the city is theirs: the Warlords capture rules (the loser's
//   share of the treasury, half to the victor; the walls lowered by one), and
//   the sack of a lord's town (kSackDays of its income);
// - a lord's seat (the capital he began with) is his realm: when it is taken
//   he is out of the war, and his towns and their garrisons are the victor's
//   (fallOfRealm; a computer lord who is outmatched gives his realm up the
//   same way, at the council of every tenth day: conquest_ai.cpp).
// While an enemy raises its banner in it, a city trains nothing and takes in
// no new armies. Deterministic: integers, cities in index order.
#include <algorithm>

#include "illyria/sim/conquest.h"

namespace illyria::sim {

namespace {
// A stack whose middle is this far beyond a town's edge may still have a man inside it.
constexpr int64_t kStragglersMm = 60000;
// A village has no wall to say where it ends: its pad and this much of the ground about it.
constexpr int64_t kVillageEdgeMm = 10000;
int64_t toMmOf(Fixed metres) { return (metres.raw * 1000) >> Fixed::kFracBits; }
}

// How many armies of each side (the last: the neutral) have men in the town.
int32_t Conquest::armiesInTown(int32_t city, int32_t (&in)[9]) const {
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

int32_t Conquest::bannerContest(int32_t city, uint8_t raiser) const {
    int32_t in[9];
    armiesInTown(city, in);
    int32_t n = 0;
    for (int32_t side = 0; side < 9; ++side) n += side != raiser ? in[side] : 0;
    return n;
}

void Conquest::bannerStep() {
    for (size_t i = 0; i < cities_.size(); ++i) {
        CityState& c = cities_[i];
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
            bannerAlarm(static_cast<int32_t>(i));  // its lord may send help at once (conquest_ai.cpp)
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
            // (Over a lord's seat it climbs at a third of the pace: his realm hangs on it, and he is given the time to come.)
            c.bannerProgress += kThinkTicks * (kBannerBase + std::min(raisers, kMaxStack)) / bannerSlow(static_cast<int32_t>(i));
        } else if (foes > raisers) {
            c.bannerProgress = std::max(0, c.bannerProgress - kThinkTicks * (kBannerBase + std::min(foes, kMaxStack)));
        }
        if (c.bannerProgress < kBannerFull) continue;
        transfer(static_cast<int32_t>(i), c.banner);
    }
}

void Conquest::handOver(int32_t city, uint8_t side) {
    CityState& c = cities_[static_cast<size_t>(city)];
    c.owner = side;
    c.banner = kNeutral;
    c.bannerProgress = 0;
    c.producing = -1;
    c.progress = 0;
    c.vectorTo = -1;
    holdGates(city);  // its gates are the new lord's now: his men do not hack at them
    for (CityState& o : cities_)
        if (o.vectorTo == city) o.vectorTo = -1;
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

void Conquest::transfer(int32_t city, uint8_t side) {
    CityState& c = cities_[static_cast<size_t>(city)];
    const uint8_t loser = c.owner;
    const std::string& name = continent_.cities()[static_cast<size_t>(city)].name;
    const bool seat = loser != kNeutral && seatOf(loser) == city;
    if (loser != kNeutral) {
        const int32_t owned = std::max(1, citiesOwned(loser));
        const int32_t lost = sides_[loser].gold / owned;
        sides_[loser].gold -= lost;
        // The sack: what the town's own coffers held, kSackDays of its income (a lord's town; a neutral one has only its levy).
        sides_[side].gold += lost / 2 + c.income * kSackDays;
        deed(loser, name + " fell to " + sides_[side].name);
    }
    fights_.push_back({side, loser, city, day()});
    handOver(city, side);
    // Each successful siege lowers the walls by one (Warlords 12).
    c.defense = std::max(1, c.defense - 1);
    // The men of his who stand in it take their stations as its garrison (the same place for them as for any garrison).
    for (const Stack& st : stacks_)
        if (st.owner == side && cityAt(st.cx, st.cy) == city) station(st);
    deed(side, "Our banner flies over " + name);
    if (seat) {
        deed(loser, "Our seat is taken: the realm is no more");
        deed(side, "The realm of " + sides_[loser].name + " is ours: its towns swear to us");
        // (To the others it is a town changing hands: news to those who can see the town, as the page tells any such.)
        fallOfRealm(loser, side);
    }
    syncLand();
}

int32_t Conquest::bannerSlow(int32_t city) const {
    const uint8_t lord = cities_[static_cast<size_t>(city)].owner;
    return lord != kNeutral && seatOf(lord) == city ? kSeatBannerSlow : 1;
}

int32_t Conquest::seatOf(uint8_t side) const {
    for (size_t i = 0; i < cities_.size(); ++i) {
        const Continent::City& c = continent_.cities()[i];
        if (c.capital && c.owner != kNeutral && inGame(c.owner) && factionOf(c.owner) == side) return static_cast<int32_t>(i);
    }
    return -1;
}

void Conquest::fallOfRealm(uint8_t loser, uint8_t victor) {
    // His seat is taken, or he has bent the knee (ConquestAi, Conquest::council): the lord is out of the war and his realm
    // is the victor's. His chest; his towns; their garrisons, who open the gates and swear to the victor, man for man where
    // they stand. His armies in the field and at sea go home (they followed a lord, and there is none); he and his heroes
    // are gone, and what they carried lies where they stood.
    sides_[victor].gold += sides_[loser].gold;
    sides_[loser].gold = 0;
    sides_[loser].alive = false;
    offers_[loser].open = false;
    for (Stack& s : stacks_) {
        if (s.owner != loser) continue;
        const int32_t town = cityAt(s.cx, s.cy);
        const bool sworn = town >= 0 && cities_[static_cast<size_t>(town)].owner == loser && s.path.empty() && !s.boat;
        for (Army& a : s.armies) {
            release(a);  // (out of the world as the loser's men; the sworn are put back in it as the victor's, where they stood)
            for (uint8_t k : a.artifacts) loot_.push_back({s.cx, s.cy, k});  // (only heroes carry any)
        }
        if (sworn) std::erase_if(s.armies, [](const Army& a) { return a.hero(); });
        else s.armies.clear();
        s.owner = victor;
        s.commanded = false;
        s.orderCell = -1;
    }
    std::erase_if(stacks_, [](const Stack& s) { return s.armies.empty(); });
    std::erase_if(transits_, [&](const Transit& t) { return t.owner == loser; });
    std::erase_if(boats_, [&](const Boat& b) { return b.owner == loser && !b.stack && b.port < 0; });  // (one in a port goes with the port: syncBoats)
    for (size_t i = 0; i < cities_.size(); ++i)
        if (cities_[i].owner == loser) handOver(static_cast<int32_t>(i), victor);
}

}  // namespace illyria::sim
