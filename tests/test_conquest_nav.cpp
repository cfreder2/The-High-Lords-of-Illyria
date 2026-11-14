// Conquest is Skirmish on the island (docs/ARCHITECTURE_RULES.md, docs/PARITY.md): a march across a walled town goes
// round its walls through its gates, and archers ordered onto the wall-walk climb the ramps, exactly as in a skirmish,
// because the men are the same units of the same World with the same navigation. These tests run the scenario in a
// Conquest and, where there is a skirmish equivalent, in a skirmish, and compare.
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>

#include "illyria/sim/conquest.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/nav.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "conquest_orders.h"
#include "test.h"

using namespace illyria::sim;

namespace {

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// The island's ground and navigation grid.
struct Island {
    Conquest c;
    explicit Island(uint64_t seed) : c(continent(), seed) {
        for (uint8_t s = 0; s < Continent::kFactionCount; ++s) c.setHuman(s, true);  // (no computer lords: only what the test does moves)
        c.prepareWorld();
    }
    Vec3 local(const Vec3& p) const { return {p.x - c.world()->region()->origin().x, p.y - c.world()->region()->origin().y, p.z}; }
};

// Whether a man on the ground stands in the core of a house or in the body of a curtain wall (not its gate passage): the
// geometry itself, not the navigation's coarser cells (a man pressed against a corner is no trespasser).
bool insideSolid(const Region& r, const Vec3& p) {
    for (const Obstacle& o : r.obstacles())
        if (o.kind == Obstacle::Kind::House && o.solid && abs(o.pos.x - p.x) < o.halfX / 2 && abs(o.pos.y - p.y) < o.halfY / 2) return true;
    for (const Gate& g : r.gates())
        if (((g.a + g.b) * Fixed::fromRatio(1, 2) - p).flat().length() < (g.b - g.a).flat().length() / 2 + g.halfThick + Fixed::fromInt(3)) return false;
    for (const Rampart& rp : r.ramparts()) {
        const size_t n = rp.corners.size();
        for (size_t e = 0; e < n; ++e) {
            const Vec3 a = rp.corners[e], ab = (rp.corners[(e + 1) % n] - a).flat();
            const Fixed len2 = ab.lengthSq();
            const Fixed t = len2 > Fixed::zero() ? clamp((p - a).flat().dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
            if ((p - (a + ab * t)).flat().length() < rp.halfThick * 6 / 10) return true;
        }
    }
    return false;
}

}  // namespace

TEST(a_march_across_a_walled_town_goes_round_its_walls) {
    // From the capital's courtyard to a place on the far side of its walls, in each of the eight directions: no man of
    // the march is ever inside a wall, a tower, a house or a shut gate (the ground the World's navigation keeps men out of).
    Island is(5);
    Conquest& c = is.c;
    const Conquest::CityState& cap = c.cities().front();
    const Continent::City& town = continent().cities().front();
    int tested = 0, inside = 0, arrived = 0;
    for (const int d : {0, 4}) {  // (east and west: each takes the men out of the courtyard and round the walls)
        static const int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        const int32_t x = cap.cx + dx[d] * 4, y = cap.cy + dy[d] * 4;
        if (x < 0 || y < 0 || x >= c.width() || y >= c.height() || c.cityAt(x, y) >= 0) continue;
        const uint32_t id = c.raise(0, cap.cx, cap.cy, {UnitType::LightInfantry, UnitType::Archers});
        if (!ConquestOrders::move(c, id, x, y)) continue;
        ++tested;
        for (int t = 0; t < 4500; ++t) {
            c.step();
            if (t % 5) continue;
            const Conquest::Stack* s = c.findStack(id);
            if (!s) break;
            std::vector<Conquest::Soldier> men;
            c.soldiers(0, men);
            for (const Conquest::Soldier& m : men) {
                if (m.stack != id) continue;
                const Vec3 p = m.unit ? m.unit->pos
                                      : Vec3{Fixed::fromRatio(static_cast<int64_t>(m.x * 100), 100) - c.world()->region()->origin().x,
                                             Fixed::fromRatio(static_cast<int64_t>(m.y * 100), 100) - c.world()->region()->origin().y, town.pos.z};
                const bool onWalk = m.unit && m.unit->pos.z > town.pos.z + Fixed::fromInt(3);
                if (!onWalk && insideSolid(*c.world()->region(), p)) ++inside;
            }
            if (s->path.empty() && t > 60) break;
        }
        // They are under way for the place: well out of the courtyard (the way round the walls, then the long way, is the World's).
        const Conquest::Stack* end = c.findStack(id);
        const int64_t ax = town.castle.courtyard.x.floorToInt() * 1000, ay = town.castle.courtyard.y.floorToInt() * 1000;
        if (end && (end->px - ax) * (end->px - ax) + (end->py - ay) * (end->py - ay) > int64_t{60000} * 60000) ++arrived;
    }
    CHECK(tested >= 1);
    std::printf("  %d marches, %d arrived, %d samples of a man inside a wall\n", tested, arrived, inside);
    CHECK(arrived >= 1);  // (one of them is out of the courtyard and well away; the other is still on its way out: a gate takes 24 men a long while)
    CHECK(inside == 0);
}

TEST(archers_ordered_onto_the_wall_walk_climb_the_ramps_as_in_a_skirmish) {
    Island is(5);
    Conquest& c = is.c;
    const Conquest::CityState& cap = c.cities().front();
    const Continent::City& town = continent().cities().front();
    const Region& r = *c.world()->region();
    // The embrasure farthest round from the courtyard, on the town's outer ring.
    const Vec3 yard = is.local(town.castle.courtyard);
    const Rampart* ring = nullptr;
    for (const Rampart& rp : r.ramparts())
        if (!rp.embrasures.empty() && !rp.ramps.empty() && (rp.centre - is.local(town.pos)).flat().length() < Fixed::fromInt(2))
            if (!ring || rp.corners.size() >= ring->corners.size()) ring = &rp;
    CHECK(ring != nullptr);
    if (!ring) return;
    Vec3 post = ring->embrasures.front().pos;
    for (const Rampart::Embrasure& e : ring->embrasures)
        if ((e.pos - yard).flat().length() > (post - yard).flat().length()) post = e.pos;
    const uint32_t id = c.raise(0, cap.cx, cap.cy, {UnitType::Archers});
    c.step();
    const Conquest::Stack* st = c.findStack(id);
    CHECK(st != nullptr);
    if (!st) return;
    std::vector<uint32_t> units;
    for (const Army& a : st->armies)
        for (const Man& m : a.men)
            if (m.unit) units.push_back(m.unit);
    CHECK(!units.empty());  // (a man of a stack is a unit of the world from the moment he is raised)
    if (units.empty()) return;
    // (A garrison's bowmen are put on the walk to begin with: bring them down into the courtyard first, so that they climb.)
    for (uint32_t u : units) {
        Conquest::Order o;
        o.kind = Conquest::Order::Kind::Command;
        o.side = 0;
        o.command = Command{0, u, Command::Type::Move, yard};
        c.submit(o);
    }
    for (int t = 0; t < 240 * kTicksPerSecond; ++t) {
        c.step();
        int down = 0;
        for (uint32_t u : units)
            if (const Unit* w = c.world()->findUnit(u); w && w->pos.z < post.z - Fixed::fromInt(5)) ++down;
        if (down == static_cast<int>(units.size())) break;
    }
    // The Conquest: the order is the skirmish's own Command, carried by an Order.
    for (uint32_t u : units) {
        Conquest::Order o;
        o.kind = Conquest::Order::Kind::Command;
        o.side = 0;
        o.command = Command{0, u, Command::Type::Move, post};
        c.submit(o);
    }
    int conquestTicks = -1;
    for (int t = 0; t < 240 * kTicksPerSecond && conquestTicks < 0; ++t) {
        c.step();
        int up = 0;
        for (uint32_t u : units)
            if (const Unit* w = c.world()->findUnit(u); w && w->pos.z > post.z - Fixed::fromRatio(1, 2) && (w->pos - post).flat().length() < Fixed::fromInt(12)) ++up;
        if (up + 1 >= static_cast<int>(units.size())) conquestTicks = t;  // (twelve men to one embrasure: the last of them may be left at the wall's foot)
    }
    CHECK(conquestTicks >= 0);
    // The skirmish: the same order to a man in the same ground, on the town's own window.
    const auto window = std::make_shared<const Region>(Region::fromContinent(continent(), town.pos, 256));
    WorldConfig cfg;
    cfg.region = window;
    World w(cfg);
    const Vec3 wyard = {town.castle.courtyard.x - window->origin().x, town.castle.courtyard.y - window->origin().y, town.castle.courtyard.z};
    const Vec3 wpost = {post.x + r.origin().x - window->origin().x, post.y + r.origin().y - window->origin().y, post.z};
    const EntityId a = w.spawnUnit(0, wyard, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::Archers));
    const Command go{0, a, Command::Type::Move, wpost};
    w.step(std::span(&go, 1));
    int skirmishTicks = -1;
    for (int t = 0; t < 240 * kTicksPerSecond && skirmishTicks < 0; ++t) {
        w.step({});
        const Unit* u = w.findUnit(a);
        if (u->pos.z > wpost.z - Fixed::fromRatio(1, 2) && (u->pos - wpost).flat().length() < Fixed::fromInt(12)) skirmishTicks = t;
    }
    CHECK(skirmishTicks >= 0);
    std::printf("  to the embrasure: conquest %d ticks, skirmish %d\n", conquestTicks, skirmishTicks);
    CHECK(conquestTicks <= skirmishTicks * 2 + 120);
}

