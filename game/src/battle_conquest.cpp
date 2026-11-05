// Conquest in the one battle system (docs/ONE_WORLD.md, docs/ARCHITECTURE_RULES.md §1): the war for the whole island is
// a battle like any other, and this Battle presents it. The player's men there, awake in the conquest's world and asleep
// in their stacks, are the Battle's squads and selection; the same taps, boxes, presses and command bar drive them as
// in a skirmish. Only two things are the conquest's own, and both are here:
//
// - A man asleep has no unit (his stack is stepped as one body), so he is a Sleeper: one picture in the renderer's one
//   pass, selectable and inspectable like a unit. The selection is of men (their ids), kept through a man's waking and
//   sleeping, and made again each frame into the world's units and the sleepers (presentConquest).
// - An order of the selection is the world's (commands to units, which Orders carry into the conquest's step) unless
//   it is out of the men's reach or for men asleep: then it is a march of their squads, whole armies on the conquest's
//   grid (marchOrder, the one place an order becomes the conquest's), which the stack does in its own time and wakes
//   to a fight on the way.

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
    selSleeping_.clear();
    sleepers_.clear();
    sleeperIx_.clear();
    manOfUnit_.clear();
    armyMemo_.clear();
    squadOfArmy_.clear();
    inspected_ = {};
    inspectedUnit_ = 0;
    selStack_ = 0;
    pending_.clear();
    forgetMen();
}

size_t Battle::sleeperIndex(uint32_t man) const {
    const auto it = std::lower_bound(sleeperIx_.begin(), sleeperIx_.end(), std::make_pair(man, size_t{0}));
    return it != sleeperIx_.end() && it->first == man ? it->second : kNoSleeper;
}

V3 Battle::sleeperPosition(const Sleeper& m) const {
    const V3 o = origin();
    return {m.x - o.x, m.y - o.y, heightAt(m.x - o.x, m.y - o.y)};
}

