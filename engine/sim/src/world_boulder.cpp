// The boulder (docs/GAME_DESIGN.md, "The boulder"). A catapult's stone is a thing in the world: it flies its high arc
// (World::updateProjectiles), smashes where it comes down (World::explode, as ever), and then it is not spent. It
// bounces once or twice and ROLLS ON, keeping what speed the ground leaves it: faster down a slope and slower up one,
// far on a road, a little way in sand, mud and bog, dead in water (a splash), turned and all but stopped by a tree,
// a rock, a house, a tower, a wall or a shut gate. Every man it passes over is crushed and thrown, his fellows
// shaken; each takes some of its speed, and a great beast or an engine may stop it. It knows no side: it rolls
// through its own men as through the foe, so the aim (World::fireAt) reckons the whole way it will go
// (World::rollToll) and the player is shown it (World::stonePath) before he orders the throw. Where it stops it lies.
//
// All of it integers, Fixed and nothing of chance: the way of a stone over the ground is the same on every machine,
// and the same in the forecast as in the throw (stepStone serves both).
#include <algorithm>
#include <utility>
#include <vector>

#include "illyria/sim/ballistics.h"
#include "illyria/sim/terrain.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }
constexpr Fixed r(int64_t n, int64_t d) { return Fixed::fromRatio(n, d); }

// What the ground does to a stone: `drag`, the share of its weight that holds it back as it rolls (a deceleration of
// drag x g); `bounce`, how much of its downward speed it comes up with; `grip`, how much forward speed each metre a
// second of downward speed costs it where it strikes (it digs in).
struct Going {
    Fixed drag, bounce, grip;
};
Going goingOn(Ground g) {
    switch (g) {
        case Ground::Road:
        case Ground::Bridge: return {r(1, 5), r(1, 5), r(1, 4)};
        case Ground::Plains: return {r(7, 25), r(3, 20), r(7, 25)};
        case Ground::Rock: return {r(8, 25), r(1, 4), r(3, 10)};  // (broken ground: it bounds higher and is checked harder)
        case Ground::Forest: return {r(2, 5), r(1, 10), r(3, 10)};
        case Ground::Sand: return {r(3, 5), r(1, 20), r(2, 5)};
        case Ground::Mud: return {r(4, 5), Fixed::zero(), r(9, 20)};
        case Ground::Swamp: return {Fixed::one(), Fixed::zero(), r(1, 2)};
        case Ground::Shallows:
        case Ground::DeepWater: break;
    }
    return {Fixed::fromInt(2), Fixed::zero(), Fixed::one()};
}

// Slower than this it has stopped (unless the slope still pulls it), and a hop lower than kHop is a roll.
constexpr Fixed kRestSpeed = r(2, 5);
constexpr Fixed kHop = r(6, 5);
// It comes down on the ground hard enough to raise dust at this (m/s downward).
constexpr Fixed kThud = Fixed::fromInt(2);
// On the ground: within this of it. Ground that falls away more than kLedge in one step is a drop, and it flies.
constexpr Fixed kContact = r(1, 20);
constexpr Fixed kLedge = r(3, 5);
// No slope makes it faster than this.
constexpr Fixed kMaxSpeed = Fixed::fromInt(32);
// Off a tree it comes back with a tenth of the speed it went in with, off stone and oak a fifth; and what glances
// on past the thing keeps half its speed.
constexpr Fixed kTreeRebound = r(1, 10), kStoneRebound = r(1, 5), kGlanceKeep = r(1, 2);
// A touch softer than this is no blow (nothing told, nothing broken).
constexpr Fixed kTap = Fixed::one();
// A stone straight from the air strikes at about this speed: the measure of a rolling one's blow (strikeStructure).
constexpr Fixed kFullBlow = Fixed::fromInt(24);
// A stone that ROLLED into a gate does this share of what the same blow from the air would (the doors are struck low,
// at their sill, and the stone has no fall behind it).
constexpr Fixed kRolledGateShare = r(1, 2);
// A deck this far above the land under it is masonry (a wall-walk, a tower's top, a ruin's floor): a stone shatters on it.
constexpr Fixed kMasonryRise = r(1, 2);
// It rolls for this long at most (a long hill), and lies where it stopped for this long.
constexpr int32_t kRollTicks = 45 * kTicksPerSecond;
constexpr int32_t kLieTicks = 20 * 60 * kTicksPerSecond;

