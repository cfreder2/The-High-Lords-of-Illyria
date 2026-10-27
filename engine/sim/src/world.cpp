#include "illyria/sim/world.h"

#include <algorithm>

#include "illyria/sim/ballistics.h"
#include "illyria/sim/hash.h"

namespace illyria::sim {

namespace {

constexpr int32_t kMaxProjectileAgeTicks = 30 * kTicksPerSecond;
// Arrows leave the bow at 80% of the archer's height.
constexpr Fixed kMuzzleHeightFraction = Fixed::fromRatio(4, 5);
// Each rank shrinks the dispersion circle by 15%, down to 40% of the base size.
constexpr Fixed kDispersionPerRank = Fixed::fromRatio(3, 20);
constexpr Fixed kMinDispersionFactor = Fixed::fromRatio(2, 5);
// Fliers climb and dive at this rate, and drop to this height to fight.
constexpr Fixed kFlyClimbRate = Fixed::fromInt(5);
constexpr Fixed kSwoopAltitude = Fixed::fromRatio(6, 5);
constexpr Fixed kSwoopDistance = Fixed::fromInt(8);
// Ticks a thrown soldier needs to get back up.
constexpr int32_t kRecoverTicks = kTicksPerSecond * 2 / 3;
// Bodies thrown by explosions and blows.
constexpr Fixed kLiftFraction = Fixed::fromRatio(7, 10);
constexpr Fixed kGroundFriction = Fixed::fromRatio(11, 20);
constexpr Fixed kRestSpeed = Fixed::fromRatio(6, 5);
constexpr Fixed kDebrisRestitution = Fixed::fromRatio(1, 4);
// An arrow falling faster than this sticks into the ground; slower, it lies flat.
constexpr Fixed kStickSpeed = Fixed::fromInt(9);
// Melee blows are blocked less often than arrows: the attacker can work round the shield.
constexpr Fixed kMeleeBlockScale = Fixed::fromRatio(7, 10);
// Terrain bonus thresholds (rise over run).
constexpr Fixed kOpenSlope = Fixed::fromRatio(3, 50);
constexpr Fixed kHillsSlope = Fixed::fromRatio(3, 20);
// Healers look this far for wounded friends.
constexpr Fixed kHealSeekRange = Fixed::fromInt(25);
// Ground units do not chase fliers cruising out of reach.
constexpr Fixed kMaxReachableAltitude = Fixed::fromInt(4);

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }

void hashVec(StateHasher& h, const Vec3& v) {
    h.addI64(v.x.raw);
    h.addI64(v.y.raw);
    h.addI64(v.z.raw);
}

void hashExplosive(StateHasher& h, const Explosive& e) {
    h.addI64(e.radius.raw);
    h.addI64(e.damage);
    h.addI64(e.impulse.raw);
    h.addI64(e.fuseTicks);
    h.addI64(e.bounce.raw);
}

void hashStats(StateHasher& h, const UnitStats& s) {
    h.addU64(s.type);
    h.addI64(s.radius.raw);
    h.addI64(s.height.raw);
    h.addI64(s.moveSpeed.raw);
    h.addI64(s.maxHp);
    h.addI64(s.ranged.muzzleSpeed.raw);
    h.addI64(s.ranged.reloadTicks);
    h.addI64(s.ranged.damage);
    h.addI64(s.ranged.dispersionAt100m.raw);
    h.addI64(s.ranged.autoRange.raw);
    h.addU64(static_cast<uint64_t>(s.ranged.kind));
    hashExplosive(h, s.ranged.explosive);
    h.addI64(s.melee.damage);
    h.addI64(s.melee.reach.raw);
    h.addI64(s.melee.cooldownTicks);
    h.addI64(s.melee.knockback.raw);
    h.addI64(s.melee.bonusVsMountedPct);
    h.addI64(s.aggroRange.raw);
    h.addI64(s.shield.blockChance.raw);
    h.addI64(s.shield.arcCos.raw);
    h.addI64(s.healing.amount);
    h.addI64(s.healing.range.raw);
    h.addI64(s.healing.cooldownTicks);
    h.addI64(s.healing.charges);
    h.addI64(s.terrain.openPct);
    h.addI64(s.terrain.hillsPct);
    h.addI64(s.flyAltitude.raw);
    h.addI64(s.mass.raw);
    h.addU64(s.mounted);
    h.addU64(s.bleeds);
}

// Unit-length horizontal direction from `from` to `to`, or `fallback` if they coincide.
Vec3 flatDirection(const Vec3& from, const Vec3& to, const Vec3& fallback) {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    return len == Fixed::zero() ? fallback : d / len;
}

Vec3 normalized(const Vec3& v) {
    const Fixed len = v.length();
    return len == Fixed::zero() ? v : v / len;
}

}  // namespace

