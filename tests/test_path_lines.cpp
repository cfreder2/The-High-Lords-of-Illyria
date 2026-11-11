// The paths soldiers will walk, drawn on the map (path_lines.h, renderer_orders.cpp): the sim's read-only route and
// stance (World::routeOf, World::stanceOf), men going one way merged into one line, and only the player's own paths
// ever told.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/game/path_lines.h"
#include "illyria/sim/campaign.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;
using illyria::game::Battle;
using illyria::game::RouteIn;
using illyria::game::RouteLine;
using illyria::game::V3;

namespace {
using Stance = World::Stance;

Fixed m(int64_t v) { return Fixed::fromInt(v); }

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

RouteIn route(EntityId id, Stance stance, std::vector<V3> points, EntityId quarry = kNoEntity) {
    RouteIn r;
    r.unit = id;
    r.stance = stance;
    r.quarry = quarry;
    r.points = std::move(points);
    return r;
}

void run(Battle& b, double seconds) {
    for (double t = 0; t < seconds; t += 0.1) b.advance(0.1);
}
int squadNamed(const Battle& b, const char* name) {
    for (size_t i = 0; i < b.squads().size(); ++i)
        if (b.squads()[i].name == name) return static_cast<int>(i);
    return -1;
}
bool crosses(const V3& a, const V3& b, const Vec3& c, const Vec3& d) {
    const auto side = [](float ax, float ay, float bx, float by, float px, float py) { return (bx - ax) * (py - ay) - (by - ay) * (px - ax); };
    const float cx = static_cast<float>(c.x.toDouble()), cy = static_cast<float>(c.y.toDouble());
    const float dx = static_cast<float>(d.x.toDouble()), dy = static_cast<float>(d.y.toDouble());
    const float s1 = side(a.x, a.y, b.x, b.y, cx, cy), s2 = side(a.x, a.y, b.x, b.y, dx, dy);
    const float s3 = side(cx, cy, dx, dy, a.x, a.y), s4 = side(cx, cy, dx, dy, b.x, b.y);
    return s1 * s2 < 0 && s3 * s4 < 0;
}
}  // namespace

TEST(men_going_one_way_are_one_line_and_the_rest_are_not) {
    std::vector<RouteIn> in;
    // Twenty men in a block, a few metres apart, all to within a few metres of one place.
    for (EntityId i = 1; i <= 20; ++i) {
        const float x = static_cast<float>(i % 5) * 1.5f, y = static_cast<float>(i / 5) * 1.5f;
        in.push_back(route(i, Stance::Patrol, {{x, y, 0}, {100 + x, 40 + y, 0}}));
    }
    std::vector<RouteLine> lines = illyria::game::mergeRoutes(in, 8.0f);
    CHECK(lines.size() == 1);
    CHECK(lines.size() == 1 && lines[0].men == 20);
    // Another place, another stance, another quarry: other lines.
    in.push_back(route(30, Stance::Patrol, {{0, 0, 0}, {-120, 40, 0}}));
    in.push_back(route(31, Stance::Move, {{0, 0, 0}, {100, 40, 0}}));
    in.push_back(route(32, Stance::Hunt, {{0, 0, 0}, {100, 40, 0}}, 7));
    in.push_back(route(33, Stance::Hunt, {{0, 0, 0}, {100, 40, 0}}, 8));
    in.push_back(route(34, Stance::Idle, {{0, 0, 0}, {100, 40, 0}}));
    lines = illyria::game::mergeRoutes(in, 8.0f);
    CHECK(lines.size() == 5);  // (the idle man has none)
    // A tolerance below zero: a line for each.
    CHECK(illyria::game::mergeRoutes(in, -1.0f).size() == 24);
    // The line of a group runs from the middle of the men to the middle of where they end.
    lines = illyria::game::mergeRoutes({in.begin(), in.begin() + 20}, 8.0f);
    CHECK(std::abs(lines[0].points.back().x - 103.0f) < 1.0f);
    CHECK(std::abs(lines[0].points.front().x - 3.0f) < 1.0f);
}