// The men under it. Slower than kCrushSpeed it hurts nobody (it trundles; a big body stops it dead). The wound is
// kCrushLeast and kCrushPerSpeed more for each metre a second, to kCrushMost: a man run over at a walk is badly hurt,
// at a run dead, and an armoured one may live through the first and not the second.
constexpr Fixed kCrushSpeed = Fixed::fromInt(3);
constexpr int32_t kCrushLeast = 30, kCrushPerSpeed = 12;
constexpr int32_t kCrushMost = 250;
// He is thrown at four fifths of its speed (for a body of mass one; World::knock).
constexpr Fixed kThrow = r(4, 5);
// Each body takes its share of the stone's speed, as a thing of the body's mass struck by one of kStoneMass: a man a
// fifteenth, a horseman a sixth, a giant a third, an elephant near half, an engine two thirds. A body of kBlockMass
// or more that leaves it slower than kCrushSpeed has stopped it.
constexpr Fixed kStoneMass = Fixed::fromInt(14);
constexpr Fixed kBlockMass = Fixed::fromInt(4);
// The shock to the man struck, and to those of his side within kShockReach who saw it plough through.
constexpr int32_t kStruckShock = 120, kSeenShock = 40;
constexpr Fixed kShockReach = Fixed::fromInt(5);

int32_t crushWound(Fixed speed) { return std::min(kCrushMost, kCrushLeast + static_cast<int32_t>((speed * kCrushPerSpeed).floorToInt())); }

Vec3 flatUnit(const Vec3& v, const Vec3& fallback) {
    const Vec3 f = v.flat();
    const Fixed len = f.length();
    return len == Fixed::zero() ? fallback : f / len;
}

}  // namespace

Structure World::structureOf(int32_t what) const {
    if (what == kHitGate) return Structure::Gate;
    if (what == kHitWall || !config_.region || static_cast<size_t>(what) >= config_.region->obstacles().size()) return Structure::Wall;
    switch (config_.region->obstacles()[static_cast<size_t>(what)].kind) {
        case Obstacle::Kind::Pine:
        case Obstacle::Kind::Oak:
        case Obstacle::Kind::Bush:
        case Obstacle::Kind::Log: return Structure::Tree;
        case Obstacle::Kind::Rock:
        case Obstacle::Kind::Boulder:
        case Obstacle::Kind::Pillar: return Structure::Rock;
        case Obstacle::Kind::House: return Structure::House;
        case Obstacle::Kind::Tower: return Structure::Tower;
    }
    return Structure::Wall;
}

void World::strikeStructure(Structure kind, const Vec3& where, const Vec3& dir, Fixed energy, bool rolled) {
    ImpactEvent e{ImpactEvent::Kind::Struck, where, dir, kNoEntity, kNoEntity, false, energy};
    e.gate = static_cast<int32_t>(kind);
    impacts_.push_back(e);
    // The doors a stone rolled into: its blow, as a smash right against them (World::blastGates knows how a gate takes one).
    if (kind == Structure::Gate && rolled) {
        const int32_t blow = static_cast<int32_t>((energy * kRolledGateShare).floorToInt());
        if (blow > 0) blastGates(where, Explosive{Fixed::one(), blow, Fixed::zero(), 0, Fixed::zero()}, ProjectileKind::Stone);
    }
    // (Walls, towers and houses: no hit points yet. Castle damage begins here.)
}

EntityId World::launchStone(PlayerId owner, const Vec3& pos, const Vec3& vel) {
    const RangedWeapon& w = unitStats(UnitType::Catapult).ranged;
    Projectile p;
    p.id = nextId_++;
    p.owner = owner;
    p.kind = ProjectileKind::Stone;
    p.pos = pos;
    p.vel = vel;
    p.explosive = w.explosive;
    p.cause = w.damageType;
    projectiles_.push_back(p);
    return p.id;
}

Vec3 World::stoneSlope(Fixed x, Fixed y, Fixed floor) const {
    // (The land's own height: a wall-walk or a bridge beside it is not a hillside.)
    if (!config_.terrain || floor != config_.terrain->heightAt(x, y)) return {};
    const Terrain& land = *config_.terrain;
    const Fixed d = Fixed::one();
    return {(land.heightAt(x + d, y) - land.heightAt(x - d, y)) / 2, (land.heightAt(x, y + d) - land.heightAt(x, y - d)) / 2, Fixed::zero()};
}

Vec3 World::stoneLaunch(const Unit& engine, const Vec3& at, Vec3& origin) const {
    origin = engine.pos + up(engine.stats.height * 4 / 5);
    return solveFixedElevation(origin, at, kStoneElevation, engine.stats.ranged.muzzleSpeed, config_.gravity).velocity;
}

EntityId World::throwStone(const Unit& engine, const Vec3& mark) {
    Vec3 origin;
    const Vec3 launch = stoneLaunch(engine, mark, origin);
    return launchStone(engine.owner, origin, launch);
}

