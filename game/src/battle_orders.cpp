// Attacks and the markers that show orders (docs/GAME_DESIGN.md §4.1.2):
// going after one soldier (of either side), at a gate, wall, house or tree,
// at the ground; the force march; what lies under the finger and what a tap
// there would do (the desktop's cursor); and where everyone is going.

#include <algorithm>
#include <cmath>

#include "illyria/game/battle.h"

namespace illyria::game {

using sim::UnitType;

namespace {

constexpr float kSquadTapRadiusPx = 48.0f;
// Within this of a wall's line on the ground, the tap is on the wall.
constexpr float kWallPickMetres = 2.5f;
// A house or tree: the ground within this of it, or its middle within this many pixels.
constexpr float kObstaclePickMetres = 1.0f;
constexpr float kObstaclePickPx = 26.0f;
// Order markers show this long after an order even when not selected.
constexpr uint32_t kMarkTicks = sim::kTicksPerSecond * 5 / 2;
// Closer than this to his marker, a soldier is there (it is hidden).
constexpr float kArrivedMetres = 0.6f;
// Higher than this above the ground, a point is on (or over) a deck.
constexpr float kOnDeck = 0.12f;

bool attackable(sim::Obstacle::Kind k) {
    return k == sim::Obstacle::Kind::House || k == sim::Obstacle::Kind::Pine || k == sim::Obstacle::Kind::Oak;
}

float segDist(const V3& p, const V3& a, const V3& b) {
    const V3 ab{b.x - a.x, b.y - a.y, 0};
    const float len2 = ab.x * ab.x + ab.y * ab.y;
    const float t = len2 > 0 ? std::clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / len2, 0.0f, 1.0f) : 0.0f;
    const float dx = p.x - (a.x + ab.x * t), dy = p.y - (a.y + ab.y * t);
    return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

void Battle::markOrdered(const std::vector<sim::EntityId>& units) {
    if (orderTick_.size() < world_->units().size()) orderTick_.resize(world_->units().size(), UINT32_MAX);
    for (sim::EntityId id : units) orderTick_[id - 1] = world_->tick();
}

void Battle::say(const std::string& notice) {
    notice_ = notice;
    noticeTick_ = world_->tick();
}

sim::EntityId Battle::nearestOf(const std::vector<sim::EntityId>& ids, const V3& p) const {
    sim::EntityId best = sim::kNoEntity;
    float bestD = 1e30f;
    for (sim::EntityId id : ids) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive()) continue;
        const V3 d = toV3(u.pos) - p;
        const float d2 = d.x * d.x + d.y * d.y;
        if (d2 < bestD) {
            bestD = d2;
            best = id;
        }
    }
    return best;
}

void Battle::orderAttackUnit(sim::EntityId target) {
    const sim::Unit* t = world_->findUnit(target);
    if (t == nullptr || !t->alive()) return;
    std::vector<sim::EntityId> mine = selectedAlive();
    std::erase(mine, target);
    if (mine.empty()) return;
    for (sim::EntityId id : mine) {
        const sim::Unit& u = world_->units()[id - 1];
        sim::Command c{kPlayer, id, sim::Command::Type::AttackUnit, t->pos};
        c.targetUnit = target;
        // The squad-mate left behind (nearest him) is whom he goes back to.
        if (const int s = squadOfUnit_[id - 1]; s >= 0) {
            std::vector<sim::EntityId> stay;
            for (sim::EntityId m : squads_[static_cast<size_t>(s)].members)
                if (m != target && !isSelected(m)) stay.push_back(m);
            c.rally = nearestOf(stay, toV3(u.pos));
        }
        pending_.push_back(c);
    }
    markOrdered(mine);
    voice_.order(Situation::Attack, *world_, mine);
    if (t->owner == kPlayer) say("Attacking our own!");
}

