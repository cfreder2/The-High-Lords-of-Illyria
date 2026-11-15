// A battle as a match's field (battle.h, mode.h): an empty field the mode
// fills, squads raised mid-battle, allies' squads the player cannot order,
// and the computer's orders for the squads of the computer's seats.
#include <algorithm>
#include <climits>
#include <cmath>

#include "illyria/game/battle.h"
#include "illyria/game/mode.h"

namespace illyria::game {

using sim::UnitType;

Battle::Battle(uint64_t seed, const sim::Vec3& centre, std::shared_ptr<const sim::Continent> continent, float farReachM)
    : scenario_(Scenario::Mode) {
    continent_ = continent ? std::move(continent) : std::make_shared<const sim::Continent>(kRegionSeed);
    foeName_.clear();
    build(seed, centre, farReachM);
    finish();
}

void Battle::setZone(V3 centre, float radius, int32_t pctPerSecond) {
    zoneCentre_ = {centre.x, centre.y, 0};
    zoneRadius_ = std::max(0.0f, radius);
    world_->setZone(toSim(zoneCentre_), sim::Fixed::fromInt(static_cast<int64_t>(zoneRadius_)), pctPerSecond);
}

Battle::~Battle() = default;

void Battle::setMode(std::unique_ptr<BattleMode> mode) { mode_ = std::move(mode); }

void Battle::growUnits() {
    const size_t n = world_->units().size();
    if (deathTick_.size() >= n) return;
    const size_t old = prevUnitPos_.size();
    deathTick_.resize(n, UINT32_MAX);
    attackTick_.resize(n, UINT32_MAX);
    hitTick_.resize(n, UINT32_MAX);
    selectedFlag_.resize(n, 0);
    prevUnitPos_.resize(n);
    seenGen_.resize(n, 0);
    for (size_t i = old; i < n; ++i) prevUnitPos_[i] = toV3(world_->units()[i].pos), seenGen_[i] = world_->generation(world_->units()[i].id);
    if (!foreign_.empty()) foreign_.resize(n, 0);
}

int Battle::addSquad(const std::string& name, sim::PlayerId owner, UnitType type, int count, V3 centre, V3 facing, int rank) {
    spawnSquad(name, owner, type, std::max(1, count), centre, facing.normalized());
    if (rank > 0)
        for (sim::EntityId id : squads_.back().members) world_->setVeterancy(id, rank, 0);
    growUnits();
    return static_cast<int>(squads_.size()) - 1;
}

V3 Battle::clearSpot(UnitType type, int count, V3 centre, V3 facing, float reachM) const {
    facing = facing.normalized();
    const V3 right{facing.y, -facing.x, 0};
    const std::vector<V3> slots = formationSlots(defaultFormation(type), std::max(1, count), formationSpacing(type));
    const auto fits = [&](V3 c) {
        if (c.x < 2 || c.y < 2 || c.x > kWorldSize - 2 || c.y > kWorldSize - 2) return false;
        for (const V3& s : slots)
            if (!world_->standable(toSim(c + right * s.x + facing * s.y))) return false;
        return true;
    };
    if (fits(centre)) return centre;
    // Out in rings, a few metres at a time, the way round taken in turn.
    for (float r = 4; r <= reachM; r += 4)
        for (int k = 0; k < 24; ++k) {
            const float a = static_cast<float>(k) * 0.2617994f;
            const V3 c = centre + V3{std::cos(a), std::sin(a), 0} * r;
            if (fits(c)) return c;
        }
    return centre;
}

void Battle::setLocalSide(sim::PlayerId side) {
    local_ = side;
    voice_.setPlayer(side);
    sfx_.setPlayer(side);
    std::erase_if(selection_, [&](sim::EntityId id) { return !controllable(world_->units()[id - 1]); });
    refreshSelection();
}

void Battle::setForeign(size_t squad, bool foreign) {
    if (squad >= squads_.size()) return;
    foreign_.resize(world_->units().size(), 0);
    for (sim::EntityId id : squads_[squad].members) foreign_[id - 1] = foreign ? 1 : 0;
    // Nobody keeps an ally's men selected.
    std::erase_if(selection_, [&](sim::EntityId id) { return !controllable(world_->units()[id - 1]); });
    refreshSelection();
}

sim::EntityId Battle::returnFallen(size_t squad, sim::EntityId fallen, V3 at, V3 facing, int32_t hpPct) {
    if (squad >= squads_.size()) return sim::kNoEntity;
    Squad& s = squads_[squad];
    const auto place = std::find(s.members.begin(), s.members.end(), fallen);
    if (place == s.members.end()) return sim::kNoEntity;
    const sim::EntityId id = world_->raiseFallen(fallen, toSim(at), toSim(facing.normalized()), hpPct);
    if (id == sim::kNoEntity) return id;
    // He takes the fallen one's place in the squad (its card, its banner).
    *place = id;
    if (squadOfUnit_.size() < id) squadOfUnit_.resize(id, -1);
    squadOfUnit_[id - 1] = static_cast<int>(squad);
    const bool wasForeign = fallen <= foreign_.size() && foreign_[fallen - 1];
    growUnits();
    if (wasForeign) foreign_[id - 1] = 1;
    return id;
}

void Battle::aiSteer(size_t squad, const V3& to) {
    if (squad < squads_.size()) steerSquad(squads_[squad], to);
}

void Battle::aiForceMarch(size_t squad, const V3& to) {
    if (squad >= squads_.size()) return;
    const Squad& s = squads_[squad];
    std::vector<sim::EntityId> alive;
    for (sim::EntityId id : s.members)
        if (world_->units()[id - 1].alive()) alive.push_back(id);
    s.goal = to;
    s.hasGoal = true;
    moveGroup(s.owner, std::move(alive), s.formation, to, nullptr, s.facing, true);
}

void Battle::aiAttack(size_t squad, sim::EntityId target) {
    if (squad >= squads_.size()) return;
    const sim::Unit* t = world_->findUnit(target);
    if (t == nullptr || !t->alive()) return;
    const Squad& s = squads_[squad];
    s.hasGoal = false;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive() || u.routed) continue;
        // Each goes for a foe at hand before the squad's (in a crowd, one
        // target for all leaves most of them pushing past the men striking them).
        const V3 at = toV3(u.pos);
        const sim::EntityId near = nearestFoe(s.owner, at, 4.0f);
        const sim::EntityId pick = near != sim::kNoEntity ? near : target;
        if (u.targetUnit == pick) continue;
        // Already after someone still standing his ground, and near: let him finish.
        if (u.targetUnit != sim::kNoEntity) {
            const sim::Unit* cur = world_->findUnit(u.targetUnit);
            if (cur && cur->alive() && !cur->routed && (near == sim::kNoEntity || (toV3(cur->pos) - at).length() < 4.0f)) continue;
        }
        const sim::Unit& p = world_->units()[pick - 1];
        sim::Command c{s.owner, id, sim::Command::Type::AttackUnit, p.pos};
        c.targetUnit = pick;
        pending_.push_back(c);
    }
}

