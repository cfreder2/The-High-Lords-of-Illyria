// Castle ramps and the wide wall-walk (docs/GAME_DESIGN.md, castles): ramps
// start on the ground inside the walls and reach the walk, men on foot climb
// them to any embrasure and horses do not, two men pass each other on the
// walk, and a town's garrison fits inside its smaller ring.
#include <cstdio>
#include <memory>
#include <span>

#include "illyria/game/battle.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/nav.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
using Castle = Continent::Castle;
Fixed m(int64_t v) { return Fixed::fromInt(v); }

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
// The first neutral town and the first capital, each with its battle window.
const Continent::City& town() { return continent().cities()[Continent::kFactionCount]; }
const Continent::City& capital() { return continent().cities().front(); }
std::shared_ptr<const Region> window(const Continent::City& city) {
    return std::make_shared<const Region>(Region::fromContinent(continent(), city.pos, 256));
}
std::shared_ptr<const Region> townWindow() {
    static const auto r = window(town());
    return r;
}
// The rampart round this point (the town's outer ring is the one whose centre is nearest).
const Rampart& outer(const Region& r, const Vec3& centre) {
    const Rampart* best = &r.ramparts().front();
    for (const Rampart& rp : r.ramparts())
        if ((rp.centre - centre).flat().length() < (best->centre - centre).flat().length() && rp.corners.size() >= best->corners.size()) best = &rp;
    return *best;
}
Vec3 local(const Region& r, const Vec3& p) { return {p.x - r.origin().x, p.y - r.origin().y, p.z}; }

// Walks the unit until it stops (or the time runs out); true if it got within 1 m.
bool reach(World& w, EntityId id, const Vec3& to, int seconds, Fixed* lowest = nullptr) {
    const Command go{0, id, Command::Type::Move, to};
    w.step(std::span(&go, 1));
    for (int t = 0; t < seconds * kTicksPerSecond; ++t) {
        w.step({});
        const Unit* u = w.findUnit(id);
        if (lowest) *lowest = min(*lowest, u->pos.z);
        if (!u->hasMoveTarget) break;
    }
    return (w.findUnit(id)->pos - to).flat().length() < Fixed::one();
}
}  // namespace

TEST(castle_ramps_start_on_the_ground_and_reach_the_walk) {
    // Every town and capital: each ramp's slope begins at the ground under
    // its foot and ends at its ring's walk; outer rings get three, inner baileys one.
    for (const Continent::City& city : continent().cities()) {
        const Castle& k = city.castle;
        CHECK(k.ramps.size() == (city.capital ? 4u : 3u));
        for (const Castle::Ramp& rp : k.ramps) {
            CHECK(rp.foot.z == city.pos.z);
            CHECK(rp.head.z == city.pos.z + k.rings[rp.ring].walk());
            // Gentle: no steeper than 1 in 2.
            CHECK((rp.head - rp.foot).flat().length() >= k.rings[rp.ring].walk() * 2 - Fixed::fromRatio(1, 10));
        }
    }
    // In battle the ground under the foot is the pad, exactly, and the deck starts there.
    for (const Continent::City* city : {&town(), &capital()}) {
        const auto r = window(*city);
        size_t ramps = 0;
        for (const Rampart& rp : r->ramparts()) {
            if ((rp.centre - local(*r, city->pos)).flat().length() > Fixed::one()) continue;  // a neighbour's
            for (const auto& [foot, head] : rp.ramps) {
                ++ramps;
                const Fixed ground = r->terrain().heightAt(foot.x, foot.y);
                if (abs(ground - city->pos.z) >= Fixed::fromRatio(1, 20))
                    std::printf("  %s: ramp foot (%.1f, %.1f) ground %.2f pad %.2f\n", city->name.c_str(), foot.x.toDouble(), foot.y.toDouble(), ground.toDouble(), city->pos.z.toDouble());
                CHECK(abs(ground - city->pos.z) < Fixed::fromRatio(1, 20));
                // (Where the wall is short the foot lies under a tower's gallery.)
                Fixed deck;
                CHECK(r->deckBelow(foot.x, foot.y, ground + Fixed::one(), deck));
                CHECK(abs(deck - ground) < Fixed::fromRatio(1, 5));
                CHECK(abs(head.z - rp.corners.front().z) < Fixed::fromRatio(1, 20));
            }
        }
        CHECK(ramps == city->castle.ramps.size());
    }
}