V3 Battle::sleeperPositionOf(uint32_t man) const {
    const size_t i = sleeperIndex(man);
    return i == kNoSleeper ? V3{} : sleeperPosition(sleepers_[i]);
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
    sleepers_.clear();
    sleeperIx_.clear();
    squads_.clear();
    squadOfArmy_.clear();
    squadOfUnit_.assign(n, -1);
    manOfUnit_.assign(n, 0);
    // The selection and the one looked at, as men, found again among the men now.
    const auto chosen = [&](uint32_t id) { return std::binary_search(selMen_.begin(), selMen_.end(), id); };
    std::vector<sim::EntityId> units;
    std::vector<uint32_t> asleep, kept;
    uint32_t stack = 0;
    bool oneStack = true;
    inspectedUnit_ = 0;
    for (const Soldier& m : men) {
        const auto of = armies.find(m.army);
        if (of == armies.end()) continue;
        int& squad = squadOfArmy_.try_emplace(m.army, -1).first->second;
        if (squad < 0) {
            const sim::Army& a = *of->second.army;
            Squad q;
            q.name = a.name.empty() ? sim::unitTypeName(a.type) : a.name;
            q.owner = m.owner;
            q.type = a.type;
            const auto memo = armyMemo_.find(a.id);
            q.formation = memo != armyMemo_.end() ? memo->second.formation : defaultFormation(a.type);
            q.facing = memo != armyMemo_.end() ? memo->second.facing : V3{1, 0, 0};
            q.army = a.id;
            q.stack = of->second.stack;
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
        if (m.awake && m.unitId && m.unitId <= n) {
            q.members.push_back(m.unitId);
            squadOfUnit_[m.unitId - 1] = squad;
            manOfUnit_[m.unitId - 1] = m.id;
            if (mine) units.push_back(m.unitId);
            if (inspected_.kind == Inspected::Kind::Man && inspected_.id == m.id) inspectedUnit_ = m.unitId;
        } else if (!m.awake) {
            q.sleeping.push_back(m.id);
            Sleeper d;
            d.x = m.x, d.y = m.y, d.yaw = m.yaw, d.speed = m.speed, d.owner = m.owner, d.type = static_cast<uint8_t>(m.type), d.aloft = m.aloft;
            d.id = m.id, d.stack = m.stack, d.army = m.army;
            d.selected = mine;
            d.inspected = inspected_.kind == Inspected::Kind::Man && inspected_.id == m.id;
            sleeperIx_.push_back({m.id, sleepers_.size()});
            sleepers_.push_back(d);
            if (mine) asleep.push_back(m.id);
        } else {
            continue;
        }
        if (mine) {
            kept.push_back(m.id);
            if (stack == 0) stack = m.stack;
            oneStack = oneStack && stack == m.stack;
        }
    }
    std::sort(sleeperIx_.begin(), sleeperIx_.end());
    std::sort(kept.begin(), kept.end());
    selMen_ = std::move(kept);
    selStack_ = oneStack ? stack : 0;
    std::sort(asleep.begin(), asleep.end());
    selection_ = std::move(units);
    selSleeping_ = std::move(asleep);
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
    std::vector<uint32_t> asleep;
    for (const Squad& q : squads_)
        if (q.stack == stack && q.owner == local_) {
            units.insert(units.end(), q.members.begin(), q.members.end());
            asleep.insert(asleep.end(), q.sleeping.begin(), q.sleeping.end());
        }
    selectMen(std::move(units), std::move(asleep));
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
    selMen_.insert(selMen_.end(), selSleeping_.begin(), selSleeping_.end());
    std::sort(selMen_.begin(), selMen_.end());
    selMen_.erase(std::unique(selMen_.begin(), selMen_.end()), selMen_.end());
}

void Battle::selectMen(std::vector<sim::EntityId> units, std::vector<uint32_t> sleepers) {
    std::erase_if(sleepers, [&](uint32_t man) {
        const size_t i = sleeperIndex(man);
        return i == kNoSleeper || sleepers_[i].owner != local_;
    });
    std::sort(sleepers.begin(), sleepers.end());
    sleepers.erase(std::unique(sleepers.begin(), sleepers.end()), sleepers.end());
    selSleeping_ = std::move(sleepers);
    applySelection(std::move(units));
}

// --- The one translation: a selection's order becomes the conquest's ---------

void Battle::submitMarch(const std::vector<uint32_t>& armies, bool halt, int32_t cx, int32_t cy) {
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
        o.kind = halt ? sim::Campaign::Order::Kind::Halt : sim::Campaign::Order::Kind::Move;
        o.side = local_;
        o.stack = stack;
        o.a = cx;
        o.b = cy;
        if (named.size() < st->armies.size()) o.armies = std::move(named);
        conquest_->submit(std::move(o));
    }
}

bool Battle::marchOrder(const V3& to, bool wholly) {
    if (!conquest_) return false;
    const std::vector<sim::EntityId> awake = selectedAlive();
    // Out of reach of the men in the world (or none in it): the whole order is a march, of every squad ordered.
    bool far = wholly || awake.empty();
    if (!far) {
        const V3 d = flat(centreOf(awake) - to);
        far = d.length() > kLocalReachM;
    }
    std::vector<uint32_t> armies;
    for (uint32_t man : selSleeping_)
        if (const size_t i = sleeperIndex(man); i != kNoSleeper) armies.push_back(sleepers_[i].army);
    if (far)
        for (sim::EntityId id : awake)
            if (const int s = squadOfUnit_[id - 1]; s >= 0) armies.push_back(squads_[static_cast<size_t>(s)].army);
    std::sort(armies.begin(), armies.end());
    armies.erase(std::unique(armies.begin(), armies.end()), armies.end());
    if (armies.empty()) return false;
    const V3 o = origin();
    const int32_t cx = std::clamp(sim::Campaign::cellOf(static_cast<int32_t>(to.x + o.x)), 0, conquest_->width() - 1);
    const int32_t cy = std::clamp(sim::Campaign::cellOf(static_cast<int32_t>(to.y + o.y)), 0, conquest_->height() - 1);
    submitMarch(armies, false, cx, cy);
    for (uint32_t army : armies)
        if (const int s = squadOfArmy(army); s >= 0) {
            squads_[static_cast<size_t>(s)].goal = to;
            squads_[static_cast<size_t>(s)].hasGoal = true;
        }
    return far;
}

