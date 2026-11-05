// Scavengers, the hyena-folk (docs/GAME_DESIGN.md §3.6). With no order a
// scavenger is a soldier like any other: he goes where he is sent and faces
// as told, holds there, fights what comes at him, and keeps what he holds;
// ordered at a man with something in hand, he throws it at him.
//
// Ordered to raid (Command::Raid), he is let loose from where he stands and
// runs his own fight, knowing only what is in his sight: he takes the best
// thing lying about (a satchel charge before a blade, a blade before a head, a
// head before a trunk or a limb; a better prize is worth a longer run), and
// darts in at a soft man or a straggler (archers, healers, wizards and
// grenadiers, the wounded, the one far from his friends) when his pack about
// him is strong enough for the foes round that man and the missile troops
// covering where he would stand; throws, falls back to one place well clear,
// and goes again. He keeps out of armoured men's reach and, where he can, out
// of the missile troops'. With nothing to throw he cuts down a soft man the
// odds allow. The raid ends when he is given another order, when his pack has
// lost half its men, or when nothing worth taking has been in sight for a
// while; then he holds where he is.
//
// The heavies' answer (World::shieldTarget): a soldier with no order of his
// own struck by a throw from one he cannot reach turns his shield to him and
// holds his place, rather than chasing what he cannot catch; a shield facing
// the throw catches it (World::updateProjectiles).
#include <algorithm>
#include <vector>

#include "illyria/sim/ballistics.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }

// He thinks (chooses a prize, a mark, a way round) every this many ticks,
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
// Armoured foes (units.h): he falls back once inside their reach plus
// kDangerMargin, steers round any within kAvoidReach, and leaves a prize
// within kGuardReach of one where it lies.
constexpr Fixed kDangerMargin = Fixed::fromRatio(5, 2);
constexpr Fixed kAvoidReach = Fixed::fromInt(8);
constexpr Fixed kGuardReach = Fixed::fromInt(5);
// Prey, by kind: archers, healers, wizards and grenadiers; the other missile
// troops; the rest; the armoured. Up to twice as much wounded; by how far he
// stands from his nearest friend, from half at his side to two and a half
// times kStragglerReach off or more; a shield facing the thrower takes off
// its chance of catching the throw. Worth falls with distance: x 10 / (d + 10).
constexpr int32_t kSoftWeight = 300, kShooterWeight = 200, kPlainWeight = 100, kArmouredWeight = 30;
constexpr Fixed kStragglerReach = Fixed::fromInt(20);
// The mark he is after counts this much more (he keeps to him unless a much better one shows).
constexpr int32_t kKeepMarkPct = 125;
// The odds (World::fightingPower). His pack: the raiders of his side within
// kPackReach of him, each at kPackPct of his power (they choose their moment
// and run). Against it: every foe within kCrowdReach of the mark (or prize),
// the armoured, those quick enough to catch him and braced pikes each counting
// again; and the missile troops his pack sees whose reach (their own on their
// own, and kMissileMargin) covers where he would stand, at kCoverPct (missiles
// are what kill a hyena) by how deep in it he would be. He dares a throw at
// kDareOdds per mille or better (a satchel, with which a pack counts twice,
// at kBreakOdds), goes in with his blade at kPounceOdds, and breaks off once
// a mark's odds fall below kBreakOdds.
constexpr Fixed kPackReach = Fixed::fromInt(20);
constexpr int32_t kPackPct = 150;
constexpr Fixed kCrowdReach = Fixed::fromInt(10);
constexpr Fixed kMissileMargin = Fixed::fromInt(5);
constexpr int32_t kCoverPct = 200;
constexpr int32_t kDareOdds = 550, kPounceOdds = 600, kBreakOdds = 450;
// Pikes at least this long, facing him within this, are braced against him.
constexpr Fixed kPikeReach = Fixed::fromInt(2);
constexpr Fixed kBracedCos = Fixed::fromRatio(1, 2);
// He darts in to this share of his throwing range before he throws.
constexpr int32_t kDartPct = 60;
constexpr int32_t kThrowRecoverTicks = kTicksPerSecond / 2;
// Falling back: away from the foes within kRetreatFrom (and the missile troops
// whose reach he is in, and toward where the raid began), to one place
// kRetreatRun off with no foe within kRetreatClear of it, out of the missile
// troops' reach if any is; turned up to 120 degrees from straight away to
// find it. He runs there and chooses again only on arriving, or when a foe
// comes near it (once a second at most), and gives up after kRetreatTicks.
constexpr Fixed kRetreatFrom = Fixed::fromInt(15);
constexpr Fixed kRetreatRun = Fixed::fromInt(20);
constexpr Fixed kRetreatClear = Fixed::fromInt(8);
constexpr int32_t kRetreatTicks = 6 * kTicksPerSecond;
constexpr int32_t kReplanTicks = kTicksPerSecond;
constexpr Fixed kArrive = Fixed::fromRatio(3, 2);
// The raid ends with nothing worth taking in sight this long.
constexpr int32_t kQuietTicks = 15 * kTicksPerSecond;
// Round the foes he sees: out this far beyond the end of their line, then this far behind their rear.
constexpr Fixed kFlankMargin = Fixed::fromInt(14);
constexpr Fixed kRearMargin = Fixed::fromInt(12);
// A satchel is never thrown to burst nearer him than its blast reaches and this much more.
constexpr Fixed kBlastClear = Fixed::fromRatio(3, 2);
// He leads a moving mark by its pace over the flight (a fifth more for the arc), at most this far.
constexpr Fixed kMaxLead = Fixed::fromInt(6);
// Throws scatter as bows do (world.cpp): tighter by 15% a rank, to 40%.
constexpr Fixed kDispersionPerRank = Fixed::fromRatio(3, 20);
constexpr Fixed kMinDispersionFactor = Fixed::fromRatio(2, 5);
// The heavies' answer: a throw is remembered this long; a scavenger this much
// beyond a man's reach, and quicker than him, is not chased.
constexpr uint32_t kThrownMemory = 5 * kTicksPerSecond;
constexpr Fixed kOutrunSlack = Fixed::one();

