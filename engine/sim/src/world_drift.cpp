// What the water does with the dead (docs/DEATH_AUDIT.md, "In the water"): a body or a part of one that falls where
// the water is deep enough to bear it floats, and a river carries it downstream. It slows over the shallows, grounds
// where the water is too thin for it (a bank, a bar, a ford's crest), lodges against rocks and a stone bridge's
// piers, and an armoured man sinks once it has him in deep water. In still water it barely moves. Arrows, blades and
// bones sink out of sight at once, as everything used to.
//
// This is simulation, not show: the hyena-folk take parts and the blades of the dead (world_scavenge.cpp), men kick
// parts along (kickRemains) and blasts throw them, so where they lie is state, in fixed point and in the checksum.
// A floater's motion is a function of where it is and how long it has been dead, read from the river's own lines
// (Region::rivers): no random draw, and no floater reads another, so their order does not matter.
#include <algorithm>

#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// The current: this fast in midstream, less toward the banks, none this far beyond them (a lake at a river's mouth
// still draws what comes down a little way in).
constexpr Fixed kMidstream = Fixed::fromRatio(8, 5);
constexpr Fixed kBankShare = Fixed::fromRatio(11, 20);
constexpr Fixed kSlackReach = Fixed::fromInt(30);
// A river is felt this far from its line at most (the renderer's flow texture uses the same reach).
constexpr Fixed kFlowReach = Fixed::fromInt(60);
// The water closes toward the middle of the stream a little, so most of what floats clears a bend.
constexpr Fixed kCentring = Fixed::fromRatio(1, 6);
// The flow is worked out once for each square this wide and kept.
constexpr Fixed kFlowCell = Fixed::fromInt(2);
// Water thinner than its draught grounds a body; with this much more under it, it rides at the stream's own pace.
constexpr Fixed kFreeWater = Fixed::fromRatio(3, 5);
constexpr Fixed kDragShare = Fixed::fromRatio(1, 4);
constexpr Fixed kPartDraught = Fixed::fromRatio(1, 8);
constexpr Fixed kTrunkDraught = Fixed::fromRatio(1, 5);
// Still water: a floater wanders this slowly, changing its mind every few seconds.
constexpr Fixed kWander = Fixed::fromRatio(1, 16);
constexpr uint32_t kWanderTicks = 6 * kTicksPerSecond;
// Mail and plate (and a horse's or a beast's weight) take a body down once the water is this deep under it and he has
// been dead this long (a few seconds more or less, by who he is); everything else waterlogs in the end.
constexpr Fixed kSinkDepth = Fixed::fromRatio(6, 5);
constexpr uint32_t kSinkTicks = 9 * kTicksPerSecond;
constexpr uint32_t kSinkSpread = 8 * kTicksPerSecond;
constexpr uint32_t kWaterlogTicks = 240 * kTicksPerSecond;
// No more than this many things are carried at once: past it what falls in lies where it fell.
constexpr size_t kMaxAfloat = 384;
// A stone bridge's piers (game/render/meshes.cpp `bridge`: a bay every 11 m or so, piers 1.8 m thick with cutwaters
// 1.4 m beyond the deck's edge).
constexpr Fixed kBay = Fixed::fromInt(11);
constexpr Fixed kPier = Fixed::fromRatio(9, 5);
constexpr Fixed kCutwater = Fixed::fromRatio(7, 5);

Vec3 unit(const Vec3& v) {
    const Fixed len = v.length();
    return len == Fixed::zero() ? v : v / len;
}

uint32_t scramble(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return v;
}

bool floats(RemainKind k) {
    return k == RemainKind::Head || k == RemainKind::Arm || k == RemainKind::Leg || k == RemainKind::Torso || k == RemainKind::Chunk;
}

}  // namespace

Fixed World::draughtOf(const UnitStats& s) { return max(s.height / 4, Fixed::fromRatio(3, 10)); }

Fixed World::waterDepth(Fixed x, Fixed y) const {
    if (!config_.region) return Fixed::zero();
    const Fixed surface = config_.region->waterSurfaceAt(x, y);
    if (surface.raw == Region::kDryRaw) return Fixed::zero();
    return max(surface - config_.region->terrain().heightAt(x, y), Fixed::zero());
}