World::World(const WorldConfig& config) : config_(config), rng_(config.seed, 0) {}

EntityId World::spawnUnit(PlayerId owner, const Vec3& pos, const Vec3& facing,
                          const UnitStats& stats) {
    Unit u;
    u.id = nextId_++;
    u.owner = owner;
    u.stats = stats;
    u.hp = stats.maxHp;
    u.healCharges = stats.healing.charges;
    u.pos = pos;
    if (config_.terrain || stats.flyAltitude > Fixed::zero()) {
        u.pos.z = groundHeight(pos.x, pos.y) + stats.flyAltitude;
    }
    u.facing = facing;
    units_.push_back(u);
    return u.id;
}

const Unit* World::findUnit(EntityId id) const {
    // Units are only ever appended, so the vector is sorted by id.
    auto it = std::lower_bound(units_.begin(), units_.end(), id,
                               [](const Unit& u, EntityId i) { return u.id < i; });
    return it != units_.end() && it->id == id ? &*it : nullptr;
}

Unit* World::mutableUnit(EntityId id) {
    return const_cast<Unit*>(static_cast<const World*>(this)->findUnit(id));
}

Fixed World::groundHeight(Fixed x, Fixed y) const {
    return config_.terrain ? config_.terrain->heightAt(x, y) : Fixed::zero();
}

Fixed World::slopeAt(Fixed x, Fixed y) const {
    if (!config_.terrain) return Fixed::zero();
    const Fixed d = Fixed::one();
    const Fixed gx = (groundHeight(x + d, y) - groundHeight(x - d, y)) / 2;
    const Fixed gy = (groundHeight(x, y + d) - groundHeight(x, y - d)) / 2;
    return sqrt(gx * gx + gy * gy);
}

void World::step(std::span<const Command> commands) {
    impacts_.clear();
    changedRemains_.clear();

    // Peers may receive one tick's commands from different players in any
    // order; applying them grouped by player keeps the outcome identical.
    // Each player's own commands keep the order they were issued in.
    std::vector<Command> ordered(commands.begin(), commands.end());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Command& a, const Command& b) { return a.player < b.player; });
    for (const Command& cmd : ordered) applyCommand(cmd);

    for (Unit& u : units_) updateUnit(u);
    updateProjectiles();
    ++tick_;
}

void World::applyCommand(const Command& cmd) {
    Unit* u = mutableUnit(cmd.unit);
    if (u == nullptr || !u->alive() || u->owner != cmd.player) return;
    switch (cmd.type) {
        case Command::Type::Move:
            u->hasMoveTarget = true;
            u->moveTarget = cmd.target;
            u->hasAttackTarget = false;
            break;
        case Command::Type::AttackGround:
            if (u->stats.ranged.muzzleSpeed == Fixed::zero()) return;
            u->hasAttackTarget = true;
            u->attackTarget = cmd.target;
            u->hasMoveTarget = false;
            break;
        case Command::Type::Stop:
            u->hasMoveTarget = false;
            u->hasAttackTarget = false;
            break;
    }
}

