#include "illyria/game/battle.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>

#include "illyria/sim/terrain.h"

namespace illyria::game {

using sim::UnitType;

namespace {

constexpr double kTickSeconds = 1.0 / sim::kTicksPerSecond;
constexpr int kMaxTicksPerFrame = 8;
constexpr int kAiIntervalTicks = sim::kTicksPerSecond * 2;
constexpr float kChargeBowshot = 60.0f;  // where missile troops halt when the line charges
constexpr float kSquadTapRadiusPx = 48.0f;
constexpr float kGravity = 9.81f;

// Small hash for cosmetic variation.
uint32_t mix(uint32_t a, uint32_t b) {
    uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u + (a << 6) + (a >> 2));
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return h;
}

float unit01(uint32_t h) { return static_cast<float>(h & 0xFFFF) / 65535.0f; }

// A town's outer ring of walls on the battlefield (the rampart round `centre`), or null.
const sim::Rampart* townRampart(const sim::Region& rg, V3 centre) {
    const sim::Rampart* best = nullptr;
    float bestD = 1e30f;
    for (const sim::Rampart& rp : rg.ramparts()) {
        const V3 c = toV3(rp.centre);
        const float d = (c.x - centre.x) * (c.x - centre.x) + (c.y - centre.y) * (c.y - centre.y);
        if (d < bestD) {  // the outer ring comes first, and keeps ties
            bestD = d;
            best = &rp;
        }
    }
    return best;
}

// `want` of the walls' embrasures (one archer each), spread evenly round them.
std::vector<sim::Rampart::Embrasure> spreadEmbrasures(const sim::Rampart& rp, int want) {
    std::vector<sim::Rampart::Embrasure> out;
    const size_t n = rp.embrasures.size();
    const size_t k = std::min(n, static_cast<size_t>(std::max(0, want)));
    for (size_t i = 0; i < k; ++i) out.push_back(rp.embrasures[(i * n + n / (2 * k)) / k]);
    return out;
}

// Archers within this of an embrasure hold it.
constexpr float kEmbrasureHeld = 1.2f;

}  // namespace

Role roleOf(UnitType type) {
    switch (type) {
        case UnitType::Archers: return Role::Ranged;
        case UnitType::DwarfGrenadier: return Role::Bomber;
        case UnitType::Wizards: return Role::Caster;
        case UnitType::Journeyman: return Role::Healer;
        case UnitType::Catapult: return Role::Siege;
        case UnitType::Griffins:
        case UnitType::Pegasi:
        case UnitType::Archons:
        case UnitType::Dragons: return Role::Flier;
        default: return Role::Melee;
    }
}

Battle::Battle(uint64_t seed, Scenario scenario) : scenario_(scenario) {
    // The battle is fought in a full-detail window of the continent: at
    // Oakhaven Ford, or at the neutral town nearest it.
    continent_ = std::make_shared<const sim::Continent>(kRegionSeed);
    sim::Vec3 centre = continent_->crossings().front().pos;
    if (scenario_ == Scenario::Siege) centre = siegeTown(*continent_).pos;
    build(seed, centre);
    if (scenario_ == Scenario::Siege) setUpSiege();
    else setUpFord();
    if (scenario_ == Scenario::Siege) placeCapturePoint(siegeTown(*continent_));
    if (scenario_ == Scenario::Siege) holdGates(kEnemy);
    finish();
}

Battle::Battle(uint64_t seed, const BattlePlan& plan) : scenario_(Scenario::Campaign) {
    continent_ = std::make_shared<const sim::Continent>(kRegionSeed);
    build(seed, plan.centre, plan.homeSide);
    setUpCampaign(plan);
    if (plan.city >= 0) placeCapturePoint(continent_->cities()[static_cast<size_t>(plan.city)]);
    if (plan.city >= 0) holdGates(kEnemy);
    finish();
}

void Battle::build(uint64_t seed, const sim::Vec3& centre, int homeSide) {
    sim::WorldConfig cfg;
    cfg.seed = seed;
    cfg.homeSide = homeSide;
    cfg.wind = {sim::Fixed::fromRatio(1, 2), -sim::Fixed::fromRatio(1, 2), sim::Fixed::zero()};
    cfg.maxRemains = kMaxRemains;
    region_ = std::make_shared<const sim::Region>(sim::Region::fromContinent(*continent_, centre, kTerrainCells));
    // The continent around the battlefield: all of it every 32 m, and a ring
    // a kilometre wide around the battlefield every 16 m.
    {
        const V3 o = toV3(region_->origin());
        far_.fill(*continent_, o, {-o.x, -o.y, 0}, static_cast<int>(sim::Continent::kWidth / FarGrid::kCell) + 1,
                  static_cast<int>(sim::Continent::kHeight / FarGrid::kCell) + 1, FarGrid::kCell);
        constexpr float kRing = 1024.0f, kNearCell = 16.0f;
        const int n = static_cast<int>((kWorldSize + 2 * kRing) / kNearCell) + 1;
        near_.fill(*continent_, o, {-kRing, -kRing, 0}, n, n, kNearCell);
    }
    heights_.reserve(region_->terrain().samples().size());
    for (sim::Fixed h : region_->terrain().samples()) heights_.push_back(static_cast<float>(h.toDouble()));
    cfg.region = region_;
    world_ = std::make_unique<sim::World>(cfg);
    dirtyFlag_.assign(kMaxRemains, 0);
}

void Battle::finish() {
    const size_t n = world_->units().size();
    deathTick_.assign(n, UINT32_MAX);
    attackTick_.assign(n, UINT32_MAX);
    hitTick_.assign(n, UINT32_MAX);
    selectedFlag_.assign(n, 0);
    prevUnitPos_.resize(n);
    for (size_t i = 0; i < n; ++i) prevUnitPos_[i] = toV3(world_->units()[i].pos);
}

