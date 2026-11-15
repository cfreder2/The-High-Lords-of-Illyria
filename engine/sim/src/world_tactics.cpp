// Position, support and the Rally: what makes a plan pay (docs/BALANCE.md, "Tactics").
// - Position: a man wading strikes weaker and is struck harder, by blades and by missiles; the high ground tells in a
//   melee and in a volley; a charge into a flank or a back shakes the man struck and the friends at his shoulder, and
//   into the back of men already fighting it breaks them on the spot; pikes braced to the front stop horse cold.
//   Each is told (TacticEvent) so that the presentation can say so over the squad.
// - Support: a man with no order goes to the help of a friend fighting within kSupportReach (horse round to the
//   flank); a man told to Hold keeps his place.
// - The Rally: a lord's order that brings back every broken man in earshot, on a cooldown.
// All of it from the men's own state, Fixed and integers only, the same for every side and for Skirmish and Conquest.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// Wading (whichever side he is on): his blows are this much weaker; a man on firm ground strikes him this much harder; a
// missile this much; and what he looses from the water himself is this much weaker.
// (35, 35 and 50 were tried first: the held bank then paid no better than before, docs/BALANCE.md.)
constexpr int32_t kWadeBlowPct = 50, kWadeStruckPct = 50, kWadeShotPct = 75, kWadeLoosePct = 40;
// Mired (mud, swamp): the same, less.
constexpr int32_t kMireBlowPct = 25, kMireStruckPct = 25, kMireShotPct = 30;
// The high ground in a melee: for each 0.01 of grade between the two, 3% more striking down (at most kHighMaxPct) and
// 3% less striking up (at most kLowMaxPct). A 1 in 8 slope is the whole of it. (2%, +30 and -20 before.)
constexpr int32_t kHighPctPerGrade = 300, kHighMaxPct = 40, kLowMaxPct = 35;
// A volley from above: 5% more for each metre over the mark beyond the first, at most half again; from below as much
// less, at most kShotLowMaxPct.
constexpr int32_t kShotPctPerMetre = 5, kShotMaxPct = 50, kShotLowMaxPct = 40;
// The men a charge takes in the flank or back are out of order this long (the rider's own: world_combat.cpp).
constexpr int32_t kFlankedDisorder = 4 * kTicksPerSecond;
// A charge's shock in morale points (of 1000, a man breaks under 200): met head on as ever; in the flank; in the back; in
// the back of a man busy with another (broken on the spot unless a hero or veterans steady him). The friends within
// kShoulder of him take kShoulderPct of it.
constexpr int32_t kChargeFront = 150, kChargeFlank = 400, kChargeRear = 600, kChargeRearBusy = 950;
constexpr Fixed kShoulder = Fixed::fromInt(6);
constexpr int32_t kShoulderPct = 50;
// Support: an idle man looks this far for a friend who is fighting, every kSupportLook ticks (each man on his own
// tick), and gives the foe up beyond kSupportLeash.
constexpr Fixed kSupportReach = Fixed::fromInt(40), kSupportLeash = Fixed::fromInt(60);
constexpr int32_t kSupportLook = 10;
// A man holding meets a foe within this of his reach, and no farther.
constexpr Fixed kHoldStride = Fixed::fromInt(3);
// Horse coming to help swing round: to a place this far out to the side of the foe and this far behind him, then in.
constexpr Fixed kFlankOut = Fixed::fromInt(12), kFlankBehind = Fixed::fromInt(5), kFlankArrive = Fixed::fromInt(5);
// A man come to help (or to meet a foe on his own) stops this short of the water's edge.
constexpr Fixed kBankProbe = Fixed::fromRatio(3, 2);
// The Rally: how far the lord's order carries, the morale it leaves a broken man with (well clear of wavering: he comes
// back to a fight, and must not break again at the first blow) and lifts a shaken one by.
constexpr Fixed kRallyEarshot = Fixed::fromInt(40);
constexpr int32_t kRallyLift = 400, kRalliedMorale = 800;

bool isHero(const Unit& u) { return u.stats.type == static_cast<uint8_t>(UnitType::Hero); }

}  // namespace

bool World::wading(const Unit& u) const {
    if (!config_.region || !u.alive() || u.inAir()) return false;
    if (groundAt(u.pos.x, u.pos.y) != Ground::Shallows) return false;
    // (Giants and their like wade as they walk.)
    return !familiarGround(u.stats.gait, Ground::Shallows);
}