TEST(a_route_round_a_wall_is_not_merged_with_one_straight) {
    const RouteIn straight = route(1, Stance::Patrol, {{0, 0, 0}, {200, 0, 0}});
    const RouteIn round = route(2, Stance::Patrol, {{0, 0, 0}, {100, 90, 0}, {200, 0, 0}});
    CHECK(illyria::game::mergeRoutes({straight, round}, 8.0f).size() == 2);
    CHECK(illyria::game::mergeRoutes({straight, straight}, 8.0f).size() == 1);
}

TEST(men_of_one_squad_bending_a_few_metres_apart_are_one_line) {
    // A bend of 20 m at the middle: two lines for strangers, one for a squad's men.
    RouteIn a = route(1, Stance::Patrol, {{0, 0, 0}, {100, 0, 0}, {200, 0, 0}});
    RouteIn b = route(2, Stance::Patrol, {{0, 0, 0}, {100, 28, 0}, {200, 0, 0}});
    CHECK(illyria::game::mergeRoutes({a, b}, 8.0f).size() == 2);
    a.squad = b.squad = 3;
    CHECK(illyria::game::mergeRoutes({a, b}, 8.0f).size() == 1);
    // A wall's width or more apart: two lines whoever they are.
    b.points[1] = {100, 70, 0};
    CHECK(illyria::game::mergeRoutes({a, b}, 8.0f).size() == 2);
}

TEST(a_whole_army_is_at_most_a_few_dozen_lines) {
    std::vector<RouteIn> in;
    for (EntityId i = 1; i <= 3000; ++i) in.push_back(route(i, Stance::Patrol, {{0, 0, 0}, {static_cast<float>(i) * 3.0f, 10, 0}}));
    CHECK(illyria::game::mergeRoutes(in, 8.0f).size() <= illyria::game::kMaxRouteLines);
}

TEST(each_order_has_its_stance) {
    WorldConfig cfg;
    cfg.region = std::make_shared<const Region>(Region::fromContinent(continent(), continent().cities()[0].pos, 256));
    World w(cfg);
    const Vec3 at = continent().cities()[0].pos - cfg.region->origin() + Vec3{m(60), m(0), m(0)};
    const EntityId a = w.spawnUnit(0, at, {m(1), m(0), m(0)}, unitStats(UnitType::LightInfantry));
    const EntityId foe = w.spawnUnit(1, at + Vec3{m(0), m(30), m(0)}, {m(1), m(0), m(0)}, unitStats(UnitType::LightInfantry));
    CHECK(World::stanceOf(*w.findUnit(a)) == Stance::Idle);
    Command mv;
    mv.player = 0;
    mv.unit = a;
    mv.type = Command::Type::Move;
    mv.target = at + Vec3{m(20), m(0), m(0)};
    w.step(std::vector<Command>{mv});
    CHECK(World::stanceOf(*w.findUnit(a)) == Stance::Patrol);  // (marches with initiative)
    mv.force = true;
    w.step(std::vector<Command>{mv});
    CHECK(World::stanceOf(*w.findUnit(a)) == Stance::Move);
    Command hunt;
    hunt.player = 0;
    hunt.unit = a;
    hunt.type = Command::Type::AttackUnit;
    hunt.targetUnit = foe;
    w.step(std::vector<Command>{hunt});
    CHECK(World::stanceOf(*w.findUnit(a)) == Stance::Hunt);
    std::vector<Vec3> out;
    w.routeOf(*w.findUnit(a), out);
    CHECK(!out.empty());  // (after the quarry, wherever he is, or as far as a shut gate lets him)
    Command stop = mv;
    stop.type = Command::Type::Stop;
    w.step(std::vector<Command>{stop});
    CHECK(World::stanceOf(*w.findUnit(a)) == Stance::Idle);
    w.routeOf(*w.findUnit(a), out);
    CHECK(out.empty());
}