void World::updateUnit(Unit& u) {
    if (u.airborne) {
        updateAirborne(u);
        return;
    }
    if (!u.alive()) return;
    if (u.reloadRemaining > 0) --u.reloadRemaining;
    if (u.meleeCooldown > 0) --u.meleeCooldown;
    if (u.healCooldown > 0) --u.healCooldown;
    u.engaged = false;

    if (u.hasMoveTarget) {
        // Orders take priority over fighting: a moving unit ignores enemies.
        const Vec3 delta = (u.moveTarget - u.pos).flat();
        const Fixed dist = delta.length();
        const Fixed stepLen = u.stats.moveSpeed * kTickDt;
        if (dist <= stepLen) {
            u.pos.x = u.moveTarget.x;
            u.pos.y = u.moveTarget.y;
            u.hasMoveTarget = false;
        } else {
            u.facing = delta / dist;
            u.pos += u.facing * stepLen;
        }
    } else if (u.hasAttackTarget) {
        u.facing = flatDirection(u.pos, u.attackTarget, u.facing);
        if (u.reloadRemaining == 0) {
            fire(u, u.attackTarget);
            u.reloadRemaining = u.stats.ranged.reloadTicks;
        }
    } else if (tryHeal(u)) {
        // Healing a wounded friend takes priority over fighting.
    } else if (Unit* enemy = u.stats.melee.damage > 0 ? nearestEnemy(u, u.stats.aggroRange) : nullptr) {
        fight(u, *enemy);
    } else if (Unit* target = u.stats.ranged.autoRange > Fixed::zero()
                                  ? nearestEnemy(u, u.stats.ranged.autoRange)
                                  : nullptr) {
        u.facing = flatDirection(u.pos, target->pos, u.facing);
        if (u.reloadRemaining == 0) {
            fire(u, target->pos);
            u.reloadRemaining = u.stats.ranged.reloadTicks;
        }
    }

    const Fixed ground = groundHeight(u.pos.x, u.pos.y);
    if (u.stats.flyAltitude > Fixed::zero()) {
        const Fixed want = ground + (u.engaged ? kSwoopAltitude : u.stats.flyAltitude);
        const Fixed climb = kFlyClimbRate * kTickDt;
        u.pos.z = clamp(want, u.pos.z - climb, u.pos.z + climb);
        u.pos.z = max(u.pos.z, ground);
    } else {
        u.pos.z = ground;
    }
}

void World::updateAirborne(Unit& u) {
    u.vel.z -= config_.gravity * kTickDt;
    u.pos += u.vel * kTickDt;
    // Living fliers catch themselves once they start to fall.
    if (u.alive() && u.stats.flyAltitude > Fixed::zero() && u.vel.z < Fixed::zero()) {
        u.vel = {};
        u.airborne = false;
        return;
    }
    const Fixed ground = groundHeight(u.pos.x, u.pos.y);
    if (u.pos.z > ground) return;
    u.pos.z = ground;
    if (u.vel.z < -Fixed::fromInt(4)) {
        // A hard landing bounces a little.
        u.vel.z = -u.vel.z * kDebrisRestitution;
        u.vel.x *= kGroundFriction;
        u.vel.y *= kGroundFriction;
        return;
    }
    u.vel = {};
    u.airborne = false;
    if (u.alive()) {
        u.reloadRemaining = std::max(u.reloadRemaining, kRecoverTicks);
        u.meleeCooldown = std::max(u.meleeCooldown, kRecoverTicks);
    }
}

Unit* World::nearestEnemy(const Unit& u, Fixed range) {
    if (range <= Fixed::zero()) return nullptr;
    const bool groundBound = u.stats.flyAltitude == Fixed::zero() && u.stats.ranged.muzzleSpeed == Fixed::zero();
    Unit* best = nullptr;
    Fixed bestDistSq = range * range;
    // Ascending id order, strict comparison: ties go to the lowest id.
    for (Unit& other : units_) {
        if (!other.alive() || other.owner == u.owner) continue;
        if (groundBound &&
            other.pos.z - groundHeight(other.pos.x, other.pos.y) > kMaxReachableAltitude) {
            continue;
        }
        const Fixed d = (other.pos - u.pos).flat().lengthSq();
        if (d <= bestDistSq && (best == nullptr || d < bestDistSq)) {
            best = &other;
            bestDistSq = d;
        }
    }
    return best;
}

bool World::shieldBlocks(const Unit& target, const Vec3& incomingDir, Fixed chanceScale) {
    if (target.stats.shield.blockChance == Fixed::zero() || !target.alive() || target.airborne) return false;
    const Vec3 in = incomingDir.flat();
    const Fixed len = in.length();
    if (len == Fixed::zero()) return false;
    // The blow comes from the front if it travels against the way the target faces.
    const Fixed facingDot = -(target.facing.dot(in) / len);
    if (facingDot < target.stats.shield.arcCos) return false;
    return rng_.nextUnit() < target.stats.shield.blockChance * chanceScale;
}

void World::knock(Unit& target, const Vec3& dir, Fixed impulse) {
    const Fixed v = impulse / target.stats.mass;
    if (v <= Fixed::one()) return;
    target.vel += dir * v + up(v * kLiftFraction);
    target.airborne = true;
}

