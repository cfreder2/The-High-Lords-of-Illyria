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
// (The gain itself: World::recoveryGain, world_tactics.cpp, which the rally countdown shares.)
constexpr Fixed kLeadReach = Fixed::fromInt(30);
// Veterans are steadier: each step of veterancy takes 3% off a shock, and
// they recover a quarter faster per step.
constexpr int32_t kVetSteadyPct = 3;  // (8% before the small edges: a rank is a slight edge, edges.h)
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

// What a man counts for in the census, in tenths: his mass (a man 1, a horse and rider 2.5), at least a man, at most
// kWeightMost: a few knights are not cowed by a crowd of footmen as a few footmen would be.
constexpr int32_t kWeightOfAMan = 10, kWeightMost = 25;
int32_t censusWeight(const Unit& u) {
    return std::clamp(static_cast<int32_t>((u.stats.mass * kWeightOfAMan).floorToInt()), kWeightOfAMan, kWeightMost);
}

}  // namespace

void World::shakeMorale(Unit& u, int32_t points) {
    if (!u.alive() || u.stats.moraleShockPct == 0 || points <= 0) return;
    // (His formation steadies him or does not; out of order he is shaken worse: world_formation.cpp.)
    const int32_t shock =
        points * u.stats.moraleShockPct / 100 * (100 - kVetSteadyPct * veteranSteps(u)) / 100 * (100 - u.steady) / 100 * (100 + formShockPct(u)) / 100;
    u.morale = std::max(0, u.morale - shock);
    u.calmTicks = 0;
}

void World::shakeBySide(Unit& u, HitSide side, bool missile) {
    const int32_t shock = (side == HitSide::Rear ? kRearShock : side == HitSide::Flank ? kFlankShock : 0) * (100 + formFlankShockPct(u)) / 100;
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
    // Position in the census (world_tactics.cpp): to a man on dry ground a foe floundering in the water is half a foe, so
    // a line that holds a bank is not cowed by the numbers wading at it. Who is wading, reckoned once a census.
    if (census && config_.region) {
        wet_.assign(units_.size(), 0);
        // (A bridge is a choke as a ford is: the men filing over it are half their number to the men at its end.)
        for (size_t i = 0; i < units_.size(); ++i)
            wet_[i] = wading(units_[i]) || (units_[i].alive() && !units_[i].inAir() && groundAt(units_[i].pos.x, units_[i].pos.y) == Ground::Bridge &&
                                            config_.region->underWater(units_[i].pos.x, units_[i].pos.y));
    } else if (census) {
        wet_.clear();
    }
    for (Unit& u : units_) {
        if (u.callCooldown > 0) --u.callCooldown;
        if (u.rallyCooldown > 0) --u.rallyCooldown;
    }
    for (Unit& u : units_) {
        if (!u.alive() || u.stats.moraleShockPct == 0) continue;
        u.calmTicks = std::min(u.calmTicks + 1, kCalmTicks);
        if (census) {
            // In tenths of a man, each by his weight (censusWeight): itself among its friends.
            int32_t friends = censusWeight(u), foes = 0;
            int32_t wetFoes = 0;  // of the foes, those wading while he is not
            const bool dry = !wet_.empty() && !wet_[static_cast<size_t>(&u - units_.data())];
            int32_t steady = 0;
            bool hero = false, lordClose = false;
            const int32_t grade = veteranGrade(u);
            nearby(u.pos.x, u.pos.y, kLeadReach, [&](uint32_t i) {
                const Unit& o = units_[i];
                if (!o.alive() || o.id == u.id) return;
                const Fixed d2 = (o.pos - u.pos).flat().lengthSq();
                if (o.owner == u.owner && isHero(o) && d2 < kLeadReach * kLeadReach) {
                    if (!hero) steady += kSteadyHero;
                    hero = true;
                    lordClose = lordClose || d2 < kRallyLordReach * kRallyLordReach;
                } else if (o.owner == u.owner && d2 < kSteadyReach * kSteadyReach && !o.routed && !o.wavering()) {
                    const int32_t og = veteranGrade(o);
                    if (og >= 2 && og > grade) steady += og >= 3 ? kSteadyElite : kSteadyVeteran;
                }
                // (A broken man is no help to his friends and no threat to his foes: he is running.)
                if (d2 >= kCensusReach * kCensusReach || o.routed) return;
                (o.owner == u.owner ? friends : foes) += censusWeight(o);
                if (dry && o.owner != u.owner && wet_[i]) wetFoes += censusWeight(o);
            });
            foes -= wetFoes / 2;
            u.steady = std::min(steady, kSteadyMax);
            if (foes > friends) {
                shakeMorale(u, (foes - friends) * kOutnumberedShock / kWeightOfAMan);
            } else if (u.calmTicks >= kCalmTicks) {
                u.morale = std::min(kMoraleFull, u.morale + recoveryGain(u, friends / kWeightOfAMan, hero, lordClose));
            }
        }
        if (!u.routed && u.morale < kMoraleBreak) {
            // Broken: the order he had is forgotten, the fight given up (a new
            // order is obeyed: World::applyCommand).
            u.routed = true;
            u.cornered = false;
            u.helping = kNoEntity;
            u.helpSince = 0;
            u.wheel = 0;
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
        nearby(c.pos.x, c.pos.y, kCallReach, [&](uint32_t i) {
            const Unit& o = units_[i];
            if (o.owner == c.owner && o.id != c.id && shaken(o) && (o.pos - c.pos).flat().lengthSq() < kCallReach * kCallReach) ++seen;
        });
        if (seen < (hero ? 1 : kCallShaken)) continue;
        const int32_t lift = hero ? kLiftHero : grade >= 3 ? kLiftElite : kLiftVeteran;
        RallyCall call{c.id, c.owner, c.pos, hero ? RallyCall::Kind::Hero : RallyCall::Kind::Veteran, 0, 0};
        nearby(c.pos.x, c.pos.y, kCallReach, [&](uint32_t i) {
            Unit& o = units_[i];
            if (o.owner != c.owner || o.id == c.id || !o.alive() || o.stats.moraleShockPct == 0 || o.morale >= kMoraleFull) return;
            if ((o.pos - c.pos).flat().lengthSq() >= kCallReach * kCallReach) return;
            o.morale = std::min(kMoraleFull, o.morale + lift);
            ++call.heard;
            if (o.routed && o.morale >= std::min(kMoraleWaver, rallyMorale(o)) && !sideBroken(o.owner)) {
                o.routed = false;  // turns back to the fight, ready for orders
                ++call.rallied;
            }
        });
        c.callCooldown = hero ? kHeroCallCooldown : kCallCooldown;
        rallyCalls_.push_back(call);
    }
}

void World::flee(Unit& u) {
    // Away from the foes near it, weighted toward the nearest.
    Vec3 away;
    nearby(u.pos.x, u.pos.y, kFleeReach, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (!o.alive() || o.owner == u.owner) return;
        const Vec3 d = (u.pos - o.pos).flat();
        const Fixed distSq = d.lengthSq();
        if (distSq >= kFleeReach * kFleeReach) return;  // (too far: no root to take)
        const Fixed len = sqrt(distSq);
        if (len == Fixed::zero()) return;
        away += d * ((kFleeReach - len) / (len * kFleeReach));
    });
    const Fixed len = away.length();
    if (len == Fixed::zero()) {  // nobody near: catch its breath
        u.cornered = true;
        return;
    }
    const Vec3 from = u.pos;
    walk(u, u.pos + away / len * Fixed::fromInt(10));
    u.cornered = (u.pos - from).flat().lengthSq() == Fixed::zero();
}

}  // namespace illyria::sim
