// Calls for help, and answers (docs/BALANCE.md, "Land, formations, calls").
// - A squad in trouble calls: outnumbered in its melee, struck in the flank or back, wavering, under missiles it cannot
//   answer, or its lord fallen beside it. No sooner than kCallAgainTicks after its last, and one call a side every
//   kSideCallGap, so that it is not noise.
// - The nearest squad of its side that is free (no order, nobody to fight, not holding) answers, by its kind: foot to the
//   caller's side, horse round to the flank of the foe pressing him, bowmen to a bowshot of that foe. The answer is a
//   march of the squad's own (Unit::helping), not an order: any order ends it. Command::Answer sets the same march by hand
//   (the computer's squads, which always have an order, answer so).
// - With no call at all a man with no order still helps (World::supportFoe): here are the parts of that which are not
//   a melee: pikes that stand to receive horse, healers who go to the wounded.
// All of it from the men's own state, Fixed and integers only, the same for every side and for Skirmish and Conquest.
#include <algorithm>
#include <map>
#include <tuple>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

constexpr uint32_t kCallLook = kTicksPerSecond;          // squads are looked over once a second
constexpr uint32_t kSideCallGap = 4 * kTicksPerSecond;   // a side's calls are this far apart at least
constexpr uint32_t kRecent = 3 * kTicksPerSecond;        // "lately" struck in the flank, or shot
// How far a free squad comes from: foot, bowmen, horse (and anything as quick).
// (70, 90 and 160 at first, with horse riding for the flank of themselves: an army nobody commanded then fought as well
// as one that was commanded. Of itself a squad reacts to what is near; the reach and the flank are the commander's.)
constexpr Fixed kFootAnswer = Fixed::fromInt(50), kBowAnswer = Fixed::fromInt(60), kHorseAnswer = Fixed::fromInt(100);
constexpr int64_t kWordFarthest = 200;                   // ... and at most this much farther as the fight goes on
constexpr Fixed kPressReach = Fixed::fromInt(10);        // friends and foes within this of a man fighting are his melee
constexpr Fixed kFoeNear = Fixed::fromInt(30);           // the foe pressing a caller is looked for this far from him
// Horse answering ride for a place this far out to the side of the foe and this far behind him (then in: World::support).
constexpr Fixed kWingOut = Fixed::fromInt(24), kWingBehind = Fixed::fromInt(10);
constexpr uint32_t kWingReplan = 2 * kTicksPerSecond;
constexpr uint32_t kAnswerLong = 60 * kTicksPerSecond;   // an answer not there by then is given up
// Pikes stand to receive horse coming faster than this.
constexpr Fixed kGallop = Fixed::fromRatio(5, 2);
constexpr Fixed kWoundedReach = Fixed::fromInt(40);

bool isHero(const Unit& u) { return u.stats.type == static_cast<uint8_t>(UnitType::Hero); }
bool shooter(const Unit& u) { return u.stats.ranged.muzzleSpeed > Fixed::zero(); }
bool quick(const Unit& u) { return u.stats.mounted || u.stats.flyAltitude > Fixed::zero(); }
bool recent(uint32_t when, uint32_t now) { return when != 0 && now + 1 - when <= kRecent; }

struct SquadSum {
    int32_t alive = 0, engaged = 0, pressed = 0, shaken = 0, routed = 0, shot = 0, flanked = 0, free = 0, loosing = 0;
    Vec3 sum;
    uint32_t called = 0;
    EntityId man = kNoEntity, fighter = kNoEntity, freeMan = kNoEntity;
    bool hero = false, shooters = false, quick = false;
    int32_t hunted = 0;  // dwarves and wizards of it with the foe's horse coming, or under arrows (World::hunted)
};

}  // namespace

bool World::freeToAnswer(const Unit& u) const {
    if (!u.alive() || u.routed || u.hold || hasOrder(u) || !u.queue.empty() || u.engaged || u.foe != kNoEntity || u.helping != kNoEntity || u.stationed ||
        u.occupying || u.posted || u.guard != kNoEntity || u.inAir())
        return false;
    // (Nor a dwarf or a wizard: never into the melee of his own accord, World::shyAway.)
    if (isHero(u) || precious(u.stats) || u.stats.gait == Gait::Wheeled || u.stats.scavenge.sight > Fixed::zero() || u.stats.healing.amount > 0) return false;
    return u.stats.melee.damage > 0 || shooter(u);
}

