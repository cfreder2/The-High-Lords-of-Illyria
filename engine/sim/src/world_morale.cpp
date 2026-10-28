// Morale: soldiers waver and break under shocks and rally when safe
// (docs/GAME_DESIGN.md, battle). Per soldier, as in Myth; friends nearby
// steady each other and a hero steadies all near him, while his fall shakes
// them badly.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// Shocks, in morale points for a man of average resolve (of 1000).
constexpr int32_t kHeroFallNear = 850;   // within kHeroFallReach of a hero who falls
constexpr int32_t kHeroFallFar = 425;    // within twice that
constexpr Fixed kHeroFallReach = Fixed::fromInt(30);
constexpr int32_t kFriendFalls = 70;     // a friend killed within kLossReach
constexpr Fixed kLossReach = Fixed::fromInt(12);
constexpr int32_t kFlankShock = 25, kRearShock = 50;  // a blow there (missiles half)
// The census, every kCensusTicks: friends and foes within kCensusReach. Each
// foe beyond its friends' number (counting itself) takes this much a census.
constexpr int32_t kCensusTicks = 10;
constexpr Fixed kCensusReach = Fixed::fromInt(15);
constexpr int32_t kOutnumberedShock = 10;
// Recovery a census, once calm for kCalmTicks: a base, more near a living
// friendly hero (within his leadership reach), more for each friend near (up to 6).
constexpr int32_t kCalmTicks = 2 * kTicksPerSecond;
constexpr int32_t kRecoverBase = 15, kRecoverHero = 15, kRecoverPerFriend = 2, kRecoverFriendsMax = 6;
constexpr Fixed kLeadReach = Fixed::fromInt(30);
// Veterans are steadier: each step of veterancy takes 8% off a shock.
constexpr int32_t kVetSteadyPct = 8;
// A broken unit rallies at this morale with no foe within kSafeReach; it flees
// from foes within kFleeReach.
constexpr int32_t kMoraleRally = 550;
constexpr Fixed kSafeReach = Fixed::fromInt(25);
constexpr Fixed kFleeReach = Fixed::fromInt(40);

bool isHero(const Unit& u) { return u.stats.type == static_cast<uint8_t>(UnitType::Hero); }

}  // namespace

void World::shakeMorale(Unit& u, int32_t points) {
    if (!u.alive() || u.stats.moraleShockPct == 0 || points <= 0) return;
    const int32_t shock = points * u.stats.moraleShockPct / 100 * (100 - kVetSteadyPct * veteranSteps(u)) / 100;
    u.morale = std::max(0, u.morale - shock);
    u.calmTicks = 0;
}

void World::shakeBySide(Unit& u, HitSide side, bool missile) {
    const int32_t shock = side == HitSide::Rear ? kRearShock : side == HitSide::Flank ? kFlankShock : 0;
    shakeMorale(u, missile ? shock / 2 : shock);
}

void World::updateMorale() {
    // A hero's fall: the severest blow to those who saw it.
    for (const HeroDeath& d : heroDeaths_)
        for (Unit& u : units_) {
            if (u.owner != d.owner || !u.alive()) continue;
            const Fixed dist = (u.pos - d.pos).flat().length();
            if (dist < kHeroFallReach) shakeMorale(u, kHeroFallNear);
            else if (dist < kHeroFallReach * 2) shakeMorale(u, kHeroFallFar);
        }
    // Friends cut down around them.
    for (const auto& [owner, at] : deaths_)
        for (Unit& u : units_)
            if (u.owner == owner && u.alive() && (u.pos - at).flat().lengthSq() < kLossReach * kLossReach) shakeMorale(u, kFriendFalls);

    const bool census = tick_ % kCensusTicks == 0;
    for (Unit& u : units_) {
        if (!u.alive() || u.stats.moraleShockPct == 0) continue;
        u.calmTicks = std::min(u.calmTicks + 1, kCalmTicks);
        if (census) {
            int32_t friends = 1, foes = 0;  // itself among its friends
            bool hero = false;
            for (const Unit& o : units_) {
                if (!o.alive() || o.id == u.id) continue;
                const Fixed d2 = (o.pos - u.pos).flat().lengthSq();
                if (o.owner == u.owner && isHero(o) && d2 < kLeadReach * kLeadReach) hero = true;
                if (d2 >= kCensusReach * kCensusReach) continue;
                (o.owner == u.owner ? friends : foes) += 1;
            }
            if (foes > friends) {
                shakeMorale(u, (foes - friends) * kOutnumberedShock);
            } else if (u.calmTicks >= kCalmTicks) {
                const int32_t gain = kRecoverBase + (hero ? kRecoverHero : 0) + std::min(friends - 1, kRecoverFriendsMax) * kRecoverPerFriend;
                u.morale = std::min(kMoraleFull, u.morale + gain);
            }
        }
        if (!u.routed && u.morale < kMoraleBreak) {
            // Broken: orders are forgotten, the fight given up.
            u.routed = true;
            u.hasMoveTarget = false;
            u.hasAttackTarget = false;
            u.attackGate = -1;
            u.path.clear();
            u.foe = kNoEntity;
        } else if (u.routed && u.morale >= kMoraleRally && nearestEnemy(u, kSafeReach) == nullptr) {
            u.routed = false;  // rallied: stands where it is, ready for orders
        }
    }
}

void World::flee(Unit& u) {
    // Away from the foes near it, weighted toward the nearest.
    Vec3 away;
    for (const Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner) continue;
        const Vec3 d = (u.pos - o.pos).flat();
        const Fixed len = d.length();
        if (len >= kFleeReach || len == Fixed::zero()) continue;
        away += d * ((kFleeReach - len) / (len * kFleeReach));
    }
    const Fixed len = away.length();
    if (len == Fixed::zero()) return;  // nobody near: catch its breath
    walk(u, u.pos + away / len * Fixed::fromInt(10));
}

}  // namespace illyria::sim