TEST(walls_are_thick_enough_to_pass_on_the_walk) {
    for (const Continent::City& city : continent().cities())
        for (const Castle::Ring& ring : city.castle.rings) CHECK(ring.thick - Castle::kParapetThick >= m(3));
    // Two men walking opposite ways along the same stretch of walk pass each other.
    const Region& r = *townWindow();
    const Vec3 centre = local(r, town().pos);
    const Rampart& rp = outer(r, centre);
    // The longest edge of the ring without the gate (whose towers stand
    // either side of the middle), on the walk's middle, 24 m apart.
    size_t e = 0;
    Fixed longest;
    for (size_t i = 0; i < rp.corners.size(); ++i) {
        const Fixed len = (rp.corners[(i + 1) % rp.corners.size()] - rp.corners[i]).flat().length();
        if (len > longest && i != town().castle.rings.front().gateEdge) {
            longest = len;
            e = i;
        }
    }
    const Vec3 a = rp.corners[e], b = rp.corners[(e + 1) % rp.corners.size()];
    const Vec3 dir = (b - a).flat() / longest;
    const Vec3 p = a + dir * (longest / 2 - m(12)), q = a + dir * (longest / 2 + m(12));
    WorldConfig cfg;
    cfg.region = townWindow();
    World w(cfg);
    const EntityId one = w.spawnUnit(0, p, dir, unitStats(UnitType::HeavyInfantry));
    const EntityId two = w.spawnUnit(0, q, -dir, unitStats(UnitType::HeavyInfantry));
    const Command go[2] = {{0, one, Command::Type::Move, q}, {0, two, Command::Type::Move, p}};
    w.step(std::span(go, 2));
    int ticks = 0;
    Fixed lowest = a.z;
    for (; ticks < 40 * kTicksPerSecond; ++ticks) {
        w.step({});
        lowest = min(lowest, min(w.findUnit(one)->pos.z, w.findUnit(two)->pos.z));
        if (!w.findUnit(one)->hasMoveTarget && !w.findUnit(two)->hasMoveTarget) break;
    }
    std::printf("  passed on a %.1f m walk in %.1f s\n", (rp.halfThick * 2 - Castle::kParapetThick).toDouble(),
                static_cast<double>(ticks) / kTicksPerSecond);
    CHECK((w.findUnit(one)->pos - q).flat().length() < Fixed::one());
    CHECK((w.findUnit(two)->pos - p).flat().length() < Fixed::one());
    CHECK(ticks < 20 * kTicksPerSecond);  // 24 m at a walk: no standing about
    CHECK(lowest > a.z - Fixed::fromRatio(1, 2));  // neither fell off
}