int32_t World::positionPct(const Unit& u, const Unit& enemy, Fixed dist, Fixed dz) const {
    int32_t pct = 0;
    const Footing mine = footing(u), his = footing(enemy);
    if (mine == Footing::Ford) pct -= kWadeBlowPct;
    else if (mine == Footing::Mire) pct -= kMireBlowPct;
    else if (his == Footing::Ford) pct += kWadeStruckPct;
    else if (his == Footing::Mire) pct += kMireStruckPct;
    if (dist > Fixed::zero() && !u.inAir() && !enemy.inAir()) {
        const int32_t grade = static_cast<int32_t>((-dz * kHighPctPerGrade / dist).floorToInt());
        pct += std::clamp(grade, -kLowMaxPct, kHighMaxPct);
    }
    return pct;
}

int32_t World::heightShotPct(Fixed above) {
    if (above < Fixed::zero()) return 100 - std::clamp(static_cast<int32_t>(((-above - Fixed::one()) * kShotPctPerMetre).floorToInt()), 0, kShotLowMaxPct);
    return 100 + std::clamp(static_cast<int32_t>(((above - Fixed::one()) * kShotPctPerMetre).floorToInt()), 0, kShotMaxPct);
}

int32_t World::wadeShotPct(const Unit& mark) const {
    const Footing f = footing(mark);
    return f == Footing::Ford ? 100 + kWadeShotPct : f == Footing::Mire ? 100 + kMireShotPct : 100;
}

int32_t World::loosePct(const Unit& shooter) const { return wading(shooter) ? 100 - kWadeLoosePct : 100; }

bool World::standsIdle(const Unit& u) {
    return u.alive() && !u.routed && !u.hold && !hasOrder(u) && u.queue.empty() && !u.engaged && u.foe == kNoEntity && u.support == kNoEntity &&
           u.reloadRemaining == 0 && !u.stationed;
}

void World::chargeShock(Unit& enemy, HitSide side, bool busy) {
    int32_t shock = side == HitSide::Front ? kChargeFront : side == HitSide::Flank ? kChargeFlank : busy ? kChargeRearBusy : kChargeRear;
    const bool stood = !enemy.routed && enemy.morale >= kMoraleBreak;
    if (side != HitSide::Front) shock = shock * (100 + formFlankShockPct(enemy)) / 100;  // (a line and a wedge have weak flanks)
    shakeMorale(enemy, shock);
    if (side == HitSide::Front) return;
    enemy.flankTick = tick_ + 1;
    disorder(enemy, kFlankedDisorder);
    // The friends at his shoulder see it come in behind them.
    nearby(enemy.pos.x, enemy.pos.y, kShoulder, [&](uint32_t i) {
        Unit& o = units_[i];
        if (o.owner != enemy.owner || o.id == enemy.id || !o.alive() || o.inAir()) return;
        if ((o.pos - enemy.pos).flat().lengthSq() >= kShoulder * kShoulder) return;
        shakeMorale(o, shock * kShoulderPct / 100);
        disorder(o, kFlankedDisorder);
    });
    if (!enemy.alive()) return;
    tell(stood && enemy.morale < kMoraleBreak ? TacticEvent::Kind::Broken : side == HitSide::Rear ? TacticEvent::Kind::Rear : TacticEvent::Kind::Flanked, enemy);
}

