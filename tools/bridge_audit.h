// Walks every bridge of the generated continent as the sim itself would (docs/TERRAIN_AUDIT.md, "Bridges"): for the audit
// tool (tools/terrainaudit.cpp --bridges) and tests/test_terrain_audit.cpp. A man on foot, a rider and a siege engine are
// each set walking across the bridge along its axis, from 14 m of ground before one end to 14 m past the other, in a World
// with the real Region, NavGrid and walking rules (World::walk's ledges, steps and water), both ways; and the deck's height
// above the ground at each end (the step a man makes onto it), and how far the road's middle is off the deck's axis there.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <span>
#include <vector>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim::audit {

struct BridgeReport {
    size_t crossing = 0;
    Vec3 pos;
    bool foot = false;
    double length = 0;           // metres of deck (and ramp) along its axis, end to end
    double halfWidth = 0;
    double rise[2] = {0, 0};     // the deck's height above the ground at its two ends (metres): the step onto it
    double roadOff[2] = {0, 0};  // how far the nearest road's middle is from the deck's axis, at each end
    double nearCastle = 1e9;     // metres from the deck's middle to the nearest castle's walls (negative: inside them)
    double waterEnds = 0;        // ends that stand in water (the bank is not reached)
    bool walks[3][2] = {};       // foot, rider, engine; each way
    bool passable() const {
        for (const auto& w : walks)
            for (bool b : w)
                if (!b) return false;
        return true;
    }
};