void Battle::setUpCampaign(const BattlePlan& plan) {
    // Where the stacks met, with the armies as they stand: their numbers,
    // their ranks and their heroes. A town is held from its walls, as in a
    // siege; in the field the two lines face each other.
    foeName_ = plan.foeName;
    const V3 o = origin();
    const auto local = [&](const sim::Vec3& p) { return toV3(p) - V3{o.x, o.y, 0}; };
    const auto raise = [&](const BattleArmy& a, sim::PlayerId owner, V3 at, V3 facing, bool garrison) {
        if (a.soldiers <= 0) return;
        spawnSquad(a.name.empty() ? sim::unitTypeName(a.type) : a.name, owner, a.type, a.soldiers, at, facing);
        Squad& s = squads_.back();
        s.armyId = a.id;
        s.garrison = garrison;
        s.post = at;
        for (sim::EntityId id : s.members) world_->setVeterancy(id, a.rank, a.blessings);
    };
    if (plan.city >= 0) {
        const sim::Continent::City& town = continent_->cities()[static_cast<size_t>(plan.city)];
        const sim::Continent::Castle& k = town.castle;
        const sim::Continent::Castle::Ring& ring = k.rings.front();
        const size_t nc = ring.corners.size();
        const V3 centre = local(town.pos);
        const V3 ga = local(ring.corners[ring.gateEdge]), gb = local(ring.corners[(ring.gateEdge + 1) % nc]);
        const V3 gate = (ga + gb) * 0.5f;
        V3 out = V3{gate.x - centre.x, gate.y - centre.y, 0};
        out = out * (1.0f / out.length());
        const V3 in = out * -1.0f, side{out.y, -out.x, 0};
        // Defenders: archers on the walls, the rest at the gate, the courtyard and the keep.
        const V3 posts[3] = {gate - out * 12.0f, local(k.courtyard), local(k.keep) + out * (static_cast<float>(k.keepHalf.toDouble()) + 8)};
        int next = 0;
        const sim::Rampart* walls = townRampart(*region_, centre);
        int archers = 0, manned = 0;
        for (const BattleArmy& a : plan.foe) archers += a.type == UnitType::Archers ? a.soldiers : 0;
        const std::vector<sim::Rampart::Embrasure> spots = walls ? spreadEmbrasures(*walls, archers) : std::vector<sim::Rampart::Embrasure>{};
        for (const BattleArmy& a : plan.foe) {
            if (a.type == UnitType::Archers && a.soldiers > 0 && walls) {
                // One to each embrasure, spread round the walls while they
                // last; the rest wait at the foot of a ramp to step up.
                Squad s{a.name.empty() ? "Archers" : a.name, kEnemy, a.type, {}};
                s.formation = Formation::Loose;
                s.garrison = true;
                s.armyId = a.id;
                s.post = centre;
                const sim::UnitStats stats = sim::unitStats(a.type);
                const V3 foot = walls->ramps.empty() ? gate - out * 12.0f : toV3(walls->ramps.front().first);
                for (int i = 0; i < a.soldiers; ++i) {
                    const bool up = static_cast<size_t>(manned) < spots.size();
                    const V3 p = up ? toV3(spots[static_cast<size_t>(manned)].pos)
                                    : foot + in * (3.0f + static_cast<float>(i / 4 % 4) * 1.5f) + side * (static_cast<float>((i % 4) - 2) * 1.5f);
                    const V3 facing = up ? toV3(spots[static_cast<size_t>(manned)].out) : out;
                    manned += up;
                    const sim::EntityId id = world_->spawnUnit(kEnemy, toSim(p), toSim(facing), stats);
                    world_->setVeterancy(id, a.rank, a.blessings);
                    s.members.push_back(id);
                    if (squadOfUnit_.size() < id) squadOfUnit_.resize(id, -1);
                    squadOfUnit_[id - 1] = static_cast<int>(squads_.size());
                }
                squads_.push_back(std::move(s));
                continue;
            }
            raise(a, kEnemy, posts[next % 3] + side * static_cast<float>((next / 3) * 14), out, true);
            ++next;
        }
        // The attackers form up on the road before the gate.
        const V3 camp = gate + out * 100.0f;
        int k2 = 0;
        for (const BattleArmy& a : plan.player) {
            const float across = static_cast<float>((k2 % 4) * 2 - 3) * 14.0f;
            const float back = static_cast<float>(k2 / 4) * 18.0f;
            raise(a, kPlayer, camp + side * across + out * back, in, false);
            ++k2;
        }
        return;
    }
    // In the field: lines a bowshot apart, the player to the west.
    const V3 mid{kWorldSize / 2, kWorldSize / 2, 0}, east{1, 0, 0}, west{-1, 0, 0}, north{0, 1, 0};
    for (int sideIdx = 0; sideIdx < 2; ++sideIdx) {
        const std::vector<BattleArmy>& armies = sideIdx == 0 ? plan.player : plan.foe;
        const V3 front = mid + (sideIdx == 0 ? west : east) * 80.0f;
        int k2 = 0;
        for (const BattleArmy& a : armies) {
            const float across = static_cast<float>((k2 % 4) * 2 - 3) * 16.0f;
            const float back = static_cast<float>(k2 / 4) * 20.0f;
            raise(a, sideIdx == 0 ? kPlayer : kEnemy, front + north * across + (sideIdx == 0 ? west : east) * back,
                  sideIdx == 0 ? east : west, false);
            ++k2;
        }
    }
}

std::vector<ArmyResult> Battle::results() const {
    std::vector<ArmyResult> out;
    for (const Squad& s : squads_) {
        if (!s.armyId) continue;
        const Standing st = standing(s);
        ArmyResult r;
        r.id = s.armyId;
        r.soldiers = aliveCount(s);
        r.kills = st.kills;
        r.rank = s.type == UnitType::Hero ? st.level - 1 : st.rank;
        out.push_back(r);
    }
    return out;
}

Standing Battle::standing(const Squad& s) const {
    Standing st;
    int alive = 0, ranks = 0, bless = 0, best = 0;
    for (sim::EntityId id : s.members) {
        const sim::Unit* u = world_->findUnit(id);
        st.kills += u->kills;
        if (!u->alive()) continue;
        ++alive;
        st.broken += u->routed;
        ranks += u->rank;
        bless = std::max(bless, u->blessings);
        best = std::max(best, u->rank);
    }
    st.blessings = bless;
    if (s.type == UnitType::Hero) {
        st.level = best + 1;
        st.strength = std::min(9, sim::warlordsStrength(s.type) + best + bless);
        return st;
    }
    st.rank = alive > 0 ? (ranks * 2 + alive) / (alive * 2) : 0;  // rounded average
    st.strength = std::min(9, sim::warlordsStrength(s.type) + bless);  // a rank is a small edge, not strength (edges.h)
    return st;
}

