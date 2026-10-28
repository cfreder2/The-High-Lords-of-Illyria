// Castles in battle: walls keep soldiers out except at the gate, stairs lead
// up to the wall-walk, and defenders up there shelter behind the merlons.
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
Fixed m(int64_t v) { return Fixed::fromInt(v); }

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
// The first neutral town (not a capital), and a 1 km battle window on it.
const Continent::City& town() { return continent().cities()[Continent::kFactionCount]; }
std::shared_ptr<const Region> siege() {
    static const auto r = std::make_shared<const Region>(Region::fromContinent(continent(), town().pos, 256));
    return r;
}
Vec3 local(const Vec3& p) { return {p.x - siege()->origin().x, p.y - siege()->origin().y, p.z}; }

// Inside any wall slab (below its top)?
bool inWall(const Region& r, const Unit& u) {
    for (const WallSeg& w : r.walls()) {
        if (u.pos.z > w.top - m(2)) continue;
        const Vec3 ab = (w.b - w.a).flat();
        const Fixed len2 = ab.lengthSq();
        const Fixed t = clamp((u.pos - w.a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one());
        if ((u.pos - (w.a + ab * t)).flat().length() < w.halfThick - Fixed::fromRatio(1, 5)) return true;
    }
    return false;
}
// The town's own (outer) gate.
size_t townGate(const Region& r) {
    size_t best = 0;
    Fixed bestD = m(100000);
    for (size_t i = 0; i < r.gates().size(); ++i) {
        const Fixed d = ((r.gates()[i].a + r.gates()[i].b) * Fixed::fromRatio(1, 2) - local(town().pos)).flat().length();
        if (d < bestD) {
            bestD = d;
            best = i;
        }
    }
    return best;
}
}  // namespace

TEST(a_town_has_walls_a_gate_stairs_and_a_walk) {
    const Region& r = *siege();
    CHECK(!r.walls().empty());
    CHECK(r.ramparts().size() == 1);
    CHECK(r.ramparts().front().stairs.size() == 3);
    int towers = 0, houses = 0;
    for (const Obstacle& o : r.obstacles()) {
        towers += o.kind == Obstacle::Kind::Tower;
        houses += o.kind == Obstacle::Kind::House;
    }
    CHECK(towers >= 6);
    CHECK(houses >= 10);
}

TEST(archers_climb_the_stairs_and_man_the_walls) {
    WorldConfig cfg;
    cfg.region = siege();
    World w(cfg);
    const Continent::Castle& k = town().castle;
    const Vec3 yard = local(k.courtyard);
    // A spot on the walk, midway along the wall opposite the gate.
    const Continent::Castle::Ring& ring = k.rings.front();
    const size_t back = (ring.gateEdge + ring.corners.size() / 2) % ring.corners.size();
    const Vec3 a = local(ring.corners[back]), b = local(ring.corners[(back + 1) % ring.corners.size()]);
    const Vec3 post{(a.x + b.x) / 2, (a.y + b.y) / 2, m(0)};
    const EntityId id = w.spawnUnit(0, yard, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::Archers));
    const Command go{0, id, Command::Type::Move, post};
    w.step(std::span(&go, 1));
    bool arrived = false, fell = false;
    for (int t = 0; t < 120 * kTicksPerSecond && !arrived; ++t) {
        w.step({});
        const Unit* u = w.findUnit(id);
        arrived = (u->pos - post).flat().length() < m(2) && u->pos.z > town().pos.z + ring.height - m(2);
        fell = fell || inWall(*cfg.region, *u);
    }
    CHECK(arrived);
    CHECK(!fell);
}

