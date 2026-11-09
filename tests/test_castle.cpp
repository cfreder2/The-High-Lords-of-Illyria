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
// The neutral town (not a capital) with the most open fields round its
// walls, out to where engines stand (the towns stand close together: its
// battle window holds some of theirs too), and the window on it.
const Continent::City& town() {
    static const Continent::City* chosen = [] {
        const auto& cs = continent().cities();
        const Continent::City* best = &cs[Continent::kFactionCount];
        int most = -1;
        for (size_t i = Continent::kFactionCount; i < cs.size(); ++i) {
            int open = 0;
            for (int a = 0; a < 24; ++a) {
                const Fixed angle = kTwoPi * a / 24;
                for (int64_t d = 40; d <= 130; d += 30) {
                    const Vec3 p = cs[i].pos + Vec3{cos(angle), sin(angle), Fixed::zero()} * (cs[i].radius + Fixed::fromInt(d));
                    bool clear = true;
                    for (size_t k = 0; k < cs.size(); ++k)
                        clear = clear && (k == i || (cs[k].pos - p).flat().length() > cs[k].radius + Fixed::fromInt(15));
                    const Ground g = continent().groundAt(p.x, p.y);
                    open += clear && (g == Ground::Plains || g == Ground::Road || g == Ground::Mud || g == Ground::Sand);
                }
            }
            if (open > most) {
                most = open;
                best = &cs[i];
            }
        }
        return best;
    }();
    return *chosen;
}
std::shared_ptr<const Region> siege() {
    static const auto r = std::make_shared<const Region>(Region::fromContinent(continent(), town().pos, 256));
    return r;
}
Vec3 local(const Vec3& p) { return {p.x - siege()->origin().x, p.y - siege()->origin().y, p.z}; }
// The town's own walls, among its neighbours' in the window.
const Rampart& ownWalls(const Region& r) {
    const Rampart* best = &r.ramparts().front();
    for (const Rampart& rp : r.ramparts())
        if ((rp.centre - local(town().pos)).flat().length() < (best->centre - local(town().pos)).flat().length()) best = &rp;
    return *best;
}

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
    size_t own = 0;
    for (const Rampart& rp : r.ramparts()) own += (rp.centre - local(town().pos)).flat().length() < Fixed::one();
    CHECK(own == 1);
    CHECK(ownWalls(r).ramps.size() == 3);
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
        for (EntityId id : legion) kept = kept && !Gate::inside(ownWalls(*w.region()), w.findUnit(id)->pos);
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