namespace {
// What one castle scenario did, the same in a Conquest and in a skirmish (a gate held by the player's side).
struct GateRun {
    int openTicks = -1;    // pinned open: ticks until the doors stand open
    int shutTicks = -1;    // let go: ticks until they stand shut again
    int outTicks = -1;     // a man ordered in through the shut gate: ticks until he is there
    bool swungForHim = false;
    bool shutBehind = false;
};
// The gate nearest a place (a capital's inner bailey has one of its own, between the courtyard and the keep).
size_t gateNear(const Region& r, const Vec3& at) {
    size_t best = 0;
    Fixed bestD = Fixed::fromInt(1000000);
    for (size_t i = 0; i < r.gates().size(); ++i) {
        const Fixed d = ((r.gates()[i].a + r.gates()[i].b) * Fixed::fromRatio(1, 2) - at).flat().length();
        if (d < bestD) bestD = d, best = i;
    }
    return best;
}
// `step` advances the one world a tick (a conquest's, or a skirmish's) with the commands given; `world` is that world.
template <class Step, class WorldOf>
GateRun runGate(Step step, WorldOf world, size_t gate, EntityId man, const Vec3& to) {
    GateRun run;
    Command pin{0, kNoEntity, Command::Type::SetGate, {}};
    pin.objectIndex = static_cast<int32_t>(gate);
    pin.gatePin = Command::GatePin::Open;
    step(pin);
    for (int t = 0; t < 10 * kTicksPerSecond && run.openTicks < 0; ++t) {
        step(std::nullopt);
        if (world().gates()[gate].opened()) run.openTicks = t + 1;
    }
    pin.gatePin = Command::GatePin::Auto;
    step(pin);
    for (int t = 0; t < 20 * kTicksPerSecond && run.shutTicks < 0; ++t) {
        step(std::nullopt);
        if (world().gates()[gate].mode == GateState::Mode::Shut) run.shutTicks = t + 1;
    }
    // A man of ours, in the courtyard, ordered in through the shut gate to the bailey: it swings open for him and shuts behind him.
    step(Command{0, man, Command::Type::Move, to});
    for (int t = 0; t < 120 * kTicksPerSecond; ++t) {
        step(std::nullopt);
        const GateState& g = world().gates()[gate];
        run.swungForHim = run.swungForHim || g.mode == GateState::Mode::Opening;
        const Unit* u = world().findUnit(man);
        if (run.outTicks < 0 && (u->pos - to).flat().length() < Fixed::fromInt(4)) run.outTicks = t;
        if (run.outTicks >= 0 && g.mode == GateState::Mode::Shut) {
            run.shutBehind = true;
            break;
        }
    }
    return run;
}
}  // namespace

