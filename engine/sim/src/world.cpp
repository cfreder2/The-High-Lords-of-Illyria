#include "illyria/sim/world.h"

#include <algorithm>
#include <functional>
#include <limits>

#include "illyria/sim/ballistics.h"
#include "illyria/sim/continent.h"
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
// A flier who lands this near his goal walks the rest of the way straight.
constexpr Fixed kLandedReach = Fixed::fromInt(6);
// Waypoints count as reached within this distance.
constexpr Fixed kWaypointReach = Fixed::fromRatio(3, 2);
// Near a waypoint, one already closer to the next with the way clear moves on
// (soldiers jostling on a road need not all touch the same spot).
constexpr Fixed kWaypointNear = Fixed::fromInt(4);
// A waypoint on a castle's ramps or walk counts as reached this near.
constexpr Fixed kRaisedReach = Fixed::fromRatio(1, 2);
// Marching soldiers keep their files this far either side of the squad's route.
constexpr Fixed kMarchFile = Fixed::fromInt(2);
// Bodies walking through loose remains kick them along.
constexpr Fixed kKickReach = Fixed::fromRatio(1, 4);
constexpr int kMaxKicksPerTick = 2;
// No one walks up or down a face steeper than 45 degrees: cliffs are walls.
constexpr Fixed kCliffGrade = Fixed::one();
// No one steps up a ledge higher than this, or down a drop deeper than this
// (a wall-walk's edge, the side of a stair).
constexpr Fixed kMaxStepUp = Fixed::fromRatio(9, 10);
constexpr Fixed kMaxStepDown = Fixed::fromRatio(3, 2);
// Soldiers this far below a wall's top are blocked by it; above, they walk its top.
constexpr Fixed kWallClear = Fixed::fromInt(2);
// Shot blind over a wall, at a mark the archer cannot see, arrows scatter this much wider.
constexpr Fixed kBlindDispersion = Fixed::fromInt(2);
// A missile's path is checked for obstacles up to this short of its mark
// (what stands at it is the mark); at a defender on a wall-walk, right up to
// him, so the parapet in front of him counts.
constexpr Fixed kClearShort = Fixed::fromInt(2);
constexpr Fixed kClearShortAtWalk = Fixed::fromRatio(3, 10);
// A missile lofted over the highest thing in its way clears it by this; a
// wall or gate this near the bow is the parapet he shoots through (walkShot's).
constexpr Fixed kLoftClear = Fixed::fromRatio(3, 10);
constexpr Fixed kOwnParapet = Fixed::fromInt(2);
// Trees and bushes this near the mark are the wood he stands in: shot into, not over.
constexpr Fixed kWoodCover = Fixed::fromInt(6);
// An arrow glancing off stone keeps this share of its speed, and drops.
constexpr int64_t kGlanceDivisor = 24;
// Veterancy. A soldier is promoted at these kill counts (regular, veteran,
// elite); a hero gains a level at these (levels 2 to 9).
constexpr int32_t kSoldierRanks[] = {2, 5, 10};
constexpr int32_t kHeroLevels[] = {3, 7, 12, 18, 25, 33, 42, 52};
// Each blessing adds this much to a soldier's damage, and each rank or
// blessing to the chance his shield turns a blow; a hero level adds damage
// and hit points. (A rank's damage is a small edge: world_edges.cpp.)
constexpr int32_t kBlessingDamagePct = 10;
constexpr int32_t kHeroLevelDamagePct = 6;
constexpr Fixed kRankBlockBonus = Fixed::fromRatio(3, 100);

// Largest obstacle extent, for grid queries (more where a region has larger:
// a sage's tower, a church's nave; World::obstacleReach_).
constexpr Fixed kMaxObstacleExtent = Fixed::fromInt(7);
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
// Parts roll down slopes only in their first seconds (then they lodge somewhere).
constexpr int32_t kRollTicks = 12 * kTicksPerSecond;
// A satchel is a heavy bag: it bounces less and drags more than loose debris.
constexpr Fixed kBombFriction = Fixed::fromRatio(2, 5);
// Grenadiers throw short to allow for the bounce and roll.
constexpr Fixed kBombAimFraction = Fixed::fromRatio(7, 10);
// An arrow falling faster than this sticks into the ground; slower, it lies flat.
constexpr Fixed kStickSpeed = Fixed::fromInt(9);
// A body part flying faster than this hurts whoever it strikes (a kick is far slower).
constexpr Fixed kDebrisHurtSpeed = Fixed::fromInt(6);
// Healers look this far for wounded friends.
constexpr Fixed kHealSeekRange = Fixed::fromInt(25);
// Catapults throw at a fixed elevation (tan 4/3, 53 degrees): a high, plunging arc.
constexpr Fixed kStoneElevation = Fixed::fromRatio(4, 3);

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
    h.addI64(s.ranged.minRange.raw);
    h.addU64(static_cast<uint64_t>(s.ranged.kind));
    h.addU64(static_cast<uint64_t>(s.ranged.damageType));
    hashExplosive(h, s.ranged.explosive);
    h.addI64(s.melee.damage);
    h.addI64(s.melee.reach.raw);
    h.addI64(s.melee.cooldownTicks);
    h.addI64(s.melee.knockback.raw);
    h.addI64(s.melee.bonusVsMountedPct);
    h.addU64(static_cast<uint64_t>(s.melee.damageType));
    h.addI64(s.aggroRange.raw);
    h.addI64(s.shield.blockChance.raw);
    h.addI64(s.shield.arcCos.raw);
    h.addI64(s.healing.amount);
    h.addI64(s.healing.range.raw);
    h.addI64(s.healing.cooldownTicks);
    h.addI64(s.healing.charges);
    h.addI64(s.terrain.openPct);
    h.addI64(s.terrain.hillsPct);
    h.addU64(static_cast<uint64_t>(s.gait));
    h.addI64(s.flyAltitude.raw);
    h.addI64(s.flySpeed.raw);
    h.addI64(s.takeoffRun.raw);
    h.addI64(s.minFlight.raw);
    h.addI64(s.staminaTicks);
    h.addI64(s.mass.raw);
    h.addU64(s.mounted);
    h.addU64(s.bleeds);
    h.addI64(s.moraleShockPct);
    // Only for bowmen who draw, so battles without them hash as before.
    if (s.ranged.drawTicks > 0) h.addI64(s.ranged.drawTicks);
    // Only for those who carry satchels to lay, so battles without them hash as before.
    if (s.satchels.count > 0) {
        h.addI64(s.satchels.count);
        hashExplosive(h, s.satchels.charge);
        h.addI64(s.satchels.layTicks);
    }
    // Only for scavengers, so battles without them hash as before.
    if (s.scavenge.sight > Fixed::zero()) {
        h.addI64(s.scavenge.sight.raw);
        h.addI64(s.scavenge.throwSpeed.raw);
        h.addI64(s.scavenge.throwRange.raw);
        h.addI64(s.scavenge.dispersionAt100m.raw);
        h.addI64(s.scavenge.windupTicks);
    }
}

void hashScavenger(StateHasher& h, const Scavenger& c) {
    h.addU64(static_cast<uint64_t>(c.mind));
    h.addU64(static_cast<uint64_t>(c.prize));
    h.addU64(c.prizeRef);
    hashVec(h, c.goal);
    h.addU64(c.mark);
    hashVec(h, c.markSeen);
    h.addI64(c.timer);
    h.addU64(static_cast<uint64_t>(c.held));
    h.addU64(c.heldOwner);
    h.addU64(c.satchel);
    h.addI64(c.fuse);
    hashExplosive(h, c.blast);
    h.addU64(c.raiding);
    h.addU64(c.wary);
    h.addU64(c.pack);
    h.addI64(c.packMen);
    hashVec(h, c.home);
    h.addI64(c.quiet);
    h.addI64(c.replan);
    h.addU64(c.shotBy);
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

// How wide an obstacle stands to a missile: trees catch them in their crowns as well as their trunks.
Fixed missileRadius(const Obstacle& o) {
    if (o.kind == Obstacle::Kind::Pine) return Fixed::fromRatio(9, 10);
    if (o.kind == Obstacle::Kind::Oak) return Fixed::fromRatio(6, 5);
    if (o.kind == Obstacle::Kind::House) return min(o.halfX, o.halfY) + Fixed::fromRatio(1, 2);
    return o.radius;
}

// The tangent of a launch velocity's elevation.
Fixed elevationTan(const Vec3& v) {
    const Fixed flat = v.flat().length();
    return flat > Fixed::zero() ? v.z / flat : Fixed::fromInt(1000);
}

// 2D cross product of two flat vectors.
Fixed cross(const Vec3& a, const Vec3& b) { return a.x * b.y - a.y * b.x; }

// Puts p `keep` out from a box (middle c, half sizes hx, hy; from inside, by
// the shallower side) or a disc (middle c, radius r). True if it moved.
bool outOfBox(Vec3& p, const Vec3& c, Fixed hx, Fixed hy, Fixed keep) {
    const Fixed cx = clamp(p.x, c.x - hx, c.x + hx);
    const Fixed cy = clamp(p.y, c.y - hy, c.y + hy);
    const Vec3 d{p.x - cx, p.y - cy, Fixed::zero()};
    const Fixed distSq = d.lengthSq();
    if (distSq >= keep * keep) return false;
    if (distSq > Fixed::zero()) {
        const Fixed dist = sqrt(distSq);
        p += d / dist * (keep - dist);
    } else {
        const Fixed px = hx - abs(p.x - c.x), py = hy - abs(p.y - c.y);
        if (px < py) p.x += (p.x < c.x ? -(px + keep) : px + keep);
        else p.y += (p.y < c.y ? -(py + keep) : py + keep);
    }
    return true;
}
bool outOfDisc(Vec3& p, const Vec3& c, Fixed r, Fixed keep) {
    const Vec3 d = (p - c).flat();
    const Fixed reach = keep + r;
    const Fixed distSq = d.lengthSq();
    if (distSq >= reach * reach) return false;
    const Fixed dist = sqrt(distSq);
    const Vec3 n = dist > Fixed::zero() ? d / dist : Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()};
    p += n * (reach - dist);
    return true;
}

}  // namespace

World::World(const WorldConfig& config) : config_(config), rng_(config.seed, 0) {
    if (config_.region) {
        // The region owns the terrain; share it without copying.
        config_.terrain = std::shared_ptr<const Terrain>(config_.region, &config_.region->terrain());
        nav_ = std::make_unique<NavGrid>(*config_.region);
        const auto& obstacles = config_.region->obstacles();
        obstacleReach_ = kMaxObstacleExtent;
        for (uint32_t i = 0; i < obstacles.size(); ++i) {
            if (!obstacles[i].solid) continue;
            const Obstacle& o = obstacles[i];
            obstacleReach_ = max(obstacleReach_, o.kind == Obstacle::Kind::House ? max(o.halfX, o.halfY) : o.radius);
        }
        obstacleGrid_.build(obstacles);
        for (const Gate& g : config_.region->gates()) gates_.push_back({g.maxHp});
    }
}

void World::ObstacleGrid::build(const std::vector<Obstacle>& obstacles) {
    int64_t cx0 = INT64_MAX, cx1 = INT64_MIN, cy0 = INT64_MAX, cy1 = INT64_MIN;
    for (const Obstacle& o : obstacles) {
        if (!o.solid) continue;
        cx0 = std::min(cx0, cellOf(o.pos.x));
        cx1 = std::max(cx1, cellOf(o.pos.x));
        cy0 = std::min(cy0, cellOf(o.pos.y));
        cy1 = std::max(cy1, cellOf(o.pos.y));
    }
    x0_ = cx0 > cx1 ? 0 : cx0;
    y0_ = cy0 > cy1 ? 0 : cy0;
    w_ = cx0 > cx1 ? 0 : cx1 - cx0 + 1;
    h_ = cy0 > cy1 ? 0 : cy1 - cy0 + 1;
    const auto slot = [&](const Obstacle& o) { return static_cast<size_t>((cellOf(o.pos.y) - y0_) * w_ + cellOf(o.pos.x) - x0_); };
    start_.assign(static_cast<size_t>(w_ * h_) + 1, 0);
    for (const Obstacle& o : obstacles)
        if (o.solid) ++start_[slot(o) + 1];
    for (size_t c = 0; c + 1 < start_.size(); ++c) start_[c + 1] += start_[c];
    recs_.resize(start_.back());
    std::vector<uint32_t> fill(start_.begin(), start_.end() - 1);
    for (uint32_t i = 0; i < obstacles.size(); ++i) {
        const Obstacle& o = obstacles[i];
        if (!o.solid) continue;
        const Fixed extent = o.kind == Obstacle::Kind::House ? max(o.halfX, o.halfY) : o.radius;
        recs_[fill[slot(o)]++] = {o.pos.x, o.pos.y, extent, i};
    }
}

EntityId World::spawnUnit(PlayerId owner, const Vec3& pos, const Vec3& facing,
                          const UnitStats& stats) {
    // A soldier's id is his place in units_ plus one (the game indexes by it),
    // even when he joins once missiles are flying (they take ids from the
    // same counter, but are never looked up among the soldiers). A place a man
    // was taken out of (removeUnit) is given to the next man, the lowest first.
    Unit u;
    if (!freeSlots_.empty()) {
        u.id = freeSlots_.back();  // (descending: the lowest is the last, taken in O(1))
        freeSlots_.pop_back();
    } else {
        u.id = nextUnitId_++;
    }
    nextId_ = std::max(nextId_, u.id + 1);
    u.owner = owner;
    u.stats = stats;
    u.hp = stats.maxHp;
    u.healCharges = stats.healing.charges;
    u.stamina = stats.staminaTicks;
    u.satchels = stats.satchels.count;
    u.pos = pos;
    if (config_.terrain) u.pos.z = groundHeight(pos.x, pos.y);
    u.facing = facing;
    if (u.id <= units_.size()) {
        units_[u.id - 1] = u;
        ++spawns_[u.id - 1];
    } else {
        units_.push_back(u);
        spawns_.push_back(1);
    }
    broadValid_ = false;
    return u.id;
}