TEST(giants_hack_a_shut_gate_down_and_walk_through_the_gap) {
    // The biggest walkers (a giant is 5.5 m tall and 2.4 m across) against the
    // town's gate: they can reach it to strike, they break it, and they pass.
    GateField f = gateField(7);
    World& w = *f.w;
    const Gate& g = w.region()->gates()[f.gate];
    const UnitStats giant = unitStats(UnitType::Giants);
    std::printf("  gate %.1f m wide, %.1f m high, %.2f m thick; giant %.1f m high, %.1f m across, reach %.1f m\n", (g.b - g.a).flat().length().toDouble(),
                (g.top - g.a.z).toDouble(), g.halfThick.toDouble() * 2, giant.height.toDouble(), giant.radius.toDouble() * 2, giant.melee.reach.toDouble());
    const Vec3 yard = local(town().castle.courtyard);
    std::vector<EntityId> pack;
    for (int i = 0; i < 3; ++i) pack.push_back(w.spawnUnit(0, f.centre + f.out * m(14) + Vec3{m(i * 4 - 4), m(0), m(0)}, -f.out, giant));
    std::vector<Command> go;
    for (EntityId id : pack) go.push_back({0, id, Command::Type::Move, yard});
    w.step(go);
    int blows = 0;
    Fixed nearest = m(100);
    for (int t = 0; t < 400 * kTicksPerSecond && !w.gates()[f.gate].broken(); ++t) {
        w.step({});
        for (const ImpactEvent& e : w.impacts()) blows += e.kind == ImpactEvent::Kind::Gate;
        for (EntityId id : pack) nearest = min(nearest, (w.findUnit(id)->pos - f.centre).flat().dot(f.out));
    }
    std::printf("  giants broke the gate with %d blows in %u s, the nearest standing %.2f m from its middle\n", blows, w.tick() / kTicksPerSecond, nearest.toDouble());
    CHECK(w.gates()[f.gate].broken());
    CHECK(blows > 0);
    CHECK(nearest > Fixed::zero());  // never through it while it stood
    for (int t = 0; t < 120 * kTicksPerSecond; ++t) w.step({});
    int inside = 0;
    for (EntityId id : pack) inside += (w.findUnit(id)->pos - yard).flat().length() < m(20);
    std::printf("  %d of 3 giants in the courtyard 120 s after\n", inside);
    CHECK(inside >= 2);
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
    const Vec3 centre = local(town().pos);
    // The first wall round from the gate with open fields 120 m out, where
    // the engine stands (towns stand close together, and some by the sea).
    size_t e = (ring.gateEdge + 1) % ring.corners.size();
    for (size_t k = 1; k < ring.corners.size(); ++k) {
        const size_t c = (ring.gateEdge + k) % ring.corners.size();
        const Vec3 m0 = (local(ring.corners[c]) + local(ring.corners[(c + 1) % ring.corners.size()])) * Fixed::fromRatio(1, 2);
        const Vec3 at = m0 + (m0 - centre).flat() / (m0 - centre).flat().length() * m(120);
        bool open = siege()->groundAt(at.x, at.y) == Ground::Plains || siege()->groundAt(at.x, at.y) == Ground::Road;
        for (const Rampart& rp : siege()->ramparts()) open = open && !Gate::inside(rp, at);
        if (open) {
            e = c;
            break;
        }
    }
    const Vec3 a = local(ring.corners[e]), b = local(ring.corners[(e + 1) % ring.corners.size()]);
    const Vec3 mid = (a + b) * Fixed::fromRatio(1, 2);
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
    constexpr uint64_t kGolden = 0xb4bb26218dc4d8bd;  // (was 0x1aa5a57fd79f59bd before every gate's fields were hashed always; was 0x1fddebc5e04c2cad before gates swung: a holder's men wait three seconds at the doors, and do not walk at a foe across a shut gate)
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
        if (Gate::inside(ownWalls(r), rm.pos)) ++over;
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
    Vec3 along;                  // the wall's own way there
};
BackRamp backRamp() {
    const Region& r = *siege();
    const Rampart& rp = ownWalls(r);
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
        b.along = ab / ab.length();
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
    const Rampart& rp = ownWalls(r);
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

TEST(a_gate_opens_to_its_holders_men_and_to_nobody_else) {
    // The gate shut. Its defenders sent from the walk to the field go down a ramp and out through the gate, which
    // swings open for them (and leaves the doors whole), and shuts behind them. An enemy who got up there hacks his way
    // out instead.
    const BackRamp b = backRamp();
    const Vec3 field = b.walk + b.out * m(25);
    WorldConfig cfg;
    cfg.region = siege();
    World w(cfg);
    const size_t gate = townGate(*cfg.region);
    w.setGateHolder(gate, 1);
    std::vector<EntityId> watch;
    for (int i = 0; i < 6; ++i) watch.push_back(w.spawnUnit(1, b.walk + b.along * m(i * 2 - 5), -b.out, unitStats(UnitType::Pikemen)));
    std::vector<Command> go;
    for (EntityId id : watch) go.push_back({1, id, Command::Type::Move, {field.x, field.y, Fixed::zero()}});
    w.step(go);
    bool opened = false;
    for (int t = 0; t < 200 * kTicksPerSecond; ++t) {
        w.step({});
        opened = opened || w.gates()[gate].opened();
    }
    const Gate& g = cfg.region->gates()[gate];
    int out = 0;
    for (EntityId id : watch) {
        const Unit& u = *w.findUnit(id);
        out += (u.pos - field).flat().length() < m(40);  // out of the gate and round the wall to the field (the last stretch of it is rough ground)
        CHECK(!inWall(*cfg.region, u));
    }
    std::printf("  %d of 6 defenders went out through their gate\n", out);
    CHECK(out == 6);
    CHECK(opened);                        // it swung open for them
    CHECK(!w.gates()[gate].opened());       // and shut behind them
    CHECK(w.gates()[gate].hp == g.maxHp);  // and they did not touch the doors
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

namespace {
// Four men of side `owner`, `from` metres from the gate's middle on the `side` (+1 outside, -1 in the town), ordered
// across to the other side.
std::vector<EntityId> crossers(GateField& f, PlayerId owner, int side, int64_t from, UnitType type = UnitType::Pikemen) {
    std::vector<EntityId> ids;
    const Vec3 across{f.out.y, -f.out.x, Fixed::zero()};
    for (int i = 0; i < 4; ++i) {
        const Vec3 at = f.centre + f.out * (m(from) * side) + across * (m(i * 2 - 3));
        ids.push_back(f.w->spawnUnit(owner, {at.x, at.y, f.w->groundHeight(at.x, at.y)}, -f.out * m(side), unitStats(type)));
    }
    return ids;
}
void send(GateField& f, const std::vector<EntityId>& ids, int side, int64_t to) {
    std::vector<Command> go;
    const Vec3 across{f.out.y, -f.out.x, Fixed::zero()};
    for (size_t i = 0; i < ids.size(); ++i) {
        const Vec3 at = f.centre + f.out * (m(to) * side) + across * (m(static_cast<int64_t>(i) * 2 - 3));
        go.push_back({f.w->findUnit(ids[i])->owner, ids[i], Command::Type::Move, {at.x, at.y, Fixed::zero()}});
    }
    f.w->step(go);
}
bool onSide(const GateField& f, EntityId id, int side) { return (f.w->findUnit(id)->pos - f.centre).flat().dot(f.out) * Fixed::fromInt(side) > m(2); }
const GateState& gateOf(const GateField& f) { return f.w->gates()[f.gate]; }
}  // namespace

TEST(a_squad_ordered_through_a_closed_gate_opens_it_passes_and_it_closes_behind) {
    GateField f = gateField(5);
    const auto men = crossers(f, 1, +1, 24);
    CHECK(gateOf(f).mode == GateState::Mode::Shut);
    send(f, men, -1, 14);
    bool opening = false, blockedWhileSwinging = true, open = false;
    int arrived = -1, closedAt = -1;
    for (int t = 0; t < 120 * kTicksPerSecond; ++t) {
        f.w->step({});
        const GateState& g = gateOf(f);
        opening = opening || g.mode == GateState::Mode::Opening;
        if (g.mode == GateState::Mode::Opening || g.mode == GateState::Mode::Closing) blockedWhileSwinging = blockedWhileSwinging && !g.passable();
        open = open || g.opened();
        bool all = true;
        for (EntityId id : men) all = all && onSide(f, id, -1);
        if (all && arrived < 0) arrived = t;
        if (arrived >= 0 && g.mode == GateState::Mode::Shut && closedAt < 0) closedAt = t;
    }
    std::printf("  through at %.1f s, shut again at %.1f s\n", arrived / 30.0, closedAt / 30.0);
    CHECK(opening);
    CHECK(open);
    CHECK(blockedWhileSwinging);  // it blocks while it swings
    CHECK(arrived > 0);           // every man is in the town
    CHECK(closedAt > arrived);    // and it closed after him
    CHECK(gateOf(f).hp == f.w->region()->gates()[f.gate].maxHp);
}

TEST(an_enemy_squad_cannot_pass_a_closed_gate_and_does_not_open_it) {
    GateField f = gateField(6);
    const auto foes = crossers(f, 0, +1, 24);
    send(f, foes, -1, 14);
    bool everOpen = false;
    for (int t = 0; t < 12 * kTicksPerSecond; ++t) {
        f.w->step({});
        everOpen = everOpen || gateOf(f).mode != GateState::Mode::Shut;
    }
    CHECK(!everOpen);
    for (EntityId id : foes) CHECK(!onSide(f, id, -1));
    CHECK(gateOf(f).hp < f.w->region()->gates()[f.gate].maxHp);  // they hacked at it instead
}

TEST(the_holder_pins_his_gate_open_or_shut_and_nobody_else_can) {
    GateField f = gateField(7);
    Command pin{1, kNoEntity, Command::Type::SetGate};
    pin.objectIndex = static_cast<int32_t>(f.gate);
    // A stranger's hand does nothing.
    pin.player = 0;
    pin.gatePin = Command::GatePin::Open;
    f.w->step(std::span(&pin, 1));
    for (int t = 0; t < 5 * kTicksPerSecond; ++t) f.w->step({});
    CHECK(gateOf(f).mode == GateState::Mode::Shut);
    // The holder's hand: it swings open (blocking while it does), and stays so with nobody near it.
    pin.player = 1;
    f.w->step(std::span(&pin, 1));
    f.w->step({});
    CHECK(gateOf(f).mode == GateState::Mode::Opening);
    CHECK(!gateOf(f).passable());
    for (int t = 0; t < 40 * kTicksPerSecond; ++t) f.w->step({});
    CHECK(gateOf(f).mode == GateState::Mode::Open);
    // Pinned shut: it closes, and his own men sent through it stop before it (the order given up).
    pin.gatePin = Command::GatePin::Shut;
    f.w->step(std::span(&pin, 1));
    for (int t = 0; t < 5 * kTicksPerSecond; ++t) f.w->step({});
    CHECK(gateOf(f).mode == GateState::Mode::Shut);
    const auto men = crossers(f, 1, +1, 24);
    send(f, men, -1, 14);
    for (int t = 0; t < 60 * kTicksPerSecond; ++t) f.w->step({});
    CHECK(gateOf(f).mode == GateState::Mode::Shut);
    for (EntityId id : men) CHECK(!onSide(f, id, -1));
    // Let go of it: the gate opens for them as it always does.
    pin.gatePin = Command::GatePin::Auto;
    f.w->step(std::span(&pin, 1));
    send(f, men, -1, 14);
    for (int t = 0; t < 60 * kTicksPerSecond; ++t) f.w->step({});
    for (EntityId id : men) CHECK(onSide(f, id, -1));
}

TEST(the_gates_state_is_in_the_checksum_and_a_replay_agrees) {
    const auto run = [](bool pinOpen) {
        GateField f = gateField(8);
        const auto men = crossers(f, 1, +1, 24);
        send(f, men, -1, 14);
        if (pinOpen) {
            Command pin{1, kNoEntity, Command::Type::SetGate};
            pin.objectIndex = static_cast<int32_t>(f.gate);
            pin.gatePin = Command::GatePin::Open;
            f.w->step(std::span(&pin, 1));
        }
        uint64_t sum = 0;
        for (int t = 0; t < 20 * kTicksPerSecond; ++t) {
            f.w->step({});
            sum = sum * 31 + f.w->checksum();
        }
        return sum;
    };
    CHECK(run(false) == run(false));
    CHECK(run(true) == run(true));
    CHECK(run(true) != run(false));
}

namespace {
struct Keep {
    GateField f;
    std::vector<EntityId> archers, foot, foes;
};
// A garrison of bowmen and pikemen in the courtyard of a garrisoned gate, and foes coming up to it from `range` metres off.
Keep keepField(uint64_t seed, int64_t range, int nFoot = 10, int nFoes = 6) {
    Keep k{gateField(seed), {}, {}, {}};
    World& w = *k.f.w;
    w.setGateGarrisoned(k.f.gate, true);
    const Vec3 yard = local(town().castle.courtyard);
    const Vec3 across{k.f.out.y, -k.f.out.x, Fixed::zero()};
    for (int i = 0; i < 6; ++i) {
        const Vec3 at = yard + across * m(i - 3);
        k.archers.push_back(w.spawnUnit(1, {at.x, at.y, w.groundHeight(at.x, at.y)}, k.f.out, unitStats(UnitType::Archers)));
    }
    for (int i = 0; i < nFoot; ++i) {
        const Vec3 at = yard - k.f.out * m(4) + across * m(i - 5);
        k.foot.push_back(w.spawnUnit(1, {at.x, at.y, w.groundHeight(at.x, at.y)}, k.f.out, unitStats(UnitType::Pikemen)));
    }
    for (int i = 0; i < nFoes; ++i) {
        const Vec3 at = k.f.centre + k.f.out * m(range) + across * m(i % 12 * 2 - 11) + k.f.out * m(i / 12 * 3);
        k.foes.push_back(w.spawnUnit(0, {at.x, at.y, w.groundHeight(at.x, at.y)}, -k.f.out, unitStats(UnitType::Pikemen)));
    }
    return k;
}
}  // namespace

namespace {
// What the doors did when a garrisoned gate was besieged: at the tick they first began to swing, how many of the foot stood
// at their places, how many foes were at the doors outside and inside, and the doors' health.
struct Lure {
    bool swung = false;
    int formedPct = 0, mannedPct = 0, outside = 0, hpPct = 0;
    int foesLeft = 0, footLeft = 0;
};
Lure besiege(Keep& k, int seconds) {
    World& w = *k.f.w;
    std::vector<Command> march;
    for (EntityId id : k.foes) march.push_back({0, id, Command::Type::Move, k.f.centre + k.f.out * m(10)});
    w.step(march);
    const Rampart& walls = ownWalls(*w.region());
    const Fixed walk = walls.corners.front().z;
    Lure lure;
    for (int t = 0; t < seconds * kTicksPerSecond; ++t) {
        w.step({});
        if (lure.swung || gateOf(k.f).mode == GateState::Mode::Shut) continue;
        lure.swung = true;
        int formed = 0;
        for (EntityId id : k.foot) {
            const Unit& u = *w.findUnit(id);
            const Fixed d = (u.pos - k.f.centre).flat().length();
            formed += u.alive() && d > m(4) && d < m(18) && (u.pos - k.f.centre).flat().dot(k.f.out) < -m(3) && u.pos.z < walk - m(2);
        }
        int up = 0;
        for (EntityId id : k.archers) up += w.findUnit(id)->alive() && w.findUnit(id)->pos.z > walk - m(1);
        lure.mannedPct = up * 100 / std::max<int>(1, static_cast<int>(k.archers.size()));
        lure.formedPct = formed * 100 / std::max<int>(1, static_cast<int>(k.foot.size()));
        for (EntityId id : k.foes) {
            const Unit& u = *w.findUnit(id);
            lure.outside += u.alive() && (u.pos - k.f.centre).flat().length() <= m(14) && (u.pos - k.f.centre).flat().dot(k.f.out) > Fixed::zero();
        }
        lure.hpPct = gateOf(k.f).hp * 100 / w.region()->gates()[k.f.gate].maxHp;
    }
    for (EntityId id : k.foes) lure.foesLeft += w.findUnit(id)->alive();
    for (EntityId id : k.foot) lure.footLeft += w.findUnit(id)->alive();
    return lure;
}
}  // namespace

TEST(a_neutral_garrison_puts_its_archers_on_the_wall_and_its_foot_behind_the_gate) {
    Keep k = keepField(9, 60);
    World& w = *k.f.w;
    const Rampart& walls = ownWalls(*w.region());
    const Fixed walk = walls.corners.front().z;
    // Nobody has come near: the garrison stands where it was put.
    for (int t = 0; t < 3 * kTicksPerSecond; ++t) w.step({});
    for (EntityId id : k.archers) CHECK(w.findUnit(id)->pos.z < walk - m(2));
    // Foes up to the gate: the stations are taken.
    std::vector<Command> march;
    for (EntityId id : k.foes) march.push_back({0, id, Command::Type::Move, k.f.centre + k.f.out * m(10)});
    w.step(march);
    int up = 0, formed = 0;
    for (int t = 0; t < 250 * kTicksPerSecond; ++t) {
        w.step({});
        if (gateOf(k.f).mode != GateState::Mode::Shut) break;  // (the doors have opened: the fight is joined)
    }
    for (EntityId id : k.archers) up += w.findUnit(id)->alive() && w.findUnit(id)->pos.z > walk - m(1) && (w.findUnit(id)->pos - k.f.centre).flat().length() < m(40);
    for (EntityId id : k.foot) {
        const Unit& u = *w.findUnit(id);
        const Fixed d = (u.pos - k.f.centre).flat().length();
        formed += u.alive() && d > m(4) && d < m(18) && (u.pos - k.f.centre).flat().dot(k.f.out) < -m(3) && u.pos.z < walk - m(2);
    }
    std::printf("  before the doors swing: %d of 6 archers on the wall, %d of 10 foot in the horseshoe\n", up, formed);
    CHECK(up >= 3);  // the doors wait for half the archers to be up
    CHECK(formed >= 7);
}

TEST(the_doors_are_swung_open_to_draw_a_few_foes_in_when_the_foot_are_formed) {
    Keep k = keepField(9, 60);
    const Lure l = besiege(k, 300);
    std::printf("  swung at %d%% formed, %d%% of the archers up, %d foes at the doors, doors at %d%%; %d of 6 foes left, %d of 10 foot\n", l.formedPct, l.mannedPct, l.outside, l.hpPct, l.foesLeft, l.footLeft);
    CHECK(l.swung);
    CHECK(l.formedPct >= 70);       // only when the foot are formed
    CHECK(l.mannedPct >= 50);       // and the archers are up
    CHECK(l.outside >= 1 && l.outside <= 10 * 2);  // foes at the doors, and not more than two to a man of the foot (or the doors were failing)
    CHECK(l.foesLeft == 0);         // the lured foes died
    CHECK(l.footLeft >= 5);
}

TEST(the_doors_stay_shut_to_a_crowd_while_they_are_sound) {
    // Thirty foes at the doors of ten foot: more than two to a man, so not while the doors are above 80%.
    Keep k = keepField(9, 30, 10, 30);
    World& w = *k.f.w;
    const int maxHp = w.region()->gates()[k.f.gate].maxHp;
    std::vector<Command> march;
    for (EntityId id : k.foes) march.push_back({0, id, Command::Type::Move, k.f.centre + k.f.out * m(10)});
    w.step(march);
    int swungAtPct = -1, foesThen = 0;
    for (int t = 0; t < 400 * kTicksPerSecond && swungAtPct < 0; ++t) {
        w.step({});
        if (gateOf(k.f).mode == GateState::Mode::Shut) continue;
        swungAtPct = gateOf(k.f).hp * 100 / maxHp;
        for (EntityId id : k.foes) foesThen += w.findUnit(id)->alive();
    }
    std::printf("  outnumbered: the doors first swung with %d foes left and themselves at %d%%\n", foesThen, swungAtPct);
    CHECK(swungAtPct >= 0);
    // Never while the crowd was more than two to a man of the foot and the doors were sound.
    CHECK(foesThen <= 10 * 2 || swungAtPct < 80);
    CHECK(foesThen < 30);  // (the archers on the walls thinned them first)
}

TEST(a_garrison_without_foot_never_opens_its_doors) {
    Keep k = keepField(9, 60, 0, 3);
    const Lure l = besiege(k, 200);
    CHECK(!l.swung);
}

TEST(a_garrisons_foot_meet_a_foe_who_gets_in_and_a_man_driven_out_falls_back) {
    // The one rule for a skirmish and a conquest: the foot meet whoever comes in, and a man of the holder's idle outside the
    // walls goes back in to his station when a foe is near.
    {
        Keep k = keepField(9, 60, 8, 0);
        World& w = *k.f.w;
        const Vec3 across{k.f.out.y, -k.f.out.x, Fixed::zero()};
        const Vec3 yard = local(town().castle.courtyard) + across * m(6);
        const EntityId foe = w.spawnUnit(0, {yard.x, yard.y, w.groundHeight(yard.x, yard.y)}, k.f.out, unitStats(UnitType::Dwarves));
        for (int t = 0; t < 90 * kTicksPerSecond; ++t) w.step({});
        int alive = 0;
        for (EntityId id : k.foot) alive += w.findUnit(id)->alive();
        std::printf("  a foe in the courtyard: %s, %d of 8 foot left\n", w.findUnit(foe)->alive() ? "alive" : "dead", alive);
        CHECK(!w.findUnit(foe)->alive());
        CHECK(alive >= 6);
    }
    {
        Keep k = keepField(9, 60, 4, 0);
        World& w = *k.f.w;
        w.setGateHp(k.f.gate, 0);  // broken: the way back in is clear
        const Vec3 at = k.f.centre + k.f.out * m(50);
        const EntityId stray = w.spawnUnit(1, {at.x, at.y, w.groundHeight(at.x, at.y)}, k.f.out, unitStats(UnitType::Pikemen));
        const Vec3 fat = k.f.centre + k.f.out * m(68);
        w.spawnUnit(0, {fat.x, fat.y, w.groundHeight(fat.x, fat.y)}, -k.f.out, unitStats(UnitType::Archers));  // (a foe about, too far to fight)
        for (int t = 0; t < 40 * kTicksPerSecond; ++t) w.step({});
        const Unit& s = *w.findUnit(stray);
        std::printf("  the stray, 50 m out, is now %.1f m from the doors on the %s side\n", (s.pos - k.f.centre).flat().length().toDouble(),
                    (s.pos - k.f.centre).flat().dot(k.f.out) < Fixed::zero() ? "town" : "field");
        CHECK(s.alive() && (s.pos - k.f.centre).flat().dot(k.f.out) < Fixed::zero());
    }
}
