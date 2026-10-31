// Scavengers, the hyena-folk (docs/GAME_DESIGN.md §3.6). Left to himself a
// scavenger runs his own fight: round the end of the enemy's line toward its
// rear, taking the best thing he sees lying about (a satchel charge before a
// blade, a blade before a head, a head before a trunk or a limb; a better
// prize is worth a longer run), darting in at the softest man in reach
// (archers, healers and wizards first), throwing, falling back, and going for
// the next. He keeps out of reach of armoured foes. With nothing to throw he
// cuts down a soft foe near him with his scimitar. Orders override it all: a
// move is obeyed (he keeps what he holds), and ordered at a man with something
// in hand he throws it at him.
#include <algorithm>
#include <vector>

#include "illyria/sim/ballistics.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }

// He thinks (chooses a prize, a mark, his way round) every this many ticks,
// staggered by his id; between, he keeps to what he chose.
constexpr uint32_t kThinkTicks = 10;
// Prizes, in points: a satchel charge, a blade, a head (it breaks whom it
// strikes), a trunk, a limb, a bone or a gobbet. Each metre of the run costs
// kRunCost, so a satchel is worth a run 27 m longer than a head, a blade 7 m.
constexpr int32_t kSatchelValue = 100, kBladeValue = 60, kHeadValue = 45, kTorsoValue = 25, kLimbValue = 18, kScrapValue = 12;
constexpr Fixed kRunCost = Fixed::fromInt(2);
// A burning satchel is worth the run only with this much fuse left when he
// gets there: enough to fly clear of him (its blast and more, at his
// throw's speed). He snatches it up on the run, and it goes at once.
constexpr int32_t kSatchelSpareTicks = 2 * kTicksPerSecond / 3;
// A satchel lying still enough to snatch up: near the ground, and slow.
constexpr Fixed kSatchelLow = Fixed::fromRatio(1, 2);
constexpr Fixed kSatchelSlow = Fixed::fromInt(3);
// What lies up on a wall-walk (or down below one) is out of his reach.
constexpr Fixed kReachUp = Fixed::fromInt(3);
// He takes a thing within this of his edge, stooping this long for it.
constexpr Fixed kTakeReach = Fixed::fromRatio(3, 5);
constexpr int32_t kTakeTicks = kTicksPerSecond / 4;
// Armoured foes (units.h): he backs off once inside their reach plus
// kDangerMargin, steers round any within kAvoidReach, and leaves a prize, or
// a soft foe, within kGuardReach of one.
constexpr Fixed kDangerMargin = Fixed::fromRatio(5, 2);
constexpr Fixed kAvoidReach = Fixed::fromInt(8);
constexpr Fixed kGuardReach = Fixed::fromInt(5);
// Marks, by kind: archers, healers and wizards; the other missile troops; the
// rest; the armoured. A mark's worth falls with distance: weight x 10 / (d + 10).
constexpr int32_t kSoftWeight = 300, kShooterWeight = 200, kPlainWeight = 100, kArmouredWeight = 50;
// He looks this much beyond his sight for prizes for a mark (men stand out more).
constexpr Fixed kMarkSightExtra = Fixed::fromInt(10);
// He darts in to this share of his throwing range before he throws.
constexpr int32_t kDartPct = 60;
// After a throw he falls back from the foes within kRetreatFrom this long,
// then looks for the next thing; his arm needs kThrowRecoverTicks.
constexpr int32_t kRetreatTicks = 4 * kTicksPerSecond / 3;
constexpr Fixed kRetreatFrom = Fixed::fromInt(15);
constexpr int32_t kThrowRecoverTicks = kTicksPerSecond / 2;
// Empty-handed with nothing to take, he goes at a soft foe within this.
constexpr Fixed kPounceReach = Fixed::fromInt(12);
// Round the enemy: out this far beyond the end of its line, then this far behind its rear.
constexpr Fixed kFlankMargin = Fixed::fromInt(14);
constexpr Fixed kRearMargin = Fixed::fromInt(12);
// A satchel is never thrown to burst nearer him than its blast reaches and this much more.
constexpr Fixed kBlastClear = Fixed::fromRatio(3, 2);
// He leads a moving mark by its pace over the flight (a fifth more for the arc), at most this far.
constexpr Fixed kMaxLead = Fixed::fromInt(6);
// Throws scatter as bows do (world.cpp): tighter by 15% a rank, to 40%.
constexpr Fixed kDispersionPerRank = Fixed::fromRatio(3, 20);
constexpr Fixed kMinDispersionFactor = Fixed::fromRatio(2, 5);