// Round the walls of a town, not through them; and asking changes nothing in the world.
TEST(a_route_across_a_castle_goes_round_its_walls_and_asking_changes_nothing) {
    WorldConfig cfg;
    const Continent::City& town = continent().cities()[Continent::kFactionCount];
    cfg.region = std::make_shared<const Region>(Region::fromContinent(continent(), town.pos, 256));
    World w(cfg);
    const Region& rg = *cfg.region;
    const Vec3 mid{town.pos.x - rg.origin().x, town.pos.y - rg.origin().y, m(0)};
    const Fixed out = town.radius + m(25);
    const Vec3 from = mid + Vec3{-out, m(0), m(0)}, to = mid + Vec3{out, m(0), m(0)};
    const EntityId a = w.spawnUnit(0, from, {m(1), m(0), m(0)}, unitStats(UnitType::LightInfantry));
    Command mv;
    mv.player = 0;
    mv.unit = a;
    mv.type = Command::Type::Move;
    mv.target = to;
    w.step(std::vector<Command>{mv});
    std::vector<Vec3> way;
    const uint64_t before = w.checksum();
    w.routeOf(*w.findUnit(a), way);
    (void)World::stanceOf(*w.findUnit(a));
    CHECK(w.checksum() == before);
    CHECK(!way.empty());
    // Not one stretch of it (from where he stands) crosses a wall.
    std::vector<V3> line{illyria::game::toV3(w.findUnit(a)->pos)};
    for (const Vec3& p : way) line.push_back(illyria::game::toV3(p));
    int crossings = 0;
    for (size_t i = 1; i < line.size(); ++i)
        for (const WallSeg& s : rg.walls()) crossings += crosses(line[i - 1], line[i], s.a, s.b);
    std::printf("  %zu waypoints, %d wall crossings\n", way.size(), crossings);
    CHECK(crossings == 0);
    // (The straight way would have: the town's walls lie between.)
    int straight = 0;
    for (const WallSeg& s : rg.walls()) straight += crosses(illyria::game::toV3(from), illyria::game::toV3(to), s.a, s.b);
    CHECK(straight > 0);
    // A hunter after a man on the far side goes by that same way: the planned path, then the quarry.
    const EntityId hunter = w.spawnUnit(0, from, {m(1), m(0), m(0)}, unitStats(UnitType::LightInfantry));
    const EntityId quarry = w.spawnUnit(1, to, {m(1), m(0), m(0)}, unitStats(UnitType::LightInfantry));
    Command hunt;
    hunt.player = 0;
    hunt.unit = hunter;
    hunt.type = Command::Type::AttackUnit;
    hunt.targetUnit = quarry;
    w.step(std::vector<Command>{hunt});
    w.step(std::vector<Command>{});
    std::vector<Vec3> hway;
    w.routeOf(*w.findUnit(hunter), hway);
    CHECK(World::stanceOf(*w.findUnit(hunter)) == Stance::Hunt);
    CHECK(hway.size() >= 2);  // (round the walls, not one straight stretch)
    CHECK(hway.back().x == w.findUnit(quarry)->pos.x);
    std::vector<V3> hline{illyria::game::toV3(w.findUnit(hunter)->pos)};
    for (const Vec3& p : hway) hline.push_back(illyria::game::toV3(p));
    int hcross = 0;
    for (size_t i = 1; i < hline.size(); ++i)
        for (const WallSeg& s : rg.walls()) hcross += crosses(hline[i - 1], hline[i], s.a, s.b);
    std::printf("  hunt: %zu waypoints, %d wall crossings\n", hway.size(), hcross);
    CHECK(hcross == 0);
}

TEST(drawing_the_paths_changes_nothing_in_the_battle) {
    Battle b(42);
    const int watch = squadNamed(b, "The Grey Watch");
    b.selectSquad(watch);
    run(b, 1);
    b.orderMove(b.squadCentre(b.squads()[static_cast<size_t>(watch)], false) + V3{-30, 8, 0});
    b.advance(0.05);
    const uint64_t before = b.world().checksum();
    const std::vector<RouteLine> lines = b.pathLines(false, true);
    (void)b.pathLines(true, false);
    CHECK(b.world().checksum() == before);
    // A squad ordered to one place: one line, of patrol (the ordinary march), from the men to the place.
    // (A man whose way bends round a rock has a line of his own, so a squad is a line or two.)
    int men = 0;
    for (const RouteLine& l : lines) {
        men += l.men;
        CHECK(l.stance == Stance::Patrol);  // (the ordinary march: he fights what he meets)
        CHECK(l.points.size() >= 2);
    }
    CHECK(!lines.empty() && lines.size() <= 3);
    CHECK(men == static_cast<int>(b.selectedAlive().size()));
    // One for each man, on request.
    CHECK(b.pathLines(true, false).size() == b.selectedAlive().size());
    // A forced march is a plain move.
    b.orderForceMarch(b.squadCentre(b.squads()[static_cast<size_t>(watch)], false) + V3{-40, 8, 0});
    b.advance(0.05);
    const std::vector<RouteLine> forced = b.pathLines(false, false);
    CHECK(!forced.empty());
    for (const RouteLine& l : forced) CHECK(l.stance == Stance::Move);
    // Not selected, a moment after the order: still shown, fading; later, gone.
    b.clearSelection();
    b.advance(0.05);
    CHECK(!b.pathLines(false, false).empty());
    run(b, 4);
    CHECK(b.pathLines(false, false).empty());
}

