#include "illyria/game/battle.h"

#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>

#include "illyria/game/mode.h"
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

// Where a man may stand when a battle starts: on dry, open ground the routes
// cross (World::standable), and inside no town's walls.
bool openFor(const sim::Region& rg, const sim::World& w, V3 p) {
    const sim::Vec3 q = toSim(p);
    if (rg.underWater(q.x, q.y) || !w.standable(q)) return false;
    for (const sim::Rampart& rp : rg.ramparts())
        if (sim::Gate::inside(rp, q)) return false;
    return true;
}

// Open ground before a town's gate for the besiegers to form up on: on the
// way out from the gate, about `want` metres off, a stretch 30 m round clear
// of every town's walls (the towns stand close together since the map shrank
// to a third), on the battlefield. The gate's way out at `want` if none is.
V3 conqBefore(const sim::Region& rg, const sim::World& w, V3 gate, V3 out, float want) {
    const V3 side{out.y, -out.x, 0};
    const float edge = Battle::kWorldSize - 60.0f;
    for (int ring = 0; ring <= 10; ++ring)
        for (int across = 0; across <= 6; ++across) {
            const float along = want + static_cast<float>((ring + 1) / 2 * (ring % 2 ? 10 : -10));
            const float off = static_cast<float>((across + 1) / 2 * (across % 2 ? 15 : -15));
            if (along < want * 0.5f) continue;
            const V3 c = gate + out * along + side * off;
            if (c.x < 60 || c.y < 60 || c.x > edge || c.y > edge) continue;
            bool open = true;
            for (int dy = -30; dy <= 30 && open; dy += 10)
                for (int dx = -30; dx <= 30 && open; dx += 10)
                    if (dx * dx + dy * dy <= 900) open = openFor(rg, w, c + V3{static_cast<float>(dx), static_cast<float>(dy), 0});
            if (open) return c;
        }
    return gate + out * want;
}

// The field of the open-ground skirmish: the first place the continent offers (mode.h) that is open grass, not in a forest or a
// swamp and not within a castle's reach (else the first of them, else the ford).
sim::Vec3 fieldCentre(const sim::Continent& c) {
    const auto inArea = [](const std::vector<sim::Continent::Area>& areas, const sim::Vec3& p) {
        for (const sim::Continent::Area& a : areas) {
            const double dx = (p.x - a.centre.x).toDouble() / a.rx.toDouble(), dy = (p.y - a.centre.y).toDouble() / a.ry.toDouble();
            if (dx * dx + dy * dy < 2.5) return true;
        }
        return false;
    };
    // Open: no water, bog or wood anywhere the two hosts stand or between them (the hosts muster 70 m either side of the
    // middle, a hundred metres wide). A river between them would make it the Ford, and one side's map.
    const auto open = [&](const sim::Vec3& p) {
        int rock = 0;
        for (int dy = -112; dy <= 112; dy += 14)
            for (int dx = -154; dx <= 154; dx += 14) {
                const sim::Ground g = c.groundAt(p.x + sim::Fixed::fromInt(dx), p.y + sim::Fixed::fromInt(dy));
                sim::Fixed surface;
                if (g == sim::Ground::Shallows || g == sim::Ground::DeepWater || g == sim::Ground::Swamp || g == sim::Ground::Mud || c.waterSurface(p.x + sim::Fixed::fromInt(dx), p.y + sim::Fixed::fromInt(dy), surface)) return false;
                if (g == sim::Ground::Rock && ++rock > 12) return false;  // (an outcrop or two, no more: 3% of the ground)
            }
        return true;
    };
    const std::vector<Place> places = battlePlaces(c);
    for (const Place& p : places) {
        if (p.kind != Place::Kind::Hill) continue;
        bool near = false;
        for (const sim::Continent::City& city : c.cities()) near = near || (city.pos - p.pos).flat().lengthSq() < sim::Fixed::fromInt(300 * 300);
        if (!near && !inArea(c.forests(), p.pos) && !inArea(c.swamps(), p.pos) && open(p.pos)) return p.pos;
    }
    // No hill is open: the open ground nearest the ford, clear of the towns.
    const sim::Vec3 ford = c.crossings().front().pos;
    sim::Vec3 best = ford;
    sim::Fixed bestD;
    bool found = false;
    for (int64_t y = 400; y < sim::Continent::kHeight - 400; y += 29)
        for (int64_t x = 400; x < sim::Continent::kWidth - 400; x += 29) {
            const sim::Vec3 p{sim::Fixed::fromInt(x), sim::Fixed::fromInt(y), sim::Fixed::zero()};
            const sim::Fixed d = (p - ford).flat().lengthSq();
            if (found && d >= bestD) continue;
            bool near = false;
            for (const sim::Continent::City& city : c.cities()) near = near || (city.pos - p).flat().lengthSq() < sim::Fixed::fromInt(200 * 200);
            if (near || !open(p)) continue;
            best = {p.x, p.y, c.heightAt(p.x, p.y)}, bestD = d, found = true;
        }
    return best;
}

constexpr int kDefaultBudget = 300;  // gold a side raises when the page says nothing
// How far apart two bought hosts form up, front to front (docs/GAME_DESIGN.md §4.5.2): near enough that the first arrows
// fly within about 25 s of the start. On open ground each host's bowmen stand out in front as skirmishers (muster), the
// two lines inside a bowshot (70 m) of each other across the river; at the Ford the far host is a short march off.
// (140 m and 230 m before, the bowmen behind two ranks of foot: 40 to 93 s passed before anyone fell.)
constexpr float kFieldGapM = 64.0f, kFordGapM = 120.0f;

}  // namespace

Role roleOf(UnitType type) {
    switch (type) {
        case UnitType::Archers: return Role::Ranged;
        case UnitType::DwarfGrenadier: return Role::Bomber;
        case UnitType::Wizards: return Role::Caster;
        case UnitType::Journeyman: return Role::Healer;
        case UnitType::Catapult: return Role::Siege;
        case UnitType::Hyena: return Role::Scout;
        case UnitType::Griffins:
        case UnitType::Pegasi:
        case UnitType::Archons:
        case UnitType::Dragons: return Role::Flier;
        default: return Role::Melee;
    }
}

Battle::Battle(uint64_t seed, Scenario scenario, const SkirmishSetup& setup) : scenario_(scenario), defend_(setup.defend) {
    // The battle is fought in a full-detail window of the continent: at Oakhaven Ford, at the neutral town nearest it, at the
    // nearest capital, or on the first hill the continent offers.
    continent_ = std::make_shared<const sim::Continent>(kRegionSeed);
    sim::Vec3 centre = continent_->crossings().front().pos;
    const bool siege = scenario_ == Scenario::Siege || scenario_ == Scenario::Capital;
    const sim::Continent::City* town = nullptr;
    if (scenario_ == Scenario::Siege) town = &siegeTown(*continent_);
    if (scenario_ == Scenario::Capital) town = &capitalTown(*continent_);
    if (town) centre = town->pos;
    if (scenario_ == Scenario::Field) centre = fieldCentre(*continent_);
    build(seed, centre);
    if (siege) setUpSiege(*town, setup);
    else if (scenario_ == Scenario::Field) setUpField(setup);
    else setUpFord(setup);
    if (siege) {
        const sim::PlayerId keeper = setup.defend ? kPlayer : kEnemy;
        placeCapturePoint(*town, keeper);
        holdGates(keeper);
    }
    finish();
}

void Battle::build(uint64_t seed, const sim::Vec3& centre, float farReachM) {
    sim::WorldConfig cfg;
    cfg.seed = seed;
    cfg.wind = {sim::Fixed::fromRatio(1, 2), -sim::Fixed::fromRatio(1, 2), sim::Fixed::zero()};
    cfg.maxRemains = kMaxRemains;
    region_ = std::make_shared<const sim::Region>(sim::Region::fromContinent(*continent_, centre, kTerrainCells));
    // The continent around the battlefield: all of it every 16 m (a match:
    // only within farReachM of the field), and a ring a kilometre wide around
    // the battlefield every 16 m.
    {
        const V3 o = toV3(region_->origin());
        float x0 = -o.x, y0 = -o.y, x1 = static_cast<float>(sim::Continent::kWidth) - o.x,
              y1 = static_cast<float>(sim::Continent::kHeight) - o.y;
        if (farReachM > 0) {
            const float mid = kWorldSize / 2;
            x0 = std::max(x0, mid - farReachM);
            y0 = std::max(y0, mid - farReachM);
            x1 = std::min(x1, mid + farReachM);
            y1 = std::min(y1, mid + farReachM);
        }
        far_.fill(*continent_, o, {x0, y0, 0}, static_cast<int>((x1 - x0) / FarGrid::kCell) + 1,
                  static_cast<int>((y1 - y0) / FarGrid::kCell) + 1, FarGrid::kCell);
        constexpr float kRing = 1024.0f, kNearCell = 16.0f;
        const int n = static_cast<int>((kWorldSize + 2 * kRing) / kNearCell) + 1;
        near_.fill(*continent_, o, {-kRing, -kRing, 0}, n, n, kNearCell);
    }
    heights_.reserve(region_->terrain().samples().size());
    for (sim::Fixed h : region_->terrain().samples()) heights_.push_back(static_cast<float>(h.toDouble()));
    cfg.region = region_;
    ownWorld_ = std::make_unique<sim::World>(cfg);
    world_ = ownWorld_.get();
    dirtyFlag_.assign(kMaxRemains, 0);
}

