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

enum class Post : uint8_t { None, Archer, Foot, Lord };

// What a man is to the garrison: bowmen to the walls, the fighting rest to the horseshoe; engines, healers and the broken
// keep to themselves.
Post postOfStats(const UnitStats& s) {
    if (s.healing.amount > 0) return Post::None;
    // The lord stands in the rank behind the foot (garrisonPosts) and keeps that place when a foe comes: he is the garrison's,
    // but no station of the gate's calls him into the horseshoe.
    if (s.type == static_cast<uint8_t>(UnitType::Hero)) return Post::Lord;
    if (s.ranged.muzzleSpeed > Fixed::zero()) return World::canBatter(s) ? Post::None : Post::Archer;
    return s.melee.damage > 0 ? Post::Foot : Post::None;
}

Post postOf(const Unit& u) {
    if (!u.alive() || u.routed || u.inAir() || u.flight != Unit::Flight::Grounded) return Post::None;
    return postOfStats(u.stats);
}

// A man with an order of his own (a move, an attack or orders waiting, unless the garrison's own: Unit::stationed) is not at the
// gate's disposal: he is no keeper, and is never pulled to a post. Once the order is done he is a garrison man again. (The computer
// lords' and neutral towns' men have no orders but their stations by construction.)
bool ordered(const Unit& u) {
    if (!u.queue.empty()) return true;
    if (u.stationed) return false;
    return u.hasMoveTarget || u.hasAttackTarget || (u.targetUnit != kNoEntity && !u.targetChosen);
}

bool idle(const Unit& u) { return !u.hasMoveTarget && !u.hasAttackTarget && u.targetUnit == kNoEntity && u.foe == kNoEntity && !u.engaged; }

}  // namespace

bool World::gateKeeper(const Unit& u) const {
    if (!config_.region || gates_.empty() || postOf(u) == Post::None || ordered(u)) return false;
    const Region& rg = *config_.region;
    for (const size_t i : rg.gatesNear(u.pos.x, u.pos.y)) {
        const GateState& st = gates_[i];
        if (!st.garrisoned || st.holder != u.owner) continue;
        const Gate& g = rg.gates()[i];
        const Fixed d2 = (u.pos - (g.a + g.b) * Fixed::fromRatio(1, 2)).flat().lengthSq();
        if ((st.recalls && d2 <= kRecallRange * kRecallRange) || (d2 <= kKeepRadius * kKeepRadius && withinRing(rg.ramparts()[static_cast<size_t>(g.ring)], u.pos))) return true;
    }
    return false;
}

std::vector<size_t> World::doorsOfRing(size_t gate) const {
    // The gates of the ring this one opens, in the world's order: a castle with two doors has one garrison between them.
    std::vector<size_t> out;
    if (!config_.region || gate >= config_.region->gates().size()) return out;
    const auto& gs = config_.region->gates();
    for (size_t j = 0; j < gs.size(); ++j)
        if (gs[j].ring == gs[gate].ring) out.push_back(j);
    return out;
}

void World::keepGates() {
    if (!config_.region || !nav_) return;
    const Region& rg = *config_.region;
    for (size_t i = 0; i < gates_.size() && i < rg.gates().size(); ++i) {
        const GateState& lead = gates_[i];
        if (!lead.garrisoned || lead.holder == GateState::kNoHolder) continue;
        // The doors of one ring and one holder are kept together, by the first of them, on its beat: one garrison, shared out by
        // where the foes are, so that no door is left bare and none is crowded (a breach at one door draws on the others).
        std::vector<size_t> doors;
        for (const size_t j : doorsOfRing(i))
            if (j < gates_.size() && gates_[j].garrisoned && gates_[j].holder == lead.holder) doors.push_back(j);
        if (doors.empty() || doors.front() != i || (tick_ + i * 3) % kKeepThink != 0) continue;
        keepDoors(doors);
    }
}

