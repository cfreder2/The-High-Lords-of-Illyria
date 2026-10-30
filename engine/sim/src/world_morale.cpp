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
// Veterans are steadier: each step of veterancy takes 3% off a shock, and
// they recover a quarter faster per step.
constexpr int32_t kVetSteadyPct = 3, kVetRecoverPct = 25;  // (8% before the small edges: a rank is a slight edge, edges.h)
// A broken unit rallies at this morale with no foe within kSafeReachM metres; each
// veterancy step lowers the bar by kRallyPerStep and shortens the reach by
// kSafePerStepM metres, so elites come back to the fight first. It flees from
// foes within kFleeReach.
constexpr int32_t kMoraleRally = 550, kRallyPerStep = 50;
constexpr int32_t kSafeReachM = 25, kSafePerStepM = 3;
constexpr Fixed kFleeReach = Fixed::fromInt(40);
// Veterans steady the lesser men around them: a veteran within kSteadyReach
// takes kSteadyVeteran% off their shocks, an elite kSteadyElite%, a hero
// within his leadership reach kSteadyHero%, all together at most kSteadyMax%;
// and half that again in morale points to each census's recovery.
constexpr Fixed kSteadyReach = Fixed::fromInt(12);
constexpr int32_t kSteadyVeteran = 4, kSteadyElite = 6, kSteadyHero = 15, kSteadyMax = 25;
// "Hold the line!": a veteran, elite or hero (not shaken himself) who sees at
// least kCallShaken friends within kCallReach wavering or broken (a hero: one)
// calls out. Every friend in earshot gains the lift, and a broken one who
// reaches the waver line (or his own rally bar) turns back. Then he waits
// out his cooldown.
constexpr Fixed kCallReach = Fixed::fromInt(20);
constexpr int32_t kCallShaken = 2;
constexpr int32_t kLiftVeteran = 150, kLiftElite = 200, kLiftHero = 300;
constexpr int32_t kCallCooldown = 15 * kTicksPerSecond, kHeroCallCooldown = 12 * kTicksPerSecond;

bool isHero(const Unit& u) { return u.stats.type == static_cast<uint8_t>(UnitType::Hero); }

}  // namespace

void World::shakeMorale(Unit& u, int32_t points) {
    if (!u.alive() || u.stats.moraleShockPct == 0 || points <= 0) return;
    const int32_t shock =
        points * u.stats.moraleShockPct / 100 * (100 - kVetSteadyPct * veteranSteps(u)) / 100 * (100 - u.steady) / 100;
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
            u.steady = 0;  // whatever steadied them, this undoes it (until the next census)
            const Fixed dist = (u.pos - d.pos).flat().length();
            if (dist < kHeroFallReach) shakeMorale(u, kHeroFallNear);
            else if (dist < kHeroFallReach * 2) shakeMorale(u, kHeroFallFar);
        }
    // Friends cut down around them.
    for (const auto& [owner, at] : deaths_)
        for (Unit& u : units_)
            if (u.owner == owner && u.alive() && (u.pos - at).flat().lengthSq() < kLossReach * kLossReach) shakeMorale(u, kFriendFalls);

    const bool census = tick_ % kCensusTicks == 0;
    for (Unit& u : units_)
        if (u.callCooldown > 0) --u.callCooldown;
    for (Unit& u : units_) {
        if (!u.alive() || u.stats.moraleShockPct == 0) continue;
        u.calmTicks = std::min(u.calmTicks + 1, kCalmTicks);
        if (census) {
            int32_t friends = 1, foes = 0;  // itself among its friends
            int32_t steady = 0;
            bool hero = false;
            const int32_t grade = veteranGrade(u);
            for (const Unit& o : units_) {
                if (!o.alive() || o.id == u.id) continue;
                const Fixed d2 = (o.pos - u.pos).flat().lengthSq();
                if (o.owner == u.owner && isHero(o) && d2 < kLeadReach * kLeadReach) {
                    if (!hero) steady += kSteadyHero;
                    hero = true;
                } else if (o.owner == u.owner && d2 < kSteadyReach * kSteadyReach && !o.routed && !o.wavering()) {
                    const int32_t og = veteranGrade(o);
                    if (og >= 2 && og > grade) steady += og >= 3 ? kSteadyElite : kSteadyVeteran;
                }
                if (d2 >= kCensusReach * kCensusReach) continue;
                (o.owner == u.owner ? friends : foes) += 1;
            }
            u.steady = std::min(steady, kSteadyMax);
            if (foes > friends) {
                shakeMorale(u, (foes - friends) * kOutnumberedShock);
            } else if (u.calmTicks >= kCalmTicks) {
                const int32_t gain = kRecoverBase + (hero ? kRecoverHero : 0) +
                                     std::min(friends - 1, kRecoverFriendsMax) * kRecoverPerFriend + u.steady / 2;
                u.morale = std::min(kMoraleFull, u.morale + gain * (100 + kVetRecoverPct * veteranSteps(u)) / 100);
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
        } else if (u.routed && u.morale >= rallyMorale(u) && nearestEnemy(u, safeReach(u)) == nullptr && !sideBroken(u.owner)) {
            u.routed = false;  // rallied: stands where it is, ready for orders
        }
    }
    if (census) callRallies();
}

int32_t World::rallyMorale(const Unit& u) const { return kMoraleRally - kRallyPerStep * veteranSteps(u); }

Fixed World::safeReach(const Unit& u) const { return Fixed::fromInt(kSafeReachM - kSafePerStepM * veteranSteps(u)); }

void World::callRallies() {
    const auto shaken = [](const Unit& o) { return o.alive() && o.stats.moraleShockPct > 0 && (o.routed || o.wavering()); };
    for (Unit& c : units_) {
        if (!c.alive() || c.routed || c.wavering() || c.callCooldown > 0) continue;
        const bool hero = isHero(c);
        const int32_t grade = veteranGrade(c);
        if (!hero && grade < 2) continue;
        int32_t seen = 0;
        for (const Unit& o : units_)
            if (o.owner == c.owner && o.id != c.id && shaken(o) && (o.pos - c.pos).flat().lengthSq() < kCallReach * kCallReach) ++seen;
        if (seen < (hero ? 1 : kCallShaken)) continue;
        const int32_t lift = hero ? kLiftHero : grade >= 3 ? kLiftElite : kLiftVeteran;
        RallyCall call{c.id, c.owner, c.pos, hero ? RallyCall::Kind::Hero : RallyCall::Kind::Veteran, 0, 0};
        for (Unit& o : units_) {
            if (o.owner != c.owner || o.id == c.id || !o.alive() || o.stats.moraleShockPct == 0 || o.morale >= kMoraleFull) continue;
            if ((o.pos - c.pos).flat().lengthSq() >= kCallReach * kCallReach) continue;
            o.morale = std::min(kMoraleFull, o.morale + lift);
            ++call.heard;
            if (o.routed && o.morale >= std::min(kMoraleWaver, rallyMorale(o)) && !sideBroken(o.owner)) {
                o.routed = false;  // turns back to the fight, ready for orders
                ++call.rallied;
            }
        }
        c.callCooldown = hero ? kHeroCallCooldown : kCallCooldown;
        rallyCalls_.push_back(call);
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
