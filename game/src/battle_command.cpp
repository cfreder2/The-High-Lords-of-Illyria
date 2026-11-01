// Commanding the battle, as in Myth: selecting soldiers (one man, a squad,
// everyone of a type in view, a box, saved groups), formations, marching as
// they stand or in a formation, with a facing, and scattering. Every order becomes sim::Commands in pending_, so
// it reaches the simulation (and every lockstep peer) the same way.

#include <algorithm>
#include <cmath>
#include <map>

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

// The least-cost matching of n rows to n columns (Kuhn-Munkres, n^3), `cost`
// row-major: for each row, its column.
std::vector<int> leastCostMatching(const std::vector<float>& cost, int n) {
    constexpr float kInf = 1e30f;
    std::vector<float> u(static_cast<size_t>(n) + 1), v(static_cast<size_t>(n) + 1);
    std::vector<int> p(static_cast<size_t>(n) + 1), way(static_cast<size_t>(n) + 1);
    for (int i = 1; i <= n; ++i) {
        p[0] = i;
        int j0 = 0;
        std::vector<float> minv(static_cast<size_t>(n) + 1, kInf);
        std::vector<char> used(static_cast<size_t>(n) + 1, 0);
        do {
            used[static_cast<size_t>(j0)] = 1;
            const int i0 = p[static_cast<size_t>(j0)];
            float delta = kInf;
            int j1 = 0;
            for (int j = 1; j <= n; ++j) {
                if (used[static_cast<size_t>(j)]) continue;
                const float cur = cost[static_cast<size_t>((i0 - 1) * n + j - 1)] - u[static_cast<size_t>(i0)] - v[static_cast<size_t>(j)];
                if (cur < minv[static_cast<size_t>(j)]) {
                    minv[static_cast<size_t>(j)] = cur;
                    way[static_cast<size_t>(j)] = j0;
                }
                if (minv[static_cast<size_t>(j)] < delta) {
                    delta = minv[static_cast<size_t>(j)];
                    j1 = j;
                }
            }
            for (int j = 0; j <= n; ++j) {
                if (used[static_cast<size_t>(j)]) {
                    u[static_cast<size_t>(p[static_cast<size_t>(j)])] += delta;
                    v[static_cast<size_t>(j)] -= delta;
                } else {
                    minv[static_cast<size_t>(j)] -= delta;
                }
            }
            j0 = j1;
        } while (p[static_cast<size_t>(j0)] != 0);
        do {
            const int j1 = way[static_cast<size_t>(j0)];
            p[static_cast<size_t>(j0)] = p[static_cast<size_t>(j1)];
            j0 = j1;
        } while (j0 != 0);
    }
    std::vector<int> col(static_cast<size_t>(n));
    for (int j = 1; j <= n; ++j) col[static_cast<size_t>(p[static_cast<size_t>(j)] - 1)] = j - 1;
    return col;
}

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
        case Role::Flier:
        case Role::Scout: return Formation::Loose;
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
        return u == nullptr || !u->alive() || !controllable(*u);
    });
    selection_ = std::move(units);
    refreshSelection();
    // Every selection starts as it stands (keepShape).
    selKeep_ = true;
    selShape_ = {};
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
    if (index < 0 || index >= static_cast<int>(squads_.size()) || squads_[static_cast<size_t>(index)].owner != local_) return;
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
    const auto& units = world_->units();
    // His picture under the tap, the nearest to the eye of those it is under.
    if (spriteBoxes_.size() >= units.size() * 6) {
        const V3 eye = camera.eye();
        const float margin = 3 * pixelScale;
        float nearest = 1e30f;
        for (size_t i = 0; i < units.size(); ++i) {
            const float* b = &spriteBoxes_[i * 6];
            if (!units[i].alive() || std::isnan(b[0])) continue;
            float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f;
            bool seen = true;
            for (int c = 0; c < 4; ++c) {
                const V3 corner{c & 1 ? b[3] : b[0], c & 1 ? b[4] : b[1], c & 2 ? b[5] : b[2]};
                float px, py;
                seen = seen && camera.project(corner, px, py);
                x0 = std::min(x0, px), x1 = std::max(x1, px), y0 = std::min(y0, py), y1 = std::max(y1, py);
            }
            if (!seen || x < x0 - margin || x > x1 + margin || y < y0 - margin || y > y1 + margin) continue;
            const float d = (unitPosition(i) - eye).length();
            if (d < nearest) {
                nearest = d;
                best = static_cast<int>(i);
            }
        }
        if (best >= 0) return best;
    }
    // Else (zoomed far out, a speck) the nearest within reach of the tap.
    float bestD2 = kTapRadiusPx * pixelScale * kTapRadiusPx * pixelScale;
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
        if (!u.alive() || u.owner != local_ || static_cast<UnitType>(u.stats.type) != type) continue;
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
        if (!u.alive() || u.owner != local_) continue;
        float px, py;
        const V3 p = unitPosition(i) + V3{0, 0, static_cast<float>(u.stats.height.toDouble()) * 0.5f};
        if (camera.project(p, px, py) && px >= x0 && px <= x1 && py >= y0 && py <= y1) out.push_back(u.id);
    }
    select(std::move(out));
}