TEST(men_climb_the_ramps_to_any_embrasure_and_horses_stay_below) {
    for (const Continent::City* city : {&town(), &capital()}) {
        const auto r = window(*city);
        WorldConfig cfg;
        cfg.region = r;
        World w(cfg);
        const Vec3 yard = local(*r, city->castle.courtyard);
        // The embrasure farthest round the walls from the gate, on every ring.
        for (const Rampart& rp : r->ramparts()) {
            if (rp.embrasures.empty() || rp.ramps.empty() || (rp.centre - yard).flat().length() > city->radius * 2) continue;
            Vec3 post = rp.embrasures.front().pos;
            for (const Rampart::Embrasure& e : rp.embrasures)
                if ((e.pos - yard).flat().length() > (post - yard).flat().length()) post = e.pos;
            // From the courtyard (or, inside an inner bailey, from the foot of its ramp).
            const Vec3 middle = local(*r, city->pos);
            const bool inner = &rp != &outer(*r, middle);
            const Vec3 from = inner ? rp.ramps.front().first : yard;
            for (UnitType type : {UnitType::LightInfantry, UnitType::Archers, UnitType::Dwarves}) {
                const EntityId id = w.spawnUnit(0, from, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(type));
                const bool there = reach(w, id, post, 240);
                if (!there) std::printf("  %s: %s did not reach the embrasure\n", city->name.c_str(), unitTypeName(type));
                CHECK(there);
                CHECK(w.findUnit(id)->pos.z > post.z - Fixed::fromRatio(1, 2));
            }
        }
        // Cavalry ordered onto the walls stays on the ground.
        const Vec3 centre = local(*r, city->pos);
        const Rampart& rp = outer(*r, centre);
        const EntityId horse = w.spawnUnit(0, yard, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::HeavyCavalry));
        reach(w, horse, rp.embrasures.front().pos, 60);
        CHECK(w.findUnit(horse)->pos.z < city->pos.z + m(1));
    }
}

TEST(griffins_hop_up_onto_the_walk) {
    // A short way from the courtyard to the walk: too short a flight to take
    // wing for on open ground, but they cannot climb, so they fly up.
    const Region& r = *townWindow();
    const Vec3 yard = local(r, town().castle.courtyard);
    const Vec3 centre = local(r, town().pos);
    const Rampart& rp = outer(r, centre);
    Vec3 post = rp.embrasures.front().pos;
    for (const Rampart::Embrasure& e : rp.embrasures)
        if ((e.pos - yard).flat().length() < (post - yard).flat().length()) post = e.pos;
    WorldConfig cfg;
    cfg.region = townWindow();
    World w(cfg);
    const EntityId id = w.spawnUnit(0, yard, {Fixed::one(), Fixed::zero(), Fixed::zero()}, unitStats(UnitType::Griffins));
    reach(w, id, post, 90);
    const Unit& u = *w.findUnit(id);
    std::printf("  griffin %.1f m from the embrasure, %.1f m up\n", (u.pos - post).flat().length().toDouble(), (u.pos.z - town().pos.z).toDouble());
    CHECK((u.pos - post).flat().length() < m(3));
    CHECK(u.pos.z > post.z - Fixed::one());
}

TEST(nobody_walks_through_a_ramp) {
    // The ramp is solid: the nav grid closes the ground under its high end, and
    // a man walking at its side cannot step into it.
    const Region& r = *townWindow();
    const NavGrid nav(r);
    const Vec3 centre = local(r, town().pos);
    const Rampart& rp = outer(r, centre);
    const auto& [foot, head] = rp.ramps.front();
    const Vec3 dir = (head - foot).flat() / (head - foot).flat().length();
    const Vec3 high = foot + dir * ((head - foot).flat().length() * 3 / 4);
    CHECK(r.inRamp(high.x, high.y, town().pos.z + Fixed::one()));
    CHECK(!r.inRamp(foot.x, foot.y, town().pos.z + Fixed::one()));
    // Beside the ramp (its open side), walking past along it, he stays on the ground.
    const Vec3 side{-dir.y, dir.x, Fixed::zero()};
    Vec3 in = side;
    if ((foot + side * m(4) - rp.centre).flat().length() > (foot - side * m(4) - rp.centre).flat().length()) in = -side;
    WorldConfig cfg;
    cfg.region = townWindow();
    World w(cfg);
    const Vec3 start = foot - dir * m(4) + in * m(4), end = head + in * m(4);
    const EntityId id = w.spawnUnit(0, {start.x, start.y, town().pos.z}, dir, unitStats(UnitType::LightInfantry));
    CHECK(reach(w, id, {end.x, end.y, town().pos.z}, 60));
    CHECK(w.findUnit(id)->pos.z < town().pos.z + Fixed::one());
}