World::StoneStep World::groundStone(Projectile& p) const {
    StoneStep s;
    s.at = p.pos;
    // Its speed into the ground: its fall, and on a slope the rise of the ground it runs into (so it strikes a hill
    // facing it harder, and one falling away from it less).
    const Fixed down = max(Fixed::zero(), p.vel.flat().dot(stoneSlope(p.pos.x, p.pos.y, p.pos.z)) - p.vel.z);
    s.blow = down;
    if (isWater(p.pos.x, p.pos.y)) {
        p.vel = {};
        p.resting = true;
        s.met = StoneStep::Met::Water;
        return s;
    }
    const Ground ground = groundAt(p.pos.x, p.pos.y);
    if (config_.terrain && ground != Ground::Bridge && p.pos.z > config_.terrain->heightAt(p.pos.x, p.pos.y) + kMasonryRise) {
        p.vel = {};
        p.resting = true;
        s.met = StoneStep::Met::Obstacle;
        s.what = kHitWall;
        return s;
    }
    const Going g = goingOn(ground);
    Vec3 flat = p.vel.flat();
    const Fixed speed = flat.length();
    if (speed > Fixed::zero()) flat = flat * (max(Fixed::zero(), speed - g.grip * down) / speed);
    const Fixed rise = down * g.bounce;
    p.vel = flat + up(rise >= kHop ? rise : Fixed::zero());
    if (down >= kThud) s.met = StoneStep::Met::Bounce;
    return s;
}

World::StoneStep World::stepStone(Projectile& p) const {
    StoneStep s;
    const Vec3 prev = p.pos;
    const Fixed floor = surfaceBelow(prev.x, prev.y, prev.z + kStoneRadius);
    const bool rolling = p.vel.z == Fixed::zero() && prev.z <= floor + kContact;
    if (rolling) {
        if (isWater(prev.x, prev.y)) {
            p.vel = {};
            p.resting = true;
            s.met = StoneStep::Met::Water;
            s.at = prev;
            return s;
        }
        // The slope's pull (on the land itself: a deck is level).
        const Vec3 grade = stoneSlope(prev.x, prev.y, floor);
        const Vec3 pull = -grade * (config_.gravity / (Fixed::one() + grade.lengthSq()));
        const Fixed drag = config_.gravity * goingOn(groundAt(prev.x, prev.y)).drag;
        Vec3 flat = p.vel.flat();
        if (flat.lengthSq() < kRestSpeed * kRestSpeed && pull.lengthSq() <= drag * drag) {
            p.vel = {};
            p.resting = true;
            s.met = StoneStep::Met::Rest;
            s.at = prev;
            return s;
        }
        flat += pull * kTickDt;
        const Fixed speed = flat.length(), cut = drag * kTickDt;
        flat = speed <= cut ? Vec3{} : flat * (min(speed - cut, kMaxSpeed) / speed);
        p.vel = flat;
        Vec3 next = prev + flat * kTickDt;
        const Fixed under = surfaceBelow(next.x, next.y, prev.z + kStoneRadius);
        if (under < prev.z - kLedge) {
            next.z = prev.z;  // over a drop: it flies
            p.vel.z = -config_.gravity * kTickDt;
        } else {
            next.z = under;
        }
        p.pos = next;
        p.rolled += flat.length() * kTickDt;
    } else {
        p.pos += p.vel * kTickDt + up(-config_.gravity * (kTickDt * kTickDt / 2));
        p.vel.z -= config_.gravity * kTickDt;
        const Fixed under = surfaceBelow(p.pos.x, p.pos.y, prev.z + kStoneRadius);
        if (p.pos.z <= under) {
            p.pos.z = under;
            s = groundStone(p);
        }
    }
    // What stands in its way turns it and takes nearly all it had.
    int32_t what = 0;
    const Vec3 mid = up(kStoneRadius);
    const Fixed t = obstacleHit(prev + mid, p.pos + mid, Fixed::one(), false, nullptr, &what);
    if (t < Fixed::zero()) return s;
    const Vec3 flat = p.vel.flat();
    const Vec3 dir = flatUnit(p.pos - prev, flatUnit(flat, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}));
    Vec3 normal = -dir;
    Fixed rebound = kStoneRebound;
    if (what >= 0) {
        const Obstacle& o = config_.region->obstacles()[static_cast<size_t>(what)];
        normal = flatUnit(prev - o.pos, -dir);
        if (structureOf(what) == Structure::Tree) rebound = kTreeRebound;
    }
    Fixed into = -flat.dot(normal);
    if (into <= Fixed::zero()) {  // (already inside its reach: straight back out)
        normal = -dir;
        into = flat.length();
    }
    s.met = StoneStep::Met::Obstacle;
    s.at = prev + (p.pos - prev) * t + mid;
    s.blow = into;
    s.what = what;
    const Vec3 off = (flat + normal * (into * (Fixed::one() + rebound))) * kGlanceKeep;
    p.pos.x = prev.x;
    p.pos.y = prev.y;
    p.vel = {off.x, off.y, p.vel.z};
    return s;
}