void World::removeUnit(EntityId id) {
    // (The campaign puts a squad's men in the world to fight and takes them out when it is over. His place stays, empty:
    // erasing it would move every unit after him, and everything that presents the world indexes by id.)
    Unit* u = mutableUnit(id);
    if (!u || u->removed) return;
    u->removed = true;
    u->hp = 0;
    u->hasMoveTarget = false;
    u->hasAttackTarget = false;
    u->targetUnit = kNoEntity;
    u->foe = kNoEntity;
    u->engaged = false;
    u->airborne = false;  // (nothing of his is stepped any more)
    u->flight = Unit::Flight::Grounded;
    u->vel = Vec3{};
    u->routed = false;
    // Nobody keeps him as a foe, a mark, a rally point or a shooter: the next man in his place is not he (and the places of
    // the dead go on being reused, so a stale id would be taken for the newcomer).
    const auto forget = [id](EntityId& ref) {
        if (ref == id) ref = kNoEntity;
    };
    for (Unit& o : units_) {
        forget(o.targetUnit);
        forget(o.rally);
        forget(o.engageFoe);
        forget(o.lastAttacker);
        forget(o.thrower);
        forget(o.foe);
        forget(o.scav.mark);
        forget(o.scav.shotBy);
    }
    for (Projectile& p : projectiles_) forget(p.shooter);
    for (Projectile& p : spawned_) forget(p.shooter);
    freeSlots_.insert(std::lower_bound(freeSlots_.begin(), freeSlots_.end(), id, std::greater<EntityId>()), id);
    broadValid_ = false;
    pathCache_.clear();
}

void World::clearFight(uint64_t seed) {
    // The empty places (freeSlots_) and the generations (spawns_) go with the men: a fight starts with neither. They are not
    // saved, and a save restores the same checksum all the same: Campaign::save settles every stack asleep first
    // (settleAll), which ends the fight and clears the world here, as it does in the game that goes on from the save; and
    // Campaign::checksum hashes the world only while a fight is on. freeSlots_ is state (it says whose id the next man has)
    // and is in World::checksum; spawns_ is not, for no step reads it: it is only what presentations (Battle, the renderer,
    // Voice) key what they keep by place on, and a loaded game presents from nothing.
    units_.clear();
    freeSlots_.clear();
    spawns_.clear();
    projectiles_.clear();
    spawned_.clear();
    impacts_.clear();
    heroDeaths_.clear();
    rallyCalls_.clear();
    decisions_.clear();
    deathEvents_.clear();
    deaths_.clear();
    remains_.clear();
    changedRemains_.clear();
    pendingExplosions_.clear();
    brokenSides_.clear();
    remainGrid_.clear();
    unitGrid_.clear();
    broadValid_ = false;
    pathCache_.clear();
    remainsAdded_ = 0;
    packs_ = 0;
    tick_ = 0;
    for (size_t i = 0; i < gates_.size(); ++i) {
        GateState& g = gates_[i];
        if (g.mode != GateState::Mode::Shut && nav_) nav_->shutGate(i);  // (a gate swung open is shut again: it is no one's business who was going through it)
        g.mode = GateState::Mode::Shut;  // (the pins and the garrisons are the holders' business and stay)
        g.since = 0;
        g.openUntil = 0;
        if (g.broken()) g.brokenTick = 0;
    }  // (when it broke is no one's business once the clock is back at nothing)
    nextId_ = 1;
    nextUnitId_ = 1;
    rng_ = Rng(seed, 0);
}

void World::restoreUnit(EntityId id, int32_t hp, int32_t kills, int32_t morale) {
    Unit* u = mutableUnit(id);
    if (!u) return;
    u->hp = std::clamp(hp, 1, u->stats.maxHp);
    u->kills = kills;
    u->morale = std::clamp(morale, 0, kMoraleFull);
}

void World::restoreQueue(EntityId id, std::vector<Command> queue) {
    Unit* u = mutableUnit(id);
    if (!u) return;
    if (queue.size() > kMaxQueuedOrders) queue.resize(kMaxQueuedOrders);
    for (Command& c : queue) {
        c.unit = id;
        c.queue = false;
    }
    u->queue = std::move(queue);
    u->orderTick = tick_;
}

const Unit* World::findUnit(EntityId id) const {
    // Units are only ever appended, so the vector is sorted by id (and a unit's place is his id less one). One taken
    // out of the world (removeUnit) is no longer found.
    if (id >= 1 && id <= units_.size() && units_[id - 1].id == id) return units_[id - 1].removed ? nullptr : &units_[id - 1];
    auto it = std::lower_bound(units_.begin(), units_.end(), id,
                               [](const Unit& u, EntityId i) { return u.id < i; });
    return it != units_.end() && it->id == id && !it->removed ? &*it : nullptr;
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

Fixed World::slopeSquared(Fixed x, Fixed y) const {
    const Fixed d = Fixed::one();
    const Fixed gx = (groundHeight(x + d, y) - groundHeight(x - d, y)) / 2;
    const Fixed gy = (groundHeight(x, y + d) - groundHeight(x, y - d)) / 2;
    return gx * gx + gy * gy;
}

Fixed World::slopeAt(Fixed x, Fixed y) const {
    if (!config_.terrain) return Fixed::zero();
    return sqrt(slopeSquared(x, y));
}

bool World::steeperThan(Fixed x, Fixed y, Fixed limit) const {
    if (!config_.terrain) return Fixed::zero() > limit;
    return sqrtExceeds(slopeSquared(x, y), limit);
}

void World::step(std::span<const Command> commands) {
    impacts_.clear();
    heroDeaths_.clear();
    rallyCalls_.clear();
    decisions_.clear();
    deathEvents_.clear();
    deaths_.clear();
    changedRemains_.clear();

    // Peers may receive one tick's commands from different players in any
    // order; applying them grouped by player keeps the outcome identical.
    // Each player's own commands keep the order they were issued in.
    std::vector<Command> ordered(commands.begin(), commands.end());
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const Command& a, const Command& b) { return a.player < b.player; });
    bool raids = false;
    for (const Command& cmd : ordered) {
        applyCommand(cmd);
        raids = raids || cmd.type == Command::Type::Raid;
    }
    // Those let loose together go as packs (world_scavenge.cpp).
    if (raids) formPacks();
    buildBroad();
    updateGates();
    keepGates();

    for (Unit& u : units_) {
        if (!u.present()) continue;
        if (!u.queue.empty()) advanceQueue(u);
        const Vec3 before = u.pos;
        updateUnit(u);
        trackMomentum(u, before);
    }
    separateBodies();
    updateProjectiles();
    updateMorale();
    applyZone();
    pathCache_.clear();
    ++tick_;
}

void World::beginMove(Unit& u, const Vec3& target, Fixed speed, const Vec3& face) {
    u.hasMoveTarget = true;
    u.moveTarget = target;
    u.marchSpeed = max(Fixed::zero(), speed);
    u.arriveFacing = face.flat() == Vec3{} ? Vec3{} : normalized(face.flat());
    u.hasAttackTarget = false;
    u.attackGate = -1;
}

void World::applyCommand(const Command& cmd) {
    if (cmd.type == Command::Type::SetGate) {
        applyGateOrder(cmd);
        return;
    }
    Unit* u = mutableUnit(cmd.unit);
    // A broken unit too: orders always win (it runs only when it has none; updateUnit).
    // Not a side that is out of the match (its flag fallen: sideBroken).
    if (u == nullptr || !u->alive() || u->owner != cmd.player || sideBroken(u->owner)) return;
    // Only a scavenger raids; to anyone else the order means nothing.
    if (cmd.type == Command::Type::Raid && u->stats.scavenge.sight == Fixed::zero()) return;
    // Shift: after the order in hand (or the ones already waiting). A soldier with nothing in hand and nothing waiting begins it now.
    if (cmd.queue && cmd.type != Command::Type::Stop && cmd.type != Command::Type::Raid) {
        if (hasOrder(*u) || !u->queue.empty()) {
            if (u->queue.size() < kMaxQueuedOrders) {
                u->queue.push_back(cmd);
                u->queue.back().queue = false;
            }
            return;
        }
    }
    u->queue.clear();  // an order that is not added replaces the last, and the ones to come
    startOrder(*u, cmd);
}

void World::startOrder(Unit& unit, const Command& cmd) {
    Unit* u = &unit;
    u->orderTick = tick_;
    u->layOrdered = false;  // every order replaces the last
    if (u->scav.raiding) endRaid(*u);  // a raid too
    // Pursuits, force marches and attacks on things (world_orders.cpp).
    applyOrder(*u, cmd);
    switch (cmd.type) {
        case Command::Type::AttackUnit:
        case Command::Type::AttackObject:
            break;
        case Command::Type::Raid: {
            // Let loose from where he stands, keeping what he holds (world_scavenge.cpp).
            u->hasMoveTarget = false;
            u->hasAttackTarget = false;
            u->attackGate = -1;
            u->path.clear();
            u->pathIndex = 0;
            Scavenger& s = u->scav;
            s.raiding = true;
            s.wary = false;
            s.pack = 0;
            s.packMen = 0;
            s.home = u->pos;
            s.quiet = 0;
            s.replan = 0;
            s.shotBy = kNoEntity;
            s.mind = Scavenger::Mind::Scout;
            s.prize = Scavenger::Prize::None;
            s.prizeRef = 0;
            s.mark = kNoEntity;
            s.timer = 0;
            s.goal = u->pos;
            break;
        }
        case Command::Type::LaySatchel:
            // There, and lay one (world_satchels.cpp); at hand, at once.
            if (u->satchels <= 0) return;
            u->hasAttackTarget = false;
            u->attackGate = -1;
            if ((cmd.target - u->pos).flat().length() <= Fixed::fromRatio(3, 2)) {
                u->hasMoveTarget = false;
                laySatchel(*u, cmd.target);
                break;
            }
            u->hasMoveTarget = true;
            u->moveTarget = cmd.target;
            u->marchSpeed = Fixed::zero();
            u->arriveFacing = {};
            u->layOrdered = true;
            planPath(*u, cmd.target);
            break;
        case Command::Type::Move: {
            const Vec3 target = insideParapet(clearOfStone(cmd.target, u->stats), u->stats.radius);
            beginMove(*u, target, cmd.speed, cmd.face);
            if (cmd.march) planMarch(*u, target, cmd.marchFrom, cmd.marchTo);
            else planPath(*u, target);
            break;
        }
        case Command::Type::AttackGate:
        case Command::Type::AttackGround:
            if (u->stats.ranged.muzzleSpeed == Fixed::zero()) return;
            u->hasAttackTarget = true;
            u->hasMoveTarget = false;
            u->attackGate = -1;
            if (cmd.type == Command::Type::AttackGate && config_.region) {
                // The standing gate nearest the spot shown.
                const auto& gs = config_.region->gates();
                Fixed best = Fixed::fromInt(12);
                for (size_t i = 0; i < gs.size(); ++i) {
                    const Fixed d = (cmd.target - (gs[i].a + gs[i].b) * Fixed::fromRatio(1, 2)).flat().length();
                    if (gates_[i].broken() || d >= best) continue;
                    best = d;
                    u->attackGate = static_cast<int32_t>(i);
                }
                if (u->attackGate >= 0) {
                    u->attackTarget = gateAimPoint(u->attackGate, u->pos);
                    break;
                }
            }
            // "Attack ground" means the ground: never aim into or above the hillside.
            u->attackTarget = {cmd.target.x, cmd.target.y, groundHeight(cmd.target.x, cmd.target.y)};
            u->hasMoveTarget = false;
            break;
        case Command::Type::Stop:
            u->hasMoveTarget = false;
            u->hasAttackTarget = false;
            u->attackGate = -1;
            break;
    }
}

Vec3 World::clearOfStone(const Vec3& p, const UnitStats& stats) const {
    if (!config_.region || !nav_ || !nav_->nearSite(p) || nav_->floorAt(p, false) >= 0) return p;
    const Fixed keep = stoneStandoff(stats.radius);
    const auto& obstacles = config_.region->obstacles();
    // As the push-out has it (separateBodies), a few times over for stones side by side.
    Vec3 q = p;
    for (int pass = 0; pass < 3; ++pass) {
        for (const Floor& f : config_.region->floors())
            outOfBox(q, (f.baseLo + f.baseHi) * Fixed::fromRatio(1, 2), (f.baseHi.x - f.baseLo.x) / 2, (f.baseHi.y - f.baseLo.y) / 2, keep);
        obstacleGrid_.query(q.x, q.y, keep + obstacleReach_, [&](uint32_t k) {
            const Obstacle& o = obstacles[k];
            if (!o.standingStone()) return;
            if (o.kind == Obstacle::Kind::House) outOfBox(q, o.pos, o.halfX, o.halfY, keep);
            else outOfDisc(q, o.pos, o.radius, keep);
        });
    }
    return q;
}

void World::planPath(Unit& u, const Vec3& to) {
    u.path.clear();
    u.pathIndex = 0;
    if (config_.region && !config_.region->ramparts().empty() && planRampartPath(u, to)) return;
    groundPath(u.pos, to, u.path, u.stats.gait);
}

void World::planMarch(Unit& u, const Vec3& to, const Vec3& from, const Vec3& goal) {
    if (!nav_ || !config_.region->ramparts().empty()) {
        planPath(u, to);
        return;
    }
    // Onto or off a ruin's platform each man finds his own way, by its stair.
    if (!config_.region->floors().empty() && (nav_->floorAt(u.pos, true) >= 0 || nav_->floorAt(to, false) >= 0 ||
                                              nav_->floorAt(from, false) >= 0 || nav_->floorAt(goal, false) >= 0)) {
        planPath(u, to);
        return;
    }
    u.path.clear();
    u.pathIndex = 0;
    // One route for the squad (planned once a tick for all of them).
    std::vector<Vec3> route;
    groundPath(from, goal, route, u.stats.gait);
    if (route.size() <= 1) {
        groundPath(u.pos, to, u.path, u.stats.gait);
        return;
    }
    // Each soldier keeps his file, a little to the side of the route, where
    // the ground there is the same (on the road, not the field beside it).
    const Vec3 dir = flatDirection(route[route.size() - 2], goal, flatDirection(from, goal, u.facing));
    const Fixed file = clamp((to - goal).flat().dot(Vec3{dir.y, -dir.x, Fixed::zero()}), -kMarchFile, kMarchFile);
    for (size_t k = 0; k + 1 < route.size(); ++k) {
        const Vec3 w = route[k];
        const Vec3 d = flatDirection(k == 0 ? from : route[k - 1], route[k + 1], dir);
        const Vec3 p = w + Vec3{d.y, -d.x, Fixed::zero()} * file;
        bool same = groundAt(p.x, p.y) == groundAt(w.x, w.y) && !nav_->blocked(p);
        // By a site's stone his file's way must stay as clear of it as the
        // route's: from his last point and on to the route's next, else he
        // closes on the route there (a file on the inside of a corner).
        if (same && (nav_->nearSite(w) || nav_->nearSite(p))) {
            const Vec3 next = route[k + 1];
            same = nav_->clearLine(w, p, u.stats.gait) && nav_->clearLine(p, next, u.stats.gait) &&
                   (u.path.empty() || nav_->clearLine(u.path.back(), p, u.stats.gait));
        }
        u.path.push_back(same ? p : w);
    }
    // Skip waypoints already behind him, and join the route where he can.
    size_t skip = 0;
    while (skip + 1 < u.path.size() &&
           (u.path[skip + 1] - u.pos).flat().lengthSq() <= (u.path[skip + 1] - u.path[skip]).flat().lengthSq())
        ++skip;
    u.path.erase(u.path.begin(), u.path.begin() + static_cast<long>(skip));
    if (!nav_->clearLine(u.pos, u.path.front(), u.stats.gait)) {
        std::vector<Vec3> join;
        groundPath(u.pos, u.path.front(), join, u.stats.gait);
        u.path.insert(u.path.begin(), join.begin(), join.end() - 1);
    }
    // His own place at the end may lie round a site's stone from the route's.
    if ((nav_->nearSite(to) || nav_->nearSite(u.path.back())) && !nav_->clearLine(u.path.back(), to, u.stats.gait)) {
        const Vec3 last = u.path.back();
        groundPath(last, to, u.path, u.stats.gait);
        return;
    }
    u.path.push_back(to);
}