void World::fight(Unit& u, Unit& enemy) {
    const Vec3 delta = (enemy.pos - u.pos).flat();
    const Fixed dist = delta.length();
    if (dist > Fixed::zero()) u.facing = delta / dist;
    const MeleeWeapon& m = u.stats.melee;
    const Fixed gap = dist - u.stats.radius - enemy.stats.radius;
    if (gap > m.reach) {
        u.pos += u.facing * min(u.stats.moveSpeed * kTickDt, gap - m.reach);
        u.engaged = gap < m.reach + kSwoopDistance;
        return;
    }
    u.engaged = true;
    // Out of reach vertically (a flier still diving, or one above a ground unit).
    const Fixed dz = enemy.pos.z - u.pos.z;
    if (dz > u.stats.height + m.reach || -dz > enemy.stats.height + m.reach) return;
    if (u.meleeCooldown > 0) return;
    u.meleeCooldown = m.cooldownTicks;

    const Vec3 at = enemy.pos + up(enemy.stats.height * 3 / 5);
    if (shieldBlocks(enemy, u.facing, kMeleeBlockScale)) {
        impacts_.push_back({ImpactEvent::Kind::Blocked, at, u.facing, kNoEntity, enemy.id, false, {}});
        return;
    }
    int32_t pct = 100;
    const Fixed slope = slopeAt(u.pos.x, u.pos.y);
    if (slope <= kOpenSlope) pct += u.stats.terrain.openPct;
    if (slope >= kHillsSlope) pct += u.stats.terrain.hillsPct;
    if (enemy.stats.mounted) pct += m.bonusVsMountedPct;
    const bool killed = damage(enemy, m.damage * pct / 100);
    if (m.knockback > Fixed::zero()) knock(enemy, u.facing, m.knockback);
    impacts_.push_back({ImpactEvent::Kind::Hit, at, u.facing, kNoEntity, enemy.id, killed, {}});
}

bool World::tryHeal(Unit& u) {
    const Healing& h = u.stats.healing;
    if (h.amount == 0 || u.healCharges == 0) return false;
    Unit* best = nullptr;
    Fixed bestDistSq = kHealSeekRange * kHealSeekRange;
    for (Unit& other : units_) {
        if (!other.alive() || other.owner != u.owner || other.id == u.id || other.airborne) continue;
        if (other.hp > other.stats.maxHp - h.amount / 2) continue;
        const Fixed d = (other.pos - u.pos).flat().lengthSq();
        if (d < bestDistSq) {
            best = &other;
            bestDistSq = d;
        }
    }
    if (best == nullptr) return false;
    const Vec3 delta = (best->pos - u.pos).flat();
    const Fixed dist = delta.length();
    if (dist > Fixed::zero()) u.facing = delta / dist;
    const Fixed gap = dist - u.stats.radius - best->stats.radius;
    if (gap > h.range) {
        u.pos += u.facing * min(u.stats.moveSpeed * kTickDt, gap - h.range);
    } else if (u.healCooldown == 0) {
        best->hp = std::min(best->stats.maxHp, best->hp + h.amount);
        --u.healCharges;
        u.healCooldown = h.cooldownTicks;
        impacts_.push_back({ImpactEvent::Kind::Heal, best->pos + up(best->stats.height / 2), u.facing,
                            kNoEntity, best->id, false, {}});
    }
    return true;
}

bool World::damage(Unit& target, int32_t amount) {
    if (!target.alive()) return false;
    target.hp = std::max(target.hp - amount, 0);
    if (target.alive()) return false;
    target.hasMoveTarget = false;
    target.hasAttackTarget = false;
    return true;
}

void World::fire(Unit& u, const Vec3& target) {
    const RangedWeapon& w = u.stats.ranged;
    const Vec3 origin = u.pos + up(u.stats.height * kMuzzleHeightFraction);

    // Pick the aim point uniformly inside the dispersion circle around the target.
    const Fixed range = (target - origin).flat().length();
    const Fixed rankFactor = max(Fixed::one() - kDispersionPerRank * u.rank, kMinDispersionFactor);
    const Fixed radius = w.dispersionAt100m * range / 100 * rankFactor;
    Fixed dx, dy;
    do {
        dx = rng_.nextSigned();
        dy = rng_.nextSigned();
    } while (dx * dx + dy * dy >= Fixed::one());
    const Vec3 aim = target + Vec3{dx * radius, dy * radius, Fixed::zero()};

    Projectile p;
    p.id = nextId_++;
    p.shooter = u.id;
    p.owner = u.owner;
    p.kind = w.kind;
    p.pos = origin;
    p.vel = solveLowArc(origin, aim, w.muzzleSpeed, config_.gravity).velocity;
    p.damage = w.damage;
    p.explosive = w.explosive;
    p.fuse = w.explosive.fuseTicks;
    projectiles_.push_back(p);
}

