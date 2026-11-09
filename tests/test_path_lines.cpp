// The paths soldiers will walk, drawn on the map (path_lines.h, renderer_orders.cpp): the sim's read-only route and
// stance (World::routeOf, World::stanceOf), men going one way merged into one line, and only the player's own paths
// ever told.
#include <algorithm>
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
    CHECK(out.size() == 1 && out[0].x == w.findUnit(foe)->pos.x);  // (after the quarry, wherever he is)
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