void World::callForHelp() {
    if (tick_ % kCallLook != 0 && heroDeaths_.empty()) return;
    // The squads, in the order of their side and number (a map by key: the same on every machine).
    std::map<std::pair<PlayerId, uint32_t>, SquadSum> squads;
    std::map<PlayerId, uint32_t> sideCalled;
    for (const Unit& u : units_) {
        if (!u.alive() || u.squad == 0) continue;
        SquadSum& s = squads[{u.owner, u.squad}];
        ++s.alive;
        s.sum += u.pos;
        s.called = std::max(s.called, u.called);
        uint32_t& side = sideCalled[u.owner];
        side = std::max(side, u.called);
        if (s.man == kNoEntity) s.man = u.id;
        s.hero = s.hero || isHero(u);
        s.shooters = s.shooters || shooter(u);
        s.quick = s.quick || quick(u);
        s.routed += u.routed;
        s.shaken += u.routed || u.wavering();
        s.shot += recent(u.shotTick, tick_);
        s.flanked += recent(u.flankTick, tick_);
        s.loosing += u.reloadRemaining > 0;
        s.hunted += hunted(u);
        if (freeToAnswer(u) && !gateKeeper(u)) {
            ++s.free;
            if (s.freeMan == kNoEntity) s.freeMan = u.id;
        }
        if (u.engaged || u.foe != kNoEntity) {
            ++s.engaged;
            if (s.fighter == kNoEntity) s.fighter = u.id;
            int32_t friends = 1, foes = 0;
            nearby(u.pos.x, u.pos.y, kPressReach, [&](uint32_t i) {
                const Unit& o = units_[i];
                if (!o.alive() || o.id == u.id || o.routed || o.inAir() || (o.pos - u.pos).flat().lengthSq() >= kPressReach * kPressReach) return;
                (o.owner == u.owner ? friends : foes) += 1;
            });
            s.pressed += foes > friends;
        }
    }
    for (auto& [key, s] : squads) {
        const PlayerId owner = key.first;
        if (s.alive == 0 || s.routed == s.alive || sideBroken(owner)) continue;
        const Vec3 centre = s.sum / Fixed::fromInt(s.alive);
        // Why it calls, the worst first.
        bool trouble = false;
        HelpCall::Why why = HelpCall::Why::Outnumbered;
        for (const HeroDeath& d : heroDeaths_)
            if (d.owner == owner && (d.pos - centre).flat().lengthSq() < kFoeNear * kFoeNear && (s.engaged > 0 || s.shot > 0)) trouble = true, why = HelpCall::Why::HeroDown;
        if (!trouble && tick_ % kCallLook != 0) continue;
        if (!trouble && s.hunted > 0) trouble = true, why = HelpCall::Why::Hunted;
        if (!trouble && s.flanked > 0) trouble = true, why = HelpCall::Why::Flanked;
        if (!trouble && s.engaged > 0 && s.pressed * 2 >= s.engaged) trouble = true, why = HelpCall::Why::Outnumbered;
        if (!trouble && s.shaken * 3 >= s.alive && (s.engaged > 0 || s.shot > 0)) trouble = true, why = HelpCall::Why::Wavering;
        if (!trouble && s.shot >= 2 && s.engaged == 0 && s.loosing == 0) trouble = true, why = HelpCall::Why::Shot;
        if (!trouble) continue;
        // Not noise: the squad has not called lately, nor has its side just now.
        if (s.called != 0 && tick_ + 1 - s.called < kCallAgainTicks) continue;
        if (const uint32_t side = sideCalled[owner]; side != 0 && tick_ + 1 - side < kSideCallGap) continue;
        sideCalled[owner] = tick_ + 1;
        // The longer the side has been fighting, the farther the word has gone: a metre a second on every reach, so that
        // the far wing comes late (an army nobody commands is slow, and arrives piecemeal) but nobody sits a battle out.
        if (owner < firstCall_.size() && firstCall_[owner] == 0) firstCall_[owner] = tick_ + 1;
        const Fixed word = owner < firstCall_.size() ? Fixed::fromInt(std::min<int64_t>((tick_ + 1 - firstCall_[owner]) / kTicksPerSecond, kWordFarthest)) : Fixed::zero();
        for (Unit& u : units_)
            if (u.alive() && u.owner == owner && u.squad == key.second) u.called = tick_ + 1;
        HelpCall call{s.fighter != kNoEntity ? s.fighter : s.man, owner, key.second, centre, why, 0, kNoEntity, {}};
        // Who answers: the nearest free squad of the side within its kind's reach (horse count nearer: they are there
        // first). Against missiles, horse or bowmen only; to a melee (outnumbered, flanked, wavering, the lord down), foot
        // or horse, and bowmen only when none is free (they go to a bowshot of the foe, never into the press).
        const auto pick = [&](bool bowsToo) {
            const SquadSum* best = nullptr;
            uint32_t bestSquad = 0;
            Fixed bestScore;
            for (const auto& [k2, t] : squads) {
                if (k2.first != owner || k2.second == key.second || t.hero || t.free == 0 || t.free * 2 < t.alive) continue;
                if (why == HelpCall::Why::Shot && !t.quick && !t.shooters) continue;
                if (why != HelpCall::Why::Shot && t.shooters && !bowsToo) continue;
                const Fixed dist = (t.sum / Fixed::fromInt(t.alive) - centre).flat().length();
                if (dist > (t.quick ? kHorseAnswer : t.shooters ? kBowAnswer : kFootAnswer) + word) continue;
                const Fixed score = t.quick ? dist / 2 : dist;
                if (best == nullptr || score < bestScore) best = &t, bestScore = score, bestSquad = k2.second;
            }
            return std::pair{best, bestSquad};
        };
        auto [best, bestSquad] = pick(false);
        if (best == nullptr && why != HelpCall::Why::Shot) std::tie(best, bestSquad) = pick(true);
        if (best != nullptr) {
            call.answer = bestSquad;
            call.answerer = best->freeMan;
            call.answerPos = best->sum / Fixed::fromInt(best->alive);
            const Unit* caller = findUnit(call.caller);
            for (Unit& u : units_)
                if (u.owner == owner && u.squad == bestSquad && freeToAnswer(u) && !gateKeeper(u)) answerCall(u, *caller);
        }
        helpCalls_.push_back(call);
    }
}