void Battle::selectHeroes() {
    std::vector<sim::EntityId> out;
    for (const sim::Unit& u : world_->units())
        if (u.alive() && u.owner == local_ && static_cast<UnitType>(u.stats.type) == UnitType::Hero) out.push_back(u.id);
    select(std::move(out));
}

void Battle::assignGroup(int slot) {
    if (slot < 0 || slot >= kGroupSlots) return;
    groups_[static_cast<size_t>(slot)] = {selectedAlive(), selFormation_, selectionShape()};
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
    selShape_ = g.shape;  // it marches as it stood when saved
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
    if (owner == local_) markOrdered(alive);
    const V3 from = centreOf(alive);
    V3 facing = flat(face ? *face : to - from).normalized();
    if (facing.length() == 0) facing = fallbackFacing;
    sendMarch(owner, planFormation(alive, formation, to, facing), from, to, facing, formationPace(formation), force);
}

std::vector<Battle::MarchOrder> Battle::planFormation(const std::vector<sim::EntityId>& units, Formation formation, const V3& to,
                                                     const V3& facing) const {
    std::vector<sim::EntityId> alive = units;
    const V3 from = centreOf(alive);
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
    std::vector<MarchOrder> plan;
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
    return plan;
}

Battle::Shape Battle::shapeOf(const std::vector<sim::EntityId>& units) const {
    const auto pos = [&](sim::EntityId id) { return flat(toV3(world_->units()[id - 1].pos)); };
    const auto sent = [&](sim::EntityId id) -> const Slot* {
        return id - 1 < slots_.size() && slots_[id - 1].order != 0 ? &slots_[id - 1] : nullptr;
    };
    // Each order's men: the middle of where they were sent, and of where they stand.
    struct Middles {
        V3 sent, stand;
        int n = 0;
    };
    std::map<uint32_t, Middles> orders;
    for (sim::EntityId id : units)
        if (const Slot* m = sent(id)) {
            Middles& o = orders[m->order];
            o.sent += flat(m->pos);
            o.stand += pos(id);
            ++o.n;
        }
    Shape s;
    V3 middle, front;
    for (sim::EntityId id : units) {
        V3 place = pos(id), face = flat(toV3(world_->units()[id - 1].facing)).normalized();
        if (const Slot* m = sent(id)) {
            // Where he was sent beside the others of his order, they standing where they are.
            const Middles& o = orders[m->order];
            place = flat(m->pos) + (o.stand - o.sent) * (1.0f / static_cast<float>(o.n));
            face = m->face;
            front += m->front;
        } else {
            front += face;
        }
        s.ids.push_back(id);
        s.offset.push_back(place);
        s.face.push_back(face);
        middle += place;
    }
    if (!s.ids.empty()) middle = middle * (1.0f / static_cast<float>(s.ids.size()));
    for (V3& o : s.offset) o = o - middle;
    s.front = flat(front).normalized();
    if (s.front.length() == 0) s.front = {1, 0, 0};
    return s;
}

