// Fliers are never stuck (docs/PARITY.md, "Fliers"). A flier that lands on a wall-walk and is then ordered down used to
// start his take-off run into the parapet and keep running at it for ever (World::updateFlight): three Griffins standing
// between the merlons of a town's curtain wall, not moving. These scenarios run every flier type through the ways a flier
// meets walls (up onto the walk, down from it, over the curtain, through a shut gate, after a foe, on a queue of orders),
// in a Conquest town and in a skirmish on the same town's ground, and require each to be carried out.
#include <cstdio>
#include <map>
#include <cmath>
#include <functional>
#include <memory>
#include <span>
#include <string>

#include "illyria/sim/conquest.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

constexpr UnitType kFliers[] = {UnitType::Griffins, UnitType::Pegasi, UnitType::Archons, UnitType::Dragons};
const char* nameOf(UnitType t) {
    switch (t) {
        case UnitType::Griffins: return "Griffins";
        case UnitType::Pegasi: return "Pegasi";
        case UnitType::Archons: return "Archons";
        default: return "Dragons";
    }
}
Fixed m(int64_t v) { return Fixed::fromInt(v); }

// One town's ground, driven the same way in either mode: a Conquest's capital (men raised in its courtyard, orders carried
// by the Conquest) or a skirmish on that town's own window (men spawned there, orders handed to the World).
struct Rig {
    virtual ~Rig() = default;
    virtual const World& world() const = 0;
    virtual EntityId spawn(PlayerId side, UnitType type, const Vec3& at, bool harmless) = 0;
    virtual void order(const Command& c) = 0;
    virtual void tick() = 0;
    virtual void kill(EntityId id) = 0;
    virtual bool conquest() const = 0;
};

struct Site {
    Vec3 yard, post, gateMid, out;  // courtyard, an embrasure on the outer walk, the gate and its way out (all local)
    size_t gate = 0;
};

Site groundOf(const Region& r, const Vec3& townLocal, const Vec3& yardLocal) {
    Site g;
    g.yard = yardLocal;
    const Rampart* ring = nullptr;
    for (const Rampart& rp : r.ramparts())
        if (!rp.embrasures.empty() && (rp.centre - townLocal).flat().length() < m(2))
            if (!ring || rp.corners.size() >= ring->corners.size()) ring = &rp;
    g.post = ring->embrasures[ring->embrasures.size() / 3].pos;
    Fixed best = m(100000);
    const int32_t ringIndex = static_cast<int32_t>(ring - r.ramparts().data());
    for (size_t i = 0; i < r.gates().size(); ++i) {
        if (r.gates()[i].ring != ringIndex) continue;
        const Vec3 mid = (r.gates()[i].a + r.gates()[i].b) * Fixed::fromRatio(1, 2);
        const Fixed d = (mid - yardLocal).flat().length();
        if (d < best) best = d, g.gate = i, g.gateMid = mid, g.out = r.gates()[i].out;
    }
    return g;
}

struct ConquestRig : Rig {
    Conquest c;
    uint32_t stack[2] = {0, 0};
    Vec3 org;
    ConquestRig() : c(continent(), 5) {
        for (uint8_t s = 0; s < Continent::kFactionCount; ++s) c.setHuman(s, true);  // (nobody moves but what the test orders)
        c.prepareWorld();
        org = c.world()->region()->origin();
    }
    const World& world() const override { return *c.world(); }
    EntityId spawn(PlayerId side, UnitType type, const Vec3&, bool) override {
        const Conquest::CityState& cap = c.cities().front();
        const uint32_t id = c.raise(side, cap.cx, cap.cy, {type});
        c.step();
        for (const Army& a : c.findStack(id)->armies)
            for (const Man& m : a.men)
                if (m.unit) return m.unit;
        return kNoEntity;
    }
    void order(const Command& cmd) override {
        Conquest::Order o;
        o.kind = Conquest::Order::Kind::Command;
        o.side = cmd.player;
        o.command = cmd;
        c.submit(o);
    }
    void tick() override { c.step(); }
    void kill(EntityId) override {}
    bool conquest() const override { return true; }
};

