// Guard duty (Command::Guard): a squad told to guard another (the dwarves, the wizard, anyone) stays with it in a ring
// round it, so that it is between it and every threat, strikes what comes to the ring, and goes to nobody else's help (World::supportFoe
// and the calls leave a guard alone). A duty of the man's own, like the support stance: it is what he does with no
// order, and any order ends it. The same for every side, Skirmish and Conquest: the computer's squads are given it by
// the same command.
//
// And the warning: a dwarf or a wizard with the foe's horse coming for him, or under arrows, calls for help through the
// calls of world_help.cpp (HelpCall::Why::Hunted), early enough for a squad to be sent.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// A guard looks for threats to his ward this far from him, and stands in a ring round him: this far out at least, the
// guards this far apart in it.
constexpr Fixed kGuardWatch = Fixed::fromInt(40);
constexpr Fixed kGuardOut = Fixed::fromInt(3);
constexpr Fixed kGuardSlack = Fixed::fromRatio(3, 2);
constexpr Fixed kGuardAbreast = Fixed::fromRatio(7, 5);
constexpr Fixed kGuardFellows = Fixed::fromInt(6);  // the ward's squadmates this near him are inside the ring too
// Hunted: the foe's horse within this of a dwarf or a wizard, coming his way.
constexpr Fixed kHuntSeen = Fixed::fromInt(55);
constexpr uint32_t kShotLately = 3 * kTicksPerSecond;

}  // namespace

const Unit* World::wardOf(const Unit& u) const {
    if (u.guard == kNoEntity) return nullptr;
    const Unit* named = findUnit(u.guard);
    if (named != nullptr && named->alive() && named->owner == u.owner) return named;
    if (u.guardSquad == 0) return nullptr;
    // His fellows are still guarded: the first of them (by place: the same on every machine).
    for (const Unit& o : units_)
        if (o.alive() && o.owner == u.owner && o.squad == u.guardSquad) return &o;
    return nullptr;
}

bool World::guarded(const Unit& u) const {
    for (const Unit& o : units_)
        if (o.alive() && o.owner == u.owner && o.guard != kNoEntity && (o.guard == u.id || (u.squad != 0 && o.guardSquad == u.squad))) return true;
    return false;
}