bool World::bounce(Projectile& p, Fixed restitution) {
    if (p.resting) return true;
    p.pos += p.vel * kTickDt + up(-config_.gravity * (kTickDt * kTickDt / 2));
    p.vel.z -= config_.gravity * kTickDt;
    const Fixed ground = groundHeight(p.pos.x, p.pos.y);
    if (p.pos.z > ground) return false;
    p.pos.z = ground;
    if (p.vel.z < Fixed::zero()) p.vel.z = -p.vel.z * restitution;
    p.vel.x *= kGroundFriction;
    p.vel.y *= kGroundFriction;
    if (p.vel.lengthSq() < kRestSpeed * kRestSpeed) {
        p.vel = {};
        p.resting = true;
    }
    return p.resting;
}

void World::updateProjectiles() {
    const Vec3 accel = config_.wind + up(-config_.gravity);
    spawned_.clear();
    pendingExplosions_.clear();
    std::vector<uint8_t> removed(projectiles_.size(), 0);

    for (size_t i = 0; i < projectiles_.size(); ++i) {
        Projectile& p = projectiles_[i];
        ++p.ageTicks;

        if (p.kind == ProjectileKind::Bomb) {
            bounce(p, p.explosive.bounce);
            if (--p.fuse <= 0) {
                pendingExplosions_.push_back({p.pos, p.explosive, p.owner});
                removed[i] = 1;
            }
            continue;
        }

        if (p.kind == ProjectileKind::Debris) {
            // An arrow coming down hard sticks; everything else bounces to rest.
            const Fixed nextZ = p.pos.z + p.vel.z * kTickDt;
            if (p.remain == RemainKind::Arrow && p.vel.z < -kStickSpeed &&
                nextZ <= groundHeight(p.pos.x, p.pos.y)) {
                placeRemain(RemainKind::Arrow, p.owner, {p.pos.x, p.pos.y, groundHeight(p.pos.x, p.pos.y)},
                            normalized(p.vel));
                removed[i] = 1;
                continue;
            }
            const Vec3 travel = p.vel.flat();
            if (bounce(p, kDebrisRestitution) || p.ageTicks >= kMaxProjectileAgeTicks) {
                Vec3 lie = normalized(travel);
                if (lie.lengthSq() == Fixed::zero()) lie = {Fixed::one(), Fixed::zero(), Fixed::zero()};
                placeRemain(p.remain, p.owner, p.pos, lie);
                removed[i] = 1;
            }
            continue;
        }

        // Arrows and fireballs: ballistic flight, hitting the first thing in their path.
        const Vec3 prev = p.pos;
        // Exact for constant acceleration, so flight paths match the aiming solution.
        p.pos += p.vel * kTickDt + accel * (kTickDt * kTickDt / 2);
        p.vel += accel * kTickDt;
        const Vec3 seg = p.pos - prev;

        // Fraction of this tick's path at which the projectile reaches the
        // ground, treating the ground as linear along the path.
        const Fixed abovePrev = prev.z - groundHeight(prev.x, prev.y);
        const Fixed aboveNow = p.pos.z - groundHeight(p.pos.x, p.pos.y);
        Fixed groundT = Fixed::one() + Fixed::one();
        if (abovePrev <= Fixed::zero()) {
            groundT = Fixed::zero();
        } else if (aboveNow <= Fixed::zero()) {
            groundT = abovePrev / (abovePrev - aboveNow);
        }

        // Earliest unit whose cylinder the path passes through before the ground.
        Unit* hit = nullptr;
        Fixed hitT = groundT;
        const Vec3 segFlat = seg.flat();
        const Fixed segFlatLenSq = segFlat.lengthSq();
        for (Unit& u : units_) {
            if (!u.alive() || u.id == p.shooter) continue;
            Fixed t = Fixed::zero();
            if (segFlatLenSq != Fixed::zero()) {
                t = clamp((u.pos - prev).flat().dot(segFlat) / segFlatLenSq, Fixed::zero(),
                          Fixed::one());
            }
            if (t >= hitT) continue;
            const Vec3 closest = prev + seg * t;
            const Fixed r = u.stats.radius;
            if ((closest - u.pos).flat().lengthSq() > r * r) continue;
            if (closest.z < u.pos.z || closest.z > u.pos.z + u.stats.height) continue;
            hit = &u;
            hitT = t;
        }

        if (hit != nullptr) {
            const Vec3 at = prev + seg * hitT;
            if (p.kind == ProjectileKind::Fireball) {
                pendingExplosions_.push_back({at, p.explosive, p.owner});
            } else if (shieldBlocks(*hit, p.vel, Fixed::one())) {
                // Deflected: the arrow glances off and tumbles to the ground.
                impacts_.push_back({ImpactEvent::Kind::Blocked, at, p.vel, p.id, hit->id, false, {}});
                const Vec3 glance{-p.vel.x / 5, -p.vel.y / 5, abs(p.vel.z) / 5 + Fixed::fromInt(2)};
                spawnDebris(RemainKind::Arrow, p.owner, at, glance);
            } else {
                const bool killed = damage(*hit, p.damage);
                impacts_.push_back({ImpactEvent::Kind::Hit, at, p.vel, p.id, hit->id, killed, {}});
            }
            removed[i] = 1;
        } else if (groundT <= Fixed::one()) {
            const Vec3 at = prev + seg * groundT;
            if (p.kind == ProjectileKind::Fireball) {
                pendingExplosions_.push_back({at, p.explosive, p.owner});
            } else {
                placeRemain(RemainKind::Arrow, p.owner, at, normalized(p.vel));
                impacts_.push_back({ImpactEvent::Kind::Ground, at, p.vel, p.id, kNoEntity, false, {}});
            }
            removed[i] = 1;
        } else if (p.ageTicks >= kMaxProjectileAgeTicks) {
            removed[i] = 1;
        }
    }

    for (const PendingExplosion& e : pendingExplosions_) explode(e.at, e.ex, e.owner);

    size_t keep = 0;
    for (size_t i = 0; i < projectiles_.size(); ++i) {
        if (!removed[i]) projectiles_[keep++] = projectiles_[i];
    }
    projectiles_.resize(keep);
    projectiles_.insert(projectiles_.end(), spawned_.begin(), spawned_.end());
}