// An order to attack one man is a hunt, and its line follows him; in the fog of Conquest the line is not drawn (it
// would tell where the foe is), nor is any route of a foe's ever told.
TEST(a_hunt_follows_its_quarry_and_is_not_drawn_to_a_foe_in_the_fog) {
    Campaign c(continent(), 3);
    for (uint8_t s = 0; s < 8; ++s) c.setHuman(s, true);
    c.prepareWorld();
    const Vec3 ro = c.presentedWorld()->region()->origin();
    Battle b(1, Vec3{ro.x + Fixed::fromInt(512), ro.y + Fixed::fromInt(512), Fixed::zero()},
             std::shared_ptr<const Continent>(&continent(), [](const Continent*) {}));
    b.borrow(&c);
    c.onWorldStep([&] { b.beforeWorldStep(); }, [&] { b.afterWorldStep(); }, [&] { b.forgetMen(); });
    std::vector<Campaign::Soldier> men;
    const auto frame = [&] {
        c.visibleSoldiers(c.localSide(), 0, men);
        b.presentConquest(men);
    };
    frame();
    int foot = -1;
    for (size_t i = 0; i < b.squads().size(); ++i)
        if (b.squads()[i].owner == 0 && b.squads()[i].type == UnitType::LightInfantry && b.aliveCount(b.squads()[i]) > 0) foot = static_cast<int>(i);
    CHECK(foot >= 0);
    if (foot < 0) return;
    b.selectSquad(foot);
    const EntityId man = b.squads()[static_cast<size_t>(foot)].members.front();
    // A foe of side 2 close by (the world is the conquest's: this is a test's own man, dropped in).
    World& world = *c.presentedWorld();
    const Vec3 p = world.units()[man - 1].pos;
    const EntityId foe = world.spawnUnit(2, p + Vec3{Fixed::fromInt(12), Fixed::zero(), Fixed::zero()}, {Fixed::one(), Fixed::zero(), Fixed::zero()},
                                         unitStats(UnitType::LightInfantry));
    frame();
    b.selectSquad(foot);
    b.orderAttackUnit(foe);
    b.flushOrders();
    c.step();
    frame();
    CHECK(world.units()[man - 1].targetUnit == foe);
    std::vector<RouteLine> lines = b.pathLines(false, false);
    int hunts = 0;
    for (const RouteLine& l : lines) {
        hunts += l.stance == Stance::Hunt;
        if (l.stance == Stance::Hunt) {
            CHECK(l.quarry == foe);
            const V3 end = l.points.back(), foeAt = b.unitPosition(foe - 1);
            CHECK(std::hypot(end.x - foeAt.x, end.y - foeAt.y) < 0.01f);  // (it ends on him)
        }
    }
    CHECK(hunts >= 1);
    // Side 2 is hidden: no line to him, none of his.
    c.setSight([](uint8_t viewer, uint8_t owner, float, float) { return owner == viewer || owner != 2; });
    lines = b.pathLines(false, false);
    for (const RouteLine& l : lines) CHECK(l.stance != Stance::Hunt);
    // (And the march of a foe is never asked for: only local men have lines at all.)
    for (const RouteLine& l : b.pathLines(true, true)) CHECK(l.stance != Stance::Hunt);
}