TEST(a_gate_swings_the_same_in_a_conquest_town_and_a_skirmish_castle) {
    Island is(5);
    Conquest& c = is.c;
    const Conquest::CityState& cap = c.cities().front();
    const Continent::City& town = continent().cities().front();
    // The Conquest: the capital's own gate (the player's side holds it, by his hand and not a garrison's), a man of his
    // raised in the courtyard, the commands carried by Orders.
    const Region& r = *c.world()->region();
    const size_t gate = gateNear(r, is.local(town.pos));
    CHECK(c.world()->gates()[gate].holder == 0);
    CHECK(c.world()->gates()[gate].garrisoned && !c.world()->gates()[gate].recalls);  // (kept for his idle men inside his walls; the gate is his hand's)
    const uint32_t id = c.raise(0, cap.cx, cap.cy, {UnitType::Pikemen});
    c.step();
    EntityId man = 0;
    for (const Army& a : c.findStack(id)->armies)
        for (const Man& m : a.men)
            if (m.unit) man = m.unit;
    CHECK(man != 0);
    if (man == 0) return;
    const Gate& g = r.gates()[gate];
    const Vec3 inner = (g.a + g.b) * Fixed::fromRatio(1, 2) - g.out * Fixed::fromInt(8);
    const GateRun conquest = runGate(
        [&](std::optional<Command> cmd) {
            if (cmd) {
                Conquest::Order o;
                o.kind = Conquest::Order::Kind::Command;
                o.side = 0;
                o.command = *cmd;
                c.submit(o);
            }
            c.step();
        },
        [&]() -> const World& { return *c.world(); }, gate, man, inner);
    // The skirmish: the same gate in the town's own window, held by side 0, the same man in the same courtyard.
    const auto window = std::make_shared<const Region>(Region::fromContinent(continent(), town.pos, 256));
    WorldConfig cfg;
    cfg.region = window;
    World w(cfg);
    const size_t wgate = gateNear(*window, {town.pos.x - window->origin().x, town.pos.y - window->origin().y, town.pos.z});
    w.keepGate(wgate, 0, true);  // (the one rule: the player's gate is kept for his idle men, his pin overruling)
    CHECK(w.gates()[wgate].garrisoned == c.world()->gates()[gate].garrisoned && w.gates()[wgate].recalls == c.world()->gates()[gate].recalls);
    const Vec3 yard = {town.castle.courtyard.x - window->origin().x, town.castle.courtyard.y - window->origin().y, town.castle.courtyard.z};
    const EntityId a = w.spawnUnit(0, yard, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::Pikemen));
    const Gate& wg = window->gates()[wgate];
    const Vec3 winner = (wg.a + wg.b) * Fixed::fromRatio(1, 2) - wg.out * Fixed::fromInt(8);
    const GateRun skirmish = runGate(
        [&](std::optional<Command> cmd) {
            if (cmd) w.step(std::span(&*cmd, 1));
            else w.step({});
        },
        [&]() -> const World& { return w; }, wgate, a, winner);
    std::printf("  open after %d / %d ticks, shut again after %d / %d, in through the gate at %d / %d (conquest / skirmish)\n", conquest.openTicks, skirmish.openTicks,
                conquest.shutTicks, skirmish.shutTicks, conquest.outTicks, skirmish.outTicks);
    // The swing is the World's: the same to the tick in both.
    CHECK(conquest.openTicks == static_cast<int>(kGateSwingTicks));
    CHECK(conquest.openTicks == skirmish.openTicks);
    CHECK(conquest.shutTicks >= static_cast<int>(kGateSwingTicks) && conquest.shutTicks == skirmish.shutTicks);
    CHECK(conquest.swungForHim && skirmish.swungForHim);
    CHECK(conquest.shutBehind && skirmish.shutBehind);
    CHECK(conquest.outTicks >= 0 && skirmish.outTicks >= 0);
    CHECK(conquest.outTicks <= skirmish.outTicks * 2 + 120);
}