// Turns from straight away, the smallest first: cos and sin of 0, +-30, +-60, +-90, +-120 degrees.
struct Turn {
    Fixed c, s;
};
constexpr Fixed kHalf = Fixed::fromRatio(1, 2), kCos30 = Fixed::fromRatio(86603, 100000);
constexpr Turn kRetreatTurns[] = {{Fixed::one(), Fixed::zero()}, {kCos30, kHalf},  {kCos30, -kHalf},       {kHalf, kCos30},
                                  {kHalf, -kCos30},              {Fixed::zero(), Fixed::one()}, {Fixed::zero(), -Fixed::one()},
                                  {-kHalf, kCos30},              {-kHalf, -kCos30}};

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

// The soft: archers, healers, wizards, grenadiers.
bool soft(const Unit& u) {
    const auto t = static_cast<UnitType>(u.stats.type);
    return t == UnitType::Archers || t == UnitType::Journeyman || t == UnitType::Wizards || t == UnitType::DwarfGrenadier;
}

bool shooter(const Unit& u) { return u.stats.ranged.muzzleSpeed > Fixed::zero() || u.stats.healing.amount > 0; }

// Missile troops who shoot on their own at whoever comes in reach (not engines, which cannot shoot near them).
bool shoots(const UnitStats& s) {
    return s.ranged.muzzleSpeed > Fixed::zero() && s.ranged.autoRange > Fixed::zero() && s.ranged.minRange == Fixed::zero();
}

Fixed missileReach(const UnitStats& s) { return s.ranged.autoRange + kMissileMargin; }

Vec3 flatDir(const Vec3& from, const Vec3& to, const Vec3& fallback) {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    return len == Fixed::zero() ? fallback : d / len;
}