void World::keepDoors(const std::vector<size_t>& doors) {
    const Region& rg = *config_.region;
    const size_t nd = doors.size();
    const PlayerId holder = gates_[doors.front()].holder;
    const Rampart& ring = rg.ramparts()[static_cast<size_t>(rg.gates()[doors.front()].ring)];
    std::vector<Vec3> mids(nd);
    for (size_t d = 0; d < nd; ++d) mids[d] = (rg.gates()[doors[d]].a + rg.gates()[doors[d]].b) * Fixed::fromRatio(1, 2);
    // Which door a place is nearest (the first on a tie).
    const auto nearestDoor = [&](const Vec3& p) {
        size_t best = 0;
        Fixed bestD2 = (p - mids[0]).flat().lengthSq();
        for (size_t d = 1; d < nd; ++d) {
            const Fixed d2 = (p - mids[d]).flat().lengthSq();
            if (d2 < bestD2) best = d, bestD2 = d2;
        }
        return best;
    };

    // Who is about: the garrison (in the world's order) and the foes, near any of the doors.
    std::vector<uint32_t> near;
    for (size_t d = 0; d < nd; ++d) unitGrid_.query(mids[d].x, mids[d].y, kAlertRange, [&](uint32_t j) { near.push_back(j); });
    std::sort(near.begin(), near.end());
    near.erase(std::unique(near.begin(), near.end()), near.end());
    struct Keeper {
        Unit* u;
        size_t door;
    };
    std::vector<Keeper> archers, foot;
    // By door: foes at the doors outside, all foes outside within the alert range, and inside.
    std::vector<int32_t> outside(nd, 0), coming(nd, 0), inside(nd, 0);
    bool alert = false;
    for (const uint32_t j : near) {
        Unit& u = units_[j];
        if (!u.alive() || u.routed) continue;
        if (u.owner != holder) {
            for (size_t d = 0; d < nd; ++d) {
                const Fixed d2 = (u.pos - mids[d]).flat().lengthSq();
                if (d2 > kAlertRange * kAlertRange) continue;
                alert = true;
                if (u.inAir() || u.flight != Unit::Flight::Grounded) continue;
                if (townSide(rg.gates()[doors[d]], ring, u.pos)) inside[d] += d2 <= kInsideRange * kInsideRange;
                else ++coming[d], outside[d] += d2 <= kLureRange * kLureRange;
            }
            continue;
        }
        bool mine = false;
        for (size_t d = 0; d < nd; ++d) {
            const Fixed d2 = (u.pos - mids[d]).flat().lengthSq();
            mine = mine || (d2 <= kKeepRadius * kKeepRadius && (withinRing(ring, u.pos) || (gates_[doors[d]].recalls && d2 <= kRecallRange * kRecallRange)));
        }
        if (!mine) continue;
        const Post post = postOf(u);
        if (ordered(u)) continue;
        // His door: the one he stands at, or (walking to a station) the one he is bound for.
        const size_t home = nearestDoor(u.stationed && u.hasMoveTarget ? u.moveTarget : u.pos);
        if (post == Post::Archer) archers.push_back({&u, home});
        else if (post == Post::Foot) foot.push_back({&u, home});
    }
    if (!alert) return;

    // A door's horseshoe for n men: the places the wall or a house leaves free, the middle rank deepest, the wings bent in toward
    // the doors.
    const auto horseshoe = [&](size_t d, int32_t n, Fixed gap) {
        const Gate& g = rg.gates()[doors[d]];
        const Vec3 inward = -g.out;
        const Vec3 along = (g.b - g.a).flat() / max(Fixed::fromRatio(1, 10), (g.b - g.a).flat().length());
        std::vector<Vec3> slots;
        // At a door with foes inside the walls (a breach) a place the wall or a house takes is made up from a rank farther back: the
        // men sent over from the other door (the quotas, below) must all find one, or they stand where they were and the breach
        // has no reserves. (Elsewhere the places are left as the courtyard gives them: the lord stands behind the foot.)
        const int32_t reach = nd > 1 && inside[d] > 0 ? n * 3 : n;
        for (int32_t k = 0; k < reach && static_cast<int32_t>(slots.size()) < n; ++k) {
            const int32_t rank = k / kRankWidth, inRank = n - rank * kRankWidth > 0 ? std::min(kRankWidth, n - rank * kRankWidth) : kRankWidth;
            Vec3 p;
            // A place the wall or a house takes is brought in toward the axis until it is free (else left to the others).
            Fixed lateral = gap * (Fixed::fromInt(k % kRankWidth * 2 - (inRank - 1)) / 2);
            for (int tries = 0; tries < 4; ++tries) {
                const Fixed depth = kHoldDepth + gap * rank - lateral * lateral / (kBowlRadius * 2);
                p = mids[d] + inward * depth + along * lateral;
                p.z = rg.terrain().heightAt(p.x, p.y);
                if (courtyardSpot(ring, p)) break;
                lateral = lateral * 3 / 4;
                p = {};
            }
            if (p == Vec3{}) continue;
            slots.push_back(p);
        }
        return slots;
    };
    Fixed gap = kSlotGap;
    for (const Keeper& k : foot) gap = max(gap, k.u->stats.radius * 2 + Fixed::fromRatio(1, 2));
    // What each door can take: the embrasures within reach of it, and the places of its horseshoe (a door that cannot hold more
    // men than it has room for is not given them).
    std::vector<std::vector<size_t>> order(nd);
    std::vector<int64_t> bowRoom(nd), footRoom(nd);
    for (size_t d = 0; d < nd; ++d) {
        for (size_t e = 0; e < ring.embrasures.size(); ++e)
            if ((ring.embrasures[e].pos - mids[d]).flat().lengthSq() <= kKeepRadius * kKeepRadius) order[d].push_back(e);
        std::stable_sort(order[d].begin(), order[d].end(), [&](size_t a, size_t b) {
            return (ring.embrasures[a].pos - mids[d]).flat().lengthSq() < (ring.embrasures[b].pos - mids[d]).flat().lengthSq();
        });
        bowRoom[d] = static_cast<int64_t>(order[d].size());
        footRoom[d] = static_cast<int64_t>(horseshoe(d, static_cast<int32_t>(foot.size()), gap).size());
    }

    // How the garrison is shared: by the foes at each door (those inside the walls count double), but no door with less than a
    // third of what the busiest has (a door not yet attacked keeps a guard, and the foes are as likely to come to it next); and
    // a man moves only when a door is two men over or under its share, so that the shares do not seesaw as the foes come and
    // go. The man who goes is the one of the surplus door nearest the door that wants him, sent whatever he was doing for the
    // garrison.
    std::vector<EntityId> sent;
    std::vector<Vec3> scratch;
    std::vector<std::vector<Vec3>> heads(nd);
    for (size_t d = 0; d < nd; ++d) {
        const std::vector<Vec3> all = horseshoe(d, static_cast<int32_t>(foot.size()), gap);
        if (!all.empty()) heads[d] = {all[all.size() / 2]};
    }
    const auto share = [&](std::vector<Keeper>& men, const std::vector<int64_t>& room, bool walk) {
        const int64_t total = static_cast<int64_t>(men.size());
        std::vector<int64_t> weight(nd), quota(nd), have(nd, 0);
        int64_t sum = 0, most = 0;
        for (size_t d = 0; d < nd; ++d) most = std::max<int64_t>(most, coming[d] + 2 * inside[d]);
        for (size_t d = 0; d < nd; ++d) sum += weight[d] = std::max<int64_t>(coming[d] + 2 * inside[d], (most + 2) / 3);
        int64_t given = 0;
        for (size_t d = 0; d < nd; ++d) given += quota[d] = total * weight[d] / sum;
        while (given < total) {
            // The rest, to the door with the largest part of a man owing.
            size_t pick = 0;
            for (size_t d = 1; d < nd; ++d)
                if (total * weight[d] - quota[d] * sum > total * weight[pick] - quota[pick] * sum) pick = d;
            ++quota[pick];
            ++given;
        }
        // (No more than a door has room for: the rest to the doors with room left.)
        for (size_t d = 0; d < nd; ++d)
            while (quota[d] > room[d]) {
                size_t spare = nd;
                for (size_t e = 0; e < nd; ++e)
                    if (e != d && quota[e] < room[e] && (spare == nd || room[e] - quota[e] > room[spare] - quota[spare])) spare = e;
                if (spare == nd) break;
                --quota[d];
                ++quota[spare];
            }
        for (const Keeper& k : men) ++have[k.door];
        for (int guard = 0; guard < 64; ++guard) {
            size_t over = nd, under = nd;
            for (size_t d = 0; d < nd; ++d) {
                if (have[d] - quota[d] >= 2 && (over == nd || have[d] - quota[d] > have[over] - quota[over])) over = d;
                if (quota[d] - have[d] >= 2 && (under == nd || quota[d] - have[d] > quota[under] - have[under])) under = d;
            }
            if (over == nd || under == nd) break;
            size_t who = men.size();
            Fixed whoD2;
            for (size_t m = 0; m < men.size(); ++m) {
                if (men[m].door != over || men[m].u->engaged || men[m].u->foe != kNoEntity) continue;
                // (A man on foot goes only where the ground leads him: a back door may lie behind walls from the front.)
                if (!walk && (heads[under].empty() || !nav_->findPath(men[m].u->pos, heads[under].front(), scratch, men[m].u->stats.gait))) continue;
                const Fixed d2 = (men[m].u->pos - mids[under]).flat().lengthSq();
                if (who == men.size() || d2 < whoD2) who = m, whoD2 = d2;
            }
            if (who == men.size()) break;
            men[who].door = under;
            sent.push_back(men[who].u->id);
            --have[over];
            ++have[under];
        }
    };
    share(archers, bowRoom, true);
    share(foot, footRoom, false);
    const auto wasSent = [&](const Unit* u) { return std::find(sent.begin(), sent.end(), u->id) != sent.end(); };

    // The archers up on the walls: an embrasure each, the nearest their door first. Those already at one are counted first, so
    // that no man is sent to a place another holds, whichever door he is of.
    std::vector<uint8_t> taken(ring.embrasures.size(), 0);
    std::vector<int32_t> manned(nd, 0), archersAt(nd, 0), formed(nd, 0), footAt(nd, 0);

    std::vector<Keeper> loose;
    for (const Keeper& k : archers) {
        ++archersAt[k.door];
        size_t at = ring.embrasures.size();
        for (size_t d = 0; d < nd && at == ring.embrasures.size(); ++d)
            for (const size_t e : order[d]) {
                const Rampart::Embrasure& em = ring.embrasures[e];
                if (!taken[e] && (k.u->pos - em.pos).flat().lengthSq() <= kEmbrasureTolerance * kEmbrasureTolerance && abs(k.u->pos.z - em.pos.z) < kEmbrasureHeight) {
                    at = e;
                    break;
                }
            }
        if (at < ring.embrasures.size() && !wasSent(k.u)) {
            taken[at] = 1;
            ++manned[k.door];
        } else if (idle(*k.u) || wasSent(k.u)) {
            loose.push_back(k);
        }
    }
    for (const Keeper& k : loose) {
        for (const size_t e : order[k.door]) {
            if (taken[e]) continue;
            taken[e] = 1;
            Unit* u = k.u;
            u->routeFrom = u->pos;
            beginMove(*u, ring.embrasures[e].pos, Fixed::zero(), ring.embrasures[e].out);
            planPath(*u, ring.embrasures[e].pos);
            u->stationed = true;
            break;
        }
    }

    // The foot in a horseshoe behind each door, the mouth of the gate its kill zone: the middle rank deepest, the wings bent in
    // toward the doors, every man facing the mouth.
    for (size_t d = 0; d < nd; ++d) {
        const Vec3& mid = mids[d];
        std::vector<Unit*> men;
        for (const Keeper& k : foot)
            if (k.door == d) men.push_back(k.u);
        footAt[d] = static_cast<int32_t>(men.size());
        if (men.empty()) continue;
        const std::vector<Vec3> slots = horseshoe(d, static_cast<int32_t>(men.size()), gap);
        // Those already at a place keep it; the rest (idle, or sent here from the other door) go to the nearest places left.
        std::vector<uint8_t> used(slots.size(), 0);
        std::vector<Unit*> unplaced;
        for (Unit* u : men) {
            size_t at = slots.size();
            Fixed atD2;
            for (size_t s = 0; s < slots.size(); ++s) {
                const Fixed d2 = (slots[s] - u->pos).flat().lengthSq();
                if (!used[s] && d2 <= kSlotTolerance * kSlotTolerance && (at == slots.size() || d2 < atD2)) at = s, atD2 = d2;
            }
            if (at < slots.size()) used[at] = 1, ++formed[d];
            else unplaced.push_back(u);
        }
        for (Unit* u : unplaced) {
            size_t best = slots.size();
            Fixed bestD2;
            for (size_t s = 0; s < slots.size(); ++s) {
                const Fixed d2 = (slots[s] - u->pos).flat().lengthSq();
                if (!used[s] && (best == slots.size() || d2 < bestD2)) best = s, bestD2 = d2;
            }
            if (best == slots.size()) continue;
            used[best] = 1;
            if (!idle(*u) && !wasSent(u)) continue;
            u->routeFrom = u->pos;
            beginMove(*u, slots[best], Fixed::zero(), mid - slots[best]);
            planPath(*u, slots[best]);
            u->stationed = true;
        }
    }

    // A foe who is in reach, inside the walls or through open doors, is met by the foot who stand idle, of whichever door (then
    // back to the places); the foe outside a shut gate is not walked at.
    for (const Keeper& k : foot) {
        Unit* u = k.u;
        if (!idle(*u)) continue;
        Unit* best = nullptr;
        Fixed bestD2;
        for (const uint32_t j : near) {
            Unit& f = units_[j];
            if (f.owner == holder || !f.alive() || f.routed || f.inAir() || f.flight != Unit::Flight::Grounded) continue;
            bool reach = false;
            for (size_t d = 0; d < nd && !reach; ++d)
                reach = (f.pos - mids[d]).flat().lengthSq() <= kMeetRange * kMeetRange && (gates_[doors[d]].passable() || townSide(rg.gates()[doors[d]], ring, f.pos));
            if (!reach) continue;
            const Fixed d2 = (f.pos - u->pos).flat().lengthSq();
            if (best == nullptr || d2 < bestD2) best = &f, bestD2 = d2;
        }
        if (best == nullptr) continue;
        Command c{u->owner, u->id, Command::Type::AttackUnit, {}};
        c.targetUnit = best->id;
        applyCommand(c);
        u->stationed = true;
    }

    // The doors: kept shut to what is outside, and swung open for a few seconds to draw foes in when the foot are formed to
    // take them, the room inside is not yet full of them, and the doors are failing or the foes few.
    for (size_t d = 0; d < nd; ++d) {
        GateState& st = gates_[doors[d]];
        const Gate& g = rg.gates()[doors[d]];
        if (st.broken() || footAt[d] == 0 || outside[d] == 0 || inside[d] >= footAt[d]) continue;
        const bool formedUp = formed[d] * 100 >= footAt[d] * kFormedPct && manned[d] * 100 >= archersAt[d] * kMannedPct;
        const bool failing = static_cast<int64_t>(st.hp) * 100 < static_cast<int64_t>(g.maxHp) * kLureHpPct;
        if (formedUp && (failing || coming[d] <= footAt[d] * kLureOddsPerMan))
            st.openUntil = std::max(st.openUntil, tick_ + static_cast<uint32_t>(kLureOpenSeconds * kTicksPerSecond));
    }
}

