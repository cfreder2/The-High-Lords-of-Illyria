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
constexpr float kFormationSpacing = 1.8f;
constexpr float kTapRadiusPx = 30.0f;
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

}  // namespace

Role roleOf(UnitType type) {
    switch (type) {
        case UnitType::Archers: return Role::Ranged;
        case UnitType::DwarfGrenadier: return Role::Bomber;
        case UnitType::Wizards: return Role::Caster;
        case UnitType::Journeyman: return Role::Healer;
        case UnitType::Griffins:
        case UnitType::Pegasi:
        case UnitType::Archons:
        case UnitType::Dragons: return Role::Flier;
        default: return Role::Melee;
    }
}

Battle::Battle(uint64_t seed, Scenario scenario) : scenario_(scenario) {
    sim::WorldConfig cfg;
    cfg.seed = seed;
    cfg.wind = {sim::Fixed::fromRatio(1, 2), -sim::Fixed::fromRatio(1, 2), sim::Fixed::zero()};
    cfg.maxRemains = kMaxRemains;
    // The battle is fought in a full-detail window of the continent: at
    // Oakhaven Ford, or at the neutral town nearest it.
    continent_ = std::make_shared<const sim::Continent>(kRegionSeed);
    sim::Vec3 centre = continent_->crossings().front().pos;
    if (scenario_ == Scenario::Siege) centre = siegeTown(*continent_).pos;
    region_ = std::make_shared<const sim::Region>(sim::Region::fromContinent(*continent_, centre, kTerrainCells));
    // The continent around the battlefield: all of it every 64 m, and a ring
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

    if (scenario_ == Scenario::Siege) setUpSiege();
    else setUpFord();

    const size_t n = world_->units().size();
    deathTick_.assign(n, UINT32_MAX);
    prevUnitPos_.resize(n);
    for (size_t i = 0; i < n; ++i) prevUnitPos_[i] = toV3(world_->units()[i].pos);
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

    // Archers spread along the walk, a pair to each wall, facing out.
    {
        Squad s{"Town Watch", kEnemy, UnitType::Archers, {}};
        s.formation = Formation::Loose;
        s.garrison = true;
        const sim::UnitStats stats = sim::unitStats(UnitType::Archers);
        for (size_t e = 0; e < nc; ++e) {
            const V3 a = local(ring.corners[e]), b = local(ring.corners[(e + 1) % nc]);
            V3 mid = (a + b) * 0.5f;
            V3 facing = V3{mid.x - centre.x, mid.y - centre.y, 0};
            facing = facing * (1.0f / facing.length());
            for (float t : {0.33f, 0.67f}) {
                const V3 p = a + (b - a) * t;
                const sim::EntityId id = world_->spawnUnit(kEnemy, toSim(p), toSim(facing), stats);
                s.members.push_back(id);
                if (squadOfUnit_.size() < id) squadOfUnit_.resize(id, -1);
                squadOfUnit_[id - 1] = static_cast<int>(squads_.size());
            }
            s.post = mid;
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
}

void Battle::spawnSquad(const std::string& name, sim::PlayerId owner, UnitType type, int count, V3 centre,
                        V3 facing) {
    Squad s{name, owner, type, {}};
    // Shield walls and missile troops stand in line; the rest in a box.
    const Role role = roleOf(type);
    s.formation = type == UnitType::Warrior || type == UnitType::HeavyInfantry || type == UnitType::Pikemen || role == Role::Ranged
                      ? Formation::Line
                      : Formation::Box;
    s.facing = facing;
    const sim::UnitStats stats = sim::unitStats(type);
    const float spacing = std::max(kFormationSpacing, static_cast<float>(stats.radius.toDouble()) * 2.6f);
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

    const auto start = std::chrono::steady_clock::now();
    world_->step(pending_);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    stepMs_ = stepMs_ * 0.95 + ms * 0.05;
    pending_.clear();
    collectEvents();
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
    pos.z = heightAt(pos.x, pos.y);
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
            case K::Hit: {
                if (e.hitUnit == sim::kNoEntity) break;
                const size_t index = e.hitUnit - 1;
                const sim::Unit& victim = world_->units()[index];
                if (e.killed) deathTick_[index] = tick;
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

const char* formationName(Formation f) {
    switch (f) {
        case Formation::Line: return "Line";
        case Formation::Box: return "Box";
        case Formation::Wedge: return "Wedge";
        case Formation::Loose: return "Loose";
        case Formation::Column: return "Column";
    }
    return "?";
}

std::vector<V3> formationSlots(Formation f, int count, float spacing) {
    std::vector<V3> slots;
    const auto rows = [&](int perRow, float gap) {
        for (int i = 0; i < count; ++i) {
            const int row = i / perRow, inRow = std::min(perRow, count - row * perRow);
            const float col = static_cast<float>(i % perRow) - (inRow - 1) / 2.0f;
            slots.push_back({col * gap, -row * gap, 0});
        }
    };
    switch (f) {
        case Formation::Line: rows(std::max(1, (count + 1) / 2), spacing); break;  // two ranks
        case Formation::Box: rows(std::max(1, static_cast<int>(std::ceil(std::sqrt(count * 1.4f)))), spacing); break;
        case Formation::Column: rows(2, spacing); break;
        case Formation::Loose:
            // A wide skirmish line, each man a little out of true.
            rows(std::max(1, (count + 1) / 2), spacing * 2.2f);
            for (size_t i = 0; i < slots.size(); ++i) {
                const float h = static_cast<float>((i * 2654435761u) % 1000) / 1000.0f;
                slots[i] = slots[i] + V3{(h - 0.5f) * spacing * 0.6f, (std::fmod(h * 7.0f, 1.0f) - 0.5f) * spacing * 0.8f, 0};
            }
            break;
        case Formation::Wedge:
            // One at the point, two behind, three behind them...
            for (int row = 0, placed = 0; placed < count; ++row) {
                const int inRow = std::min(row + 1, count - placed);
                for (int k = 0; k < inRow; ++k, ++placed)
                    slots.push_back({(static_cast<float>(k) - (inRow - 1) / 2.0f) * spacing, -row * spacing * 0.9f, 0});
            }
            break;
    }
    // Centre the formation on its own centroid.
    V3 mean;
    for (const V3& p : slots) mean = mean + p;
    if (!slots.empty()) mean = mean * (1.0f / static_cast<float>(slots.size()));
    for (V3& p : slots) p = p - mean;
    return slots;
}

void Battle::moveSquad(const Squad& squad, const V3& to) {
    const V3 from = squadCentre(squad, false);
    V3 facing = V3{to.x - from.x, to.y - from.y, 0}.normalized();
    if (facing.length() == 0) facing = squad.facing;
    squad.facing = facing;
    const V3 right{facing.y, -facing.x, 0};
    std::vector<sim::EntityId> alive;
    for (sim::EntityId id : squad.members)
        if (world_->units()[id - 1].alive()) alive.push_back(id);
    if (alive.empty()) return;
    const float spacing =
        std::max(kFormationSpacing, static_cast<float>(sim::unitStats(squad.type).radius.toDouble()) * 2.6f);
    std::vector<V3> slots = formationSlots(squad.formation, static_cast<int>(alive.size()), spacing);
    // Who goes where: the soldiers nearest the front take the front ranks, and
    // within a rank keep their left-to-right order, so nobody crosses another.
    const auto pos = [&](sim::EntityId id) { return toV3(world_->units()[id - 1].pos); };
    std::vector<size_t> order(slots.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return slots[a].y != slots[b].y ? slots[a].y > slots[b].y : slots[a].x < slots[b].x;
    });
    std::stable_sort(alive.begin(), alive.end(), [&](sim::EntityId a, sim::EntityId b) {
        return (pos(a) - from).dot(facing) > (pos(b) - from).dot(facing);
    });
    std::vector<std::pair<sim::EntityId, V3>> plan;
    for (size_t start = 0; start < order.size();) {
        size_t end = start;
        while (end < order.size() && slots[order[end]].y == slots[order[start]].y) ++end;
        std::vector<sim::EntityId> rank(alive.begin() + static_cast<long>(start), alive.begin() + static_cast<long>(end));
        std::stable_sort(rank.begin(), rank.end(), [&](sim::EntityId a, sim::EntityId b) {
            return (pos(a) - from).dot(right) < (pos(b) - from).dot(right);
        });
        for (size_t k = start; k < end; ++k) {
            const V3 slot = slots[order[k]];
            plan.push_back({rank[k - start], to + right * slot.x + facing * slot.y});
        }
        start = end;
    }
    // Everyone arrives together at the slowest member's pace: the farthest
    // walks at that pace, the others slower in proportion.
    float slowest = 1e9f, farthest = 0;
    for (const auto& [id, target] : plan) {
        slowest = std::min(slowest, static_cast<float>(world_->units()[id - 1].stats.moveSpeed.toDouble()));
        farthest = std::max(farthest, (target - pos(id)).length());
    }
    for (const auto& [id, target] : plan) {
        const float share = farthest > 0 ? std::clamp((target - pos(id)).length() / farthest, 0.35f, 1.0f) : 1.0f;
        sim::Command c{squad.owner, id, sim::Command::Type::Move, toSim(target)};
        c.speed = sim::Fixed::fromRatio(static_cast<int64_t>(slowest * share * 0.95f * 1000.0f), 1000);
        pending_.push_back(c);
    }
}

void Battle::cycleFormation() {
    if (selected_ < 0) return;
    Squad& s = squads_[static_cast<size_t>(selected_)];
    s.formation = static_cast<Formation>((static_cast<int>(s.formation) + 1) % 5);
    moveSquad(s, squadCentre(s, false));
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
        for (sim::EntityId id : s.members) issue(kEnemy, id, sim::Command::Type::Stop, {});
    };

    for (const Squad& s : squads_) {
        if (s.owner != kEnemy || aliveCount(s) == 0) continue;
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
            if (friendDist < 1e30f && friendDist > 15.0f) moveSquad(s, friendPos + V3{10, 0, 0});
            continue;
        }
        const V3 goal = nearest(c, kPlayer, dist);
        if (dist == 1e30f) return;
        if (s.garrison) {
            // Hold the town: archers keep their places on the walk (they shoot
            // on their own); the rest meet whoever gets near their post, and
            // otherwise return to it.
            if (role == Role::Ranged) continue;
            float near;
            const V3 threat = nearest(s.post, kPlayer, near);
            if (near < 45.0f) moveSquad(s, threat);
            else if ((c - s.post).length() > 6.0f) moveSquad(s, s.post);
            continue;
        }
        const sim::UnitStats stats = sim::unitStats(s.type);
        const float range = static_cast<float>(stats.ranged.autoRange.toDouble());
        if (range > 0 && role != Role::Flier) {
            const float keep = range * 0.8f;
            if (dist > keep) moveSquad(s, c + (goal - c) * ((dist - keep + 4) / dist));
            else stop(s);
        } else if (dist > 15.0f) {
            moveSquad(s, goal);
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
    return 0;
}

V3 Battle::unitPosition(size_t index) const {
    return lerp(prevUnitPos_[index], toV3(world_->units()[index].pos), alpha());
}

bool Battle::unitMoving(size_t index) const {
    const V3 d = toV3(world_->units()[index].pos) - prevUnitPos_[index];
    return d.x * d.x + d.y * d.y > 1e-6f;
}

void Battle::selectSquad(int index) {
    if (index >= 0 && index < static_cast<int>(squads_.size()) && squads_[index].owner == kPlayer) selected_ = index;
}

void Battle::stopSelected() {
    if (selected_ < 0) return;
    for (sim::EntityId id : squads_[selected_].members) issue(kPlayer, id, sim::Command::Type::Stop, {});
}

void Battle::tap(const Camera& camera, float x, float y, float pixelScale) {
    // Nearest living soldier under the finger.
    int bestUnit = -1;
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
            bestUnit = static_cast<int>(i);
        }
    }
    // Zoomed far out, soldiers are specks: fall back to the nearest squad centre.
    int bestSquad = bestUnit >= 0 ? squadOfUnit_[bestUnit] : -1;
    if (bestSquad < 0) {
        float bestS = kSquadTapRadiusPx * pixelScale * kSquadTapRadiusPx * pixelScale;
        for (size_t s = 0; s < squads_.size(); ++s) {
            if (aliveCount(squads_[s]) == 0) continue;
            float px, py;
            if (!camera.project(squadCentre(squads_[s]), px, py)) continue;
            const float d2 = (px - x) * (px - x) + (py - y) * (py - y);
            if (d2 < bestS) {
                bestS = d2;
                bestSquad = static_cast<int>(s);
            }
        }
    }

    if (bestSquad >= 0 && squads_[bestSquad].owner == kPlayer) {
        selected_ = bestSquad;
        return;
    }
    if (selected_ < 0 || aliveCount(squads_[selected_]) == 0) return;
    const Squad& mine = squads_[selected_];
    const bool shoots = sim::unitStats(mine.type).ranged.muzzleSpeed > sim::Fixed::zero();

    if (bestSquad >= 0) {
        // Tapped an enemy: missile troops loose on that spot, everyone else charges it.
        V3 at = bestUnit >= 0 ? unitPosition(bestUnit) : squadCentre(squads_[bestSquad]);
        if (shoots) {
            at.z = heightAt(at.x, at.y);
            for (sim::EntityId id : mine.members) issue(kPlayer, id, sim::Command::Type::AttackGround, at);
        } else {
            moveSquad(mine, at);
        }
        return;
    }
    V3 ground;
    if (camera.pickGround(x, y, ground)) moveSquad(mine, ground);
}

}  // namespace illyria::game