Vec3 World::flowAt(Fixed x, Fixed y) const {
    if (!config_.region || config_.region->rivers().empty()) return {};
    const int64_t cx = (x / kFlowCell).floorToInt(), cy = (y / kFlowCell).floorToInt();
    const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(cx)) << 32) | static_cast<uint32_t>(cy);
    if (const auto it = flowCache_.find(key); it != flowCache_.end()) return it->second;  // (looked up, never walked)
    const Vec3 p{kFlowCell * cx + kFlowCell / 2, kFlowCell * cy + kFlowCell / 2, Fixed::zero()};
    // Whose water this is: the river nearest by its own width (a brook's mouth in a broad river's bed is the river's).
    Fixed best, bestRel;
    Vec3 along, nearest;
    Fixed half;
    bool found = false;
    for (const Polyline& rv : config_.region->rivers()) {
        for (size_t i = 0; i + 1 < rv.points.size(); ++i) {
            const Vec3 a = rv.points[i].flat(), ab = (rv.points[i + 1] - rv.points[i]).flat();
            // (Nowhere near this stretch: most of a land's rivers are not.)
            if (abs(a.x - p.x) > kFlowReach + abs(ab.x) || abs(a.y - p.y) > kFlowReach + abs(ab.y)) continue;
            const Fixed lenSq = ab.lengthSq();
            if (lenSq == Fixed::zero()) continue;
            const Fixed t = clamp((p - a).dot(ab) / lenSq, Fixed::zero(), Fixed::one());
            const Vec3 c = a + ab * t;
            const Fixed d = (c - p).lengthSq();
            if (d >= kFlowReach * kFlowReach) continue;
            const Fixed h = max(rv.widthAt(i, t), Fixed::fromInt(2));
            const Fixed rel = d / (h * h);
            if (found && rel >= bestRel) continue;
            best = d;
            bestRel = rel;
            along = ab;
            nearest = c;
            half = h;
            found = true;
        }
    }
    Vec3 flow;
    if (found) {
        const Fixed d = sqrt(best);
        // Quickest in the middle; beyond the banks (a pool beside it, the lake it runs into) it dies away.
        const Fixed pace = d < half ? Fixed::one() - (Fixed::one() - kBankShare) * d / half
                                    : kBankShare * max(Fixed::zero(), Fixed::one() - (d - half) / kSlackReach);
        flow = unit(along) * (kMidstream * pace);
        if (d > Fixed::zero() && pace > Fixed::zero()) flow += (nearest - p) / d * (kCentring * min(d / half, Fixed::one()));
    }
    flowCache_.emplace(key, flow);
    return flow;
}

bool World::pierAt(Fixed x, Fixed y) const {
    if (!config_.region || config_.region->decks().empty()) return false;
    for (const uint32_t k : config_.region->decksAt(x, y)) {
        const Deck& d = config_.region->decks()[k];
        if (d.foot || d.raised || d.ramp || d.arc) continue;  // a footbridge stands on piles a body passes between
        const Fixed dx = x - d.centre.x, dy = y - d.centre.y;
        const Fixed across = abs(dx * d.dir.y - dy * d.dir.x);
        if (across > d.halfWidth + kCutwater) continue;
        const Fixed span = d.halfSpan * 2;
        const int64_t bays = std::max<int64_t>(1, ((span + kBay / 2) / kBay).floorToInt());
        if (bays < 2) continue;
        const Fixed bay = (span - kPier * (bays - 1)) / bays;
        // Along the deck from its near end: a bay, a pier, a bay...
        const Fixed s = dx * d.dir.x + dy * d.dir.y + d.halfSpan;
        if (s <= Fixed::zero() || s >= span) continue;
        const Fixed inPitch = s - (bay + kPier) * (s / (bay + kPier)).floorToInt();
        if (inPitch > bay) return true;
    }
    return false;
}

void World::floatBody(Unit& u) {
    if (u.afloat || u.alive() || u.gibbed || u.inAir() || !config_.region || afloat_.size() >= kMaxAfloat) return;
    if (waterDepth(u.pos.x, u.pos.y) < draughtOf(u.stats)) return;
    // On a bridge over the water he lies on the bridge.
    if (u.pos.z > config_.region->waterSurfaceAt(u.pos.x, u.pos.y)) return;
    u.afloat = true;
    afloat_.push_back({true, u.id});
}

bool World::floatRemain(uint32_t slot) {
    const Remain& r = remains_[slot];
    if (!floats(r.kind) || afloat_.size() >= kMaxAfloat) return false;
    if (waterDepth(r.pos.x, r.pos.y) < (r.kind == RemainKind::Torso ? kTrunkDraught : kPartDraught)) return false;
    // (The slot may have been afloat before, under what lay in it: one entry a slot.)
    const bool listed = std::any_of(afloat_.begin(), afloat_.end(), [&](const Floater& f) { return !f.unit && f.ref == slot; });
    if (!listed) afloat_.push_back({false, slot});
    return true;
}

bool World::sinksInWater(RemainKind kind) { return !floats(kind); }