// A gate that is shut, opening or closing stops a route at its face; one that stands open lets the line through.
TEST(a_route_stops_at_a_shut_or_swinging_gate_and_goes_through_an_open_one) {
    WorldConfig cfg;
    const Continent::City& town = continent().cities()[Continent::kFactionCount];
    cfg.region = std::make_shared<const Region>(Region::fromContinent(continent(), town.pos, 256));
    World w(cfg);
    const Region& rg = *cfg.region;
    if (rg.gates().empty()) return;
    // The town's own gate (the window holds its neighbours' too, and some of those lie off its edge).
    size_t gi = 0;
    const Vec3 heart = town.pos - rg.origin();
    for (size_t k = 0; k < rg.gates().size(); ++k)
        if ((rg.gates()[k].a - heart).flat().lengthSq() < (rg.gates()[gi].a - heart).flat().lengthSq()) gi = k;
    w.setGateHolder(gi, 1);
    const Gate& g = rg.gates()[gi];
    const Vec3 centre = (g.a + g.b) * Fixed::fromRatio(1, 2);
    // (To open ground behind the doors: 14 m in, or as much farther as a house or a ramp's foot there makes it.)
    int64_t in = 14;
    while (in < 30 && !w.standable(centre - g.out * m(in))) ++in;
    const Vec3 from = centre + g.out * m(14), to = centre - g.out * m(in);
    const auto way = [&] {
        // (A man newly come to the gate each time: the last one has walked on.)
        const EntityId a = w.spawnUnit(0, from, -g.out, unitStats(UnitType::LightInfantry));
        Command mv;
        mv.player = 0;
        mv.unit = a;
        mv.type = Command::Type::Move;
        mv.target = to;
        w.step(std::vector<Command>{mv});
        std::vector<Vec3> out;
        w.routeOf(*w.findUnit(a), out);
        return out;
    };
    const auto stoppedAtFace = [&](const std::vector<Vec3>& out) {
        if (out.empty()) return false;
        const Vec3 last = out.back();
        const Vec3 ab = (g.b - g.a).flat();
        const Fixed t = clamp((last - g.a).flat().dot(ab) / ab.lengthSq(), Fixed::zero(), Fixed::one());
        return ((last - (g.a + ab * t)).flat().length() < Fixed::fromRatio(3, 2)) && (last - to).flat().length() > m(5);
    };
    CHECK(w.gates()[gi].mode == GateState::Mode::Shut);
    CHECK(stoppedAtFace(way()));
    // Swinging open it still blocks (as it does the men), so the line still stops.
    Command pin{1, kNoEntity, Command::Type::SetGate};
    pin.objectIndex = static_cast<int32_t>(gi);
    pin.gatePin = Command::GatePin::Open;
    w.step(std::vector<Command>{pin});
    for (int t = 0; t < 4 && w.gates()[gi].mode != GateState::Mode::Opening; ++t) w.step({});
    CHECK(w.gates()[gi].mode == GateState::Mode::Opening);
    CHECK(stoppedAtFace(way()));
    // Open: through, to the place he was sent.
    for (int t = 0; t < 40 * kTicksPerSecond; ++t) w.step({});
    CHECK(w.gates()[gi].mode == GateState::Mode::Open);
    const std::vector<Vec3> through = way();
    CHECK(!through.empty() && (through.back() - to).flat().length() < m(2));
    // Standing open but unseen by the one asking: the line stops at it as at a shut one (its state is not told).
    {
        const EntityId b = w.spawnUnit(0, from, -g.out, unitStats(UnitType::LightInfantry));
        Command mv;
        mv.player = 0;
        mv.unit = b;
        mv.type = Command::Type::Move;
        mv.target = to;
        w.step(std::vector<Command>{mv});
        std::vector<uint8_t> hidden(rg.gates().size(), 0);
        std::vector<Vec3> seen, unseen;
        w.routeOf(*w.findUnit(b), seen, &hidden);
        hidden[gi] = 1;
        w.routeOf(*w.findUnit(b), unseen, &hidden);
        CHECK(!seen.empty() && (seen.back() - to).flat().length() < m(2));
        CHECK(stoppedAtFace(unseen));
    }
    // And broken, likewise: the gate is no obstacle.
    w.setGatePin(gi, Command::GatePin::Shut);
    for (int t = 0; t < 10 * kTicksPerSecond; ++t) w.step({});
    CHECK(stoppedAtFace(way()));
    w.setGateHp(gi, 0);
    const std::vector<Vec3> broken = way();
    CHECK(!broken.empty() && (broken.back() - to).flat().length() < m(2));
}