TEST(walls_keep_soldiers_out_but_the_gate_lets_them_in) {
    WorldConfig cfg;
    cfg.region = siege();
    World w(cfg);
    const Continent::Castle& k = town().castle;
    const Vec3 yard = local(k.courtyard), centre = local(town().pos);
    // Start well outside, behind the castle from the gate, and head for the courtyard.
    const Vec3 away = (centre - yard).flat() / (centre - yard).flat().length();
    const Vec3 start = centre + away * (town().radius + m(60));
    const EntityId id = w.spawnUnit(0, start, -away, unitStats(UnitType::LightInfantry));
    const Command go{0, id, Command::Type::Move, yard};
    w.step(std::span(&go, 1));
    // The town's gate: shut, it holds him (he hacks at it, slowly); broken, he walks in.
    const size_t gate = townGate(*cfg.region);
    bool inside = false, breached = false;
    for (int t = 0; t < 240 * kTicksPerSecond && !inside; ++t) {
        w.step({});
        const Unit* u = w.findUnit(id);
        inside = (u->pos - yard).flat().length() < m(4);
        breached = breached || inWall(*cfg.region, *u);
    }
    CHECK(!inside);
    CHECK(w.gates()[gate].hp < cfg.region->gates()[gate].maxHp);
    CHECK(!w.gates()[gate].broken());
    w.setGateHp(gate, 0);
    for (int t = 0; t < 240 * kTicksPerSecond && !inside; ++t) {
        w.step({});
        const Unit* u = w.findUnit(id);
        inside = (u->pos - yard).flat().length() < m(4);
        breached = breached || inWall(*cfg.region, *u);
    }
    CHECK(inside);
    CHECK(!breached);
}

TEST(merlons_shelter_defenders_from_arrows_shot_from_outside) {
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 7;
    World w(cfg);
    const Continent::Castle::Ring& ring = town().castle.rings.front();
    const size_t e = (ring.gateEdge + 1) % ring.corners.size();
    const Vec3 a = local(ring.corners[e]), b = local(ring.corners[(e + 1) % ring.corners.size()]);
    const Vec3 mid = (a + b) * Fixed::fromRatio(1, 2), centre = local(town().pos);
    const Vec3 out = (mid - centre).flat() / (mid - centre).flat().length();
    // A defender placed on the walk (it walks the stairs in play; here it starts there).
    // Archers have no shields: whatever stops a missile here is the stone.
    const EntityId target = w.spawnUnit(1, {mid.x, mid.y, m(0)}, out, unitStats(UnitType::Archers));
    // Six archers 40 m out, with a clear shot (no tree in the way).
    for (int i = 0; i < 6; ++i) w.spawnUnit(0, mid + out * m(40) + Vec3{m(i % 3) * 2, m(i / 3) * 2, m(0)}, -out, unitStats(UnitType::Archers));
    int blocked = 0, hits = 0;
    for (int t = 0; t < 120 * kTicksPerSecond && w.findUnit(target)->alive(); ++t) {
        w.step({});
        for (const ImpactEvent& ev : w.impacts()) {
            if (ev.hitUnit != target) continue;
            blocked += ev.kind == ImpactEvent::Kind::Blocked;
            hits += ev.kind == ImpactEvent::Kind::Hit;
        }
    }
    std::printf("  on the walk: %d arrows stopped by merlons, %d hits\n", blocked, hits);
    CHECK(w.findUnit(target)->pos.z > town().pos.z + ring.height - m(2));  // it really stood on the walk
    CHECK(blocked >= 2 && blocked * 3 >= hits);
}

TEST(a_neutral_garrison_mans_its_walls_and_holds_them) {
    illyria::game::Battle b(1, illyria::game::Scenario::Siege);
    const Continent::City& t = illyria::game::siegeTown(b.continent());
    CHECK(t.owner == Continent::kNeutral);
    const Fixed walk = t.pos.z + t.castle.rings.front().height - m(2);
    const auto onWalls = [&] {
        int n = 0;
        for (const illyria::game::Squad& s : b.squads())
            if (s.name == "Town Watch")
                for (EntityId id : s.members) n += b.world().findUnit(id)->pos.z > walk;
        return n;
    };
    const int start = onWalls();
    CHECK(start >= 8);
    b.advance(30.0);
    CHECK(onWalls() == start);
}