int32_t valueOf(RemainKind k) {
    switch (k) {
        case RemainKind::Blade: return kBladeValue;
        case RemainKind::Head: return kHeadValue;
        case RemainKind::Torso: return kTorsoValue;
        case RemainKind::Arm:
        case RemainKind::Leg: return kLimbValue;
        case RemainKind::Bone:
        case RemainKind::Chunk: return kScrapValue;
        case RemainKind::None:
        case RemainKind::Arrow: break;  // an arrow is no weight in the hand
    }
    return 0;
}

bool soft(const Unit& u) {
    const auto t = static_cast<UnitType>(u.stats.type);
    return t == UnitType::Archers || t == UnitType::Journeyman || t == UnitType::Wizards;
}

bool shooter(const Unit& u) { return u.stats.ranged.muzzleSpeed > Fixed::zero() || u.stats.healing.amount > 0; }

Vec3 flatDir(const Vec3& from, const Vec3& to, const Vec3& fallback) {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    return len == Fixed::zero() ? fallback : d / len;
}

Vec3 unitOr(const Vec3& v, const Vec3& fallback) {
    const Fixed len = v.length();
    return len == Fixed::zero() ? fallback : v / len;
}

bool fleshy(RemainKind k) { return k != RemainKind::Bone && k != RemainKind::Blade && k != RemainKind::Arrow && k != RemainKind::None; }

void emptyHands(Scavenger& s) {
    s.held = RemainKind::None;
    s.heldSource = kNoSource;
    s.heldOwner = 0;
    s.satchel = false;
    s.fuse = 0;
    s.blast = {};
}

}  // namespace

Fixed World::scavengeDanger(const Unit& u, Fixed reach, Vec3* away) const {
    Fixed worst = -Fixed::fromInt(1000);
    Vec3 push;
    for (const Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner || o.routed || o.inAir() || !armoured(o.stats)) continue;
        const Vec3 d = (u.pos - o.pos).flat();
        const Fixed dist = d.length();
        const Fixed gap = dist - u.stats.radius - o.stats.radius;
        if (gap >= reach) continue;
        worst = max(worst, o.stats.melee.reach + kDangerMargin - gap);
        if (dist > Fixed::zero()) push += d / dist * ((reach - max(gap, Fixed::zero())) / reach);
    }
    if (away) *away = push;
    return worst;
}

void World::scavengeWalk(Unit& u, const Vec3& goal) {
    const Vec3 to = (goal - u.pos).flat();
    const Fixed dist = to.length();
    if (dist == Fixed::zero()) return;
    if (nav_ && !nav_->clearLine(u.pos, goal)) {
        // Round walls, water and houses by a route, planned again as the goal moves on.
        if (u.path.empty() || (u.path.back() - goal).flat().lengthSq() > Fixed::fromInt(9) || (tick_ + u.id) % kTicksPerSecond == 0) {
            u.path.clear();
            u.pathIndex = 0;
            groundPath(u.pos, goal, u.path, u.stats.gait);
        }
        followPath(u, Fixed::one());
        return;
    }
    if (!u.path.empty()) {
        u.path.clear();
        u.pathIndex = 0;
    }
    // Straight there, bent away from the armoured foes about.
    Vec3 away;
    scavengeDanger(u, kAvoidReach, &away);
    if (away == Vec3{}) {
        walk(u, goal);
        return;
    }
    const Vec3 dir = unitOr(to / dist + away * Fixed::fromInt(2), to / dist);
    walk(u, u.pos + dir * min(dist, Fixed::fromInt(2)));
}