struct SkirmishRig : Rig {
    std::unique_ptr<World> w;
    SkirmishRig() {
        const Continent::City& town = continent().cities().front();
        WorldConfig cfg;
        cfg.region = std::make_shared<const Region>(Region::fromContinent(continent(), town.pos, 256));
        w = std::make_unique<World>(cfg);
    }
    const World& world() const override { return *w; }
    EntityId spawn(PlayerId side, UnitType type, const Vec3& at, bool harmless) override {
        UnitStats s = unitStats(type);
        if (harmless) s.melee.damage = 0, s.ranged = {}, s.aggroRange = Fixed::zero();
        return w->spawnUnit(side, {at.x, at.y, w->groundHeight(at.x, at.y)}, {Fixed::one(), Fixed::zero(), Fixed::zero()}, s);
    }
    void order(const Command& cmd) override { w->step(std::span(&cmd, 1)); }
    void tick() override { w->step({}); }
    void kill(EntityId id) override { w->removeUnit(id); }
    bool conquest() const override { return false; }
};

// What a run did: whether the order was carried out, how long it took, and the longest time he stayed within a metre of
// one place with an order still to carry out (the "stuck" of the complaint).
struct Outcome {
    bool done = false;
    double seconds = 0, stuck = 0;
};

struct Watch {
    Vec3 anchor;
    int since = 0, worst = 0;
    bool began = false;
    void sample(const Unit& u, int tickNow) {
        if (!began || (u.pos - anchor).flat().length() >= Fixed::one() || !(u.hasMoveTarget || u.targetUnit != kNoEntity)) {
            anchor = u.pos, since = tickNow, began = true;
        }
        worst = std::max(worst, tickNow - since);
    }
};

// Runs until `done` or `limit` seconds; returns the outcome.
Outcome runUntil(Rig& rig, EntityId id, int limit, const std::function<bool(const Unit&)>& done, Watch& watch, int& tickNow) {
    Outcome o;
    for (int t = 0; t < limit * kTicksPerSecond; ++t) {
        rig.tick();
        ++tickNow;
        const Unit* u = rig.world().findUnit(id);
        watch.sample(*u, tickNow);
        if (done(*u)) {
            o.done = true;
            o.seconds = (t + 1) / 30.0;
            break;
        }
    }
    o.stuck = watch.worst / 30.0;
    return o;
}

Command moveTo(PlayerId p, EntityId id, const Vec3& to, bool queue = false) {
    Command c{p, id, Command::Type::Move, to};
    c.queue = queue;
    return c;
}

enum class Scenario { UpThenDown, QueuedUpDown, OverTheCurtain, ThroughShutGate, FoeInTheYard, FoeOutsideTheWalls, FoeDiesOnTheWay, GarrisonHoldsTheGate };
const char* scenarioName(Scenario s) {
    switch (s) {
        case Scenario::UpThenDown: return "up onto the walk, then down to the yard";
        case Scenario::QueuedUpDown: return "queued: onto the walk, then the yard";
        case Scenario::OverTheCurtain: return "yard to 120 m outside";
        case Scenario::ThroughShutGate: return "yard to 25 m outside (a shut gate)";
        case Scenario::FoeInTheYard: return "from the walk, attack a foe in the yard";
        case Scenario::FoeOutsideTheWalls: return "from the yard, attack a foe 25 m outside";
        case Scenario::FoeDiesOnTheWay: return "chase a foe 80 m off, who dies on the way";
        default: return "in a garrison, foes at the gate";
    }
}

