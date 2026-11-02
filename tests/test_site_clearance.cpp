// Soldiers and the sites' stone (Region::fromContinent, NavGrid, World::
// separateBodies): what is solid in battle is what is drawn (a ruin's
// stepped base and its stones from its 3D model, a temple's columns and hall,
// a sage's tower, a shrine's pillars and altar), men keep their drawn breadth
// from it (stoneStandoff), and a squad sent past one goes round it the plain
// way, never stuck grinding along its face.
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <memory>
#include <span>
#include <vector>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"
#include "test.h"

using namespace illyria::sim;

namespace {
// The drawn ruin's footprint (tools/models/ruin_stones.py): kRuinDrawnCells, kRuinDrawnGround, kRuinDrawnFloor.
#include "../engine/sim/src/site_ruin_drawn.inc"

Fixed m(int64_t v) { return Fixed::fromInt(v); }

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// A site in its battle window: model metres (unturned, from its middle) to
// the region's and back.
struct SiteView {
    const Continent::Site* site = nullptr;
    std::shared_ptr<const Region> region;
    Vec3 at;  // region local
    double size = 0;
    int turns = 0;
    Vec3 toWorld(double x, double y) const {
        double px = x, py = y;
        for (int k = 0; k < turns; ++k) {
            const double t = px;
            px = -py;
            py = t;
        }
        return {at.x + Fixed::fromRatio(static_cast<int64_t>(std::lround(px * 1000)), 1000),
                at.y + Fixed::fromRatio(static_cast<int64_t>(std::lround(py * 1000)), 1000), at.z};
    }
    void toModel(const Vec3& p, double& x, double& y) const {
        double px = (p.x - at.x).toDouble(), py = (p.y - at.y).toDouble();
        for (int k = 0; k < turns; ++k) {
            const double t = px;
            px = py;
            py = -t;
        }
        x = px;
        y = py;
    }
};

const SiteView& siteView(Continent::Site::Kind kind) {
    static std::deque<std::pair<Continent::Site::Kind, SiteView>> made;  // (references to them are kept)
    for (const auto& [k, v] : made)
        if (k == kind) return v;
    const Continent& c = continent();
    SiteView v;
    // Of its kind, the one with the clearest ground round it in its window
    // (men walk round it 30 m out: past no other stone, tree or water, and
    // not into a wood, a lake or a town), the first with none.
    size_t pick = c.sites().size();
    int fewest = 1 << 30;
    for (size_t i = 0; i < c.sites().size() && fewest > 0; ++i) {
        if (c.sites()[i].kind != kind) continue;
        const Region r = Region::fromContinent(c, c.sites()[i].pos, 256);
        const Vec3 at{c.sites()[i].pos.x - r.origin().x, c.sites()[i].pos.y - r.origin().y, Fixed::zero()};
        int in = 0;
        for (const Obstacle& o : r.obstacles())
            in += o.solid && o.variant != Obstacle::kSiteStone && (o.pos - at).flat().length() < m(40);
        for (int64_t dy = -40; dy <= 40; dy += 4)
            for (int64_t dx = -40; dx <= 40; dx += 4) {
                const Ground g = r.groundAt(at.x + m(dx), at.y + m(dy));
                in += g != Ground::Plains && g != Ground::Road && g != Ground::Mud && g != Ground::Sand;
            }
        if (in < fewest) {
            fewest = in;
            pick = i;
        }
    }
    for (size_t i = pick; i < c.sites().size(); ++i) {
        if (c.sites()[i].kind != kind) continue;
        v.site = &c.sites()[i];
        v.region = std::make_shared<const Region>(Region::fromContinent(c, v.site->pos, 256));
        v.at = {v.site->pos.x - v.region->origin().x, v.site->pos.y - v.region->origin().y, v.site->pos.z};
        v.size = Continent::Site::size(kind);
        v.turns = Continent::Site::turns(i);
        break;
    }
    made.push_back({kind, v});
    return made.back().second;
}

double boxGap(double x, double y, double x0, double y0, double x1, double y1) {
    const double dx = std::max({x0 - x, x - x1, 0.0}), dy = std::max({y0 - y, y - y1, 0.0});
    return std::sqrt(dx * dx + dy * dy);
}

// The drawn ruin at a man's level (on the ground or up on the floor): the
// distance from a point (model metres) to the nearest drawn square, looked
// for out to `reach`.
bool drawnAt(const char* const rows[], int i, int j) {
    if (i < 0 || j < 0 || i >= kRuinDrawnCells || j >= kRuinDrawnCells) return false;
    const char c = rows[j][i / 4];
    const int v = c <= '9' ? c - '0' : c - 'a' + 10;
    return (v >> (3 - i % 4)) & 1;
}
double ruinGap(const char* const rows[], double size, double x, double y, double reach) {
    const double cell = size / kRuinDrawnCells, lo = -size / 2;
    double best = reach;
    const int i0 = static_cast<int>(std::floor((x - reach - lo) / cell)), i1 = static_cast<int>(std::floor((x + reach - lo) / cell));
    const int j0 = static_cast<int>(std::floor((y - reach - lo) / cell)), j1 = static_cast<int>(std::floor((y + reach - lo) / cell));
    for (int j = j0; j <= j1; ++j)
        for (int i = i0; i <= i1; ++i)
            if (drawnAt(rows, i, j)) best = std::min(best, boxGap(x, y, lo + i * cell, lo + j * cell, lo + (i + 1) * cell, lo + (j + 1) * cell));
    return best;
}

// The temple as game/render/meshes.cpp temple() draws it: two rows of six
// square columns and the hall between, in its size's thousandths.
double templeGap(double size, double x, double y) {
    double best = 1e9;
    const double k = size / 1000;
    for (int i = 0; i < 6; ++i)
        for (int side = -1; side <= 1; side += 2) {
            const double cx = (-420 + i * 168) * k, cy = side * 250 * k;
            best = std::min(best, boxGap(x, y, cx - 30 * k, cy - 30 * k, cx + 30 * k, cy + 30 * k));
        }
    return std::min(best, boxGap(x, y, -300 * k, -180 * k, 300 * k, 180 * k));
}

// A squad of `men` in ranks of four marched (as Battle::sendMarch orders it)
// from `from` to `to` (model metres): how it went.
struct March {
    int men = 0, arrived = 0, stalled = 0;
    double seconds = 0;
    double worst = 1e9;  // the least, over men and ticks, of the gap from drawn stone less his standoff
};
using Gap = std::function<double(const Vec3& world, double reach)>;

March march(const SiteView& v, double fx, double fy, double tx, double ty, UnitType type, int men, const Gap& gap) {
    WorldConfig cfg;
    cfg.region = v.region;
    World w(cfg);
    const Vec3 from = v.toWorld(fx, fy), to = v.toWorld(tx, ty);
    const Vec3 dir = (to - from).flat() / (to - from).flat().length();
    const Vec3 side{-dir.y, dir.x, Fixed::zero()};
    const UnitStats st = unitStats(type);
    const Fixed spacing = st.radius * 2 + Fixed::fromRatio(1, 2);
    std::vector<EntityId> ids;
    std::vector<Command> orders;
    for (int i = 0; i < men; ++i) {
        const Fixed f = spacing * (i % 4) - spacing * 3 / 2, r = spacing * (i / 4);
        const Vec3 p = from + side * f - dir * r;
        ids.push_back(w.spawnUnit(0, {p.x, p.y, v.region->terrain().heightAt(p.x, p.y)}, dir, st));
        Command c{0, ids.back(), Command::Type::Move, to + side * f - dir * r};
        c.march = true;
        c.marchFrom = from;
        c.marchTo = to;
        c.speed = st.moveSpeed * 9 / 10;
        orders.push_back(c);
    }
    w.step(std::span(orders.data(), orders.size()));
    March out;
    out.men = men;
    const double standoff = stoneStandoff(st.radius).toDouble();
    // Stuck: under a tenth of the way he would walk in three seconds, made in three seconds.
    const int window = 3 * kTicksPerSecond;
    const double least = (st.moveSpeed * 9 / 10).toDouble() * 3 / 10;
    std::vector<std::vector<Vec3>> trail(ids.size());
    std::vector<bool> stuck(ids.size(), false);
    const double straight = (to - from).flat().length().toDouble() / (st.moveSpeed * 9 / 10).toDouble();
    const int limit = static_cast<int>((straight * 2 + 20) * kTicksPerSecond);
    int t = 0;
    for (; t < limit; ++t) {
        w.step({});
        bool moving = false;
        for (size_t i = 0; i < ids.size(); ++i) {
            const Unit& u = *w.findUnit(ids[i]);
            trail[i].push_back(u.pos);
            out.worst = std::min(out.worst, gap(u.pos, standoff + 1) - standoff);
            moving = moving || u.hasMoveTarget;
            if (u.hasMoveTarget && !stuck[i] && trail[i].size() > static_cast<size_t>(window) &&
                (u.pos - trail[i][trail[i].size() - 1 - window]).flat().length().toDouble() < least) {
                stuck[i] = true;
                ++out.stalled;
                double mx, my;
                v.toModel(u.pos, mx, my);
                std::printf("    man %zu stuck at %.1f s, at (%.1f, %.1f) of the site\n", i, t / double(kTicksPerSecond), mx, my);
            }
        }
        if (!moving) break;
    }
    out.seconds = t / double(kTicksPerSecond);
    for (size_t i = 0; i < ids.size(); ++i) {
        // Got to his place (horses arriving after him may shove him a little off it since).
        const Unit& u = *w.findUnit(ids[i]);
        out.arrived += u.hasMoveTarget ? 0 : 1;
        if (u.hasMoveTarget) {
            double mx, my, gx, gy;
            v.toModel(u.pos, mx, my);
            v.toModel(orders[i].target, gx, gy);
            std::printf("    man %zu still at (%.1f, %.1f) of the site, sent to (%.1f, %.1f); path", i, mx, my, gx, gy);
            for (size_t k = 0; k < u.path.size(); ++k) {
                double px, py;
                v.toModel(u.path[k], px, py);
                std::printf("%s(%.1f, %.1f)", k == u.pathIndex ? " >" : " ", px, py);
            }
            std::printf("\n");
        }
    }
    std::printf("  %s (%.0f, %.0f) to (%.0f, %.0f): %d of %d there in %.1f s (straight: %.1f s), %d stuck, nearest %.2f m inside his standoff\n",
                unitTypeName(type), fx, fy, tx, ty, out.arrived, out.men, out.seconds, straight, out.stalled, std::max(0.0, -out.worst));
    return out;
}

// Marches past a ruin, round its corners and along its faces.
struct Run {
    double fx, fy, tx, ty;
};
const std::vector<Run>& ruinRuns() {
    static const std::vector<Run> runs = {
        {-35, 4, 35, 4},     // across it, round the side away from the stair or the other
        {35, 4, -35, 4},     // and back
        {-30, -20, 30, 30},  // past the front corner, diagonally
        {0, 35, -25, -25},   // from behind it to the front, past a back corner
        {-30, 14, 30, 14},   // along its back, close by
        {20, -25, 0, 40},    // up past the stair and a side
    };
    return runs;
}
Gap ruinGround(const SiteView& v) {
    return [&v](const Vec3& p, double reach) {
        // Only on the ground (these marches never climb it).
        if (p.z > v.at.z + Fixed::one()) return reach;
        double x, y;
        v.toModel(p, x, y);
        return ruinGap(kRuinDrawnGround, v.size, x, y, reach);
    };
}
}  // namespace