bool World::prizeAt(const Unit& u, Vec3& at) const {
    const Scavenger& s = u.scav;
    switch (s.prize) {
        case Scavenger::Prize::Remain: {
            if (s.prizeRef >= remains_.size()) return false;
            const Remain& r = remains_[s.prizeRef];
            if (valueOf(r.kind) == 0 || !(r.pos == s.goal)) return false;  // taken, kicked or blown away
            at = r.pos;
            return true;
        }
        case Scavenger::Prize::Corpse: {
            const Unit* o = findUnit(s.prizeRef);
            if (o == nullptr || o->alive() || o->gibbed || o->looted || o->inAir()) return false;
            at = o->pos;
            return true;
        }
        case Scavenger::Prize::Satchel:
            for (const Projectile& p : projectiles_) {
                if (p.id != s.prizeRef) continue;
                if (p.kind != ProjectileKind::Bomb || p.thrown || p.pos.z - surfaceBelow(p.pos.x, p.pos.y, p.pos.z) > kSatchelLow ||
                    p.vel.lengthSq() > kSatchelSlow * kSatchelSlow)
                    return false;
                at = p.pos;
                return true;
            }
            return false;
        case Scavenger::Prize::None: break;
    }
    return false;
}

bool World::choosePrize(Unit& u) {
    Scavenger& s = u.scav;
    const Fixed sight = u.stats.scavenge.sight;
    // Armoured foes about: what lies near them is left where it lies. And
    // what a friend is already running for is his.
    std::vector<Vec3> guards;
    std::vector<std::pair<Scavenger::Prize, uint32_t>> claimed;
    for (const Unit& o : units_) {
        if (!o.alive()) continue;
        if (o.owner != u.owner && !o.routed && armoured(o.stats) && (o.pos - u.pos).flat().length() < sight + kGuardReach) guards.push_back(o.pos);
        if (o.owner == u.owner && o.id != u.id && o.scav.mind == Scavenger::Mind::Fetch) claimed.emplace_back(o.scav.prize, o.scav.prizeRef);
    }
    bool found = false;
    Fixed best;
    Scavenger::Prize kind = Scavenger::Prize::None;
    uint32_t ref = 0;
    Vec3 at;
    const auto consider = [&](Scavenger::Prize k, uint32_t r, const Vec3& p, int32_t value) {
        const Fixed d = (p - u.pos).flat().length();
        if (d > sight || abs(p.z - u.pos.z) > kReachUp) return;
        const Fixed score = Fixed::fromInt(value) - d * kRunCost;
        if (found && score <= best) return;  // ties keep the first found
        for (const Vec3& g : guards)
            if ((g - p).flat().lengthSq() < kGuardReach * kGuardReach) return;
        if (std::find(claimed.begin(), claimed.end(), std::make_pair(k, r)) != claimed.end()) return;
        found = true;
        best = score;
        kind = k;
        ref = r;
        at = p;
    };
    // Satchels lying still (in the order thrown); a burning one only if it can be reached in time.
    for (const Projectile& p : projectiles_) {
        if (p.kind != ProjectileKind::Bomb || p.thrown || p.pos.z - surfaceBelow(p.pos.x, p.pos.y, p.pos.z) > kSatchelLow ||
            p.vel.lengthSq() > kSatchelSlow * kSatchelSlow)
            continue;
        const Fixed run = (p.pos - u.pos).flat().length() - u.stats.radius - kTakeReach;
        const int32_t ticks = static_cast<int32_t>((max(run, Fixed::zero()) * kTicksPerSecond / u.stats.moveSpeed).floorToInt());
        if (p.fuse > 0 && p.fuse - ticks < kSatchelSpareTicks) continue;
        consider(Scavenger::Prize::Satchel, p.id, p.pos, kSatchelValue);
    }
    // The blades the dead still hold.
    for (const Unit& o : units_) {
        if (o.alive() || o.gibbed || o.looted || o.inAir() || !bearsBlade(static_cast<UnitType>(o.stats.type))) continue;
        consider(Scavenger::Prize::Corpse, o.id, o.pos, kBladeValue);
    }
    // Body parts and thrown blades lying about.
    remainGrid_.query(u.pos.x, u.pos.y, sight, [&](uint32_t slot) {
        const Remain& r = remains_[slot];
        if (const int32_t v = valueOf(r.kind); v > 0) consider(Scavenger::Prize::Remain, slot, r.pos, v);
    });
    if (!found) return false;
    s.mind = Scavenger::Mind::Fetch;
    s.prize = kind;
    s.prizeRef = ref;
    s.goal = at;
    return true;
}