bool World::returnToPost(Unit& u) {
    if (!u.posted || u.hold || (u.postAt - u.pos).flat().lengthSq() < Fixed::fromInt(4)) return false;
    walk(u, u.postAt);
    return true;
}

void World::answerCall(Unit& u, const Unit& caller, bool flank) {
    if (!u.alive() || !caller.alive() || caller.owner != u.owner || caller.id == u.id) return;
    // The foe pressing the caller: the man he fights, the man who last struck him, or the nearest to him.
    const Unit* foe = caller.foe != kNoEntity ? findUnit(caller.foe) : nullptr;
    if (foe == nullptr || !foe->alive()) foe = caller.lastAttacker != kNoEntity ? findUnit(caller.lastAttacker) : nullptr;
    if (foe == nullptr || !foe->alive() || foe->owner == u.owner) foe = nearestEnemy(caller, kFoeNear);
    Vec3 spot = caller.pos;
    const auto dry = [&](const Vec3& p) { return standable(p) && !isWater(p.x, p.y); };
    if (foe != nullptr) {
        const Vec3 along = (foe->pos - caller.pos).flat();
        const Fixed len = along.length();
        const Vec3 toMe = (u.pos - foe->pos).flat();
        const Fixed far = toMe.length();
        if (shooter(u)) {
            // A good bowshot from the foe, on his own side of him.
            const Fixed keep = autoReach(u) * 4 / 5;
            spot = far > keep && far > Fixed::zero() ? foe->pos + toMe / far * keep : u.pos;
        } else if (flank && u.stats.mounted && len > Fixed::zero()) {
            // Round to the wing: out to the side of the foe (his own side of it), a little behind him.
            const Vec3 axis = along / len;
            const Vec3 side{-axis.y, axis.x, Fixed::zero()};
            const Fixed sign = toMe.dot(side) >= Fixed::zero() ? Fixed::one() : -Fixed::one();
            const Vec3 wing = foe->pos + side * (kWingOut * sign) + axis * kWingBehind;
            const Vec3 other = foe->pos - side * (kWingOut * sign) + axis * kWingBehind;
            spot = dry(wing) ? wing : dry(other) ? other : caller.pos;
            // Already out there (or behind him): in at once (World::support takes him).
            if ((toMe.dot(axis) > Fixed::zero() && far < kFoeNear) || (wing - u.pos).flat().lengthSq() < Fixed::fromInt(36)) spot = u.pos;
        }
    }
    u.helping = caller.id;
    u.helpFlank = flank;
    if (u.helpSince == 0) u.helpSince = tick_ + 1;
    if ((spot - u.pos).flat().lengthSq() < Fixed::fromInt(9)) {
        // There already: nothing to march (support and his bow do the rest).
        u.hasMoveTarget = false;
        u.path.clear();
        u.pathIndex = 0;
        u.helping = kNoEntity;
        u.helpSince = 0;
        return;
    }
    u.forceMarch = false;
    u.engageFoe = kNoEntity;
    u.routeFrom = u.pos;
    beginMove(u, spot, Fixed::zero(), {});
    planPath(u, spot);
}

