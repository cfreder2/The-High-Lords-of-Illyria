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

void hashVec(StateHasher& h, const Vec3& v) {
    h.addI64(v.x.raw);
    h.addI64(v.y.raw);
    h.addI64(v.z.raw);
}

// Unit-length horizontal direction from `from` to `to`, or `fallback` if they coincide.
Vec3 flatDirection(const Vec3& from, const Vec3& to, const Vec3& fallback) {
    const Vec3 d = (to - from).flat();
    const Fixed len = d.length();
    return len == Fixed::zero() ? fallback : d / len;
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
    u.pos = pos;
    if (config_.terrain) u.pos.z = groundHeight(pos.x, pos.y);
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

void World::step(std::span<const Command> commands) {
    impacts_.clear();

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

Fixed World::groundHeight(Fixed x, Fixed y) const {
    return config_.terrain ? config_.terrain->heightAt(x, y) : Fixed::zero();
}

void World::updateUnit(Unit& u) {
    if (!u.alive()) return;
    if (u.reloadRemaining > 0) --u.reloadRemaining;
    if (u.meleeCooldown > 0) --u.meleeCooldown;

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

    if (config_.terrain) u.pos.z = groundHeight(u.pos.x, u.pos.y);
}

Unit* World::nearestEnemy(const Unit& u, Fixed range) {
    if (range <= Fixed::zero()) return nullptr;
    Unit* best = nullptr;
    Fixed bestDistSq = range * range;
    // Ascending id order, strict comparison: ties go to the lowest id.
    for (Unit& other : units_) {
        if (!other.alive() || other.owner == u.owner) continue;
        const Fixed d = (other.pos - u.pos).flat().lengthSq();
        if (d <= bestDistSq && (best == nullptr || d < bestDistSq)) {
            best = &other;
            bestDistSq = d;
        }
    }
    return best;
}

void World::fight(Unit& u, Unit& enemy) {
    const Vec3 delta = (enemy.pos - u.pos).flat();
    const Fixed dist = delta.length();
    if (dist > Fixed::zero()) u.facing = delta / dist;
    const Fixed gap = dist - u.stats.radius - enemy.stats.radius;
    if (gap > u.stats.melee.reach) {
        u.pos += u.facing * min(u.stats.moveSpeed * kTickDt, gap - u.stats.melee.reach);
    } else if (u.meleeCooldown == 0) {
        const bool killed = damage(enemy, u.stats.melee.damage);
        u.meleeCooldown = u.stats.melee.cooldownTicks;
        const Vec3 at = enemy.pos + Vec3{Fixed::zero(), Fixed::zero(), enemy.stats.height * 3 / 5};
        impacts_.push_back({at, u.facing, kNoEntity, enemy.id, killed});
    }
}

bool World::damage(Unit& target, int32_t amount) {
    target.hp = std::max(target.hp - amount, 0);
    if (target.alive()) return false;
    target.hasMoveTarget = false;
    target.hasAttackTarget = false;
    return true;
}

void World::fire(Unit& u, const Vec3& target) {
    const RangedWeapon& w = u.stats.ranged;
    const Vec3 origin = u.pos + Vec3{Fixed::zero(), Fixed::zero(), u.stats.height * kMuzzleHeightFraction};

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
    p.pos = origin;
    p.vel = solveLowArc(origin, aim, w.muzzleSpeed, config_.gravity).velocity;
    p.damage = w.damage;
    projectiles_.push_back(p);
}

void World::updateProjectiles() {
    const Vec3 accel = config_.wind + Vec3{Fixed::zero(), Fixed::zero(), -config_.gravity};

    std::erase_if(projectiles_, [&](Projectile& p) {
        const Vec3 prev = p.pos;
        // Exact for constant acceleration, so flight paths match the aiming solution.
        p.pos += p.vel * kTickDt + accel * (kTickDt * kTickDt / 2);
        p.vel += accel * kTickDt;
        ++p.ageTicks;

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
            const bool killed = damage(*hit, p.damage);
            impacts_.push_back({prev + seg * hitT, p.vel, p.id, hit->id, killed});
            return true;
        }
        if (groundT <= Fixed::one()) {
            impacts_.push_back({prev + seg * groundT, p.vel, p.id, kNoEntity, false});
            return true;
        }
        return p.ageTicks >= kMaxProjectileAgeTicks;
    });
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
        h.addI64(u.stats.radius.raw);
        h.addI64(u.stats.height.raw);
        h.addI64(u.stats.moveSpeed.raw);
        h.addI64(u.stats.maxHp);
        h.addI64(u.stats.ranged.muzzleSpeed.raw);
        h.addI64(u.stats.ranged.reloadTicks);
        h.addI64(u.stats.ranged.damage);
        h.addI64(u.stats.ranged.dispersionAt100m.raw);
        h.addI64(u.stats.ranged.autoRange.raw);
        h.addI64(u.stats.melee.damage);
        h.addI64(u.stats.melee.reach.raw);
        h.addI64(u.stats.melee.cooldownTicks);
        h.addI64(u.stats.aggroRange.raw);
        h.addI64(u.hp);
        h.addI64(u.rank);
        hashVec(h, u.pos);
        hashVec(h, u.facing);
        h.addU64(u.hasMoveTarget);
        hashVec(h, u.moveTarget);
        h.addU64(u.hasAttackTarget);
        hashVec(h, u.attackTarget);
        h.addI64(u.reloadRemaining);
        h.addI64(u.meleeCooldown);
    }
    h.addU64(projectiles_.size());
    for (const Projectile& p : projectiles_) {
        h.addU64(p.id);
        h.addU64(p.shooter);
        hashVec(h, p.pos);
        hashVec(h, p.vel);
        h.addI64(p.damage);
        h.addI64(p.ageTicks);
    }
    return h.value();
}

}  // namespace illyria::sim