Outcome play(Rig& rig, UnitType type, Scenario sc) {
    const Region& r = *rig.world().region();
    const Continent::City& town = continent().cities().front();
    const Vec3 org = r.origin();
    const Vec3 yard{town.castle.courtyard.x - org.x, town.castle.courtyard.y - org.y, town.castle.courtyard.z};
    const Site g = groundOf(r, town.pos - org, yard);
    if (!rig.conquest()) {
        // (A skirmish's gate is the player's.)
        const_cast<World&>(rig.world()).setGateHolder(g.gate, 0);
    }
    const EntityId f = rig.spawn(0, type, yard, false);
    Watch watch;
    int now = 0;
    const auto near = [](const Vec3& p, Fixed d) { return [p, d](const Unit& u) { return (u.pos - p).flat().length() < d && !u.hasMoveTarget; }; };
    const auto nearEnough = [](const Vec3& p, Fixed d) { return [p, d](const Unit& u) { return (u.pos - p).flat().length() < d; }; };
    switch (sc) {
        case Scenario::UpThenDown: {
            rig.order(moveTo(0, f, g.post));
            runUntil(rig, f, 60, nearEnough(g.post, m(3)), watch, now);
            rig.order(moveTo(0, f, g.yard));
            return runUntil(rig, f, 90, nearEnough(g.yard, m(3)), watch, now);
        }
        case Scenario::QueuedUpDown: {
            rig.order(moveTo(0, f, g.post));
            rig.order(moveTo(0, f, g.yard, true));
            return runUntil(rig, f, 150, [&](const Unit& u) { return (u.pos - g.yard).flat().length() < m(3) && !u.hasMoveTarget && u.queue.empty() && u.pos.z < g.post.z - m(2); }, watch, now);
        }
        case Scenario::OverTheCurtain: {
            const Vec3 to = g.gateMid + g.out * m(120);
            rig.order(moveTo(0, f, to));
            return runUntil(rig, f, 120, nearEnough(to, m(8)), watch, now);
        }
        case Scenario::ThroughShutGate: {
            const Vec3 to = g.gateMid + g.out * m(25);
            rig.order(moveTo(0, f, to));
            return runUntil(rig, f, 120, nearEnough(to, m(6)), watch, now);
        }
        case Scenario::FoeInTheYard: {
            rig.order(moveTo(0, f, g.post));
            runUntil(rig, f, 60, nearEnough(g.post, m(3)), watch, now);
            const Vec3 at = yard + Vec3{m(6), m(0), m(0)};
            const EntityId foe = rig.spawn(1, UnitType::LightInfantry, at, true);
            Command a{0, f, Command::Type::AttackUnit, {}};
            a.targetUnit = foe;
            rig.order(a);
            return runUntil(rig, f, 120, [&](const Unit&) { return !rig.world().findUnit(foe)->alive(); }, watch, now);
        }
        case Scenario::FoeOutsideTheWalls: {
            if (rig.conquest()) return {};  // (a stack can only be raised in the town)
            const Vec3 at = g.gateMid + g.out * m(25);
            const EntityId foe = rig.spawn(1, UnitType::LightInfantry, at, true);
            Command a{0, f, Command::Type::AttackUnit, {}};
            a.targetUnit = foe;
            rig.order(a);
            return runUntil(rig, f, 150, [&](const Unit&) { return !rig.world().findUnit(foe)->alive(); }, watch, now);
        }
        case Scenario::FoeDiesOnTheWay: {
            if (rig.conquest()) return {};
            const Vec3 at = g.gateMid + g.out * m(80);
            const EntityId foe = rig.spawn(1, UnitType::LightInfantry, at, true);
            Command a{0, f, Command::Type::AttackUnit, {}};
            a.targetUnit = foe;
            rig.order(a);
            for (int t = 0; t < 6 * kTicksPerSecond; ++t) rig.tick(), ++now;
            const bool aloft = rig.world().findUnit(f)->flight != Unit::Flight::Grounded;
            rig.kill(foe);
            // His quarry gone: the order is over, and he comes down and stands at ease (no flight after nobody).
            const Outcome o = runUntil(rig, f, 20, [&](const Unit& u) { return u.targetUnit == kNoEntity && !u.hasMoveTarget && u.flight == Unit::Flight::Grounded; }, watch, now);
            return aloft ? o : Outcome{};
        }
        default: {
            if (rig.conquest()) return {};
            // The gate is kept by its garrison: bowmen on the walls, foot behind the doors, and this flier among them.
            auto& w = const_cast<World&>(rig.world());
            w.setGateGarrisoned(g.gate, true);
            const Vec3 across{g.out.y, -g.out.x, Fixed::zero()};
            for (int i = 0; i < 4; ++i) rig.spawn(0, UnitType::Archers, yard + across * m(i * 2 - 3), false);
            for (int i = 0; i < 6; ++i) rig.spawn(0, UnitType::Pikemen, yard - g.out * m(4) + across * m(i * 2 - 5), false);
            std::vector<EntityId> foes;
            for (int i = 0; i < 4; ++i) foes.push_back(rig.spawn(1, UnitType::Pikemen, g.gateMid + g.out * m(60) + across * m(i * 2 - 3), false));
            for (const EntityId id : foes) rig.order(moveTo(1, id, g.gateMid + g.out * m(10)));
            // He keeps his station like the rest and takes the foes who come in reach; he must not be left standing with an order
            // in hand (the longest stand over two minutes of the siege is the measure).
            Outcome o = runUntil(rig, f, 120, [&](const Unit&) { return false; }, watch, now);
            o.done = o.stuck < 20.0;  // (how the fight ends is the garrison's; what is asked is that he is never left standing)
            return o;
        }
    }
    (void)near;
}

}  // namespace