void World::takePrize(Unit& u) {
    Scavenger& s = u.scav;
    switch (s.prize) {
        case Scavenger::Prize::Remain: {
            const Remain r = remains_[s.prizeRef];
            s.held = r.kind;
            s.heldSource = r.source;
            s.heldOwner = r.owner;
            clearRemain(s.prizeRef);
            break;
        }
        case Scavenger::Prize::Corpse: {
            Unit* o = mutableUnit(s.prizeRef);
            o->looted = true;
            s.held = RemainKind::Blade;
            s.heldSource = o->stats.type;
            s.heldOwner = o->owner;
            break;
        }
        case Scavenger::Prize::Satchel:
            for (size_t i = 0; i < projectiles_.size(); ++i) {
                if (projectiles_[i].id != s.prizeRef) continue;
                s.satchel = true;
                s.fuse = projectiles_[i].fuse;
                s.blast = projectiles_[i].explosive;
                s.heldOwner = projectiles_[i].owner;
                projectiles_.erase(projectiles_.begin() + static_cast<long>(i));
                break;
            }
            break;
        case Scavenger::Prize::None: return;
    }
    s.prize = Scavenger::Prize::None;
    s.prizeRef = 0;
    s.mind = Scavenger::Mind::Strike;
    s.mark = kNoEntity;
    s.timer = 0;
    // A stoop (the game plays his stroke); a burning satchel is snatched up on the run.
    if (!(s.satchel && s.fuse > 0)) u.reloadRemaining = std::max(u.reloadRemaining, kTakeTicks);
}

Unit* World::chooseMark(Unit& u) {
    const Scavenger& s = u.scav;
    const Fixed sight = u.stats.scavenge.sight + kMarkSightExtra;
    Unit* best = nullptr;
    Fixed bestScore;
    for (Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner || o.inAir()) continue;
        const Fixed d = (o.pos - u.pos).flat().length();
        if (d > sight) continue;
        int32_t w = soft(o) ? kSoftWeight : shooter(o) ? kShooterWeight : armoured(o.stats) ? kArmouredWeight : kPlainWeight;
        // A head is wasted on those who never break, or have already run.
        if (s.held == RemainKind::Head && (o.stats.moraleShockPct == 0 || o.routed)) w /= 4;
        else if (o.routed) w /= 2;
        // A satchel is worth more among more of them.
        if (s.satchel)
            for (const Unit& q : units_)
                if (q.alive() && q.owner == o.owner && q.id != o.id && (q.pos - o.pos).flat().lengthSq() < s.blast.radius * s.blast.radius)
                    w += kPlainWeight / 2;
        const Fixed score = Fixed::fromInt(w) * 10 / (d + Fixed::fromInt(10));
        if (best == nullptr || score > bestScore) {
            best = &o;
            bestScore = score;
        }
    }
    return best;
}

Unit* World::pounceTarget(Unit& u) {
    Unit* best = nullptr;
    Fixed bestScore;
    for (Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner || o.inAir() || armoured(o.stats)) continue;
        const Fixed d = (o.pos - u.pos).flat().length();
        if (d > kPounceReach) continue;
        bool guarded = false;
        for (const Unit& g : units_)
            if (g.alive() && g.owner == o.owner && !g.routed && armoured(g.stats) && (g.pos - o.pos).flat().lengthSq() < kGuardReach * kGuardReach)
                guarded = true;
        if (guarded) continue;
        const Fixed score = Fixed::fromInt(soft(o) ? kSoftWeight : kPlainWeight) * 10 / (d + Fixed::fromInt(10));
        if (best == nullptr || score > bestScore) {
            best = &o;
            bestScore = score;
        }
    }
    return best;
}

