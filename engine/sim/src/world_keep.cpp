// A garrisoned gate (GateState::garrisoned) is kept: when a foe comes near, the garrison inside takes its stations by
// role, the archers up on the walls beside the gate, the foot in a horseshoe behind the doors with the gate's mouth as the
// kill zone; the doors are kept shut against what is outside, and swung open now and then to draw a few in where the foot
// can take them (docs/GAME_DESIGN.md, castles). Whose gate it is, a skirmish's or a conquest town's, makes no difference:
// the rule is the World's, and everything in it is integers and Fixed, thought of from the world's state alone.
#include <algorithm>

#include "gate_geometry.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// A gate thinks this often (ticks), each at its own beat so the continent's eighty-odd do not all think in one tick.
constexpr uint32_t kKeepThink = 10;
// The garrison is those of the holder inside the walls this near the gate's middle.
constexpr Fixed kKeepRadius = Fixed::fromInt(60);
// The holder's idle men just outside the walls this near the gate (fled and rallied, or driven out) fall back inside when a
// foe is near, to their stations; foot meet a foe within this of the gate (inside the walls, or when the doors stand open).
constexpr Fixed kRecallRange = Fixed::fromInt(45);
constexpr Fixed kMeetRange = Fixed::fromInt(45);
// A foe this near the gate (or nearer) sends the garrison to its stations.
constexpr Fixed kAlertRange = Fixed::fromInt(70);
// Foes this near outside the doors may be drawn in, and counted inside this near.
constexpr Fixed kLureRange = Fixed::fromInt(14);
constexpr Fixed kInsideRange = Fixed::fromInt(20);
// How long the doors stand open for a lure (seconds), and the doors' state below which they would rather fight at the mouth
// than wait for the doors to go (percent of their hit points), or foes at the door at most this many to a man of the foot.
constexpr int32_t kLureOpenSeconds = 4;
constexpr int32_t kLureHpPct = 80;
constexpr int32_t kLureOddsPerMan = 2;
// The foot are formed when this share of them (percent) stand at their places.
constexpr int32_t kFormedPct = 70;
// ... and this share (percent) of the archers stand at their embrasures.
constexpr int32_t kMannedPct = 50;
// The horseshoe: its middle rank stands this deep behind the doors' middle, the wings bent toward the doors by the arc of
// this radius, in ranks of at most kRankWidth, a man's breadth and a little apart; a man stands at his place within this.
constexpr Fixed kHoldDepth = Fixed::fromInt(10);
constexpr Fixed kBowlRadius = Fixed::fromInt(10);
constexpr int32_t kRankWidth = 9;
constexpr Fixed kSlotGap = Fixed::fromRatio(17, 10);
constexpr Fixed kSlotTolerance = Fixed::fromInt(2);
// An archer at an embrasure: this near it (flat) and this near its height.
constexpr Fixed kEmbrasureTolerance = Fixed::fromRatio(6, 5);
constexpr Fixed kEmbrasureHeight = Fixed::fromRatio(3, 2);

enum class Post : uint8_t { None, Archer, Foot };

// What a man is to the garrison: bowmen to the walls, the fighting rest to the horseshoe; engines, healers and the broken
// keep to themselves.
Post postOf(const Unit& u) {
    if (!u.alive() || u.routed || u.inAir() || u.flight != Unit::Flight::Grounded || u.stats.healing.amount > 0) return Post::None;
    if (u.stats.ranged.muzzleSpeed > Fixed::zero()) return World::canBatter(u.stats) ? Post::None : Post::Archer;
    return u.stats.melee.damage > 0 ? Post::Foot : Post::None;
}

bool idle(const Unit& u) { return !u.hasMoveTarget && !u.hasAttackTarget && u.targetUnit == kNoEntity && u.foe == kNoEntity && !u.engaged; }

}  // namespace

bool World::gateKeeper(const Unit& u) const {
    if (!config_.region || gates_.empty() || postOf(u) == Post::None) return false;
    const Region& rg = *config_.region;
    for (const size_t i : rg.gatesNear(u.pos.x, u.pos.y)) {
        const GateState& st = gates_[i];
        if (!st.garrisoned || st.holder != u.owner) continue;
        const Gate& g = rg.gates()[i];
        const Fixed d2 = (u.pos - (g.a + g.b) * Fixed::fromRatio(1, 2)).flat().lengthSq();
        if (d2 <= kRecallRange * kRecallRange || (d2 <= kKeepRadius * kKeepRadius && withinRing(rg.ramparts()[static_cast<size_t>(g.ring)], u.pos))) return true;
    }
    return false;
}