Battle::Shape Battle::selectionShape() const {
    const std::vector<sim::EntityId> alive = selectedAlive();
    if (selShape_.ids.empty()) return shapeOf(alive);
    // The saved one, of those still standing (closed on its middle again); any
    // soldier it does not know, and it is theirs as they stand.
    Shape s;
    s.front = selShape_.front;
    V3 middle;
    for (sim::EntityId id : alive) {
        const auto it = std::find(selShape_.ids.begin(), selShape_.ids.end(), id);
        if (it == selShape_.ids.end()) return shapeOf(alive);
        const size_t k = static_cast<size_t>(it - selShape_.ids.begin());
        s.ids.push_back(id);
        s.offset.push_back(selShape_.offset[k]);
        s.face.push_back(selShape_.face[k]);
        middle += selShape_.offset[k];
    }
    if (!s.ids.empty()) middle = middle * (1.0f / static_cast<float>(s.ids.size()));
    for (V3& o : s.offset) o = o - middle;
    return s;
}

std::vector<Battle::MarchOrder> Battle::planShape(const Shape& shape, const std::vector<sim::EntityId>& units, const V3& to,
                                                  const V3& facing) const {
    // The arrangement turned from the way it faced to the new front.
    const V3 b = flat(facing).normalized();
    const float c = shape.front.dot(b), s = shape.front.x * b.y - shape.front.y * b.x;
    const auto turn = [&](const V3& v) { return V3{v.x * c - v.y * s, v.x * s + v.y * c, 0}; };
    // Its places, by squad (the soldiers of none, by kind): no squad takes another's.
    std::map<int, std::vector<size_t>> bySquad;
    for (size_t k = 0; k < shape.ids.size(); ++k) {
        const sim::EntityId id = shape.ids[k];
        if (std::find(units.begin(), units.end(), id) == units.end()) continue;
        const int squad = squadOfUnit_[id - 1];
        bySquad[squad >= 0 ? squad : -1 - static_cast<int>(world_->units()[id - 1].stats.type)].push_back(k);
    }
    std::vector<MarchOrder> plan;
    for (const auto& [squad, members] : bySquad) {
        // Within it, the least walking in all: no one crosses another.
        const int n = static_cast<int>(members.size());
        std::vector<float> cost(static_cast<size_t>(n) * static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const V3 at = flat(toV3(world_->units()[shape.ids[members[static_cast<size_t>(i)]] - 1].pos));
            for (int j = 0; j < n; ++j)
                cost[static_cast<size_t>(i * n + j)] = (to + turn(shape.offset[members[static_cast<size_t>(j)]]) - at).length();
        }
        const std::vector<int> place = leastCostMatching(cost, n);
        for (int i = 0; i < n; ++i) {
            const size_t k = members[static_cast<size_t>(place[static_cast<size_t>(i)])];
            plan.push_back({shape.ids[members[static_cast<size_t>(i)]], to + turn(shape.offset[k]), turn(shape.face[k]).normalized()});
        }
    }
    std::sort(plan.begin(), plan.end(), [](const MarchOrder& a, const MarchOrder& b) { return a.id < b.id; });
    return plan;
}

std::vector<Battle::MarchOrder> Battle::planSelection(const std::vector<sim::EntityId>& units, const V3& to, const V3& facing) const {
    std::vector<MarchOrder> plan = selKeep_ ? planShape(selectionShape(), units, to, facing) : planFormation(units, selFormation_, to, facing);
    keepClear(plan);
    return plan;
}