void Battle::finish() {
    const size_t n = world_->units().size();
    deathTick_.assign(n, UINT32_MAX);
    attackTick_.assign(n, UINT32_MAX);
    hitTick_.assign(n, UINT32_MAX);
    selectedFlag_.assign(n, 0);
    prevUnitPos_.resize(n);
    seenGen_.assign(n, 0);
    for (size_t i = 0; i < n; ++i) prevUnitPos_[i] = toV3(world_->units()[i].pos), seenGen_[i] = world_->generation(world_->units()[i].id);
    startRank_.resize(n);
    for (size_t i = 0; i < n; ++i) startRank_[i] = world_->units()[i].rank;
}

void Battle::seatCarried(const CarriedSquad& c) {
    Squad& s = squads_.back();
    s.total = std::max(c.full, static_cast<int>(s.members.size()));
    for (size_t i = 0; i < s.members.size() && i < c.men.size(); ++i) {
        const CarriedMan& m = c.men[i];
        world_->setVeterancy(s.members[i], m.rank, 0);  // (a hero's levels make him tougher: before his wounds are put back)
        const sim::Unit* u = world_->findUnit(s.members[i]);
        world_->restoreUnit(s.members[i], static_cast<int32_t>(static_cast<int64_t>(u->stats.maxHp) * std::clamp(m.hp, 1, 1000) / 1000), m.kills, sim::kMoraleFull);
    }
}

std::vector<CarriedSquad> Battle::carried() const {
    std::vector<CarriedSquad> out;
    for (const Squad& q : squads_) {
        if (q.owner != local_) continue;
        CarriedSquad c{q.name.empty() ? std::string(sim::unitTypeName(q.type)) : q.name, q.type, totalCount(q), {}};
        for (size_t i = 0; i < q.members.size(); ++i) {
            const sim::Unit* u = world_->findUnit(q.members[i]);
            if (!u || !u->alive()) continue;
            c.men.push_back({std::max(1, static_cast<int>(static_cast<int64_t>(u->hp) * 1000 / std::max(1, u->stats.maxHp))), u->rank, u->kills, static_cast<int>(i)});
        }
        out.push_back(std::move(c));
    }
    return out;
}

