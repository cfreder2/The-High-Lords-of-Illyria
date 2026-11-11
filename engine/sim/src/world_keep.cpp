// A garrisoned gate (GateState::garrisoned) is kept: when a foe comes near, the garrison inside takes its stations by
// role, the archers up on the walls beside the gate, the foot in a horseshoe behind the doors with the gate's mouth as the
// kill zone; the doors are kept shut against what is outside, and swung open now and then to draw a few in where the foot
// can take them (docs/GAME_DESIGN.md, castles). Whose gate it is, a skirmish's or a conquest town's, makes no difference:
// the rule is the World's, and everything in it is integers and Fixed, thought of from the world's state alone.
//
// The same stations are where a garrison is first put (garrisonPosts): a town's men standing in it, at the start, taken, or
// come to it, are placed by the same roles, so that the keepers are at their posts before any foe comes.
#include <algorithm>

#include "gate_geometry.h"
#include "placement.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

// A gate thinks this often (ticks), each at its own beat so the continent's eighty-odd do not all think in one tick.
constexpr uint32_t kKeepThink = 10;
// The garrison is those of the holder inside the walls this near the gate's middle.
constexpr Fixed kKeepRadius = Fixed::fromInt(60);
// The holder's idle men just outside the walls, this near the gate (fled and rallied, or driven out) fall back inside when a
// foe is near, to their stations; foot meet a foe within this of the gate (inside the walls, or when the doors stand open).
constexpr Fixed kRecallRange = Fixed::fromInt(70);
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
// The garrison at rest stands in wider ranks, where the courtyard is open (yardDepth).
constexpr int32_t kYardWidth = 12;
constexpr Fixed kYardDepth = Fixed::fromInt(12);
constexpr Fixed kSlotGap = Fixed::fromRatio(17, 10);
constexpr Fixed kSlotTolerance = Fixed::fromInt(2);
// An archer at an embrasure: this near it (flat) and this near its height.
constexpr Fixed kEmbrasureTolerance = Fixed::fromRatio(6, 5);
constexpr Fixed kEmbrasureHeight = Fixed::fromRatio(3, 2);

enum class Post : uint8_t { None, Archer, Foot };

// What a man is to the garrison: bowmen to the walls, the fighting rest to the horseshoe; engines, healers and the broken
// keep to themselves.
Post postOfStats(const UnitStats& s) {
    if (s.healing.amount > 0) return Post::None;
    if (s.ranged.muzzleSpeed > Fixed::zero()) return World::canBatter(s) ? Post::None : Post::Archer;
    return s.melee.damage > 0 ? Post::Foot : Post::None;
}