// ---------------------------------------------------------------- where a garrison is put

bool World::courtyardSpot(const Rampart& ring, const Vec3& p) const {
    // On the courtyard's own ground: not the foot of a ramp or a stair, nor under the wall-walk's gallery (an order there is an
    // order up onto the walk).
    if (!standable(p) || !Gate::inside(ring, p) || abs(groundHeight(p.x, p.y) - p.z) >= Fixed::fromRatio(1, 4) ||
        config_.region->rampartAt(p.x, p.y, ring.corners.front().z) >= 0)
        return false;
    // Not inside an inner bailey (a ring within this one): its doors are shut, and a man put there could not come out. A back door
    // has the bailey close behind it, and its horseshoe is the narrow ground between.
    for (const Rampart& inner : config_.region->ramparts()) {
        if (&inner == &ring || !Gate::inside(inner, p)) continue;
        bool within = !inner.corners.empty();
        for (const Vec3& c : inner.corners) within = within && Gate::inside(ring, c);
        if (within) return false;
    }
    return true;
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
        // (The outer ring's: its gate is farthest from the heart; of a ring with two, the first, whose road leads to the town.)
        if (g.side) continue;
        if (best == kNoGate || d > bestD) best = i, bestD = d;
    }
    return best;
}

std::vector<World::GarrisonPost> World::garrisonPosts(size_t gate, const std::vector<const UnitStats*>& men, const std::vector<EntityId>& except) const {
    if (!config_.region || gate >= config_.region->gates().size()) return {};
    // A ring with more than one door: the bowmen and the foot are dealt out between the doors in turn (this one first, which
    // also has the lord and the rest), and each door's share is placed by the one rule, clear of the places already given.
    std::vector<size_t> doors{gate};
    for (const size_t j : doorsOfRing(gate))
        if (j != gate) doors.push_back(j);
    if (doors.size() == 1) return garrisonPostsAt(gate, men, except, {});
    std::vector<std::vector<size_t>> share(doors.size());
    size_t nextWall = 0, nextFoot = 0;
    for (size_t i = 0; i < men.size(); ++i) {
        switch (garrisonRole(*men[i])) {
            case GarrisonRole::Wall: share[nextWall++ % doors.size()].push_back(i); break;
            case GarrisonRole::Foot: share[nextFoot++ % doors.size()].push_back(i); break;
            case GarrisonRole::Rear: share[0].push_back(i); break;
        }
    }
    std::vector<GarrisonPost> out(men.size());
    std::vector<Vec3> given;
    for (size_t d = 0; d < doors.size(); ++d) {
        std::vector<const UnitStats*> mine;
        for (const size_t i : share[d]) mine.push_back(men[i]);
        if (mine.empty()) continue;
        const std::vector<GarrisonPost> posts = garrisonPostsAt(doors[d], mine, except, given);
        for (size_t k = 0; k < posts.size(); ++k) {
            out[share[d][k]] = posts[k];
            given.push_back(posts[k].pos);
        }
    }
    return out;
}

std::vector<World::GarrisonPost> World::garrisonPostsAt(size_t gate, const std::vector<const UnitStats*>& men, const std::vector<EntityId>& except, const std::vector<Vec3>& alsoHeld) const {
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
    held.insert(held.end(), alsoHeld.begin(), alsoHeld.end());

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
        men[i]->stationed = true;  // (a placing, as the keepers' own)
    }
}

}  // namespace illyria::sim