void World::groundPath(const Vec3& from, const Vec3& to, std::vector<Vec3>& out, Gait gait) {
    if (!nav_) {
        out.push_back(to);
        return;
    }
    // Units of a squad start and end in neighbouring cells; plan once per cell
    // pair (on a ruin's platform, per cell of its finer grid: NavGrid::floorCell).
    const int64_t cs = nav_->cellSize();
    const auto cell = [&](const Vec3& p, bool byHeight) {
        const int32_t f = nav_->floorCell(p, byHeight);
        return f >= 0 ? (int64_t{1} << 40) + f : (p.x.floorToInt() / cs) * 4096 + p.y.floorToInt() / cs;
    };
    const auto key = std::make_tuple(static_cast<int>(gait), cell(from, true), cell(to, false));
    auto it = pathCache_.find(key);
    if (it == pathCache_.end() && config_.region && !config_.region->ramparts().empty()) {
        // Round a castle a squad's men, come down the same ramp, share one
        // route: one planned this tick from the same cell to a goal a few
        // metres from this one, its last leg turned to this goal (in plain
        // sight of the waypoint before it).
        constexpr Fixed kShare = Fixed::fromInt(8);
        for (auto j = pathCache_.lower_bound(std::make_tuple(std::get<0>(key), std::get<1>(key), std::numeric_limits<int64_t>::min()));
             j != pathCache_.end() && std::get<0>(j->first) == std::get<0>(key) && std::get<1>(j->first) == std::get<1>(key); ++j) {
            const std::vector<Vec3>& r = j->second;
            if (r.size() < 2 || (r.back() - to).flat().lengthSq() > kShare * kShare || !nav_->clearLine(r[r.size() - 2], to, gait)) continue;
            out.insert(out.end(), r.begin(), r.end());
            out.back() = to;
            return;
        }
    }
    if (it == pathCache_.end()) {
        std::vector<Vec3> route;
        if (!nav_->findPath(from, to, route, gait) && !routeToGate(from, to, route, gait)) route = {to};
        it = pathCache_.emplace(key, std::move(route)).first;
    } else if (nav_->nearSite(from) || nav_->nearSite(to)) {
        // By a site's stone a route shared across the cell may set off (or end)
        // round the stone from this man: then his own, shared by the metre.
        const std::vector<Vec3>& r = it->second;
        const Vec3 before = r.size() >= 2 ? r[r.size() - 2] : from;
        if (!nav_->clearLine(from, r.front(), gait) || !nav_->clearLine(before, to, gait)) {
            const auto fine = [](const Vec3& p) { return (int64_t{1} << 41) + p.x.floorToInt() * 4096 + p.y.floorToInt(); };
            const auto own = std::make_tuple(static_cast<int>(gait), fine(from), fine(to));
            it = pathCache_.find(own);
            if (it == pathCache_.end()) {
                std::vector<Vec3> route;
                if (!nav_->findPath(from, to, route, gait) && !routeToGate(from, to, route, gait)) route = {to};
                it = pathCache_.emplace(own, std::move(route)).first;
            }
        }
    }
    out.insert(out.end(), it->second.begin(), it->second.end());
    out.back() = to;
}