void Battle::aiHalt(size_t squad) {
    if (squad >= squads_.size()) return;
    const Squad& s = squads_[squad];
    s.hasGoal = false;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.alive() && (u.hasMoveTarget || u.targetUnit != sim::kNoEntity)) issue(s.owner, id, sim::Command::Type::Stop, {});
    }
}

void Battle::aiRaid(size_t squad, const V3& goal) {
    // From the simulation's state only, like the rest of the computer's orders.
    if (squad >= squads_.size()) return;
    const Squad& s = squads_[squad];
    if (aiMemo_.size() < squads_.size()) aiMemo_.resize(squads_.size());
    AiMemo& memo = aiMemo_[squad];
    std::vector<sim::EntityId> alive;
    bool raiding = false;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive() || u.routed) continue;
        alive.push_back(id);
        raiding = raiding || u.scav.raiding;
    }
    if (alive.empty()) return;
    // Raiding: theirs to run. Just over: back out of it a way, and a rest before the next.
    const uint32_t now = world_->tick();
    if (raiding) {
        memo.raiding = true;
        return;
    }
    const V3 c = squadCentre(s, false);
    if (memo.raiding) {
        memo.raiding = false;
        memo.restUntil = now + kRaidRestTicks;
        if (const sim::EntityId foe = nearestFoe(s.owner, c, kRaidCallM); foe != sim::kNoEntity) {
            const V3 fp = toV3(world_->units()[foe - 1].pos);
            const V3 away = V3{c.x - fp.x, c.y - fp.y, 0}.normalized();
            steerSquad(s, c + away * kRaidCallM);
        }
        return;
    }
    if (now < memo.restUntil) return;
    // Near enough to see the foe: let loose. Else on toward him.
    if (nearestFoe(s.owner, c, kRaidCallM) != sim::kNoEntity) {
        s.hasGoal = false;
        for (sim::EntityId id : alive) issue(s.owner, id, sim::Command::Type::Raid, {});
        memo.raiding = true;
        return;
    }
    if (V3{goal.x - c.x, goal.y - c.y, 0}.length() > 6.0f) steerSquad(s, goal);
}

