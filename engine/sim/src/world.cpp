#include "illyria/sim/world.h"

#include <algorithm>

#include "illyria/sim/ballistics.h"
#include "illyria/sim/hash.h"
#include "illyria/sim/units.h"

namespace illyria::sim {

namespace {

constexpr int32_t kMaxProjectileAgeTicks = 30 * kTicksPerSecond;
// Arrows leave the bow at 80% of the archer's height.
constexpr Fixed kMuzzleHeightFraction = Fixed::fromRatio(4, 5);
// Each rank shrinks the dispersion circle by 15%, down to 40% of the base size.
constexpr Fixed kDispersionPerRank = Fixed::fromRatio(3, 20);
constexpr Fixed kMinDispersionFactor = Fixed::fromRatio(2, 5);
// Fliers climb and descend at these rates; they land this many cruise heights short of the goal.
constexpr Fixed kClimbRate = Fixed::fromInt(3);
constexpr Fixed kDescentRate = Fixed::fromRatio(5, 2);
constexpr int64_t kLandingGlide = 3;
// Waypoints count as reached within this distance.
constexpr Fixed kWaypointReach = Fixed::fromRatio(3, 2);
// Corpses are smaller obstacles than the living, and get shoved aside.
constexpr Fixed kCorpseRadiusScale = Fixed::fromRatio(3, 5);
// Bodies walking through loose remains kick them along.
constexpr Fixed kKickReach = Fixed::fromRatio(1, 4);
constexpr int kMaxKicksPerTick = 2;
// Slopes steeper than this slow walkers down even across them.
constexpr Fixed kSteepSlope = Fixed::fromRatio(7, 20);
constexpr Fixed kSteepSpeed = Fixed::fromRatio(4, 5);
// Climbing costs: speed divides by 1 + grade x this; going down is a little quicker.
constexpr Fixed kClimbCost = Fixed::fromRatio(5, 2);
constexpr Fixed kDescentGain = Fixed::fromRatio(1, 2);
constexpr Fixed kDescentMax = Fixed::fromRatio(3, 20);
// No one walks up or down a face steeper than 45 degrees: cliffs are walls.
constexpr Fixed kCliffGrade = Fixed::one();
// No one steps up a ledge higher than this, or down a drop deeper than this
// (a wall-walk's edge, the side of a stair).
constexpr Fixed kMaxStepUp = Fixed::fromRatio(9, 10);
constexpr Fixed kMaxStepDown = Fixed::fromRatio(3, 2);
// Soldiers this far below a wall's top are blocked by it; above, they walk its top.
constexpr Fixed kWallClear = Fixed::fromInt(2);
// Merlons stop this share of the missiles shot up at defenders on a wall-walk.
constexpr Fixed kMerlonCover = Fixed::fromRatio(1, 2);
// Veterancy. A soldier is promoted at these kill counts (regular, veteran,
// elite); a hero gains a level at these (levels 2 to 9).
constexpr int32_t kSoldierRanks[] = {2, 5, 10};
constexpr int32_t kHeroLevels[] = {3, 7, 12, 18, 25, 33, 42, 52};
// Each rank or blessing adds this much to a soldier's damage, and to the
// chance its shield turns a blow; a hero level adds less but also hit points.
constexpr int32_t kRankDamagePct = 10;
constexpr int32_t kHeroLevelDamagePct = 6;
constexpr Fixed kRankBlockBonus = Fixed::fromRatio(3, 100);
constexpr int32_t kHeroLevelHpPct = 8;
// A hero leads the soldiers near them: their Warlords strength (3 + level, at
// most 9) gives 1 to 3 leadership points, as in Warlords, each worth this much
// damage to friends within reach.
constexpr Fixed kLeadershipReach = Fixed::fromInt(30);
constexpr int32_t kLeadershipPct = 5;
// High ground: striking down the slope at a foe adds this much melee damage
// per unit of grade between them (a 1 in 3 slope: +20%), within limits; blows
// struck downhill also throw harder.
constexpr int32_t kHighGroundPctPerGrade = 60;
constexpr int32_t kHighGroundPctMax = 35;
constexpr int32_t kHighGroundPctMin = -25;
// Largest obstacle extent, for grid queries.
constexpr Fixed kMaxObstacleExtent = Fixed::fromInt(7);
// Ticks a thrown soldier needs to get back up.
constexpr int32_t kRecoverTicks = kTicksPerSecond * 2 / 3;
// Bodies thrown by explosions and blows.
constexpr Fixed kLiftFraction = Fixed::fromRatio(7, 10);
constexpr Fixed kGroundFriction = Fixed::fromRatio(11, 20);
constexpr Fixed kRestSpeed = Fixed::fromRatio(6, 5);
constexpr Fixed kDebrisRestitution = Fixed::fromRatio(1, 4);
// Flung body parts (see World::tumble): what they keep at each strike, how hard
// the ground drags them while skidding, and how far apart their blood marks fall.
constexpr Fixed kTumbleRestitution = Fixed::fromRatio(3, 10);
constexpr Fixed kTumbleKeep = Fixed::fromRatio(4, 5);
constexpr Fixed kStrikeSpeed = Fixed::fromRatio(3, 2);
constexpr Fixed kSkidDecel = Fixed::fromInt(7);
constexpr Fixed kTumbleRest = Fixed::fromRatio(1, 2);
constexpr Fixed kSmearSpacing = Fixed::fromRatio(9, 20);
// A satchel is a heavy bag: it bounces less and drags more than loose debris.
constexpr Fixed kBombFriction = Fixed::fromRatio(2, 5);
// Grenadiers throw short to allow for the bounce and roll.
constexpr Fixed kBombAimFraction = Fixed::fromRatio(7, 10);
// An arrow falling faster than this sticks into the ground; slower, it lies flat.
constexpr Fixed kStickSpeed = Fixed::fromInt(9);
// Melee blows are blocked less often than arrows: the attacker can work round the shield.
constexpr Fixed kMeleeBlockScale = Fixed::fromRatio(7, 10);
// Terrain bonus thresholds (rise over run).
constexpr Fixed kOpenSlope = Fixed::fromRatio(3, 50);
constexpr Fixed kHillsSlope = Fixed::fromRatio(3, 20);
// Healers look this far for wounded friends.
constexpr Fixed kHealSeekRange = Fixed::fromInt(25);

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
    h.addI64(s.ranged.maxRange.raw);
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
    h.addI64(s.flySpeed.raw);
    h.addI64(s.takeoffRun.raw);
    h.addI64(s.minFlight.raw);
    h.addI64(s.staminaTicks);
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

World::World(const WorldConfig& config) : config_(config), rng_(config.seed, 0) {
    if (config_.region) {
        // The region owns the terrain; share it without copying.
        config_.terrain = std::shared_ptr<const Terrain>(config_.region, &config_.region->terrain());
        nav_ = std::make_unique<NavGrid>(*config_.region);
        const auto& obstacles = config_.region->obstacles();
        for (uint32_t i = 0; i < obstacles.size(); ++i) {
            if (obstacles[i].solid) obstacleGrid_.insert(i, obstacles[i].pos.x, obstacles[i].pos.y);
        }
    }
}

EntityId World::spawnUnit(PlayerId owner, const Vec3& pos, const Vec3& facing,
                          const UnitStats& stats) {
    Unit u;
    u.id = nextId_++;
    u.owner = owner;
    u.stats = stats;
    u.hp = stats.maxHp;
    u.healCharges = stats.healing.charges;
    u.stamina = stats.staminaTicks;
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

Fixed World::groundHeight(Fixed x, Fixed y) const {
    const Fixed ground = config_.terrain ? config_.terrain->heightAt(x, y) : Fixed::zero();
    // On a bridge, the deck is the ground.
    Fixed deck;
    if (config_.region && !config_.region->decks().empty() && config_.region->deckAt(x, y, deck)) return max(ground, deck);
    return ground;
}

Fixed World::standingHeight(Fixed x, Fixed y, Fixed z) const {
    const Fixed ground = config_.terrain ? config_.terrain->heightAt(x, y) : Fixed::zero();
    Fixed deck;
    if (config_.region && !config_.region->decks().empty() && config_.region->deckBelow(x, y, z + kMaxStepUp, deck)) return max(ground, deck);
    return ground;
}

Ground World::groundAt(Fixed x, Fixed y) const {
    return config_.region ? config_.region->groundAt(x, y) : Ground::Plains;
}

bool World::isWater(Fixed x, Fixed y) const {
    const Ground g = groundAt(x, y);
    return g == Ground::Shallows || g == Ground::DeepWater;
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
    separateBodies();
    updateProjectiles();
    pathCache_.clear();
    ++tick_;
}

void World::applyCommand(const Command& cmd) {
    Unit* u = mutableUnit(cmd.unit);
    if (u == nullptr || !u->alive() || u->owner != cmd.player) return;
    switch (cmd.type) {
        case Command::Type::Move:
            u->hasMoveTarget = true;
            u->moveTarget = cmd.target;
            u->marchSpeed = max(Fixed::zero(), cmd.speed);
            u->hasAttackTarget = false;
            planPath(*u, cmd.target);
            break;
        case Command::Type::AttackGround:
            if (u->stats.ranged.muzzleSpeed == Fixed::zero()) return;
            u->hasAttackTarget = true;
            // "Attack ground" means the ground: never aim into or above the hillside.
            u->attackTarget = {cmd.target.x, cmd.target.y, groundHeight(cmd.target.x, cmd.target.y)};
            u->hasMoveTarget = false;
            break;
        case Command::Type::Stop:
            u->hasMoveTarget = false;
            u->hasAttackTarget = false;
            break;
    }
}

void World::planPath(Unit& u, const Vec3& to) {
    u.path.clear();
    u.pathIndex = 0;
    if (config_.region && !config_.region->ramparts().empty() && planRampartPath(u, to)) return;
    groundPath(u.pos, to, u.path);
}

void World::groundPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out) {
    if (!nav_ || nav_->clearLine(from, to)) {
        out.push_back(to);
        return;
    }
    // Units of a squad start and end in neighbouring cells; plan once per cell pair.
    const int64_t cs = nav_->cellSize();
    const auto cell = [cs](const Vec3& p) { return (p.x.floorToInt() / cs) * 4096 + p.y.floorToInt() / cs; };
    const auto key = std::make_pair(cell(from), cell(to));
    auto it = pathCache_.find(key);
    if (it == pathCache_.end()) {
        std::vector<Vec3> route;
        if (!nav_->findPath(from, to, route)) route = {to};
        it = pathCache_.emplace(key, std::move(route)).first;
    }
    out.insert(out.end(), it->second.begin(), it->second.end());
    out.back() = to;
}

namespace {
// Where a point lies along a closed ring of walk corners: edge index and
// distance from that edge's start.
struct RingPos {
    size_t edge = 0;
    Fixed along;
};
RingPos ringPos(const Rampart& rp, const Vec3& p) {
    RingPos best;
    Fixed bestD = Fixed::fromInt(1000000);
    const size_t n = rp.corners.size();
    for (size_t e = 0; e < n; ++e) {
        const Vec3 a = rp.corners[e], b = rp.corners[(e + 1) % n];
        const Vec3 ab = (b - a).flat();
        const Fixed len = ab.length();
        if (len == Fixed::zero()) continue;
        const Fixed t = clamp((p - a).flat().dot(ab) / len, Fixed::zero(), len);
        const Vec3 q = a + ab * (t / len);
        const Fixed d = (p - q).flat().length();
        if (d < bestD) {
            bestD = d;
            best = {e, t};
        }
    }
    return best;
}
// The point on the walk's centreline nearest p.
Vec3 onWalk(const Rampart& rp, const Vec3& p) {
    const RingPos rpos = ringPos(rp, p);
    const Vec3 a = rp.corners[rpos.edge], b = rp.corners[(rpos.edge + 1) % rp.corners.size()];
    const Fixed len = (b - a).flat().length();
    return len > Fixed::zero() ? a + (b - a) * (rpos.along / len) : a;
}
// The shorter way round the ring from one point to another: the corners passed.
std::vector<Vec3> ringRoute(const Rampart& rp, const Vec3& from, const Vec3& to, Fixed* length) {
    const size_t n = rp.corners.size();
    const RingPos a = ringPos(rp, from), b = ringPos(rp, to);
    std::vector<Fixed> lens(n);
    Fixed total;
    for (size_t e = 0; e < n; ++e) {
        lens[e] = (rp.corners[(e + 1) % n] - rp.corners[e]).flat().length();
        total += lens[e];
    }
    // Forward distance from a to b.
    Fixed fwd;
    if (a.edge == b.edge && b.along >= a.along) {
        fwd = b.along - a.along;
    } else {
        fwd = lens[a.edge] - a.along;
        for (size_t e = (a.edge + 1) % n; e != b.edge; e = (e + 1) % n) fwd += lens[e];
        fwd += b.along;
    }
    std::vector<Vec3> out;
    if (fwd <= total - fwd) {
        if (!(a.edge == b.edge && b.along >= a.along))
            for (size_t e = (a.edge + 1) % n;; e = (e + 1) % n) {
                out.push_back(rp.corners[e]);
                if (e == b.edge) break;
            }
        if (length) *length = fwd;
    } else {
        if (!(a.edge == b.edge && b.along <= a.along))
            for (size_t e = a.edge;; e = (e + n - 1) % n) {
                out.push_back(rp.corners[e]);
                if (e == (b.edge + 1) % n) break;
            }
        if (length) *length = total - fwd;
    }
    out.push_back(to);
    return out;
}
}  // namespace

bool World::planRampartPath(Unit& u, const Vec3& to) {
    // Up the stairs to a wall-walk, along it, and down again.
    const Region& rg = *config_.region;
    const int from = rg.rampartAt(u.pos.x, u.pos.y, u.pos.z);
    int target = -1;
    for (size_t i = 0; i < rg.ramparts().size() && target < 0; ++i) {
        const Fixed walk = rg.ramparts()[i].corners.front().z;
        if (rg.rampartAt(to.x, to.y, walk) == static_cast<int>(i)) target = static_cast<int>(i);
    }
    if (from < 0 && target < 0) return false;
    if (from >= 0 && target == from) {
        u.path = ringRoute(rg.ramparts()[static_cast<size_t>(from)], u.pos, to, nullptr);
        return true;
    }
    if (from >= 0) {
        // Down the nearest stair (along the walk, then to the ground target).
        const Rampart& rp = rg.ramparts()[static_cast<size_t>(from)];
        if (rp.stairs.empty()) return false;
        size_t best = 0;
        Fixed bestCost = Fixed::fromInt(1000000);
        for (size_t s = 0; s < rp.stairs.size(); ++s) {
            Fixed along;
            ringRoute(rp, u.pos, rp.stairs[s].second, &along);
            const Fixed cost = along + (rp.stairs[s].first - to).flat().length();
            if (cost < bestCost) {
                bestCost = cost;
                best = s;
            }
        }
        u.path = ringRoute(rp, u.pos, onWalk(rp, rp.stairs[best].second), nullptr);
        u.path.push_back(rp.stairs[best].second);
        u.path.push_back(rp.stairs[best].first);
        if (target >= 0) {
            const Rampart& tr = rg.ramparts()[static_cast<size_t>(target)];
            if (tr.stairs.empty()) return true;
            groundPath(rp.stairs[best].first, tr.stairs.front().first, u.path);
            u.path.push_back(tr.stairs.front().second);
            u.path.push_back(onWalk(tr, tr.stairs.front().second));
            const std::vector<Vec3> walk = ringRoute(tr, onWalk(tr, tr.stairs.front().second), to, nullptr);
            u.path.insert(u.path.end(), walk.begin(), walk.end());
            return true;
        }
        groundPath(rp.stairs[best].first, to, u.path);
        return true;
    }
    // From the ground: to the stair foot that makes the trip shortest, up, along.
    const Rampart& tr = rg.ramparts()[static_cast<size_t>(target)];
    if (tr.stairs.empty()) return false;
    size_t best = 0;
    Fixed bestCost = Fixed::fromInt(1000000);
    for (size_t s = 0; s < tr.stairs.size(); ++s) {
        Fixed along;
        ringRoute(tr, tr.stairs[s].second, to, &along);
        const Fixed cost = (tr.stairs[s].first - u.pos).flat().length() + along;
        if (cost < bestCost) {
            bestCost = cost;
            best = s;
        }
    }
    groundPath(u.pos, tr.stairs[best].first, u.path);
    u.path.push_back(tr.stairs[best].second);
    u.path.push_back(onWalk(tr, tr.stairs[best].second));
    const std::vector<Vec3> walk = ringRoute(tr, onWalk(tr, tr.stairs[best].second), to, nullptr);
    u.path.insert(u.path.end(), walk.begin(), walk.end());
    return true;
}

bool World::walk(Unit& u, const Vec3& to, Fixed speedScale) {
    const Vec3 delta = (to - u.pos).flat();
    const Fixed dist = delta.length();
    Fixed speed = u.stats.moveSpeed * speedScale * groundSpeed(groundAt(u.pos.x, u.pos.y));
    if (speed == Fixed::zero()) speed = u.stats.moveSpeed / 4;  // wading out of deep water
    if (slopeAt(u.pos.x, u.pos.y) > kSteepSlope) speed *= kSteepSpeed;
    if (dist == Fixed::zero()) return true;
    u.facing = delta / dist;
    // Uphill is slow going; downhill a little quicker.
    {
        const Fixed probe = Fixed::one();
        const Fixed here = standingHeight(u.pos.x, u.pos.y, u.pos.z);
        const Fixed grade = standingHeight(u.pos.x + u.facing.x * probe, u.pos.y + u.facing.y * probe, u.pos.z) - here;
        if (grade > Fixed::zero()) speed = speed / (Fixed::one() + grade * kClimbCost);
        else speed = speed * (Fixed::one() + min(kDescentMax, -grade * kDescentGain));
    }
    const Fixed stepLen = speed * kTickDt;
    Vec3 next = dist <= stepLen ? Vec3{to.x, to.y, u.pos.z} : u.pos + u.facing * stepLen;
    // Deep water and cliffs stop walkers: slide along if one axis is clear.
    const Fixed here = standingHeight(u.pos.x, u.pos.y, u.pos.z);
    const auto barred = [&](Fixed x, Fixed y) {
        const Ground g = groundAt(x, y);
        if (g == Ground::DeepWater && groundAt(u.pos.x, u.pos.y) != Ground::DeepWater) return true;
        // Over the water beside a narrow deck: off the edge is the river.
        Fixed deck;
        if (g == Ground::Bridge && config_.region && !config_.region->deckAt(x, y, deck) && config_.region->underWater(x, y)) return true;
        const Fixed run = (Vec3{x, y, Fixed::zero()} - u.pos).flat().length();
        const Fixed dh = standingHeight(x, y, u.pos.z) - here;
        if (dh > kMaxStepUp || dh < -kMaxStepDown) return true;  // a ledge: off a wall-walk, up the side of a stair
        // Onto or along a deck is never a cliff, whatever lies below its edges.
        if (g == Ground::Bridge || (config_.region && config_.region->deckBelow(x, y, u.pos.z + kMaxStepUp, deck))) return false;
        return run > Fixed::zero() && abs(dh) > run * kCliffGrade && slopeAt(x, y) > kCliffGrade;
    };
    if (barred(next.x, next.y)) {
        if (!barred(next.x, u.pos.y)) next.y = u.pos.y;
        else if (!barred(u.pos.x, next.y)) next.x = u.pos.x;
        else return false;
    }
    const Vec3 moved = (next - u.pos).flat();
    u.pos = next;
    if (moved.lengthSq() > Fixed::zero()) kickRemains(u, u.facing);
    return dist <= stepLen;
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

    const bool flier = u.stats.flyAltitude > Fixed::zero();
    if (flier && u.flight != Unit::Flight::Grounded) {
        updateFlight(u);
        return;
    }
    // Resting on the ground restores a flier at half the rate flying spends it.
    if (flier && u.stamina < u.stats.staminaTicks && (tick_ & 1) == 0) ++u.stamina;

    if (u.hasMoveTarget) {
        // Orders take priority over fighting: a moving unit ignores enemies.
        const Fixed remaining = (u.moveTarget - u.pos).flat().length();
        if (flier && remaining >= u.stats.minFlight && u.stamina * 2 >= u.stats.staminaTicks) {
            u.flight = Unit::Flight::TakingOff;
            u.runway = Fixed::zero();
            updateFlight(u);
            return;
        }
        if (u.path.empty()) u.path.push_back(u.moveTarget);
        const Vec3& waypoint = u.path[std::min<size_t>(u.pathIndex, u.path.size() - 1)];
        const bool last = u.pathIndex + 1 >= u.path.size();
        // A squad marches at its slowest member's pace, keeping its formation.
        const Fixed pace = u.marchSpeed > Fixed::zero() && u.marchSpeed < u.stats.moveSpeed ? u.marchSpeed / u.stats.moveSpeed : Fixed::one();
        const bool arrived = walk(u, waypoint, pace);
        if (!last && (arrived || (waypoint - u.pos).flat().lengthSq() < kWaypointReach * kWaypointReach)) {
            ++u.pathIndex;
        } else if (last && arrived) {
            u.hasMoveTarget = false;
            u.path.clear();
        }
    } else if (u.hasAttackTarget) {
        u.facing = flatDirection(u.pos, u.attackTarget, u.facing);
        if ((u.attackTarget - u.pos).flat().length() > u.stats.ranged.maxRange) {
            walk(u, u.attackTarget);  // close to range before loosing
        } else if (u.reloadRemaining == 0) {
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
    u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
}

void World::updateFlight(Unit& u) {
    const Vec3 goal = u.hasMoveTarget ? u.moveTarget : u.pos;
    const Vec3 delta = (goal - u.pos).flat();
    const Fixed dist = delta.length();
    if (dist > Fixed::zero()) u.facing = delta / dist;
    const Fixed ground = groundHeight(u.pos.x, u.pos.y);
    switch (u.flight) {
        case Unit::Flight::Grounded:
            break;
        case Unit::Flight::TakingOff: {
            // A ground run, faster than a walk, before the wings can lift.
            const Vec3 before = u.pos;
            walk(u, u.pos + u.facing * Fixed::fromInt(50), Fixed::fromRatio(3, 2));
            u.runway += (u.pos - before).flat().length();
            u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
            if (u.runway >= u.stats.takeoffRun) u.flight = Unit::Flight::Flying;
            break;
        }
        case Unit::Flight::Flying: {
            u.pos += u.facing * min(u.stats.flySpeed * kTickDt, dist);
            const Fixed below = groundHeight(u.pos.x, u.pos.y);
            u.pos.z = min(u.pos.z + kClimbRate * kTickDt, below + u.stats.flyAltitude);
            u.pos.z = max(u.pos.z, below);
            --u.stamina;
            if (u.stamina <= 0 || dist < u.stats.flyAltitude * kLandingGlide || !u.hasMoveTarget) {
                u.flight = Unit::Flight::Landing;
            }
            break;
        }
        case Unit::Flight::Landing: {
            u.pos += u.facing * min(u.stats.flySpeed / 2 * kTickDt, dist);
            const Fixed below = groundHeight(u.pos.x, u.pos.y);
            u.pos.z = max(u.pos.z - kDescentRate * kTickDt, below);
            if (u.stamina > 0) --u.stamina;
            if (u.pos.z <= below) {
                u.flight = Unit::Flight::Grounded;
                // Walk the rest of the way (a tired flier may have come down early).
                if (u.hasMoveTarget) planPath(u, u.moveTarget);
            }
            break;
        }
    }
    (void)ground;
}

void World::separateBodies() {
    // Bodies have size and mass: living soldiers push each other apart (the
    // lighter gives way), shove corpses aside, and are kept out of scenery.
    unitGrid_.clear();
    for (uint32_t i = 0; i < units_.size(); ++i) {
        const Unit& u = units_[i];
        if (!u.inAir() && u.flight == Unit::Flight::Grounded) unitGrid_.insert(i, u.pos.x, u.pos.y);
    }
    const Fixed maxRadius = Fixed::fromInt(3);
    for (uint32_t i = 0; i < units_.size(); ++i) {
        Unit& a = units_[i];
        if (!a.alive() || a.inAir()) continue;
        unitGrid_.query(a.pos.x, a.pos.y, a.stats.radius + maxRadius, [&](uint32_t j) {
            if (j == i) return;
            Unit& b = units_[j];
            if (b.inAir() || (b.alive() && j < i)) return;  // each living pair once
            const Fixed rb = b.alive() ? b.stats.radius : b.stats.radius * kCorpseRadiusScale;
            const Vec3 d = (b.pos - a.pos).flat();
            const Fixed reach = a.stats.radius + rb;
            const Fixed distSq = d.lengthSq();
            if (distSq >= reach * reach) return;
            Fixed dist = sqrt(distSq);
            Vec3 n = dist > Fixed::zero() ? d / dist : Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()};
            const Fixed overlap = reach - dist;
            if (!b.alive()) {
                b.pos += n * overlap;  // the dead give way entirely
                b.pos.z = standingHeight(b.pos.x, b.pos.y, b.pos.z);
                return;
            }
            const Fixed total = a.stats.mass + b.stats.mass;
            a.pos -= n * (overlap * b.stats.mass / total);
            b.pos += n * (overlap * a.stats.mass / total);
        });
    }
    if (!config_.region) return;
    const auto& obstacles = config_.region->obstacles();
    for (Unit& u : units_) {
        if (u.inAir() || u.flight != Unit::Flight::Grounded) continue;
        obstacleGrid_.query(u.pos.x, u.pos.y, u.stats.radius + kMaxObstacleExtent, [&](uint32_t k) {
            const Obstacle& o = obstacles[k];
            if (o.kind == Obstacle::Kind::House) {
                // Push out of the footprint along the shallower side.
                const Fixed cx = clamp(u.pos.x, o.pos.x - o.halfX, o.pos.x + o.halfX);
                const Fixed cy = clamp(u.pos.y, o.pos.y - o.halfY, o.pos.y + o.halfY);
                const Vec3 d{u.pos.x - cx, u.pos.y - cy, Fixed::zero()};
                const Fixed distSq = d.lengthSq();
                if (distSq >= u.stats.radius * u.stats.radius) return;
                if (distSq > Fixed::zero()) {
                    const Fixed dist = sqrt(distSq);
                    u.pos += d / dist * (u.stats.radius - dist);
                } else {
                    const Fixed px = o.halfX - abs(u.pos.x - o.pos.x), py = o.halfY - abs(u.pos.y - o.pos.y);
                    if (px < py) u.pos.x += (u.pos.x < o.pos.x ? -(px + u.stats.radius) : px + u.stats.radius);
                    else u.pos.y += (u.pos.y < o.pos.y ? -(py + u.stats.radius) : py + u.stats.radius);
                }
                return;
            }
            if (o.passAbove > Fixed::zero() && u.pos.z >= o.pos.z + o.passAbove) return;  // on the wall-walk
            const Vec3 d = (u.pos - o.pos).flat();
            const Fixed reach = u.stats.radius + o.radius;
            const Fixed distSq = d.lengthSq();
            if (distSq >= reach * reach) return;
            const Fixed dist = sqrt(distSq);
            const Vec3 n = dist > Fixed::zero() ? d / dist : Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()};
            u.pos += n * (reach - dist);
        });
        // Castle walls: out of the slab, unless walking along its top.
        for (const WallSeg& wl : config_.region->walls()) {
            if (u.pos.z > wl.top - kWallClear) continue;
            const Fixed reach = wl.halfThick + u.stats.radius;
            if (u.pos.x < min(wl.a.x, wl.b.x) - reach || u.pos.x > max(wl.a.x, wl.b.x) + reach ||
                u.pos.y < min(wl.a.y, wl.b.y) - reach || u.pos.y > max(wl.a.y, wl.b.y) + reach)
                continue;
            const Vec3 ab = (wl.b - wl.a).flat();
            const Fixed len2 = ab.lengthSq();
            const Fixed t = len2 > Fixed::zero() ? clamp((u.pos - wl.a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
            const Vec3 p = wl.a + ab * t;
            const Vec3 d = (u.pos - p).flat();
            const Fixed distSq = d.lengthSq();
            if (distSq >= reach * reach) continue;
            const Fixed dist = sqrt(distSq);
            const Vec3 n = dist > Fixed::zero() ? d / dist : Vec3{-ab.y, ab.x, Fixed::zero()} / sqrt(len2);
            u.pos += n * (reach - dist);
        }
    }
    for (Unit& u : units_) {
        if (!u.inAir() && u.flight == Unit::Flight::Grounded) u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
    }
}

void World::kickRemains(Unit& u, const Vec3& moveDir) {
    const Fixed reach = u.stats.radius + kKickReach;
    // Collect first: kicking edits the grid being searched.
    std::vector<uint32_t> touched;
    remainGrid_.query(u.pos.x, u.pos.y, reach, [&](uint32_t slot) {
        if (touched.size() >= static_cast<size_t>(kMaxKicksPerTick)) return;
        const Remain& r = remains_[slot];
        // Arrows stay where they fell, stuck or lying (only explosions move
        // them); body parts get nudged along.
        if (r.kind == RemainKind::None || r.kind == RemainKind::Arrow) return;
        if ((r.pos - u.pos).flat().lengthSq() > reach * reach) return;
        touched.push_back(slot);
    });
    for (uint32_t slot : touched) {
        const Remain& r = remains_[slot];
        // A nudge and a short roll, not a launch.
        const Fixed push = u.stats.moveSpeed * (Fixed::fromRatio(1, 4) + rng_.nextUnit() / 4) * min(u.stats.mass, Fixed::fromInt(2));
        const Fixed side = rng_.nextSigned() / 2;
        const Fixed lift = Fixed::fromRatio(3, 10) + rng_.nextUnit() * 3 / 10;
        const Vec3 vel = moveDir * push + Vec3{-moveDir.y * side, moveDir.x * side, lift};
        spawnDebris(r.kind, r.owner, r.pos + up(Fixed::fromRatio(1, 10)), vel);
        clearRemain(slot);
    }
}

bool World::onRampartFacing(const Unit& u, const Vec3& vel) const {
    if (!config_.region || config_.region->ramparts().empty()) return false;
    const int ri = config_.region->rampartAt(u.pos.x, u.pos.y, u.pos.z);
    if (ri < 0) return false;
    // Coming in from outside the ring.
    return vel.flat().dot((config_.region->ramparts()[static_cast<size_t>(ri)].centre - u.pos).flat()) > Fixed::zero();
}

Fixed World::obstacleHit(const Vec3& a, const Vec3& b, Fixed limitT) const {
    if (!config_.region) return -Fixed::one();
    const auto& obstacles = config_.region->obstacles();
    const Vec3 seg = b - a;
    const Vec3 segFlat = seg.flat();
    const Fixed lenSq = segFlat.lengthSq();
    const Vec3 mid = a + seg * Fixed::fromRatio(1, 2);
    Fixed best = limitT;
    bool hit = false;
    obstacleGrid_.query(mid.x, mid.y, sqrt(lenSq) / 2 + kMaxObstacleExtent, [&](uint32_t k) {
        const Obstacle& o = obstacles[k];
        // Trees catch missiles in their crowns as well as their trunks.
        Fixed radius = o.radius;
        if (o.kind == Obstacle::Kind::Pine) radius = Fixed::fromRatio(9, 10);
        if (o.kind == Obstacle::Kind::Oak) radius = Fixed::fromRatio(6, 5);
        if (o.kind == Obstacle::Kind::House) radius = min(o.halfX, o.halfY) + Fixed::fromRatio(1, 2);
        Fixed t;
        if (lenSq > Fixed::zero()) t = clamp((o.pos - a).flat().dot(segFlat) / lenSq, Fixed::zero(), Fixed::one());
        if (t >= best) return;
        const Vec3 p = a + seg * t;
        if ((p - o.pos).flat().lengthSq() > radius * radius) return;
        if (p.z < o.pos.z || p.z > o.pos.z + o.height) return;
        best = t;
        hit = true;
    });
    // Castle walls: the first of a few points along the path inside a slab, below its top.
    for (const WallSeg& wl : config_.region->walls()) {
        const Fixed pad = wl.halfThick + Fixed::fromInt(2);
        if (max(a.x, b.x) < min(wl.a.x, wl.b.x) - pad || min(a.x, b.x) > max(wl.a.x, wl.b.x) + pad ||
            max(a.y, b.y) < min(wl.a.y, wl.b.y) - pad || min(a.y, b.y) > max(wl.a.y, wl.b.y) + pad)
            continue;
        const Vec3 ab = (wl.b - wl.a).flat();
        const Fixed len2 = ab.lengthSq();
        for (int k = 1; k <= 4; ++k) {
            const Fixed t = Fixed::fromRatio(k, 4);
            if (t >= best) break;
            const Vec3 p = a + seg * t;
            const Fixed solid = wl.walk > Fixed::zero() ? wl.walk : wl.top - Fixed::fromRatio(3, 10);
            if (p.z > solid || p.z < wl.a.z) continue;
            const Fixed u = len2 > Fixed::zero() ? clamp((p - wl.a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
            if ((p - (wl.a + ab * u)).flat().lengthSq() > wl.halfThick * wl.halfThick) continue;
            best = t;
            hit = true;
            break;
        }
    }
    return hit ? best : -Fixed::one();
}

void World::clearRemain(uint32_t slot) {
    Remain& r = remains_[slot];
    if (r.kind == RemainKind::None) return;
    remainGrid_.remove(slot, r.pos.x, r.pos.y);
    r.kind = RemainKind::None;
    changedRemains_.push_back(slot);
}

void World::SpatialHash::remove(uint32_t index, Fixed x, Fixed y) {
    auto it = buckets_.find(key(cellOf(x), cellOf(y)));
    if (it == buckets_.end()) return;
    auto& v = it->second;
    auto pos = std::find(v.begin(), v.end(), index);
    if (pos != v.end()) v.erase(pos);
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
    const Fixed ground = standingHeight(u.pos.x, u.pos.y, u.pos.z);
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
    // Melee-only units ignore enemies flying above their reach (giants can swat low fliers).
    const bool groundBound = u.stats.ranged.muzzleSpeed == Fixed::zero();
    const Fixed reachUp = u.stats.height + u.stats.melee.reach;
    Unit* best = nullptr;
    Fixed bestDistSq = range * range;
    // Ascending id order, strict comparison: ties go to the lowest id.
    for (Unit& other : units_) {
        if (!other.alive() || other.owner == u.owner) continue;
        if (groundBound && other.pos.z - u.pos.z > reachUp) continue;
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
    return rng_.nextUnit() < (target.stats.shield.blockChance + kRankBlockBonus * (target.rank + target.blessings)) * chanceScale;
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
        walk(u, u.pos + u.facing * min(gap - m.reach, u.stats.moveSpeed));
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
    // The high ground: striking down at a foe below is easier than reaching up.
    if (dist > Fixed::zero()) {
        const int32_t downhill = static_cast<int32_t>((-dz / dist * kHighGroundPctPerGrade).floorToInt());
        pct += std::clamp(downhill, kHighGroundPctMin, kHighGroundPctMax);
    }
    pct += prowessPct(u) - 100;
    const bool killed = damage(enemy, m.damage * std::max<int32_t>(pct, 10) / 100, &u);
    if (m.knockback > Fixed::zero()) {
        const Fixed downhill = dist > Fixed::zero() ? clamp(-dz / dist, Fixed::zero(), Fixed::fromRatio(1, 2)) : Fixed::zero();
        knock(enemy, u.facing, m.knockback * (Fixed::one() + downhill));
    }
    impacts_.push_back({ImpactEvent::Kind::Hit, at, u.facing, kNoEntity, enemy.id, killed, {}});
}

bool World::tryHeal(Unit& u) {
    const Healing& h = u.stats.healing;
    if (h.amount == 0 || u.healCharges == 0) return false;
    Unit* best = nullptr;
    Fixed bestDistSq = kHealSeekRange * kHealSeekRange;
    for (Unit& other : units_) {
        if (!other.alive() || other.owner != u.owner || other.id == u.id || other.inAir()) continue;
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
        walk(u, u.pos + u.facing * min(gap - h.range, u.stats.moveSpeed));
    } else if (u.healCooldown == 0) {
        best->hp = std::min(best->stats.maxHp, best->hp + h.amount);
        --u.healCharges;
        u.healCooldown = h.cooldownTicks;
        impacts_.push_back({ImpactEvent::Kind::Heal, best->pos + up(best->stats.height / 2), u.facing,
                            kNoEntity, best->id, false, {}});
    }
    return true;
}

bool World::damage(Unit& target, int32_t amount, Unit* attacker) {
    if (!target.alive()) return false;
    target.hp = std::max(target.hp - amount, 0);
    if (target.alive()) return false;
    target.hasMoveTarget = false;
    target.hasAttackTarget = false;
    if (attacker != nullptr && attacker->owner != target.owner) creditKill(*attacker);
    return true;
}

void World::setVeterancy(EntityId id, int32_t rank, int32_t blessings) {
    Unit* u = mutableUnit(id);
    if (!u) return;
    u->rank = std::max<int32_t>(0, rank);
    u->blessings = std::max<int32_t>(0, blessings);
    if (u->stats.type == static_cast<uint8_t>(UnitType::Hero)) {
        const int32_t gain = unitStats(UnitType::Hero).maxHp * kHeroLevelHpPct / 100 * u->rank;
        u->stats.maxHp += gain;
        u->hp += gain;
    }
}

void World::creditKill(Unit& killer) {
    ++killer.kills;
    if (killer.stats.type == static_cast<uint8_t>(UnitType::Hero)) {
        while (killer.rank < 8 && killer.kills >= kHeroLevels[killer.rank]) {
            ++killer.rank;
            // Tougher with every level, and the new strength shows at once.
            const int32_t gain = unitStats(UnitType::Hero).maxHp * kHeroLevelHpPct / 100;
            killer.stats.maxHp += gain;
            if (killer.alive()) killer.hp += gain;
        }
        return;
    }
    while (killer.rank < 3 && killer.kills >= kSoldierRanks[killer.rank]) ++killer.rank;
}

int32_t World::prowessPct(const Unit& u) const {
    const bool hero = u.stats.type == static_cast<uint8_t>(UnitType::Hero);
    int32_t pct = 100 + u.rank * (hero ? kHeroLevelDamagePct : kRankDamagePct) + u.blessings * kRankDamagePct;
    // The best hero near them leads them.
    int32_t lead = 0;
    for (const Unit& h : units_) {
        if (h.owner != u.owner || !h.alive() || h.stats.type != static_cast<uint8_t>(UnitType::Hero) || h.id == u.id) continue;
        if ((h.pos - u.pos).flat().lengthSq() > kLeadershipReach * kLeadershipReach) continue;
        const int32_t strength = std::min(9, 4 + h.rank);  // level + 3
        lead = std::max(lead, strength >= 9 ? 3 : strength >= 7 ? 2 : strength >= 4 ? 1 : 0);
    }
    return pct + lead * kLeadershipPct;
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
    Vec3 aim = target + Vec3{dx * radius, dy * radius, Fixed::zero()};
    if (w.kind == ProjectileKind::Bomb) {
        aim = origin + (aim - origin).flat() * kBombAimFraction;
        aim.z = groundHeight(aim.x, aim.y);
    }

    Projectile p;
    p.id = nextId_++;
    p.shooter = u.id;
    p.owner = u.owner;
    p.kind = w.kind;
    p.pos = origin;
    p.vel = solveLowArc(origin, aim, w.muzzleSpeed, config_.gravity).velocity;
    p.damage = w.damage * prowessPct(u) / 100;
    p.explosive = w.explosive;
    p.fuse = w.explosive.fuseTicks;
    projectiles_.push_back(p);
}

bool World::bounce(Projectile& p, Fixed restitution, Fixed friction) {
    if (p.resting) return true;
    const Vec3 before = p.pos;
    p.pos += p.vel * kTickDt + up(-config_.gravity * (kTickDt * kTickDt / 2));
    p.vel.z -= config_.gravity * kTickDt;
    // Rocks, trees and walls send bouncing things back the way they came.
    if (obstacleHit(before, p.pos, Fixed::one()) >= Fixed::zero()) {
        p.pos.x = before.x;
        p.pos.y = before.y;
        p.vel.x = -p.vel.x * restitution;
        p.vel.y = -p.vel.y * restitution;
    }
    const Fixed ground = groundHeight(p.pos.x, p.pos.y);
    if (p.pos.z > ground) return false;
    p.pos.z = ground;
    if (p.vel.z < Fixed::zero()) p.vel.z = -p.vel.z * restitution;
    p.vel.x *= friction;
    p.vel.y *= friction;
    if (p.vel.lengthSq() < kRestSpeed * kRestSpeed) {
        p.vel = {};
        p.resting = true;
    }
    return p.resting;
}

bool World::tumble(Projectile& p) {
    if (p.resting) return true;
    const Vec3 before = p.pos;
    p.pos += p.vel * kTickDt + up(-config_.gravity * (kTickDt * kTickDt / 2));
    p.vel.z -= config_.gravity * kTickDt;
    if (obstacleHit(before, p.pos, Fixed::one()) >= Fixed::zero()) {
        p.pos.x = before.x;
        p.pos.y = before.y;
        p.vel.x = -p.vel.x * kTumbleRestitution;
        p.vel.y = -p.vel.y * kTumbleRestitution;
    }
    const Fixed ground = groundHeight(p.pos.x, p.pos.y);
    if (p.pos.z > ground) return false;
    p.pos.z = ground;
    const Vec3 travel = p.vel.flat();
    const auto bleed = [&](Fixed size) {
        if (!p.bleeding || isWater(p.pos.x, p.pos.y)) return;
        impacts_.push_back({ImpactEvent::Kind::Bleed, p.pos, normalized(travel), p.id, kNoEntity, false, size});
    };
    if (p.vel.z < -kStrikeSpeed) {
        // A strike: it hops on, keeping most of its speed; blood where it hit.
        const Fixed speed = -p.vel.z;
        p.vel.z = speed * kTumbleRestitution;
        p.vel.x *= kTumbleKeep;
        p.vel.y *= kTumbleKeep;
        bleed(clamp(speed * Fixed::fromRatio(1, 15), Fixed::fromRatio(1, 6), Fixed::fromRatio(1, 2)));
        p.smear = Fixed::zero();
    } else {
        // Skidding: the ground drags it to a stop, smearing as it goes.
        p.vel.z = Fixed::zero();
        const Fixed speed = travel.length();
        const Fixed slower = max(Fixed::zero(), speed - kSkidDecel * kTickDt);
        if (speed > Fixed::zero()) {
            p.vel.x = p.vel.x * slower / speed;
            p.vel.y = p.vel.y * slower / speed;
        }
        p.smear += speed * kTickDt;
        if (p.smear >= kSmearSpacing) {
            bleed(Fixed::fromRatio(1, 10) + min(speed, Fixed::fromInt(6)) / 50);
            p.smear = Fixed::zero();
        }
    }
    if (p.vel.lengthSq() < kTumbleRest * kTumbleRest) {
        p.vel = {};
        p.resting = true;
    }
    return p.resting;
}

void World::updateProjectiles() {
    const Vec3 accel = config_.wind + up(-config_.gravity);
    // spawned_ may already hold debris kicked up by walkers this tick.
    pendingExplosions_.clear();
    std::vector<uint8_t> removed(projectiles_.size(), 0);

    for (size_t i = 0; i < projectiles_.size(); ++i) {
        Projectile& p = projectiles_[i];
        ++p.ageTicks;

        if (p.kind == ProjectileKind::Bomb) {
            bounce(p, p.explosive.bounce, kBombFriction);
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
            if (tumble(p) || p.ageTicks >= kMaxProjectileAgeTicks) {
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

        // Trees, rocks and walls in the way stop it first.
        const Fixed obstacleT = obstacleHit(prev, p.pos, hitT);
        if (obstacleT >= Fixed::zero()) {
            const Vec3 at = prev + seg * obstacleT;
            if (p.kind == ProjectileKind::Fireball) {
                pendingExplosions_.push_back({at, p.explosive, p.owner});
            } else {
                placeRemain(RemainKind::Arrow, p.owner, at, normalized(p.vel));  // stuck in the wood
                impacts_.push_back({ImpactEvent::Kind::Ground, at, p.vel, p.id, kNoEntity, false, {}});
            }
            removed[i] = 1;
        } else if (hit != nullptr) {
            const Vec3 at = prev + seg * hitT;
            if (p.kind == ProjectileKind::Fireball) {
                pendingExplosions_.push_back({at, p.explosive, p.owner});
            } else if (onRampartFacing(*hit, p.vel) && rng_.nextUnit() < kMerlonCover) {
                // Behind the merlons: the missile strikes stone instead.
                impacts_.push_back({ImpactEvent::Kind::Blocked, at, p.vel, p.id, hit->id, false, {}});
                const Vec3 glance{-p.vel.x / 6, -p.vel.y / 6, Fixed::fromInt(1)};
                spawnDebris(RemainKind::Arrow, p.owner, at, glance);
            } else if (shieldBlocks(*hit, p.vel, Fixed::one())) {
                // Deflected: the arrow glances off and tumbles to the ground.
                impacts_.push_back({ImpactEvent::Kind::Blocked, at, p.vel, p.id, hit->id, false, {}});
                const Vec3 glance{-p.vel.x / 5, -p.vel.y / 5, abs(p.vel.z) / 5 + Fixed::fromInt(2)};
                spawnDebris(RemainKind::Arrow, p.owner, at, glance);
            } else {
                const bool killed = damage(*hit, p.damage, mutableUnit(p.shooter));
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
    spawned_.clear();
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
    std::vector<uint32_t> nearby;
    remainGrid_.query(at.x, at.y, ex.radius, [&](uint32_t slot) { nearby.push_back(slot); });
    for (uint32_t i : nearby) {
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
        clearRemain(i);
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

void World::spawnGibs(Unit& victim, const Vec3& from, Fixed strength) {
    // Torn apart: head, torso, both arms and legs, and gobbets of flesh.
    victim.gibbed = true;
    const RemainKind parts[] = {RemainKind::Head, RemainKind::Torso, RemainKind::Arm, RemainKind::Arm,
                                RemainKind::Leg,  RemainKind::Leg,   RemainKind::Chunk, RemainKind::Chunk};
    const Vec3 origin = victim.pos + up(victim.stats.height * 3 / 5);
    const Vec3 away = flatDirection(from, victim.pos, victim.facing);
    for (RemainKind part : parts) {
        const RemainKind kind = victim.stats.bleeds ? part : RemainKind::Bone;
        // One RNG draw per statement: the evaluation order of operands within
        // an expression is unspecified and differs between compilers.
        // Flung long and low, so they strike, hop and skid away from the blast.
        const Fixed outward = Fixed::fromRatio(9, 10) + rng_.nextUnit() * 3 / 5;
        const Fixed lift = Fixed::fromRatio(7, 20) + rng_.nextUnit() * 7 / 20;
        const Fixed sideX = rng_.nextSigned() * 3;
        const Fixed sideY = rng_.nextSigned() * 3;
        const Fixed heft = part == RemainKind::Torso ? Fixed::fromRatio(3, 5) : Fixed::one();  // the trunk flies less far
        const Vec3 vel = away * (strength * outward * heft) + up(strength * lift * heft) + Vec3{sideX, sideY, Fixed::zero()};
        spawnDebris(kind, victim.owner, origin, vel);
        spawned_.back().bleeding = victim.stats.bleeds;
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
    // Whatever falls in the water sinks out of sight.
    if (isWater(pos.x, pos.y) && pos.z <= groundHeight(pos.x, pos.y) + Fixed::fromRatio(1, 2)) return;
    const uint32_t slot = static_cast<uint32_t>(remainsAdded_ % config_.maxRemains);
    const Remain r{kind, owner, pos, dir, tick_};
    if (remains_.size() < config_.maxRemains) {
        remains_.push_back(r);
    } else {
        if (remains_[slot].kind != RemainKind::None) remainGrid_.remove(slot, remains_[slot].pos.x, remains_[slot].pos.y);
        remains_[slot] = r;
    }
    remainGrid_.insert(slot, pos.x, pos.y);
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
        h.addI64(u.kills);
        h.addI64(u.blessings);
        h.addI64(u.stats.maxHp);
        hashVec(h, u.pos);
        hashVec(h, u.facing);
        hashVec(h, u.vel);
        h.addU64(u.airborne);
        h.addU64(u.engaged);
        h.addU64(u.hasMoveTarget);
        hashVec(h, u.moveTarget);
        h.addI64(u.marchSpeed.raw);
        h.addU64(u.gibbed);
        h.addU64(u.hasAttackTarget);
        hashVec(h, u.attackTarget);
        h.addI64(u.reloadRemaining);
        h.addI64(u.meleeCooldown);
        h.addI64(u.healCooldown);
        h.addI64(u.healCharges);
        h.addU64(static_cast<uint64_t>(u.flight));
        h.addI64(u.runway.raw);
        h.addI64(u.stamina);
        h.addU64(u.path.size());
        for (const Vec3& w : u.path) hashVec(h, w);
        h.addU64(u.pathIndex);
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
        h.addU64(p.bleeding);
        h.addI64(p.smear.raw);
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