void Battle::orderAttackGround(const V3& at) {
    const std::vector<sim::EntityId> mine = selectedAlive();
    if (mine.empty()) return;
    std::vector<sim::EntityId> march;
    for (sim::EntityId id : mine) {
        if (world_->units()[id - 1].stats.ranged.muzzleSpeed > sim::Fixed::zero()) issue(kPlayer, id, sim::Command::Type::AttackGround, at);
        else march.push_back(id);
    }
    // The rest march there, fighting whoever they meet on the way.
    moveGroup(kPlayer, march, selFormation_, at, nullptr, selectionFacing());
    markOrdered(mine);
    voice_.order(Situation::Attack, *world_, mine);
}

void Battle::orderForceMarch(const V3& to, const V3* facing) {
    const std::vector<sim::EntityId> mine = selectedAlive();
    if (mine.empty()) return;
    moveGroup(kPlayer, mine, selFormation_, to, facing, selectionFacing(), true);
    voice_.order(Situation::Move, *world_, mine);
}

void Battle::orderAttackObject(const Pick& pick) {
    const std::vector<sim::EntityId> mine = selectedAlive();
    if (mine.empty()) return;
    if (pick.kind == Pick::Kind::Gate) {
        // Missile troops shoot the doors; the rest go through, hacking at them.
        const sim::Gate& g = region_->gates()[static_cast<size_t>(pick.index)];
        std::vector<sim::EntityId> through;
        for (sim::EntityId id : mine) {
            if (world_->units()[id - 1].stats.ranged.muzzleSpeed > sim::Fixed::zero()) {
                sim::Command c{kPlayer, id, sim::Command::Type::AttackObject, g.a};
                c.object = sim::Command::Object::Gate;
                c.objectIndex = pick.index;
                pending_.push_back(c);
            } else {
                through.push_back(id);
            }
        }
        if (!through.empty()) {
            const V3 centre = toV3(g.a + g.b) * 0.5f, out = toV3(g.out);
            const float side = (centreOf(through) - centre).dot(out) >= 0 ? -1.0f : 1.0f;
            moveGroup(kPlayer, through, selFormation_, centre + out * (10.0f * side), nullptr, out * side);
        }
        markOrdered(mine);
        voice_.order(Situation::Attack, *world_, mine);
        return;
    }
    // A wall, a house or a tree: only what flies and bursts harms anyone there.
    std::vector<sim::EntityId> able;
    for (sim::EntityId id : mine)
        if (sim::World::canBatter(world_->units()[id - 1].stats)) able.push_back(id);
    if (able.empty()) {
        say(pick.kind == Pick::Kind::Wall ? "Only engines, grenadiers and wizards can strike at a wall"
                                          : "Only engines, grenadiers and wizards can strike at that");
        return;
    }
    for (sim::EntityId id : able) {
        sim::Command c{kPlayer, id, sim::Command::Type::AttackObject, toSim(pick.at)};
        c.object = pick.kind == Pick::Kind::Wall ? sim::Command::Object::Wall : sim::Command::Object::Obstacle;
        c.objectIndex = pick.index;
        pending_.push_back(c);
    }
    markOrdered(able);
    voice_.order(Situation::Attack, *world_, able);
}