Vec3 World::flankGoal(const Unit& u) const {
    // The enemy's middle, and his own side's (else his own place): the way
    // from the one to the other is the front.
    Vec3 foes, friends;
    int64_t nf = 0, nm = 0;
    for (const Unit& o : units_) {
        if (!o.alive() || o.inAir()) continue;
        if (o.owner != u.owner) {
            if (o.routed) continue;
            foes += o.pos.flat();
            ++nf;
        } else if (o.stats.scavenge.sight == Fixed::zero()) {
            friends += o.pos.flat();
            ++nm;
        }
    }
    if (nf == 0) return u.pos;
    const Vec3 e = foes / Fixed::fromInt(nf);
    const Vec3 f = nm > 0 ? friends / Fixed::fromInt(nm) : u.pos.flat();
    const Vec3 ahead = flatDir(f, e, u.facing);
    const Vec3 side{-ahead.y, ahead.x, Fixed::zero()};
    // How wide the enemy stands either side of its middle, how far forward and back.
    Fixed wide, front, deep;
    for (const Unit& o : units_) {
        if (!o.alive() || o.inAir() || o.owner == u.owner || o.routed) continue;
        const Vec3 rel = (o.pos - e).flat();
        wide = max(wide, abs(rel.dot(side)));
        front = max(front, -rel.dot(ahead));
        deep = max(deep, rel.dot(ahead));
    }
    const Vec3 rel = (u.pos - e).flat();
    const Fixed lateral = rel.dot(side), along = rel.dot(ahead);
    // Round the end of the line on his own side of it.
    const Fixed sign = lateral < Fixed::zero() ? -Fixed::one() : lateral > Fixed::zero() ? Fixed::one() : (u.id & 1 ? Fixed::one() : -Fixed::one());
    const Fixed out = wide + kFlankMargin;
    const Fixed inBand = abs(lateral) - (out - Fixed::fromInt(4));
    const Fixed z = u.pos.z;
    const auto ground = [&](Vec3 p) {
        p.z = z;
        return p;
    };
    // In front of their line: out past its end first; then round behind; then in their rear.
    if (inBand < Fixed::zero() && along < -front) return ground(e + side * (sign * out) - ahead * (front + Fixed::fromInt(6)));
    if (along < deep + kRearMargin - Fixed::fromInt(4)) return ground(e + side * (sign * out) + ahead * (deep + kRearMargin));
    return ground(e + ahead * (deep + kRearMargin) + side * (sign * wide / 2));
}

void World::hurl(Unit& u, const Vec3& at) {
    Scavenger& s = u.scav;
    const Scavenging& sc = u.stats.scavenge;
    const Vec3 face = flatDir(u.pos, at, u.facing);
    u.facing = face;
    const Vec3 origin = u.pos + face * (u.stats.radius + Fixed::fromRatio(1, 5)) + up(u.stats.height * 17 / 20);
    // Scattered like a bowshot, tighter with rank, wider shaken.
    const Fixed range = (at - origin).flat().length();
    Fixed rankFactor = max(Fixed::one() - kDispersionPerRank * u.rank, kMinDispersionFactor);
    if (u.wavering()) rankFactor = rankFactor * 3 / 2;
    const Fixed radius = sc.dispersionAt100m * range / 100 * rankFactor;
    Fixed dx, dy;
    do {
        dx = rng_.nextSigned();
        dy = rng_.nextSigned();
    } while (dx * dx + dy * dy >= Fixed::one());
    const Vec3 aim = at + Vec3{dx * radius, dy * radius, Fixed::zero()};
    Projectile p;
    p.id = nextId_++;
    p.shooter = u.id;
    p.pos = origin;
    // The flat arc; out of reach, as far as his arm sends it.
    p.vel = solveLowArc(origin, aim, sc.throwSpeed, config_.gravity).velocity;
    p.thrown = true;
    if (s.satchel) {
        p.kind = ProjectileKind::Bomb;
        p.owner = u.owner;
        p.explosive = s.blast;
        p.fuse = s.fuse;
        p.cause = DamageType::Explosive;
    } else {
        // Still the soldier's it came from (its look, its side's colours).
        p.kind = ProjectileKind::Debris;
        p.owner = s.heldOwner;
        p.remain = s.held;
        p.source = s.heldSource;
        p.bleeding = fleshy(s.held) && (s.heldSource == kNoSource || unitStats(static_cast<UnitType>(s.heldSource)).bleeds);
    }
    projectiles_.push_back(p);
    emptyHands(s);
}