void Battle::haltOrder() {
    if (!conquest_ || selSleeping_.empty()) return;
    std::vector<uint32_t> armies;
    for (uint32_t man : selSleeping_)
        if (const size_t i = sleeperIndex(man); i != kNoSleeper) armies.push_back(sleepers_[i].army);
    std::sort(armies.begin(), armies.end());
    armies.erase(std::unique(armies.begin(), armies.end()), armies.end());
    submitMarch(armies, true, 0, 0);
}

// --- Picking and looking ------------------------------------------------------

Battle::ManPick Battle::manPick(const Camera& camera, float x, float y, float pixelScale) const {
    ManPick best;
    // A unit: his picture, else the nearest within a finger.
    if (const int u = unitAt(camera, x, y, pixelScale); u >= 0) {
        best = {ManPick::Kind::Unit, world_->units()[static_cast<size_t>(u)].id, static_cast<size_t>(u), 0.0f};
        float px, py;
        const V3 p = unitPosition(static_cast<size_t>(u));
        if (camera.project({p.x, p.y, p.z + 1.0f}, px, py)) best.px = std::hypot(px - x, py - y);
    }
    // A sleeper: the nearest within a finger, of those the viewer may see.
    float bestD = 30.0f * pixelScale;
    for (size_t i = 0; i < sleepers_.size(); ++i) {
        const V3 at = sleeperPosition(sleepers_[i]);
        if (!sees(sleepers_[i].owner, at.x, at.y)) continue;
        float px, py;
        if (!camera.project({at.x, at.y, at.z + 1.0f}, px, py)) continue;
        const float d = std::hypot(px - x, py - y);
        if (d < bestD && d < best.px) bestD = d, best = {ManPick::Kind::Sleeper, sleepers_[i].id, i, d};
    }
    return best;
}

void Battle::inspect(Inspected what) {
    inspected_ = what;
    inspectedUnit_ = what.kind == Inspected::Kind::Unit ? what.id : 0;
    if (what.kind == Inspected::Kind::Man)
        for (size_t i = 0; i < manOfUnit_.size(); ++i)
            if (manOfUnit_[i] == what.id) inspectedUnit_ = static_cast<sim::EntityId>(i + 1);
    for (Sleeper& m : sleepers_) m.inspected = what.kind == Inspected::Kind::Man && m.id == what.id;
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
    } else if (const size_t i = sleeperIndex(inspected_.id); i != kNoSleeper) {
        g.any = true;
        g.type = sim::unitTypeName(static_cast<sim::UnitType>(sleepers_[i].type));
        g.owner = sleepers_[i].owner;
        if (const sim::Campaign::Stack* s = conquest_ ? conquest_->findStack(sleepers_[i].stack) : nullptr) g.stack = static_cast<int>(s->armies.size());
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
    for (uint32_t man : selSleeping_)
        if (const size_t i = sleeperIndex(man); i != kNoSleeper) note(sleepers_[i].type);
    if (type < 0) return "";
    return mixed ? "Mixed" : sim::unitTypeName(static_cast<sim::UnitType>(type));
}

bool Battle::selectionBatters() const {
    for (sim::EntityId id : selectedAlive())
        if (sim::World::canBatter(world_->units()[id - 1].stats)) return true;
    for (uint32_t man : selSleeping_)
        if (const size_t i = sleeperIndex(man); i != kNoSleeper && sim::World::canBatter(sim::unitStats(static_cast<sim::UnitType>(sleepers_[i].type)))) return true;
    return false;
}

}  // namespace illyria::game