namespace {
// A world on the town with its gate held by side 1, and where its doors stand.
struct GateField {
    std::unique_ptr<World> w;
    size_t gate = 0;
    Vec3 centre, out;  // the gate's middle, and the way out of the town
};
GateField gateField(uint64_t seed) {
    GateField f;
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = seed;
    f.w = std::make_unique<World>(cfg);
    f.gate = townGate(*cfg.region);
    f.w->setGateHolder(f.gate, 1);
    const Gate& g = cfg.region->gates()[f.gate];
    f.centre = (g.a + g.b) * Fixed::fromRatio(1, 2);
    f.out = g.out;
    return f;
}
}  // namespace

TEST(soldiers_hack_a_shut_gate_down_and_march_in) {
    GateField f = gateField(3);
    World& w = *f.w;
    const Vec3 yard = local(town().castle.courtyard);
    std::vector<EntityId> legion;
    for (int i = 0; i < 10; ++i)
        legion.push_back(w.spawnUnit(0, f.centre + f.out * m(14) + Vec3{m(i % 5 - 2) * 2, m(i / 5) * 2, m(0)}, -f.out, unitStats(UnitType::Dwarves)));
    std::vector<Command> go;
    for (EntityId id : legion) go.push_back({0, id, Command::Type::Move, yard});
    w.step(go);
    int blows = 0;
    bool kept = true;
    for (int t = 0; t < 400 * kTicksPerSecond && !w.gates()[f.gate].broken(); ++t) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) blows += e.kind == ImpactEvent::Kind::Gate;
        // Nobody gets through while it stands.
        for (EntityId id : legion) kept = kept && (w.findUnit(id)->pos - yard).flat().length() > m(20);
    }
    const uint32_t brokeAt = w.tick();
    std::printf("  %d blows broke the gate in %u s\n", blows, brokeAt / kTicksPerSecond);
    CHECK(kept);
    CHECK(w.gates()[f.gate].broken());
    CHECK(brokeAt > 40 * kTicksPerSecond);  // slow work for swords and axes
    int inside = 0;
    for (int t = 0; t < 90 * kTicksPerSecond; ++t) w.step({});
    for (EntityId id : legion) inside += (w.findUnit(id)->pos - yard).flat().length() < m(12);
    CHECK(inside >= 6);
}

TEST(catapult_stones_break_a_gate_and_the_way_is_open) {
    GateField f = gateField(5);
    World& w = *f.w;
    const EntityId engine = w.spawnUnit(0, f.centre + f.out * m(110), -f.out, unitStats(UnitType::Catapult));
    const Command shoot{0, engine, Command::Type::AttackGate, f.centre};
    w.step(std::span(&shoot, 1));
    CHECK(w.findUnit(engine)->attackGate == static_cast<int32_t>(f.gate));
    int stones = 0, smashes = 0;
    for (int t = 0; t < 300 * kTicksPerSecond && !w.gates()[f.gate].broken(); ++t) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) smashes += e.kind == ImpactEvent::Kind::Smash;
        for (const Projectile& p : w.projectiles()) stones += p.kind == ProjectileKind::Stone && p.ageTicks == 1;
    }
    std::printf("  %d stones thrown, %d struck, gate down at %u s\n", stones, smashes, w.tick() / kTicksPerSecond);
    CHECK(w.gates()[f.gate].broken());
    CHECK(stones >= 2 && stones <= 14);
    // It stops throwing at a gate that is gone.
    CHECK(!w.findUnit(engine)->hasAttackTarget);
    // And the way in is open.
    const Vec3 yard = local(town().castle.courtyard);
    const EntityId man = w.spawnUnit(0, f.centre + f.out * m(30), -f.out, unitStats(UnitType::LightInfantry));
    const Command go{0, man, Command::Type::Move, yard};
    w.step(std::span(&go, 1));
    bool inside = false;
    for (int t = 0; t < 90 * kTicksPerSecond && !inside; ++t) {
        w.step({});
        inside = (w.findUnit(man)->pos - yard).flat().length() < m(4);
    }
    CHECK(inside);
}