sim::EntityId Battle::nearestFoe(sim::PlayerId owner, const V3& at, float reach) const {
    sim::EntityId best = sim::kNoEntity;
    float bestD2 = reach * reach;
    for (const sim::Unit& u : world_->units()) {
        if (!u.alive() || u.owner == owner || u.routed || u.inAir()) continue;
        if (aiDryFoes_ && world_->wading(u)) continue;
        const V3 d = toV3(u.pos) - at;
        const float d2 = d.x * d.x + d.y * d.y;
        if (d2 < bestD2) {
            bestD2 = d2;
            best = u.id;
        }
    }
    return best;
}

namespace {
float dot2(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y; }
V3 dir2(const V3& from, const V3& to) {
    V3 d{to.x - from.x, to.y - from.y, 0};
    return d.length() > 0.001f ? d.normalized() : V3{1, 0, 0};
}
// Casualties that make a squad of this many men shattered: a third left.
bool shatteredSelf(int alive, int total, int wavering) {
    return alive > 0 && ((total >= 4 && alive * 3 <= total) || wavering * 2 >= alive);
}
}  // namespace

bool Battle::aiShattered(const Squad& s) const {
    const auto broken = [&](const Squad& q, int& alive) {
        alive = 0;
        int wavering = 0;
        for (sim::EntityId id : q.members) {
            const sim::Unit& u = world_->units()[id - 1];
            if (!u.alive()) continue;
            ++alive;
            wavering += u.routed || u.wavering();
        }
        return shatteredSelf(alive, static_cast<int>(q.members.size()), wavering);
    };
    int alive;
    if (!broken(s, alive)) return false;
    // Only with someone to fall back on; cornered, they fight on.
    for (const Squad& f : squads_) {
        if (f.owner != s.owner || &f == &s || f.garrison) continue;
        const Role r = roleOf(f.type);
        if (r == Role::Siege || r == Role::Healer) continue;
        int fa;
        if (!broken(f, fa) && fa > 0) return true;
    }
    return false;
}

sim::EntityId Battle::pickFoe(sim::PlayerId owner, const V3& around, float reach, bool mounted) const {
    if (aiSkill_ == AiSkill::Easy) return nearestFoe(owner, around, reach);
    sim::EntityId best = sim::kNoEntity;
    float bestScore = reach;
    for (const sim::Unit& u : world_->units()) {
        if (!u.alive() || u.owner == owner || u.routed || u.inAir()) continue;
        if (aiDryFoes_ && world_->wading(u)) continue;
        const V3 d = toV3(u.pos) - around;
        const float d2 = d.x * d.x + d.y * d.y;
        if (d2 >= reach * reach) continue;
        // The softer the target, the nearer it counts: horse ride for the
        // archers and the healers, foot only prefer them a little.
        float f = 1.0f;
        if (u.stats.type == static_cast<uint8_t>(UnitType::Hero)) f = 0.7f;
        else
            switch (roleOf(static_cast<UnitType>(u.stats.type))) {
                case Role::Healer: f = 0.45f; break;
                case Role::Caster: f = 0.5f; break;
                case Role::Siege: f = 0.5f; break;
                case Role::Ranged:
                case Role::Bomber: f = 0.55f; break;
                default: break;
            }
        if (!mounted) f = (1.0f + f) * 0.5f;
        f *= 0.6f + 0.4f * static_cast<float>(u.hp) / static_cast<float>(std::max(1, u.stats.maxHp));  // the wounded first
        const float score = std::sqrt(d2) * f;
        if (score < bestScore) {
            bestScore = score;
            best = u.id;
        }
    }
    return best;
}