void Battle::keepClear(std::vector<MarchOrder>& plan) const {
    if (!region_ || plan.empty()) return;
    constexpr float kSpare = 0.3f;
    // The obstacles about the places, once.
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
    for (const MarchOrder& o : plan) {
        x0 = std::min(x0, o.target.x), x1 = std::max(x1, o.target.x);
        y0 = std::min(y0, o.target.y), y1 = std::max(y1, o.target.y);
    }
    constexpr float kReach = 10.0f;  // more than any obstacle and man together
    std::vector<const sim::Obstacle*> near;
    for (const sim::Obstacle& o : region_->obstacles()) {
        const V3 p = toV3(o.pos);
        if (p.x > x0 - kReach && p.x < x1 + kReach && p.y > y0 - kReach && p.y < y1 + kReach) near.push_back(&o);
    }
    if (near.empty()) return;
    for (MarchOrder& m : plan) {
        const float r = static_cast<float>(world_->units()[m.id - 1].stats.radius.toDouble()) + kSpare;
        // Out of each in turn, twice over for those that touch.
        for (int pass = 0; pass < 2; ++pass)
            for (const sim::Obstacle* o : near) {
                const V3 c = toV3(o->pos);
                if (o->kind == sim::Obstacle::Kind::House) {
                    const float hx = static_cast<float>(o->halfX.toDouble()), hy = static_cast<float>(o->halfY.toDouble());
                    const float dx = m.target.x - c.x, dy = m.target.y - c.y;
                    if (std::abs(dx) >= hx + r || std::abs(dy) >= hy + r) continue;
                    // Out by the nearer side.
                    if (hx + r - std::abs(dx) < hy + r - std::abs(dy)) m.target.x = c.x + (dx < 0 ? -(hx + r) : hx + r);
                    else m.target.y = c.y + (dy < 0 ? -(hy + r) : hy + r);
                    continue;
                }
                const float reach = r + static_cast<float>(o->radius.toDouble());
                V3 d = flat(m.target - c);
                const float len = d.length();
                if (len >= reach) continue;
                d = len > 0.01f ? d * (1.0f / len) : V3{1, 0, 0};
                m.target = c + d * reach;
            }
    }
}

void Battle::marchSelection(const std::vector<sim::EntityId>& units, const V3& to, const V3* facing, bool force) {
    if (units.empty()) return;
    markOrdered(units);
    // Facing as they do now, unless given a way to face.
    const V3 front = facing ? flat(*facing).normalized() : selKeep_ ? selectionShape().front : selectionFacing();
    // In a formation, its pace; as they stand, the slowest of their squads' (see formationPace).
    float pace = formationPace(selFormation_);
    if (selKeep_) {
        pace = 1.0f;
        for (sim::EntityId id : units) {
            const int squad = squadOfUnit_[id - 1];
            pace = std::min(pace, squad >= 0 ? formationPace(squads_[static_cast<size_t>(squad)].formation) : 0.9f);
        }
    }
    sendMarch(local_, planSelection(units, to, front), centreOf(units), to, front, pace, force);
}