namespace {
// Where a point lies along a rampart's way round (Rampart::way): the leg's
// index and the distance from its start.
struct RingPos {
    size_t edge = 0;
    Fixed along;
};
RingPos ringPos(const Rampart& rp, const Vec3& p) {
    RingPos best;
    Fixed bestD = Fixed::fromInt(1000000);
    const size_t n = rp.way.size();
    for (size_t e = 0; e < n; ++e) {
        const Vec3 a = rp.way[e], b = rp.way[(e + 1) % n];
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
// The point on the way round nearest p.
Vec3 onWalk(const Rampart& rp, const Vec3& p) {
    const RingPos rpos = ringPos(rp, p);
    const Vec3 a = rp.way[rpos.edge], b = rp.way[(rpos.edge + 1) % rp.way.size()];
    const Fixed len = (b - a).flat().length();
    return len > Fixed::zero() ? a + (b - a) * (rpos.along / len) : a;
}
// The shorter way round the ring from one point to another: the bends passed
// (the corners, and round each tower on its gallery).
std::vector<Vec3> ringRoute(const Rampart& rp, const Vec3& from, const Vec3& to, Fixed* length) {
    const size_t n = rp.way.size();
    const RingPos a = ringPos(rp, from), b = ringPos(rp, to);
    std::vector<Fixed> lens(n);
    Fixed total;
    for (size_t e = 0; e < n; ++e) {
        lens[e] = (rp.way[(e + 1) % n] - rp.way[e]).flat().length();
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
                out.push_back(rp.way[e]);
                if (e == b.edge) break;
            }
        if (length) *length = fwd;
    } else {
        if (!(a.edge == b.edge && b.along <= a.along))
            for (size_t e = a.edge;; e = (e + n - 1) % n) {
                out.push_back(rp.way[e]);
                if (e == (b.edge + 1) % n) break;
            }
        if (length) *length = total - fwd;
    }
    out.push_back(to);
    return out;
}
// Along a walk, keep to the right of it: the walk is wide enough for two
// lanes, so men going opposite ways pass instead of meeting head on. The
// route's corners are mitred onto the lane, and lane points are added a
// metre into the first and last legs (the destination stays where it is).
std::vector<Vec3> keepRight(const Rampart& rp, const Vec3& from, std::vector<Vec3> route) {
    if (route.empty()) return route;
    // Lanes either side of the middle of the walk (inside the parapet).
    const Fixed parapet = Continent::Castle::kParapetThick;
    const Fixed lane = (rp.halfThick * 2 - parapet) / 4, shift = parapet / 2;
    const auto unit = [](const Vec3& v) {
        const Fixed len = v.flat().length();
        return len > Fixed::zero() ? v.flat() / len : Vec3{};
    };
    const auto rightOf = [](const Vec3& d) { return Vec3{d.y, -d.x, Fixed::zero()}; };
    // The lane's offset from the middle of the wall for a leg running `dir` past p.
    const auto laneAt = [&](const Vec3& right, const Vec3& p) {
        const bool outward = right.dot((p - rp.centre).flat()) > Fixed::zero();
        return outward ? lane - shift : lane + shift;
    };
    std::vector<Vec3> pts{from};
    pts.insert(pts.end(), route.begin(), route.end());
    std::vector<Vec3> out;
    const size_t n = pts.size();
    for (size_t i = 1; i + 1 < n; ++i) {  // the corners
        const Vec3 rin = rightOf(unit(pts[i] - pts[i - 1])), rout = rightOf(unit(pts[i + 1] - pts[i]));
        const Vec3 b = rin + rout;
        const Fixed b2 = b.flat().lengthSq();
        if (i == 1 && (pts[1] - from).flat().length() > Fixed::fromInt(3)) {
            const Vec3 d = unit(pts[1] - from), mid = onWalk(rp, from + d);
            out.push_back(mid + rightOf(d) * laneAt(rightOf(d), mid));
        }
        out.push_back(b2 > Fixed::fromRatio(1, 10) ? pts[i] + b * (laneAt(rin, pts[i]) * 2 / b2) : pts[i]);
    }
    // The last leg: onto the lane, then off it to the destination.
    const Vec3 to = pts[n - 1], prev = out.empty() ? from : out.back();
    if ((to - prev).flat().length() > Fixed::fromInt(3)) {
        const Vec3 d = unit(to - prev), first = onWalk(rp, prev + d), last = onWalk(rp, to - d);
        if (out.empty()) out.push_back(first + rightOf(d) * laneAt(rightOf(d), first));
        out.push_back(last + rightOf(d) * laneAt(rightOf(d), last));
    }
    for (Vec3& p : out) p.z = to.z;
    out.push_back(to);
    return out;
}
// Men on foot (and dwarves, elves, giants, the dead) climb castle ramps to
// the walls; horses, wolves, engines and beasts too big for the walk do not.
bool climbsRamps(const UnitStats& s) {
    return s.gait != Gait::Horse && s.gait != Gait::Wolf && s.gait != Gait::Wheeled && s.radius <= Fixed::fromRatio(6, 5);
}
// Where a point is among the castles' walls: up a ring's ramp (its slope or
// the landing at its head), on a ring's walk, or on the ground.
struct Perch {
    int ring = -1;  // -1: on the ground
    int ramp = -1;  // up that ring's ramp; -1: on its walk
    Fixed along;    // up a ramp: how far from its foot
};
// A soldier is placed by his height (beside the upper slope of a ramp, the
// walk stands higher than it); where an order sends him, by the map alone.
Perch perchOf(const Region& rg, const Vec3& p, bool byHeight) {
    const Fixed landing = Continent::Castle::kRampLanding, halfWidth = Continent::Castle::kRampHalfWidth;
    for (size_t i = 0; i < rg.ramparts().size(); ++i) {
        const Rampart& rp = rg.ramparts()[i];
        for (size_t s = 0; s < rp.ramps.size(); ++s) {
            // The slope and the landing, one strip from the foot to the
            // landing's end (the head is half a metre short of it).
            const auto& [foot, head] = rp.ramps[s];
            const Vec3 run = (head - foot).flat();
            const Fixed len = run.length();
            if (len <= Fixed::zero()) continue;
            const Vec3 dir = run / len, d = (p - foot).flat();
            const Fixed along = d.dot(dir), end = len + Fixed::fromRatio(1, 2);
            if (along < Fixed::zero() || along > end || abs(d.x * dir.y - d.y * dir.x) > halfWidth) continue;
            if (byHeight) {
                const Fixed slope = max(Fixed::one(), end - landing);
                const Fixed surface = foot.z + (head.z - foot.z) * min(along, slope) / slope;
                if (abs(p.z - surface) > Fixed::fromRatio(1, 2)) continue;
            }
            return {static_cast<int>(i), static_cast<int>(s), along};
        }
    }
    if (byHeight) {
        if (const int ring = rg.rampartAt(p.x, p.y, p.z); ring >= 0) return {ring, -1, {}};
        // Up at a walk's height beside its way round (on the walk where the
        // way bends round a tower, or on a gallery's outer part): on that walk.
        for (size_t i = 0; i < rg.ramparts().size(); ++i) {
            const Rampart& rp = rg.ramparts()[i];
            if (rp.way.empty() || abs(p.z - rp.way.front().z) > Fixed::one() || !Gate::inside(rp, p)) continue;
            const Fixed reach = rp.halfThick + Continent::Castle::kGalleryWidth;
            for (size_t e = 0; e < rp.way.size(); ++e) {
                const Vec3 a = rp.way[e], ab = (rp.way[(e + 1) % rp.way.size()] - a).flat();
                const Fixed len2 = ab.lengthSq();
                const Fixed t = len2 > Fixed::zero() ? clamp((p - a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
                if ((p - (a + ab * t)).flat().lengthSq() <= reach * reach) return {static_cast<int>(i), -1, {}};
            }
        }
        return {};
    }
    for (size_t i = 0; i < rg.ramparts().size(); ++i)
        if (rg.rampartAt(p.x, p.y, rg.ramparts()[i].corners.front().z) == static_cast<int>(i)) return {static_cast<int>(i), -1, {}};
    return {};
}
// The ground just off a ramp's foot, in line with it: men step on and off a
// ramp there, by its end, never up or down its sides.
constexpr Fixed kRampApron = Fixed::fromRatio(3, 2);
Vec3 apronOf(const Rampart& rp, size_t s) {
    const auto& [foot, head] = rp.ramps[s];
    const Vec3 run = (head - foot).flat();
    const Fixed len = run.length();
    return len > Fixed::zero() ? foot - run * (kRampApron / len) : foot;
}
// Between the apron and a point on the ground beside the ramp (past its
// foot), the way goes round the foot's corner, not over the ramp's side:
// that corner, if there is one to go round.
bool footCorner(const Rampart& rp, size_t s, const Vec3& p, Vec3& corner) {
    const auto& [foot, head] = rp.ramps[s];
    const Vec3 run = (head - foot).flat();
    const Fixed len = run.length();
    if (len <= Fixed::zero()) return false;
    const Vec3 dir = run / len, side{dir.y, -dir.x, Fixed::zero()}, d = (p - foot).flat();
    const Fixed across = d.dot(side), wide = Continent::Castle::kRampHalfWidth + Fixed::fromRatio(1, 2);
    if (d.dot(dir) <= Fixed::zero() || abs(across) <= Continent::Castle::kRampHalfWidth) return false;
    corner = foot - dir * kRampApron + side * (across < Fixed::zero() ? -wide : wide);
    corner.z = foot.z;
    return true;
}
// A way between a perch and the ground: where it meets the ground (the apron
// of the ramp taken, or the point itself, on the ground), how far it is from
// there by the walk and the ramp, and the ramp.
struct RampWay {
    Vec3 foot;
    Fixed cost;
    int ramp = -1;
};
std::vector<RampWay> rampWays(const Region& rg, const Perch& p, const Vec3& at) {
    if (p.ring < 0) return {{at, Fixed::zero(), -1}};
    const Rampart& rp = rg.ramparts()[static_cast<size_t>(p.ring)];
    if (p.ramp >= 0) return {{apronOf(rp, static_cast<size_t>(p.ramp)), p.along + kRampApron, p.ramp}};
    std::vector<RampWay> out;
    for (size_t s = 0; s < rp.ramps.size(); ++s) {
        const auto& [foot, head] = rp.ramps[s];
        const Vec3 side = onWalk(rp, head);
        Fixed along;
        ringRoute(rp, at, side, &along);
        out.push_back({apronOf(rp, s), along + (head - side).flat().length() + (head - foot).flat().length() + kRampApron, static_cast<int>(s)});
    }
    return out;
}
// Roughly how far it is on foot from x to y: straight, but out or in by the
// gate of each ring of walls between them (the nearest first).
Fixed groundEstimate(const Region& rg, Vec3 x, const Vec3& y) {
    const auto& gs = rg.gates();
    const size_t n = std::min<size_t>(gs.size(), 64);
    Fixed total;
    uint64_t used = 0;
    for (size_t k = 0; k < n; ++k) {
        size_t best = n;
        Fixed bestD;
        for (size_t i = 0; i < n; ++i) {
            if ((used >> i & 1) != 0) continue;
            const Rampart& ring = rg.ramparts()[static_cast<size_t>(gs[i].ring)];
            if (Gate::inside(ring, x) == Gate::inside(ring, y)) continue;
            const Fixed d = ((gs[i].a + gs[i].b) * Fixed::fromRatio(1, 2) - x).flat().length();
            if (best == n || d < bestD) {
                best = i;
                bestD = d;
            }
        }
        if (best == n) break;
        used |= uint64_t{1} << best;
        // Through it: out beyond the doors, or in past the passage.
        const Gate& g = gs[best];
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
        const Vec3 c = (g.a + g.b) * Fixed::fromRatio(1, 2);
        const Fixed through = Gate::inside(ring, x) ? Fixed::one() : -(ring.halfThick * 2 + Fixed::one());
        total += bestD + abs(through);
        x = c + g.out * through;
    }
    return total + (y - x).flat().length();
}
}  // namespace

bool World::planRampartPath(Unit& u, const Vec3& to) {
    // Up the ramps to the wall-walks, along them and down again, and round by
    // the gates on the ground between: never off a walk but down a ramp.
    // Horses, wolves, wheels and the largest beasts do not go up on the walls.
    const Region& rg = *config_.region;
    if (!climbsRamps(u.stats)) return false;
    const Vec3 from = u.pos;
    const Perch a = perchOf(rg, from, true), b = perchOf(rg, to, false);
    if (a.ring < 0 && b.ring < 0) return false;
    if (a.ring >= 0 && a.ring == b.ring && a.ramp < 0 && b.ramp < 0) {
        // Along the walk.
        const Rampart& rp = rg.ramparts()[static_cast<size_t>(a.ring)];
        u.path = keepRight(rp, from, ringRoute(rp, from, to, nullptr));
        return true;
    }
    // Up on the same ring at both ends, a ramp at one or both: by its walk,
    // unless down one ramp and over the ground to another is shorter.
    Fixed upper = -Fixed::one();
    if (a.ring >= 0 && a.ring == b.ring) {
        const Rampart& rp = rg.ramparts()[static_cast<size_t>(a.ring)];
        if (a.ramp >= 0 && a.ramp == b.ramp) {
            upper = (to - from).flat().length();
        } else {
            Vec3 p = from, q = to;
            Fixed cost;
            if (a.ramp >= 0) {
                const Vec3 head = rp.ramps[static_cast<size_t>(a.ramp)].second;
                p = onWalk(rp, head);
                cost += (head - from).flat().length() + (p - head).flat().length();
            }
            if (b.ramp >= 0) {
                const Vec3 head = rp.ramps[static_cast<size_t>(b.ramp)].second;
                q = onWalk(rp, head);
                cost += (head - q).flat().length() + (to - head).flat().length();
            }
            Fixed along;
            ringRoute(rp, p, q, &along);
            upper = cost + along;
        }
    }
    // Down to the ground and up again: the ramp to go down by (along the walk
    // to it, or the one he is on) and the one to go up by, the pair that makes
    // the whole way shortest, the ground between reckoned through the gates.
    const std::vector<RampWay> downs = rampWays(rg, a, from), ups = rampWays(rg, b, to);
    size_t down = 0, up = 0;
    Fixed lower = -Fixed::one();
    for (size_t i = 0; i < downs.size(); ++i) {
        for (size_t j = 0; j < ups.size(); ++j) {
            const Fixed cost = downs[i].cost + groundEstimate(rg, downs[i].foot, ups[j].foot) + ups[j].cost;
            if (lower >= Fixed::zero() && cost >= lower) continue;
            lower = cost;
            down = i;
            up = j;
        }
    }
    if (upper < Fixed::zero() && lower < Fixed::zero()) return false;
    if (upper >= Fixed::zero() && (lower < Fixed::zero() || upper <= lower)) {
        const Rampart& rp = rg.ramparts()[static_cast<size_t>(a.ring)];
        if (a.ramp >= 0 && a.ramp == b.ramp) {
            u.path = {to};  // along the ramp
            return true;
        }
        Vec3 p = from;
        if (a.ramp >= 0) {
            // Up the ramp he is on, and onto the walk.
            const Vec3 head = rp.ramps[static_cast<size_t>(a.ramp)].second;
            p = onWalk(rp, head);
            u.path.push_back(head);
            u.path.push_back(p);
        }
        const Vec3 q = b.ramp >= 0 ? onWalk(rp, rp.ramps[static_cast<size_t>(b.ramp)].second) : to;
        const std::vector<Vec3> walk = keepRight(rp, p, ringRoute(rp, p, q, nullptr));
        u.path.insert(u.path.end(), walk.begin(), walk.end());
        if (b.ramp >= 0) {
            // Off the walk onto the ramp, and down it.
            u.path.push_back(rp.ramps[static_cast<size_t>(b.ramp)].second);
            u.path.push_back(to);
        }
        return true;
    }
    const RampWay& off = downs[down];
    const RampWay& on = ups[up];
    if (a.ring >= 0) {
        // Along the walk to the head of a ramp, down it, and off its foot.
        const Rampart& rp = rg.ramparts()[static_cast<size_t>(a.ring)];
        const auto& [foot, head] = rp.ramps[static_cast<size_t>(off.ramp)];
        if (a.ramp < 0) {
            const Vec3 side = onWalk(rp, head);
            u.path = keepRight(rp, from, ringRoute(rp, from, side, nullptr));
            u.path.push_back(head);
        }
        u.path.push_back(foot);
        u.path.push_back(off.foot);
    }
    // Over the ground, out and in by the gates (and round a ramp's foot).
    const size_t leg = u.path.size();
    groundPath(off.foot, on.foot, u.path, u.stats.gait);
    if (Vec3 corner; a.ring >= 0 && footCorner(rg.ramparts()[static_cast<size_t>(a.ring)], static_cast<size_t>(off.ramp), u.path[leg], corner))
        u.path.insert(u.path.begin() + static_cast<long>(leg), corner);
    if (b.ring >= 0) {
        // Onto the ramp by its foot, up it (and along the walk).
        const Rampart& tr = rg.ramparts()[static_cast<size_t>(b.ring)];
        const auto& [foot, head] = tr.ramps[static_cast<size_t>(on.ramp)];
        const Vec3 before = u.path.size() >= 2 ? u.path[u.path.size() - 2] : from;
        if (Vec3 corner; footCorner(tr, static_cast<size_t>(on.ramp), before, corner)) u.path.insert(u.path.end() - 1, corner);
        u.path.push_back(foot);
        if (b.ramp >= 0) {
            u.path.push_back(to);
        } else {
            const Vec3 side = onWalk(tr, head);
            u.path.push_back(head);
            u.path.push_back(side);
            const std::vector<Vec3> walk = keepRight(tr, side, ringRoute(tr, side, to, nullptr));
            u.path.insert(u.path.end(), walk.begin(), walk.end());
        }
    }
    return true;
}

bool World::walk(Unit& u, const Vec3& to, Fixed speedScale) {
    const Vec3 delta = (to - u.pos).flat();
    const Fixed dist = delta.length();
    Fixed speed = u.stats.moveSpeed * speedScale * gaitSpeed(u.stats.gait, groundAt(u.pos.x, u.pos.y));
    if (speed == Fixed::zero()) speed = u.stats.moveSpeed / 4;  // wading out of deep water
    if (steeperThan(u.pos.x, u.pos.y, kSteepSlope)) speed *= kSteepSpeed;
    if (dist == Fixed::zero()) return true;
    u.facing = delta / dist;
    // Uphill is slow going; downhill a little quicker.
    const Fixed here = standingHeight(u.pos.x, u.pos.y, u.pos.z);
    {
        const Fixed probe = Fixed::one();
        const Fixed grade = standingHeight(u.pos.x + u.facing.x * probe, u.pos.y + u.facing.y * probe, u.pos.z) - here;
        if (grade > Fixed::zero()) speed = speed / (Fixed::one() + grade * kClimbCost * gaitClimbPct(u.stats.gait) / 100);
        else speed = speed * (Fixed::one() + min(kDescentMax, -grade * kDescentGain));
    }
    const Fixed stepLen = speed * kTickDt;
    Vec3 next = dist <= stepLen ? Vec3{to.x, to.y, u.pos.z} : u.pos + u.facing * stepLen;
    // Deep water and cliffs stop walkers: slide along if one axis is clear.
    const auto barred = [&](Fixed x, Fixed y) {
        const Ground g = groundAt(x, y);
        if (g == Ground::DeepWater && groundAt(u.pos.x, u.pos.y) != Ground::DeepWater) return true;
        // Over the water beside a narrow deck: off the edge is the river.
        Fixed deck;
        if (g == Ground::Bridge && config_.region && !config_.region->deckAt(x, y, deck) && config_.region->underWater(x, y)) return true;
        const Fixed dh = standingHeight(x, y, u.pos.z) - here;
        // Off the end of a bridge deck onto the bank is a step down, never a ledge.
        const auto offDeckEnd = [&] {
            if (!config_.region || dh < -kMaxStepDown * 2) return false;
            for (const uint32_t ki : config_.region->decksAt(u.pos.x, u.pos.y)) {
                const Deck& k = config_.region->decks()[ki];
                if (k.raised || !k.contains(u.pos.x, u.pos.y)) continue;
                const Fixed dx = x - k.centre.x, dy = y - k.centre.y;
                if (abs(dx * k.dir.x + dy * k.dir.y) > k.halfSpan && abs(dx * k.dir.y - dy * k.dir.x) <= k.halfWidth) return true;
            }
            return false;
        };
        const bool deckEnd = offDeckEnd();
        if (dh > kMaxStepUp || (dh < -kMaxStepDown && !deckEnd)) return true;  // a ledge: off a wall-walk, up the side of a ramp
        if (deckEnd) return false;  // off a bridge's end onto its bank: a step down, never a cliff
        if (config_.region && config_.region->inRamp(x, y, u.pos.z + kMaxStepUp)) return true;  // into a ramp's side, below its slope
        // Onto or along a deck is never a cliff, whatever lies below its edges.
        if (g == Ground::Bridge || (config_.region && config_.region->deckBelow(x, y, u.pos.z + kMaxStepUp, deck))) return false;
        const Fixed run = (Vec3{x, y, Fixed::zero()} - u.pos).flat().length();
        return run > Fixed::zero() && abs(dh) > run * kCliffGrade && steeperThan(x, y, kCliffGrade);
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

bool World::followPath(Unit& u, Fixed pace) {
    if (u.path.empty()) return true;
    const Vec3& waypoint = u.path[std::min<size_t>(u.pathIndex, u.path.size() - 1)];
    const bool last = u.pathIndex + 1 >= u.path.size();
    const bool arrived = walk(u, waypoint, pace);
    bool onward = false;
    // A ramp's foot and the walk's lanes (their heights given) are reached,
    // not cut: beside a ramp is not on it.
    const bool raised = waypoint.z != Fixed::zero();
    // The end of a bridge's deck is reached, not cut: a deck may be no wider than a man and a corner cut to the next waypoint
    // from beside it leaves the deck's width (the water beside it is a drop).
    bool deckEnd = false;
    if (!raised && !last && config_.region && !config_.region->decks().empty()) {
        for (const uint32_t ki : config_.region->decksAt(waypoint.x, waypoint.y)) {
            const Deck& k = config_.region->decks()[ki];
            if (k.raised || !k.contains(waypoint.x, waypoint.y) || k.contains(u.pos.x, u.pos.y)) continue;  // (not for a man already on that deck)
            const Vec3 d = (waypoint - k.centre).flat();
            deckEnd = deckEnd || abs(d.x * k.dir.x + d.y * k.dir.y) > k.halfSpan - Fixed::fromRatio(3, 5);
        }
    }
    if (deckEnd && !arrived && (waypoint - u.pos).flat().lengthSq() < Fixed::one()) onward = true;
    // (A body wider than the walk's lane is held off the parapet and cannot centre on it: his own breadth is near enough.)
    const Fixed raisedReach = max(kRaisedReach, u.stats.radius);
    if (raised && !last && !arrived && (waypoint - u.pos).flat().lengthSq() < raisedReach * raisedReach) onward = true;
    if (!last && !arrived && !raised && !deckEnd && (waypoint - u.pos).flat().lengthSq() < kWaypointNear * kWaypointNear) {
        const Vec3& next = u.path[u.pathIndex + 1];
        onward = (waypoint - u.pos).flat().lengthSq() < kWaypointReach * kWaypointReach ||
                 (next - u.pos).flat().lengthSq() < (next - waypoint).flat().lengthSq();
        if (onward && nav_) {
            // ... unless that cuts over slower ground than the way by the waypoint.
            Fixed slowest = Fixed::fromInt(100), via = Fixed::fromInt(100);
            nav_->travelTime(u.pos, next, u.stats.gait, &slowest);
            nav_->travelTime(waypoint, next, u.stats.gait, &via);
            onward = slowest >= via && nav_->clearLine(u.pos, next, u.stats.gait);
            // By a site's stone the next leg may be clear only from the
            // waypoint itself, which a file's men cannot all stand on: within
            // a man's breadth of it is there.
            const Fixed breadth = u.stats.radius * 2;
            if (!onward && slowest >= via && nav_->nearSite(waypoint) && (waypoint - u.pos).flat().lengthSq() < breadth * breadth) onward = true;
        }
    }
    if (!last && (arrived || onward)) {
        // The next leg of the route begins (the leash is measured from it).
        u.routeFrom = waypoint;
        ++u.pathIndex;
        return false;
    }
    return last && arrived;
}

void World::updateUnit(Unit& u) {
    // A satchel in a scavenger's hands burns on whatever he is doing.
    if (u.scav.satchel) burnHeld(u);
    // Not raiding, he throws it before it goes off in his hands.
    if (u.scav.satchel && !u.scav.raiding && u.targetUnit == kNoEntity && u.alive() && !u.inAir()) shedBurning(u);
    if (u.airborne) {
        updateAirborne(u);
        return;
    }
    if (!u.alive()) return;
    if (u.reloadRemaining > 0) --u.reloadRemaining;
    if (u.meleeCooldown > 0) --u.meleeCooldown;
    if (u.healCooldown > 0) --u.healCooldown;
    if (u.layCooldown > 0) --u.layCooldown;
    u.engaged = false;
    const EntityId lastFoe = u.foe;
    u.foe = kNoEntity;

    const bool flier = u.stats.flyAltitude > Fixed::zero();
    if (flier && u.flight != Unit::Flight::Grounded) {
        updateFlight(u);
        return;
    }
    // Resting on the ground restores a flier at half the rate flying spends it.
    if (flier && u.stamina < u.stats.staminaTicks && tick_ % kFlierRestTicks == 0) ++u.stamina;

    // Broken, he runs from the foes near him; but an order given him since is obeyed.
    if (u.routed && !u.hasMoveTarget && !u.hasAttackTarget && u.targetUnit == kNoEntity && u.attackGate < 0) {
        flee(u);
        u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
        return;
    }

    if (const int gate = gateToHack(u); gate >= 0) {
        // At a shut gate on the way in: hack at the timbers.
        hackGate(u, gate);
    } else if (u.targetUnit != kNoEntity) {
        // Ordered at one soldier: after him, wherever he goes (a scavenger
        // with something in hand throws it at him instead). A flier goes over the walls and the miles as for a move.
        const Unit* quarry = findUnit(u.targetUnit);
        if (flier && quarry != nullptr && quarry->alive() && !quarry->inAir() && takesWing(u, quarry->pos)) {
            u.flight = Unit::Flight::TakingOff;
            u.runway = Fixed::zero();
            updateFlight(u);
            return;
        }
        if (!scavengeOrdered(u)) pursue(u);
    } else if (u.hasMoveTarget) {
        // Marching with initiative: he answers whoever strikes him or
        // crosses his path, on a leash from his route (world_orders.cpp).
        if (marchInitiative(u)) {
            u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
            return;
        }
        if (flier && takesWing(u, u.moveTarget)) {
            u.flight = Unit::Flight::TakingOff;
            u.runway = Fixed::zero();
            updateFlight(u);
            return;
        }
        if (Vec3 spot; placeAtGate(u, spot)) {
            walk(u, spot);  // up to the doors beside the others, to hack at them too
            u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
            return;
        }
        const GateHold held = heldAtGate(u);
        if (held == GateHold::Wait) {
            u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);  // (the doors are swinging open for him)
            return;
        }
        if (held == GateHold::Halt) {
            // As near as he can get: the gate is shut to him.
            u.hasMoveTarget = false;
            u.layOrdered = false;
            u.path.clear();
            u.pathIndex = 0;
            u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
            return;
        }
        if (u.path.empty()) u.path.push_back(u.moveTarget);
        // A squad marches at its slowest member's pace, keeping its formation.
        const Fixed pace = u.marchSpeed > Fixed::zero() && u.marchSpeed < u.stats.moveSpeed ? u.marchSpeed / u.stats.moveSpeed : Fixed::one();
        if (followPath(u, pace)) {
            u.hasMoveTarget = false;
            u.path.clear();
            if (u.arriveFacing != Vec3{}) u.facing = u.arriveFacing;
            // Sent to lay a satchel: here, the way he came (world_satchels.cpp).
            if (u.layOrdered) {
                u.layOrdered = false;
                laySatchel(u, u.pos + u.facing);
            }
        }
    } else if (u.hasAttackTarget) {
        u.facing = flatDirection(u.pos, u.attackTarget, u.facing);
        const Fixed range = (u.attackTarget - u.pos).flat().length();
        if (range > shotRange(u, u.stats.ranged.maxRange, u.attackTarget)) {
            walk(u, u.attackTarget);  // close to range before loosing
        } else if (range >= u.stats.ranged.minRange && readyToLoose(u)) {
            Vec3 aim = u.attackTarget;
            const bool atWalk = u.attackGate < 0 && aimHigh(u, aim, Fixed::fromRatio(9, 5));  // a man up there
            if (fire(u, aim, atWalk)) u.reloadRemaining = attackTicks(u, u.stats.ranged.reloadTicks);
        }
    } else if (u.scav.raiding) {
        // A scavenger let loose runs his own fight (world_scavenge.cpp); else
        // he holds and fights like anyone.
        raid(u);
    } else if (u.satchels > 0 && layInPath(u)) {
        // The enemy coming: a satchel in their path, then back from it (world_satchels.cpp).
    } else if (tryHeal(u)) {
        // Healing a wounded friend takes priority over fighting.
    } else if (Unit* enemy = u.stats.melee.damage > 0 ? meleeTarget(u, lastFoe) : nullptr; enemy != nullptr && !outruns(u, *enemy)) {
        // (Not one he cannot catch: shieldTarget, below.)
        fight(u, *enemy);
    } else if (u.stats.ranged.kind == ProjectileKind::Molotov && shootSatchel(u)) {
        // Fire on a satchel lying among the enemy, before any one man.
    } else if (Unit* target = u.stats.ranged.autoRange > Fixed::zero()
                                  ? nearestEnemy(u, u.stats.ranged.autoRange * 3 / 2)
                                  : nullptr;
               target != nullptr && (target->pos - u.pos).flat().length() <= shotRange(u, u.stats.ranged.autoRange, target->pos)) {
        // From high ground the bow carries farther (shotRange).
        u.facing = flatDirection(u.pos, target->pos, u.facing);
        if ((target->pos - u.pos).flat().length() >= u.stats.ranged.minRange && readyToLoose(u)) {
            Vec3 aim = target->pos;
            const bool atWalk = aimHigh(u, aim, target->stats.height);
            if (fire(u, aim, atWalk)) u.reloadRemaining = attackTicks(u, u.stats.ranged.reloadTicks);
        }
    } else if (Unit* t = shieldTarget(u, enemy)) {
        // Struck at by one he cannot reach, or one he cannot catch at hand:
        // his shield to him, holding his place (world_scavenge.cpp).
        turnToward(u, flatDirection(u.pos, t->pos, u.facing));
    }
    u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
}

bool World::takesWing(const Unit& u, const Vec3& goal) const {
    if (u.stamina * 2 < u.stats.staminaTicks) return false;
    if ((goal - u.pos).flat().length() >= u.stats.minFlight) return true;  // a long way
    if (!config_.region || config_.region->ramparts().empty()) return false;
    const Region& rg = *config_.region;
    const bool goalOnWalk = rg.rampartAt(goal.x, goal.y, groundHeight(goal.x, goal.y)) >= 0;
    const bool onWalk = rg.rampartAt(u.pos.x, u.pos.y, u.pos.z) >= 0;
    // Up onto a wall-walk from below, and down off it (they do not use the ramps: a wall is a hop, not a mile of stairs).
    if (goalOnWalk != onWalk) return true;
    // Over a curtain wall, in or out (a flier does not queue at a gate, or stop at one that is shut to him).
    for (const Vec3& end : {u.pos, goal})
        for (const uint32_t gi : rg.gatesNear(end.x, end.y)) {
            const Rampart& ring = rg.ramparts()[static_cast<size_t>(rg.gates()[gi].ring)];
            if (Gate::inside(ring, u.pos) != Gate::inside(ring, goal)) return true;
        }
    return false;
}

void World::updateFlight(Unit& u) {
    // His goal: the place he was sent to, or the man he is after (stopping a few metres short of him).
    const Unit* quarry = u.hasMoveTarget || u.targetUnit == kNoEntity ? nullptr : findUnit(u.targetUnit);
    if (quarry != nullptr && !quarry->alive()) quarry = nullptr;
    const bool hasGoal = u.hasMoveTarget || quarry != nullptr;
    Vec3 goal = u.hasMoveTarget ? u.moveTarget : u.pos;
    if (quarry != nullptr) {
        const Vec3 to = (quarry->pos - u.pos).flat();
        const Fixed len = to.length();
        const Fixed standoff = min(len, u.stats.melee.reach + u.stats.radius + quarry->stats.radius);
        goal = len > Fixed::zero() ? quarry->pos - to * (standoff / len) : quarry->pos;
    }
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
            walk(u, u.pos + u.facing * Fixed::fromInt(50), kTakeoffRunPace);
            const Fixed ran = (u.pos - before).flat().length();
            u.runway += ran;
            // Up on a wall-walk (or any deck) he needs no runway: he steps off the parapet and the air takes him. And a run
            // that is blocked outright (a wall at his beak, a crowd) lifts him where he stands, rather than leaving him
            // running at it for ever (the flier "stuck" on the walk, running at the merlons).
            const bool onDeck = u.pos.z > (config_.terrain ? config_.terrain->heightAt(u.pos.x, u.pos.y) : Fixed::zero()) + kKickReach * 4;
            u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
            if (u.runway >= u.stats.takeoffRun || onDeck || ran == Fixed::zero()) u.flight = Unit::Flight::Flying;
            break;
        }
        case Unit::Flight::Flying: {
            u.pos += u.facing * min(u.stats.flySpeed * kTickDt, dist);
            const Fixed below = groundHeight(u.pos.x, u.pos.y);
            u.pos.z = min(u.pos.z + kClimbRate * kTickDt, below + u.stats.flyAltitude);
            u.pos.z = max(u.pos.z, below);
            --u.stamina;
            if (u.stamina <= 0 || dist < u.stats.flyAltitude * kLandingGlide || !hasGoal) {
                u.flight = Unit::Flight::Landing;
            }
            break;
        }
        case Unit::Flight::Landing: {
            u.pos += u.facing * min(u.stats.flySpeed / 2 * kTickDt, dist);
            const Fixed below = groundHeight(u.pos.x, u.pos.y);
            u.pos.z = max(u.pos.z - kDescentRate * kTickDt, below);
            if (u.stamina > 0) --u.stamina;
            // Come down over a wall or a tower on the way (a deck, not the ground), he skims it and goes on: he lands on his goal,
            // not on the first battlement he crosses (from which he would only rise again for the ground beyond).
            const auto onDeck = [&](const Vec3& p, Fixed height) {
                return height > (config_.terrain ? config_.terrain->heightAt(p.x, p.y) : Fixed::zero()) + kLandedReach / 6;
            };
            const bool skimming = onDeck(u.pos, below) && !onDeck(goal, groundHeight(goal.x, goal.y));
            if (u.pos.z <= below && !skimming) {
                u.flight = Unit::Flight::Grounded;
                // Walk the rest of the way (a tired flier may have come down early).
                // Come down on his goal, he steps the last few metres straight (no ramps and gates for a man who has just
                // dropped onto a wall-walk, where the walker's route would lead him round by the stairs).
                if (u.hasMoveTarget) {
                    if ((u.moveTarget - u.pos).flat().length() <= kLandedReach) u.path = {u.moveTarget}, u.pathIndex = 0;
                    else planPath(u, u.moveTarget);
                } else {
                    u.path.clear();  // (after a man, pursue plans afresh)
                    u.pathIndex = 0;
                }
            }
            break;
        }
    }
    (void)ground;
}

void World::separateBodies() {
    // Bodies have size and mass: living soldiers push each other apart (the
    // lighter gives way) and are kept out of scenery. The dead lie where they
    // fell and are walked over; only a blast or a blow moves them (knock).
    unitGrid_.clear();
    for (uint32_t i = 0; i < units_.size(); ++i) {
        const Unit& u = units_[i];
        if (u.alive() && !u.inAir() && u.flight == Unit::Flight::Grounded) unitGrid_.insert(i, u.pos.x, u.pos.y);
    }
    unitGrid_.build();
    // The pairs: where each stands and how wide, in the grid's order and in step with the soldiers (a push moves both),
    // so that the many that are near but not touching are told apart by reading one run of numbers, not the soldiers.
    const Fixed maxRadius = Fixed::fromInt(3);
    const std::vector<uint32_t>& items = unitGrid_.items();
    bodies_.resize(items.size());
    bodyOf_.resize(units_.size());
    for (uint32_t m = 0; m < items.size(); ++m) {
        const Unit& u = units_[items[m]];
        bodies_[m] = {u.pos.x, u.pos.y, u.stats.radius, items[m]};
        bodyOf_[items[m]] = m;
    }
    for (uint32_t i = 0; i < units_.size(); ++i) {
        Unit& a = units_[i];
        if (!a.alive() || a.inAir()) continue;
        const Fixed radius = a.stats.radius;
        unitGrid_.queryRuns(a.pos.x, a.pos.y, radius + maxRadius, [&](uint32_t first, uint32_t last) {
            for (uint32_t m = first; m < last; ++m) {
                const Body& body = bodies_[m];
                const uint32_t j = body.index;
                const Vec3 d{body.x - a.pos.x, body.y - a.pos.y, Fixed::zero()};
                const Fixed reach = radius + body.radius;
                const Fixed distSq = d.lengthSq();
                if (j <= i || distSq >= reach * reach) continue;  // each pair once, and only the touching
                Unit& b = units_[j];
                Fixed dist = sqrt(distSq);
                Vec3 n = dist > Fixed::zero() ? d / dist : Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()};
                const Fixed overlap = reach - dist;
                const Fixed total = a.stats.mass + b.stats.mass;
                a.pos -= n * (overlap * b.stats.mass / total);
                b.pos += n * (overlap * a.stats.mass / total);
                bodies_[m].x = b.pos.x;
                bodies_[m].y = b.pos.y;
            }
        });
    }
    if (!config_.region) return;
    const auto& obstacles = config_.region->obstacles();
    std::vector<uint32_t> nearWalls, nearFloors;
    for (Unit& u : units_) {
        if (!u.present() || u.inAir() || u.flight != Unit::Flight::Grounded) continue;
        // From standing stone he keeps his drawn breadth, from the rest his radius.
        const Fixed standoff = stoneStandoff(u.stats.radius);
        bool offStone = false;  // pushed off standing stone (or a ruin's base)
        const auto offObstacles = [&](bool stoneOnly) {
            obstacleGrid_.queryNear(u.pos, standoff + obstacleReach_, max(standoff, u.stats.radius), [&](uint32_t k) {
                const Obstacle& o = obstacles[k];
                if (stoneOnly && !o.standingStone()) return;
                const Fixed keep = o.standingStone() ? standoff : u.stats.radius;
                const bool moved = o.kind == Obstacle::Kind::House ? outOfBox(u.pos, o.pos, o.halfX, o.halfY, keep) : outOfDisc(u.pos, o.pos, o.radius, keep);
                offStone = offStone || (moved && o.standingStone());
            });
        };
        // A ruin's platform stands on its stepped base, solid to the ground:
        // one on the ground keeps his standoff from the base (one on the
        // stair is climbing into it).
        const auto offBases = [&] {
            // (None within reach of him: nothing to do; else every one in turn, for a push can bring him within reach of the next.)
            config_.region->floorBasesNear(u.pos.x - standoff, u.pos.y - standoff, u.pos.x + standoff, u.pos.y + standoff, nearFloors);
            if (nearFloors.empty()) return;
            for (const Floor& f : config_.region->floors()) {
                if (u.pos.z + kMaxStepUp >= f.baseHi.z || config_.region->decks()[f.stair].contains(u.pos.x, u.pos.y)) continue;
                offStone = outOfBox(u.pos, (f.baseLo + f.baseHi) * Fixed::fromRatio(1, 2), (f.baseHi.x - f.baseLo.x) / 2, (f.baseHi.y - f.baseLo.y) / 2, standoff) || offStone;
            }
        };
        offObstacles(false);
        keepOutOfGates(u);
        // Castle walls: out of the slab, unless walking along its top (or
        // under the stone over a gate passage).
        config_.region->wallsNear(u.pos.x - Fixed::fromInt(4), u.pos.y - Fixed::fromInt(4), u.pos.x + Fixed::fromInt(4), u.pos.y + Fixed::fromInt(4), nearWalls);
        for (const uint32_t wi : nearWalls) {
            const WallSeg& wl = config_.region->walls()[wi];
            if (wl.walk > Fixed::zero() ? u.pos.z >= wl.walk - Fixed::fromRatio(1, 2) : u.pos.z > wl.top - kWallClear) continue;
            if (u.pos.z + u.stats.height <= wl.a.z) continue;
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
        offBases();
        // A ruin's stair is solid to the ground too: one shoved in under it is
        // put back out by the nearest side.
        const bool stairHere = !config_.region->floorStairsAt(u.pos.x, u.pos.y).empty();
        for (const Floor& f : config_.region->floors()) {
            if (!stairHere) break;  // (none holds him: and if one does, all are asked in turn, a push can bring another to hold him)
            const Deck& d = config_.region->decks()[f.stair];
            if (!d.contains(u.pos.x, u.pos.y) || u.pos.z + kMaxStepUp >= d.heightAt(u.pos.x, u.pos.y)) continue;
            const Vec3 across{-d.dir.y, d.dir.x, Fixed::zero()}, rel = (u.pos - d.centre).flat();
            const Fixed along = rel.dot(d.dir), side = rel.dot(across);
            if (d.halfSpan - abs(along) < d.halfWidth - abs(side))
                u.pos += d.dir * ((along < Fixed::zero() ? -d.halfSpan - standoff : d.halfSpan + standoff) - along);
            else
                u.pos += across * ((side < Fixed::zero() ? -d.halfWidth - standoff : d.halfWidth + standoff) - side);
        }
        // Pushed off one stone into another's way (rubble by the base, a
        // column by a wall): once more off them all.
        if (offStone) {
            offObstacles(true);
            offBases();
        }
    }
    for (Unit& u : units_) {
        if (u.present()) keepOnWalk(u);  // the thrown too
    }
    for (Unit& u : units_) {
        if (u.present() && !u.inAir() && u.flight == Unit::Flight::Grounded) u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
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
        spawnDebris(r.kind, r.owner, r.pos + up(Fixed::fromRatio(1, 10)), vel, r.source);
        clearRemain(slot);
    }
}

Fixed World::obstacleHit(const Vec3& a, const Vec3& b, Fixed limitT, bool masonryOnly, bool* stone) const {
    if (!config_.region) return -Fixed::one();
    const auto& obstacles = config_.region->obstacles();
    const Vec3 seg = b - a;
    const Vec3 segFlat = seg.flat();
    const Fixed lenSq = segFlat.lengthSq();
    const Vec3 mid = a + seg * Fixed::fromRatio(1, 2);
    Fixed best = limitT;
    bool hit = false;
    obstacleGrid_.query(mid.x, mid.y, sqrt(lenSq) / 2 + obstacleReach_, [&](uint32_t k) {
        const Obstacle& o = obstacles[k];
        if (masonryOnly && o.kind != Obstacle::Kind::House && o.kind != Obstacle::Kind::Tower && o.kind != Obstacle::Kind::Pillar) return;
        const Fixed radius = missileRadius(o);
        Fixed t;
        if (lenSq > Fixed::zero()) t = clamp((o.pos - a).flat().dot(segFlat) / lenSq, Fixed::zero(), Fixed::one());
        if (t >= best) return;
        const Vec3 p = a + seg * t;
        if ((p - o.pos).flat().lengthSq() > radius * radius) return;
        if (p.z < o.pos.z || p.z > o.pos.z + o.height) return;
        best = t;
        hit = true;
        if (stone) *stone = o.kind != Obstacle::Kind::Pine && o.kind != Obstacle::Kind::Oak && o.kind != Obstacle::Kind::Log;
    });
    // Castle walls: their stone, open at the embrasures (world_walls.cpp).
    const Fixed wall = wallHit(a, b, best);
    if (wall >= Fixed::zero()) {
        best = wall;
        hit = true;
        if (stone) *stone = true;
    }
    const Fixed gate = gateHit(a, b, best);
    if (gate >= Fixed::zero()) {
        if (stone) *stone = false;  // oak
        return gate;
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
    Slot* found = find(key(cellOf(x), cellOf(y)));
    if (found == nullptr) return;
    auto& v = found->items;
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
        u.reloadRemaining = std::max(u.reloadRemaining, recoverTicks(u));
        u.meleeCooldown = std::max(u.meleeCooldown, recoverTicks(u));
    }
}

void World::buildBroad() {
    int64_t x0 = kBroadCells, x1 = -1, y0 = kBroadCells, y1 = -1;
    for (const Unit& u : units_) {
        if (!u.alive()) continue;
        const int64_t cx = broadCell(u.pos.x.floorToInt()), cy = broadCell(u.pos.y.floorToInt());
        x0 = std::min(x0, cx);
        x1 = std::max(x1, cx);
        y0 = std::min(y0, cy);
        y1 = std::max(y1, cy);
    }
    broadX0_ = x0 > x1 ? 0 : x0;
    broadY0_ = y0 > y1 ? 0 : y0;
    broadW_ = x0 > x1 ? 0 : x1 - x0 + 1;
    broadH_ = y0 > y1 ? 0 : y1 - y0 + 1;
    const auto slot = [&](const Unit& u) {
        return static_cast<size_t>((broadCell(u.pos.y.floorToInt()) - broadY0_) * broadW_ + broadCell(u.pos.x.floorToInt()) - broadX0_);
    };
    broadStart_.assign(static_cast<size_t>(broadW_ * broadH_) + 1, 0);
    uint32_t count = 0;
    for (const Unit& u : units_)
        if (u.alive()) {
            ++broadStart_[slot(u) + 1];
            ++count;
        }
    for (size_t c = 0; c + 1 < broadStart_.size(); ++c) broadStart_[c + 1] += broadStart_[c];
    broadItems_.resize(count);
    broadSide_.resize(count);
    broadFill_.assign(broadStart_.begin(), broadStart_.end() - 1);
    for (uint32_t i = 0; i < units_.size(); ++i)
        if (units_[i].alive()) {
            const uint32_t k = broadFill_[slot(units_[i])]++;
            broadItems_[k] = i;
            broadSide_[k] = units_[i].owner;
        }
    broadValid_ = true;
}

Unit* World::nearestEnemy(const Unit& u, Fixed range) {
    if (range <= Fixed::zero()) return nullptr;
    // Melee-only units ignore enemies flying above their reach (giants can swat low fliers).
    const bool groundBound = u.stats.ranged.muzzleSpeed == Fixed::zero();
    const Fixed reachUp = u.stats.height + u.stats.melee.reach;
    Unit* best = nullptr;
    Fixed bestDistSq = range * range;
    uint32_t bestIndex = 0;
    // The nearest, and of those as near the lowest id (the soldiers come in any order).
    nearbyNotOf(u.owner, u.pos.x, u.pos.y, range, [&](uint32_t i) {
        Unit& other = units_[i];
        if (!other.alive() || other.owner == u.owner) return;
        const Fixed d = (other.pos - u.pos).flat().lengthSq();
        if (d > bestDistSq) return;  // (too far: no need to look at the height)
        // (Above the land itself, or above the soldier: a foe up a hillside is fair game.)
        if (groundBound) {
            const Fixed land = config_.terrain ? config_.terrain->heightAt(other.pos.x, other.pos.y) : Fixed::zero();
            if (other.pos.z - max(u.pos.z, land) > reachUp) return;
        }
        if (best == nullptr || d < bestDistSq || i < bestIndex) {
            best = &other;
            bestDistSq = d;
            bestIndex = i;
        }
    });
    return best;
}

bool World::shieldBlocks(const Unit& target, const Vec3& incomingDir, Fixed chanceScale) {
    if (target.stats.shield.blockChance == Fixed::zero() || !target.alive() || target.airborne) return false;
    const Vec3 in = incomingDir.flat();
    const Fixed len = in.length();
    if (len == Fixed::zero()) return false;
    // The blow comes from the front if it travels against the way the target faces.
    const Fixed facingDot = -(target.facing.dot(in) / len);
    // Never from behind, however wide the shield's arc.
    if (facingDot < target.stats.shield.arcCos || hitSide(target.facing, in) == HitSide::Rear) return false;
    return rng_.nextUnit() < (target.stats.shield.blockChance + kRankBlockBonus * (target.rank + target.blessings)) * chanceScale;
}

void World::knock(Unit& target, const Vec3& dir, Fixed impulse) {
    const Fixed v = impulse / target.stats.mass;
    if (v <= Fixed::one()) return;
    target.vel += dir * v + up(v * kLiftFraction);
    target.airborne = true;
}

bool World::tryHeal(Unit& u) {
    const Healing& h = u.stats.healing;
    if (h.amount == 0 || u.healCharges == 0) return false;
    Unit* best = nullptr;
    Fixed bestDistSq = kHealSeekRange * kHealSeekRange;
    uint32_t bestIndex = 0;
    nearby(u.pos.x, u.pos.y, kHealSeekRange, [&](uint32_t i) {
        Unit& other = units_[i];
        if (!other.alive() || other.owner != u.owner || other.id == u.id || other.inAir()) return;
        if (other.hp > other.stats.maxHp - h.amount / 2) return;
        const Fixed d = (other.pos - u.pos).flat().lengthSq();
        if (d < bestDistSq || (best != nullptr && d == bestDistSq && i < bestIndex)) {
            best = &other;
            bestDistSq = d;
            bestIndex = i;
        }
    });
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

bool World::damage(Unit& target, int32_t amount, Unit* attacker, const Blow& blow) {
    if (!target.alive()) return false;
    if (attacker != nullptr) noteAttack(target, *attacker);
    // A raider shot at knows where from, seen or not (world_scavenge.cpp).
    if (target.scav.raiding && blow.missile && attacker != nullptr && attacker->owner != target.owner) target.scav.shotBy = attacker->id;
    const int32_t dealt = defendedDamage(target, amount);
    const int32_t overkill = dealt - target.hp;
    target.hp = std::max(target.hp - dealt, 0);
    if (target.alive()) return false;
    target.hasMoveTarget = false;
    target.hasAttackTarget = false;
    target.targetUnit = kNoEntity;
    target.engageFoe = kNoEntity;
    deaths_.push_back({target.owner, target.pos});
    if (target.stats.scavenge.sight > Fixed::zero()) dropHeld(target);  // what a scavenger held falls from his hands
    // The satchels a grenadier still carries: fire or a blast sets them all
    // off at once; any other death spills them where he falls (world_satchels.cpp).
    if (target.satchels > 0) packBlast(target, blow.type == DamageType::Explosive || blow.type == DamageType::Fire);
    if (target.stats.type == static_cast<uint8_t>(UnitType::Hero))
        heroDeaths_.push_back({target.id, target.owner, target.pos, attacker != nullptr ? attacker->id : kNoEntity});
    if (attacker != nullptr && attacker->owner != target.owner) creditKill(*attacker);
    resolveDeath(target, blow, overkill, attacker);
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

int32_t World::gracePct(const Unit& u) const {
    // A hero's levels are his strength; blessings (and the artifacts the
    // campaign counts with them) the gods' favour. A soldier's rank is an
    // edge instead (world_edges.cpp), and so is a hero leading him.
    const bool hero = u.stats.type == static_cast<uint8_t>(UnitType::Hero);
    return 100 + (hero ? u.rank * kHeroLevelDamagePct : 0) + u.blessings * kBlessingDamagePct;
}

int32_t World::prowessPct(const Unit& u) const { return gracePct(u) + edgesOf(u).total(); }

bool World::clearFlight(const Vec3& origin, const Vec3& velocity, const Vec3& target, Fixed shortOf) const {
    const Vec3 flat = velocity.flat();
    const Fixed speed = flat.length();
    if (speed <= Fixed::zero()) return true;
    const Fixed reach = max(Fixed::zero(), (target - origin).flat().length() - shortOf);
    const Fixed flight = reach / speed;
    constexpr int kSteps = 16;
    Vec3 prev = origin;
    for (int i = 1; i <= kSteps; ++i) {
        const Fixed t = flight * Fixed::fromRatio(i, kSteps);
        const Vec3 at = origin + velocity * t + up(-config_.gravity * (t * t / 2));
        if (obstacleHit(prev, at, Fixed::one(), true) >= Fixed::zero()) return false;
        prev = at;
    }
    return true;
}

Fixed World::clearingTan(const Vec3& origin, const Vec3& mark, Fixed shortOf) const {
    const Vec3 flat = (mark - origin).flat();
    const Fixed dist = flat.length();
    Fixed need = -Fixed::fromInt(1000);
    const Fixed reach = dist - shortOf;
    if (reach <= Fixed::one()) return need;
    const Vec3 dir = flat / dist;
    const Fixed rise = mark.z - origin.z;
    // An arc through the mark at elevation e stands x tan(e) (1 - x/d) + rise (x/d)^2
    // above the bow x along the way: so over `top` there when tan(e) is at least this.
    const auto over = [&](Fixed x, Fixed top) {
        if (x <= Fixed::zero() || x >= reach) return;
        const Fixed f = x / dist;
        need = max(need, (top + kLoftClear - origin.z - rise * f * f) / (x * (Fixed::one() - f)));
    };
    // The ground, about every metre.
    const int steps = static_cast<int>(clamp(dist, Fixed::fromInt(2), Fixed::fromInt(96)).floorToInt());
    for (int i = 1; i < steps; ++i) {
        const Fixed x = dist * Fixed::fromRatio(i, steps);
        const Vec3 p = origin + dir * x;
        over(x, groundHeight(p.x, p.y));
    }
    if (!config_.region) return need;
    // Rocks, trees and houses across the way: over their near edge, middle and
    // far edge; but not the wood around the mark, which shelters him.
    const auto& obstacles = config_.region->obstacles();
    const Vec3 mid = origin + flat * Fixed::fromRatio(1, 2);
    obstacleGrid_.query(mid.x, mid.y, dist / 2 + obstacleReach_, [&](uint32_t k) {
        const Obstacle& o = obstacles[k];
        const bool green = o.kind == Obstacle::Kind::Pine || o.kind == Obstacle::Kind::Oak || o.kind == Obstacle::Kind::Bush;
        if (green && (o.pos - mark).flat().lengthSq() < kWoodCover * kWoodCover) return;
        const Fixed radius = missileRadius(o);
        const Vec3 rel = (o.pos - origin).flat();
        const Fixed across = abs(cross(rel, dir));
        if (across >= radius) return;
        const Fixed along = rel.dot(dir), half = sqrt(radius * radius - across * across), top = o.pos.z + o.height;
        over(along - half, top);
        over(along, top);
        over(along + half, top);
    });
    // Walls and standing gates the way crosses (not the parapet he shoots through).
    const auto crossing = [&](const Vec3& a, const Vec3& b, Fixed halfThick, Fixed top) {
        const Vec3 ab = (b - a).flat();
        const Fixed turn = cross(dir, ab);
        if (abs(turn) < Fixed::fromRatio(1, 100)) return;  // along the way, not across it
        const Vec3 rel = (a - origin).flat();
        const Fixed x = cross(rel, ab) / turn, s = cross(rel, dir) / turn;
        if (s < Fixed::zero() || s > Fixed::one() || x < kOwnParapet) return;
        over(x - halfThick, top);
        over(x + halfThick, top);
    };
    thread_local std::vector<uint32_t> nearWalls;
    config_.region->wallsNear(min(origin.x, mark.x) - Fixed::one(), min(origin.y, mark.y) - Fixed::one(), max(origin.x, mark.x) + Fixed::one(),
                              max(origin.y, mark.y) + Fixed::one(), nearWalls);
    for (const uint32_t wi : nearWalls) {
        const WallSeg& wl = config_.region->walls()[wi];
        crossing(wl.a, wl.b, wl.halfThick, wl.top);
    }
    const auto& gs = config_.region->gates();
    for (size_t i = 0; i < gs.size() && i < gates_.size(); ++i)
        if (!gates_[i].broken()) crossing(gs[i].a, gs[i].b, gs[i].halfThick, gs[i].top);
    return need;
}

bool World::fire(Unit& u, const Vec3& target, bool atWalk) {
    const RangedWeapon& w = u.stats.ranged;
    Vec3 origin = u.pos + up(u.stats.height * kMuzzleHeightFraction);
    // From a wall-walk, out over the wall only through an embrasure.
    if (!walkShot(u, target, origin)) return false;

    // Pick the aim point uniformly inside the dispersion circle around the target.
    const Fixed range = (target - origin).flat().length();
    Fixed rankFactor = max(Fixed::one() - kDispersionPerRank * u.rank, kMinDispersionFactor);
    if (u.wavering()) rankFactor = rankFactor * 3 / 2;  // shaken hands shoot wide
    Fixed radius = w.dispersionAt100m * range / 100 * rankFactor;
    // Arrows and fireballs go over what is in the way when the flat shot
    // would strike it; shot blind (the mark itself hidden), they scatter more.
    Vec3 lob;
    bool lobbed = false;
    if (w.kind == ProjectileKind::Arrow || w.kind == ProjectileKind::Fireball || w.kind == ProjectileKind::Molotov) {
        // A defender on a wall-walk is shot at direct, over the parapet, or not at all.
        const Vec3 mark = atWalk ? target : target + up(Fixed::one());
        const Fixed shortOf = atWalk ? kClearShortAtWalk : kClearShort;
        if (!clearFlight(origin, solveLowArc(origin, mark, w.muzzleSpeed, config_.gravity).velocity, mark, shortOf)) {
            if (atWalk) return false;
            const LaunchSolution high = solveHighArc(origin, mark, w.muzzleSpeed, config_.gravity);
            if (!high.inRange || !clearFlight(origin, high.velocity, mark, shortOf)) return false;
            lobbed = true;
            lob = high.velocity;
            if (obstacleHit(origin, mark, Fixed::one(), true) >= Fixed::zero()) radius = radius * kBlindDispersion;
        }
    }
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
    if (w.kind == ProjectileKind::Stone) {
        p.vel = solveFixedElevation(origin, aim, kStoneElevation, w.muzzleSpeed, config_.gravity).velocity;
    } else if (lobbed) {
        p.vel = solveHighArc(origin, aim, w.muzzleSpeed, config_.gravity).velocity;
        if (p.vel.lengthSq() == Fixed::zero()) p.vel = lob;
    } else {
        p.vel = solveLowArc(origin, aim, w.muzzleSpeed, config_.gravity).velocity;
    }
    // Over the highest thing in the way (a brow, a tree, a house, a wall) at
    // the lowest elevation that clears it, drawn short of full for the range.
    // When no arc the bow reaches clears it, as chosen above: into the wood,
    // or the full lob over masonry; a defender on a wall-walk is shot direct.
    if (!atWalk && (w.kind == ProjectileKind::Arrow || w.kind == ProjectileKind::Fireball || w.kind == ProjectileKind::Molotov)) {
        const LaunchSolution flat = solveLowArc(origin, aim, w.muzzleSpeed, config_.gravity);
        const Fixed need = clearingTan(origin, aim, kClearShort);
        if (flat.inRange && need > elevationTan(flat.velocity)) {
            const LaunchSolution steep = solveHighArc(origin, aim, w.muzzleSpeed, config_.gravity);
            if (steep.inRange && need <= elevationTan(steep.velocity))
                p.vel = solveFixedElevation(origin, aim, need, w.muzzleSpeed, config_.gravity).velocity;
        }
    }
    p.damage = scaledDamage(w.damage, prowessPct(u));
    p.explosive = w.explosive;
    p.fuse = w.explosive.fuseTicks;
    p.cause = w.damageType;
    projectiles_.push_back(p);
    return true;
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
    const Fixed ground = surfaceBelow(p.pos.x, p.pos.y, before.z);
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
    const Fixed ground = surfaceBelow(p.pos.x, p.pos.y, before.z);
    if (p.pos.z > ground) return false;
    p.pos.z = ground;
    bool rolling = false;
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
        // Skidding: the ground drags it to a stop, smearing as it goes;
        // on a slope too steep for it to grip, it rolls on down instead.
        p.vel.z = Fixed::zero();
        const Fixed speed = travel.length();
        rolling = p.ageTicks < kRollTicks && rollDownhill(p);
        if (!rolling) {
            const Fixed slower = max(Fixed::zero(), speed - kSkidDecel * kTickDt);
            if (speed > Fixed::zero()) {
                p.vel.x = p.vel.x * slower / speed;
                p.vel.y = p.vel.y * slower / speed;
            }
        }
        p.smear += speed * kTickDt;
        if (p.smear >= kSmearSpacing) {
            bleed(Fixed::fromRatio(1, 10) + min(speed, Fixed::fromInt(6)) / 50);
            p.smear = Fixed::zero();
        }
    }
    if (!rolling && p.vel.lengthSq() < kTumbleRest * kTumbleRest) {
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
            const Vec3 prev = p.pos;
            bounce(p, p.explosive.bounce, kBombFriction);
            // Hurled by a scavenger, it bursts where it strikes: a man, or the ground.
            if (p.thrown && (debrisHit(prev, p.pos, p.shooter) != nullptr || p.pos.z <= surfaceBelow(p.pos.x, p.pos.y, p.pos.z))) {
                pendingExplosions_.push_back({p.pos, p.explosive, p.owner, p.kind, p.cause});
                p.fuse = -1;  // going off: no blast sets it off again
                removed[i] = 1;
                continue;
            }
            // Laid, with no fuse burning, it waits for a blast or fire (explode).
            if (p.fuse > 0 && --p.fuse <= 0) {
                pendingExplosions_.push_back({p.pos, p.explosive, p.owner, p.kind, p.cause});
                p.fuse = -1;
                removed[i] = 1;
            }
            continue;
        }

        if (p.kind == ProjectileKind::Debris) {
            // An arrow coming down hard sticks; everything else bounces to rest.
            const Fixed nextZ = p.pos.z + p.vel.z * kTickDt;
            const Fixed below = surfaceBelow(p.pos.x, p.pos.y, p.pos.z);
            if (p.remain == RemainKind::Arrow && p.vel.z < -kStickSpeed && nextZ <= below) {
                placeRemain(RemainKind::Arrow, p.owner, {p.pos.x, p.pos.y, below}, normalized(p.vel));
                removed[i] = 1;
                continue;
            }
            const Vec3 travel = p.vel.flat();
            const Vec3 prev = p.pos;
            const Fixed speedSq = p.vel.lengthSq();
            const bool done = tumble(p) || p.ageTicks >= kMaxProjectileAgeTicks;
            // A body part flying fast (thrown by a blast, or by a hand) hurts
            // the first man it strikes, a little, once; a kick never does.
            if (!done && p.remain != RemainKind::Arrow && p.damage == 0 && speedSq >= kDebrisHurtSpeed * kDebrisHurtSpeed) {
                Unit* hit = debrisHit(prev, p.pos, p.thrown ? p.shooter : kNoEntity);
                if (hit != nullptr && p.thrown) {
                    // Thrown by a hand: he remembers who threw it (World::shieldTarget),
                    // and a shield facing the throw catches it as it would an arrow.
                    if (const Unit* thrower = findUnit(p.shooter); thrower != nullptr && thrower->owner != hit->owner) {
                        hit->thrower = thrower->id;
                        hit->thrownTick = tick_;
                    }
                    if (shieldBlocks(*hit, p.vel, Fixed::one())) {
                        impacts_.push_back({ImpactEvent::Kind::Blocked, p.pos, p.vel, p.id, hit->id, false, {}});
                        p.damage = 1;  // spent on the shield
                        p.vel = -p.vel.flat() / Fixed::fromInt(6) + up(Fixed::one());
                        hit = nullptr;
                    }
                }
                if (hit != nullptr) {
                    const Fixed speed = sqrt(speedSq);
                    int32_t amount = std::clamp<int32_t>((speed / 3).floorToInt(), 2, 8);
                    if (p.remain == RemainKind::Torso) amount = amount * 3 / 2;
                    Blow blow;
                    blow.type = DamageType::Blunt;
                    blow.dir = normalized(p.vel);
                    blow.at = p.pos;
                    // A blade cuts, and deeper (it is only ever thrown by a hand, or blown off the ground again).
                    if (p.remain == RemainKind::Blade) {
                        blow.type = DamageType::Slash;
                        amount = static_cast<int32_t>(std::clamp<int64_t>((speed * 3 / 4).floorToInt(), 5, 14));
                    }
                    // Thrown by a hand: the thrower's blow (his kill, if it kills).
                    Unit* thrower = p.thrown ? mutableUnit(p.shooter) : nullptr;
                    if (thrower != nullptr && thrower->owner == hit->owner) thrower = nullptr;
                    const bool killed = damage(*hit, amount, thrower, blow);
                    if (p.thrown) {
                        // A head hurled into a man's face breaks him (those who never break excepted).
                        if (p.remain == RemainKind::Head && hit->alive() && hit->stats.moraleShockPct > 0) {
                            hit->morale = 0;
                            hit->calmTicks = 0;
                        }
                        impacts_.push_back({ImpactEvent::Kind::Hit, p.pos, p.vel, p.id, hit->id, killed, {}, -1, blow.type});
                    }
                    p.damage = amount;  // spent: it strikes no one else
                    p.vel = p.vel / Fixed::fromInt(4);  // and drops
                }
            }
            if (done) {
                Vec3 lie = normalized(travel);
                if (lie.lengthSq() == Fixed::zero()) lie = {Fixed::one(), Fixed::zero(), Fixed::zero()};
                placeRemain(p.remain, p.owner, p.pos, lie, p.source);
                removed[i] = 1;
            }
            continue;
        }

        // Arrows and fireballs: ballistic flight, hitting the first thing in their path.
        const Vec3 prev = p.pos;
        // Exact for constant acceleration, so flight paths match the aiming solution.
        // A stone is too heavy for the wind to carry.
        const Vec3 a = p.kind == ProjectileKind::Stone ? up(-config_.gravity) : accel;
        p.pos += p.vel * kTickDt + a * (kTickDt * kTickDt / 2);
        p.vel += a * kTickDt;
        const Vec3 seg = p.pos - prev;

        // Fraction of this tick's path at which the projectile reaches the
        // ground, treating the ground as linear along the path.
        // (Not the wall-walk overhead while it flies through a gate passage.)
        const Fixed abovePrev = prev.z - surfaceBelow(prev.x, prev.y, prev.z);
        const Fixed aboveNow = p.pos.z - surfaceBelow(p.pos.x, p.pos.y, prev.z);
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
        uint32_t hitIndex = 0;
        nearby(prev.x + seg.x / 2, prev.y + seg.y / 2, sqrt(segFlatLenSq) / 2 + Fixed::fromInt(6), [&](uint32_t k) {
            Unit& u = units_[k];
            if (!u.alive() || u.id == p.shooter) return;
            Fixed t = Fixed::zero();
            if (segFlatLenSq != Fixed::zero()) {
                t = clamp((u.pos - prev).flat().dot(segFlat) / segFlatLenSq, Fixed::zero(), Fixed::one());
            }
            if (t > hitT || (t == hitT && (hit == nullptr || k > hitIndex))) return;  // (the earliest, and of those the lowest id)
            if (t == hitT && hit == nullptr) return;
            const Vec3 closest = prev + seg * t;
            const Fixed r = u.stats.radius;
            if ((closest - u.pos).flat().lengthSq() > r * r) return;
            if (closest.z < u.pos.z || closest.z > u.pos.z + u.stats.height) return;
            hit = &u;
            hitT = t;
            hitIndex = k;
        });

        // Trees, rocks and walls in the way stop it first.
        bool stone = false;
        const Fixed obstacleT = obstacleHit(prev, p.pos, hitT, false, &stone);
        if (obstacleT >= Fixed::zero()) {
            const Vec3 at = prev + seg * obstacleT;
            if (burstsOnImpact(p.kind)) {
                pendingExplosions_.push_back({at, p.explosive, p.owner, p.kind, p.cause});
            } else if (stone) {
                // Off stone it glances back and drops to the foot of it.
                impacts_.push_back({ImpactEvent::Kind::Ground, at, p.vel, p.id, kNoEntity, false, {}});
                spawnDebris(RemainKind::Arrow, p.owner, at - normalized(seg) * Fixed::fromRatio(1, 20), -p.vel.flat() / Fixed::fromInt(kGlanceDivisor));
            } else {
                placeRemain(RemainKind::Arrow, p.owner, at, normalized(p.vel));  // stuck in the wood
                impacts_.push_back({ImpactEvent::Kind::Ground, at, p.vel, p.id, kNoEntity, false, {}});
            }
            removed[i] = 1;
        } else if (hit != nullptr) {
            const Vec3 at = prev + seg * hitT;
            if (burstsOnImpact(p.kind)) {
                pendingExplosions_.push_back({at, p.explosive, p.owner, p.kind, p.cause});
            } else if (shieldBlocks(*hit, p.vel, Fixed::one())) {
                // Caught: half the time the arrow sticks in the shield (a few at most), else it glances off.
                impacts_.push_back({ImpactEvent::Kind::Blocked, at, p.vel, p.id, hit->id, false, {}});
                if (hit->shieldArrows < kMaxShieldArrows && rng_.nextBelow(2) == 0) {
                    ++hit->shieldArrows;
                } else {
                    const Vec3 glance{-p.vel.x / 5, -p.vel.y / 5, abs(p.vel.z) / 5 + Fixed::fromInt(2)};
                    spawnDebris(RemainKind::Arrow, p.owner, at, glance);
                }
            } else {
                // Worse in the flank or back, and from a plunging shot from above.
                const HitSide side = hitSide(hit->facing, p.vel);
                const int32_t amount = p.damage * sidePct(side, true) / 100 * plungePct(p.vel) / 100;
                const bool killed = damage(*hit, amount, mutableUnit(p.shooter), {p.cause, p.vel, at, true});
                shakeBySide(*hit, side, true);
                impacts_.push_back({ImpactEvent::Kind::Hit, at, p.vel, p.id, hit->id, killed, {}});
            }
            removed[i] = 1;
        } else if (groundT <= Fixed::one()) {
            const Vec3 at = prev + seg * groundT;
            if (burstsOnImpact(p.kind)) {
                pendingExplosions_.push_back({at, p.explosive, p.owner, p.kind, p.cause});
            } else {
                placeRemain(RemainKind::Arrow, p.owner, at, normalized(p.vel));
                impacts_.push_back({ImpactEvent::Kind::Ground, at, p.vel, p.id, kNoEntity, false, {}});
            }
            removed[i] = 1;
        } else if (p.ageTicks >= kMaxProjectileAgeTicks) {
            removed[i] = 1;
        }
    }

    // By index: a blast may set off another (a satchel in a man's hands).
    for (size_t k = 0; k < pendingExplosions_.size(); ++k) {
        const PendingExplosion e = pendingExplosions_[k];
        explode(e.at, e.ex, e.owner, e.kind, e.cause);
    }

    size_t keep = 0;
    for (size_t i = 0; i < projectiles_.size(); ++i) {
        if (!removed[i]) projectiles_[keep++] = projectiles_[i];
    }
    projectiles_.resize(keep);
    projectiles_.insert(projectiles_.end(), spawned_.begin(), spawned_.end());
    spawned_.clear();
}

void World::explode(const Vec3& at, const Explosive& ex, PlayerId owner, ProjectileKind kind, DamageType cause) {
    // A stone smashes and scatters, without fire; a Molotov's oil bursts into flame.
    const ImpactEvent::Kind event = kind == ProjectileKind::Stone     ? ImpactEvent::Kind::Smash
                                    : kind == ProjectileKind::Molotov ? ImpactEvent::Kind::Burst
                                                                      : ImpactEvent::Kind::Explosion;
    impacts_.push_back({event, at, up(Fixed::one()), kNoEntity, kNoEntity, false, ex.radius, -1, cause});
    blastGates(at, ex, kind);
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
        if (!u.present()) continue;
        const Vec3 centre = u.pos + up(u.stats.height / 2);
        const Fixed d = (centre - at).length();
        if (d >= ex.radius) continue;
        const Fixed f = Fixed::one() - d / ex.radius;
        const Vec3 dir = awayFrom(u.pos);
        if (u.scav.satchel) setOffHeld(u);  // the satchel in his hands goes off too
        if (u.alive()) {
            const bool killed = damage(u, static_cast<int32_t>((f * ex.damage).floorToInt()), nullptr, {cause, dir, centre, false, true});
            impacts_.push_back({ImpactEvent::Kind::Hit, centre, dir, kNoEntity, u.id, killed, {}, -1, cause});
            // Blown far past death, he is torn to pieces (world_death.cpp decides).
            if (killed && u.death.kind == DeathKind::Gibbed) spawnGibs(u, at, ex.impulse * f);
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
        spawnDebris(r.kind, r.owner, r.pos + up(Fixed::fromRatio(1, 10)), away * v + up(v) + jitter, r.source);
        clearRemain(i);
    }

    // Satchels nearby (laid, burning or flying) are thrown and set off a
    // moment later, or sooner: chain reactions. A Molotov's fire does it too.
    for (Projectile& p : projectiles_) {
        if (p.kind != ProjectileKind::Bomb || p.fuse < 0) continue;  // (going off already)
        const Fixed d = (p.pos - at).length();
        if (d >= ex.radius) continue;
        const Fixed v = ex.impulse * (Fixed::one() - d / ex.radius);
        p.vel += awayFrom(p.pos) * v + up(v * kLiftFraction);
        p.resting = false;
        const int32_t fuse = 4 + static_cast<int32_t>(rng_.nextBelow(8));
        p.fuse = p.fuse > 0 ? std::min(p.fuse, fuse) : fuse;
    }
    (void)owner;
}

void World::spawnGibs(Unit& victim, const Vec3& from, Fixed strength) {
    // Torn apart: head, torso, both arms and legs, and gobbets of flesh.
    // Bigger bodies make more gobbets and fly less far.
    victim.gibbed = true;
    std::vector<RemainKind> parts = {RemainKind::Head, RemainKind::Torso, RemainKind::Arm, RemainKind::Arm,
                                     RemainKind::Leg,  RemainKind::Leg};
    // A limb already cut off is not there to fly.
    if (victim.death.severed != RemainKind::None) parts.erase(std::find(parts.begin(), parts.end(), victim.death.severed));
    const int64_t chunks = std::clamp<int64_t>(victim.stats.mass.floorToInt() + 1, 2, 6);
    for (int64_t i = 0; i < chunks; ++i) parts.push_back(RemainKind::Chunk);
    strength = strength * sqrt(Fixed::fromInt(2) / (Fixed::one() + min(victim.stats.mass, Fixed::fromInt(15))));
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
        spawnDebris(kind, victim.owner, origin, vel, static_cast<uint8_t>(victim.stats.type));
        spawned_.back().bleeding = victim.stats.bleeds;
    }
}

// The first living man a flying body part passes through between two points
// (his upright cylinder, as an arrow finds him), or null.
Unit* World::debrisHit(const Vec3& a, const Vec3& b, EntityId ignore) {
    const Vec3 seg = b - a;
    const Vec3 segFlat = seg.flat();
    const Fixed lenSq = segFlat.lengthSq();
    Unit* hit = nullptr;
    Fixed hitT = Fixed::fromInt(2);
    uint32_t hitIndex = 0;
    nearby(a.x + seg.x / 2, a.y + seg.y / 2, sqrt(lenSq) / 2 + Fixed::fromInt(6), [&](uint32_t k) {
        Unit& u = units_[k];
        if (!u.alive() || u.inAir() || (ignore != kNoEntity && u.id == ignore)) return;
        const Fixed t = lenSq != Fixed::zero() ? clamp((u.pos - a).flat().dot(segFlat) / lenSq, Fixed::zero(), Fixed::one()) : Fixed::zero();
        if (t > hitT || (t == hitT && (hit == nullptr || k > hitIndex))) return;
        const Vec3 closest = a + seg * t;
        if ((closest - u.pos).flat().lengthSq() > u.stats.radius * u.stats.radius) return;
        if (closest.z < u.pos.z || closest.z > u.pos.z + u.stats.height) return;
        hit = &u;
        hitT = t;
        hitIndex = k;
    });
    return hit;
}

void World::spawnDebris(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& vel, uint8_t source) {
    Projectile p;
    p.id = nextId_++;
    p.owner = owner;
    p.source = source;
    p.kind = ProjectileKind::Debris;
    p.remain = kind;
    p.pos = pos;
    p.vel = vel;
    spawned_.push_back(p);
}

EntityId World::placeSatchel(PlayerId owner, const Vec3& pos, const Explosive& ex, int32_t fuseTicks) {
    Projectile p;
    p.id = nextId_++;
    p.owner = owner;
    p.kind = ProjectileKind::Bomb;
    p.pos = {pos.x, pos.y, surfaceBelow(pos.x, pos.y, pos.z + Fixed::one())};
    p.resting = true;
    p.explosive = ex;
    p.fuse = fuseTicks;
    p.cause = DamageType::Explosive;
    projectiles_.push_back(p);
    return p.id;
}

void World::placeRemain(RemainKind kind, PlayerId owner, const Vec3& pos, const Vec3& dir, uint8_t source) {
    // Whatever falls in the water sinks out of sight.
    if (isWater(pos.x, pos.y) && pos.z <= groundHeight(pos.x, pos.y) + Fixed::fromRatio(1, 2)) return;
    const uint32_t slot = static_cast<uint32_t>(remainsAdded_ % config_.maxRemains);
    const Remain r{kind, owner, pos, dir, tick_, source};
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
    // Only once a pack has been let loose to raid, so battles without one hash as before.
    if (packs_ > 0) h.addU64(packs_);
    // Only for a match with a field's edge, so battles without one hash as before.
    if (zoneRadius_ > Fixed::zero()) {
        hashVec(h, zoneCentre_);
        h.addI64(zoneRadius_.raw);
        h.addI64(zonePct_);
    }
    // (The men taken out of the world are not in it: they are no part of its state, but the places they left are, once
    // there are any, for they say whose id the next man has. spawns_ is not hashed: see clearFight.)
    for (EntityId free : freeSlots_) h.addU64(free);  // (in the order kept: descending)
    h.addU64(std::count_if(units_.begin(), units_.end(), [](const Unit& u) { return u.present(); }));
    for (const Unit& u : units_) {
        if (!u.present()) continue;
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
        h.addU64(u.foe);
        h.addI64(u.momentum.raw);
        h.addI64(u.chargeGrade.raw);
        h.addI64(u.morale);
        h.addU64(u.routed);
        if (u.cornered) h.addU64(0xC0A7);  // (only once one stands broken, so battles without any hash as before)
        h.addI64(u.calmTicks);
        // Only once some veteran steadies him, so battles without any hash as before.
        if (u.steady != 0 || u.callCooldown != 0) {
            h.addI64(u.steady);
            h.addI64(u.callCooldown);
        }
        h.addU64(u.hasMoveTarget);
        hashVec(h, u.moveTarget);
        h.addI64(u.marchSpeed.raw);
        // Only when ordered, so battles without facing orders hash as before.
        if (u.arriveFacing != Vec3{}) hashVec(h, u.arriveFacing);
        h.addU64(u.gibbed);
        // Only once someone has died or caught an arrow in his shield, so older battles hash alike.
        if (u.death.kind != DeathKind::None) {
            h.addU64(static_cast<uint64_t>(u.death.kind));
            h.addU64(static_cast<uint64_t>(u.death.cause));
            hashVec(h, u.death.dir);
            h.addI64(u.death.overkill);
            h.addU64(static_cast<uint64_t>(u.death.severed));
            h.addU64(u.death.tick);
            h.addU64(u.death.arrowPinned);
            hashVec(h, u.death.arrowDir);
            h.addI64(u.death.arrowHeight.raw);
        }
        if (u.shieldArrows != 0) h.addI64(u.shieldArrows);
        // Only once a scavenger is about, so battles without one hash as before.
        if (u.looted) h.addU64(u.looted);
        // Only for those who carry satchels, so battles without them hash as before.
        if (u.stats.satchels.count > 0) {
            h.addI64(u.satchels);
            h.addI64(u.layCooldown);
            h.addU64(u.layOrdered);
        }
        if (u.stats.scavenge.sight > Fixed::zero()) hashScavenger(h, u.scav);
        // Only once a scavenger's throw has struck him, so battles without one hash as before.
        if (u.thrower != kNoEntity) {
            h.addU64(u.thrower);
            h.addU64(u.thrownTick);
        }
        h.addU64(u.hasAttackTarget);
        hashVec(h, u.attackTarget);
        h.addI64(u.reloadRemaining);
        if (u.stats.ranged.drawTicks > 0) {  // only for bowmen who draw, so battles without them hash as before
            h.addI64(u.drawn);
            h.addU64(u.drawTick);
        }
        h.addI64(u.meleeCooldown);
        h.addI64(u.healCooldown);
        h.addI64(u.healCharges);
        h.addU64(static_cast<uint64_t>(u.flight));
        h.addI64(u.runway.raw);
        h.addI64(u.stamina);
        h.addU64(u.path.size());
        for (const Vec3& w : u.path) hashVec(h, w);
        h.addU64(u.pathIndex);
        h.addI64(u.attackGate);
        // Orders (world_orders.cpp): only once given, so battles without them hash as before.
        if (u.targetUnit != kNoEntity || u.rally != kNoEntity || u.forceMarch || u.engageFoe != kNoEntity ||
            u.graceTicks != 0 || u.lastAttacker != kNoEntity) {
            h.addU64(u.targetUnit);
            h.addU64(u.targetChosen);
            hashVec(h, u.targetSeen);
            h.addU64(u.rally);
            hashVec(h, u.rallyOffset);
            h.addU64(u.forceMarch);
            h.addU64(u.engageFoe);
            h.addI64(u.graceTicks);
            h.addU64(u.lastAttacker);
            h.addU64(u.attackedTick);
        }
        // Where his leg of the route began, for the leash.
        if (u.hasMoveTarget) hashVec(h, u.routeFrom);
        // The orders waiting (Command::queue): only while there are any, so battles without a queue hash as before.
        if (!u.queue.empty()) {
            h.addU64(u.queue.size());
            h.addU64(u.orderTick);
            for (const Command& c : u.queue) {
                h.addU64(static_cast<uint64_t>(c.type));
                hashVec(h, c.target);
                h.addI64(c.speed.raw);
                h.addU64(c.march);
                hashVec(h, c.marchFrom);
                hashVec(h, c.marchTo);
                hashVec(h, c.face);
                h.addU64(c.force);
                h.addU64(c.targetUnit);
                h.addU64(c.rally);
                h.addU64(static_cast<uint64_t>(c.object));
                h.addI64(c.objectIndex);
            }
        }
    }
    h.addU64(gates_.size());
    for (const GateState& g : gates_) {
        h.addI64(g.hp);
        h.addU64(g.holder);
        h.addU64(g.brokenTick);
        h.addU64(static_cast<uint64_t>(g.mode));
        h.addU64(g.since);
        h.addU64(g.openUntil);
        h.addU64(static_cast<uint64_t>(g.pin) | (g.garrisoned ? 4u : 0u));
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
        h.addU64(static_cast<uint64_t>(p.cause));
        hashExplosive(h, p.explosive);
        if (p.thrown) h.addU64(p.thrown);
    }
    // Only once a side is out of a match, so battles without one hash as before.
    if (!brokenSides_.empty()) {
        h.addU64(brokenSides_.size());
        for (PlayerId o : brokenSides_) h.addU64(o);
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