TEST(a_towns_garrison_fits_inside_its_walls) {
    // Eight armies holding a town: every man starts on open ground inside the
    // walls (not in a house, a wall, a tower or a ramp) or on the walk.
    illyria::game::BattlePlan plan;
    const size_t city = Continent::kFactionCount;
    plan.city = static_cast<int>(city);
    plan.centre = continent().cities()[city].pos;
    plan.foeName = "Garrison";
    const UnitType types[8] = {UnitType::Archers, UnitType::Pikemen, UnitType::LightInfantry, UnitType::HeavyInfantry,
                               UnitType::Archers, UnitType::Dwarves, UnitType::HeavyCavalry, UnitType::Warrior};
    for (uint32_t i = 0; i < 8; ++i) plan.foe.push_back({100 + i, types[i], "", 12});
    plan.player.push_back({1, UnitType::LightInfantry, "", 12});
    illyria::game::Battle b(5, plan);
    const Region& r = *b.world().region();
    const NavGrid nav(r);
    const Continent::City& t = b.continent().cities()[city];
    const Vec3 centre = local(r, t.pos);
    const Rampart& rp = outer(r, centre);
    int men = 0, misplaced = 0;
    for (const illyria::game::Squad& s : b.squads()) {
        if (s.owner == 0) continue;
        for (EntityId id : s.members) {
            const Unit& u = *b.world().findUnit(id);
            ++men;
            const bool onWalk = u.pos.z > t.pos.z + m(3);
            const bool inside = onWalk || Gate::inside(rp, u.pos);
            if (!inside || (!onWalk && nav.blocked(u.pos))) {
                ++misplaced;
                std::printf("  %s at (%.1f, %.1f) %s\n", s.name.c_str(), u.pos.x.toDouble(), u.pos.y.toDouble(), inside ? "on closed ground" : "outside");
            }
        }
    }
    CHECK(men == 8 * 12);
    CHECK(misplaced == 0);
}

TEST(every_tower_has_a_gallery_clear_of_the_ramps_and_houses) {
    // Round the inner side of every corner and gate tower, at the walk. The
    // way round the walk keeps a man's breadth (and his lane) off every
    // tower and goes on round, never back; no ramp's landing or house
    // stands in a gallery.
    Fixed rampRoom = m(100), houseRoom = m(100), towerRoom = m(100), shortest = m(100);
    for (const Continent::City& city : continent().cities()) {
        const Castle& k = city.castle;
        for (size_t ri = 0; ri < k.rings.size(); ++ri) {
            const Castle::Ring& ring = k.rings[ri];
            const std::vector<Castle::Gallery> gs = ring.galleries();
            CHECK(gs.size() == ring.corners.size() + 2);
            const Vec3 middle = ring.centre();
            for (const Castle::Gallery& g : gs) {
                CHECK((g.pos + g.mid * (g.radius + Castle::kGalleryWidth) - middle).flat().length() < (g.pos - middle).flat().length());
                const Fixed reach = g.radius + Castle::kGalleryWidth;
                for (const Castle::Ramp& rp : k.ramps) {
                    if (rp.ring != ri) continue;
                    const Vec3 dir = (rp.head - rp.foot).flat() / (rp.head - rp.foot).flat().length(), across{-dir.y, dir.x, Fixed::zero()};
                    for (int s = 0; s <= 6; ++s)  // over the landing
                        for (int w = -2; w <= 2; ++w) {
                            const Vec3 p = rp.head + dir * (Castle::kRampLanding * s / 6) + across * (rp.halfWidth * w / 2);
                            rampRoom = min(rampRoom, (p - g.pos).flat().length() - reach);
                        }
                }
                for (const Castle::House& h : k.houses) {
                    if ((h.pos - g.pos).flat().dot(g.mid) <= Fixed::zero()) continue;  // on the tower's far side
                    const Fixed dx = max(abs(h.pos.x - g.pos.x) - h.halfX, Fixed::zero()), dy = max(abs(h.pos.y - g.pos.y) - h.halfY, Fixed::zero());
                    houseRoom = min(houseRoom, sqrt(dx * dx + dy * dy) - reach);
                }
            }
            // The way: off every tower by at least a lane and a man's breadth,
            // along legs that each go on round (the ring's turn, never back).
            const std::vector<Vec3> way = ring.way();
            for (size_t i = 0; i < way.size(); ++i) {
                const Vec3 a = way[i], b = way[(i + 1) % way.size()];
                shortest = min(shortest, (b - a).flat().length());
                for (int s = 0; s < 4; ++s) {
                    const Vec3 p = a + (b - a) * Fixed::fromRatio(s, 4);
                    for (const Castle::Gallery& g : gs) towerRoom = min(towerRoom, (p - g.pos).flat().length() - g.radius);
                }
            }
        }
    }
    std::printf("  room: %.2f m from the way to a tower, %.1f m to a ramp's landing, %.1f m to a house; shortest leg %.2f m\n", towerRoom.toDouble(),
                rampRoom.toDouble(), houseRoom.toDouble(), shortest.toDouble());
    CHECK(towerRoom > Fixed::one());
    CHECK(shortest > Fixed::zero());
    CHECK(rampRoom > Fixed::zero());
    CHECK(houseRoom > Fixed::zero());
}

