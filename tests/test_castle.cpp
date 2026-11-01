// Castles in battle: walls keep soldiers out except at the gate, ramps lead
// up to the wall-walk, and gates hold until broken (the parapet: test_walls.cpp).
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
// The first neutral town (not a capital) with no other town's walls in its
// 1 km battle window (corners included), and the window on it.
const Continent::City& town() {
    static const Continent::City* chosen = [] {
        const auto& cs = continent().cities();
        for (size_t i = Continent::kFactionCount; i < cs.size(); ++i) {
            bool alone = true;
            for (size_t k = 0; k < cs.size(); ++k)
                alone = alone && (k == i || (cs[k].pos - cs[i].pos).flat().length() > Fixed::fromInt(1100));
            if (alone) return &cs[i];
        }
        return &cs[Continent::kFactionCount];
    }();
    return *chosen;
}
std::shared_ptr<const Region> siege() {
    static const auto r = std::make_shared<const Region>(Region::fromContinent(continent(), town().pos, 256));
    return r;
}
Vec3 local(const Vec3& p) { return {p.x - siege()->origin().x, p.y - siege()->origin().y, p.z}; }

// Inside any wall slab (below its walk, above a gate passage's head)?
bool inWall(const Region& r, const Unit& u) {
    for (const WallSeg& w : r.walls()) {
        if (w.walk > Fixed::zero() ? u.pos.z >= w.walk - Fixed::fromRatio(1, 2) : u.pos.z > w.top - m(2)) continue;
        if (u.pos.z + u.stats.height <= w.a.z) continue;
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

TEST(a_town_has_walls_a_gate_ramps_and_a_walk) {
    const Region& r = *siege();
    CHECK(!r.walls().empty());
    CHECK(r.ramparts().size() == 1);
    CHECK(r.ramparts().front().ramps.size() == 3);
    int towers = 0, houses = 0;
    for (const Obstacle& o : r.obstacles()) {
        towers += o.kind == Obstacle::Kind::Tower;
        houses += o.kind == Obstacle::Kind::House;
    }
    CHECK(towers >= 6);
    CHECK(houses >= 10);
}

TEST(archers_climb_the_ramps_and_man_the_walls) {
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
    Vec3 away = (centre - yard).flat() / (centre - yard).flat().length();
    Vec3 start = centre + away * (town().radius + m(60));
    // On open ground (not a creek's bridge or the water), turning round the town if need be.
    for (int k = 0; k < 12 && cfg.region->groundAt(start.x, start.y) != Ground::Plains; ++k) {
        const Fixed c = Fixed::fromRatio(866, 1000), sn = Fixed::fromRatio(1, 2);  // 30 degrees
        away = Vec3{away.x * c - away.y * sn, away.x * sn + away.y * c, Fixed::zero()};
        start = centre + away * (town().radius + m(60));
    }
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

// How the parapet shelters defenders from arrows: tests/test_walls.cpp.

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
    constexpr uint64_t kGolden = 0xfda8884c0f8ddd66;
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

TEST(archers_shoot_over_a_wall_not_into_it) {
    // Archers in the field, men behind the town wall: the flat shot would
    // strike the stone, so they lob over it (blind, and so more scattered).
    WorldConfig cfg;
    cfg.region = siege();
    cfg.seed = 5;
    World w(cfg);
    const Region& r = *cfg.region;
    const Continent::Castle::Ring& ring = town().castle.rings.front();
    const size_t e = (ring.gateEdge + 1) % ring.corners.size();
    const Vec3 a = local(ring.corners[e]), b = local(ring.corners[(e + 1) % ring.corners.size()]);
    const Vec3 mid = (a + b) * Fixed::fromRatio(1, 2), centre = local(town().pos);
    const Vec3 out = (mid - centre).flat() / (mid - centre).flat().length();
    const Vec3 along = (b - a).flat() / (b - a).flat().length();
    for (int i = 0; i < 6; ++i) w.spawnUnit(1, mid - out * m(12) + along * m(i * 2 - 5), out, unitStats(UnitType::Pikemen));
    for (int i = 0; i < 6; ++i) w.spawnUnit(0, mid + out * m(35) + along * m(i * 2 - 5), -out, unitStats(UnitType::Archers));
    for (int t = 0; t < 40 * kTicksPerSecond; ++t) w.step({});
    int inWall = 0, over = 0;
    for (const Remain& rm : w.remains()) {
        if (rm.kind != RemainKind::Arrow) continue;
        if (Gate::inside(r.ramparts().front(), rm.pos)) ++over;
        else if (rm.pos.z > w.groundHeight(rm.pos.x, rm.pos.y) + m(1)) ++inWall;
    }
    std::printf("  arrows over the wall %d, stuck in it %d\n", over, inWall);
    CHECK(over >= 10);
    CHECK(inWall * 5 <= over);
}

namespace {
// Up a ramp (its slope or the landing at its head), a little either side.
bool onRamp(const Rampart& rp, const Vec3& p) {
    for (const auto& [foot, head] : rp.ramps) {
        const Vec3 run = (head - foot).flat();
        const Fixed len = run.length();
        const Vec3 dir = run / len, d = (p - foot).flat();
        const Fixed along = d.dot(dir), across = abs(d.x * dir.y - d.y * dir.x);
        if (along >= -m(1) && along <= len + m(1) && across <= Continent::Castle::kRampHalfWidth + Fixed::fromRatio(1, 2)) return true;
    }
    return false;
}
// The town's ramp farthest from its gate (on the back wall), and the walk
// beside its head (the nearest point of the walk's middle).
struct BackRamp {
    Vec3 foot, head, walk, out;  // out: from the town's middle through that walk
};
BackRamp backRamp() {
    const Region& r = *siege();
    const Rampart& rp = r.ramparts().front();
    const Gate& g = r.gates()[townGate(r)];
    const Vec3 doors = (g.a + g.b) * Fixed::fromRatio(1, 2);
    BackRamp b;
    Fixed far;
    for (const auto& [foot, head] : rp.ramps) {
        if ((head - doors).flat().length() <= far) continue;
        far = (head - doors).flat().length();
        b.foot = foot;
        b.head = head;
    }
    Fixed best = m(100000);
    for (size_t e = 0; e < rp.corners.size(); ++e) {
        const Vec3 a = rp.corners[e], ab = (rp.corners[(e + 1) % rp.corners.size()] - a).flat();
        const Fixed t = clamp((b.head - a).flat().dot(ab) / ab.lengthSq(), Fixed::zero(), Fixed::one());
        const Vec3 p = a + ab * t;
        if ((p - b.head).flat().length() >= best) continue;
        best = (p - b.head).flat().length();
        b.walk = p;
    }
    const Vec3 c = local(town().pos);
    b.out = (b.walk - c).flat() / (b.walk - c).flat().length();
    return b;
}
// How a soldier's march went, watched every tick.
struct Trip {
    bool arrived = false;   // at the spot, his order done
    bool leapt = false;     // between the ground and the walk anywhere but up a ramp
    bool walled = false;    // inside a wall's stone
    bool byRamp = false;    // half way up a ramp
    bool byGate = false;    // on the ground at the gate
    Fixed fromDoors;        // at the end, from the middle of the gate's doors
    bool townSide = false;  // ... on the town's side of them
    int seconds = 0;
};
Trip march(World& w, EntityId id, const Vec3& to, int seconds) {
    const Region& r = *siege();
    const Rampart& rp = r.ramparts().front();
    const Gate& g = r.gates()[townGate(r)];
    const Vec3 doors = (g.a + g.b) * Fixed::fromRatio(1, 2);
    const Fixed walk = rp.corners.front().z;
    Trip trip;
    const Command go{w.findUnit(id)->owner, id, Command::Type::Move, to};
    w.step(std::span(&go, 1));
    Fixed was = w.findUnit(id)->pos.z;
    int t = 0;
    for (; t < seconds * kTicksPerSecond && w.findUnit(id)->hasMoveTarget; ++t) {
        w.step({});
        const Unit& u = *w.findUnit(id);
        const Fixed above = u.pos.z - r.terrain().heightAt(u.pos.x, u.pos.y);
        const bool ramp = onRamp(rp, u.pos);
        // Between the ground and the walk only on a ramp, and never a fall.
        if ((above > m(1) && u.pos.z < walk - Fixed::fromRatio(1, 2) && !ramp) || abs(u.pos.z - was) > Fixed::one()) trip.leapt = true;
        was = u.pos.z;
        trip.byRamp = trip.byRamp || (ramp && above > m(2) && u.pos.z < walk - m(2));
        trip.byGate = trip.byGate || (above < m(1) && (u.pos - doors).flat().length() < m(5));
        trip.walled = trip.walled || inWall(r, u);
    }
    const Unit& u = *w.findUnit(id);
    trip.arrived = !u.hasMoveTarget && (u.pos - to).flat().length() < Fixed::fromRatio(3, 2);
    trip.fromDoors = (u.pos - doors).flat().length();
    trip.townSide = (u.pos - g.a).flat().dot(g.out) < Fixed::zero();
    trip.seconds = t / kTicksPerSecond;
    return trip;
}
}  // namespace

TEST(from_the_walk_a_soldier_goes_down_a_ramp_and_out_by_the_gate) {
    // A man on the back wall, beside the head of a ramp, sent to the field
    // behind the wall: not off the parapet, but along the walk, down the
    // ramp, across the courtyard, out of the (broken) gate and round. From
    // the ramp's landing and from half way down it, the same.
    const BackRamp b = backRamp();
    const Vec3 field = b.walk + b.out * m(25);
    const Vec3 starts[3] = {b.walk, b.head, (b.foot + b.head) * Fixed::fromRatio(1, 2)};
    const char* names[3] = {"walk", "landing", "ramp"};
    for (int i = 0; i < 3; ++i) {
        WorldConfig cfg;
        cfg.region = siege();
        World w(cfg);
        w.setGateHolder(townGate(*cfg.region), 1);
        w.setGateHp(townGate(*cfg.region), 0);
        const EntityId id = w.spawnUnit(0, starts[i], -b.out, unitStats(UnitType::LightInfantry));
        const Trip trip = march(w, id, {field.x, field.y, Fixed::zero()}, 300);
        std::printf("  from the %s: %s in %d s%s%s%s\n", names[i], trip.arrived ? "there" : "not there", trip.seconds, trip.byRamp ? ", down the ramp" : "",
                    trip.byGate ? ", out by the gate" : "", trip.leapt ? ", OFF THE WALL" : "");
        CHECK(trip.arrived);
        CHECK(!trip.leapt);
        CHECK(!trip.walled);
        CHECK(trip.byRamp);
        CHECK(trip.byGate);
    }
}

TEST(from_the_field_a_soldier_goes_in_by_the_gate_and_up_a_ramp) {
    // The other way: from behind the town, in at the (broken) gate, up the
    // ramp and along the walk; or to the ramp's landing.
    const BackRamp b = backRamp();
    const Vec3 field = b.walk + b.out * m(25);
    const Vec3 ends[2] = {b.walk, b.head};
    const char* names[2] = {"walk", "landing"};
    for (int i = 0; i < 2; ++i) {
        WorldConfig cfg;
        cfg.region = siege();
        World w(cfg);
        w.setGateHolder(townGate(*cfg.region), 1);
        w.setGateHp(townGate(*cfg.region), 0);
        const EntityId id = w.spawnUnit(0, field, -b.out, unitStats(UnitType::LightInfantry));
        const Trip trip = march(w, id, ends[i], 300);
        std::printf("  to the %s: %s in %d s\n", names[i], trip.arrived ? "there" : "not there", trip.seconds);
        CHECK(trip.arrived);
        CHECK(w.findUnit(id)->pos.z > ends[i].z - Fixed::fromRatio(1, 2));
        CHECK(!trip.leapt);
        CHECK(!trip.walled);
        CHECK(trip.byRamp);
        CHECK(trip.byGate);
    }
}

TEST(nobody_passes_a_shut_gate_and_its_own_side_stop_at_it) {
    // The gate shut. Its defenders sent from the walk to the field go down a
    // ramp and as near as they can get: to the inside of the gate, where they
    // stop (and leave the doors alone). An enemy who got up there hacks his
    // way out instead.
    const BackRamp b = backRamp();
    const Vec3 field = b.walk + b.out * m(25);
    WorldConfig cfg;
    cfg.region = siege();
    World w(cfg);
    const size_t gate = townGate(*cfg.region);
    w.setGateHolder(gate, 1);
    const Vec3 along{-b.out.y, b.out.x, Fixed::zero()};
    std::vector<EntityId> watch;
    for (int i = 0; i < 6; ++i) watch.push_back(w.spawnUnit(1, b.walk + along * m(i * 2 - 5), -b.out, unitStats(UnitType::Pikemen)));
    std::vector<Command> go;
    for (EntityId id : watch) go.push_back({1, id, Command::Type::Move, {field.x, field.y, Fixed::zero()}});
    w.step(go);
    for (int t = 0; t < 200 * kTicksPerSecond; ++t) w.step({});
    const Gate& g = cfg.region->gates()[gate];
    const Vec3 doors = (g.a + g.b) * Fixed::fromRatio(1, 2);
    int stopped = 0;
    for (EntityId id : watch) {
        const Unit& u = *w.findUnit(id);
        const bool there = !u.hasMoveTarget && (u.pos - doors).flat().length() < m(14) && (u.pos - g.a).flat().dot(g.out) < Fixed::zero() &&
                           u.pos.z < town().pos.z + m(1);
        stopped += there;
        if (!there)
            std::printf("  defender at (%.1f, %.1f, +%.1f) %s\n", u.pos.x.toDouble(), u.pos.y.toDouble(), (u.pos.z - town().pos.z).toDouble(),
                        u.hasMoveTarget ? "marching" : "halted");
        CHECK(!inWall(*cfg.region, u));
    }
    std::printf("  %d of 6 defenders stopped inside their shut gate\n", stopped);
    CHECK(stopped == 6);
    CHECK(w.gates()[gate].hp == g.maxHp);
    // One of the enemy on the walk (the defenders elsewhere), sent the same
    // way: down the ramp, and at the doors from inside.
    World w2(cfg);
    w2.setGateHolder(gate, 1);
    const EntityId foe = w2.spawnUnit(0, b.walk, -b.out, unitStats(UnitType::Dwarves));
    const Trip trip = march(w2, foe, {field.x, field.y, Fixed::zero()}, 150);
    std::printf("  the enemy: %.1f m from the doors, the gate at %d of %d\n", trip.fromDoors.toDouble(), w2.gates()[gate].hp, g.maxHp);
    CHECK(!trip.leapt);
    CHECK(trip.byRamp);
    CHECK(trip.townSide);
    CHECK(trip.fromDoors < m(4));
    CHECK(w2.gates()[gate].hp < g.maxHp);
}