bool World::tellStone(Projectile& p, const StoneStep& step) {
    switch (step.met) {
        case StoneStep::Met::Nothing:
        case StoneStep::Met::Rest: break;
        case StoneStep::Met::Bounce:
            impacts_.push_back({ImpactEvent::Kind::Bounce, step.at, p.vel, p.id, kNoEntity, false, step.blow});
            break;
        case StoneStep::Met::Water:
            impacts_.push_back({ImpactEvent::Kind::Splash, step.at, up(Fixed::one()), p.id, kNoEntity, false, step.blow});
            return groundAt(step.at.x, step.at.y) == Ground::DeepWater;  // sunk; in the shallows it lies
        case StoneStep::Met::Obstacle:
            if (step.blow < kTap) break;
            strikeStructure(structureOf(step.what), step.at, p.vel, Fixed::fromInt(p.explosive.damage) * min(Fixed::one(), step.blow / kFullBlow), true);
            break;
    }
    return false;
}

bool World::landStone(Projectile& p, const Vec3& at, StoneFall how, int32_t what) {
    p.landed = true;
    const Fixed full = Fixed::fromInt(p.explosive.damage);
    switch (how) {
        case StoneFall::Obstacle: {
            strikeStructure(structureOf(what), at, p.vel, full, false);
            // It drops at the foot of what it struck.
            const Vec3 dir = flatUnit(p.vel, {});
            p.pos = at - dir * (kStoneRadius + r(3, 10));
            p.pos.z = max(at.z - kStoneRadius, surfaceBelow(p.pos.x, p.pos.y, at.z));
            p.vel = -p.vel.flat() / Fixed::fromInt(12);
            return true;
        }
        case StoneFall::Man:
            p.pos = at;
            p.vel = p.vel * r(3, 4);  // through him, and on down
            return true;
        case StoneFall::Ground: {
            p.pos = at;
            const Vec3 vel = p.vel;
            const StoneStep step = groundStone(p);
            if (step.met == StoneStep::Met::Obstacle) {  // on a wall-walk or a tower's top: it shatters there
                strikeStructure(Structure::Wall, at, vel, full, false);
                return false;
            }
            // (Its dust on the ground is the smash's own.)
            return step.met != StoneStep::Met::Water || !tellStone(p, step);
        }
    }
    return true;
}

bool World::rollStone(Projectile& p) {
    if (p.resting) return p.ageTicks > kLieTicks;
    if (p.ageTicks > kRollTicks) {
        p.vel = {};
        p.resting = true;
        return false;
    }
    const Vec3 prev = p.pos;
    const StoneStep step = stepStone(p);
    if (tellStone(p, step)) return true;

    // The men it passed over in this step: each once, as its middle comes abreast of him.
    const Vec3 travel = (p.pos - prev).flat();
    const Fixed len = travel.length();
    if (len == Fixed::zero()) return false;
    const Vec3 dir = travel / len;
    const Fixed low = min(prev.z, p.pos.z), high = max(prev.z, p.pos.z) + kStoneRadius * 2;
    thread_local std::vector<std::pair<Fixed, uint32_t>> under;
    under.clear();
    nearby(prev.x + travel.x / 2, prev.y + travel.y / 2, len / 2 + Fixed::fromInt(6), [&](uint32_t k) {
        const Unit& u = units_[k];
        if (!u.alive() || u.inAir() || high < u.pos.z || low > u.pos.z + u.stats.height) return;
        const Vec3 rel = (u.pos - prev).flat();
        const Fixed along = rel.dot(dir);
        if (along < Fixed::zero() || along >= len) return;
        const Fixed reach = kStoneRadius + u.stats.radius;
        if (rel.lengthSq() - along * along > reach * reach) return;
        under.push_back({along, k});
    });
    std::sort(under.begin(), under.end());
    for (const auto& [along, k] : under) {
        Unit& u = units_[k];
        const Fixed speed = p.vel.flat().length();
        const bool big = u.stats.mass >= kBlockMass;
        if (speed < kCrushSpeed) {
            if (!big) continue;
            p.vel = {};  // it comes to rest against him
            break;
        }
        const Vec3 rel = (u.pos - prev).flat();
        const Fixed side = dir.x * rel.y - dir.y * rel.x;
        const Vec3 away = flatUnit(dir + Vec3{-dir.y, dir.x, Fixed::zero()} * (side < Fixed::zero() ? -r(1, 2) : r(1, 2)), dir);
        const Vec3 centre = u.pos + up(u.stats.height / 2);
        if (u.scav.satchel) setOffHeld(u);
        const bool killed = damage(u, crushWound(speed), nullptr, {DamageType::Blunt, p.vel, centre, false, true});
        impacts_.push_back({ImpactEvent::Kind::Hit, centre, dir, p.id, u.id, killed, {}, -1, DamageType::Blunt});
        if (killed && u.death.kind == DeathKind::Gibbed) spawnGibs(u, u.pos - dir, speed * kThrow);
        knock(u, away, speed * kThrow);
        shakeMorale(u, kStruckShock);
        const PlayerId side0 = u.owner;
        const EntityId struck = u.id;
        const Vec3 where = u.pos;
        nearby(where.x, where.y, kShockReach, [&](uint32_t j) {
            Unit& o = units_[j];
            if (o.owner != side0 || o.id == struck || !o.alive() || o.inAir()) return;
            if ((o.pos - where).flat().lengthSq() < kShockReach * kShockReach) shakeMorale(o, kSeenShock);
        });
        p.vel = p.vel * (kStoneMass / (kStoneMass + u.stats.mass));
        if (big && p.vel.flat().lengthSq() < kCrushSpeed * kCrushSpeed) {
            p.vel = {};
            break;
        }
    }
    return false;
}