bool World::guardStep(Unit& u) {
    const Unit* ward = wardOf(u);
    if (ward == nullptr) {
        u.guard = kNoEntity;
        u.guardSquad = 0;
        return false;
    }
    if (u.inAir()) return false;
    const bool shoots = u.stats.ranged.muzzleSpeed > Fixed::zero();
    // His place: one of a ring round the ward and his fellows, the guards shoulder to shoulder (his turn in it by his id
    // among them), so that nothing reaches the ward without passing a man of the guard. Wide enough for their number
    // and for the ward's squad (those of it close by him).
    int32_t n = 0, turn = 0;
    Fixed spread;
    for (const Unit& o : units_) {
        if (!o.alive() || o.owner != u.owner || o.inAir()) continue;
        if (ward->squad != 0 && o.squad == ward->squad && o.id != ward->id) {
            const Fixed d = (o.pos - ward->pos).flat().length();
            if (d <= kGuardFellows) spread = max(spread, d);
        }
        if (o.guard == kNoEntity) continue;
        if (!(o.guard == u.guard || (u.guardSquad != 0 && o.guardSquad == u.guardSquad))) continue;
        ++n;
        turn += o.id < u.id;
    }
    const Fixed ring = max(kGuardOut + spread, kGuardAbreast * n * 1000 / 6283);
    // A foe at the ring or inside it, or within his own reach: he strikes it, the nearest such to himself (the first of
    // equals by id). Pikes take horse on their points as it comes: nobody goes out to it.
    if (u.stats.melee.damage > 0 && !shoots) {
        Unit* threat = nullptr;
        Fixed best;
        const Fixed in = ring + Fixed::fromInt(2);
        nearbyNotOf(u.owner, ward->pos.x, ward->pos.y, kGuardWatch, [&](uint32_t i) {
            Unit& o = units_[i];
            if (!o.alive() || o.owner == u.owner || o.inAir()) return;
            const Fixed mine = (o.pos - u.pos).flat().length();
            const bool atRing = (o.pos - ward->pos).flat().lengthSq() <= in * in;
            const bool inReach = mine - u.stats.radius - o.stats.radius <= u.stats.melee.reach + Fixed::one();
            if (!atRing && !inReach) return;
            if (threat == nullptr || mine < best || (mine == best && o.id < threat->id)) {
                best = mine;
                threat = &o;
            }
        });
        if (threat != nullptr) {
            u.support = kNoEntity;
            support(u, *threat);
            return true;
        }
    }
    // With a threat in sight they close up shoulder to shoulder on its side of the ring (an arc, its middle toward the
    // nearest foe to the ward: the first of equals by id); with none, they stand all round.
    const Unit* near = nullptr;
    Fixed nearest = kGuardWatch * kGuardWatch;
    nearbyNotOf(u.owner, ward->pos.x, ward->pos.y, kGuardWatch, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (!o.alive() || o.owner == u.owner || o.routed || o.inAir()) return;
        const Fixed d = (o.pos - ward->pos).flat().lengthSq();
        if (d < nearest || (d == nearest && near != nullptr && o.id < near->id)) {
            nearest = d;
            near = &o;
        }
    });
    const Fixed round = Fixed::fromRatio(6283, 1000);
    Vec3 toward{Fixed::one(), Fixed::zero(), Fixed::zero()};
    Fixed step = round / std::max(1, n);
    Fixed angle = step * turn;
    if (near != nullptr && nearest > Fixed::zero()) {
        toward = (near->pos - ward->pos).flat() / sqrt(nearest);
        step = min(step, kGuardAbreast / ring);
        angle = step * (2 * turn - (n - 1)) / 2;
    }
    const Vec3 across{-toward.y, toward.x, Fixed::zero()};
    // (A place he cannot stand on, deep water or a wall: nearer the ward on the same line; none: he stays where he is.
    // Never on the ward himself: a guard crowding him shoves him about. A ford's shallows are ground to stand on.)
    const Vec3 dir = toward * cos(angle) + across * sin(angle);
    Vec3 place = ward->pos + dir * ring;
    for (int32_t k = 2; k >= 1 && !standable(place); --k) place = ward->pos + dir * (ring * k / 3);
    if (!standable(place)) place = u.pos;
    if ((place - u.pos).flat().lengthSq() > kGuardSlack * kGuardSlack) {
        walk(u, place);
        return true;
    }
    // In his place: his face outward (to the threat if it is his side of the ring).
    const Vec3 out = (u.pos - ward->pos).flat();
    const Fixed len = out.length();
    if (len > Fixed::zero()) turnToward(u, out / len);
    return !shoots;  // (a bowman on guard shoots what is in his reach: shootNearby)
}

bool World::hunted(const Unit& u) const {
    if (!precious(u.stats) || !u.alive() || u.inAir()) return false;
    if (u.shotTick != 0 && tick_ + 1 - u.shotTick <= kShotLately) return true;
    bool seen = false;
    nearbyNotOf(u.owner, u.pos.x, u.pos.y, kHuntSeen, [&](uint32_t i) {
        const Unit& o = units_[i];
        if (seen || !o.alive() || o.owner == u.owner || o.routed || !o.stats.mounted) return;
        const Vec3 to = (u.pos - o.pos).flat();
        if (to.lengthSq() >= kHuntSeen * kHuntSeen) return;
        // Coming his way: bound for him, or riding toward him.
        const Vec3 going = o.hasMoveTarget ? (o.moveTarget - o.pos).flat() : o.facing;
        seen = o.targetUnit == u.id || going.dot(to) > Fixed::zero();
    });
    return seen;
}

}  // namespace illyria::sim