void World::explode(const Vec3& at, const Explosive& ex, PlayerId owner) {
    impacts_.push_back({ImpactEvent::Kind::Explosion, at, up(Fixed::one()), kNoEntity, kNoEntity, false, ex.radius});
    const auto awayFrom = [&](const Vec3& p) {
        Vec3 d = flatDirection(at, p, {});
        if (d.lengthSq() == Fixed::zero()) {
            const Fixed x = rng_.nextSigned();
            const Fixed y = rng_.nextSigned();
            d = normalized(Vec3{x, y, Fixed::zero()});
        }
        return d;
    };

    // Soldiers, living and dead: hurt, thrown, and torn apart.
    for (Unit& u : units_) {
        const Vec3 centre = u.pos + up(u.stats.height / 2);
        const Fixed d = (centre - at).length();
        if (d >= ex.radius) continue;
        const Fixed f = Fixed::one() - d / ex.radius;
        const Vec3 dir = awayFrom(u.pos);
        if (u.alive()) {
            const bool killed = damage(u, static_cast<int32_t>((f * ex.damage).floorToInt()));
            impacts_.push_back({ImpactEvent::Kind::Hit, centre, dir, kNoEntity, u.id, killed, {}});
            if (killed) spawnGibs(u, at, ex.impulse * f);
        }
        knock(u, dir, ex.impulse * f);
    }

    // Remains on the ground are thrown back into the air.
    for (uint32_t i = 0; i < remains_.size(); ++i) {
        Remain& r = remains_[i];
        if (r.kind == RemainKind::None) continue;
        const Fixed d = (r.pos - at).length();
        if (d >= ex.radius) continue;
        const Fixed f = Fixed::one() - d / ex.radius;
        const Fixed v = ex.impulse * f;
        const Fixed jx = rng_.nextSigned() * 2;
        const Fixed jy = rng_.nextSigned() * 2;
        const Fixed jz = rng_.nextUnit() * 2;
        const Vec3 jitter{jx, jy, jz};
        const Vec3 away = awayFrom(r.pos);
        spawnDebris(r.kind, r.owner, r.pos + up(Fixed::fromRatio(1, 10)), away * v + up(v) + jitter);
        r.kind = RemainKind::None;
        changedRemains_.push_back(i);
    }

    // Other bombs nearby are thrown and their fuses cut short: chain reactions.
    for (Projectile& p : projectiles_) {
        if (p.kind != ProjectileKind::Bomb || p.fuse <= 0) continue;
        const Fixed d = (p.pos - at).length();
        if (d >= ex.radius) continue;
        const Fixed v = ex.impulse * (Fixed::one() - d / ex.radius);
        p.vel += awayFrom(p.pos) * v + up(v * kLiftFraction);
        p.resting = false;
        p.fuse = std::min(p.fuse, 4 + static_cast<int32_t>(rng_.nextBelow(8)));
    }
    (void)owner;
}