void World::keepGates() {
    if (!config_.region || !nav_) return;
    const Region& rg = *config_.region;
    for (size_t i = 0; i < gates_.size() && i < rg.gates().size(); ++i) {
        GateState& st = gates_[i];
        if (!st.garrisoned || st.holder == GateState::kNoHolder || (tick_ + i * 3) % kKeepThink != 0) continue;
        const Gate& g = rg.gates()[i];
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
        const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);

        // Who is about: the garrison (in the world's order) and the foes.
        std::vector<uint32_t> near;
        unitGrid_.query(mid.x, mid.y, kAlertRange, [&](uint32_t j) { near.push_back(j); });
        std::sort(near.begin(), near.end());
        std::vector<Unit*> archers, foot;
        int32_t outside = 0, coming = 0, inside = 0;  // foes at the doors outside, all foes outside within the alert range, and inside
        bool alert = false;
        for (const uint32_t j : near) {
            Unit& u = units_[j];
            if (!u.alive() || u.routed) continue;
            const Fixed d2 = (u.pos - mid).flat().lengthSq();
            if (u.owner != st.holder) {
                alert = true;
                if (u.inAir() || u.flight != Unit::Flight::Grounded) continue;
                if (townSide(g, ring, u.pos)) inside += d2 <= kInsideRange * kInsideRange;
                else ++coming, outside += d2 <= kLureRange * kLureRange;
                continue;
            }
            if (d2 > kKeepRadius * kKeepRadius || (d2 > kRecallRange * kRecallRange && !withinRing(ring, u.pos))) continue;
            const Post post = postOf(u);
            if (post == Post::Archer) archers.push_back(&u);
            else if (post == Post::Foot) foot.push_back(&u);
        }
        if (!alert) continue;

        int32_t manned = 0;
        // The archers up on the walls: an embrasure each, the nearest the doors first.
        if (!archers.empty()) {
            std::vector<size_t> order;
            for (size_t e = 0; e < ring.embrasures.size(); ++e)
                if ((ring.embrasures[e].pos - mid).flat().lengthSq() <= kKeepRadius * kKeepRadius) order.push_back(e);
            std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return (ring.embrasures[a].pos - mid).flat().lengthSq() < (ring.embrasures[b].pos - mid).flat().lengthSq();
            });
            std::vector<uint8_t> taken(ring.embrasures.size(), 0);
            std::vector<Unit*> loose;
            for (Unit* u : archers) {
                bool placed = false;
                for (const size_t e : order) {
                    const Rampart::Embrasure& em = ring.embrasures[e];
                    if (!taken[e] && (u->pos - em.pos).flat().lengthSq() <= kEmbrasureTolerance * kEmbrasureTolerance && abs(u->pos.z - em.pos.z) < kEmbrasureHeight) {
                        taken[e] = 1;
                        placed = true;
                        break;
                    }
                }
                manned += placed;
                if (!placed && idle(*u)) loose.push_back(u);
            }
            for (Unit* u : loose) {
                for (const size_t e : order) {
                    if (taken[e]) continue;
                    taken[e] = 1;
                    u->routeFrom = u->pos;
                    beginMove(*u, ring.embrasures[e].pos, Fixed::zero(), ring.embrasures[e].out);
                    planPath(*u, ring.embrasures[e].pos);
                    break;
                }
            }
        }

        // The foot in a horseshoe behind the doors, the mouth of the gate its kill zone: the middle rank deepest, the wings
        // bent in toward the doors, every man facing the mouth.
        int32_t formed = 0;
        if (!foot.empty()) {
            Fixed gap = kSlotGap;
            for (const Unit* u : foot) gap = max(gap, u->stats.radius * 2 + Fixed::fromRatio(1, 2));
            const Vec3 inward = -g.out;
            const Vec3 along = (g.b - g.a).flat() / max(Fixed::fromRatio(1, 10), (g.b - g.a).flat().length());
            const int32_t n = static_cast<int32_t>(foot.size());
            std::vector<Vec3> slots;
            for (int32_t k = 0; k < n; ++k) {
                const int32_t rank = k / kRankWidth, inRank = std::min(kRankWidth, n - rank * kRankWidth);
                Vec3 p;
                // A place the wall or a house takes is brought in toward the axis until it is free (else left to the others).
                Fixed lateral = gap * (Fixed::fromInt(k % kRankWidth * 2 - (inRank - 1)) / 2);
                for (int tries = 0; tries < 4; ++tries) {
                    const Fixed depth = kHoldDepth + gap * rank - lateral * lateral / (kBowlRadius * 2);
                    p = mid + inward * depth + along * lateral;
                    p.z = rg.terrain().heightAt(p.x, p.y);
                    // On the courtyard's own ground: not the foot of a ramp or a stair, nor under the wall-walk's gallery (an order
                    // there is an order up onto the walk).
                    if (standable(p) && Gate::inside(ring, p) && abs(groundHeight(p.x, p.y) - p.z) < Fixed::fromRatio(1, 4) &&
                        rg.rampartAt(p.x, p.y, ring.corners.front().z) < 0)
                        break;
                    lateral = lateral * 3 / 4;
                    p = {};
                }
                if (p == Vec3{}) continue;
                slots.push_back(p);
            }
            // Those already at a place keep it; the rest (idle) go to the nearest places left.
            std::vector<uint8_t> taken(slots.size(), 0);
            std::vector<Unit*> loose;
            for (Unit* u : foot) {
                size_t at = slots.size();
                Fixed atD2;
                for (size_t s = 0; s < slots.size(); ++s) {
                    const Fixed d2 = (slots[s] - u->pos).flat().lengthSq();
                    if (!taken[s] && d2 <= kSlotTolerance * kSlotTolerance && (at == slots.size() || d2 < atD2)) at = s, atD2 = d2;
                }
                if (at < slots.size()) taken[at] = 1, ++formed;
                else loose.push_back(u);
            }
            for (Unit* u : loose) {
                size_t best = slots.size();
                Fixed bestD2;
                for (size_t s = 0; s < slots.size(); ++s) {
                    const Fixed d2 = (slots[s] - u->pos).flat().lengthSq();
                    if (!taken[s] && (best == slots.size() || d2 < bestD2)) best = s, bestD2 = d2;
                }
                if (best == slots.size()) continue;
                taken[best] = 1;
                if (!idle(*u)) continue;
                u->routeFrom = u->pos;
                beginMove(*u, slots[best], Fixed::zero(), mid - slots[best]);
                planPath(*u, slots[best]);
            }
        }

        // A foe who is in reach, inside the walls or through the open doors, is met by the foot who stand idle (then back to the
        // places); the foe outside a shut gate is not walked at.
        const bool through = st.passable();
        for (Unit* u : foot) {
            if (!idle(*u)) continue;
            Unit* best = nullptr;
            Fixed bestD2;
            for (const uint32_t j : near) {
                Unit& f = units_[j];
                if (f.owner == st.holder || !f.alive() || f.routed || f.inAir() || f.flight != Unit::Flight::Grounded) continue;
                if ((f.pos - mid).flat().lengthSq() > kMeetRange * kMeetRange || !(through || townSide(g, ring, f.pos))) continue;
                const Fixed d2 = (f.pos - u->pos).flat().lengthSq();
                if (best == nullptr || d2 < bestD2) best = &f, bestD2 = d2;
            }
            if (best == nullptr) continue;
            Command c{u->owner, u->id, Command::Type::AttackUnit, {}};
            c.targetUnit = best->id;
            applyCommand(c);
        }

        // The doors: kept shut to what is outside, and swung open for a few seconds to draw foes in when the foot are formed
        // to take them, the room inside is not yet full of them, and the doors are failing or the foes few.
        if (st.broken() || foot.empty() || outside == 0 || inside >= static_cast<int32_t>(foot.size())) continue;
        const bool formedUp = formed * 100 >= static_cast<int32_t>(foot.size()) * kFormedPct && manned * 100 >= static_cast<int32_t>(archers.size()) * kMannedPct;
        const bool failing = static_cast<int64_t>(st.hp) * 100 < static_cast<int64_t>(g.maxHp) * kLureHpPct;
        if (formedUp && (failing || coming <= static_cast<int32_t>(foot.size()) * kLureOddsPerMan))
            st.openUntil = std::max(st.openUntil, tick_ + static_cast<uint32_t>(kLureOpenSeconds * kTicksPerSecond));
    }
}

}  // namespace illyria::sim