// Not a rule but a measurement, printed for the record (docs/ONE_WORLD.md, "What it costs"): every man on the island is a
// unit of the world, stepped every tick as in a skirmish. An all-computer conquest from the start, then the same with
// stacks added in every town until the island holds about 3,000 and 6,800 men (the population of day 40 and day 120).
TEST(the_whole_population_in_one_world_costs_what_a_skirmish_man_costs) {
    Conquest c(continent(), 7);
    c.prepareWorld();
    c.step();
    size_t town = 0;
    for (const int32_t want : {0, 3000, 6800}) {
        while (c.menInWorld() < want) {
            const Conquest::CityState& cs = c.cities()[town++ % c.cities().size()];
            c.raise(cs.owner == Conquest::kNeutral ? 0 : cs.owner, cs.cx, cs.cy, std::vector<UnitType>(8, UnitType::LightInfantry));
            if (town % 8 == 0) c.step();  // (seated as they come)
        }
        c.step();
        const int32_t men = c.menInWorld();
        const auto t0 = std::chrono::steady_clock::now();
        constexpr int kTicks = 200;
        for (int k = 0; k < kTicks; ++k) c.step();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kTicks;
        std::printf("  %d men in the world: %.3f ms a tick, %.2f us a man\n", men, ms, ms * 1000.0 / std::max(1, men));
        CHECK(men >= std::max(1000, want));
    }
}