TEST(the_ruin_is_solid_in_battle_where_it_is_drawn) {
    // Every square of the drawn ruin a man would stand in (tools/models/
    // ruin_stones.py) lies inside its stepped base or one of its stones: on
    // the ground, and up on its floor.
    const SiteView& v = siteView(Continent::Site::Kind::Ruin);
    const Region& r = *v.region;
    CHECK(r.floors().size() >= 1);
    const Floor* fl = &r.floors().front();
    for (const Floor& f : r.floors())
        if (((f.baseLo + f.baseHi) * Fixed::fromRatio(1, 2) - v.at).flat().lengthSq() < ((fl->baseLo + fl->baseHi) * Fixed::fromRatio(1, 2) - v.at).flat().lengthSq())
            fl = &f;
    const Deck& deck = r.decks()[fl->deck];
    // The base is the plinth the floor stands on: wider than the floor all round but the front.
    CHECK(fl->baseLo.x < deck.centre.x - deck.halfSpan - Fixed::fromRatio(3, 10));
    CHECK(fl->baseHi.x > deck.centre.x + deck.halfSpan + Fixed::fromRatio(3, 10));
    const double slack = 0.05;
    const auto covered = [&](const Vec3& p, bool up) {
        if (!up && p.x.toDouble() > fl->baseLo.x.toDouble() - slack && p.x.toDouble() < fl->baseHi.x.toDouble() + slack &&
            p.y.toDouble() > fl->baseLo.y.toDouble() - slack && p.y.toDouble() < fl->baseHi.y.toDouble() + slack)
            return true;
        for (const Obstacle& o : r.obstacles()) {
            if (o.variant != Obstacle::kSiteStone || (o.pos - p).flat().lengthSq() > m(64)) continue;
            if (deck.contains(o.pos.x, o.pos.y) != up) continue;  // the floor's stones for the floor, the rest for the ground
            const double dx = std::abs((p.x - o.pos.x).toDouble()), dy = std::abs((p.y - o.pos.y).toDouble());
            if (o.kind == Obstacle::Kind::House ? dx < o.halfX.toDouble() + slack && dy < o.halfY.toDouble() + slack
                                                : std::hypot(dx, dy) < o.radius.toDouble() + slack)
                return true;
        }
        return false;
    };
    const double cell = v.size / kRuinDrawnCells;
    int drawn[2] = {0, 0}, bare[2] = {0, 0};
    for (int up = 0; up < 2; ++up) {
        const char* const* rows = up ? kRuinDrawnFloor : kRuinDrawnGround;
        for (int j = 0; j < kRuinDrawnCells; ++j)
            for (int i = 0; i < kRuinDrawnCells; ++i) {
                if (!drawnAt(rows, i, j)) continue;
                ++drawn[up];
                const Vec3 p = v.toWorld(-v.size / 2 + (i + 0.5) * cell, -v.size / 2 + (j + 0.5) * cell);
                if (!covered(p, up != 0)) ++bare[up];
            }
    }
    std::printf("  drawn squares not solid: %d of %d on the ground, %d of %d on the floor\n", bare[0], drawn[0], bare[1], drawn[1]);
    CHECK(drawn[0] > 1000 && drawn[1] > 100);
    CHECK(bare[0] == 0);
    CHECK(bare[1] == 0);
}