// The point `metres` along the road beyond a deck's end (continent coordinates), away from the deck: the road's own ground, which
// is where men and engines go on and off a bridge.
inline Vec3 roadBeyond(const Continent& c, const Vec3& end, const Vec3& outward, double metres) {
    double best = 1e18;
    size_t bi = 0, bk = 0;
    for (size_t i = 0; i < c.roads().size(); ++i)
        for (size_t k = 0; k + 1 < c.roads()[i].points.size(); ++k) {
            const Vec3 a = c.roads()[i].points[k], b = c.roads()[i].points[k + 1], ab = (b - a).flat();
            const Fixed len2 = ab.lengthSq();
            const Fixed t = len2 > Fixed::zero() ? clamp(((end - a).flat()).dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
            const double d = ((a + ab * t) - end).flat().length().toDouble();
            if (d < best) best = d, bi = i, bk = k;
        }
    if (best > 20) return end + outward * Fixed::fromInt(static_cast<int64_t>(metres));  // (no road here: straight out)
    const std::vector<Vec3>& pts = c.roads()[bi].points;
    const bool up = (pts[bk + 1] - pts[bk]).flat().dot(outward) > Fixed::zero();
    double left = metres;
    Vec3 at = up ? pts[bk + 1] : pts[bk];
    size_t k = up ? bk + 1 : bk;
    while (left > 0 && (up ? k + 1 < pts.size() : k > 0)) {
        const Vec3 nx = up ? pts[k + 1] : pts[k - 1];
        const double step = (nx - at).flat().length().toDouble();
        if (step >= left) return at + (nx - at) * Fixed::fromRatio(static_cast<int64_t>(left * 1000), static_cast<int64_t>(std::max(step, 0.001) * 1000));
        left -= step;
        at = nx;
        k = up ? k + 1 : k - 1;
    }
    return at;
}

// The deck's far ends along its axis, scanned from the middle in 0.25 m steps over every deck of this crossing.
inline BridgeReport auditBridge(const Continent& c, size_t crossing, int cells = 128, bool trace = false) {
    const Continent::Crossing& cr = c.crossings()[crossing];
    BridgeReport out;
    out.crossing = crossing;
    out.pos = cr.pos;
    out.foot = cr.kind == Continent::Crossing::Kind::Footbridge;
    out.halfWidth = cr.halfWidth.toDouble();
    for (const Continent::City& city : c.cities()) out.nearCastle = std::min(out.nearCastle, (cr.pos - city.pos).flat().length().toDouble() - city.radius.toDouble());
    const auto region = std::make_shared<const Region>(Region::fromContinent(c, cr.pos, cells));
    const Vec3 o = region->origin();
    const Vec3 mid{cr.pos.x - o.x, cr.pos.y - o.y, Fixed::zero()};
    Vec3 end[2];
    for (int side = 0; side < 2; ++side) {
        const Vec3 d = side ? -cr.dir : cr.dir;
        Fixed s;
        Vec3 last = mid;
        Fixed h;
        for (s = Fixed::zero(); s < Fixed::fromInt(260); s += Fixed::fromRatio(1, 4)) {
            const Vec3 p = mid + d * s;
            if (!region->deckAt(p.x, p.y, h)) {
                if (s > Fixed::fromInt(4)) break;  // (the middle itself may be off a deck's centre line only at its ends)
                continue;
            }
            last = p;
        }
        end[side] = last;
        Fixed deck;
        region->deckAt(last.x, last.y, deck);
        out.rise[side] = (deck - region->terrain().heightAt(last.x, last.y)).toDouble();
        Fixed surface;
        out.waterEnds += region->groundAt(last.x + d.x * Fixed::fromInt(2), last.y + d.y * Fixed::fromInt(2)) == Ground::DeepWater;
        // The road nearest the end, in continent coordinates.
        double best = 1e9;
        const Vec3 world{last.x + o.x, last.y + o.y, Fixed::zero()};
        for (const Polyline& rd : c.roads())
            for (size_t i = 0; i + 1 < rd.points.size(); ++i) {
                const Vec3 a = rd.points[i], b = rd.points[i + 1], ab = (b - a).flat();
                const Fixed len2 = ab.lengthSq();
                const Fixed t = len2 > Fixed::zero() ? clamp(((world - a).flat()).dot(ab) / len2, Fixed::zero(), Fixed::one()) : Fixed::zero();
                const Vec3 q = a + ab * t;
                const double dd = (q - world).flat().length().toDouble();
                if (dd < best) {
                    best = dd;
                    // sideways to the deck's axis: the distance of q from the axis line
                    const Vec3 across{-cr.dir.y, cr.dir.x, Fixed::zero()};
                    out.roadOff[side] = std::abs(((q - cr.pos).flat().dot(across)).toDouble());
                }
            }
    }
    static const UnitType types[3] = {UnitType::LightInfantry, UnitType::LightCavalry, UnitType::Catapult};
    for (int k = 0; k < 3; ++k)
        for (int dir = 0; dir < 2; ++dir) {
            const Vec3 d = (end[1] - end[0]).flat();
            const Vec3 u = d.length() > Fixed::zero() ? d / d.length() : cr.dir;
            const auto beyond = [&](const Vec3& e, const Vec3& out, bool to40 = false) {
                const Vec3 r = roadBeyond(c, Vec3{e.x + o.x, e.y + o.y, Fixed::zero()}, out, to40 ? 40 : 14);
                return Vec3{r.x - o.x, r.y - o.y, Fixed::zero()};
            };
            const Vec3 from = dir ? beyond(end[1], u) : beyond(end[0], -u);
            const Vec3 to = dir ? beyond(end[0], -u, true) : beyond(end[1], u, true);  // (a goal well on: a heavy unit stops short of it)
            WorldConfig cfg;
            cfg.region = region;
            World w(cfg);
            const EntityId id = w.spawnUnit(0, {from.x, from.y, Fixed::zero()}, dir ? -u : u, unitStats(types[k]));
            const Command go{0, id, Command::Type::Move, {to.x, to.y, Fixed::zero()}};
            w.step(std::span(&go, 1));
            bool arrived = false;
            for (int t = 0; t < 500 * kTicksPerSecond && !arrived; ++t) {
                w.step({});
                const Unit* un = w.findUnit(id);
                // Across: past the far end of the deck (an engine stops a few metres short of a distant goal, so the goal's own
                // distance is no test), along the span.
                const Fixed past = (un->pos - (dir ? end[0] : end[1])).flat().dot(dir ? -u : u);
                arrived = past > Fixed::fromInt(2);
                if (!un->hasMoveTarget) break;
            }
            if (trace && !arrived) {
                const Unit* un = w.findUnit(id);
                std::printf("   stuck: type %d dir %d at (%.1f, %.1f, %.2f) ground %d terrain %.2f; deck from (%.1f,%.1f) to (%.1f,%.1f), goal (%.1f,%.1f), path:", k, dir, un->pos.x.toDouble(), un->pos.y.toDouble(), un->pos.z.toDouble(),
                            static_cast<int>(region->groundAt(un->pos.x, un->pos.y)), region->terrain().heightAt(un->pos.x, un->pos.y).toDouble(), end[0].x.toDouble(), end[0].y.toDouble(), end[1].x.toDouble(), end[1].y.toDouble(), to.x.toDouble(), to.y.toDouble());
                for (const Vec3& q : un->path) std::printf(" (%.1f,%.1f)", q.x.toDouble(), q.y.toDouble());
                std::printf("\n");
            }
            out.walks[k][dir] = arrived;
        }
    out.length = (end[1] - end[0]).flat().length().toDouble();
    return out;
}

}  // namespace illyria::sim::audit