bool World::strike(Unit& u, Unit& mark) {
    Scavenger& s = u.scav;
    const Scavenging& sc = u.stats.scavenge;
    const Vec3 seen = s.mark == mark.id ? s.markSeen : mark.pos;
    s.mark = mark.id;
    s.markSeen = mark.pos;
    const Fixed dist = (mark.pos - u.pos).flat().length();
    const bool burning = s.satchel && s.fuse > 0;
    if (s.timer > 0) {
        // Drawn back, turning on him: then the throw, leading him by his pace.
        turnToward(u, flatDir(u.pos, mark.pos, u.facing));
        if (--s.timer > 0) return false;
        const Fixed flight = dist / sc.throwSpeed * Fixed::fromRatio(6, 5);
        Vec3 lead = (mark.pos - seen).flat() * Fixed::fromInt(kTicksPerSecond) * flight;
        if (lead.length() > kMaxLead) lead = unitOr(lead, {}) * kMaxLead;
        // A part at his chest (it must strike him to hurt him); a satchel at
        // his feet, or past him when he is too near to be out of the blast.
        Vec3 at = mark.pos + lead + (s.satchel ? Vec3{} : up(mark.stats.height / 2));
        const Fixed safe = s.blast.radius + kBlastClear;
        if (s.satchel && (at - u.pos).flat().length() < safe) at = u.pos + flatDir(u.pos, at, u.facing) * safe;
        hurl(u, at);
        s.mind = Scavenger::Mind::Retreat;
        s.timer = kRetreatTicks;
        return true;
    }
    if (u.reloadRemaining > 0) return false;  // still stooping, or his arm still swinging through
    // Too near for a satchel's blast: back off first, while the fuse allows.
    if (s.satchel && dist < s.blast.radius + kBlastClear && !(burning && s.fuse <= kSatchelSpareTicks)) {
        scavengeWalk(u, u.pos + flatDir(mark.pos, u.pos, -u.facing) * Fixed::fromInt(3));
        return false;
    }
    // Close enough (darting in), or a burning satchel that cannot wait: draw back.
    const Fixed dart = sc.throwRange * kDartPct / 100;
    if (dist <= dart || (burning && (dist <= sc.throwRange || s.fuse <= kSatchelSpareTicks))) {
        s.timer = burning ? 1 : std::max(1, sc.windupTicks);
        u.reloadRemaining = s.timer + kThrowRecoverTicks;  // the game plays his stroke from here
        turnToward(u, flatDir(u.pos, mark.pos, u.facing));
        return false;
    }
    scavengeWalk(u, mark.pos);
    return false;
}

void World::scavenge(Unit& u) {
    Scavenger& s = u.scav;
    const bool think = (tick_ + u.id) % kThinkTicks == 0;
    // Out of an armoured foe's reach before anything else (a throw drawn back is spoiled).
    Vec3 away;
    if (scavengeDanger(u, kAvoidReach, &away) > Fixed::zero() && !(away == Vec3{})) {
        s.timer = s.mind == Scavenger::Mind::Retreat ? s.timer : 0;
        walk(u, u.pos + unitOr(away, -u.facing) * Fixed::fromInt(3));
        return;
    }
    if (s.mind == Scavenger::Mind::Retreat) {
        // Back from the foes near him, for a moment.
        Vec3 from;
        for (const Unit& o : units_) {
            if (!o.alive() || o.owner == u.owner || o.inAir()) continue;
            const Vec3 d = (u.pos - o.pos).flat();
            const Fixed len = d.length();
            if (len >= kRetreatFrom || len == Fixed::zero()) continue;
            from += d / len * ((kRetreatFrom - len) / kRetreatFrom);
        }
        if (s.timer > 0 && !(from == Vec3{})) {
            --s.timer;
            scavengeWalk(u, u.pos + unitOr(from, -u.facing) * Fixed::fromInt(4));
            return;
        }
        s.timer = 0;
        s.mind = Scavenger::Mind::Scout;
    }
    if (s.holding()) {
        s.mind = Scavenger::Mind::Strike;
        Unit* mark = s.mark != kNoEntity ? mutableUnit(s.mark) : nullptr;
        if (mark != nullptr && (!mark->alive() || mark->inAir())) mark = nullptr;
        // A better mark may come in sight, but not while he is drawn back.
        if (mark == nullptr || (think && s.timer == 0)) {
            Unit* m = chooseMark(u);
            if (m != mark) s.timer = 0;
            mark = m;
            s.mark = mark != nullptr ? mark->id : kNoEntity;
            if (mark != nullptr) s.markSeen = mark->pos;
        }
        if (mark != nullptr) {
            strike(u, *mark);
            return;
        }
        // Nobody to hit in sight. A burning satchel cannot wait: away with it.
        if (s.satchel && s.fuse > 0) {
            hurl(u, u.pos + u.facing * u.stats.scavenge.throwRange);
            s.mind = Scavenger::Mind::Retreat;
            s.timer = kRetreatTicks;
            return;
        }
        if (think || s.goal == Vec3{}) s.goal = flankGoal(u);
        scavengeWalk(u, s.goal);
        return;
    }
    // Empty-handed: after the prize he chose, or the best in sight.
    Vec3 at;
    bool have = s.mind == Scavenger::Mind::Fetch && prizeAt(u, at);
    if (!have || think) have = choosePrize(u) && prizeAt(u, at);
    if (have) {
        if (s.prize != Scavenger::Prize::Remain) s.goal = at;
        if ((at - u.pos).flat().length() - u.stats.radius <= kTakeReach) takePrize(u);
        else scavengeWalk(u, at);
        return;
    }
    s.mind = Scavenger::Mind::Scout;
    s.prize = Scavenger::Prize::None;
    s.prizeRef = 0;
    // Nothing to take: a soft foe near him gets the blade.
    if (Unit* t = pounceTarget(u)) {
        fight(u, *t);
        return;
    }
    // Else round the enemy's flank, toward its rear.
    if (think || s.goal == Vec3{}) s.goal = flankGoal(u);
    if ((s.goal - u.pos).flat().lengthSq() > Fixed::one()) scavengeWalk(u, s.goal);
}