bool World::plunge(const Projectile& p, const Vec3& before) {
    if (!config_.region || !isWater(p.pos.x, p.pos.y)) return false;
    const Fixed surface = config_.region->waterSurfaceAt(p.pos.x, p.pos.y);
    if (surface.raw == Region::kDryRaw || p.pos.z > surface || before.z <= surface) return false;
    if (waterDepth(p.pos.x, p.pos.y) < kPartDraught) return false;
    // A trunk goes in with a splash, a gobbet with a plop.
    const Fixed size = p.remain == RemainKind::Torso ? Fixed::fromRatio(9, 10) : p.remain == RemainKind::Arrow ? Fixed::fromRatio(1, 5) : Fixed::fromRatio(1, 2);
    impacts_.push_back({ImpactEvent::Kind::Splash, {p.pos.x, p.pos.y, surface}, p.vel, p.id, kNoEntity, false, size});
    return true;
}

bool World::carry(Vec3& pos, Fixed draught, bool heavy, uint32_t age, uint32_t who) const {
    const bool adrift = drift(pos, draught, heavy, age, who);
    // What floats rides at the surface; aground or sunk it lies on the bed.
    pos.z = adrift ? config_.region->waterSurfaceAt(pos.x, pos.y) : config_.region->terrain().heightAt(pos.x, pos.y);
    return adrift;
}

bool World::drift(Vec3& pos, Fixed draught, bool heavy, uint32_t age, uint32_t who) const {
    const Fixed depth = waterDepth(pos.x, pos.y);
    if (depth < draught) return false;  // aground
    if (age >= kWaterlogTicks) return false;  // sunk
    if (heavy && depth >= kSinkDepth && age >= kSinkTicks + scramble(who) % kSinkSpread) return false;
    Vec3 flow = flowAt(pos.x, pos.y);
    if (flow.lengthSq() == Fixed::zero()) {
        // Still water: this way a while, then that.
        static constexpr int kDirs[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
        const auto& d = kDirs[scramble(who * 31u + (tick_ + who * 7u) / kWanderTicks) & 7u];
        flow = Vec3{Fixed::fromInt(d[0]), Fixed::fromInt(d[1]), Fixed::zero()} * kWander;
    }
    // Dragging over the shallows, free once there is water under it.
    const Fixed share = kDragShare + (Fixed::one() - kDragShare) * min((depth - draught) / kFreeWater, Fixed::one());
    const Vec3 step = flow * (share * kTickDt);
    const auto open = [&](const Vec3& to) {
        if (waterDepth(to.x, to.y) < draught || pierAt(to.x, to.y)) return false;
        return obstacleHit(pos, {to.x, to.y, pos.z}, Fixed::one() + Fixed::one()) < Fixed::zero();
    };
    // Against a bank, a rock or a pier it slides along whichever way is clear, or lodges there.
    Vec3 to = pos + step;
    if (!open(to)) {
        to = {pos.x + step.x, pos.y, pos.z};
        if (step.x == Fixed::zero() || !open(to)) {
            to = {pos.x, pos.y + step.y, pos.z};
            if (step.y == Fixed::zero() || !open(to)) return true;
        }
    }
    pos.x = to.x;
    pos.y = to.y;
    return true;
}

void World::driftAfloat() {
    if (afloat_.empty()) return;
    size_t kept = 0;
    for (size_t i = 0; i < afloat_.size(); ++i) {
        const Floater f = afloat_[i];
        bool adrift = false;
        if (f.unit) {
            Unit* u = mutableUnit(f.ref);
            if (u == nullptr || !u->afloat) continue;
            // Thrown out again by a blast (he is listed anew where he comes down), raised, or taken out of the world.
            if (u->present() && !u->alive() && !u->gibbed && !u->inAir())
                adrift = carry(u->pos, draughtOf(u->stats), armoured(u->stats), tick_ - u->death.tick, u->id);
            u->afloat = adrift;
        } else {
            Remain& r = remains_[f.ref];
            if (r.kind == RemainKind::None || !r.afloat) continue;
            const Vec3 before = r.pos;
            adrift = carry(r.pos, r.kind == RemainKind::Torso ? kTrunkDraught : kPartDraught, false, tick_ - r.tick, f.ref + 0x9E37u);
            r.afloat = adrift;
            if (r.pos != before) {
                remainGrid_.remove(f.ref, before.x, before.y);
                remainGrid_.insert(f.ref, r.pos.x, r.pos.y);
            }
            if (r.pos != before || !adrift) changedRemains_.push_back(f.ref);
        }
        if (adrift) afloat_[kept++] = f;
    }
    afloat_.resize(kept);
}

}  // namespace illyria::sim