StonePath World::stonePath(const Unit& engine, const Vec3& mark) const {
    StonePath path;
    Vec3 at = mark;
    at.z = surfaceBelow(mark.x, mark.y, mark.z + Fixed::one());
    path.landing = at;
    // As it is thrown, and as it comes down at the end of that arc.
    Vec3 origin;
    const Vec3 launch = stoneLaunch(engine, at, origin);
    const Fixed over = launch.flat().length();
    if (over == Fixed::zero()) return path;
    Projectile p;
    p.kind = ProjectileKind::Stone;
    p.landed = true;
    p.pos = at;
    p.vel = launch + up(-config_.gravity * ((at - origin).flat().length() / over));
    groundStone(p);
    for (int32_t i = 0; i < kRollTicks && !p.resting; ++i) {
        path.roll.push_back({p.pos, p.vel.flat().length()});
        stepStone(p);
    }
    path.roll.push_back({p.pos, Fixed::zero()});
    return path;
}

World::RollToll World::rollToll(const Unit& u, const Vec3& mark, Fixed spread) const {
    RollToll t;
    const StonePath path = stonePath(u, mark);
    if (path.roll.size() < 2) return t;
    Vec3 lo = path.roll.front().pos, hi = lo;
    for (const StonePath::Point& q : path.roll) {
        lo = {min(lo.x, q.pos.x), min(lo.y, q.pos.y), Fixed::zero()};
        hi = {max(hi.x, q.pos.x), max(hi.y, q.pos.y), Fixed::zero()};
    }
    const Vec3 mid = (lo + hi) * r(1, 2);
    nearby(mid.x, mid.y, (hi - lo).flat().length() / 2 + spread + Fixed::fromInt(3), [&](uint32_t k) {
        const Unit& o = units_[k];
        if (!o.alive() || o.inAir() || o.id == u.id) return;
        // In its lane, or as near it as the stone may fall wide: then it passes over him with the odds of the lane in that width.
        const Fixed lane = kStoneRadius + o.stats.radius, wide = lane + spread;
        Fixed best = wide * wide, speed;
        bool found = false;
        for (size_t i = 0; i < path.roll.size(); i += 2) {
            const StonePath::Point& q = path.roll[i];
            const Fixed d2 = (o.pos - q.pos).flat().lengthSq();
            if (d2 >= best || q.pos.z + kStoneRadius * 2 < o.pos.z || q.pos.z > o.pos.z + o.stats.height) continue;
            best = d2;
            speed = q.speed;
            found = true;
        }
        if (!found || speed < kCrushSpeed) return;
        const Fixed hurt = Fixed::fromInt(crushWound(speed)) * (lane / wide);
        if (o.owner != u.owner) {
            t.foe += hurt;
            return;
        }
        t.friends += hurt;
        t.badlyHurtFriend = t.badlyHurtFriend || hurt * 10 > Fixed::fromInt(o.stats.maxHp);
    });
    return t;
}

}  // namespace illyria::sim