// What drawing the paths costs a frame with the whole side selected and marching (the castle's gates among the route's
// stretches): pathLines is what the renderer calls each frame.
TEST(the_paths_of_a_whole_army_cost_little_a_frame) {
    Battle b(42);
    std::vector<EntityId> all;
    for (const Unit& u : b.world().units())
        if (u.alive() && u.owner == Battle::kPlayer) all.push_back(u.id);
    b.select(all);
    b.orderMove(b.squadCentre(b.squads().front(), false) + V3{70, -40, 0});
    b.advance(0.05);
    const int reps = 200;
    const auto t0 = std::chrono::steady_clock::now();
    size_t lines = 0;
    for (int k = 0; k < reps; ++k) lines += b.pathLines(false, false).size();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
    std::printf("  %zu men selected: pathLines %.3f ms a frame (%zu lines)\n", all.size(), ms, lines / reps);
    CHECK(ms < 1.0);
}

// The cost of asking for 200 men's routes in the conquest's world (the whole island: every town's gates).
TEST(two_hundred_routes_across_the_island_are_cheap) {
    Campaign c(continent(), 3);
    for (uint8_t s = 0; s < 8; ++s) c.setHuman(s, true);
    c.prepareWorld();
    World& w = *c.presentedWorld();
    std::vector<Command> go;
    for (const Unit& u : w.units()) {
        if (!u.alive() || go.size() >= 200) continue;
        Command mv;
        mv.player = u.owner;
        mv.unit = u.id;
        mv.type = Command::Type::Move;
        mv.target = u.pos + Vec3{Fixed::fromInt(400), Fixed::fromInt(300), Fixed::zero()};
        go.push_back(mv);
    }
    w.step(go);
    std::vector<Vec3> way;
    size_t points = 0;
    const int reps = 50;
    const auto t0 = std::chrono::steady_clock::now();
    for (int k = 0; k < reps; ++k)
        for (const Command& g : go) {
            w.routeOf(*w.findUnit(g.unit), way);
            points += way.size();
        }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / reps;
    std::printf("  %zu men, %zu waypoints each on average, %zu gates in the world: routeOf %.3f ms for all\n", go.size(),
                points / (reps * std::max<size_t>(1, go.size())), w.gates().size(), ms);
    CHECK(!go.empty());
    CHECK(ms < 1.0);
}

// A foe's marching stack is never drawn, faint or not; a 500-man selection is still a few dozen lines.
TEST(an_enemy_stack_never_has_a_faint_marching_line_and_a_big_selection_is_capped) {
    Campaign c(continent(), 7);
    c.setHuman(0, true);
    for (int t = 0; t < 3 * Campaign::kTicksPerDay; ++t) c.step();
    c.prepareWorld();
    const Vec3 ro = c.presentedWorld()->region()->origin();
    Battle b(1, Vec3{ro.x + Fixed::fromInt(512), ro.y + Fixed::fromInt(512), Fixed::zero()},
             std::shared_ptr<const Continent>(&continent(), [](const Continent*) {}));
    b.borrow(&c);
    c.onWorldStep([&] { b.beforeWorldStep(); }, [&] { b.afterWorldStep(); }, [&] { b.forgetMen(); });
    std::vector<Campaign::Soldier> men;
    c.visibleSoldiers(c.localSide(), 0, men);
    b.presentConquest(men);
    int foes = 0;
    const V3 o = b.origin();
    const std::vector<RouteLine> lines = b.pathLines(false, true);
    for (const Campaign::Stack& s : c.stacks()) {
        if (s.owner == c.localSide() || s.path.empty()) continue;
        ++foes;
        for (const RouteLine& l : lines)
            CHECK(std::hypot(l.points.front().x - (static_cast<float>(s.px) * 0.001f - o.x),
                             l.points.front().y - (static_cast<float>(s.py) * 0.001f - o.y)) > 1.0f);
    }
    std::printf("  %d foes marching, %zu lines drawn\n", foes, lines.size());
    CHECK(foes > 0);
    // Five hundred men, each going somewhere of his own: lines are capped, so are the marks they make.
    std::vector<RouteIn> in;
    for (EntityId i = 1; i <= 500; ++i) in.push_back(route(i, i % 3 == 0 ? Stance::Hunt : Stance::Patrol, {{0, 0, 0}, {static_cast<float>(i) * 7.0f, 20, 0}}, i % 3 == 0 ? i : kNoEntity));
    const std::vector<RouteLine> big = illyria::game::mergeRoutes(in, 8.0f);
    CHECK(big.size() <= illyria::game::kMaxRouteLines);
    // (Each line draws at most 600 marks and the frame at most 3000 decals, two to a mark: renderer_orders.cpp.)
}