bool Battle::footFront(sim::PlayerId owner, const Squad& except, const V3& dir, float& front) const {
    bool any = false;
    front = -1e30f;
    for (const Squad& f : squads_) {
        if (f.owner != owner || &f == &except || f.garrison || aliveCount(f) == 0) continue;
        if (roleOf(f.type) != Role::Melee || f.type == UnitType::Hero || aiShattered(f)) continue;
        const V3 fc = squadCentre(f, false);
        const float p = dot2(fc, dir);
        if (p > front) {
            front = p;
            any = true;
        }
    }
    return any;
}

bool Battle::armyStrung(const Squad& s, const V3& dir) const {
    // A friend within reach of the army that is well behind this squad: it
    // would arrive piecemeal, alone, ahead of the rest.
    const V3 c = squadCentre(s, false);
    const float mine = dot2(c, dir);
    for (const Squad& f : squads_) {
        if (f.owner != s.owner || &f == &s || f.garrison || aliveCount(f) == 0) continue;
        const Role r = roleOf(f.type);
        if (r == Role::Siege || r == Role::Healer || r == Role::Flier) continue;
        const V3 fc = squadCentre(f, false);
        if ((fc - c).length() < 150.0f && dot2(fc, dir) < mine - 35.0f && !aiShattered(f)) return true;
    }
    return false;
}


bool Battle::aiSiege(sim::PlayerId owner, const V3& from, V3& engines, V3& gate) const {
    // While any of its engines is at a standing gate (aimEngines has sent it), the siege is on: the battery nearest `from`
    // (a camp before each door, or one before the gate and one before a side wall), and the gate being battered.
    int at = -1;
    float best = 1e30f;
    for (const Squad& s : squads_) {
        if (s.owner != owner || roleOf(s.type) != Role::Siege || aliveCount(s) == 0) continue;
        for (sim::EntityId id : s.members) {
            const sim::Unit& u = world_->units()[id - 1];
            if (u.alive() && u.attackGate >= 0 && u.hasAttackTarget && !world_->gates()[static_cast<size_t>(u.attackGate)].broken()) at = u.attackGate;
        }
        const V3 c = squadCentre(s, false);
        const float d = V3{c.x - from.x, c.y - from.y, 0}.length();
        if (d < best) best = d, engines = c;
    }
    if (at < 0) return false;
    gate = toV3(worldRegion().gates()[static_cast<size_t>(at)].a + worldRegion().gates()[static_cast<size_t>(at)].b) * 0.5f;
    return true;
}

bool Battle::siegeGate(size_t gate) const {
    if (!capture_.active()) return true;
    for (const CapturePoint::Stage& stage : capture_.stages())
        if (worldRegion().gates()[gate].ring == stage.ring) return true;
    return false;
}

bool Battle::aiShellsWalls(const Squad& s) const {
    // A second battery (another of its side's is nearer the gate, and has that work) with the foe's men on a wall-walk within
    // the reach it creeps to (World::creepToWall): left to itself it shells them (World::autoTarget), which is its work, and
    // the gate is battered by one train, not two (a keeper's clock is the gate's strength: battle_capture.h).
    const sim::Region& rg = worldRegion();
    const V3 c = squadCentre(s, false);
    int gate = -1;
    float best = kSiegeScope;
    for (size_t i = 0; i < rg.gates().size(); ++i) {
        const sim::GateState& gs = world_->gates()[i];
        if (gs.broken() || gs.holder == s.owner || gs.holder == sim::GateState::kNoHolder || !siegeGate(i)) continue;
        const V3 g = toV3(rg.gates()[i].a + rg.gates()[i].b) * 0.5f;
        const float d = V3{g.x - c.x, g.y - c.y, 0}.length();
        if (d < best) best = d, gate = static_cast<int>(i);
    }
    if (gate < 0) return false;
    const V3 g = toV3(rg.gates()[static_cast<size_t>(gate)].a + rg.gates()[static_cast<size_t>(gate)].b) * 0.5f;
    bool second = false;
    for (const Squad& o : squads_) {
        if (&o == &s || o.owner != s.owner || roleOf(o.type) != Role::Siege || aliveCount(o) == 0) continue;
        const V3 oc = squadCentre(o, false);
        second = second || V3{oc.x - g.x, oc.y - g.y, 0}.length() < best;
    }
    if (!second) return false;
    for (const sim::Unit& o : world_->units()) {
        if (!o.alive() || o.owner == s.owner || o.inAir() || rg.rampartAt(o.pos.x, o.pos.y, o.pos.z) < 0) continue;
        const V3 p = toV3(o.pos);
        if (V3{p.x - c.x, p.y - c.y, 0}.length() <= kWallReachM) return true;
    }
    return false;
}