Battle::Pick Battle::pickAt(const Camera& camera, float x, float y, float pixelScale) const {
    Pick p;
    const auto& units = world_->units();
    if (const int u = unitAt(camera, x, y, pixelScale); u >= 0) {
        p.kind = units[static_cast<size_t>(u)].owner == kPlayer ? Pick::Kind::Own : Pick::Kind::Enemy;
        p.index = u;
        p.squad = squadOfUnit_[static_cast<size_t>(u)];
        p.at = unitPosition(static_cast<size_t>(u));
        return p;
    }
    if (const int g = gateAt(camera, x, y, pixelScale); g >= 0) {
        p.kind = Pick::Kind::Gate;
        p.index = g;
        p.at = toV3(region_->gates()[static_cast<size_t>(g)].a + region_->gates()[static_cast<size_t>(g)].b) * 0.5f;
        return p;
    }
    // Zoomed far out, soldiers are specks: the nearest squad centre.
    {
        float best = kSquadTapRadiusPx * pixelScale * kSquadTapRadiusPx * pixelScale;
        for (size_t s = 0; s < squads_.size(); ++s) {
            if (aliveCount(squads_[s]) == 0) continue;
            float px, py;
            if (!camera.project(squadCentre(squads_[s]), px, py)) continue;
            const float d2 = (px - x) * (px - x) + (py - y) * (py - y);
            if (d2 < best) {
                best = d2;
                p.kind = squads_[s].owner == kPlayer ? Pick::Kind::Own : Pick::Kind::Enemy;
                p.squad = static_cast<int>(s);
                p.at = squadCentre(squads_[s]);
            }
        }
        if (p.kind != Pick::Kind::None) return p;
    }
    V3 ground;
    if (!camera.pickGround(x, y, ground)) return p;
    p.kind = Pick::Kind::Ground;
    p.at = ground;
    // The castle walls: the tap lands at the wall's foot, or on its walk.
    const auto& rings = region_->ramparts();
    for (size_t r = 0; r < rings.size(); ++r) {
        const auto& cs = rings[r].corners;
        const float reach = static_cast<float>(rings[r].halfThick.toDouble()) + kWallPickMetres;
        for (size_t e = 0; e < cs.size(); ++e) {
            if (segDist(ground, toV3(cs[e]), toV3(cs[(e + 1) % cs.size()])) <= reach) {
                p.kind = Pick::Kind::Wall;
                p.index = static_cast<int>(r);
                return p;
            }
        }
    }
    // Houses and trees: the ground at them, or the middle of them on the screen.
    const auto& obs = region_->obstacles();
    float bestPx = kObstaclePickPx * pixelScale * kObstaclePickPx * pixelScale;
    for (size_t i = 0; i < obs.size(); ++i) {
        const sim::Obstacle& o = obs[i];
        if (!attackable(o.kind)) continue;
        const V3 c = toV3(o.pos);
        const float dx = ground.x - c.x, dy = ground.y - c.y;
        bool on;
        if (o.kind == sim::Obstacle::Kind::House)
            on = std::abs(dx) <= static_cast<float>(o.halfX.toDouble()) + kObstaclePickMetres &&
                 std::abs(dy) <= static_cast<float>(o.halfY.toDouble()) + kObstaclePickMetres;
        else
            on = dx * dx + dy * dy <= std::pow(static_cast<float>(o.radius.toDouble()) + kObstaclePickMetres, 2.0f);
        float d2 = on ? 0.0f : 1e30f;
        if (!on) {
            float px, py;
            if (dx * dx + dy * dy < 900.0f && camera.project(c + V3{0, 0, static_cast<float>(o.height.toDouble()) * 0.6f}, px, py))
                d2 = (px - x) * (px - x) + (py - y) * (py - y);
        }
        if (d2 < bestPx || on) {
            bestPx = d2;
            p.kind = Pick::Kind::Obstacle;
            p.index = static_cast<int>(i);
            if (on) break;
        }
    }
    return p;
}

Battle::Intent Battle::intentAt(const Camera& camera, float x, float y, float pixelScale) const {
    const Pick p = pickAt(camera, x, y, pixelScale);
    const std::vector<sim::EntityId> mine = selectedAlive();
    if (mine.empty()) return p.kind == Pick::Kind::Own ? Intent::Select : Intent::None;
    const OrderMode mode = forceKey_ ? OrderMode::Attack : marchKey_ ? OrderMode::ForceMarch : orderMode_;
    using K = Pick::Kind;
    if (mode == OrderMode::ForceMarch) return p.kind == K::None ? Intent::None : Intent::ForceMarch;
    if (mode == OrderMode::Attack) {
        switch (p.kind) {
            case K::Own:
            case K::Enemy: return Intent::Attack;
            case K::Gate: return Intent::AttackObject;
            case K::Wall:
            case K::Obstacle:
                for (sim::EntityId id : mine)
                    if (sim::World::canBatter(world_->units()[id - 1].stats)) return Intent::AttackObject;
                return Intent::Cannot;
            case K::Ground: return Intent::AttackGround;
            case K::None: return Intent::None;
        }
    }
    switch (p.kind) {
        case K::Own: return Intent::Select;
        case K::Enemy: return Intent::Attack;
        case K::Gate: return Intent::AttackObject;
        case K::Wall:
        case K::Obstacle:
        case K::Ground: return Intent::Move;
        case K::None: break;
    }
    return Intent::None;
}

