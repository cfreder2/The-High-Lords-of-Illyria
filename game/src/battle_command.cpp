// Commanding the battle, as in Myth: selecting soldiers (one man, a squad,
// everyone of a type in view, a box, saved groups), formations, marching with
// a facing, and scattering. Every order becomes sim::Commands in pending_, so
// it reaches the simulation (and every lockstep peer) the same way.

#include <algorithm>
#include <cmath>

#include "illyria/game/battle.h"

namespace illyria::game {

using sim::UnitType;

namespace {

constexpr float kMinSpacing = 1.8f;
constexpr float kTapRadiusPx = 30.0f;
constexpr float kScatterMetres = 7.0f;  // how far they run from the centre
constexpr float kAimMetres = 1.5f;      // drag this far from the destination to set a facing
constexpr float kBoxSlopPx = 12.0f;     // a smaller box is a tap
constexpr float kCrescent = 3.84f;      // Encircle: 220 degrees of arc
constexpr float kVeeStep = 0.8f;        // V: each man this many gaps forward and out (1.13 gaps apart)

uint32_t hash32(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

V3 flat(const V3& v) { return {v.x, v.y, 0}; }

}  // namespace

const char* formationName(Formation f) {
    switch (f) {
        case Formation::Line: return "Line";
        case Formation::Box: return "Box";
        case Formation::Wedge: return "Wedge";
        case Formation::Loose: return "Loose";
        case Formation::Column: return "Column";
        case Formation::Staggered: return "Staggered";
        case Formation::ShortLine: return "Short line";
        case Formation::Encircle: return "Encircle";
        case Formation::Vee: return "V";
    }
    return "?";
}

std::vector<FormationSlot> formationLayout(Formation f, int count, float spacing) {
    std::vector<FormationSlot> slots;
    if (count <= 0) return slots;
    // Ranks of `perRow`, `gap` apart side to side and `depth` front to back;
    // `stagger` shifts every other rank by that share of a gap (a short rank
    // centred is already shifted by half).
    const auto rows = [&](int perRow, float gap, float depth, float stagger) {
        for (int i = 0; i < count; ++i) {
            const int row = i / perRow, inRow = std::min(perRow, count - row * perRow);
            float col = static_cast<float>(i % perRow) - (inRow - 1) / 2.0f;
            if (row % 2 == 1 && (perRow - inRow) % 2 == 0) col += stagger;
            slots.push_back({{col * gap, -row * depth, 0}});
        }
    };
    switch (f) {
        case Formation::Line: rows(std::max(1, (count + 1) / 2), spacing, spacing, 0); break;  // two ranks
        case Formation::ShortLine: rows(std::max(1, (count + 2) / 3), spacing, spacing, 0); break;  // three
        case Formation::Box: rows(std::max(1, static_cast<int>(std::ceil(std::sqrt(count * 1.4f)))), spacing, spacing, 0); break;
        case Formation::Column: rows(2, spacing, spacing, 0); break;
        case Formation::Staggered:
            // Two ranks, each man in the gap of the rank before: no two nearer
            // than 1.8 gaps, so one bomb or fireball takes one man, not three.
            rows(std::max(1, (count + 1) / 2), spacing * 2.0f, spacing * 1.5f, 0.5f);
            break;
        case Formation::Loose:
            // A wide skirmish line, each man a little out of true.
            rows(std::max(1, (count + 1) / 2), spacing * 2.2f, spacing * 2.2f, 0);
            for (size_t i = 0; i < slots.size(); ++i) {
                const float h = static_cast<float>((i * 2654435761u) % 1000) / 1000.0f;
                slots[i].pos = slots[i].pos + V3{(h - 0.5f) * spacing * 0.6f, (std::fmod(h * 7.0f, 1.0f) - 0.5f) * spacing * 0.8f, 0};
            }
            break;
        case Formation::Wedge:
            // One at the point, two behind, three behind them...
            for (int row = 0, placed = 0; placed < count; ++row) {
                const int inRow = std::min(row + 1, count - placed);
                for (int k = 0; k < inRow; ++k, ++placed)
                    slots.push_back({{(static_cast<float>(k) - (inRow - 1) / 2.0f) * spacing, -row * spacing, 0}});
            }
            break;
        case Formation::Vee:
            // The point at the back, the arms reaching forward and out at 45
            // degrees, one man a step forward and to the side of the last.
            for (int i = 0; i < count; ++i) {
                const int k = (i + 1) / 2;
                const float side = i == 0 ? 0.0f : (i % 2 == 1 ? -1.0f : 1.0f);
                slots.push_back({{side * k * spacing * kVeeStep, k * spacing * kVeeStep, 0}});
            }
            break;
        case Formation::Encircle: {
            // A crescent around the point itself (the foe to pin, or the hero
            // to guard), bowed forward, everyone facing out. The radius gives
            // each man his gap along the arc, and never closes in on the centre.
            const float step = count > 1 ? kCrescent / static_cast<float>(count - 1) : 0;
            const float radius = std::max(spacing * 2.5f, count > 1 ? spacing / (2.0f * std::sin(step / 2)) : 0);
            for (int i = 0; i < count; ++i) {
                const float a = count > 1 ? -kCrescent / 2 + step * static_cast<float>(i) : 0;
                const V3 out{std::sin(a), std::cos(a), 0};
                slots.push_back({out * radius, out});
            }
            return slots;  // centred on the point, not on the men
        }
    }
    // Centre the formation on its own centroid.
    V3 mean;
    for (const FormationSlot& s : slots) mean = mean + s.pos;
    mean = mean * (1.0f / static_cast<float>(slots.size()));
    for (FormationSlot& s : slots) s.pos = s.pos - mean;
    return slots;
}

std::vector<V3> formationSlots(Formation f, int count, float spacing) {
    std::vector<V3> out;
    for (const FormationSlot& s : formationLayout(f, count, spacing)) out.push_back(s.pos);
    return out;
}

float formationPace(Formation f) {
    switch (f) {
        case Formation::Box: return 0.85f;  // shoulder to shoulder, every way
        case Formation::Line:
        case Formation::ShortLine:
        case Formation::Wedge:
        case Formation::Vee:
        case Formation::Encircle: return 0.9f;
        case Formation::Staggered: return 0.95f;
        case Formation::Loose:
        case Formation::Column: return 1.0f;  // the column of march
    }
    return 0.9f;
}

float formationSpacing(UnitType type) {
    return std::max(kMinSpacing, static_cast<float>(sim::unitStats(type).radius.toDouble()) * 2.6f);
}

Formation defaultFormation(UnitType type) {
    switch (roleOf(type)) {
        case Role::Ranged:
        case Role::Bomber: return Formation::Staggered;
        case Role::Caster:
        case Role::Healer:
        case Role::Flier: return Formation::Loose;
        case Role::Siege: return Formation::Line;
        case Role::Melee: break;
    }
    switch (type) {
        case UnitType::LightCavalry:
        case UnitType::HeavyCavalry:
        case UnitType::WolfRiders:
        case UnitType::Unicorns: return Formation::Wedge;
        case UnitType::Berserker: return Formation::Loose;
        case UnitType::Hero: return Formation::Box;
        default: return Formation::Line;
    }
}

// --- Selection --------------------------------------------------------------

std::vector<sim::EntityId> Battle::selectedAlive() const {
    std::vector<sim::EntityId> out;
    for (sim::EntityId id : selection_)
        if (world_->units()[id - 1].alive()) out.push_back(id);
    return out;
}

void Battle::refreshSelection() {
    std::sort(selection_.begin(), selection_.end());
    selection_.erase(std::unique(selection_.begin(), selection_.end()), selection_.end());
    selectedFlag_.assign(world_->units().size(), 0);
    for (sim::EntityId id : selection_) selectedFlag_[id - 1] = 1;
}

void Battle::select(std::vector<sim::EntityId> units) {
    std::erase_if(units, [&](sim::EntityId id) {
        const sim::Unit* u = world_->findUnit(id);
        return u == nullptr || !u->alive() || u->owner != kPlayer;
    });
    selection_ = std::move(units);
    refreshSelection();
    if (selection_.empty()) return;
    voice_.order(Situation::Selected, *world_, selection_);
    // Their own squad's formation if they are of one squad, else what suits their kind.
    const int squad = squadOfUnit_[selection_.front() - 1];
    bool oneSquad = squad >= 0, oneType = true;
    const UnitType type = static_cast<UnitType>(world_->units()[selection_.front() - 1].stats.type);
    for (sim::EntityId id : selection_) {
        oneSquad = oneSquad && squadOfUnit_[id - 1] == squad;
        oneType = oneType && static_cast<UnitType>(world_->units()[id - 1].stats.type) == type;
    }
    selFormation_ = oneSquad ? squads_[static_cast<size_t>(squad)].formation : oneType ? defaultFormation(type) : Formation::Line;
}

void Battle::clearSelection() {
    selection_.clear();
    refreshSelection();
}

void Battle::selectSquad(int index) {
    if (index < 0 || index >= static_cast<int>(squads_.size()) || squads_[static_cast<size_t>(index)].owner != kPlayer) return;
    select(squads_[static_cast<size_t>(index)].members);
}

int Battle::selectedSquad() const {
    const std::vector<sim::EntityId> alive = selectedAlive();
    if (alive.empty()) return -1;
    const int squad = squadOfUnit_[alive.front() - 1];
    if (squad < 0) return -1;
    for (sim::EntityId id : alive)
        if (squadOfUnit_[id - 1] != squad) return -1;
    return static_cast<int>(alive.size()) == aliveCount(squads_[static_cast<size_t>(squad)]) ? squad : -1;
}

int Battle::squadSelection(const Squad& s) const {
    int alive = 0, chosen = 0;
    for (sim::EntityId id : s.members) {
        if (!world_->units()[id - 1].alive()) continue;
        ++alive;
        chosen += isSelected(id);
    }
    return chosen == 0 ? 0 : chosen == alive ? 2 : 1;
}

int Battle::groupSelection(int slot) const {
    const std::vector<sim::EntityId>& g = groups_[static_cast<size_t>(slot)].units;
    int alive = 0, chosen = 0;
    for (sim::EntityId id : g) {
        if (!world_->units()[id - 1].alive()) continue;
        ++alive;
        chosen += isSelected(id);
    }
    if (chosen == 0) return 0;
    return chosen == alive && alive == static_cast<int>(selectedAlive().size()) ? 2 : 1;
}

int Battle::unitAt(const Camera& camera, float x, float y, float pixelScale) const {
    int best = -1;
    float bestD2 = kTapRadiusPx * pixelScale * kTapRadiusPx * pixelScale;
    const auto& units = world_->units();
    for (size_t i = 0; i < units.size(); ++i) {
        if (!units[i].alive()) continue;
        float px, py;
        const V3 p = unitPosition(i) + V3{0, 0, static_cast<float>(units[i].stats.height.toDouble()) * 0.5f};
        if (!camera.project(p, px, py)) continue;
        const float d2 = (px - x) * (px - x) + (py - y) * (py - y);
        if (d2 < bestD2) {
            bestD2 = d2;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void Battle::selectVisibleOfType(const Camera& camera, UnitType type) {
    std::vector<sim::EntityId> out;
    const auto& units = world_->units();
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        if (!u.alive() || u.owner != kPlayer || static_cast<UnitType>(u.stats.type) != type) continue;
        float px, py;
        if (!camera.project(unitPosition(i), px, py)) continue;
        if (px >= 0 && py >= 0 && px <= camera.width() && py <= camera.height()) out.push_back(u.id);
    }
    select(std::move(out));
}

void Battle::selectInRect(const Camera& camera, float x0, float y0, float x1, float y1, bool additive) {
    if (x0 > x1) std::swap(x0, x1);
    if (y0 > y1) std::swap(y0, y1);
    std::vector<sim::EntityId> out = additive ? selection_ : std::vector<sim::EntityId>{};
    const auto& units = world_->units();
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        if (!u.alive() || u.owner != kPlayer) continue;
        float px, py;
        const V3 p = unitPosition(i) + V3{0, 0, static_cast<float>(u.stats.height.toDouble()) * 0.5f};
        if (camera.project(p, px, py) && px >= x0 && px <= x1 && py >= y0 && py <= y1) out.push_back(u.id);
    }
    select(std::move(out));
}

void Battle::selectHeroes() {
    std::vector<sim::EntityId> out;
    for (const sim::Unit& u : world_->units())
        if (u.alive() && u.owner == kPlayer && static_cast<UnitType>(u.stats.type) == UnitType::Hero) out.push_back(u.id);
    select(std::move(out));
}

void Battle::assignGroup(int slot) {
    if (slot < 0 || slot >= kGroupSlots) return;
    groups_[static_cast<size_t>(slot)] = {selectedAlive(), selFormation_};
}

int Battle::saveGroup() {
    if (selectedAlive().empty()) return -1;
    // Keys 1-9, then 0, as along the keyboard.
    for (int k = 1; k <= kGroupSlots; ++k) {
        const int slot = k % kGroupSlots;
        bool used = false;
        for (sim::EntityId id : groups_[static_cast<size_t>(slot)].units) used = used || world_->units()[id - 1].alive();
        if (used) continue;
        assignGroup(slot);
        return slot;
    }
    return -1;
}

bool Battle::recallGroup(int slot) {
    if (slot < 0 || slot >= kGroupSlots) return false;
    const Group& g = groups_[static_cast<size_t>(slot)];
    select(g.units);
    if (selection_.empty()) return false;
    selFormation_ = g.formation;
    return true;
}

void Battle::clearGroup(int slot) {
    if (slot >= 0 && slot < kGroupSlots) groups_[static_cast<size_t>(slot)] = {};
}

// --- Orders -----------------------------------------------------------------

V3 Battle::centreOf(const std::vector<sim::EntityId>& units) const {
    V3 sum;
    int n = 0;
    for (sim::EntityId id : units) {
        if (!world_->units()[id - 1].alive()) continue;
        sum += toV3(world_->units()[id - 1].pos);
        ++n;
    }
    return n > 0 ? sum * (1.0f / static_cast<float>(n)) : sum;
}

V3 Battle::selectionFacing() const {
    if (const int s = selectedSquad(); s >= 0) return squads_[static_cast<size_t>(s)].facing;
    V3 sum;
    for (sim::EntityId id : selectedAlive()) sum += toV3(world_->units()[id - 1].facing);
    sum = flat(sum).normalized();
    return sum.length() > 0 ? sum : V3{1, 0, 0};
}

std::vector<FormationSlot> Battle::placeFormation(const std::vector<sim::EntityId>& units, Formation formation, const V3& to,
                                                  const V3& facing) const {
    // Room for the biggest of them.
    float spacing = kMinSpacing;
    for (sim::EntityId id : units)
        spacing = std::max(spacing, formationSpacing(static_cast<UnitType>(world_->units()[id - 1].stats.type)));
    std::vector<FormationSlot> slots = formationLayout(formation, static_cast<int>(units.size()), spacing);
    const V3 right{facing.y, -facing.x, 0};
    for (FormationSlot& s : slots) {
        s.pos = to + right * s.pos.x + facing * s.pos.y;
        s.face = right * s.face.x + facing * s.face.y;
    }
    return slots;
}

void Battle::moveSquad(const Squad& squad, const V3& to) {
    squad.goal = to;
    squad.hasGoal = true;
    std::vector<sim::EntityId> alive;
    for (sim::EntityId id : squad.members)
        if (world_->units()[id - 1].alive()) alive.push_back(id);
    moveGroup(squad.owner, std::move(alive), squad.formation, to, nullptr, squad.facing);
}

void Battle::moveGroup(sim::PlayerId owner, std::vector<sim::EntityId> alive, Formation formation, const V3& to,
                       const V3* face, const V3& fallbackFacing, bool force) {
    if (alive.empty()) return;
    if (owner == kPlayer) markOrdered(alive);
    const V3 from = centreOf(alive);
    V3 facing = flat(face ? *face : to - from).normalized();
    if (facing.length() == 0) facing = fallbackFacing;
    const V3 right{facing.y, -facing.x, 0};
    std::vector<FormationSlot> slots = placeFormation(alive, formation, to, facing);
    // Who goes where: the soldiers nearest the front take the front ranks, and
    // within a rank keep their left-to-right order, so nobody crosses another.
    const auto pos = [&](sim::EntityId id) { return toV3(world_->units()[id - 1].pos); };
    const auto ahead = [&](const V3& p) { return (p - to).dot(facing); };
    const auto across = [&](const V3& p) { return (p - to).dot(right); };
    std::vector<size_t> order(slots.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const float ya = ahead(slots[a].pos), yb = ahead(slots[b].pos);
        return std::abs(ya - yb) > 0.01f ? ya > yb : across(slots[a].pos) < across(slots[b].pos);
    });
    std::stable_sort(alive.begin(), alive.end(), [&](sim::EntityId a, sim::EntityId b) {
        return (pos(a) - from).dot(facing) > (pos(b) - from).dot(facing);
    });
    struct Order {
        sim::EntityId id;
        V3 target, face;
    };
    std::vector<Order> plan;
    for (size_t start = 0; start < order.size();) {
        size_t end = start;
        while (end < order.size() && std::abs(ahead(slots[order[end]].pos) - ahead(slots[order[start]].pos)) <= 0.01f) ++end;
        std::vector<sim::EntityId> rank(alive.begin() + static_cast<long>(start), alive.begin() + static_cast<long>(end));
        std::stable_sort(rank.begin(), rank.end(), [&](sim::EntityId a, sim::EntityId b) {
            return (pos(a) - from).dot(right) < (pos(b) - from).dot(right);
        });
        for (size_t k = start; k < end; ++k) plan.push_back({rank[k - start], slots[order[k]].pos, slots[order[k]].face});
        start = end;
    }
    // Everyone arrives together at the slowest member's pace (slower in the
    // tight formations): the farthest walks at that pace, the others slower
    // in proportion.
    float slowest = 1e9f, farthest = 0;
    for (const Order& o : plan) {
        slowest = std::min(slowest, static_cast<float>(world_->units()[o.id - 1].stats.moveSpeed.toDouble()));
        farthest = std::max(farthest, (o.target - pos(o.id)).length());
    }
    const float pace = formationPace(formation);
    for (const Order& o : plan) {
        const float share = farthest > 0 ? std::clamp((o.target - pos(o.id)).length() / farthest, 0.35f, 1.0f) : 1.0f;
        sim::Command c{owner, o.id, sim::Command::Type::Move, toSim(o.target)};
        // All take the group's one route (a column on the road), each keeping his file.
        c.march = true;
        c.marchFrom = toSim(from);
        c.marchTo = toSim(to);
        c.speed = sim::Fixed::fromRatio(static_cast<int64_t>(slowest * share * pace * 1000.0f), 1000);
        // And turn to the front when there (outward, in an encirclement).
        c.face = toSim(o.face);
        c.force = force;
        pending_.push_back(c);
    }
    // Squads that marched whole remember the way they face.
    for (Squad& s : squads_) {
        if (s.owner != owner) continue;
        int in = 0, living = 0;
        for (sim::EntityId id : s.members) {
            if (!world_->units()[id - 1].alive()) continue;
            ++living;
            in += std::find(alive.begin(), alive.end(), id) != alive.end();
        }
        if (living > 0 && in == living) s.facing = facing;
    }
}

void Battle::attackGate(const Squad& squad, int gate) {
    std::vector<sim::EntityId> alive;
    for (sim::EntityId id : squad.members)
        if (world_->units()[id - 1].alive()) alive.push_back(id);
    attackGateWith(squad.owner, alive, squad.formation, gate);
}

void Battle::attackGateWith(sim::PlayerId owner, const std::vector<sim::EntityId>& units, Formation formation, int gate) {
    const sim::Gate& g = region_->gates()[static_cast<size_t>(gate)];
    const V3 centre = toV3(g.a + g.b) * 0.5f;
    // Engines and bombers shoot at it; the rest go through it, to the far
    // side: they hack at it until it gives.
    std::vector<sim::EntityId> through;
    for (sim::EntityId id : units) {
        const Role role = roleOf(static_cast<UnitType>(world_->units()[id - 1].stats.type));
        if (role == Role::Siege || role == Role::Bomber) issue(owner, id, sim::Command::Type::AttackGate, centre);
        else through.push_back(id);
    }
    if (through.empty()) return;
    const V3 out = toV3(g.out);
    const V3 from = centreOf(through);
    const float side = (from - centre).dot(out) >= 0 ? -1.0f : 1.0f;
    moveGroup(owner, through, formation, centre + out * (10.0f * side), nullptr, out * side);
}

void Battle::orderAtGate(int gate) {
    if (gate < 0 || gate >= static_cast<int>(region_->gates().size())) return;
    attackGateWith(kPlayer, selectedAlive(), selFormation_, gate);
    voice_.order(Situation::Attack, *world_, selection_);
}

void Battle::orderMove(const V3& to) {
    moveGroup(kPlayer, selectedAlive(), selFormation_, to, nullptr, selectionFacing());
    voice_.order(Situation::Move, *world_, selection_);
}

void Battle::orderMoveFacing(const V3& to, const V3& facing) {
    moveGroup(kPlayer, selectedAlive(), selFormation_, to, &facing, selectionFacing());
    voice_.order(Situation::Move, *world_, selection_);
}

void Battle::stopSelected() {
    for (sim::EntityId id : selectedAlive()) issue(kPlayer, id, sim::Command::Type::Stop, {});
    voice_.order(Situation::Move, *world_, selection_);
}

void Battle::scatter() {
    const std::vector<sim::EntityId> alive = selectedAlive();
    const V3 centre = centreOf(alive);
    for (sim::EntityId id : alive) {
        const V3 p = toV3(world_->units()[id - 1].pos);
        V3 away = flat(p - centre);
        const uint32_t h = hash32(id);
        // The man at the very centre picks a way of his own.
        if (away.length() < 0.5f) {
            const float a = static_cast<float>(h % 6283) / 1000.0f;
            away = {std::cos(a), std::sin(a), 0};
        }
        away = away.normalized();
        // Flat out, each on his own (no route, no formation, no pace).
        const float run = kScatterMetres + static_cast<float>((h >> 16) % 300) / 100.0f;
        issue(kPlayer, id, sim::Command::Type::Move, p + away * run);
    }
    markOrdered(alive);
    voice_.order(Situation::Move, *world_, alive);
}

void Battle::setFormation(Formation f) {
    const std::vector<sim::EntityId> alive = selectedAlive();
    if (alive.empty()) return;
    const V3 facing = selectionFacing();
    selFormation_ = f;
    for (Squad& s : squads_)
        if (s.owner == kPlayer && squadSelection(s) == 2) s.formation = f;
    for (int k = 0; k < kGroupSlots; ++k)
        if (groupSelection(k) == 2) groups_[static_cast<size_t>(k)].formation = f;
    // Re-form where they stand, keeping their front.
    moveGroup(kPlayer, alive, f, centreOf(alive), &facing, facing);
}

void Battle::cycleFormation() {
    int k = 0;
    while (k < kFormationCount && kFormationOrder[k] != selFormation_) ++k;
    setFormation(kFormationOrder[(k + 1) % kFormationCount]);
}

// --- Taps and presses -------------------------------------------------------

void Battle::tap(const Camera& camera, float x, float y, float pixelScale, bool additive) {
    const Pick pick = pickAt(camera, x, y, pixelScale);
    const auto& units = world_->units();
    const std::vector<sim::EntityId> mine = selectedAlive();
    // Attack and force-march modes (the phone's button, A or M) last one
    // order; Ctrl and Alt held on the desktop do the same for one click.
    const OrderMode mode = forceKey_ ? OrderMode::Attack : marchKey_ ? OrderMode::ForceMarch : orderMode_;
    using K = Pick::Kind;
    if (!mine.empty() && mode != OrderMode::Normal && pick.kind != K::None) {
        orderMode_ = OrderMode::Normal;
        if (mode == OrderMode::ForceMarch) {
            V3 ground;
            if (camera.pickGround(x, y, ground)) orderForceMarch(ground);
            return;
        }
        switch (pick.kind) {
            case K::Own:
            case K::Enemy:
                // Whoever was tapped, of either side: a force attack on a friend.
                if (pick.index >= 0) orderAttackUnit(units[static_cast<size_t>(pick.index)].id);
                else orderAttackUnit(nearestOf(squads_[static_cast<size_t>(pick.squad)].members, centreOf(mine)));
                return;
            case K::Gate:
            case K::Wall:
            case K::Obstacle: orderAttackObject(pick); return;
            case K::Ground: orderAttackGround(pick.at); return;
            case K::None: return;
        }
    }

    // One of ours: him alone; him again, everyone of his kind in view.
    if (pick.kind == K::Own && pick.index >= 0) {
        const sim::EntityId id = units[static_cast<size_t>(pick.index)].id;
        if (additive) {
            std::vector<sim::EntityId> next = selection_;
            if (isSelected(id)) std::erase(next, id);
            else next.push_back(id);
            const Formation keep = selFormation_;
            const bool had = !mine.empty();
            select(std::move(next));
            if (had) selFormation_ = keep;
            return;
        }
        if (mine.size() == 1 && mine.front() == id) selectVisibleOfType(camera, static_cast<UnitType>(units[static_cast<size_t>(pick.index)].stats.type));
        else select({id});
        return;
    }
    if (pick.kind == K::Own) {
        selectSquad(pick.squad);
        return;
    }
    if (mine.empty()) return;
    switch (pick.kind) {
        case K::Enemy:
            // Tapped an enemy: everyone selected goes after him (missile troops
            // shoot him); zoomed far out, each after the nearest of that squad.
            if (pick.index >= 0) {
                orderAttackUnit(units[static_cast<size_t>(pick.index)].id);
            } else {
                const std::vector<sim::EntityId> all = selection_;
                const std::vector<sim::EntityId>& foes = squads_[static_cast<size_t>(pick.squad)].members;
                for (sim::EntityId id : mine) {
                    selection_ = {id};
                    orderAttackUnit(nearestOf(foes, toV3(units[id - 1].pos)));
                }
                selection_ = all;
            }
            return;
        case K::Gate:
            // A standing gate: engines and bombers shoot at it, the rest go to break it.
            orderAtGate(pick.index);
            return;
        case K::Wall:
        case K::Obstacle:
        case K::Ground: orderMove(pick.at); return;
        case K::Own:
        case K::None: return;
    }
}

bool Battle::pressBegin(const Camera& camera, float x, float y, float pixelScale, bool box) {
    drag_ = {};
    drag_.pixelScale = pixelScale;
    drag_.x0 = drag_.x1 = x;
    drag_.y0 = drag_.y1 = y;
    if (!box) {
        const int u = unitAt(camera, x, y, pixelScale);
        box = selectedAlive().empty() || (u >= 0 && world_->units()[static_cast<size_t>(u)].owner == kPlayer);
    }
    if (box) {
        drag_.mode = Drag::Mode::Box;
        return true;
    }
    if (!camera.pickGround(x, y, drag_.anchor)) return false;
    drag_.mode = Drag::Mode::Facing;
    drag_.tip = drag_.anchor;
    // Until dragged: the way they would face marching there.
    drag_.facing = flat(drag_.anchor - centreOf(selectedAlive())).normalized();
    if (drag_.facing.length() == 0) drag_.facing = selectionFacing();
    updateGhost();
    return true;
}

void Battle::updateGhost() {
    drag_.ghost = placeFormation(selectedAlive(), selFormation_, drag_.anchor, drag_.facing);
    for (FormationSlot& s : drag_.ghost) s.pos.z = heightAt(s.pos.x, s.pos.y);
}

void Battle::pressMove(const Camera& camera, float x, float y) {
    drag_.x1 = x;
    drag_.y1 = y;
    if (drag_.mode != Drag::Mode::Facing) return;
    V3 hit;
    if (!camera.pickGround(x, y, hit)) return;
    drag_.tip = hit;
    const V3 d = flat(hit - drag_.anchor);
    if (d.length() < kAimMetres) return;
    drag_.aimed = true;
    drag_.facing = d.normalized();
    updateGhost();
}

void Battle::pressEnd(const Camera& camera, float x, float y, bool cancelled, bool additive) {
    const Drag d = drag_;
    drag_ = {};
    if (cancelled || d.mode == Drag::Mode::None) return;
    if (d.mode == Drag::Mode::Box) {
        // Too small a box to mean anything: a long tap.
        if (std::abs(x - d.x0) < kBoxSlopPx * d.pixelScale && std::abs(y - d.y0) < kBoxSlopPx * d.pixelScale) tap(camera, d.x0, d.y0, d.pixelScale, additive);
        else selectInRect(camera, d.x0, d.y0, x, y, additive);
        return;
    }
    const bool force = marchKey_ || orderMode_ == OrderMode::ForceMarch;
    orderMode_ = OrderMode::Normal;
    if (force) orderForceMarch(d.anchor, d.aimed ? &d.facing : nullptr);
    else if (d.aimed) orderMoveFacing(d.anchor, d.facing);
    else orderMove(d.anchor);
}

}  // namespace illyria::game