TEST(soldiers_marching_past_a_ruin_keep_their_breadth_from_its_stone) {
    // Men, horse and elephants: never nearer the drawn ruin than their
    // standoff (a hand's slack for the drawn squares' corners).
    const SiteView& v = siteView(Continent::Site::Kind::Ruin);
    const Gap gap = ruinGround(v);
    for (const UnitType type : {UnitType::HeavyInfantry, UnitType::HeavyCavalry, UnitType::Elephants}) {
        double worst = 1e9;
        for (const Run& run : ruinRuns()) worst = std::min(worst, march(v, run.fx, run.fy, run.tx, run.ty, type, type == UnitType::Elephants ? 2 : 12, gap).worst);
        CHECK(worst > -0.1);
    }
}

TEST(a_squad_goes_round_a_ruin_without_grinding_along_it) {
    // Sent from one side of the ruin to the other, all of them get there in
    // not much over the time of the straight walk, and no one is ever stuck
    // (under a tenth of his pace made in three seconds).
    const SiteView& v = siteView(Continent::Site::Kind::Ruin);
    const Gap gap = ruinGround(v);
    for (const UnitType type : {UnitType::LightInfantry, UnitType::HeavyInfantry, UnitType::LightCavalry}) {
        for (const Run& run : ruinRuns()) {
            const March r = march(v, run.fx, run.fy, run.tx, run.ty, type, 12, gap);
            const double straight = std::hypot(run.tx - run.fx, run.ty - run.fy) / (unitStats(type).moveSpeed * 9 / 10).toDouble();
            CHECK(r.arrived == r.men);
            CHECK(r.stalled == 0);
            CHECK(r.seconds < straight * 1.5 + 8);
        }
    }
}