void Battle::sendMarch(sim::PlayerId owner, const std::vector<MarchOrder>& plan, const V3& from, const V3& to, const V3& front,
                       float pace, bool force) {
    if (plan.empty()) return;
    const auto pos = [&](sim::EntityId id) { return toV3(world_->units()[id - 1].pos); };
    // Everyone arrives together at the slowest member's pace (slower in the
    // tight formations): the farthest walks at that pace, the others slower
    // in proportion.
    float slowest = 1e9f, farthest = 0;
    for (const MarchOrder& o : plan) {
        slowest = std::min(slowest, static_cast<float>(world_->units()[o.id - 1].stats.moveSpeed.toDouble()));
        farthest = std::max(farthest, (o.target - pos(o.id)).length());
    }
    if (slots_.size() < world_->units().size()) slots_.resize(world_->units().size());
    const uint32_t order = ++marches_;
    for (const MarchOrder& o : plan) {
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
        slots_[o.id - 1] = {o.target, o.face, front, order};
    }
    // Squads that marched whole remember the way they face.
    for (Squad& s : squads_) {
        if (s.owner != owner) continue;
        int in = 0, living = 0;
        for (sim::EntityId id : s.members) {
            if (!world_->units()[id - 1].alive()) continue;
            ++living;
            in += std::any_of(plan.begin(), plan.end(), [&](const MarchOrder& o) { return o.id == id; });
        }
        if (living > 0 && in == living) s.facing = front;
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
    attackGateWith(local_, selectedAlive(), selFormation_, gate);
    voice_.order(Situation::Attack, *world_, selection_);
}

void Battle::orderMove(const V3& to) {
    marchSelection(selectedAlive(), to, nullptr, false);
    voice_.order(Situation::Move, *world_, selection_);
}

void Battle::orderMoveFacing(const V3& to, const V3& facing) {
    marchSelection(selectedAlive(), to, &facing, false);
    voice_.order(Situation::Move, *world_, selection_);
}

void Battle::stopSelected() {
    for (sim::EntityId id : selectedAlive()) issue(local_, id, sim::Command::Type::Stop, {});
    voice_.order(Situation::Move, *world_, selection_);
}

void Battle::scatter() {
    const std::vector<sim::EntityId> alive = selectedAlive();
    const V3 centre = centreOf(alive);
    const V3 front = selectionShape().front;
    const uint32_t order = ++marches_;
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
        issue(local_, id, sim::Command::Type::Move, p + away * run);
        // Scattered, as they will stand: so they march on (keepShape).
        if (slots_.size() < world_->units().size()) slots_.resize(world_->units().size());
        slots_[id - 1] = {p + away * run, away, front, order};
    }
    markOrdered(alive);
    voice_.order(Situation::Move, *world_, alive);
}

void Battle::setFormation(Formation f) {
    const std::vector<sim::EntityId> alive = selectedAlive();
    if (alive.empty()) return;
    const V3 facing = selKeep_ ? selectionShape().front : selectionFacing();
    selFormation_ = f;
    selKeep_ = false;
    selShape_ = {};
    for (Squad& s : squads_)
        if (s.owner == local_ && squadSelection(s) == 2) s.formation = f;
    for (int k = 0; k < kGroupSlots; ++k)
        if (groupSelection(k) == 2) groups_[static_cast<size_t>(k)].formation = f;
    // Re-form where they stand, keeping their front.
    moveGroup(local_, alive, f, centreOf(alive), &facing, facing);
}

void Battle::keepShape() {
    selKeep_ = true;
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
        if (mode == OrderMode::LaySatchel) {
            V3 ground;
            if (camera.pickGround(x, y, ground)) orderLaySatchels(ground);
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
            const bool had = !mine.empty(), asTheyStand = selKeep_;
            select(std::move(next));
            if (had) {
                selFormation_ = keep;
                selKeep_ = asTheyStand;
            }
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
        box = selectedAlive().empty() || (u >= 0 && world_->units()[static_cast<size_t>(u)].owner == local_);
    }
    if (box) {
        drag_.mode = Drag::Mode::Box;
        return true;
    }
    if (!camera.pickGround(x, y, drag_.anchor)) return false;
    drag_.mode = Drag::Mode::Facing;
    drag_.tip = drag_.anchor;
    // Until dragged: there, facing as they do now.
    drag_.facing = selKeep_ ? selectionShape().front : selectionFacing();
    updateGhost();
    return true;
}

void Battle::updateGhost() {
    drag_.ghost.clear();
    for (const MarchOrder& o : planSelection(selectedAlive(), drag_.anchor, drag_.facing))
        drag_.ghost.push_back({{o.target.x, o.target.y, heightAt(o.target.x, o.target.y)}, o.face});
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
    // The way the line points: the whole arrangement turns to face it.
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