TEST(men_go_round_the_towers_on_their_galleries) {
    // Along the walk past a corner tower, and along the gate's wall past both
    // of the gate's towers: they get there at the walk's height, and never
    // set foot inside a tower.
    const Region& r = *townWindow();
    const Vec3 centre = local(r, town().pos);
    const Rampart& rp = outer(r, centre);
    const Castle::Ring& ring = town().castle.rings.front();
    const size_t n = rp.corners.size(), g = ring.gateEdge;
    std::vector<Obstacle> towers;
    for (const Obstacle& o : r.obstacles())
        if (o.kind == Obstacle::Kind::Tower && (o.pos - centre).flat().length() < town().radius * 2) towers.push_back(o);
    const auto point = [&](size_t e, Fixed fromEnd, bool atStart) {
        const Vec3 a = rp.corners[e], b = rp.corners[(e + 1) % n];
        const Fixed len = (b - a).flat().length();
        return a + (b - a) * ((atStart ? fromEnd : len - fromEnd) / len);
    };
    const std::pair<Vec3, Vec3> trips[2] = {
        {point((g + 1) % n, m(16), false), point((g + 2) % n, m(16), true)},  // round the corner beyond the gate's edge
        {point(g, m(18), true), point(g, m(18), false)},                     // along the gate's edge, over the gate
    };
    for (const auto& [from, to] : trips) {
        WorldConfig cfg;
        cfg.region = townWindow();
        World w(cfg);
        const EntityId id = w.spawnUnit(0, from, (to - from).flat() / (to - from).flat().length(), unitStats(UnitType::HeavyInfantry));
        const Command go{0, id, Command::Type::Move, to};
        w.step(std::span(&go, 1));
        Fixed lowest = from.z, closest = m(100);
        int ticks = 0;
        for (; ticks < 60 * kTicksPerSecond; ++ticks) {
            w.step({});
            const Unit& u = *w.findUnit(id);
            lowest = min(lowest, u.pos.z);
            for (const Obstacle& o : towers) closest = min(closest, (u.pos - o.pos).flat().length() - o.radius);
            if (!u.hasMoveTarget) break;
        }
        const Fixed off = (w.findUnit(id)->pos - to).flat().length();
        std::printf("  %.1f m in %.1f s; %.2f m off at the end, %.2f m clear of the towers, lowest %.2f m below the walk\n",
                    (to - from).flat().length().toDouble(), static_cast<double>(ticks) / kTicksPerSecond, off.toDouble(), closest.toDouble(),
                    (from.z - lowest).toDouble());
        CHECK(off < Fixed::one());
        CHECK(lowest > from.z - Fixed::fromRatio(1, 2));
        CHECK(closest > Fixed::zero());
    }
}