float Battle::floorAt(const V3& p) const {
    // The ground, or a deck (a wall-walk, a stair, a bridge) he stands on or is above.
    const float ground = heightAt(p.x, p.y);
    sim::Fixed deck;
    if (p.z > ground + kOnDeck && inBattlefield(p.x, p.y) &&
        region_->deckBelow(toFixed(p.x), toFixed(p.y), toFixed(p.z + 0.5f), deck))
        return std::max(ground, static_cast<float>(deck.toDouble()));
    return ground;
}

float Battle::markOpacity(size_t index) const {
    if (isSelected(static_cast<sim::EntityId>(index + 1))) return 1.0f;
    if (index >= orderTick_.size() || orderTick_[index] == UINT32_MAX) return 0.0f;
    const uint32_t since = world_->tick() - orderTick_[index];
    if (since >= kMarkTicks) return 0.0f;
    // Full at first, fading out over the last second.
    return std::min(1.0f, static_cast<float>(kMarkTicks - since) / static_cast<float>(sim::kTicksPerSecond));
}

std::vector<Battle::OrderMark> Battle::orderMarks() const {
    std::vector<OrderMark> out;
    const auto& units = world_->units();
    // Whose orders show: each soldier selected (not the rest of his squad or
    // group), and for a moment, fading, anyone just ordered. Only his current
    // order: a new one, or his arrival, takes the old marker away.
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        if (!u.alive() || u.owner != kPlayer || u.routed) continue;
        const float opacity = markOpacity(i);
        if (opacity <= 0.0f) continue;
        const V3 at = unitPosition(i);
        if (u.targetUnit != sim::kNoEntity) {
            const sim::Unit& t = units[u.targetUnit - 1];
            if (!t.alive()) continue;
            const V3 tp = unitPosition(u.targetUnit - 1);
            OrderMark m{OrderMark::Kind::Target, u.id, {tp.x, tp.y, floorAt(tp)}, {}, at};
            m.size = static_cast<float>(t.stats.radius.toDouble()) * 2.0f + 0.5f;
            m.opacity = opacity;
            out.push_back(m);
        } else if (u.hasAttackTarget) {
            const V3 aim = toV3(u.attackTarget);
            OrderMark m{OrderMark::Kind::Spot, u.id, {aim.x, aim.y, floorAt(aim)}, {}, at, u.attackGate >= 0 ? 2.2f : 1.6f};
            m.opacity = opacity;
            out.push_back(m);
        } else if (u.hasMoveTarget) {
            const V3 dest = toV3(u.moveTarget);
            const float size = std::max(0.7f, static_cast<float>(u.stats.radius.toDouble()) * 1.6f);
            // As good as there (jostled just off his place): no marker.
            const float dx = dest.x - at.x, dy = dest.y - at.y;
            if (dx * dx + dy * dy < kArrivedMetres * kArrivedMetres && std::abs(dest.z - at.z) < 1.0f) continue;
            V3 face = toV3(u.arriveFacing);
            if (face.length() < 0.5f) face = V3{dest.x - at.x, dest.y - at.y, 0}.normalized();
            if (face.length() < 0.5f) face = toV3(u.facing);
            // A flier that flies there lands on the topmost deck there (a
            // wall-walk), whatever height his slot was given.
            const bool flies = u.stats.flyAltitude > sim::Fixed::zero() &&
                               (u.flight != sim::Unit::Flight::Grounded ||
                                dx * dx + dy * dy >= static_cast<float>((u.stats.minFlight * u.stats.minFlight).toDouble()));
            const float floor = floorAt(flies ? V3{dest.x, dest.y, dest.z + 1000.0f} : dest);
            OrderMark m{OrderMark::Kind::Slot, u.id, {dest.x, dest.y, floor}, face, at};
            m.size = size;
            m.opacity = opacity;
            out.push_back(m);
        }
    }
    return out;
}

}  // namespace illyria::game