Post postOf(const Unit& u) {
    if (!u.alive() || u.routed || u.inAir() || u.flight != Unit::Flight::Grounded) return Post::None;
    return postOfStats(u.stats);
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
                    if (courtyardSpot(ring, p)) break;
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

// ---------------------------------------------------------------- where a garrison is put

bool World::courtyardSpot(const Rampart& ring, const Vec3& p) const {
    // On the courtyard's own ground: not the foot of a ramp or a stair, nor under the wall-walk's gallery (an order there is an
    // order up onto the walk).
    return standable(p) && Gate::inside(ring, p) && abs(groundHeight(p.x, p.y) - p.z) < Fixed::fromRatio(1, 4) &&
           config_.region->rampartAt(p.x, p.y, ring.corners.front().z) < 0;
}

std::vector<std::vector<Vec3>> World::yardRanks(const Gate& g, const Rampart& ring, const std::vector<int32_t>& groups, Fixed gap, const std::vector<Vec3>& avoid) const {
    // Ranks across the courtyard behind the doors, each at its depth, as many abreast as the open ground lets (at most
    // kYardWidth, centred on the doors' axis, a man's breadth and a little apart); a rank the ground makes too short is passed
    // by, a metre deeper. Each group (the line, then those behind it) begins a rank of its own.
    const Region& rg = *config_.region;
    const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
    const Vec3 along = (g.b - g.a).flat() / max(Fixed::fromRatio(1, 10), (g.b - g.a).flat().length());
    std::vector<Vec3> held = avoid;
    const auto open = [&](Fixed depth, Fixed lateral, Vec3& p) {
        p = mid - g.out * depth + along * lateral;
        p.z = rg.terrain().heightAt(p.x, p.y);
        if (!courtyardSpot(ring, p)) return false;
        for (const Vec3& q : held)
            if ((q - p).flat().lengthSq() < gap * gap * Fixed::fromRatio(9, 25)) return false;
        return true;
    };
    std::vector<std::vector<Vec3>> out(groups.size());
    Fixed depth = kYardDepth;
    for (size_t gi = 0; gi < groups.size(); ++gi) {
        int32_t need = groups[gi];
        while (need > 0 && depth <= kYardDepth + Fixed::fromInt(30)) {
            // The open places across the courtyard at this depth, on a lattice a gap apart centred on the doors' axis (so that every
            // rank lines up with the last, a gap where a house or a stair stands), the ones nearest the axis.
            constexpr int32_t kReach = 7;
            const int32_t want = std::min(need, kYardWidth);
            std::vector<std::pair<Fixed, Vec3>> row;
            Vec3 q;
            if (open(depth, Fixed::zero(), q)) row.push_back({Fixed::zero(), q});
            for (int32_t step = 1; step <= kReach && static_cast<int32_t>(row.size()) < want; ++step)
                for (const int32_t side : {-1, 1})
                    if (static_cast<int32_t>(row.size()) < want && open(depth, gap * (step * side), q)) row.push_back({gap * (step * side), q});
            const int32_t take = static_cast<int32_t>(row.size());
            // (A courtyard too narrow for a rank that wide anywhere near the doors takes what it can, once it has been looked over.)
            if (take < std::min(want, depth > kYardDepth + Fixed::fromInt(8) ? 3 : kYardWidth * 2 / 3)) {
                depth += Fixed::one();
                continue;
            }
            std::sort(row.begin(), row.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
            for (const auto& [lateral, q] : row) {
                out[gi].push_back(q);
                held.push_back(q);
            }
            need -= static_cast<int32_t>(row.size());
            depth += gap;
        }
        depth += gap / 2;
    }
    return out;
}

World::GarrisonRole World::garrisonRole(const UnitStats& s) {
    // The lord and the fliers stand behind the foot; the rest as the keepers' split has it.
    if (s.type == static_cast<uint8_t>(UnitType::Hero) || s.flyAltitude > Fixed::zero()) return GarrisonRole::Rear;
    switch (postOfStats(s)) {
        case Post::Archer: return GarrisonRole::Wall;
        case Post::Foot: return GarrisonRole::Foot;
        case Post::None: break;
    }
    return GarrisonRole::Rear;
}

size_t World::gateOfTown(const Vec3& heart) const {
    if (!config_.region) return kNoGate;
    const Region& rg = *config_.region;
    size_t best = kNoGate;
    Fixed bestD;
    for (size_t i = 0; i < rg.gates().size(); ++i) {
        const Gate& g = rg.gates()[i];
        if (g.ring < 0 || static_cast<size_t>(g.ring) >= rg.ramparts().size()) continue;
        if ((rg.ramparts()[static_cast<size_t>(g.ring)].centre - heart).flat().length() >= Fixed::fromInt(20)) continue;
        const Fixed d = ((g.a + g.b) * Fixed::fromRatio(1, 2) - heart).flat().lengthSq();
        if (best == kNoGate || d > bestD) best = i, bestD = d;  // (the outer ring's: its gate is farthest from the heart)
    }
    return best;
}

std::vector<World::GarrisonPost> World::garrisonPosts(size_t gate, const std::vector<const UnitStats*>& men, const std::vector<EntityId>& except) const {
    std::vector<GarrisonPost> out;
    if (!config_.region || !nav_ || gate >= config_.region->gates().size() || men.empty()) return out;
    const Region& rg = *config_.region;
    const Gate& g = rg.gates()[gate];
    const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
    const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
    const Vec3 along = (g.b - g.a).flat() / max(Fixed::fromRatio(1, 10), (g.b - g.a).flat().length());
    out.assign(men.size(), GarrisonPost{});

    // Who already stands about the gate (the places they hold are left to them).
    std::vector<Vec3> held;
    for (const Unit& u : units_) {
        if (!u.alive() || (u.pos - mid).flat().lengthSq() > kKeepRadius * kKeepRadius) continue;
        if (std::find(except.begin(), except.end(), u.id) != except.end()) continue;
        held.push_back(u.pos);
    }

    std::vector<size_t> wall, line, rear;
    Fixed gap = kSlotGap;
    for (size_t i = 0; i < men.size(); ++i) {
        gap = max(gap, men[i]->radius * 2 + Fixed::fromRatio(1, 2));
        switch (garrisonRole(*men[i])) {
            case GarrisonRole::Wall: wall.push_back(i); break;
            case GarrisonRole::Foot: line.push_back(i); break;
            case GarrisonRole::Rear: rear.push_back(i); break;
        }
    }

    // The bowmen: an embrasure each, the two nearest the doors first (one either side; nothing stands over the gate), the rest
    // spread by an even pitch round the rest of the ring. More bowmen than embrasures: the others take the line, behind the foot.
    {
        std::vector<size_t> cand;
        for (size_t e = 0; e < ring.embrasures.size(); ++e) {
            bool taken = false;
            for (const Vec3& q : held)
                taken = taken || ((q - ring.embrasures[e].pos).flat().lengthSq() <= kEmbrasureTolerance * kEmbrasureTolerance && abs(q.z - ring.embrasures[e].pos.z) < kEmbrasureHeight);
            if (!taken) cand.push_back(e);
        }
        const size_t k = std::min(wall.size(), cand.size());
        std::vector<size_t> sel;
        if (k > 0) {
            size_t left = kNoGate, right = kNoGate;
            Fixed leftD, rightD;
            for (const size_t e : cand) {
                const Vec3 off = (ring.embrasures[e].pos - mid).flat();
                const Fixed d = off.lengthSq();
                if (off.dot(along) < Fixed::zero()) {
                    if (left == kNoGate || d < leftD) left = e, leftD = d;
                } else if (right == kNoGate || d < rightD) {
                    right = e, rightD = d;
                }
            }
            if (k == 1) {
                sel.push_back(left == kNoGate || (right != kNoGate && rightD < leftD) ? right : left);
            } else {
                if (left != kNoGate) sel.push_back(left);
                if (right != kNoGate) sel.push_back(right);
            }
            const size_t rest = k - std::min(k, sel.size());
            if (rest > 0) {
                // The others, in the order round the ring from the last of the pair on, at an even pitch.
                const size_t from = sel.back();
                std::vector<size_t> rem;
                for (size_t step = 1; step < ring.embrasures.size(); ++step) {
                    const size_t e = (from + step) % ring.embrasures.size();
                    if (std::find(cand.begin(), cand.end(), e) != cand.end() && std::find(sel.begin(), sel.end(), e) == sel.end()) rem.push_back(e);
                }
                const size_t take = std::min(rest, rem.size());
                for (size_t i = 0; i < take; ++i) sel.push_back(rem[(i * rem.size() + rem.size() / 2) / take]);
            }
        }
        for (size_t i = 0; i < wall.size(); ++i) {
            if (i < sel.size()) {
                const Rampart::Embrasure& em = ring.embrasures[sel[i]];
                out[wall[i]] = {em.pos, em.out, GarrisonRole::Wall};
            } else {
                line.push_back(wall[i]);
            }
        }
    }

    // The foot (and the bowmen over) in the horseshoe; the casters, healers, engines and the lord in the ranks behind them,
    // the lord in the middle.
    // (Fliers need room to run and take wing: they stand apart, at the back of the courtyard.)
    std::vector<size_t> behind, flyers;
    for (const size_t i : rear)
        if (men[i]->flyAltitude > Fixed::zero()) flyers.push_back(i);
        else if (men[i]->type != static_cast<uint8_t>(UnitType::Hero)) behind.push_back(i);
    for (const size_t i : rear)
        if (men[i]->flyAltitude <= Fixed::zero() && men[i]->type == static_cast<uint8_t>(UnitType::Hero)) behind.insert(behind.begin() + static_cast<std::ptrdiff_t>(behind.size() / 2), i);
    const int32_t nLine = static_cast<int32_t>(line.size()), nBehind = static_cast<int32_t>(behind.size());
    const std::vector<std::vector<Vec3>> ranks = yardRanks(g, ring, {nLine, nBehind}, gap, held);
    const std::vector<Vec3>& slotsLine = ranks[0];
    const std::vector<Vec3>& slotsRear = ranks[1];
    std::vector<Vec3> avoid = held;
    avoid.insert(avoid.end(), slotsLine.begin(), slotsLine.end());
    const auto face = [&](const Vec3& p) {
        const Vec3 d = (mid - p).flat();
        return d / max(Fixed::fromRatio(1, 10), d.length());
    };
    // A man a place was refused: the open ground nearest the back of the ranks.
    std::vector<Vec3> placed = avoid;
    placed.insert(placed.end(), slotsRear.begin(), slotsRear.end());
    const auto fallback = [&](int64_t extra = 12, Fixed apart = kSlotGap) {
        Vec3 want = mid - g.out * (kYardDepth + Fixed::fromInt(extra));
        want.z = rg.terrain().heightAt(want.x, want.y);
        Vec3 r = nearestFit(want, [&](const Vec3& q) {
            Vec3 c = q;
            c.z = rg.terrain().heightAt(c.x, c.y);
            for (const Vec3& h : placed)
                if ((h - c).flat().lengthSq() < apart * apart) return false;
            return courtyardSpot(ring, c);
        });
        r.z = rg.terrain().heightAt(r.x, r.y);
        placed.push_back(r);
        return r;
    };
    for (size_t k = 0; k < line.size(); ++k) {
        const Vec3 p = k < slotsLine.size() ? slotsLine[k] : fallback();
        out[line[k]] = {p, face(p), GarrisonRole::Foot};
    }
    for (size_t k = 0; k < behind.size(); ++k) {
        const Vec3 p = k < slotsRear.size() ? slotsRear[k] : fallback();
        out[behind[k]] = {p, face(p), GarrisonRole::Foot};
    }
    for (const size_t i : flyers) {
        const Vec3 p = fallback(30, max(kSlotGap * 3, men[i]->radius * 3));
        out[i] = {p, face(p), GarrisonRole::Foot};
    }
    return out;
}

void World::stationMen(size_t gate, const std::vector<EntityId>& ids) {
    std::vector<Unit*> men;
    std::vector<const UnitStats*> stats;
    std::vector<EntityId> live;
    for (const EntityId id : ids) {
        Unit* u = mutableUnit(id);
        if (u == nullptr || !u->alive()) continue;
        men.push_back(u);
        stats.push_back(&u->stats);
        live.push_back(id);
    }
    const std::vector<GarrisonPost> posts = garrisonPosts(gate, stats, live);
    if (posts.size() != men.size()) return;
    // Each to the nearest post of his kind (the walls, or the ground), the men in the order given.
    std::vector<uint8_t> taken(posts.size(), 0);
    for (size_t i = 0; i < men.size(); ++i) {
        size_t best = posts.size();
        Fixed bestD;
        for (size_t p = 0; p < posts.size(); ++p) {
            if (taken[p] || (posts[p].role == GarrisonRole::Wall) != (posts[i].role == GarrisonRole::Wall)) continue;
            const Fixed d = (posts[p].pos - men[i]->pos).flat().lengthSq();
            if (best == posts.size() || d < bestD) best = p, bestD = d;
        }
        if (best == posts.size()) continue;
        taken[best] = 1;
        Command c{men[i]->owner, men[i]->id, Command::Type::Move, posts[best].pos};
        c.face = posts[best].face;
        applyCommand(c);
    }
}

}  // namespace illyria::sim