Unit* World::supportFoe(Unit& u) {
    const auto none = [&]() -> Unit* {
        u.support = kNoEntity;
        return nullptr;
    };
    // Not a man told to hold, a garrison at its station, a lord, a healer (seekWounded), a scavenger or an engine. A bowman
    // goes too: to where he can shoot the foe pressing his friend (support).
    const bool shooter = u.stats.ranged.muzzleSpeed > Fixed::zero();
    if (u.hold || u.stationed || u.guard != kNoEntity || isHero(u) || (u.stats.melee.damage <= 0 && !shooter) || u.stats.gait == Gait::Wheeled || u.stats.healing.amount > 0 ||
        u.stats.scavenge.sight > Fixed::zero() || (shooter && u.stats.ranged.autoRange <= Fixed::zero()))
        return none();
    if ((tick_ + u.id) % kSupportLook == 0) {
        u.support = kNoEntity;
        // Nobody comes down off a wall-walk to it, and a gate's keepers keep their gate.
        if (WalkSpot spot; walkSpot(u.pos, spot)) return nullptr;
        if (gateKeeper(u)) return nullptr;
        Fixed best = kSupportLeash * kSupportLeash;
        nearby(u.pos.x, u.pos.y, kSupportReach, [&](uint32_t i) {
            const Unit& o = units_[i];
            // (A dwarf or a wizard with a foe closing on him is a friend to go to, though he strikes no blow: World::shyAway.)
            const EntityId against = o.foe != kNoEntity ? o.foe : o.menacedBy;
            if (o.owner != u.owner || o.id == u.id || !o.alive() || o.routed || against == kNoEntity) return;
            if ((o.pos - u.pos).flat().lengthSq() > kSupportReach * kSupportReach) return;
            const Unit* f = findUnit(against);
            if (f == nullptr || !f->alive() || f->inAir() || f->owner == u.owner || f->fleeing()) return;
            const Fixed d = (f->pos - u.pos).flat().lengthSq();
            if (d < best || (d == best && f->id < u.support)) {
                best = d;
                u.support = f->id;
            }
        });
    }
    if (u.support == kNoEntity) return nullptr;
    Unit* f = mutableUnit(u.support);
    if (f == nullptr || !f->alive() || f->inAir() || f->owner == u.owner || f->fleeing()) return none();
    if ((f->pos - u.pos).flat().lengthSq() > kSupportLeash * kSupportLeash || shutGateBetween(u, f->pos)) return none();
    // Posted, he keeps his ground: he helps against a foe who is within the leash of it (a bowman shoots from it anyway).
    if (u.posted && !withinPost(u, *f)) return none();
    return f;
}

bool World::withinHold(const Unit& u, const Unit& foe) const {
    const Fixed gap = (foe.pos - u.pos).flat().length() - u.stats.radius - foe.stats.radius;
    return gap <= u.stats.melee.reach + kHoldStride;
}

void World::support(Unit& u, Unit& foe) {
    // A bowman: up to a good bowshot of the foe pressing his friend, never into the water, and no nearer; then he looses
    // on his own (shootNearby).
    if (u.stats.ranged.muzzleSpeed > Fixed::zero()) {
        const Vec3 to = (foe.pos - u.pos).flat();
        const Fixed dist = to.length(), keep = autoReach(u) * 4 / 5;
        if (dist <= keep || dist == Fixed::zero()) return;
        const Vec3 step = u.pos + to / dist * kBankProbe;
        if (isWater(step.x, step.y) && !isWater(u.pos.x, u.pos.y)) return;
        walk(u, u.pos + to / dist * min(dist - keep, u.stats.moveSpeed));
        return;
    }
    if (braceFor(u, foe)) return;
    // Horse does not ride in at the front: round to the side of the man his friend is fighting, then in at his flank.
    const Unit* busyWith = u.stats.mounted && foe.foe != kNoEntity ? findUnit(foe.foe) : nullptr;
    if (busyWith != nullptr && busyWith->alive() && busyWith->owner == u.owner && busyWith->id != u.id) {
        const Vec3 along = (foe.pos - busyWith->pos).flat();  // from the friend to the foe
        const Fixed len = along.length();
        const Vec3 rel = (u.pos - foe.pos).flat();
        // Still on his friend's side of the foe: out to the wing first.
        if (len > Fixed::zero() && rel.dot(along) < Fixed::zero() && rel.lengthSq() > kFlankArrive * kFlankArrive * 4) {
            const Vec3 axis = along / len;
            const Vec3 side{-axis.y, axis.x, Fixed::zero()};
            const Vec3 wing = foe.pos + side * (rel.dot(side) >= Fixed::zero() ? kFlankOut : -kFlankOut) + axis * kFlankBehind;
            if ((wing - u.pos).flat().lengthSq() > kFlankArrive * kFlankArrive && standable(wing)) {
                walk(u, wing);
                return;
            }
        }
    }
    // On his own he does not wade in after a foe: he keeps the dry ground and lets him come (an order takes him across).
    const Vec3 to = (foe.pos - u.pos).flat();
    const Fixed dist = to.length();
    if (dist - u.stats.radius - foe.stats.radius > u.stats.melee.reach && !isWater(u.pos.x, u.pos.y)) {
        const Vec3 step = u.pos + to / dist * kBankProbe;
        if (isWater(step.x, step.y)) {
            turnToward(u, to / dist);
            return;
        }
    }
    fight(u, foe);
}