void Battle::setUpFord() {
    const V3 east{1, 0, 0}, west{-1, 0, 0};
    // The armies face each other across the river, either side of the ford.
    const float cx = kWorldSize / 2, cy = kWorldSize / 2;
    const float px = cx - 60, ex = cx + 170;

    // Oakhaven holds the berm above the ford: shield wall in front, archers and
    // grenadiers behind, healers at the rear, horse on the flanks, fliers behind.
    spawnSquad("Oakhaven Warriors", kPlayer, UnitType::Warrior, 12, {px, cy - 12, 0}, east);
    spawnSquad("The Grey Watch", kPlayer, UnitType::HeavyInfantry, 12, {px, cy + 12, 0}, east);
    spawnSquad("Ferry Pikes", kPlayer, UnitType::Pikemen, 12, {px, cy + 36, 0}, east);
    spawnSquad("Iron Hill Legion", kPlayer, UnitType::Dwarves, 10, {px, cy - 36, 0}, east);
    spawnSquad("Wildmen of Kell", kPlayer, UnitType::Berserker, 10, {px + 6, cy - 58, 0}, east);
    spawnSquad("1st Oakhaven Bowmen", kPlayer, UnitType::Archers, 12, {px - 18, cy - 14, 0}, east);
    spawnSquad("Silverwood Archers", kPlayer, UnitType::Archers, 12, {px - 18, cy + 14, 0}, east);
    spawnSquad("Deepdelve Sappers", kPlayer, UnitType::DwarfGrenadier, 6, {px - 10, cy, 0}, east);
    spawnSquad("Brothers of the Root", kPlayer, UnitType::Journeyman, 4, {px - 28, cy, 0}, east);
    spawnSquad("Magi of the Tower", kPlayer, UnitType::Wizards, 3, {px - 26, cy + 30, 0}, east);
    spawnSquad("Lord Aldric", kPlayer, UnitType::Hero, 1, {px - 8, cy + 2, 0}, east);
    spawnSquad("Oakhaven Lancers", kPlayer, UnitType::HeavyCavalry, 8, {px - 6, cy - 82, 0}, east);
    spawnSquad("Ferry Riders", kPlayer, UnitType::LightCavalry, 8, {px - 6, cy + 64, 0}, east);
    spawnSquad("Moonwood Unicorns", kPlayer, UnitType::Unicorns, 4, {px - 14, cy + 84, 0}, east);
    spawnSquad("War Elephants of Tal", kPlayer, UnitType::Elephants, 2, {px - 30, cy - 50, 0}, east);
    spawnSquad("Sky Lancers", kPlayer, UnitType::Pegasi, 4, {px - 34, cy + 50, 0}, east);
    spawnSquad("Eyrie Griffins", kPlayer, UnitType::Griffins, 3, {px - 34, cy - 24, 0}, east);
    spawnSquad("Host of Archons", kPlayer, UnitType::Archons, 3, {px - 40, cy + 6, 0}, east);

    // The Red Hand comes over the eastern hills.
    spawnSquad("Red Hand Warband", kEnemy, UnitType::LightInfantry, 16, {ex, cy - 20, 0}, west);
    spawnSquad("Barrow Dead", kEnemy, UnitType::Undead, 16, {ex, cy + 20, 0}, west);
    spawnSquad("Blood Berserks", kEnemy, UnitType::Berserker, 8, {ex - 6, cy + 48, 0}, west);
    spawnSquad("Wolf Riders of Gnarl", kEnemy, UnitType::WolfRiders, 8, {ex - 4, cy - 66, 0}, west);
    spawnSquad("Steppe Riders", kEnemy, UnitType::LightCavalry, 6, {ex - 4, cy + 76, 0}, west);
    spawnSquad("Hill Giants", kEnemy, UnitType::Giants, 3, {ex + 14, cy - 44, 0}, west);
    spawnSquad("Minotaurs of the Maze", kEnemy, UnitType::Minotaurs, 4, {ex + 14, cy + 2, 0}, west);
    spawnSquad("Brood of Mirkwold", kEnemy, UnitType::Spiders, 6, {ex + 10, cy + 34, 0}, west);
    spawnSquad("Pit Devils", kEnemy, UnitType::Devils, 3, {ex + 24, cy - 16, 0}, west);
    spawnSquad("Demons of the Rift", kEnemy, UnitType::Demons, 2, {ex + 24, cy + 18, 0}, west);
    spawnSquad("Red Hand Longbows", kEnemy, UnitType::Archers, 12, {ex + 30, cy, 0}, west);
    spawnSquad("Gnarl Bombers", kEnemy, UnitType::DwarfGrenadier, 4, {ex + 20, cy - 4, 0}, west);
    spawnSquad("Witch-Kings", kEnemy, UnitType::Wizards, 2, {ex + 36, cy + 26, 0}, west);
    spawnSquad("Red Hand Menders", kEnemy, UnitType::Journeyman, 3, {ex + 40, cy - 30, 0}, west);
    spawnSquad("Ashen Wyrm", kEnemy, UnitType::Dragons, 1, {ex + 60, cy, 0}, west);
    spawnSquad("Red Hand Mangonel", kEnemy, UnitType::Catapult, 1, {ex + 48, cy - 12, 0}, west);

}

const sim::Continent::City& siegeTown(const sim::Continent& c) {
    // The neutral town nearest Oakhaven Ford.
    const sim::Vec3 ford = c.crossings().front().pos;
    const sim::Continent::City* best = nullptr;
    for (const sim::Continent::City& city : c.cities()) {
        if (city.capital) continue;
        if (!best || (city.pos - ford).flat().lengthSq() < (best->pos - ford).flat().lengthSq()) best = &city;
    }
    return *best;
}