Vec3 unitOr(const Vec3& v, const Vec3& fallback) {
    const Fixed len = v.length();
    return len == Fixed::zero() ? fallback : v / len;
}

// Whether `o` faces `at` within the arc of his shield.
bool facesWithShield(const Unit& o, const Vec3& at) {
    if (o.stats.shield.blockChance == Fixed::zero()) return false;
    return o.facing.dot(flatDir(o.pos, at, o.facing)) >= o.stats.shield.arcCos;
}

// How much a foe counts against a scavenger, in percent of his fighting
// power: again if armoured, again if quick enough to catch him, again for a
// long pike braced toward him.
int32_t threatPct(const Unit& u, const Unit& q) {
    int32_t pct = 100;
    if (armoured(q.stats)) pct += 100;
    if (q.stats.moveSpeed >= u.stats.moveSpeed) pct += 100;
    if (q.stats.melee.reach >= kPikeReach && q.facing.dot(flatDir(q.pos, u.pos, q.facing)) >= kBracedCos) pct += 100;
    return pct;
}

// Any foe of `u` he can see, standing within `r` of `at`.
bool foeNear(const std::vector<Unit>& units, const Unit& u, const Vec3& at, Fixed r) {
    const Fixed sightSq = u.stats.scavenge.sight * u.stats.scavenge.sight;
    for (const Unit& o : units) {
        if (!o.alive() || o.owner == u.owner || o.routed || o.inAir()) continue;
        if ((o.pos - u.pos).flat().lengthSq() > sightSq) continue;
        if ((o.pos - at).flat().lengthSq() < r * r) return true;
    }
    return false;
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
    if (nav_ && !nav_->clearLine(u.pos, goal, u.stats.gait)) {
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

void World::formPacks() {
    // In id order: the first let loose leads those of his side let loose
    // within reach of him (not those of another pack).
    for (Unit& u : units_) {
        if (!u.alive() || !u.scav.raiding || u.scav.pack != 0) continue;
        const uint32_t pack = ++packs_;
        int32_t men = 0;
        for (Unit& o : units_) {
            if (!o.alive() || o.owner != u.owner || !o.scav.raiding || o.scav.pack != 0) continue;
            if (o.id != u.id && (o.pos - u.pos).flat().lengthSq() > kPackReach * kPackReach) continue;
            o.scav.pack = pack;
            ++men;
        }
        for (Unit& o : units_)
            if (o.scav.pack == pack) o.scav.packMen = men;
    }
}

int32_t World::packLeft(const Unit& u) const {
    int32_t n = 0;
    for (const Unit& o : units_) n += o.alive() && o.owner == u.owner && o.scav.pack == u.scav.pack ? 1 : 0;
    return n;
}

int64_t World::packStrength(const Unit& u) const {
    int64_t sum = 0;
    for (const Unit& o : units_) {
        if (!o.alive() || o.owner != u.owner || o.routed || !o.scav.raiding) continue;
        if (o.id != u.id && (o.pos - u.pos).flat().lengthSq() > kPackReach * kPackReach) continue;
        sum += fightingPower(o) * kPackPct / 100;
    }
    return sum;
}

int64_t World::crowdAt(const Unit& u, const Vec3& at) const {
    int64_t sum = 0;
    for (const Unit& q : units_) {
        if (!q.alive() || q.owner == u.owner || q.routed || q.inAir()) continue;
        if ((q.pos - at).flat().lengthSq() > kCrowdReach * kCrowdReach) continue;
        sum += fightingPower(q) * threatPct(u, q) / 100;
    }
    return sum;
}

int64_t World::missileCover(const Unit& u, const Vec3& spot, const Unit* mark, bool* covered) const {
    int64_t sum = 0;
    bool any = false;
    // Where he and his pack stand: what they see between them.
    const Fixed sightSq = u.stats.scavenge.sight * u.stats.scavenge.sight;
    std::vector<Vec3> eyes{u.pos};
    if (u.scav.pack != 0)
        for (const Unit& o : units_)
            if (o.id != u.id && o.alive() && o.owner == u.owner && o.scav.raiding && o.scav.pack == u.scav.pack) eyes.push_back(o.pos);
    for (const Unit& q : units_) {
        if (!q.alive() || q.owner == u.owner || q.routed || q.inAir() || !shoots(q.stats)) continue;
        if (mark != nullptr && q.id == mark->id) continue;  // the mark himself: the odds say
        bool seen = false;
        for (const Vec3& e : eyes) seen = seen || (q.pos - e).flat().lengthSq() <= sightSq;
        if (!seen) continue;  // out of his pack's sight: he knows nothing of him
        const Fixed reach = missileReach(q.stats);
        const Fixed d = (spot - q.pos).flat().length();
        if (d >= reach) continue;
        any = true;
        if (mark != nullptr && (q.pos - mark->pos).flat().lengthSq() <= kCrowdReach * kCrowdReach) continue;  // in the crowd
        sum += fightingPower(q) * kCoverPct / 100 * (reach - d).raw / reach.raw;
    }
    if (covered) *covered = any;
    return sum;
}

int32_t World::raidOdds(const Unit& u, int64_t crowd, int64_t cover) const {
    int64_t pack = packStrength(u);
    if (u.scav.satchel) pack *= 2;  // a satchel is worth a pack
    return static_cast<int32_t>(pack * 1000 / std::max<int64_t>(1, pack + crowd + cover));
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
            if (o == nullptr || !o->present() || o->alive() || o->gibbed || o->looted || o->inAir()) return false;
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
    const int64_t pack = packStrength(u);
    bool found = false, bestCovered = true;
    Fixed best;
    Scavenger::Prize kind = Scavenger::Prize::None;
    uint32_t ref = 0;
    Vec3 at;
    const auto consider = [&](Scavenger::Prize k, uint32_t r, const Vec3& p, int32_t value) {
        const Fixed d = (p - u.pos).flat().length();
        if (d > sight || abs(p.z - u.pos.z) > kReachUp) return;
        const Fixed score = Fixed::fromInt(value) - d * kRunCost;
        if (found && !bestCovered && score <= best) return;  // ties keep the first found
        for (const Vec3& g : guards)
            if ((g - p).flat().lengthSq() < kGuardReach * kGuardReach) return;
        if (std::find(claimed.begin(), claimed.end(), std::make_pair(k, r)) != claimed.end()) return;
        // Not into a crowd his pack is no match for; out of the missile troops' reach before anything in it.
        bool covered = false;
        const int64_t cover = missileCover(u, p, nullptr, &covered);
        const int64_t crowd = crowdAt(u, p);
        if (pack * 1000 / std::max<int64_t>(1, pack + crowd + cover) < kDareOdds) return;
        if (found && ((covered && !bestCovered) || (covered == bestCovered && score <= best))) return;
        found = true;
        bestCovered = covered;
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
        if (!o.present() || o.alive() || o.gibbed || o.looted || o.inAir() || !bearsBlade(static_cast<UnitType>(o.stats.type))) continue;
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

Unit* World::choosePrey(Unit& u, bool throwing) {
    const Scavenger& s = u.scav;
    const Fixed sight = u.stats.scavenge.sight;
    const Fixed dart = u.stats.scavenge.throwRange * kDartPct / 100;
    int64_t pack = packStrength(u);
    if (throwing && s.satchel) pack *= 2;  // a satchel is worth a pack
    Unit* best = nullptr;
    Fixed bestScore;
    bool bestCovered = true;
    for (Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner || o.inAir()) continue;
        const Vec3 rel = (o.pos - u.pos).flat();
        if (rel.lengthSq() > sight * sight || abs(o.pos.z - u.pos.z) > kReachUp) continue;
        if (!throwing && armoured(o.stats)) continue;  // he never stands up to an armoured man
        const Fixed d = rel.length();
        // What he is: the soft first, armoured men last.
        int32_t w = soft(o) ? kSoftWeight : shooter(o) ? kShooterWeight : armoured(o.stats) ? kArmouredWeight : kPlainWeight;
        // A head is wasted on those who never break, or have already run.
        if (throwing && s.held == RemainKind::Head && (o.stats.moraleShockPct == 0 || o.routed)) w /= 4;
        else if (o.routed) w /= 2;
        // The wounded, up to twice.
        Fixed score = Fixed::fromInt(w) * (2 * int64_t{o.stats.maxHp} - o.hp) / std::max<int64_t>(1, o.stats.maxHp);
        // A satchel is worth more among more of them; anything else at a straggler.
        Fixed alone = kStragglerReach;
        int32_t beside = 0;
        for (const Unit& q : units_) {
            if (!q.alive() || q.id == o.id || q.owner != o.owner || q.routed) continue;
            const Fixed dq = (q.pos - o.pos).flat().length();
            alone = min(alone, dq);
            if (dq < s.blast.radius) ++beside;
        }
        if (throwing && s.satchel) score += Fixed::fromInt(kPlainWeight / 2 * beside);
        else score = score * (alone + Fixed::fromInt(5)) / Fixed::fromInt(10);
        // A shield turned to him catches what he throws.
        if (throwing && facesWithShield(o, u.pos)) score = score * (Fixed::one() - min(o.stats.shield.blockChance, Fixed::one()));
        score = score * 10 / (d + Fixed::fromInt(10));
        const bool current = o.id == s.mark;
        if (current) score = score * kKeepMarkPct / 100;
        // The odds: his pack against those about the man, and the missile
        // troops covering where he would stand to throw (or strike).
        const Vec3 spot = throwing && d > Fixed::zero() ? o.pos - rel / d * min(d, dart) : o.pos;
        bool covered = false;
        const int64_t cover = missileCover(u, spot, &o, &covered);
        const int64_t crowd = crowdAt(u, o.pos);
        const int32_t odds = static_cast<int32_t>(pack * 1000 / std::max<int64_t>(1, pack + crowd + cover));
        const int32_t need = current ? kBreakOdds : !throwing ? kPounceOdds : s.satchel ? kBreakOdds : kDareOdds;
        if (odds < need) continue;
        // Out of the missile troops' reach before anyone in it; then the best (ties keep the first).
        if (best != nullptr && ((covered && !bestCovered) || (covered == bestCovered && score <= bestScore))) continue;
        best = &o;
        bestScore = score;
        bestCovered = covered;
    }
    return best;
}

Vec3 World::prowlGoal(const Unit& u) const {
    // The foes he sees: their middle; the way from where his raid began to
    // it (else from him) is their front.
    const Fixed sightSq = u.stats.scavenge.sight * u.stats.scavenge.sight;
    const auto seen = [&](const Unit& o) {
        return o.alive() && !o.inAir() && o.owner != u.owner && !o.routed && (o.pos - u.pos).flat().lengthSq() <= sightSq;
    };
    Vec3 foes;
    int64_t nf = 0;
    for (const Unit& o : units_) {
        if (!seen(o)) continue;
        foes += o.pos.flat();
        ++nf;
    }
    if (nf == 0) return u.pos;
    const Vec3 e = foes / Fixed::fromInt(nf);
    const Vec3 ahead = flatDir(u.scav.home, e, flatDir(u.pos, e, u.facing));
    const Vec3 side{-ahead.y, ahead.x, Fixed::zero()};
    // How wide they stand either side of their middle, how far forward and back.
    Fixed wide, front, deep;
    for (const Unit& o : units_) {
        if (!seen(o)) continue;
        const Vec3 rel = (o.pos - e).flat();
        wide = max(wide, abs(rel.dot(side)));
        front = max(front, -rel.dot(ahead));
        deep = max(deep, rel.dot(ahead));
    }
    const Vec3 rel = (u.pos - e).flat();
    const Fixed lateral = rel.dot(side), along = rel.dot(ahead);
    // Round the end of their line on his own side of it.
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
        s.mind = Scavenger::Mind::Retreat;  // raiding, he falls back (World::fallBack)
        s.timer = 0;
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

Vec3 World::retreatSpot(const Unit& u) const {
    const Scavenger& s = u.scav;
    const Fixed sightSq = u.stats.scavenge.sight * u.stats.scavenge.sight;
    // Away from the foes near him (the more dangerous the more), and a little
    // from the missile troops he sees whose reach he is in.
    Vec3 away;
    for (const Unit& o : units_) {
        if (!o.alive() || o.owner == u.owner || o.routed || o.inAir()) continue;
        const Vec3 d = (u.pos - o.pos).flat();
        const Fixed lsq = d.lengthSq();
        if (lsq == Fixed::zero() || lsq > sightSq) continue;
        const Fixed len = sqrt(lsq);
        if (len < kRetreatFrom) away += d / len * ((kRetreatFrom - len) / kRetreatFrom * threatPct(u, o) / 100);
        else if (shoots(o.stats) && len < missileReach(o.stats)) away += d / len * Fixed::fromRatio(1, 4);
    }
    // And from the bowman who just shot him, seen or not.
    if (const Unit* q = s.shotBy != kNoEntity ? findUnit(s.shotBy) : nullptr; q != nullptr && q->alive())
        away += flatDir(q->pos, u.pos, Vec3{}) * Fixed::fromInt(2);
    // And toward where the raid began.
    const Vec3 toHome = (s.home - u.pos).flat();
    const Fixed homeLen = toHome.length();
    Vec3 base = unitOr(away, {});
    if (homeLen > Fixed::fromInt(4)) base = base * Fixed::fromInt(2) + toHome / homeLen;
    base = unitOr(base.flat(), -u.facing.flat());
    // Straight that way if it is clear, else turned as little as will do.
    Vec3 best;
    bool have = false, bestCrowded = true;
    int64_t bestCover = 0;
    for (const Turn& t : kRetreatTurns) {
        const Vec3 dir{base.x * t.c - base.y * t.s, base.x * t.s + base.y * t.c, Fixed::zero()};
        Vec3 spot = u.pos + dir * kRetreatRun;
        spot.z = groundHeight(spot.x, spot.y);
        if (!standable(spot) || outsideZone(spot)) continue;
        const bool crowded = foeNear(units_, u, spot, kRetreatClear);
        const int64_t cover = missileCover(u, spot, nullptr, nullptr);
        if (!crowded && cover == 0) return spot;
        if (have && (crowded && !bestCrowded)) continue;
        if (have && crowded == bestCrowded && cover >= bestCover) continue;
        best = spot;
        have = true;
        bestCrowded = crowded;
        bestCover = cover;
    }
    if (have) return best;
    Vec3 spot = u.pos + base * kRetreatRun;
    spot.z = groundHeight(spot.x, spot.y);
    return spot;
}

void World::fallBack(Unit& u) {
    Scavenger& s = u.scav;
    s.mind = Scavenger::Mind::Retreat;
    s.goal = retreatSpot(u);
    s.shotBy = kNoEntity;
    s.timer = kRetreatTicks;
    s.replan = kReplanTicks;
    s.prize = Scavenger::Prize::None;
    s.prizeRef = 0;
    u.path.clear();
    u.pathIndex = 0;
}

bool World::fallingBack(Unit& u, bool think) {
    Scavenger& s = u.scav;
    if (s.replan > 0) --s.replan;
    if (s.timer > 0) --s.timer;
    if (s.timer <= 0 || (s.goal - u.pos).flat().lengthSq() <= kArrive * kArrive) {
        // There (or as far as he will go): he looks about afresh.
        s.timer = 0;
        s.mind = Scavenger::Mind::Scout;
        s.mark = kNoEntity;
        return false;
    }
    // One place, kept: chosen again only when a foe comes near it.
    if (think && s.replan == 0 && foeNear(units_, u, s.goal, kRetreatClear)) {
        s.goal = retreatSpot(u);
        s.replan = kReplanTicks;
    }
    scavengeWalk(u, s.goal);
    return true;
}

void World::prowl(Unit& u, bool think) {
    Scavenger& s = u.scav;
    ++s.quiet;
    if (think) {
        // Standing in the reach of more missile troops than his pack would
        // dare, with nothing to take: out of it, once; and he goes looking
        // round no more this raid. (A lone wizard's reach is no such place.)
        if (!s.wary && raidOdds(u, 0, missileCover(u, u.pos, nullptr, nullptr)) < kDareOdds) {
            s.wary = true;
            fallBack(u);
            return;
        }
        if (s.wary) {
            s.goal = u.pos;
        } else {
            // Round the foes he sees toward their rear, but not into such a reach.
            const Vec3 g = prowlGoal(u);
            s.goal = raidOdds(u, 0, missileCover(u, g, nullptr, nullptr)) < kDareOdds ? u.pos : g;
        }
    }
    if ((s.goal - u.pos).flat().lengthSq() > Fixed::one()) scavengeWalk(u, s.goal);
}

void World::endRaid(Unit& u) {
    Scavenger& s = u.scav;
    s.raiding = false;
    s.wary = false;
    s.mind = Scavenger::Mind::Scout;
    s.prize = Scavenger::Prize::None;
    s.prizeRef = 0;
    s.mark = kNoEntity;
    s.timer = 0;
    s.quiet = 0;
    s.replan = 0;
    s.shotBy = kNoEntity;
    s.goal = {};
    // He holds where he is (his pack still counts him).
    u.hasMoveTarget = false;
    u.path.clear();
    u.pathIndex = 0;
}

void World::raid(Unit& u) {
    Scavenger& s = u.scav;
    const bool think = (tick_ + u.id) % kThinkTicks == 0;
    // Over: half the pack gone since it began, or nothing worth taking in sight this long.
    if (think && (s.quiet >= kQuietTicks || (s.packMen > 1 && packLeft(u) * 2 <= s.packMen))) {
        endRaid(u);
        return;
    }
    // Shot at (by a bowman he may not even see): away from him to one place
    // (unless drawn back to throw: that first), and no more looking round.
    if (s.shotBy != kNoEntity && !(s.mind == Scavenger::Mind::Strike && s.timer > 0)) {
        s.wary = true;
        if (s.mind != Scavenger::Mind::Retreat || s.replan == 0) fallBack(u);
        s.shotBy = kNoEntity;
    }
    // In an armoured foe's reach: away to one place well clear (a throw drawn
    // back is spoiled), unless he is already running somewhere away from him.
    Vec3 away;
    if (scavengeDanger(u, kAvoidReach, &away) > Fixed::zero() && !(away == Vec3{})) {
        const bool into = s.mind == Scavenger::Mind::Retreat && (s.goal - u.pos).flat().dot(away) < Fixed::zero();
        if (s.mind != Scavenger::Mind::Retreat || (into && s.replan == 0)) fallBack(u);
    }
    if (s.mind == Scavenger::Mind::Retreat && fallingBack(u, think)) return;
    if (s.holding()) {
        s.mind = Scavenger::Mind::Strike;
        Unit* mark = s.mark != kNoEntity ? mutableUnit(s.mark) : nullptr;
        if (mark != nullptr && (!mark->alive() || mark->inAir())) mark = nullptr;
        // Weighed again as he goes (a better mark, or the odds turned), but not while drawn back.
        if (s.timer == 0 && (mark == nullptr || think)) {
            Unit* m = choosePrey(u, true);
            if (m == nullptr && mark != nullptr) {
                fallBack(u);  // the odds turned: he breaks off
                return;
            }
            mark = m;
            s.mark = mark != nullptr ? mark->id : kNoEntity;
            if (mark != nullptr) s.markSeen = mark->pos;
        }
        if (mark != nullptr) {
            s.quiet = 0;
            if (strike(u, *mark)) fallBack(u);
            return;
        }
        // Nobody to hit. A burning satchel cannot wait: away with it.
        if (s.satchel && s.fuse > 0) {
            hurl(u, u.pos + u.facing * u.stats.scavenge.throwRange);
            fallBack(u);
            return;
        }
        prowl(u, think);
        return;
    }
    // Empty-handed: after the prize he chose, or the best in sight.
    Vec3 at;
    bool have = s.mind == Scavenger::Mind::Fetch && prizeAt(u, at);
    if (!have || think) have = choosePrize(u) && prizeAt(u, at);
    if (have) {
        s.quiet = 0;
        if (s.prize != Scavenger::Prize::Remain) s.goal = at;
        if ((at - u.pos).flat().length() - u.stats.radius <= kTakeReach) takePrize(u);
        else scavengeWalk(u, at);
        return;
    }
    s.mind = Scavenger::Mind::Scout;
    s.prize = Scavenger::Prize::None;
    s.prizeRef = 0;
    // Nothing to take: a soft man he can cut down, if the odds allow.
    Unit* t = s.mark != kNoEntity ? mutableUnit(s.mark) : nullptr;
    if (t != nullptr && (!t->alive() || t->inAir())) t = nullptr;
    if (t == nullptr || think) {
        Unit* n = choosePrey(u, false);
        if (n == nullptr && t != nullptr) {
            fallBack(u);  // the odds turned: he breaks off
            return;
        }
        t = n;
        s.mark = t != nullptr ? t->id : kNoEntity;
    }
    if (t != nullptr) {
        s.quiet = 0;
        fight(u, *t);
        return;
    }
    prowl(u, think);
}

bool World::scavengeOrdered(Unit& u) {
    if (u.stats.scavenge.sight == Fixed::zero() || !u.scav.holding()) return false;
    Unit* t = mutableUnit(u.targetUnit);
    if (t == nullptr || !t->alive() || t->inAir()) {
        u.targetUnit = kNoEntity;  // gone: he holds where he is
        return true;
    }
    if (u.scav.mark != t->id) u.scav.timer = 0;
    if (strike(u, *t)) u.targetUnit = kNoEntity;  // thrown: the order is done
    return true;
}

void World::shedBurning(Unit& u) {
    Scavenger& s = u.scav;
    if (!s.satchel || s.fuse <= 0 || s.fuse > kSatchelSpareTicks) return;
    // At the nearest foe within his throw (but not within its blast of him), else ahead of him.
    Vec3 at = u.pos + u.facing * u.stats.scavenge.throwRange;
    if (const Unit* t = nearestEnemy(u, u.stats.scavenge.throwRange); t != nullptr && (t->pos - u.pos).flat().length() >= s.blast.radius + kBlastClear)
        at = t->pos;
    hurl(u, at);
    s.mind = Scavenger::Mind::Retreat;
    s.timer = 0;
}

bool World::outruns(const Unit& u, const Unit& foe) const {
    if (foe.stats.scavenge.sight == Fixed::zero() || foe.stats.moveSpeed <= u.stats.moveSpeed) return false;
    const Fixed gap = (foe.pos - u.pos).flat().length() - u.stats.radius - foe.stats.radius;
    return gap > u.stats.melee.reach + kOutrunSlack;
}

Unit* World::shieldTarget(Unit& u, Unit* near) {
    if (u.thrower != kNoEntity) {
        // The last to throw at him, while he remembers it (he would be fighting him if he could reach him).
        Unit* t = mutableUnit(u.thrower);
        if (t != nullptr && t->alive() && !t->routed && !t->inAir() && tick_ - u.thrownTick <= kThrownMemory) return t;
        u.thrower = kNoEntity;
    }
    return near;  // one he cannot catch at hand (updateUnit: the rest he fights)
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