// --- The order queue, as the player gives and sees it ----------------------------------------------------------------

namespace {
struct Field {
    Battle b{42};
    illyria::game::Camera cam;
    int watch;
    std::vector<EntityId> chosen;
    V3 home;
    Field() {
        watch = squadNamed(b, "The Grey Watch");
        b.selectSquad(watch);
        chosen = b.selectedAlive();
        home = b.squadCentre(b.squads()[static_cast<size_t>(watch)]);
        cam.setBounds(Battle::kWorldSize);
        cam.setViewport(800, 600);
        cam.setHeightFn([this](float x, float y) { return b.heightAt(x, y); });
        cam.lookAt(home.x, home.y, 120);
    }
    // A right click at a spot `dx`, `dy` metres from the squad's starting place.
    void click(float dx, float dy, bool shift) {
        V3 spot = home + V3{dx, dy, 0};
        spot.z = b.heightAt(spot.x, spot.y);
        float px, py;
        CHECK(cam.project(spot, px, py));
        b.orderTap(cam, px, py, 1.0f, shift);
        b.advance(0.05);
    }
};
}  // namespace

TEST(shift_right_click_queues_and_a_plain_one_replaces_and_stop_clears) {
    Field g;
    const EntityId first = g.chosen.front();
    g.click(-30, 0, false);
    g.click(-30, 25, true);
    g.click(0, 25, true);
    // Every man holds the first march and has two behind it; the Watch is a squad, so all of them.
    for (EntityId id : g.chosen) CHECK(g.b.world().units()[id - 1].queue.size() == 2);
    CHECK(g.b.world().units()[first - 1].hasMoveTarget);
    // Drawn: the march in hand, then a numbered leg for each order behind it, 1 and 2.
    const std::vector<RouteLine> lines = g.b.pathLines(false, false);
    int one = 0, two = 0, hand = 0;
    for (const RouteLine& l : lines) {
        one += l.number == 1;
        two += l.number == 2;
        hand += l.number == 0;
        CHECK(!l.pending);
    }
    CHECK(hand >= 1 && one >= 1 && two >= 1);
    // A plain right click replaces the march and the queue.
    g.click(30, 0, false);
    for (EntityId id : g.chosen) CHECK(g.b.world().units()[id - 1].queue.empty());
    for (const RouteLine& l : g.b.pathLines(false, false)) CHECK(l.number == 0);
    // Queue again, and Space halts: the queue goes too, and they stand.
    g.click(30, 25, true);
    g.click(0, 25, true);
    for (EntityId id : g.chosen) CHECK(!g.b.world().units()[id - 1].queue.empty());
    g.b.stopSelected();
    g.b.advance(0.2);
    for (EntityId id : g.chosen) {
        CHECK(g.b.world().units()[id - 1].queue.empty());
        CHECK(!g.b.world().units()[id - 1].hasMoveTarget);
    }
}

TEST(a_queue_is_drawn_for_the_players_own_men_only_and_a_queued_hunt_of_a_foe_in_the_fog_not_at_all) {
    Field g;
    g.click(-30, 0, false);
    g.click(-30, 25, true);
    // The foe's men may hold queues (the computer's, or another seat's): none is told to the player.
    const size_t before = g.b.pathLines(false, false).size();
    for (const illyria::game::Squad& s : g.b.squads())
        if (s.owner != Battle::kPlayer)
            for (EntityId id : s.members) {
                Command c{s.owner, id, Command::Type::Move, {}};
                c.target = Vec3{m(10), m(10), Fixed::zero()};
                g.b.testQueueFor(id, c);
            }
    CHECK(g.b.pathLines(false, false).size() == before);
}