void Battle::setUpSiege() {
    // A neutral town's own garrison: few, but behind walls. Archers line the
    // walk, pikes hold the gate, militia wait in the courtyard.
    const sim::Continent::City& town = siegeTown(*continent_);
    foeName_ = "Garrison of " + town.name;
    const sim::Continent::Castle& k = town.castle;
    const V3 o = origin();
    const auto local = [&](const sim::Vec3& p) { return toV3(p) - V3{o.x, o.y, 0}; };
    const sim::Continent::Castle::Ring& ring = k.rings.front();
    const size_t nc = ring.corners.size();
    const V3 centre = local(town.pos);
    const V3 ga = local(ring.corners[ring.gateEdge]), gb = local(ring.corners[(ring.gateEdge + 1) % nc]);
    const V3 gate = (ga + gb) * 0.5f;
    V3 out = V3{gate.x - centre.x, gate.y - centre.y, 0};
    out = out * (1.0f / out.length());

    // Archers spread round the walk, one to an embrasure, a pair to a wall.
    {
        Squad s{"Town Watch", kEnemy, UnitType::Archers, {}};
        s.formation = Formation::Loose;
        s.garrison = true;
        const sim::UnitStats stats = sim::unitStats(UnitType::Archers);
        const sim::Rampart* walls = townRampart(*region_, centre);
        for (const sim::Rampart::Embrasure& e : walls ? spreadEmbrasures(*walls, static_cast<int>(nc * 2)) : std::vector<sim::Rampart::Embrasure>{}) {
            const sim::EntityId id = world_->spawnUnit(kEnemy, e.pos, e.out, stats);
            s.members.push_back(id);
            if (squadOfUnit_.size() < id) squadOfUnit_.resize(id, -1);
            squadOfUnit_[id - 1] = static_cast<int>(squads_.size());
        }
        s.post = centre;
        squads_.push_back(std::move(s));
    }
    const auto hold = [&](const std::string& name, UnitType type, int count, V3 at, V3 facing) {
        spawnSquad(name, kEnemy, type, count, at, facing);
        squads_.back().garrison = true;
        squads_.back().post = at;
    };
    hold("Gate Wardens", UnitType::Pikemen, 8, gate - out * 12.0f, out);
    hold("Town Militia", UnitType::LightInfantry, 12, local(k.courtyard), out);
    hold("Castellan's Guard", UnitType::HeavyInfantry, 6, local(k.keep) + out * (static_cast<float>(k.keepHalf.toDouble()) + 8), out);

    // The besiegers form up on the road before the gate.
    const V3 camp = gate + out * 110.0f, in = out * -1.0f;
    const V3 side{out.y, -out.x, 0};
    spawnSquad("Oakhaven Warriors", kPlayer, UnitType::Warrior, 12, camp + side * -14.0f, in);
    spawnSquad("The Grey Watch", kPlayer, UnitType::HeavyInfantry, 12, camp + side * 14.0f, in);
    spawnSquad("Iron Hill Legion", kPlayer, UnitType::Dwarves, 10, camp + side * 40.0f, in);
    spawnSquad("1st Oakhaven Bowmen", kPlayer, UnitType::Archers, 12, camp + out * 18.0f + side * -16.0f, in);
    spawnSquad("Silverwood Archers", kPlayer, UnitType::Archers, 12, camp + out * 18.0f + side * 16.0f, in);
    spawnSquad("Deepdelve Sappers", kPlayer, UnitType::DwarfGrenadier, 6, camp + out * 10.0f, in);
    spawnSquad("Brothers of the Root", kPlayer, UnitType::Journeyman, 4, camp + out * 30.0f, in);
    spawnSquad("Lord Aldric", kPlayer, UnitType::Hero, 1, camp + out * 6.0f, in);
    spawnSquad("Eyrie Griffins", kPlayer, UnitType::Griffins, 3, camp + out * 36.0f + side * -30.0f, in);
    // Two mangonels well back, within a long throw of the gate.
    spawnSquad("Oakhaven Siege Train", kPlayer, UnitType::Catapult, 2, camp + out * 26.0f + side * 34.0f, in);
}

void Battle::breakGate(int gate) {
    if (gate >= 0) world_->setGateHp(static_cast<size_t>(gate), 0);
}

void Battle::testShootAt(const V3& at) {
    for (const Squad& s : squads_)
        if (s.owner == kPlayer && s.type == UnitType::Archers)
            for (sim::EntityId id : s.members) issue(kPlayer, id, sim::Command::Type::AttackGround, at);
}

void Battle::testMove(sim::EntityId unit, const V3& to) {
    if (const sim::Unit* u = world_->findUnit(unit)) issue(u->owner, unit, sim::Command::Type::Move, to);
}

void Battle::holdGates(sim::PlayerId holder) {
    for (size_t i = 0; i < world_->gates().size(); ++i) world_->setGateHolder(i, holder);
}