TEST(soldiers_go_round_a_temple_a_sages_tower_and_a_shrine_clear_of_their_stone) {
    // (Each within the clearing round the site, where nothing grows.)
    // A temple's columns and hall, as drawn: past it lengthwise, across and slantwise.
    const SiteView& t = siteView(Continent::Site::Kind::Temple);
    const Gap temple = [&t](const Vec3& p, double) {
        double x, y;
        t.toModel(p, x, y);
        return templeGap(t.size, x, y);
    };
    for (const Run& run : {Run{-29, 1, 29, -1}, Run{3, -29, -3, 29}, Run{-28, -14, 28, 14}}) {
        const March r = march(t, run.fx, run.fy, run.tx, run.ty, UnitType::HeavyInfantry, 12, temple);
        CHECK(r.arrived == r.men);
        CHECK(r.stalled == 0);
        CHECK(r.worst > -0.1);
    }
    // A sage's tower: eight-sided, 0.3 of its size to the corners at its foot.
    const SiteView& s = siteView(Continent::Site::Kind::Sage);
    const Gap tower = [&s](const Vec3& p, double) {
        double x, y;
        s.toModel(p, x, y);
        return std::hypot(x, y) - 0.3 * s.size;
    };
    for (const Run& run : {Run{-17, 2, 17, -2}, Run{3, 17, -3, -17}}) {
        const March r = march(s, run.fx, run.fy, run.tx, run.ty, UnitType::HeavyCavalry, 4, tower);
        CHECK(r.arrived == r.men);
        CHECK(r.stalled == 0);
        CHECK(r.worst > -0.1);
    }
    // The shrine (every window has one): through its ring of pillars, past its
    // altar. A pillar's radius is its foot's, as drawn (meshes::kPillarFoot);
    // the altar is drawn within its block.
    const Region& rg = *t.region;
    const Obstacle* altar = nullptr;
    for (const Obstacle& o : rg.obstacles())
        if (o.kind == Obstacle::Kind::House && o.variant == Obstacle::kAltar) altar = &o;
    CHECK(altar != nullptr);
    if (!altar) return;
    SiteView shrine = t;
    shrine.at = altar->pos;
    shrine.turns = 0;
    const Gap stones = [&rg, altar](const Vec3& p, double reach) {
        double best = reach;
        for (const Obstacle& o : rg.obstacles()) {
            if (!o.standingStone() || (o.pos - altar->pos).flat().lengthSq() > m(400)) continue;
            const double dx = (p.x - o.pos.x).toDouble(), dy = (p.y - o.pos.y).toDouble();
            best = std::min(best, o.kind == Obstacle::Kind::House ? boxGap(dx, dy, -o.halfX.toDouble(), -o.halfY.toDouble(), o.halfX.toDouble(), o.halfY.toDouble())
                                                                  : std::hypot(dx, dy) - o.radius.toDouble());
        }
        return best;
    };
    for (const Run& run : {Run{-13, 1, 13, -1}, Run{-9, -9, 9, 9}}) {
        const March r = march(shrine, run.fx, run.fy, run.tx, run.ty, UnitType::HeavyInfantry, 8, stones);
        CHECK(r.arrived == r.men);
        CHECK(r.stalled == 0);
        CHECK(r.worst > -0.1);
    }
}