void World::spawnGibs(const Unit& victim, const Vec3& from, Fixed strength) {
    const RemainKind parts[] = {RemainKind::Head, RemainKind::Arm, RemainKind::Leg};
    const Vec3 origin = victim.pos + up(victim.stats.height * 3 / 5);
    const Vec3 away = flatDirection(from, victim.pos, victim.facing);
    for (RemainKind part : parts) {
        const RemainKind kind = victim.stats.bleeds ? part : RemainKind::Bone;
        // One RNG draw per statement: the evaluation order of operands within
        // an expression is unspecified and differs between compilers.
        const Fixed outward = Fixed::fromRatio(1, 2) + rng_.nextUnit() / 2;
        const Fixed lift = Fixed::fromRatio(3, 5) + rng_.nextUnit() / 2;
        const Fixed sideX = rng_.nextSigned() * 3;
        const Fixed sideY = rng_.nextSigned() * 3;
        const Vec3 vel = away * (strength * outward) + up(strength * lift) + Vec3{sideX, sideY, Fixed::zero()};
        spawnDebris(kind, victim.owner, origin, vel);
    }
}

void World::spawnDebris(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& vel) {
    Projectile p;
    p.id = nextId_++;
    p.owner = owner;
    p.kind = ProjectileKind::Debris;
    p.remain = kind;
    p.pos = pos;
    p.vel = vel;
    spawned_.push_back(p);
}

void World::placeRemain(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& dir) {
    const uint32_t slot = static_cast<uint32_t>(remainsAdded_ % config_.maxRemains);
    const Remain r{kind, owner, pos, dir, tick_};
    if (remains_.size() < config_.maxRemains) {
        remains_.push_back(r);
    } else {
        remains_[slot] = r;
    }
    ++remainsAdded_;
    changedRemains_.push_back(slot);
}

uint64_t World::checksum() const {
    StateHasher h;
    h.addU64(tick_);
    h.addU64(nextId_);
    h.addU64(rng_.state());
    h.addU64(rng_.increment());
    h.addU64(units_.size());
    for (const Unit& u : units_) {
        h.addU64(u.id);
        h.addU64(u.owner);
        hashStats(h, u.stats);
        h.addI64(u.hp);
        h.addI64(u.rank);
        hashVec(h, u.pos);
        hashVec(h, u.facing);
        hashVec(h, u.vel);
        h.addU64(u.airborne);
        h.addU64(u.engaged);
        h.addU64(u.hasMoveTarget);
        hashVec(h, u.moveTarget);
        h.addU64(u.hasAttackTarget);
        hashVec(h, u.attackTarget);
        h.addI64(u.reloadRemaining);
        h.addI64(u.meleeCooldown);
        h.addI64(u.healCooldown);
        h.addI64(u.healCharges);
    }
    h.addU64(projectiles_.size());
    for (const Projectile& p : projectiles_) {
        h.addU64(p.id);
        h.addU64(p.shooter);
        h.addU64(p.owner);
        h.addU64(static_cast<uint64_t>(p.kind));
        h.addU64(static_cast<uint64_t>(p.remain));
        hashVec(h, p.pos);
        hashVec(h, p.vel);
        h.addI64(p.damage);
        h.addI64(p.ageTicks);
        h.addI64(p.fuse);
        h.addU64(p.resting);
        hashExplosive(h, p.explosive);
    }
    h.addU64(remainsAdded_);
    h.addU64(remains_.size());
    for (const Remain& r : remains_) {
        h.addU64(static_cast<uint64_t>(r.kind));
        if (r.kind == RemainKind::None) continue;
        h.addU64(r.owner);
        hashVec(h, r.pos);
        hashVec(h, r.dir);
        h.addU64(r.tick);
    }
    return h.value();
}

}  // namespace illyria::sim