int Battle::gateAt(const Camera& camera, float x, float y, float pixelScale) const {
    int best = -1;
    float bestD2 = kSquadTapRadiusPx * pixelScale * kSquadTapRadiusPx * pixelScale;
    const auto& gates = region_->gates();
    for (size_t i = 0; i < gates.size(); ++i) {
        if (world_->gates()[i].broken()) continue;
        V3 c = toV3(gates[i].a + gates[i].b) * 0.5f;
        c.z = (c.z + static_cast<float>(gates[i].top.toDouble())) * 0.5f;
        float px, py;
        if (!camera.project(c, px, py)) continue;
        const float d2 = (px - x) * (px - x) + (py - y) * (py - y);
        if (d2 < bestD2) {
            bestD2 = d2;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void Battle::aimEngines(const Squad& s) {
    // A town the enemy besieges: batter its gate. Otherwise they throw at
    // whoever comes within reach on their own (the wall-walks included).
    const V3 c = squadCentre(s, false);
    int best = -1;
    float bestD = 1e30f;
    for (size_t i = 0; i < region_->gates().size(); ++i) {
        const sim::GateState& gs = world_->gates()[i];
        if (gs.broken() || gs.holder != kPlayer) continue;
        const V3 g = toV3(region_->gates()[i].a + region_->gates()[i].b) * 0.5f;
        const float d = V3{g.x - c.x, g.y - c.y, 0}.length();
        if (d < bestD) {
            bestD = d;
            best = static_cast<int>(i);
        }
    }
    if (best < 0) return;
    // Already at it.
    for (sim::EntityId id : s.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.alive() && u.attackGate != best) {
            attackGate(s, best);
            break;
        }
    }
}

void Battle::spawnSquad(const std::string& name, sim::PlayerId owner, UnitType type, int count, V3 centre,
                        V3 facing) {
    Squad s{name, owner, type, {}};
    s.formation = defaultFormation(type);
    s.facing = facing;
    const sim::UnitStats stats = sim::unitStats(type);
    const float spacing = formationSpacing(type);
    const std::vector<V3> slots = formationSlots(s.formation, count, spacing);
    const V3 right{facing.y, -facing.x, 0};
    for (int i = 0; i < count; ++i) {
        const V3 p = centre + right * slots[static_cast<size_t>(i)].x + facing * slots[static_cast<size_t>(i)].y;
        const sim::EntityId id = world_->spawnUnit(owner, toSim(p), toSim(facing), stats);
        s.members.push_back(id);
        if (squadOfUnit_.size() < id) squadOfUnit_.resize(id, -1);
        squadOfUnit_[id - 1] = static_cast<int>(squads_.size());
    }
    squads_.push_back(std::move(s));
}

void FarGrid::fill(const sim::Continent& c, const V3& origin, V3 localMin, int nxIn, int nyIn, float cellIn) {
    reset(localMin, nxIn, nyIn, cellIn);
    fillRows(c, origin, 0, ny);
}

void FarGrid::reset(V3 localMin, int nxIn, int nyIn, float cellIn) {
    cell = cellIn;
    nx = nxIn;
    ny = nyIn;
    min = localMin;
    height.assign(static_cast<size_t>(nx) * ny, 0.0f);
    ground.assign(height.size(), 0);
    depth.assign(height.size(), 0.0f);
}

void FarGrid::fillRows(const sim::Continent& c, const V3& origin, int j0, int j1) {
    for (int j = std::max(0, j0); j < std::min(ny, j1); ++j) {
        for (int i = 0; i < nx; ++i) {
            // Whole metres in continent coordinates (cells and origins are whole).
            const sim::Fixed x = sim::Fixed::fromInt(static_cast<int64_t>(origin.x + min.x + i * cell));
            const sim::Fixed y = sim::Fixed::fromInt(static_cast<int64_t>(origin.y + min.y + j * cell));
            const sim::Continent::Sample smp = c.sample(x, y);
            sim::Fixed surface;
            const size_t k = static_cast<size_t>(j) * nx + i;
            if (c.waterSurfaceOf(smp, surface)) {
                height[k] = static_cast<float>(surface.toDouble());
                ground[k] = static_cast<uint8_t>(sim::Ground::DeepWater);
                depth[k] = static_cast<float>((surface - smp.height).toDouble());
            } else {
                height[k] = static_cast<float>(smp.height.toDouble());
                ground[k] = static_cast<uint8_t>(c.landGround(x, y, smp.height, smp.mountain, smp.roadDist));
            }
        }
    }
}

float FarGrid::heightAt(float x, float y) const {
    const float fx = std::clamp((x - min.x) / cell, 0.0f, static_cast<float>(nx - 1) - 0.001f);
    const float fy = std::clamp((y - min.y) / cell, 0.0f, static_cast<float>(ny - 1) - 0.001f);
    const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
    const float tx = fx - ix, ty = fy - iy;
    const auto h = [&](int i, int j) { return height[static_cast<size_t>(j) * nx + i]; };
    const float top = h(ix, iy) + (h(ix + 1, iy) - h(ix, iy)) * tx;
    const float bottom = h(ix, iy + 1) + (h(ix + 1, iy + 1) - h(ix, iy + 1)) * tx;
    return top + (bottom - top) * ty;
}

float Battle::heightAt(float x, float y) const {
    if (!inBattlefield(x, y)) return near_.contains(x, y) ? near_.heightAt(x, y) : far_.heightAt(x, y);
    const int n = kTerrainCells;
    const float fx = std::clamp(x / kCellSize, 0.0f, static_cast<float>(n));
    const float fy = std::clamp(y / kCellSize, 0.0f, static_cast<float>(n));
    const int ix = std::min(static_cast<int>(fx), n - 1), iy = std::min(static_cast<int>(fy), n - 1);
    const float tx = fx - ix, ty = fy - iy;
    const auto h = [&](int i, int j) { return heights_[static_cast<size_t>(j) * (n + 1) + i]; };
    const float a = h(ix, iy), b = h(ix + 1, iy), c = h(ix, iy + 1), d = h(ix + 1, iy + 1);
    // Same two-triangle split as the simulation and the terrain mesh.
    if (tx >= ty) return a + (b - a) * tx + (d - b) * ty;
    return a + (c - a) * ty + (d - c) * tx;
}

void Battle::advance(double seconds) {
    if (paused_) return;
    const double scaled = std::min(seconds, 0.25) * speed_;
    accumulator_ += scaled;
    int ticks = 0;
    while (accumulator_ >= kTickSeconds && ticks < kMaxTicksPerFrame) {
        step();
        accumulator_ -= kTickSeconds;
        ++ticks;
    }
    // Drop time we could not catch up on rather than spiralling.
    if (accumulator_ >= kTickSeconds) accumulator_ = 0;
    updateParticles(static_cast<float>(scaled));
}

void Battle::step() {
    const auto& units = world_->units();
    for (size_t i = 0; i < units.size(); ++i) prevUnitPos_[i] = toV3(units[i].pos);
    prevProjectilePos_.clear();
    for (const sim::Projectile& p : world_->projectiles()) prevProjectilePos_.push_back({p.id, toV3(p.pos)});

    if (world_->tick() % kAiIntervalTicks == 0) runEnemyAi();

    // Who strikes or looses this tick, for the attack animation.
    readied_.resize(units.size());
    for (size_t i = 0; i < units.size(); ++i) readied_[i] = {units[i].meleeCooldown, units[i].reloadRemaining};
    const auto start = std::chrono::steady_clock::now();
    world_->step(pending_);
    for (size_t i = 0; i < units.size() && i < readied_.size(); ++i)
        if (units[i].meleeCooldown > readied_[i].first || units[i].reloadRemaining > readied_[i].second) attackTick_[i] = world_->tick();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stepMs_ = stepMs_ * 0.95 + ms * 0.05;
    pending_.clear();
    capture_.step(*world_);  // battle_capture.cpp
    collectEvents();
    voice_.observe(*world_, outcome());
    presentDeaths();
}

std::vector<uint32_t> Battle::takeDirtyRemains() {
    std::vector<uint32_t> out;
    out.swap(dirtyRemains_);
    for (uint32_t slot : out) dirtyFlag_[slot] = 0;
    return out;
}

void Battle::addMark(GroundMark::Kind kind, V3 pos, float yaw, float size, uint32_t seed) {
    // Water carries blood and scorch marks away.
    const sim::Ground g = region_->groundAt(toFixed(pos.x), toFixed(pos.y));
    if (g == sim::Ground::Shallows || g == sim::Ground::DeepWater) return;
    // On the ground, or on the deck it happened above (a wall-walk, a bridge).
    float z = heightAt(pos.x, pos.y);
    sim::Fixed deck;
    if (pos.z > z + 1.0f && region_->deckBelow(toFixed(pos.x), toFixed(pos.y), toFixed(pos.z + 0.5f), deck))
        z = std::max(z, static_cast<float>(deck.toDouble()));
    pos.z = z;
    const GroundMark m{kind, pos, yaw, size, world_->tick(), seed};
    if (marks_.size() < kMaxGroundMarks) {
        marks_.push_back(m);
    } else {
        // Prototype cap; the full game consolidates old marks instead (§5.5).
        marks_[marksAdded_ % kMaxGroundMarks] = m;
    }
    ++marksAdded_;
}

void Battle::emit(Particle::Kind kind, V3 pos, V3 vel, float life, float size) {
    if (particles_.size() >= kMaxParticles) return;
    particles_.push_back({kind, pos, vel, 0.0f, life, size});
}

void Battle::collectEvents() {
    for (uint32_t slot : world_->changedRemains()) {
        if (!dirtyFlag_[slot]) {
            dirtyFlag_[slot] = 1;
            dirtyRemains_.push_back(slot);
        }
    }

    const uint32_t tick = world_->tick();
    const auto rnd = [&] { return unit01(fxSeed_ = mix(fxSeed_, tick)); };
    const auto srnd = [&] { return rnd() * 2.0f - 1.0f; };

    std::erase_if(recent_, [&](const Recent& r) { return tick - r.tick > 4 * sim::kTicksPerSecond; });
    for (const sim::ImpactEvent& e : world_->impacts()) {
        const V3 pos = toV3(e.pos);
        if (e.kind != sim::ImpactEvent::Kind::Ground && e.kind != sim::ImpactEvent::Kind::Bleed && recent_.size() < 600) {
            recent_.push_back({pos, tick, e.kind == sim::ImpactEvent::Kind::Explosion ? 8.0f : (e.killed ? 3.0f : 1.0f)});
        }
        const V3 dir = toV3(e.dir).normalized();
        const uint32_t seed = mix(e.projectile ? e.projectile : e.hitUnit, tick);
        using K = sim::ImpactEvent::Kind;
        switch (e.kind) {
            case K::Ground:
                for (int i = 0; i < 2; ++i) emit(Particle::Kind::Dirt, pos, {srnd(), srnd(), 1.5f + rnd()}, 0.5f, 0.06f);
                break;
            case K::Bleed: {
                // A flung part struck or skidded here: a splat, and a few
                // droplets thrown on ahead.
                const float size = static_cast<float>(e.radius.toDouble());
                const float yaw = std::atan2(dir.y, dir.x);
                addMark(GroundMark::Kind::Blood, pos, yaw, size, seed);
                if (size > 0.3f) {
                    for (int i = 0; i < 2; ++i) {
                        const uint32_t s = mix(seed, static_cast<uint32_t>(i + 1));
                        const V3 p = pos + V3{dir.x, dir.y, 0} * (0.4f + unit01(s) * 0.8f) + V3{-dir.y, dir.x, 0} * (unit01(s >> 8) - 0.5f) * 0.5f;
                        addMark(GroundMark::Kind::Blood, p, yaw, size * 0.35f, s);
                    }
                }
                break;
            }
            case K::Blocked:
                for (int i = 0; i < 6; ++i) emit(Particle::Kind::Spark, pos, V3{srnd() * 3, srnd() * 3, 1 + rnd() * 3} - dir * 2, 0.35f, 0.05f);
                break;
            case K::Heal:
                for (int i = 0; i < 14; ++i) emit(Particle::Kind::Heal, pos + V3{srnd() * 0.5f, srnd() * 0.5f, srnd()}, {0, 0, 0.8f + rnd()}, 1.2f, 0.12f);
                break;
            case K::Explosion: {
                const float r = static_cast<float>(e.radius.toDouble());
                addMark(GroundMark::Kind::Crater, pos, rnd() * 6.28f, r * 0.95f, seed);
                emit(Particle::Kind::Flash, pos + V3{0, 0, 0.5f}, {}, 0.18f, r * 1.3f);
                for (int i = 0; i < 18; ++i) emit(Particle::Kind::Fire, pos + V3{0, 0, 0.3f}, V3{srnd(), srnd(), rnd() * 0.8f} * (r * 2.5f), 0.45f + rnd() * 0.3f, 0.5f + rnd() * 0.6f);
                for (int i = 0; i < 14; ++i) emit(Particle::Kind::Smoke, pos + V3{srnd(), srnd(), 0.5f}, {srnd() * 1.5f, srnd() * 1.5f, 1.5f + rnd() * 2}, 2.5f + rnd() * 2, 1.0f + rnd());
                for (int i = 0; i < 24; ++i) emit(Particle::Kind::Dirt, pos, V3{srnd() * 6, srnd() * 6, 5 + rnd() * 9}, 1.6f, 0.08f + rnd() * 0.1f);
                break;
            }
            case K::Smash: {
                // A stone shatters: a scar in the ground, a burst of dust and chips.
                const float r = static_cast<float>(e.radius.toDouble());
                addMark(GroundMark::Kind::Crater, pos, rnd() * 6.28f, r * 0.5f, seed);
                for (int i = 0; i < 10; ++i) emit(Particle::Kind::Smoke, pos + V3{srnd(), srnd(), 0.3f}, {srnd() * 2, srnd() * 2, 0.8f + rnd()}, 1.8f + rnd(), 0.8f + rnd() * 0.6f);
                for (int i = 0; i < 20; ++i) emit(Particle::Kind::Dirt, pos, V3{srnd() * 5, srnd() * 5, 3 + rnd() * 6}, 1.4f, 0.07f + rnd() * 0.1f);
                break;
            }
            case K::Gate: {
                // Splinters off the doors; when they give, a cloud of them.
                const int chips = e.killed ? 40 : 4;
                for (int i = 0; i < chips; ++i) emit(Particle::Kind::Dirt, pos, V3{srnd() * 3, srnd() * 3, 1.5f + rnd() * 3} - dir * 2, 1.0f, 0.06f + rnd() * 0.08f);
                if (e.killed)
                    for (int i = 0; i < 16; ++i) emit(Particle::Kind::Smoke, pos + V3{srnd() * 3, srnd() * 3, srnd()}, {srnd(), srnd(), 1 + rnd()}, 3.0f + rnd() * 2, 1.2f + rnd());
                break;
            }
            case K::Hit: {
                if (e.hitUnit == sim::kNoEntity) break;
                const size_t index = e.hitUnit - 1;
                const sim::Unit& victim = world_->units()[index];
                if (e.killed) deathTick_[index] = tick;
                else hitTick_[index] = tick;
                const V3 body = toV3(victim.pos);
                const float scale = static_cast<float>(victim.stats.height.toDouble()) / 1.8f;
                const int splats = e.killed ? 3 : 1;
                for (int i = 0; i < splats; ++i) {
                    const uint32_t s = mix(seed, static_cast<uint32_t>(i));
                    const float throwDist = (0.3f + unit01(s) * (e.killed ? 1.6f : 1.0f)) * scale;
                    const float side = (unit01(s >> 8) - 0.5f) * 0.8f * scale;
                    const V3 p = body + V3{dir.x, dir.y, 0} * throwDist + V3{-dir.y, dir.x, 0} * side;
                    const float size = ((e.killed && i == 0) ? 1.3f : 0.45f + unit01(s >> 16) * 0.5f) * scale;
                    addMark(victim.stats.bleeds ? GroundMark::Kind::Blood : GroundMark::Kind::Dust, p,
                            unit01(s >> 4) * 6.28f, size, s);
                }
                break;
            }
        }
    }

    // Trails: fireballs burn, satchel fuses fizz.
    for (const sim::Projectile& p : world_->projectiles()) {
        if (p.kind == sim::ProjectileKind::Fireball) {
            emit(Particle::Kind::Fire, toV3(p.pos), V3{srnd(), srnd(), srnd()} * 0.6f, 0.35f, 0.45f);
        } else if (p.kind == sim::ProjectileKind::Bomb && (tick & 1)) {
            emit(Particle::Kind::Spark, toV3(p.pos) + V3{0, 0, 0.25f}, {srnd(), srnd(), 1.5f}, 0.25f, 0.04f);
        }
    }
}

bool Battle::hotSpot(V3& at) const {
    float best = 0;
    for (const Recent& a : recent_) {
        float score = 0;
        for (const Recent& b : recent_) {
            const V3 d = b.pos - a.pos;
            if (d.x * d.x + d.y * d.y < 25.0f * 25.0f) score += b.weight;
        }
        if (score > best) {
            best = score;
            at = a.pos;
        }
    }
    return best > 0;
}

void Battle::updateParticles(float seconds) {
    if (seconds <= 0) return;
    for (Particle& p : particles_) {
        p.age += seconds;
        switch (p.kind) {
            case Particle::Kind::Dirt:
            case Particle::Kind::Spark: {
                p.vel.z -= kGravity * seconds;
                p.pos += p.vel * seconds;
                const float g = heightAt(p.pos.x, p.pos.y);
                if (p.pos.z < g) {
                    p.pos.z = g;
                    p.vel = p.vel * 0.3f;
                    p.vel.z = std::abs(p.vel.z);
                }
                break;
            }
            case Particle::Kind::Fire:
                p.vel = p.vel * std::exp(-4.0f * seconds);
                p.vel.z += 3.0f * seconds;
                p.pos += p.vel * seconds;
                break;
            case Particle::Kind::Smoke:
                p.vel = p.vel * std::exp(-0.8f * seconds);
                p.pos += p.vel * seconds;
                p.size += seconds * 1.2f;
                break;
            case Particle::Kind::Heal:
            case Particle::Kind::Flash:
                p.pos += p.vel * seconds;
                break;
        }
    }
    std::erase_if(particles_, [](const Particle& p) { return p.age >= p.life; });
}

void Battle::issue(sim::PlayerId player, sim::EntityId unit, sim::Command::Type type, const V3& target) {
    pending_.push_back({player, unit, type, toSim(target)});
}

int Battle::aliveCount(const Squad& s) const {
    int n = 0;
    for (sim::EntityId id : s.members) n += world_->units()[id - 1].alive();
    return n;
}

V3 Battle::squadCentre(const Squad& s, bool interpolated) const {
    const auto pos = [&](size_t i) { return interpolated ? unitPosition(i) : toV3(world_->units()[i].pos); };
    V3 sum;
    int n = 0;
    for (sim::EntityId id : s.members) {
        if (!world_->units()[id - 1].alive()) continue;
        sum += pos(id - 1);
        ++n;
    }
    if (n == 0) return pos(s.members.front() - 1);
    return sum * (1.0f / static_cast<float>(n));
}

void Battle::steerSquad(const Squad& squad, const V3& to) {
    if (squad.hasGoal) {
        const V3 c = squadCentre(squad, false);
        bool moving = false;
        for (sim::EntityId id : squad.members) moving = moving || (world_->units()[id - 1].alive() && world_->units()[id - 1].hasMoveTarget);
        if (moving && (to - squad.goal).length() < std::max(6.0f, (to - c).length() * 0.15f)) return;
    }
    moveSquad(squad, to);
}

bool Battle::flankPoint(const Squad& riders, V3& out) const {
    // Once in the melee themselves, they fight it out.
    for (sim::EntityId id : riders.members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.alive() && (u.engaged || u.foe != sim::kNoEntity)) return false;
    }
    // The nearest foe within 120 m that is fighting someone else.
    const V3 c = squadCentre(riders, false);
    const Squad* target = nullptr;
    float best = 120.0f;
    for (const Squad& t : squads_) {
        if (t.owner == riders.owner || aliveCount(t) == 0) continue;
        bool engaged = false;
        V3 facing;
        for (sim::EntityId id : t.members) {
            const sim::Unit& u = world_->units()[id - 1];
            if (!u.alive()) continue;
            engaged = engaged || u.engaged || u.foe != sim::kNoEntity;
            facing = facing + V3{static_cast<float>(u.facing.x.toDouble()), static_cast<float>(u.facing.y.toDouble()), 0};
        }
        const float d = (squadCentre(t, false) - c).length();
        if (engaged && facing.length() > 0.1f && d < best) {
            best = d;
            target = &t;
        }
    }
    if (target == nullptr) return false;
    V3 facing;
    for (sim::EntityId id : target->members) {
        const sim::Unit& u = world_->units()[id - 1];
        if (u.alive()) facing = facing + V3{static_cast<float>(u.facing.x.toDouble()), static_cast<float>(u.facing.y.toDouble()), 0};
    }
    facing = facing.normalized();
    const V3 tc = squadCentre(*target, false);
    const V3 rel = c - tc;
    const V3 side{-facing.y, facing.x, 0};
    const float ahead = rel.dot(facing);
    if (ahead > -8.0f) {
        // Still in front or beside it: swing wide round the nearer flank first.
        out = tc + side * (rel.dot(side) >= 0 ? 28.0f : -28.0f) - facing * 10.0f;
        if ((out - c).length() > 12.0f) return true;
    }
    // Behind it: charge home into its back; close in, they fight on their own
    // (stopped, they pick the nearest foe, at its back, and ride it down).
    if (rel.length() < 18.0f) return false;
    out = tc - facing * 4.0f;
    return true;
}

void Battle::manTheWalls(const Squad& s) {
    // Archers below or behind a merlon step up to the embrasures left empty
    // (by the dead, or never manned): one archer to each.
    const sim::Rampart* walls = townRampart(*region_, s.post);
    if (!walls || walls->embrasures.empty()) return;
    const auto& units = world_->units();
    std::vector<uint8_t> held(walls->embrasures.size(), 0);
    for (const sim::Unit& u : units) {
        if (!u.alive() || u.owner != s.owner) continue;
        const V3 p = toV3(u.hasMoveTarget ? u.moveTarget : u.pos);
        for (size_t k = 0; k < held.size(); ++k) {
            const V3 e = toV3(walls->embrasures[k].pos);
            if ((p.x - e.x) * (p.x - e.x) + (p.y - e.y) * (p.y - e.y) < kEmbrasureHeld * kEmbrasureHeld && std::fabs(p.z - e.z) < 1.5f) held[k] = 1;
        }
    }
    for (sim::EntityId id : s.members) {
        const sim::Unit* u = world_->findUnit(id);
        if (!u || !u->alive() || u->hasMoveTarget || u->routed) continue;
        const V3 p = toV3(u->pos);
        size_t best = held.size();
        float bestD = 1e30f;
        for (size_t k = 0; k < held.size(); ++k) {
            const V3 e = toV3(walls->embrasures[k].pos);
            const float d = (p.x - e.x) * (p.x - e.x) + (p.y - e.y) * (p.y - e.y);
            if (d < kEmbrasureHeld * kEmbrasureHeld && std::fabs(p.z - e.z) < 1.5f) {
                best = held.size();  // at one already
                break;
            }
            if (!held[k] && d < bestD) {
                bestD = d;
                best = k;
            }
        }
        if (best == held.size()) continue;
        held[best] = 1;
        issue(s.owner, id, sim::Command::Type::Move, toV3(walls->embrasures[best].pos));
    }
}

void Battle::runEnemyAi() {
    // Depends only on simulation state (never on interpolated positions), so in
    // multiplayer the relay or every peer can run it and issue identical
    // commands. TODO(multiplayer): move to fixed point before lockstep play.
    const auto nearest = [&](const V3& from, sim::PlayerId owner, float& bestDist) {
        bestDist = 1e30f;
        V3 goal;
        for (const Squad& t : squads_) {
            if (t.owner != owner || aliveCount(t) == 0) continue;
            const V3 tc = squadCentre(t, false);
            const float d = (tc - from).length();
            if (d < bestDist) {
                bestDist = d;
                goal = tc;
            }
        }
        return goal;
    };
    const auto stop = [&](const Squad& s) {
        s.hasGoal = false;
        for (sim::EntityId id : s.members) issue(kEnemy, id, sim::Command::Type::Stop, {});
    };

    for (const Squad& s : squads_) {
        if (s.owner != kEnemy || aliveCount(s) == 0 || standing(s).broken == aliveCount(s)) continue;  // routed: beyond orders
        const V3 c = squadCentre(s, false);
        float dist;
        const Role role = roleOf(s.type);
        if (role == Role::Healer) {
            // Stay a little behind the nearest friendly fighting squad.
            float friendDist = 1e30f;
            V3 friendPos;
            for (const Squad& f : squads_) {
                if (f.owner != kEnemy || &f == &s || aliveCount(f) == 0 || roleOf(f.type) == Role::Healer) continue;
                const V3 fc = squadCentre(f, false);
                const float d = (fc - c).length();
                if (d < friendDist) {
                    friendDist = d;
                    friendPos = fc;
                }
            }
            if (friendDist < 1e30f && friendDist > 15.0f) steerSquad(s, friendPos + V3{10, 0, 0});
            continue;
        }
        const V3 goal = nearest(c, kPlayer, dist);
        if (dist == 1e30f) return;
        if (role == Role::Siege) {
            // Engines never march at the foe; they throw from where they stand.
            if (!s.garrison) aimEngines(s);
            continue;
        }
        if (s.garrison) {
            // Hold the town: archers keep their places on the walk (they shoot
            // on their own); the rest meet whoever gets near their post, and
            // otherwise return to it.
            if (role == Role::Ranged) {
                manTheWalls(s);
                continue;
            }
            float near;
            const V3 threat = nearest(s.post, kPlayer, near);
            if (near < 45.0f) steerSquad(s, threat);
            else if ((c - s.post).length() > 6.0f) steerSquad(s, s.post);
            continue;
        }
        const sim::UnitStats stats = sim::unitStats(s.type);
        const float range = static_cast<float>(stats.ranged.autoRange.toDouble());
        V3 flank;
        if (stats.mounted && role == Role::Melee && flankPoint(s, flank)) {
            // Horse ride round a foe already locked in melee and charge its rear.
            steerSquad(s, flank);
        } else if (range > 0 && role != Role::Flier) {
            const float keep = range * 0.8f;
            if (dist > keep) steerSquad(s, c + (goal - c) * ((dist - keep + 4) / dist));
            else stop(s);
        } else if (dist > 15.0f) {
            steerSquad(s, goal);
        } else {
            stop(s);
        }
    }
}

int Battle::outcome() const {
    bool playerAlive = false, enemyAlive = false;
    for (const sim::Unit& u : world_->units()) {
        if (!u.alive()) continue;
        (u.owner == kPlayer ? playerAlive : enemyAlive) = true;
    }
    if (!enemyAlive) return 1;
    if (!playerAlive) return 2;
    if (capture_.taken()) return capture_.attacker() == kPlayer ? 1 : 2;  // the town's heart is taken
    return 0;
}

V3 Battle::unitPosition(size_t index) const {
    return lerp(prevUnitPos_[index], toV3(world_->units()[index].pos), alpha());
}

bool Battle::unitMoving(size_t index) const {
    const V3 d = toV3(world_->units()[index].pos) - prevUnitPos_[index];
    return d.x * d.x + d.y * d.y > 1e-6f;
}

void Battle::chargeAll() {
    for (Squad& mine : squads_) {
        if (mine.owner != kPlayer || aliveCount(mine) == 0 || roleOf(mine.type) == Role::Siege) continue;
        const V3 from = squadCentre(mine, false);
        const Squad* target = nullptr;
        float best = 1e18f;
        for (const Squad& s : squads_) {
            if (s.owner == kPlayer || aliveCount(s) == 0) continue;
            const V3 d = squadCentre(s, false) - from;
            const float d2 = d.x * d.x + d.y * d.y;
            if (d2 < best) {
                best = d2;
                target = &s;
            }
        }
        if (!target) return;
        V3 at = squadCentre(*target, false);
        if (sim::unitStats(mine.type).ranged.muzzleSpeed > sim::Fixed::zero()) {
            const V3 d = V3{at.x - from.x, at.y - from.y, 0};
            const float len = d.length();
            if (len <= kChargeBowshot) continue;
            at = from + d * ((len - kChargeBowshot) / len);
        }
        moveSquad(mine, at);
    }
    std::vector<sim::EntityId> ours;
    for (const sim::Unit& u : world_->units())
        if (u.owner == kPlayer && u.alive()) ours.push_back(u.id);
    voice_.order(Situation::Charge, *world_, ours);
}

}  // namespace illyria::game