TEST(a_squad_ordered_a_then_b_walks_a_then_b_in_a_battle) {
    Field g;
    g.click(-25, 0, false);
    g.click(-25, 25, true);
    const EntityId id = g.chosen.front();
    const auto flat = [&](const V3& a) { return V3{a.x - g.b.unitPosition(id - 1).x, a.y - g.b.unitPosition(id - 1).y, 0}.length(); };
    V3 a = g.home + V3{-25, 0, 0}, bb = g.home + V3{-25, 25, 0};
    bool atA = false;
    for (int t = 0; t < 600 && !(atA && flat(bb) < 8.0f); ++t) {
        g.b.advance(0.1);
        if (flat(a) < 8.0f) atA = true;
    }
    CHECK(atA);
    CHECK(flat(bb) < 8.0f);
}

// --- Planning: orders held on the client until the release ----------------------------------------------------------

TEST(planning_holds_orders_off_the_sim_and_the_release_sends_them_in_one_tick_as_if_given_then) {
    Field g;
    g.b.setPlanning(true);
    CHECK(g.b.planning());
    g.click(-30, 0, false);
    g.click(-30, 25, true);
    g.click(0, 25, true);
    g.b.advance(1.0);
    // Nothing reached the sim: nobody is marching, nothing queued; the orders are held.
    for (EntityId id : g.chosen) {
        CHECK(!g.b.world().units()[id - 1].hasMoveTarget);
        CHECK(g.b.world().units()[id - 1].queue.empty());
    }
    CHECK(g.b.plannedCount() == 3 * g.chosen.size());
    // Drawn, for every man that has any, in the held look, numbered 1 to 3.
    int pending = 0, three = 0;
    for (const RouteLine& l : g.b.pathLines(false, false)) {
        pending += l.pending;
        three += l.pending && l.number == 3;
        CHECK(l.pending);  // (nothing else is under way)
    }
    CHECK(pending >= 3 && three >= 1);
    // Even with the men deselected.
    g.b.clearSelection();
    CHECK(!g.b.pathLines(false, false).empty());
    // The release: all at once, as if just given in that order.
    g.b.setPlanning(false);
    CHECK(!g.b.planning());
    CHECK(g.b.plannedCount() == 0);
    g.b.advance(0.05);
    for (EntityId id : g.chosen) {
        CHECK(g.b.world().units()[id - 1].hasMoveTarget);
        CHECK(g.b.world().units()[id - 1].queue.size() == 2);
    }
}

TEST(a_plain_order_held_replaces_the_held_ones_and_backspace_drops_them_all) {
    Field g;
    g.b.setPlanning(true);
    g.click(-30, 0, false);
    g.click(-30, 25, true);
    g.click(30, 0, false);  // replaces both
    CHECK(g.b.plannedCount() == g.chosen.size());
    g.b.discardPlan();
    CHECK(!g.b.planning());
    CHECK(g.b.plannedCount() == 0);
    g.b.advance(0.5);
    for (EntityId id : g.chosen) CHECK(!g.b.world().units()[id - 1].hasMoveTarget);
}

TEST(halt_while_planning_halts_at_once_and_drops_that_mens_held_orders_and_the_dead_are_skipped) {
    Field g;
    g.click(-30, 0, false);  // really marching
    g.b.setPlanning(true);
    g.click(-30, 25, true);
    CHECK(g.b.plannedCount() == g.chosen.size());
    g.b.stopSelected();
    CHECK(g.b.plannedCount() == 0);
    g.b.advance(0.2);
    for (EntityId id : g.chosen) CHECK(!g.b.world().units()[id - 1].hasMoveTarget);  // halted for real, at once
    // Held again; one man dead before the release is skipped, the rest go.
    g.click(0, 25, false);
    const EntityId doomed = g.chosen.front();
    g.b.testRemove(doomed);
    g.b.setPlanning(false);
    g.b.advance(0.05);
    int marching = 0;
    for (EntityId id : g.chosen) marching += g.b.world().units()[id - 1].hasMoveTarget;
    CHECK(marching == static_cast<int>(g.chosen.size()) - 1);
}