bool Battle::aiBreakOff() {
    if (besiegers_ == 0) return false;
    if (!brokeOff_) {
        // Spent: so few of its fighting men still stand that the storm cannot be made, or has failed.
        int standing = 0;
        for (const sim::Unit& u : world_->units())
            standing += u.owner == kEnemy && u.alive() && !u.routed && roleOf(static_cast<UnitType>(u.stats.type)) != Role::Siege;
        if (standing * 100 > besiegers_ * kSpentPct) return false;
        brokeOff_ = true;
    }
    // Away from the town, deaf to the foe.
    for (size_t q = 0; q < squads_.size(); ++q) {
        const Squad& s = squads_[q];
        if (s.owner != kEnemy || aliveCount(s) == 0) continue;
        const V3 c = squadCentre(s, false);
        const V3 to = c + dir2(siegeCentre_, c) * 150.0f;
        aiForceMarch(q, {std::clamp(to.x, 40.0f, kWorldSize - 40.0f), std::clamp(to.y, 40.0f, kWorldSize - 40.0f), 0});
    }
    return true;
}

void Battle::aiEscort(size_t squad, const V3& engines, const V3& gate) {
    // The engines work from their own reach, and their men keep the line in front of them (World::escortSpot): come up to them, then stand.
    const V3 c = squadCentre(squads_[squad], false);
    if (V3{c.x - engines.x, c.y - engines.y, 0}.length() > 50.0f) aiSteer(squad, engines + dir2(engines, gate) * 12.0f);
    else aiHalt(squad);
}