bool World::scavengeOrdered(Unit& u) {
    if (u.stats.scavenge.sight == Fixed::zero() || !u.scav.holding()) return false;
    Unit* t = mutableUnit(u.targetUnit);
    if (t == nullptr || !t->alive() || t->inAir()) {
        u.targetUnit = kNoEntity;  // gone: back to his own fight
        return true;
    }
    if (u.scav.mark != t->id) u.scav.timer = 0;
    if (strike(u, *t)) u.targetUnit = kNoEntity;  // thrown: the order is done
    return true;
}

void World::burnHeld(Unit& u) {
    Scavenger& s = u.scav;
    if (s.fuse <= 0 || --s.fuse > 1) return;
    // At the last it slips from his hands, and goes off at his feet.
    Projectile p;
    p.id = nextId_++;
    p.shooter = u.id;
    p.owner = u.owner;
    p.kind = ProjectileKind::Bomb;
    p.pos = u.pos + up(u.stats.height / 2);
    p.explosive = s.blast;
    p.fuse = 1;
    p.cause = DamageType::Explosive;
    spawned_.push_back(p);
    emptyHands(s);
}

void World::dropHeld(Unit& u) {
    Scavenger& s = u.scav;
    const Vec3 hand = u.pos + up(u.stats.height * 3 / 5);
    if (s.satchel) {
        // It falls with him, its fuse still burning.
        Projectile p;
        p.id = nextId_++;
        p.shooter = u.id;
        p.owner = u.owner;
        p.kind = ProjectileKind::Bomb;
        p.pos = hand;
        p.vel = u.vel;
        p.explosive = s.blast;
        p.fuse = s.fuse;
        p.cause = DamageType::Explosive;
        spawned_.push_back(p);
    } else if (s.held != RemainKind::None) {
        spawnDebris(s.held, s.heldOwner, hand, u.vel + up(Fixed::one()), s.heldSource);
    }
    emptyHands(s);
    s.mind = Scavenger::Mind::Scout;
    s.prize = Scavenger::Prize::None;
    s.mark = kNoEntity;
    s.timer = 0;
}

void World::setOffHeld(Unit& u) {
    Scavenger& s = u.scav;
    if (!s.satchel) return;
    pendingExplosions_.push_back({u.pos + up(u.stats.height * 3 / 5), s.blast, u.owner, ProjectileKind::Bomb, DamageType::Explosive});
    emptyHands(s);
    s.timer = 0;
}

}  // namespace illyria::sim