TEST(a_table_of_every_flier_in_every_walled_scenario) {
    int failures = 0, run = 0;
    for (const bool conquest : {true, false}) {
        std::printf("  %s\n", conquest ? "Conquest (the capital)" : "Skirmish (the same town's ground)");
        for (const Scenario sc : {Scenario::UpThenDown, Scenario::QueuedUpDown, Scenario::OverTheCurtain, Scenario::ThroughShutGate, Scenario::FoeInTheYard, Scenario::FoeOutsideTheWalls, Scenario::FoeDiesOnTheWay, Scenario::GarrisonHoldsTheGate}) {
            for (const UnitType type : kFliers) {
                std::unique_ptr<Rig> rig;
                if (conquest) rig = std::make_unique<ConquestRig>();
                else rig = std::make_unique<SkirmishRig>();
                if (conquest && (sc == Scenario::FoeOutsideTheWalls || sc == Scenario::FoeDiesOnTheWay || sc == Scenario::GarrisonHoldsTheGate)) continue;
                const Outcome o = play(*rig, type, sc);
                ++run;
                failures += !o.done;
                std::printf("    %-8s %-44s %s %6.1f s, longest stand %5.1f s\n", nameOf(type), scenarioName(sc), o.done ? "done  " : "STUCK ", o.seconds, o.stuck);
                CHECK(o.done);
                CHECK(o.stuck < 30.0);  // never a long stand with an order in hand
            }
        }
    }
    std::printf("  %d runs, %d stuck\n", run, failures);
}

// The complaint, as it was seen: three Griffins standing on a town's curtain wall, not moving. Sent up onto the walk (each to
// his own embrasure) and then told to come down into the courtyard, a flier's take-off run began on the walk, into the
// parapet at his beak, and never ended (World::updateFlight): his order stayed in hand, so he did nothing else either.
TEST(three_griffins_on_the_curtain_wall_come_down_when_told_and_do_not_stand_running_at_the_merlons) {
    ConquestRig rig;
    const Region& r = *rig.world().region();
    const Continent::City& town = continent().cities().front();
    const Vec3 org = r.origin();
    const Vec3 yard{town.castle.courtyard.x - org.x, town.castle.courtyard.y - org.y, town.castle.courtyard.z};
    const Rampart* ring = nullptr;
    for (const Rampart& rp : r.ramparts())
        if (!rp.embrasures.empty() && (rp.centre - (town.pos - org)).flat().length() < m(2))
            if (!ring || rp.corners.size() >= ring->corners.size()) ring = &rp;
    CHECK(ring != nullptr);
    if (!ring) return;
    std::vector<EntityId> griffins;
    for (int i = 0; i < 3; ++i) griffins.push_back(rig.spawn(0, UnitType::Griffins, yard, false));
    const size_t first = ring->embrasures.size() / 3;
    for (size_t i = 0; i < griffins.size(); ++i) rig.order(moveTo(0, griffins[i], ring->embrasures[first + i * 3].pos));
    for (int t = 0; t < 45 * kTicksPerSecond; ++t) rig.tick();
    int onWalk = 0;
    for (const EntityId id : griffins) onWalk += rig.world().findUnit(id)->pos.z > yard.z + m(5);
    CHECK(onWalk == 3);
    // Down into the courtyard, each to a place of his own.
    for (size_t i = 0; i < griffins.size(); ++i) rig.order(moveTo(0, griffins[i], yard + Vec3{m(static_cast<int64_t>(i) * 4 - 4), m(0), m(0)}));
    std::vector<Watch> watches(griffins.size());
    std::vector<Vec3> still(griffins.size());
    int down = 0;
    for (int t = 1; t <= 60 * kTicksPerSecond; ++t) {
        rig.tick();
        down = 0;
        for (size_t i = 0; i < griffins.size(); ++i) {
            const Unit& u = *rig.world().findUnit(griffins[i]);
            watches[i].sample(u, t);
            down += u.pos.z < yard.z + m(2) && (u.pos - yard).flat().length() < m(8);
        }
        if (down == 3) break;
    }
    double worst = 0;
    for (const Watch& w : watches) worst = std::max(worst, w.worst / 30.0);
    std::printf("  three griffins on the wall, sent down: %d of 3 in the yard, the longest any stood still with an order: %.1f s\n", down, worst);
    CHECK(down == 3);
    CHECK(worst < 5.0);
}