TEST(catapult_stones_kill_defenders_on_the_walls) {
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 11;
    World w(cfg);
    const Continent::Castle::Ring& ring = town().castle.rings.front();
    const size_t e = (ring.gateEdge + 1) % ring.corners.size();
    const Vec3 a = local(ring.corners[e]), b = local(ring.corners[(e + 1) % ring.corners.size()]);
    const Vec3 mid = (a + b) * Fixed::fromRatio(1, 2), centre = local(town().pos);
    const Vec3 out = (mid - centre).flat() / (mid - centre).flat().length();
    const Vec3 along = (b - a).flat() / (b - a).flat().length();
    std::vector<EntityId> watch;
    for (int i = 0; i < 5; ++i) watch.push_back(w.spawnUnit(1, mid + along * m(i * 2 - 4), out, unitStats(UnitType::Archers)));
    const Fixed walk = w.findUnit(watch[0])->pos.z;
    w.spawnUnit(0, mid + out * m(120), -out, unitStats(UnitType::Catapult));
    for (int t = 0; t < 150 * kTicksPerSecond; ++t) w.step({});
    int hurt = 0;
    for (EntityId id : watch) hurt += w.findUnit(id)->hp < unitStats(UnitType::Archers).maxHp;
    std::printf("  %d of 5 on the wall hurt or killed\n", hurt);
    CHECK(walk > town().pos.z + ring.height - m(2));  // they stood on the walk
    CHECK(hurt >= 2);
}

TEST(siege_with_engines_is_deterministic_and_pinned) {
    const auto run = [](uint64_t seed) {
        GateField f = gateField(seed);
        World& w = *f.w;
        std::vector<Command> orders;
        for (int i = 0; i < 2; ++i) {
            const EntityId c = w.spawnUnit(0, f.centre + f.out * m(100) + Vec3{m(i * 6), m(0), m(0)}, -f.out, unitStats(UnitType::Catapult));
            orders.push_back({0, c, Command::Type::AttackGate, f.centre});
        }
        for (int i = 0; i < 8; ++i) {
            const EntityId s = w.spawnUnit(0, f.centre + f.out * m(30) + Vec3{m(i % 4) * 2, m(i / 4) * 2, m(0)}, -f.out, unitStats(UnitType::Warrior));
            orders.push_back({0, s, Command::Type::Move, local(town().castle.courtyard)});
        }
        for (int i = 0; i < 6; ++i) w.spawnUnit(1, f.centre - f.out * m(10) + Vec3{m(i % 3) * 2, m(i / 3) * 2, m(0)}, f.out, unitStats(UnitType::Pikemen));
        w.step(orders);
        for (int t = 0; t < 120 * kTicksPerSecond; ++t) w.step({});
        return w.checksum();
    };
    const uint64_t a = run(21);
    CHECK(a == run(21));
    CHECK(a != run(22));
    constexpr uint64_t kGolden = 0xb03b78723eddc04c;
    if (a != kGolden) std::printf("  Siege checksum: 0x%016llx\n", static_cast<unsigned long long>(a));
    CHECK(a == kGolden);
}

TEST(the_siege_train_batters_the_gate_when_ordered) {
    illyria::game::Battle b(1, illyria::game::Scenario::Siege);
    int train = -1;
    for (size_t i = 0; i < b.squads().size(); ++i)
        if (b.squads()[i].type == UnitType::Catapult && b.squads()[i].owner == illyria::game::Battle::kPlayer) train = static_cast<int>(i);
    CHECK(train >= 0);
    // The town's gate: the one nearest the siege train.
    const illyria::game::V3 at = b.squadCentre(b.squads()[static_cast<size_t>(train)], false);
    int gate = -1;
    float best = 1e9f;
    for (size_t i = 0; i < b.region().gates().size(); ++i) {
        const float d = (illyria::game::toV3(b.region().gates()[i].a) - at).length();
        if (d < best) best = d, gate = static_cast<int>(i);
    }
    const size_t g = static_cast<size_t>(gate);
    CHECK(b.world().gates()[g].holder == illyria::game::Battle::kEnemy);
    b.selectSquad(train);
    b.orderAtGate(gate);
    for (int i = 0; i < 300; ++i) b.advance(0.1);
    std::printf("  gate after 30 s of the siege train: %d hp (%.0f m away)\n", b.world().gates()[g].hp, best);
    CHECK(b.world().gates()[g].hp < b.region().gates()[g].maxHp);
}