bool World::answering(Unit& u) {
    const auto over = [&] {
        u.helping = kNoEntity;
        u.helpSince = 0;
        u.hasMoveTarget = false;
        u.path.clear();
        u.pathIndex = 0;
        return false;
    };
    const Unit* caller = findUnit(u.helping);
    if (caller == nullptr || tick_ + 1 - u.helpSince > kAnswerLong) return over();
    if (!caller->alive()) {
        // He has fallen: to the nearest of his squad still standing where he stood, if any.
        const Unit* next = nullptr;
        Fixed best;
        nearby(caller->pos.x, caller->pos.y, kFoeNear, [&](uint32_t i) {
            const Unit& o = units_[i];
            if (!o.alive() || o.owner != u.owner || o.squad != caller->squad || o.squad == 0 || o.id == u.id) return;
            const Fixed d = (o.pos - caller->pos).flat().lengthSq();
            if (d < kFoeNear * kFoeNear && (next == nullptr || d < best)) next = &o, best = d;
        });
        if (next == nullptr) {
            // None of them is left: he rides on to where he was bound, as a man marching, and fights what he finds there.
            u.helping = kNoEntity;
            u.helpSince = 0;
            return true;
        }
        answerCall(u, *next, u.helpFlank);
        return u.helping != kNoEntity;
    }
    // Horse ride for where the foe's flank is now, not where it was.
    if (u.helpFlank && u.stats.mounted && (tick_ + u.id) % kWingReplan == 0 && u.engageFoe == kNoEntity) {
        answerCall(u, *caller, u.helpFlank);
        return u.helping != kNoEntity;
    }
    return true;
}

bool World::braceFor(Unit& u, const Unit& foe) {
    // (Pikes, and any foot in closed ranks that hold: World::bracedFoot.)
    const bool closed = formed(u) && formationRule(u.formation).tight;
    if ((u.stats.melee.bonusVsMountedPct <= 0 && !closed) || u.stats.mounted || !foe.stats.mounted || foe.momentum < kGallop || u.disorder > 0 || footing(u) != Footing::Firm) return false;
    const Vec3 to = (foe.pos - u.pos).flat();
    const Fixed dist = to.length();
    if (dist == Fixed::zero() || dist - u.stats.radius - foe.stats.radius <= u.stats.melee.reach) return false;  // (in reach: he strikes)
    turnToward(u, to / dist);
    return true;
}

bool World::seekWounded(Unit& u) {
    const Healing& h = u.stats.healing;
    if (h.amount == 0 || u.healCharges == 0 || u.hold || u.stationed) return false;
    const Unit* best = nullptr;
    Fixed bestD;
    nearby(u.pos.x, u.pos.y, kWoundedReach, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (!o.alive() || o.owner != u.owner || o.id == u.id || o.inAir() || o.routed || o.hp > o.stats.maxHp - h.amount / 2) return;
        const Fixed d = (o.pos - u.pos).flat().lengthSq();
        if (d < kWoundedReach * kWoundedReach && (best == nullptr || d < bestD)) best = &o, bestD = d;
    });
    if (best == nullptr) return false;
    const Vec3 to = (best->pos - u.pos).flat();
    const Fixed dist = to.length();
    if (dist == Fixed::zero()) return false;
    walk(u, u.pos + to / dist * min(dist, u.stats.moveSpeed));
    return true;
}

}  // namespace illyria::sim