void Battle::aiFight(size_t squad, const V3& goal, const V3& around, float reach) {
    // As runEnemyAi, for any side, from the simulation's state only.
    if (squad >= squads_.size()) return;
    const Squad& s = squads_[squad];
    const int alive = aliveCount(s);
    if (alive == 0 || standing(s).broken == alive) return;  // routed: beyond orders
    const V3 c = squadCentre(s, false);
    const Role role = roleOf(s.type);
    const auto go = [&](const V3& to) {
        if (V3{to.x - c.x, to.y - c.y, 0}.length() > 6.0f) aiSteer(squad, to);
    };
    if (role == Role::Siege) return;  // engines throw from where they stand
    if (role == Role::Scout) {
        aiRaid(squad, goal);  // the hyena-folk are let loose near the foe (world_scavenge.cpp)
        return;
    }
    if (role == Role::Healer) {
        // A little behind the nearest friendly fighting squad.
        float best = 1e30f;
        V3 at = goal;
        for (const Squad& f : squads_) {
            if (f.owner != s.owner || &f == &s || aliveCount(f) == 0 || roleOf(f.type) == Role::Healer) continue;
            const V3 fc = squadCentre(f, false);
            const float d = (fc - c).length();
            if (d < best) {
                best = d;
                at = fc;
            }
        }
        if (best > 15.0f) go(at + (c - at).normalized() * 8.0f);
        return;
    }
    // Stuck (not moving, not fighting) while after a foe: the foe cannot be
    // reached from here (a cliff, a wood packed with men); march to the goal
    // deaf to him for a few looks.
    if (aiMemo_.size() < squads_.size()) aiMemo_.resize(squads_.size());
    AiMemo& memo = aiMemo_[squad];
    bool fighting = false, ordered = false;
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (!u.alive()) continue;
        fighting = fighting || u.engaged || u.reloadRemaining > 0;
        ordered = ordered || u.targetUnit != sim::kNoEntity || u.hasMoveTarget;
    }
    const float moved = V3{c.x - memo.last.x, c.y - memo.last.y, 0}.length();
    memo.last = c;
    memo.still = !fighting && ordered && moved < 1.0f ? memo.still + 1 : 0;
    if (memo.still >= 3) {
        memo.still = 0;
        memo.detour = 4;
        aiForceMarch(squad, goal);
        return;
    }
    if (memo.detour > 0) {
        --memo.detour;
        if (V3{goal.x - c.x, goal.y - c.y, 0}.length() > 6.0f) return;  // still on the way round
        memo.detour = 0;
    }
    const bool smart = aiSkill_ == AiSkill::Normal;
    if (memo.rested > 0) --memo.rested;
    const sim::UnitStats stats = sim::unitStats(s.type);
    if (smart) {
        // The shattered fall back on their friends to mend, deaf to the foe,
        // and stay out a few looks before they come back.
        if (memo.retreat > 0) {
            --memo.retreat;
            return;
        }
        const sim::EntityId threat = nearestFoe(s.owner, c, 60.0f);
        if (threat != sim::kNoEntity && aiShattered(s)) {
            const V3 away = dir2(toV3(world_->units()[threat - 1].pos), c);
            const Squad* friendly = nullptr;
            float best = 1e30f;
            for (const Squad& f : squads_) {
                if (f.owner != s.owner || &f == &s || f.garrison || aliveCount(f) == 0 || aiShattered(f)) continue;
                const Role r = roleOf(f.type);
                if (r == Role::Siege) continue;
                const float d = (squadCentre(f, false) - c).length();
                if (d < best) {
                    best = d;
                    friendly = &f;
                }
            }
            if (friendly) {
                // Behind them: on their far side from the foe.
                aiForceMarch(squad, squadCentre(*friendly, false) + away * 14.0f);
                memo.retreat = 4;
                return;
            }
        }
    }
    const sim::EntityId foe = pickFoe(s.owner, around, reach, stats.mounted);
    if (foe == sim::kNoEntity) {
        go(goal);
        return;
    }
    const V3 fp = toV3(world_->units()[foe - 1].pos);
    const float dist = V3{fp.x - c.x, fp.y - c.y, 0}.length();
    const float range = static_cast<float>(stats.ranged.autoRange.toDouble());
    const V3 fdir = dir2(c, fp);
    if (smart && dist > 60.0f && role != Role::Flier && role != Role::Ranged && role != Role::Caster && s.type != UnitType::Hero) {
        // Mass before engaging: nobody marches in ahead of the army, for a while.
        bool joined = false;
        for (const Squad& f : squads_)
            for (sim::EntityId id : f.members)
                if (f.owner == s.owner && world_->units()[id - 1].alive() && world_->units()[id - 1].engaged) joined = true;
        if (!joined && memo.rested == 0 && armyStrung(s, fdir)) {
            if (++memo.waited <= 8) {
                aiHalt(squad);
                return;
            }
            memo.waited = 0;
            memo.rested = 10;  // the laggard is not coming: go
        } else if (!armyStrung(s, fdir)) {
            memo.waited = 0;
        }
    }
    V3 flank;
    const bool covered = smart && (role == Role::Ranged || role == Role::Caster || s.type == UnitType::Hero);
    float front = 0;
    if (covered && footFront(s.owner, s, fdir, front)) {
        // Behind the foot: the archers and the lord never stand ahead of it.
        const float gap = s.type == UnitType::Hero ? 10.0f : 7.0f;
        const float limit = front - gap;
        const float mine = dot2(c, fdir);
        const float keep = range > 0 ? range * 0.8f : 0.0f;
        if (mine > limit + 2.0f) {
            aiSteer(squad, c - fdir * (mine - limit));  // back through the line
        } else if (range > 0 && dist > keep) {
            const float room = limit - mine;
            if (room > 3.0f) aiSteer(squad, c + fdir * std::min(dist - keep + 4, room));  // on with the foot, not past
            else aiHalt(squad);
        } else if (s.type == UnitType::Hero && dist > 20.0f) {
            const float room = limit - mine;
            if (room > 3.0f) aiSteer(squad, c + fdir * room);
            else aiHalt(squad);
        } else if (range > 0) {
            aiHalt(squad);
        }
        if (range > 0 || s.type == UnitType::Hero) return;
    }
    if (stats.mounted && role == Role::Melee && flankPoint(s, flank)) {
        aiSteer(squad, flank);  // round a foe locked in melee, into its flank or rear
    } else if (range > 0 && role != Role::Flier) {
        // A good bowshot off, then they shoot on their own.
        const float keep = range * 0.8f;
        if (dist > keep) aiSteer(squad, c + (fp - c) * ((dist - keep + 4) / dist));
        else if (role != Role::Bomber) aiHalt(squad);  // (grenadiers in range lay satchels and step back from them: no halt)
    } else if (dist < 30.0f) {
        aiAttack(squad, foe);
    } else {
        aiSteer(squad, fp);
    }
}

}  // namespace illyria::game
