// Conquest in the one battle system (docs/ONE_WORLD.md, docs/ARCHITECTURE_RULES.md §1): the war for the whole island is
// a battle like any other, and this Battle presents it. The player's men there are units of the conquest's world, the
// Battle's squads and selection; the same taps, boxes, presses and command bar drive them as in a skirmish. Only two
// things are the conquest's own, and both are here:
//
// - The selection is of men (their ids), kept through a man's boarding a boat and landing, and made again each frame
//   into the world's units (presentConquest).
// - An order of the selection is the world's (commands to units, which Orders carry into the conquest's step) unless it
//   is out of the men's reach: then it is a march of their squads along a route planned on the conquest's coarse grid
//   (marchOrder, the one place an order becomes the conquest's), whose cells are only waypoints: the world's own
//   navigation, steering, ramps and gates do every step of it.

#include <algorithm>
#include <cmath>

#include "illyria/game/battle.h"

namespace illyria::game {

bool Battle::sees(uint8_t owner, float x, float y) const {
    return !conquest_ || conquest_->canSee(conquest_->localSide(), owner, x + origin().x, y + origin().y);
}

void Battle::borrow(sim::Campaign* conquest) {
    conquest_ = conquest;
    if (conquest == nullptr) {
        world_ = ownWorld_.get();
        alphaSet_ = false;
        setLocalSide(kPlayer);
    } else {
        world_ = conquest->presentedWorld();
        setLocalSide(conquest->localSide());
    }
    squads_.clear();
    squadOfUnit_.clear();
    selection_.clear();
    selMen_.clear();
    manOfUnit_.clear();
    armyMemo_.clear();
    squadOfArmy_.clear();
    inspected_ = {};
    inspectedUnit_ = 0;
    selStack_ = 0;
    pending_.clear();
    forgetMen();
}

int Battle::squadOfArmy(uint32_t army) const {
    const auto it = squadOfArmy_.find(army);
    return it == squadOfArmy_.end() ? -1 : it->second;
}

void Battle::presentConquest(const std::vector<sim::Campaign::Soldier>& men) {
    using Soldier = sim::Campaign::Soldier;
    const sim::Campaign& c = *conquest_;
    growUnits();
    const size_t n = world_->units().size();
    // Who is of which army: one pass over the stacks.
    struct Of {
        const sim::Army* army;
        uint32_t stack;
    };
    std::map<uint32_t, Of> armies;
    for (const sim::Campaign::Stack& s : c.stacks())
        for (const sim::Army& a : s.armies) armies[a.id] = {&a, s.id};

    for (const Squad& q : squads_) armyMemo_[q.army] = {q.formation, q.facing};  // (the squads are made afresh)
    squads_.clear();
    squadOfArmy_.clear();
    squadOfUnit_.assign(n, -1);
    manOfUnit_.assign(n, 0);
    // The selection and the one looked at, as men, found again among the men now.
    const auto chosen = [&](uint32_t id) { return std::binary_search(selMen_.begin(), selMen_.end(), id); };
    std::vector<sim::EntityId> units;
    std::vector<uint32_t> kept;
    uint32_t stack = 0;
    bool oneStack = true;
    inspectedUnit_ = 0;
    // (The men come army by army: the army and its squad are looked up when it changes.)
    uint32_t armyNow = 0;
    const Of* of = nullptr;
    int* squadNow = nullptr;
    for (const Soldier& m : men) {
        if (armyNow != m.army || squadNow == nullptr) {
            armyNow = m.army;
            const auto found = armies.find(m.army);
            of = found == armies.end() ? nullptr : &found->second;
            squadNow = of == nullptr ? nullptr : &squadOfArmy_.try_emplace(m.army, -1).first->second;
        }
        if (of == nullptr) continue;
        int& squad = *squadNow;
        if (squad < 0) {
            const sim::Army& a = *of->army;
            Squad q;
            q.name = a.name.empty() ? sim::unitTypeName(a.type) : a.name;
            q.owner = m.owner;
            q.type = a.type;
            const auto memo = armyMemo_.find(a.id);
            q.formation = memo != armyMemo_.end() ? memo->second.formation : defaultFormation(a.type);
            q.facing = memo != armyMemo_.end() ? memo->second.facing : V3{1, 0, 0};
            q.army = a.id;
            q.stack = of->stack;
            q.total = a.fullStrength;
            q.told = true;
            q.standing.strength = sim::Campaign::strength(a);
            q.standing.rank = a.hero() ? 0 : a.rank;
            q.standing.level = a.hero() ? a.rank + 1 : 0;
            q.standing.blessings = a.blessings;
            q.standing.kills = a.kills;
            for (uint8_t k : a.artifacts) q.arms += (q.arms.empty() ? "" : ", ") + std::string(sim::Campaign::artifacts()[k].name);
            q.rare = sim::Campaign::rare(a.type) && !a.hero();
            q.hired = a.ally;
            q.command = sim::Campaign::command(a);
            squad = static_cast<int>(squads_.size());
            squads_.push_back(std::move(q));
        }
        if (!m.alive) continue;
        Squad& q = squads_[static_cast<size_t>(squad)];
        const bool mine = chosen(m.id) && m.owner == local_;
        if (m.unitId && m.unitId <= n) {
            q.members.push_back(m.unitId);
            squadOfUnit_[m.unitId - 1] = squad;
            manOfUnit_[m.unitId - 1] = m.id;
            if (mine) units.push_back(m.unitId);
            if (inspected_.kind == Inspected::Kind::Man && inspected_.id == m.id) inspectedUnit_ = m.unitId;
        } else {
            continue;
        }
        if (mine) {
            kept.push_back(m.id);
            if (stack == 0) stack = m.stack;
            oneStack = oneStack && stack == m.stack;
        }
    }
    std::sort(kept.begin(), kept.end());
    selMen_ = std::move(kept);
    selStack_ = oneStack ? stack : 0;
    selection_ = std::move(units);
    refreshSelection();
    if (inspected_.kind == Inspected::Kind::Gate && (inspected_.id >= world_->gates().size())) inspected_ = {};
}

void Battle::flushOrders() {
    if (!conquest_) return;
    for (const sim::Command& cmd : pending_) {
        sim::Campaign::Order o;
        o.kind = sim::Campaign::Order::Kind::Command;
        o.side = local_;
        o.command = cmd;
        conquest_->submit(std::move(o));
    }
    pending_.clear();
}

void Battle::selectStack(uint32_t stack) {
    std::vector<sim::EntityId> units;
    for (const Squad& q : squads_)
        if (q.stack == stack && q.owner == local_) units.insert(units.end(), q.members.begin(), q.members.end());
    select(std::move(units));
}

uint32_t Battle::selectedArmy() const {
    const int s = selectedSquad();
    return s >= 0 ? squads_[static_cast<size_t>(s)].army : 0;
}

void Battle::keepSelectedMen() {
    if (!conquest_) return;
    selMen_.clear();
    for (sim::EntityId id : selection_)
        if (id <= manOfUnit_.size() && manOfUnit_[id - 1]) selMen_.push_back(manOfUnit_[id - 1]);
    std::sort(selMen_.begin(), selMen_.end());
    selMen_.erase(std::unique(selMen_.begin(), selMen_.end()), selMen_.end());
}

// --- The one translation: a selection's order becomes the conquest's ---------

void Battle::submitMarch(const std::vector<uint32_t>& armies, int32_t cx, int32_t cy) {
    // By stack: the squads of a stack that are ordered; all of them, the stack's own march.
    std::map<uint32_t, std::vector<uint32_t>> byStack;
    for (uint32_t army : armies) {
        const int s = squadOfArmy(army);
        if (s >= 0) byStack[squads_[static_cast<size_t>(s)].stack].push_back(army);
    }
    for (auto& [stack, named] : byStack) {
        const sim::Campaign::Stack* st = conquest_->findStack(stack);
        if (!st) continue;
        sim::Campaign::Order o;
        o.kind = sim::Campaign::Order::Kind::Move;
        o.side = local_;
        o.stack = stack;
        o.a = cx;
        o.b = cy;
        if (named.size() < st->armies.size()) o.armies = std::move(named);
        conquest_->submit(std::move(o));
    }
}

bool Battle::beyondReach(const V3& from, const V3& to) { return flat(from - to).length() > kLocalReachM; }

// The squads (armies) marched to a place: the conquest's grid cell it is in, the Order, and where each was sent.
void Battle::marchArmies(std::vector<uint32_t> armies, const V3& to) {
    std::sort(armies.begin(), armies.end());
    armies.erase(std::unique(armies.begin(), armies.end()), armies.end());
    if (armies.empty()) return;
    const V3 o = origin();
    const int32_t cx = std::clamp(sim::Campaign::cellOf(static_cast<int32_t>(to.x + o.x)), 0, conquest_->width() - 1);
    const int32_t cy = std::clamp(sim::Campaign::cellOf(static_cast<int32_t>(to.y + o.y)), 0, conquest_->height() - 1);
    submitMarch(armies, cx, cy);
    for (uint32_t army : armies)
        if (const int s = squadOfArmy(army); s >= 0) {
            squads_[static_cast<size_t>(s)].goal = to;
            squads_[static_cast<size_t>(s)].hasGoal = true;
        }
}

bool Battle::marchOrder(const V3& to, bool wholly) {
    if (!conquest_) return false;
    const std::vector<sim::EntityId> awake = selectedAlive();
    // Out of reach of the men (a long way): the whole order is a march, of every squad ordered.
    const bool far = wholly || (!awake.empty() && beyondReach(centreOf(awake), to));
    if (!far) return false;
    std::vector<uint32_t> armies;
    for (sim::EntityId id : awake)
        if (const int s = squadOfUnit_[id - 1]; s >= 0) armies.push_back(squads_[static_cast<size_t>(s)].army);
    if (armies.empty()) return false;
    marchArmies(std::move(armies), to);
    return true;
}

// --- Picking and looking ------------------------------------------------------

Battle::ManPick Battle::manPick(const Camera& camera, float x, float y, float pixelScale) const {
    ManPick best;
    // His picture, else the nearest within a finger.
    if (const int u = unitAt(camera, x, y, pixelScale); u >= 0) {
        best = {ManPick::Kind::Unit, world_->units()[static_cast<size_t>(u)].id, static_cast<size_t>(u), 0.0f};
        float px, py;
        const V3 p = unitPosition(static_cast<size_t>(u));
        if (camera.project({p.x, p.y, p.z + 1.0f}, px, py)) best.px = std::hypot(px - x, py - y);
    }
    return best;
}

void Battle::inspect(Inspected what) {
    inspected_ = what;
    inspectedUnit_ = what.kind == Inspected::Kind::Unit ? what.id : 0;
    if (what.kind == Inspected::Kind::Man)
        for (size_t i = 0; i < manOfUnit_.size(); ++i)
            if (manOfUnit_[i] == what.id) inspectedUnit_ = static_cast<sim::EntityId>(i + 1);
}

Battle::Glance Battle::glance() const {
    Glance g;
    switch (inspected_.kind) {
        case Inspected::Kind::None: return g;
        case Inspected::Kind::Gate: {
            if (inspected_.id >= world_->gates().size()) return g;
            const sim::GateState& gate = world_->gates()[inspected_.id];
            g.any = g.gate = true;
            g.hp = std::max(0, gate.hp);
            g.max = worldRegion().gates()[inspected_.id].maxHp;
            g.broken = gate.broken();
            return g;
        }
        case Inspected::Kind::Unit:
        case Inspected::Kind::Man: break;
    }
    const sim::Unit* u = inspectedUnit_ ? world_->findUnit(inspectedUnit_) : nullptr;
    if (u && u->alive()) {
        g.any = g.awake = true;
        g.type = sim::unitTypeName(static_cast<sim::UnitType>(u->stats.type));
        g.owner = u->owner;
        g.hp = u->hp, g.max = u->stats.maxHp, g.rank = sim::veteranGrade(*u);
        g.morale = u->routed ? 2 : u->wavering() ? 1 : 0;
    }
    if (u && conquest_)
        if (const sim::Campaign::Stack* s = conquest_->findStack(conquest_->stackOfUnit(u->id))) g.stack = static_cast<int>(s->armies.size());
    return g;
}

const char* Battle::selectionLabel() const {
    int type = -1;
    bool mixed = false;
    const auto note = [&](int t) {
        if (type < 0) type = t;
        else if (type != t) mixed = true;
    };
    for (sim::EntityId id : selectedAlive()) note(static_cast<int>(world_->units()[id - 1].stats.type));
    if (type < 0) return "";
    return mixed ? "Mixed" : sim::unitTypeName(static_cast<sim::UnitType>(type));
}

std::string Battle::selectionEdges() const {
    for (sim::EntityId id : selectedAlive()) return world_->edgesOf(world_->units()[id - 1]).describe();
    return "";
}

bool Battle::selectionBatters() const {
    for (sim::EntityId id : selectedAlive())
        if (sim::World::canBatter(world_->units()[id - 1].stats)) return true;
    return false;
}

}  // namespace illyria::game