Standing Battle::standing(const Squad& s) const {
    if (s.told) {  // (Conquest: the army says, and counts what no unit holds)
        Standing st = s.standing;
        for (sim::EntityId id : s.members) st.broken += world_->units()[id - 1].routed;
        return st;
    }
    Standing st;
    int alive = 0, ranks = 0, bless = 0, best = 0;
    for (sim::EntityId id : s.members) {
        const sim::Unit* u = world_->findUnit(id);
        if (!u) continue;  // (taken out of the world)
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

void Battle::setUpFord(const SkirmishSetup& setup) {
    const V3 east{1, 0, 0}, west{-1, 0, 0};
    // The armies face each other across the river, either side of the ford.
    const float cx = kWorldSize / 2, cy = kWorldSize / 2;
    const float px = cx - 60, ex = cx + 170;
    if (setup.budget > 0) {
        // (Bought hosts form up kFordGapM apart, the far one just beyond the bowmen's reach: 230 m of walking was a dead minute.)
        muster(kPlayer, playerBuys(setup, true), {px, cy, 0}, east, false, false, &setup.roster);
        muster(kEnemy, foeBuys(setup, false), {px + kFordGapM, cy, 0}, west);
        return;
    }

    // Oakhaven holds the berm above the ford: shield wall in front, archers and
    // grenadiers behind, healers at the rear, horse on the flanks, fliers behind.
    spawnSquad("Oakhaven Warriors", kPlayer, UnitType::Warrior, 12, {px, cy - 12, 0}, east);
    spawnSquad("The Grey Watch", kPlayer, UnitType::HeavyInfantry, 12, {px, cy + 12, 0}, east);
    spawnSquad("Ferry Pikes", kPlayer, UnitType::Pikemen, 12, {px, cy + 36, 0}, east);
    spawnSquad("Iron Hill Sappers", kPlayer, UnitType::DwarfGrenadier, 8, {px, cy - 36, 0}, east);
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
    spawnSquad("Laughing Pack of Gnarl", kEnemy, UnitType::Hyena, 6, {ex - 10, cy - 92, 0}, west);

}

const sim::Continent::City& siegeTown(const sim::Continent& c) {
    // The neutral walled town nearest Oakhaven Ford.
    const sim::Vec3 ford = c.crossings().front().pos;
    const sim::Continent::City* best = nullptr;
    for (const sim::Continent::City& city : c.cities()) {
        if (city.capital || !city.walled) continue;
        if (!best || (city.pos - ford).flat().lengthSq() < (best->pos - ford).flat().lengthSq()) best = &city;
    }
    return *best;
}

const sim::Continent::City& capitalTown(const sim::Continent& c) {
    // The capital nearest Oakhaven Ford: a castle of two doors in its outer wall, and an inner bailey.
    const sim::Vec3 ford = c.crossings().front().pos;
    const sim::Continent::City* best = nullptr;
    for (const sim::Continent::City& city : c.cities()) {
        if (!city.capital) continue;
        if (!best || (city.pos - ford).flat().lengthSq() < (best->pos - ford).flat().lengthSq()) best = &city;
    }
    return *best;
}

std::vector<sim::UnitType> skirmishTypes() {
    std::vector<UnitType> out;
    for (UnitType t : {UnitType::LightInfantry, UnitType::HeavyInfantry, UnitType::Pikemen, UnitType::Warrior, UnitType::Archers, UnitType::DwarfGrenadier,
                       UnitType::LightCavalry, UnitType::HeavyCavalry, UnitType::WolfRiders, UnitType::Catapult})
        if (sim::Conquest::trainable(t)) out.push_back(t);
    return out;
}

int armyCost(const std::vector<Buy>& army) {
    int gold = 0;
    for (const Buy& b : army) gold += b.squads * sim::Conquest::price(b.type);
    return gold;
}

std::vector<Buy> buyArmy(int budget, bool engines, int every, int most) {
    // A lord's mix: foot to hold, bowmen behind them, horse and bombers to finish, an engine to every eight squads when the
    // work wants one (`every`: a siege train has one to every four, kSiegeEngines: two trains for 300 gold; `most`: no more trains than that). What the next pick costs too much for
    // becomes the cheapest foot, until the purse is empty.
    static const UnitType order[] = {UnitType::HeavyInfantry, UnitType::Archers, UnitType::Pikemen, UnitType::Warrior, UnitType::Archers, UnitType::HeavyInfantry,
                                     UnitType::DwarfGrenadier, UnitType::HeavyCavalry, UnitType::LightInfantry, UnitType::Archers, UnitType::LightCavalry, UnitType::Pikemen};
    std::vector<Buy> army;
    const auto add = [&](UnitType t) {
        for (Buy& b : army)
            if (b.type == t) {
                ++b.squads;
                return;
            }
        army.push_back({t, 1});
    };
    int left = budget, n = 0, trains = 0;
    for (size_t k = 0; left >= sim::Conquest::price(UnitType::LightInfantry) && n < 60; ++k) {
        UnitType t = order[k % (sizeof order / sizeof order[0])];
        if (engines && n > 0 && n % every == 0 && trains < most && left >= sim::Conquest::price(UnitType::Catapult)) t = UnitType::Catapult, ++trains;
        if (sim::Conquest::price(t) > left) t = UnitType::LightInfantry;
        add(t);
        left -= sim::Conquest::price(t);
        ++n;
    }
    return army;
}

std::vector<Buy> Battle::playerBuys(const SkirmishSetup& setup, bool engines, int every) const {
    return setup.army.empty() ? buyArmy(setup.budget > 0 ? setup.budget : kDefaultBudget, engines, every) : setup.army;
}

std::vector<Buy> Battle::foeBuys(const SkirmishSetup& setup, bool engines, int every, int pct) const {
    // The same purse as the player's (what he bought is what the other side has to match); on Easy, three quarters of it;
    // and `pct` of that: walls are worth men, so a siege's besiegers have half as much again as its keepers (kSiegeOddsPct),
    // whichever the player is.
    const int purse = setup.foePurse > 0 ? setup.foePurse : setup.army.empty() ? (setup.budget > 0 ? setup.budget : kDefaultBudget) : std::max(armyCost(setup.army), 1);
    const int easy = setup.easy ? std::max(purse * kEasyPursePct / 100, 1) : purse;
    return buyArmy(std::max(easy * pct / 100, 1), engines, every, every == kSiegeEngines ? 2 : 99);  // (a computer's siege: a train a camp)
}

void Battle::muster(sim::PlayerId owner, const std::vector<Buy>& army, V3 anchor, V3 facing, bool lord, bool enginesFirst, const std::vector<CarriedSquad>* roster, bool skirmishers) {
    if (roster && roster->empty()) roster = nullptr;
    // Ranks behind `anchor` (the middle of the front), away from `facing`: foot in front, bowmen, bombers and the lord behind
    // them, engines at the back; horse on the wings of the front. One squad to a place of kSlotM across.
    // `enginesFirst` (a siege's besiegers): the engines are the front rank, at their throw from the gate, and the army stands
    // behind them out of the walls' bowshot until the engines have done their work (World::escortSpot calls up their guard).
    constexpr float kSlotM = 28.0f, kRowM = 24.0f;
    constexpr int kAcross = 5;
    const V3 right{facing.y, -facing.x, 0};
    // A place to stand a squad: the slot, or the nearest open ground to it (dry, standable, not in a wall's ring), looking out
    // 10 m each way so a squad does not form up on a cliff's lip or in a wood.
    const auto settle = [&](V3 p) {
        const auto clear = [&](V3 q) {
            for (const V3 d : {V3{0, 0, 0}, V3{10, 0, 0}, V3{-10, 0, 0}, V3{0, 10, 0}, V3{0, -10, 0}})
                if (!openFor(*region_, *world_, q + d)) return false;
            return true;
        };
        if (clear(p)) return p;
        for (int r = 8; r <= 80; r += 8)
            for (int k = 0; k < 12; ++k) {
                static const float kCos[12] = {1, 0.866f, 0.5f, 0, -0.5f, -0.866f, -1, -0.866f, -0.5f, 0, 0.5f, 0.866f};  // (no libm: peers place alike)
                static const float kSin[12] = {0, 0.5f, 0.866f, 1, 0.866f, 0.5f, 0, -0.5f, -0.866f, -1, -0.866f, -0.5f};
                const V3 q = p + V3{kCos[k] * static_cast<float>(r), kSin[k] * static_cast<float>(r), 0};
                if (q.x > 40 && q.y > 40 && q.x < kWorldSize - 40 && q.y < kWorldSize - 40 && clear(q)) return q;
            }
        return p;
    };
    struct Item {
        UnitType type;
        std::string name;
        const CarriedSquad* carried = nullptr;
    };
    std::vector<Item> all, foot, rear, engines, horse, lords, bows;
    if (roster) {
        for (const CarriedSquad& c : *roster)
            if (!c.men.empty()) (c.type == UnitType::Hero ? lords : all).push_back({c.type, c.name, &c});
    } else {
        for (const Buy& b : army)
            for (int i = 0; i < b.squads; ++i) all.push_back({b.type, std::string(sim::unitTypeName(b.type)) + (b.squads > 1 ? " " + std::to_string(i + 1) : ""), nullptr});
        if (lord) lords.push_back({UnitType::Hero, "The Lord", nullptr});
    }
    const auto count = [](const Item& it) { return it.carried ? static_cast<int>(it.carried->men.size()) : it.type == UnitType::Hero ? 1 : sim::Conquest::squadSize(it.type); };
    const auto raise = [&](const Item& it, V3 at) {
        spawnSquad(it.name, owner, it.type, count(it), at, facing);
        if (it.carried) seatCarried(*it.carried);
    };
    for (const Item& it : all) {
        switch (it.type) {
            case UnitType::LightCavalry:
            case UnitType::HeavyCavalry:
            case UnitType::WolfRiders: horse.push_back(it); break;
            case UnitType::Catapult: engines.push_back(it); break;
            case UnitType::Archers: (skirmishers ? bows : rear).push_back(it); break;
            case UnitType::DwarfGrenadier:
            case UnitType::Wizards:
            case UnitType::Journeyman: rear.push_back(it); break;
            default: foot.push_back(it);
        }
    }
    for (const Item& it : lords) rear.insert(rear.begin() + static_cast<long>(rear.size() / 2), it);
    float row = 0;
    const auto rank = [&](const std::vector<Item>& items) {
        for (size_t i = 0; i < items.size(); i += kAcross, ++row) {
            const size_t n = std::min<size_t>(kAcross, items.size() - i);
            for (size_t j = 0; j < n; ++j) {
                const float lateral = (static_cast<float>(j) - static_cast<float>(n - 1) / 2) * kSlotM;
                const Item& it = items[i + j];
                raise(it, settle(anchor + right * lateral - facing * (row * kRowM)));
            }
        }
    };
    if (enginesFirst) rank(engines);
    rank(bows);  // skirmishers: the bowmen out in front, to open the battle (the player's to pull back as he sets his line)
    const float footRow = row;
    rank(foot);
    // The horse on either wing of the front rank, outside the foot.
    const float wing = (static_cast<float>(std::min<size_t>(kAcross, foot.size())) / 2 + 0.5f) * kSlotM;
    for (size_t i = 0; i < horse.size(); ++i) {
        const float side = i % 2 ? 1.0f : -1.0f, out = wing + static_cast<float>(i / 2) * kSlotM;
        raise(horse[i], settle(anchor + right * (side * out) - facing * (8.0f + footRow * kRowM)));
    }
    rank(rear);
    if (!enginesFirst) rank(engines);
}

void Battle::setUpField(const SkirmishSetup& setup) {
    // Open ground: the two armies a bowshot apart (kFieldGapM), facing.
    const float cx = kWorldSize / 2, cy = kWorldSize / 2;
    muster(kPlayer, playerBuys(setup, true), {cx - kFieldGapM / 2, cy, 0}, {1, 0, 0}, true, false, &setup.roster, true);
    muster(kEnemy, foeBuys(setup, false, kFieldEngines, setup.hard ? kOpenHardPursePct : 100), {cx + kFieldGapM / 2, cy, 0}, {-1, 0, 0}, false, false, nullptr, true);
    foeName_ = "other host";
}

void Battle::setUpSiege(const sim::Continent::City& town, const SkirmishSetup& setup) {
    // A walled town's keepers, few but behind walls, against an army raised at the Conquest's prices before its doors (a
    // capital has two; the besiegers come to both). `defend`: the player keeps the walls.
    const bool classic = setup.budget == 0 && setup.army.empty() && scenario_ == Scenario::Siege && !setup.defend;
    const sim::PlayerId keeper = setup.defend ? kPlayer : kEnemy, besieger = setup.defend ? kEnemy : kPlayer;
    foeName_ = (setup.defend ? "Besiegers of " : "Garrison of ") + town.name;
    const sim::Continent::Castle& k = town.castle;
    const V3 o = origin();
    const auto local = [&](const sim::Vec3& p) { return toV3(p) - V3{o.x, o.y, 0}; };
    const sim::Continent::Castle::Ring& ring = k.rings.front();
    const size_t nc = ring.corners.size();
    const V3 centre = local(town.pos);
    // The doors: the first (the road's) and, in a capital or a long town, the second in a side wall.
    std::vector<V3> gates, outs;
    for (const size_t edge : {ring.gateEdge, ring.gateEdge2}) {
        if (edge == sim::Continent::Castle::Ring::kNoEdge) continue;
        const V3 mid = (local(ring.corners[edge]) + local(ring.corners[(edge + 1) % nc])) * 0.5f;
        V3 away{mid.x - centre.x, mid.y - centre.y, 0};
        gates.push_back(mid);
        outs.push_back(away * (1.0f / away.length()));
    }
    const V3 gate = gates.front(), out = outs.front();
    siegeCentre_ = centre, siegeOut_ = out, siegeDoors_ = static_cast<int>(gates.size());
    for (const sim::Vec3& corner : ring.corners) siegeRadius_ = std::max(siegeRadius_, V3{local(corner).x - centre.x, local(corner).y - centre.y, 0}.length());

    // The keepers, stationed as any garrison is (World::garrisonPosts): archers on the walk, one to an embrasure, spread round
    // the ring; the pikes and the militia in ranks behind the doors; the lord behind them.
    {
        struct Company {
            std::string name;
            UnitType type;
            int count;
            const CarriedSquad* carried = nullptr;
        };
        std::vector<Company> companies;
        if (classic) {
            companies = {{"Town Watch", UnitType::Archers, static_cast<int>(nc * 2)},
                         {"Gate Wardens", UnitType::Pikemen, 8},
                         {"Town Militia", UnitType::LightInfantry, 12},
                         {"Castellan's Guard", UnitType::HeavyInfantry, 6}};
        } else if (keeper == kPlayer && !setup.roster.empty()) {
            for (const CarriedSquad& c : setup.roster)  // (the March: each squad its own company, its lord among them)
                if (!c.men.empty()) companies.push_back({c.name, c.type, static_cast<int>(c.men.size()), &c});
        } else {
            for (const Buy& b : keeper == kPlayer ? playerBuys(setup, false) : foeBuys(setup, false, kFieldEngines, 10000 / kSiegeOddsPct))
                companies.push_back({sim::unitTypeName(b.type), b.type, b.squads * sim::Conquest::squadSize(b.type)});
            if (keeper == kPlayer) companies.push_back({"The Lord", UnitType::Hero, 1});
        }
        std::vector<sim::UnitStats> kinds;
        for (const Company& c : companies)
            for (int i = 0; i < c.count; ++i) kinds.push_back(sim::unitStats(c.type));
        std::vector<const sim::UnitStats*> ptrs;
        for (const sim::UnitStats& u : kinds) ptrs.push_back(&u);
        const size_t gateIndex = world_->gateOfTown(town.pos - region_->origin());
        const std::vector<sim::World::GarrisonPost> posts = gateIndex == sim::World::kNoGate ? std::vector<sim::World::GarrisonPost>{} : world_->garrisonPosts(gateIndex, ptrs);
        size_t next = 0;
        for (const Company& c : companies) {
            Squad s{c.name, keeper, c.type, {}};
            s.formation = c.type == UnitType::Archers ? Formation::Loose : defaultFormation(c.type);
            s.facing = out;
            s.garrison = true;
            const sim::UnitStats stats = sim::unitStats(c.type);
            for (int i = 0; i < c.count; ++i, ++next) {
                if (next >= posts.size()) break;
                const sim::EntityId id = world_->spawnUnit(keeper, posts[next].pos, posts[next].face, stats);
                world_->setSquad(id, static_cast<uint32_t>(squads_.size()) + 1, simFormation(s.formation));
                s.members.push_back(id);
                if (squadOfUnit_.size() < id) squadOfUnit_.resize(id, -1);
                squadOfUnit_[id - 1] = static_cast<int>(squads_.size());
            }
            if (s.members.empty()) continue;
            squads_.push_back(std::move(s));
            if (c.carried) seatCarried(*c.carried);
        }
    }

    // The besiegers form up on the road before the gate (or the nearest open ground out from it, clear of the neighbouring
    // towns), half to each door of a castle with two: their engines in front, set up at their throw from the doors, the army
    // behind them. A computer besieging a town of one door makes a second camp of a third of its host and its second
    // engines before a side wall, to shell the walks from there (World::autoTarget) and to come in by the breach from
    // another quarter: the keeper has two batteries to watch, and a sortie against one is seen by the other.
    if (!classic) {
        std::vector<Buy> all = besieger == kPlayer ? playerBuys(setup, true, kSiegeEngines) : foeBuys(setup, true, kSiegeEngines, kSiegeOddsPct);
        // The engines are dealt first, so the road's door has the first of them.
        std::stable_partition(all.begin(), all.end(), [](const Buy& b) { return b.type == UnitType::Catapult; });
        std::vector<V3> camps = gates, faces = outs;
        std::vector<float> off(gates.size(), kBatteryM);
        if (gates.size() == 1 && besieger != kPlayer) {
            // The side wall that looks most squarely away from the road, with open ground before it and no water between
            // that ground and the wall, or between it and the road before the gate (the way its men go to the breach).
            const auto dry = [&](const V3& a, const V3& b) {
                const int steps = std::max(1, static_cast<int>((b - a).length() / 8.0f));
                for (int k = 0; k <= steps; ++k) {
                    const sim::Vec3 q = toSim(a + (b - a) * (static_cast<float>(k) / static_cast<float>(steps)));
                    if (region_->underWater(q.x, q.y)) return false;
                }
                return true;
            };
            size_t best = nc;
            float bestDot = 2;
            for (size_t e = 0; e < nc; ++e) {
                const V3 mid = (local(ring.corners[e]) + local(ring.corners[(e + 1) % nc])) * 0.5f;
                const V3 away = V3{mid.x - centre.x, mid.y - centre.y, 0}.normalized();
                const float d = std::abs(away.dot(out));
                if (ring.gateOn(e) || d >= bestDot) continue;
                const V3 camp = conqBefore(*region_, *world_, mid, away, kFlankM);
                if (!openFor(*region_, *world_, camp) || !dry(camp, mid + away * 6.0f) || !dry(camp, gate + out * 60.0f)) continue;
                bestDot = d;
                best = e;
            }
            if (best < nc) {
                const V3 mid = (local(ring.corners[best]) + local(ring.corners[(best + 1) % nc])) * 0.5f;
                camps.push_back(mid);
                faces.push_back(V3{mid.x - centre.x, mid.y - centre.y, 0}.normalized());
                off.push_back(kFlankM);
            }
        }
        if (besieger == kPlayer && !setup.roster.empty()) {
            // The March: the squads as they stand, dealt to the camps as a bought army's are (the engines first), the lord at the road's door.
            std::vector<std::vector<CarriedSquad>> rosters(camps.size());
            size_t engine = 0, other = 0;
            for (const CarriedSquad& c : setup.roster) {
                if (c.men.empty()) continue;
                rosters[c.type == UnitType::Hero ? 0 : (c.type == UnitType::Catapult ? engine++ : other++) % camps.size()].push_back(c);
            }
            for (size_t d = 0; d < camps.size(); ++d)
                if (!rosters[d].empty()) muster(besieger, {}, conqBefore(*region_, *world_, camps[d], faces[d], off[d]), faces[d] * -1.0f, false, true, &rosters[d]);
            return;
        }
        std::vector<std::vector<Buy>> byCamp(camps.size());
        size_t engine = 0, other = 0;
        for (const Buy& b : all)
            for (int i = 0; i < b.squads; ++i) {
                // Each camp its engines in turn; the rest half to each door, or a third to the camp before the side wall.
                const size_t turn = b.type == UnitType::Catapult ? engine++ : other++;
                std::vector<Buy>& mine = byCamp[camps.size() > gates.size() && b.type != UnitType::Catapult ? (turn % 3 == 2 ? 1 : 0) : turn % camps.size()];
                if (!mine.empty() && mine.back().type == b.type) ++mine.back().squads;
                else mine.push_back({b.type, 1});
            }
        for (size_t d = 0; d < camps.size(); ++d)
            muster(besieger, byCamp[d], conqBefore(*region_, *world_, camps[d], faces[d], off[d]), faces[d] * -1.0f, besieger == kPlayer && d == 0, true);
        if (besieger != kPlayer)
            for (const sim::Unit& u : world_->units()) besiegers_ += u.owner == besieger && roleOf(static_cast<UnitType>(u.stats.type)) != Role::Siege;
        return;
    }
    const V3 camp = conqBefore(*region_, *world_, gate, out, 110.0f), in = out * -1.0f;
    const V3 side{out.y, -out.x, 0};
    spawnSquad("Oakhaven Warriors", kPlayer, UnitType::Warrior, 12, camp + side * -14.0f, in);
    spawnSquad("The Grey Watch", kPlayer, UnitType::HeavyInfantry, 12, camp + side * 14.0f, in);
    spawnSquad("Iron Hill Sappers", kPlayer, UnitType::DwarfGrenadier, 8, camp + side * 40.0f, in);
    spawnSquad("1st Oakhaven Bowmen", kPlayer, UnitType::Archers, 12, camp + out * 18.0f + side * -16.0f, in);
    spawnSquad("Silverwood Archers", kPlayer, UnitType::Archers, 12, camp + out * 18.0f + side * 16.0f, in);
    spawnSquad("Deepdelve Sappers", kPlayer, UnitType::DwarfGrenadier, 6, camp + out * 10.0f, in);
    spawnSquad("Brothers of the Root", kPlayer, UnitType::Journeyman, 4, camp + out * 30.0f, in);
    spawnSquad("Lord Aldric", kPlayer, UnitType::Hero, 1, camp + out * 6.0f, in);
    spawnSquad("Eyrie Griffins", kPlayer, UnitType::Griffins, 3, camp + out * 36.0f + side * -30.0f, in);
    // Two mangonels well back, within a long throw of the gate.
    spawnSquad("Oakhaven Siege Train", kPlayer, UnitType::Catapult, 2, camp + out * 26.0f + side * 34.0f, in);
}

bool Battle::siegeFocus(V3& at, float& distance, V3& out) const {
    if (siegeDoors_ == 0) return false;
    // The besiegers' squads are those that are no garrison's.
    V3 sum{};
    int n = 0;
    for (const Squad& s : squads_)
        if (!s.garrison && aliveCount(s) > 0) sum = sum + squadCentre(s, false), ++n;
    at = n ? V3{(siegeCentre_.x + sum.x / static_cast<float>(n)) / 2, (siegeCentre_.y + sum.y / static_cast<float>(n)) / 2, 0} : siegeCentre_;
    float reach = siegeRadius_;
    for (const Squad& s : squads_)
        if (!s.garrison && aliveCount(s) > 0) {
            const V3 c = squadCentre(s, false);
            reach = std::max(reach, V3{c.x - at.x, c.y - at.y, 0}.length() + 20.0f);
        }
    distance = std::max(110.0f, reach * 3.0f);
    out = siegeOut_;
    return true;
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
    // Every gate is kept by its holder's garrison (World::keepGate, the Conquest's rule too): the archers go up on the walls, the
    // foot form behind the doors; the player's keepers are his idle men inside his walls, and his hand on the gate overrules them.
    for (size_t i = 0; i < world_->gates().size(); ++i) world_->keepGate(i, holder, holder == kPlayer);
}

int Battle::gateAt(const Camera& camera, float x, float y, float pixelScale) const {
    int best = -1;
    float bestD2 = kSquadTapRadiusPx * pixelScale * kSquadTapRadiusPx * pixelScale;
    const auto& gates = worldRegion().gates();
    for (size_t i = 0; i < gates.size(); ++i) {
        // A gate that stands, or a foe's that is down: a breach is a place to order men through (stormGate).
        if (world_->gates()[i].broken() && (world_->gates()[i].holder == local_ || world_->gates()[i].holder == sim::GateState::kNoHolder)) continue;
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
    for (size_t i = 0; i < worldRegion().gates().size(); ++i) {
        const sim::GateState& gs = world_->gates()[i];
        if (gs.broken() || gs.holder != kPlayer || !siegeGate(i)) continue;
        const V3 g = toV3(worldRegion().gates()[i].a + worldRegion().gates()[i].b) * 0.5f;
        // The nearest door, but a door already failing is worth some metres more than it lies (a castle with two doors is
        // battered at the weaker, and the engines go on at it until it falls, then turn to the other).
        const sim::Gate& door = worldRegion().gates()[i];
        const float failing = static_cast<float>(door.maxHp - gs.hp) / static_cast<float>(std::max(1, door.maxHp));
        const float away = V3{g.x - c.x, g.y - c.y, 0}.length();
        if (away > kSiegeScope) continue;  // (a gate of another town beyond the field: not this siege's)
        const float d = away - 30.0f * failing;
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
        world_->setSquad(id, static_cast<uint32_t>(squads_.size()) + 1, simFormation(s.formation));  // (a lone man has no formation: World::formed)
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
                // A landmark's ground changes faster than this grid: its
                // cliffs are bare rock, and the coarse land keeps under the
                // close-up land (a gorge's floor, a road's cut) rather than
                // bridging over it at the height of its rims.
                if (c.landmarkAt(x, y)) {
                    const sim::Fixed half = sim::Fixed::fromInt(static_cast<int64_t>(cell / 2));
                    sim::Fixed lowest = smp.height, steepest;
                    for (int n = 0; n < 4; ++n) {
                        const sim::Fixed dx = n == 0 ? half : n == 1 ? -half : sim::Fixed::zero(), dy = n == 2 ? half : n == 3 ? -half : sim::Fixed::zero();
                        const sim::Fixed h = c.sample(x + dx, y + dy).height;
                        lowest = sim::min(lowest, h);
                        steepest = sim::max(steepest, sim::abs(h - smp.height) / half);
                    }
                    height[k] = static_cast<float>(lowest.toDouble());
                    if (steepest > sim::Fixed::fromRatio(3, 5)) ground[k] = static_cast<uint8_t>(sim::Ground::Rock);
                }
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
    if (!inBattlefield(x, y)) {
        // Off the field's own ground, over the borrowed world's, the same ground the world's men stand on.
        if (conquest_ && inWorld(x, y)) return static_cast<float>(worldRegion().terrain().heightAt(toFixed(x), toFixed(y)).toDouble());
        return near_.contains(x, y) ? near_.heightAt(x, y) : far_.heightAt(x, y);
    }
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
    sfx_.setSpeed(decided() ? 1.0f : static_cast<float>(speed_));
    const bool siege = scenario_ == Scenario::Siege || scenario_ == Scenario::Capital;
    if (over() && !resultsCued_) {  // the results sheet: its own sound, and the field goes quiet under it
        resultsCued_ = true;
        sfx_.calm();
        sfx_.ui(Sound::Results);
    }
    if (paused_ || deploy_ || over()) {
        // The player studies the field: there is air, though nothing steps.
        if (!over() && stillFrames_++ % 30 == 0) sfx_.refreshAmbience(*world_, *continent_, origin(), siege);
        return;
    }
    stillFrames_ = 0;
    const double scaled = std::min(seconds, 0.25) * (decided() ? kLastBlowPace : speed_ * slow_);
    accumulator_ += scaled;
    int ticks = 0;
    while (accumulator_ >= kTickSeconds && ticks < kMaxTicksPerFrame && !over()) {
        step();
        accumulator_ -= kTickSeconds;
        ++ticks;
    }
    // Drop time we could not catch up on rather than spiralling.
    if (accumulator_ >= kTickSeconds) accumulator_ = 0;
    updateParticles(static_cast<float>(scaled));
}

void Battle::beforeWorldStep() {
    growUnits();
    const auto& units = world_->units();
    // A place in the world that a man was taken out of and another came to (Conquest's fights): all that is kept by place
    // is forgotten, so that the new man is not the one who stood there (his blows and wounds, his orders, his selection).
    for (size_t i = 0; i < units.size(); ++i) {
        const uint32_t g = world_->generation(units[i].id);
        if (seenGen_[i] == g) continue;
        seenGen_[i] = g;
        deathTick_[i] = attackTick_[i] = hitTick_[i] = UINT32_MAX;
        selectedFlag_[i] = 0;
        if (i < orderTick_.size()) orderTick_[i] = UINT32_MAX;
        if (i < slots_.size()) slots_[i] = {};
        if (i < foreign_.size()) foreign_[i] = 0;
        std::erase_if(smouldering_, [&](const Smoulder& s) { return s.unit == units[i].id; });
    }
    for (size_t i = 0; i < units.size(); ++i) prevUnitPos_[i] = toV3(units[i].pos);
    prevProjectilePos_.clear();
    for (const sim::Projectile& p : world_->projectiles()) prevProjectilePos_.push_back({p.id, toV3(p.pos)});
    // Who strikes or looses this tick, for the attack animation.
    readied_.resize(units.size());
    for (size_t i = 0; i < units.size(); ++i) readied_[i] = {units[i].meleeCooldown, units[i].reloadRemaining};
}

void Battle::afterWorldStep() {
    const auto& units = world_->units();
    growUnits();
    for (size_t i = 0; i < units.size() && i < readied_.size(); ++i)
        if (units[i].meleeCooldown > readied_[i].first || units[i].reloadRemaining > readied_[i].second) attackTick_[i] = world_->tick();
    collectEvents();
    collectTactics();
    collectCalls();
    voice_.observe(*world_, outcome());
    presentDeaths();
    observeSquads();
}

void Battle::forgetMen() {
    plan_.clear();  // held orders name men of the world that was
    const size_t n = world_->units().size();
    deathTick_.assign(n, UINT32_MAX);
    attackTick_.assign(n, UINT32_MAX);
    hitTick_.assign(n, UINT32_MAX);
    selectedFlag_.assign(n, 0);
    prevUnitPos_.assign(n, V3{});
    seenGen_.assign(n, 0);
    for (size_t i = 0; i < n; ++i) prevUnitPos_[i] = toV3(world_->units()[i].pos), seenGen_[i] = world_->generation(world_->units()[i].id);
    readied_.clear();
    smouldering_.clear();
    bleeders_.clear();
    rings_.clear();
    dye_.clear();
    recent_.clear();
}

void Battle::step() {
    beforeWorldStep();
    // A match gathers every seat's orders and plays the computer's (mode.h).
    if (mode_) mode_->beforeStep(*this, pending_);
    else if (world_->tick() % kAiIntervalTicks == 0) runEnemyAi();
    // (the orders may have raised men: their places and ticks are kept too)
    growUnits();
    readied_.resize(world_->units().size());
    for (size_t i = 0; i < world_->units().size(); ++i) readied_[i] = {world_->units()[i].meleeCooldown, world_->units()[i].reloadRemaining};
    const auto start = std::chrono::steady_clock::now();
    world_->step(pending_);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stepMs_ = stepMs_ * 0.95 + ms * 0.05;
    pending_.clear();
    capture_.step(*world_);  // battle_capture.cpp
    if (mode_) mode_->afterStep(*this);  // its rules: the hill, the flags, the waves
    else judge();                        // battle_end.cpp: a host quits the field, the defenders lay down arms
    afterWorldStep();
}

std::vector<uint32_t> Battle::takeDirtyRemains() {
    std::vector<uint32_t> out;
    out.swap(dirtyRemains_);
    for (uint32_t slot : out) dirtyFlag_[slot] = 0;
    return out;
}

void Battle::addMark(GroundMark::Kind kind, V3 pos, float yaw, float size, uint32_t seed) {
    // Water carries blood and scorch marks away.
    const sim::Ground g = worldRegion().groundAt(toFixed(pos.x), toFixed(pos.y));
    if (g == sim::Ground::Shallows || g == sim::Ground::DeepWater) return;
    // On the ground, or on the deck it happened above (a wall-walk, a bridge).
    pos.z = floorAt(pos);
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
    // Spray lives as long as it is in the air: up and back down to the water it left.
    if (kind == Particle::Kind::Spray) life = std::max(0.15f, std::min(life * 3.0f, 2.0f * vel.z / kGravity));
    particles_.push_back({kind, pos, vel, 0.0f, life, size});
}

void Battle::stepEffects(float seconds) {
    updateParticles(seconds);
}

void Battle::collectEvents() {
    for (uint32_t slot : world_->changedRemains()) {
        if (!dirtyFlag_[slot]) {
            dirtyFlag_[slot] = 1;
            dirtyRemains_.push_back(slot);
        }
    }

    applyImpacts(world_->impacts(), *world_, V3{0, 0, 0}, true);
    stepWater();

    // Trails: fireballs burn, a Molotov's rag flames at the bottle's neck,
    // a satchel's fuse fizzes once something has lit it (laid, it lies quiet).
    const uint32_t tick = world_->tick();
    const auto rnd = [&] { return unit01(fxSeed_ = mix(fxSeed_, tick)); };
    const auto srnd = [&] { return rnd() * 2.0f - 1.0f; };
    for (const sim::Projectile& p : world_->projectiles()) {
        if (p.kind == sim::ProjectileKind::Fireball) {
            emit(Particle::Kind::Fire, toV3(p.pos), V3{srnd(), srnd(), srnd()} * 0.6f, 0.35f, 0.45f);
        } else if (p.kind == sim::ProjectileKind::Molotov) {
            emit(Particle::Kind::Fire, toV3(p.pos) + V3{0, 0, 0.12f}, V3{srnd(), srnd(), 1.0f + rnd()} * 0.4f, 0.25f, 0.16f);
        } else if (p.kind == sim::ProjectileKind::Bomb && p.fuse > 0 && (tick & 1)) {
            emit(Particle::Kind::Spark, toV3(p.pos) + V3{0, 0, 0.25f}, {srnd(), srnd(), 1.5f}, 0.25f, 0.04f);
        } else if (p.kind == sim::ProjectileKind::Stone && p.landed && !p.resting && p.vel.z == sim::Fixed::zero()) {
            // A boulder rolling: its furrow in the grass (a stretch each half metre it has gone), the dust it raises
            // and the clods it throws up behind it.
            const V3 at = toV3(p.pos), v = toV3(p.vel);
            const float speed = std::sqrt(v.x * v.x + v.y * v.y), rolled = static_cast<float>(p.rolled.toDouble());
            if (speed < 0.5f) continue;
            if (static_cast<int>(rolled * 2.0f) != static_cast<int>((rolled - speed / sim::kTicksPerSecond) * 2.0f))
                addMark(GroundMark::Kind::Furrow, at, std::atan2(v.y, v.x), 0.8f, mix(p.id, static_cast<uint32_t>(rolled * 2.0f)));
            if (rnd() < speed / 9.0f) emit(Particle::Kind::Smoke, at + V3{srnd() * 0.4f, srnd() * 0.4f, 0.25f}, V3{srnd() - v.x * 0.08f, srnd() - v.y * 0.08f, 0.5f + rnd()}, 1.0f + rnd(), 0.5f + rnd() * 0.6f);
            if (speed > 4.0f && (tick & 1)) emit(Particle::Kind::Dirt, at, V3{srnd() * 2.0f + v.x * 0.15f, srnd() * 2.0f + v.y * 0.15f, 2.0f + rnd() * 2.5f}, 0.9f, 0.05f + rnd() * 0.07f);
        }
    }
}

// What blows and wounds do to the look of the field: blood and dust on the ground, sparks, splinters
// off a gate, craters and flames. `source` is the world that reported them (the battle's own, or the
// conquest's, whose men fight on the same ground), `shift` its places in this battle's coordinates;
// `own`: they are this battle's men, whose hits and deaths are kept for the pictures (hitTick_, deathTick_).
void Battle::applyImpacts(const std::vector<sim::ImpactEvent>& events, const sim::World& source, V3 shift, bool own) {
    const uint32_t tick = world_->tick();
    const auto rnd = [&] { return unit01(fxSeed_ = mix(fxSeed_, tick)); };
    const auto srnd = [&] { return rnd() * 2.0f - 1.0f; };

    std::erase_if(recent_, [&](const Recent& r) { return tick - r.tick > 4 * sim::kTicksPerSecond; });
    for (const sim::ImpactEvent& e : events) {
        const V3 pos = toV3(e.pos) + shift;
        // The fight the camera is taken to (hotSpot) is one the viewer may see: of whose man was struck, whose gate was
        // battered, or else whose man stands nearest the blast (nobody's, if none does).
        sim::PlayerId whose = sim::GateState::kNoHolder;
        if (e.hitUnit != sim::kNoEntity && e.hitUnit <= source.units().size()) {
            whose = source.units()[e.hitUnit - 1].owner;
        } else if (e.gate >= 0 && static_cast<size_t>(e.gate) < source.gates().size()) {
            whose = source.gates()[static_cast<size_t>(e.gate)].holder;
        } else {
            sim::Fixed best = sim::Fixed::fromInt(12 * 12);
            for (const sim::Unit& u : source.units()) {
                const sim::Fixed d = (u.pos - e.pos).flat().lengthSq();
                if (u.alive() && d < best) best = d, whose = u.owner;
            }
        }
        if (e.kind != sim::ImpactEvent::Kind::Ground && e.kind != sim::ImpactEvent::Kind::Bleed && recent_.size() < 600 && sees(whose, pos.x, pos.y)) {
            const bool blast = e.kind == sim::ImpactEvent::Kind::Explosion || e.kind == sim::ImpactEvent::Kind::Burst;
            recent_.push_back({pos, tick, blast ? 8.0f : (e.killed ? 3.0f : 1.0f)});
        }
        const V3 dir = toV3(e.dir).normalized();
        const uint32_t seed = mix(e.projectile ? e.projectile : e.hitUnit, tick);
        using K = sim::ImpactEvent::Kind;
        switch (e.kind) {
            case K::Ground:
                if (presentInWater(e, pos, nullptr, {})) break;
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
                emit(Particle::Kind::Flash, pos + V3{0, 0, 0.5f}, {}, 0.18f, r * 1.3f);
                // On water: the flash, and steam (battle_water.cpp); no crater, no fire left burning, no dirt thrown.
                if (presentInWater(e, pos, nullptr, {})) break;
                addMark(GroundMark::Kind::Crater, pos, rnd() * 6.28f, r * 0.95f, seed);
                for (int i = 0; i < 18; ++i) emit(Particle::Kind::Fire, pos + V3{0, 0, 0.3f}, V3{srnd(), srnd(), rnd() * 0.8f} * (r * 2.5f), 0.45f + rnd() * 0.3f, 0.5f + rnd() * 0.6f);
                for (int i = 0; i < 14; ++i) emit(Particle::Kind::Smoke, pos + V3{srnd(), srnd(), 0.5f}, {srnd() * 1.5f, srnd() * 1.5f, 1.5f + rnd() * 2}, 2.5f + rnd() * 2, 1.0f + rnd());
                for (int i = 0; i < 24; ++i) emit(Particle::Kind::Dirt, pos, V3{srnd() * 6, srnd() * 6, 5 + rnd() * 9}, 1.6f, 0.08f + rnd() * 0.1f);
                break;
            }
            case K::Burst: {
                // A Molotov shatters: a sheet of flame rolling out low over the
                // ground and burning a while, a black scorch, smoke; no blast of dirt.
                const float r = static_cast<float>(e.radius.toDouble());
                emit(Particle::Kind::Flash, pos + V3{0, 0, 0.3f}, {}, 0.12f, r * 0.8f);
                if (presentInWater(e, pos, nullptr, {})) break;  // the oil goes out in a hiss of steam
                addMark(GroundMark::Kind::Crater, pos, rnd() * 6.28f, r * 0.8f, seed);
                for (int i = 0; i < 30; ++i) {
                    const float a = rnd() * 6.2832f, d = r * (0.2f + rnd() * 0.8f);
                    emit(Particle::Kind::Fire, pos + V3{std::cos(a) * d * 0.4f, std::sin(a) * d * 0.4f, 0.15f},
                         V3{std::cos(a) * d * 1.6f, std::sin(a) * d * 1.6f, 0.4f + rnd() * 1.2f}, 0.9f + rnd() * 1.1f, 0.35f + rnd() * 0.45f);
                }
                for (int i = 0; i < 10; ++i) emit(Particle::Kind::Smoke, pos + V3{srnd() * r * 0.6f, srnd() * r * 0.6f, 0.6f}, {srnd(), srnd(), 1.2f + rnd() * 1.5f}, 3.0f + rnd() * 2, 0.8f + rnd());
                break;
            }
            case K::Smash: {
                // A stone shatters: a scar in the ground, a burst of dust and chips.
                const float r = static_cast<float>(e.radius.toDouble());
                if (presentInWater(e, pos, nullptr, {})) break;  // a column of water, not a scar
                addMark(GroundMark::Kind::Crater, pos, rnd() * 6.28f, r * 0.5f, seed);
                for (int i = 0; i < 10; ++i) emit(Particle::Kind::Smoke, pos + V3{srnd(), srnd(), 0.3f}, {srnd() * 2, srnd() * 2, 0.8f + rnd()}, 1.8f + rnd(), 0.8f + rnd() * 0.6f);
                for (int i = 0; i < 20; ++i) emit(Particle::Kind::Dirt, pos, V3{srnd() * 5, srnd() * 5, 3 + rnd() * 6}, 1.4f, 0.07f + rnd() * 0.1f);
                break;
            }
            case K::Bounce: {
                // A boulder strikes the ground and goes on: a gouge, dust, clods thrown the way it is going.
                const float hard = std::min(1.0f, static_cast<float>(e.radius.toDouble()) / 10.0f);
                const V3 on = toV3(e.dir);
                addMark(GroundMark::Kind::Crater, pos, rnd() * 6.28f, 0.7f + 0.7f * hard, seed);
                for (int i = 0; i < 5 + static_cast<int>(8 * hard); ++i) emit(Particle::Kind::Smoke, pos + V3{srnd() * 0.6f, srnd() * 0.6f, 0.3f}, {srnd() * 1.5f, srnd() * 1.5f, 0.6f + rnd()}, 1.4f + rnd(), 0.6f + rnd() * 0.6f);
                for (int i = 0; i < 8 + static_cast<int>(12 * hard); ++i) emit(Particle::Kind::Dirt, pos, V3{srnd() * 3 + on.x * 0.3f, srnd() * 3 + on.y * 0.3f, 2 + rnd() * 5}, 1.2f, 0.06f + rnd() * 0.09f);
                break;
            }
            case K::Splash: {
                // Into the water: the rings, the blood and the spray of whatever fell in (battle_water.cpp); a boulder
                // (a projectile's splash, a heavy one) also throws up a column of spray and a ring of it.
                presentInWater(e, pos, nullptr, {});
                if (e.radius.toDouble() < 3.0) break;
                for (int i = 0; i < 26; ++i) {
                    const float a = rnd() * 6.2832f, out = 0.5f + rnd() * 2.5f;
                    emit(Particle::Kind::Smoke, pos + V3{std::cos(a) * 0.3f, std::sin(a) * 0.3f, 0.2f}, V3{std::cos(a) * out, std::sin(a) * out, 2.5f + rnd() * 4.5f}, 0.7f + rnd() * 0.6f, 0.35f + rnd() * 0.4f);
                }
                break;
            }
            case K::Struck: {
                // A boulder against stone, timber or a tree: chips and dust thrown back off it.
                const int chips = 6 + static_cast<int>(std::min(1.0f, static_cast<float>(e.radius.toDouble()) / 100.0f) * 22);
                for (int i = 0; i < chips; ++i) emit(Particle::Kind::Dirt, pos, V3{srnd() * 3, srnd() * 3, 1.5f + rnd() * 4} - dir * 3, 1.1f, 0.06f + rnd() * 0.1f);
                for (int i = 0; i < chips / 3; ++i) emit(Particle::Kind::Smoke, pos + V3{srnd(), srnd(), srnd() * 0.5f}, V3{srnd(), srnd(), 0.8f + rnd()} - dir, 1.6f + rnd(), 0.7f + rnd() * 0.7f);
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
                const sim::Unit* found = source.findUnit(e.hitUnit);
                if (found == nullptr) break;
                const sim::Unit& victim = *found;
                if (own) {
                    const size_t index = e.hitUnit - 1;
                    if (e.killed) deathTick_[index] = tick;
                    else hitTick_[index] = tick;
                }
                const V3 body = toV3(victim.pos) + shift;
                const float scale = static_cast<float>(victim.stats.height.toDouble()) / 1.8f;
                if (own) noteWound(victim);  // hurt badly, he drips as he goes
                // Standing in water, his blood goes into it (battle_water.cpp), not onto the bed under it.
                if (presentInWater(e, pos, &victim, body)) break;
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
            case Particle::Kind::Steam:
                p.vel = p.vel * std::exp(-0.8f * seconds);
                p.pos += p.vel * seconds;
                p.size += seconds * (p.kind == Particle::Kind::Steam ? 0.6f : 1.2f);
                break;
            case Particle::Kind::Spray:
                p.vel.z -= kGravity * seconds;
                p.pos += p.vel * seconds;
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
    if (n == 0) return s.members.empty() ? V3{} : pos(s.members.front() - 1);
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
    if (aiBreakOff()) return;  // a siege it has lost: it marches off, and the day is the keepers'
    // A town it besieges, once a way in is open and nobody holds the ground: its foot go to the pole (battle_capture.h).
    const bool toPole = capture_.active() && capture_.attacker() == kEnemy && capture_.defendersIn() == 0 && [&] {
        const int ring = capture_.stages()[capture_.stage()].ring;
        for (size_t g = 0; g < world_->gates().size(); ++g)
            if (worldRegion().gates()[g].ring == ring && world_->gates()[g].broken()) return true;
        return false;
    }();
    V3 anyEngines, anyDoor;
    const bool siege = aiSiege(kEnemy, {}, anyEngines, anyDoor);
    constexpr float kEasyNearM = 60.0f;  // Easy: a foe this near a squad is fought at once, however early
    // Normal and Hard, in the open: the bows first. The host comes up to a bowshot and stands, its foot in front of its
    // bowmen, and shoots (its foot in loose order under the arrows that come back: aiFormation); whoever comes out at
    // it is met by all of it. Then it goes in together: when the foe's bows are beaten or its own are, when the fight
    // is joined anyway, or after kDuelSeconds. (A host that walks in piecemeal under a standing line's arrows loses to
    // an army nobody commands.)
    V3 armyAt, wingAt;
    bool wing = false;
    float standM = 0;
    if (aiSkill_ != AiSkill::Easy && !siege && !capture_.active()) {
        int mine = 0, myBows = 0, hisBows = 0, fighting = 0, loosing = 0;
        for (const sim::Unit& u : world_->units()) {
            if (!u.alive() || u.routed) continue;
            const bool bow = u.stats.ranged.muzzleSpeed > sim::Fixed::zero() && u.stats.ranged.autoRange > sim::Fixed::zero() && u.stats.gait != sim::Gait::Wheeled;
            if (u.owner != kEnemy) {
                hisBows += bow && u.owner == kPlayer;
                continue;
            }
            ++mine;
            myBows += bow;
            fighting += u.engaged || u.foe != sim::kNoEntity;
            loosing += bow && u.reloadRemaining > 0;
            armyAt += toV3(u.pos);
        }
        if (mine > 0) armyAt = armyAt * (1.0f / static_cast<float>(mine));
        const uint32_t now = world_->tick();
        if (loosing > 0 && aiDuelSince_ == 0) aiDuelSince_ = now;
        const uint32_t duel = (aiSkill_ == AiSkill::Hard ? 40u : 60u) * sim::kTicksPerSecond;
        if (!aiAssault_ && (myBows == 0 || hisBows * 3 <= myBows || myBows * 2 < hisBows || fighting * 5 >= mine || (aiDuelSince_ != 0 && now - aiDuelSince_ > duel)))
            aiAssault_ = true;
        if (!aiAssault_) standM = static_cast<float>(sim::unitStats(UnitType::Archers).ranged.autoRange.toDouble()) * 0.8f - 10.0f;
        // Going in, it goes in on one wing: the end of the foe's line nearer to it, every squad of foot together, and rolls
        // the line up from there (the end squad of what is left, look by look). A line nobody commands is beaten in detail.
        if (aiAssault_) {
            V3 foeAt;
            int foes = 0;
            for (const Squad& t : squads_)
                if (t.owner == kPlayer && aliveCount(t) > 0 && !t.garrison) foeAt += squadCentre(t, false), ++foes;
            if (foes > 0) {
                foeAt = foeAt * (1.0f / static_cast<float>(foes));
                const V3 axis = V3{foeAt.x - armyAt.x, foeAt.y - armyAt.y, 0}.normalized();
                const V3 across{-axis.y, axis.x, 0};
                float lo = 0, hi = 0;
                V3 loAt = foeAt, hiAt = foeAt;
                for (const Squad& t : squads_) {
                    if (t.owner != kPlayer || aliveCount(t) == 0 || t.garrison || standing(t).broken == aliveCount(t)) continue;
                    const V3 tc = squadCentre(t, false);
                    const float lat = V3{tc.x - foeAt.x, tc.y - foeAt.y, 0}.dot(across);
                    if (lat < lo) lo = lat, loAt = tc;
                    if (lat > hi) hi = lat, hiAt = tc;
                }
                if (aiWing_ == 0) aiWing_ = (loAt - armyAt).length() <= (hiAt - armyAt).length() ? -1 : 1;
                wingAt = aiWing_ < 0 ? loAt : hiAt;
                wing = true;
            }
        }
    }
    for (const Squad& s : squads_) {
        if (s.owner != kEnemy || aliveCount(s) == 0 || standing(s).broken == aliveCount(s)) continue;  // routed: beyond orders
        const V3 c = squadCentre(s, false);
        float dist;
        const Role role = roleOf(s.type);
        const V3 goal = nearest(c, kPlayer, dist);
        if (dist == 1e30f) return;
        const size_t q = static_cast<size_t>(&s - squads_.data());
        aiRally(q);
        aiFormation(q);
        // Gone to a friend's call (aiAnswer): left to it until it is there, or a while.
        if (q < aiAnswering_.size() && world_->tick() < aiAnswering_[q]) {
            bool going = false;
            for (sim::EntityId id : s.members) going = going || (world_->units()[id - 1].alive() && world_->units()[id - 1].helping != sim::kNoEntity);
            if (going) continue;
            aiAnswering_[q] = 0;
        }
        const bool easyMarch = aiSkill_ == AiSkill::Easy && !siege && !s.garrison && role != Role::Siege && dist > kEasyNearM;
        // Easy comes late: it stands a while (the player has time to set his line), then each squad goes for the foe nearest
        // it, piecemeal, with no massing and no falling back (aiFight). (Every squad on the one foe nearest the army's middle,
        // "by one road", was tried: that is a concentration, and it cost a player who gave no orders 86 men for 57.)
        if (easyMarch && world_->tick() < kEasyWaitTicks) continue;
        if (role == Role::Siege) {
            // Engines never march at the foe; they throw from where they stand: at a gate in their throw, or (a battery set
            // before a side wall, the gate beyond its throw) at the men on the walks until none are left in its reach.
            if (!s.garrison && !aiShellsWalls(s)) aimEngines(s);
            continue;
        }
        if (s.garrison && role != Role::Healer) continue;  // its gate's keepers (World::keepGates): stations on the walls and behind the doors, then it meets what comes
        V3 engineAt, doorAt;
        if (role != Role::Healer && role != Role::Scout && aiSiege(kEnemy, c, engineAt, doorAt)) {
            aiEscort(q, engineAt, doorAt);  // engines first, from range; the rest hold their line by the nearest battery until the doors are down
            continue;
        }
        if (toPole && role == Role::Melee) {
            const V3 pole = toV3(capture_.at());
            if (V3{pole.x - c.x, pole.y - c.y, 0}.length() > 8.0f) aiSteer(q, pole);
            continue;
        }
        if (standM > 0 && role == Role::Melee && s.type != UnitType::Hero) {
            // Standing off: up to the line and no farther, unless a foe has come within a charge of it.
            const sim::EntityId near = nearestFoe(kEnemy, c, 1e4f);
            if (near != sim::kNoEntity) {
                const V3 fp = toV3(world_->units()[near - 1].pos);
                const float gap = V3{fp.x - c.x, fp.y - c.y, 0}.length();
                if (gap > 32.0f && gap < standM + 60.0f) {  // (farther off: the march as ever, massed: aiFight)
                    if (gap > standM + 4.0f) aiSteer(q, c + (fp - c) * ((gap - standM) / gap));
                    else aiHalt(q);
                    continue;
                }
            }
        }
        // Healers behind the nearest friend, the hyena-folk let loose (aiRaid), the rest as every computer seat plays: one code
        // path.
        if (wing && !s.garrison && role == Role::Melee && s.type != UnitType::Hero) {
            // To the wing together; once there its men are let go, and fight as men with no order do (they close with what
            // is near, go to the help of friends, answer calls, horse by the flank: world_tactics.cpp, world_help.cpp). The
            // computer's hand on each squad in a melee did worse than that. Horse stop a charge short and wait to be called.
            const sim::EntityId near = nearestFoe(kEnemy, c, 1e4f);
            const V3 fp = near != sim::kNoEntity ? toV3(world_->units()[near - 1].pos) : wingAt;
            const float gap = V3{fp.x - c.x, fp.y - c.y, 0}.length();
            bool answering = false;
            for (sim::EntityId id : s.members) answering = answering || (world_->units()[id - 1].alive() && world_->units()[id - 1].helping != sim::kNoEntity);
            if (answering) continue;  // (gone to a call: left to it)
            const float close = sim::unitStats(s.type).mounted ? 60.0f : 20.0f;
            if (gap > 120.0f || aiShattered(s)) aiFight(q, wingAt, c, 1e4f);  // (far off: the march as ever, massed; shattered: it falls back to mend)
            else if (gap <= close) aiHalt(q);
            else aiSteer(q, gap < 70.0f ? c + (fp - c) * ((gap - close + 4.0f) / gap) : wingAt);
            continue;
        }
        if (wing && !s.garrison) aiFight(q, wingAt, c, 1e4f);  // (bowmen and the rest: toward the wing, behind the foot, shooting what is in reach)
        else aiFight(q, goal, c, 1e4f);
    }
}

int Battle::outcome() const {
    if (conquest_) return 0;  // (a fight in the conquest is not the end of anything: its own end is Conquest::winner)
    if (mode_) return mode_->outcome();
    bool playerAlive = false, enemyAlive = false;
    for (const sim::Unit& u : world_->units()) {
        if (!u.alive()) continue;
        (u.owner == kPlayer ? playerAlive : enemyAlive) = true;
    }
    if (!enemyAlive) return 1;
    if (!playerAlive) return 2;
    if (brokeOff_) return 1;  // the besiegers are spent and have gone (aiBreakOff): the walls are held
    if (capture_.taken()) return capture_.attacker() == kPlayer ? 1 : 2;  // the town's heart is taken
    if (rout_.called(*world_)) return rout_.quitter() == kPlayer ? 2 : 1;  // a host has quit the field
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
        if (!controllable(mine) || aliveCount(mine) == 0 || roleOf(mine.type) == Role::Siege) continue;
        const V3 from = squadCentre(mine, false);
        const Squad* target = nullptr;
        float best = 1e18f;
        for (const Squad& s : squads_) {
            if (s.owner == local_ || aliveCount(s) == 0) continue;
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
        if (u.alive() && controllable(u)) ours.push_back(u.id);
    answer(Situation::Charge, ours);
}

}  // namespace illyria::game