TEST(a_place_in_a_ruins_stone_is_moved_out_of_it) {
    // Sent to stand against the ruin (a formation's slot inside its base):
    // he stops at his standoff from it, there, not pressed into it for ever.
    const SiteView& v = siteView(Continent::Site::Kind::Ruin);
    WorldConfig cfg;
    cfg.region = v.region;
    World w(cfg);
    const UnitStats st = unitStats(UnitType::HeavyInfantry);
    const Vec3 start = v.toWorld(-20, 4);
    const EntityId id = w.spawnUnit(0, start, {Fixed::one(), Fixed::zero(), Fixed::zero()}, st);
    Command c{0, id, Command::Type::Move, v.toWorld(-10.2, 4)};  // in the plinth's step
    w.step(std::span(&c, 1));
    for (int t = 0; t < 30 * kTicksPerSecond && w.findUnit(id)->hasMoveTarget; ++t) w.step({});
    const Unit& u = *w.findUnit(id);
    CHECK(!u.hasMoveTarget);
    double x, y;
    v.toModel(u.pos, x, y);
    const double standoff = stoneStandoff(st.radius).toDouble();
    const double gap = ruinGap(kRuinDrawnGround, v.size, x, y, standoff + 1);
    std::printf("  stopped at (%.2f, %.2f) of the ruin, %.2f m from its stone (standoff %.2f)\n", x, y, gap, standoff);
    CHECK(gap > standoff - 0.1 && gap < standoff + 0.5);
}

TEST(standoffs_fit_within_their_gaits_clearance) {
    // Routes keep each gait's clearance from a site's stone, which is never
    // less than the standoff of those who walk it: a route never runs a man
    // into stone that pushes him off it.
    for (int t = 0; t < kUnitTypeCount; ++t) {
        const UnitStats s = unitStats(static_cast<UnitType>(t));
        if (s.flyAltitude > Fixed::zero()) continue;  // fliers on foot go as men, and seldom
        if (stoneStandoff(s.radius) > gaitClearance(s.gait)) std::printf("  %s: standoff %.2f over %s's %.2f\n", unitTypeName(static_cast<UnitType>(t)), stoneStandoff(s.radius).toDouble(), gaitName(s.gait), gaitClearance(s.gait).toDouble());
        CHECK(stoneStandoff(s.radius) <= gaitClearance(s.gait));
    }
}