int32_t World::recoveryGain(const Unit& u, int32_t friends, bool heroLeads, bool lordClose) const {
    // A base, more with a hero leading (within his reach), more for each friend near (up to six), and half his steadiness.
    constexpr int32_t kRecoverBase = 15, kRecoverHero = 15, kRecoverPerFriend = 2, kRecoverFriendsMax = 6, kVetRecoverPct = 25;
    int32_t gain = kRecoverBase + (heroLeads ? kRecoverHero : 0) + std::min(std::max(friends - 1, 0), kRecoverFriendsMax) * kRecoverPerFriend + u.steady / 2;
    // A broken man with his lord close by comes back in half the time.
    if (u.routed && lordClose) gain *= 2;
    return gain * (100 + kVetRecoverPct * veteranSteps(u)) / 100;
}

int32_t World::rallyTicks(const Unit& u) const {
    // (As updateMorale's census reckons it: world_morale.cpp.)
    constexpr int32_t kCensusTicks = 10, kCalmTicks = 2 * kTicksPerSecond;
    constexpr Fixed kLeadReach = Fixed::fromInt(30), kCensusReach = Fixed::fromInt(15);
    if (!u.alive() || !u.routed) return 0;
    if (sideBroken(u.owner)) return -1;
    const Fixed safe = safeReach(u);
    int32_t friends = 1, foes = 0;
    bool lead = false, close = false, pressed = false;
    nearby(u.pos.x, u.pos.y, kLeadReach, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (!o.alive() || o.id == u.id) return;
        const Fixed d2 = (o.pos - u.pos).flat().lengthSq();
        if (o.owner == u.owner && isHero(o)) {
            lead = lead || d2 < kLeadReach * kLeadReach;
            close = close || d2 < kRallyLordReach * kRallyLordReach;
        }
        if (o.owner != u.owner && d2 <= safe * safe) pressed = true;
        if (d2 < kCensusReach * kCensusReach) (o.owner == u.owner ? friends : foes) += 1;
    });
    if (pressed || foes > friends) return -1;
    const int32_t need = std::max(0, rallyMorale(u) - u.morale);
    const int32_t gain = std::max(1, recoveryGain(u, friends, lead, close));
    return std::max(0, kCalmTicks - u.calmTicks) + (need + gain - 1) / gain * kCensusTicks + 1;
}

bool World::canRally(const Unit& lord) const {
    return lord.alive() && isHero(lord) && !lord.routed && !lord.inAir() && lord.rallyCooldown == 0 && !sideBroken(lord.owner);
}

int32_t World::rallyHeard(const Unit& lord) const {
    int32_t heard = 0;
    nearby(lord.pos.x, lord.pos.y, kRallyEarshot, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (o.owner == lord.owner && o.alive() && o.routed && (o.pos - lord.pos).flat().lengthSq() < kRallyEarshot * kRallyEarshot) ++heard;
    });
    return heard;
}

void World::orderRally(Unit& lord) {
    if (!canRally(lord)) return;
    RallyCall call{lord.id, lord.owner, lord.pos, RallyCall::Kind::Order, 0, 0};
    nearby(lord.pos.x, lord.pos.y, kRallyEarshot, [&](uint32_t i) {
        Unit& o = units_[i];
        if (o.owner != lord.owner || o.id == lord.id || !o.alive() || o.stats.moraleShockPct == 0) return;
        if ((o.pos - lord.pos).flat().lengthSq() >= kRallyEarshot * kRallyEarshot) return;
        if (o.routed) {
            o.routed = false;
            o.cornered = false;
            o.morale = std::max(o.morale, kRalliedMorale);
            ++call.rallied;
            ++call.heard;
        } else if (o.morale < kMoraleFull) {
            o.morale = std::min(kMoraleFull, o.morale + kRallyLift);
            ++call.heard;
        }
    });
    lord.rallyCooldown = kRallyCooldownTicks;
    rallyCalls_.push_back(call);
}

}  // namespace illyria::sim
