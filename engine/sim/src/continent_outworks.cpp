// Outworks and timber (docs/MAP.md, "Outworks"; docs/GAME_DESIGN.md, "Outworks and timber"). Some castles, not all, and
// fairly: every realm's seat and one walled town of its country (the first drafted for it: the one at its crossing or pass,
// if it has one) have a ditch dug round their walls, a bank thrown up inside it with a low palisade along its lip, a belt of
// abatis before it across the approaches, watchtowers on the bank beside the causeways, and an engine on a gate tower (two at
// a seat). A ditch holds water (a moat) where the town stands low by a river or lake on level ground. Every other walled town
// has a watchtower beside its road before the gate, and every village one at the end of its lane.
//
// Everything here is the continent's own fixed-point plan, the same on every machine; the land is cut by carve
// (City::outworksAt), and the works are handed to the battle's Region as they stand.
#include <algorithm>

#include "continent_shapes.h"
#include "illyria/sim/continent.h"
#include "illyria/sim/noise.h"

namespace illyria::sim {

using namespace continent_shapes;

namespace {

// From the outermost stone of the walls (a corner tower's face): the bank begins kBankFrom out, the ditch kDitchFrom out,
// kDitchWide across, kDitchDepth deep with sides kDitchSide wide; the bank stands kBankRise.
constexpr Fixed kBankFrom = Fixed::fromInt(3), kDitchFrom = Fixed::fromInt(10), kDitchWide = Fixed::fromInt(8);
constexpr Fixed kDitchDepth = Fixed::fromRatio(12, 5), kDitchSide = Fixed::fromRatio(5, 2), kBankRise = Fixed::fromRatio(6, 5);
// The palisade stands on the bank's lip, this far inside the ditch's edge; the abatis this far beyond the ditch, across the
// approaches within kAbatisArc (a cosine: 75 degrees) of a door's way out.
constexpr Fixed kPalisadeIn = Fixed::fromRatio(4, 5), kAbatisOut = Fixed::fromRatio(11, 2);
constexpr Fixed kAbatisArc = Fixed::fromRatio(26, 100);
constexpr Fixed kPalisadeRun = Fixed::fromInt(6), kAbatisRun = Fixed::fromInt(5);
// A causeway: a door's is this half wide; a road's its own half width and this much more.
constexpr Fixed kDoorCauseway = Fixed::fromInt(5), kRoadCausewayMore = Fixed::fromInt(3);
// A moat: the town within this of a river's or a lake's water, on a ring no more uneven than this.
constexpr Fixed kMoatNear = Fixed::fromInt(160), kMoatLevel = Fixed::fromRatio(6, 5);
// The works' strength (hit points) and size.
constexpr int32_t kPalisadeHp = 500, kAbatisHp = 400, kTowerHp = 280, kEngineHp = 260;  // (a watchtower: one good stone)
constexpr Fixed kPalisadeHeight = Fixed::fromRatio(13, 10), kAbatisHeight = Fixed::fromRatio(8, 5);
constexpr Fixed kTowerPlatform = Fixed::fromInt(6), kTowerHalf = Fixed::fromRatio(3, 2);

Vec3 radial(Fixed a) { return {cos(a), sin(a), Fixed::zero()}; }

}  // namespace

bool Ditch::causeway(Fixed x, Fixed y) const {
    const Vec3 p{x - centre.x, y - centre.y, Fixed::zero()};
    for (const Vec3& c : causeways) {
        if (p.x * c.x + p.y * c.y <= Fixed::zero()) continue;
        if (abs(p.x * c.y - p.y * c.x) < c.z) return true;
    }
    return false;
}

bool Ditch::holds(Fixed x, Fixed y) const {
    const Fixed dx = x - centre.x, dy = y - centre.y;
    if (abs(dx) > outer || abs(dy) > outer) return false;
    const Fixed d2 = dx * dx + dy * dy;
    return d2 >= inner * inner && d2 <= outer * outer && !causeway(x, y);
}

Fixed Continent::City::outworksAt(Fixed x, Fixed y, Fixed land) const {
    if (!fortified()) return land;
    const Fixed dx = x - ditch.centre.x, dy = y - ditch.centre.y;
    const Fixed reach = ditch.outer + Fixed::one();
    if (abs(dx) > reach || abs(dy) > reach) return land;
    const Fixed d = sqrt(dx * dx + dy * dy);
    if (d < ditch.bankIn || d > reach) return land;
    // The bank: up over its first 3 m, level to the ditch's lip.
    const Fixed bank = bankRise * noise::smoothstep(ditch.bankIn, ditch.bankIn + m(3), d) * (Fixed::one() - noise::smoothstep(ditch.inner - r(1, 2), ditch.inner + r(1, 2), d));
    if (ditch.causeway(x, y)) return land + bank;
    // The cut: sloping sides, a level bottom.
    const Fixed cut = noise::smoothstep(ditch.inner - r(1, 2), ditch.inner + kDitchSide, d) * (Fixed::one() - noise::smoothstep(ditch.outer - kDitchSide, ditch.outer + r(1, 2), d));
    if (ditch.wet) return min(land + bank, land + (moatBed - land) * cut);
    return land + bank - ditchDepth * cut;
}

void Continent::planOutworks() {
    fortified_.clear();
    const uint64_t sw = noise::nextSeed(seed_ ^ 0x0B7E0);
    // Fair: each realm's seat, and the first walled town drafted for it.
    std::vector<uint8_t> chosen(cities_.size(), 0), townTaken(kFactionCount, 0);
    for (size_t ci = 0; ci < cities_.size(); ++ci) {
        const City& c = cities_[ci];
        if (!c.walled || c.realm >= kFactionCount || c.castle.rings.empty()) continue;
        if (c.capital) chosen[ci] = 1;
        else if (!townTaken[c.realm]) chosen[ci] = townTaken[c.realm] = 1;
    }
    for (size_t ci = 0; ci < cities_.size(); ++ci) {
        City& city = cities_[ci];
        city.works.clear();
        const Vec3 c = city.pos;
        const Castle& k = city.castle;
        const auto ground = [&](const Vec3& p) { return Vec3{p.x, p.y, sample(p.x, p.y).height}; };
        const auto tower = [&](const Vec3& at, const Vec3& out, int32_t ring, uint8_t variant) {
            Work w;
            w.kind = Work::Kind::Watchtower;
            w.a = w.b = at;
            w.halfThick = kTowerHalf;
            w.height = kTowerPlatform;
            w.out = out;
            w.maxHp = kTowerHp;
            w.ring = ring;
            w.variant = variant;
            city.works.push_back(w);
        };
        const auto clearOfHouses = [&](const Vec3& p, Fixed room) {
            for (const Castle::House& h : k.houses)
                if ((h.pos - p).flat().length() < max(h.halfX, h.halfY) + room) return false;
            return (k.keep - p).flat().length() > k.keepHalf * 3 / 2 + room;
        };
        const auto dry = [&](const Vec3& p) {
            Fixed surface;
            const Sample s = sample(p.x, p.y);
            return !waterSurfaceOf(s, surface) && s.riverDist > s.riverHalfWidth + m(8);
        };
        if (!city.walled) {
            // A village: a watchtower at the end of its lane, beside it, clear of the houses.
            const Vec3 toward = k.keepAxis, side{-toward.y, toward.x, Fixed::zero()};
            for (const Fixed s : {m(7), -m(7), m(11), -m(11)}) {
                const Vec3 p = c + toward * (city.radius - m(3)) + side * s;
                if (!clearOfHouses(p, m(3)) || !dry(p)) continue;
                tower(ground(p), toward, -1, static_cast<uint8_t>(ci & 3));
                break;
            }
            continue;
        }
        if (k.rings.empty()) continue;
        const Castle::Ring& ring = k.rings.front();
        const size_t nc = ring.corners.size();
        // The doors' ways out.
        std::vector<Vec3> doors, doorMids;
        for (const size_t e : {ring.gateEdge, ring.gateEdge2}) {
            if (e == Castle::Ring::kNoEdge) continue;
            const Vec3 mid = (ring.corners[e] + ring.corners[(e + 1) % nc]) * r(1, 2);
            doors.push_back(unitFlat(mid - c));
            doorMids.push_back(mid);
        }
        if (!chosen[ci]) {
            // An ordinary walled town: a watchtower beside its road before the gate.
            const Vec3 out = doors.front(), side{-out.y, out.x, Fixed::zero()};
            for (const Fixed s : {m(8), -m(8)}) {
                const Vec3 p = doorMids.front() + out * m(16) + side * s;
                if (!dry(p)) continue;
                tower(ground(p), out, 0, static_cast<uint8_t>(ci & 3));
                break;
            }
            continue;
        }
        // The outworks: the ditch round the outermost stone (a corner tower's face), the bank inside it.
        Fixed reach;
        for (const Vec3& q : ring.corners) reach = max(reach, (q - c).flat().length());
        for (const Castle::Tower& t : k.towers) reach = max(reach, (t.pos - c).flat().length() + t.radius);
        Ditch& d = city.ditch;
        d.centre = c;
        d.bankIn = reach + kBankFrom;
        d.inner = reach + kDitchFrom;
        d.outer = d.inner + kDitchWide;
        d.causeways.clear();
        for (const Vec3& out : doors) d.causeways.push_back({out.x, out.y, kDoorCauseway});
        // Every road that crosses the ditch's line crosses it on a causeway.
        const Fixed mid = (d.inner + d.outer) / 2;
        for (const Polyline& road : roads_) {
            for (size_t si = 0; si + 1 < road.points.size(); ++si) {
                const Fixed da = (road.points[si] - c).flat().length(), db = (road.points[si + 1] - c).flat().length();
                if ((da < mid) == (db < mid)) continue;
                const Fixed t = (mid - da) / (db - da);
                const Vec3 at = road.points[si] + (road.points[si + 1] - road.points[si]) * t;
                const Vec3 out = unitFlat(at - c);
                d.causeways.push_back({out.x, out.y, road.widthAt(si, t) + kRoadCausewayMore});
            }
        }
        city.ditchDepth = kDitchDepth;
        city.bankRise = kBankRise;
        // A moat where the land allows: near a river's or a lake's water, on a level ring, every point of it dry land now.
        {
            Fixed lo = m(100000), hi = -m(100000);
            bool dryRing = true;
            for (int i = 0; i < 24; ++i) {
                const Vec3 p = c + radial(kTwoPi * i / 24) * mid;
                const Sample s = sample(p.x, p.y);
                lo = min(lo, s.height);
                hi = max(hi, s.height);
                dryRing = dryRing && dry(p);
            }
            const Sample at = sample(c.x, c.y);
            const bool nearWater = at.riverDist < at.riverHalfWidth + city.radius + kMoatNear || at.inLake;
            d.wet = dryRing && nearWater && hi - lo <= kMoatLevel;
            city.moatBed = lo - kDitchDepth;
        }
        fortified_.push_back(ci);
        const int32_t outer = 0;  // the ring whose holder holds them: the outer one
        // The palisade along the bank's lip, in runs, broken at every causeway (and where the water comes near).
        {
            const Fixed pr = d.inner - kPalisadeIn;
            const int64_t n = std::max<int64_t>(8, (kTwoPi * pr / kPalisadeRun).floorToInt());
            for (int64_t i = 0; i < n; ++i) {
                const Fixed a0 = kTwoPi * i / n, a1 = kTwoPi * (i + 1) / n;
                const Vec3 p0 = c + radial(a0) * pr, p1 = c + radial(a1) * pr, pm = c + radial((a0 + a1) / 2) * pr;
                if (d.causeway(p0.x, p0.y) || d.causeway(p1.x, p1.y) || d.causeway(pm.x, pm.y) || !dry(pm)) continue;
                Work w;
                w.kind = Work::Kind::Palisade;
                w.a = ground(p0);
                w.b = ground(p1);
                w.halfThick = r(3, 10);
                w.height = kPalisadeHeight;
                w.out = radial((a0 + a1) / 2);
                w.maxHp = kPalisadeHp;
                w.ring = outer;
                w.variant = static_cast<uint8_t>(noise::lattice(sw, static_cast<int64_t>(ci), i).raw & 0xFF);
                city.works.push_back(w);
            }
        }
        // The abatis: a belt of felled trees beyond the ditch across each door's approach, the causeways left open.
        {
            const Fixed ar = d.outer + kAbatisOut;
            const int64_t n = std::max<int64_t>(8, (kTwoPi * ar / kAbatisRun).floorToInt());
            for (int64_t i = 0; i < n; ++i) {
                const Fixed a0 = kTwoPi * i / n, a1 = kTwoPi * (i + 1) / n;
                const Vec3 dir = radial((a0 + a1) / 2);
                bool facing = false;
                for (const Vec3& out : doors) facing = facing || dir.dot(out) > kAbatisArc;
                if (!facing) continue;
                const Vec3 p0 = c + radial(a0) * ar, p1 = c + radial(a1) * ar, pm = c + dir * ar;
                // (Clear of the causeways by a cart's width more, so the way in is a way in.)
                bool open = false;
                const Vec3 side{-dir.y, dir.x, Fixed::zero()};
                for (const Fixed s : {-m(4), Fixed::zero(), m(4)}) {
                    const Vec3 q = pm + side * s;
                    open = open || d.causeway(q.x, q.y);
                }
                if (open || d.causeway(p0.x, p0.y) || d.causeway(p1.x, p1.y) || !dry(pm)) continue;
                Work w;
                w.kind = Work::Kind::Abatis;
                w.a = ground(p0);
                w.b = ground(p1);
                w.halfThick = r(7, 5);
                w.height = kAbatisHeight;
                w.out = dir;
                w.maxHp = kAbatisHp;
                w.ring = outer;
                w.variant = static_cast<uint8_t>(noise::lattice(sw ^ 0xAB, static_cast<int64_t>(ci), i).raw & 0xFF);
                city.works.push_back(w);
            }
        }
        // Watchtowers on the bank either side of the main causeway, one beside each other door's.
        for (size_t di = 0; di < doors.size(); ++di) {
            const Vec3 out = doors[di], side{-out.y, out.x, Fixed::zero()};
            for (const Fixed s : {kDoorCauseway + m(5), -(kDoorCauseway + m(5))}) {
                const Vec3 p = c + out * (d.inner - m(3)) + side * s;
                if (!dry(p)) continue;
                tower(ground(p), out, outer, static_cast<uint8_t>((ci + di) & 3));
                if (di > 0) break;
            }
        }
        // The engines: on the main gate's flanking towers (one at a town, both at a seat), at the towers' tops.
        {
            const Vec3 gm = doorMids.front();
            int placed = 0;
            const int want = city.capital ? 2 : 1;
            for (const Castle::Tower& t : k.towers) {
                if (placed >= want || t.roofed || (t.pos - gm).flat().length() > ring.gateTowerAt() + m(1)) continue;
                Work w;
                w.kind = Work::Kind::Engine;
                w.a = w.b = Vec3{t.pos.x, t.pos.y, c.z + t.height};
                w.halfThick = r(6, 5);
                w.height = t.height;
                w.out = doors.front();
                w.maxHp = kEngineHp;
                w.ring = outer;
                city.works.push_back(w);
                ++placed;
            }
        }
    }
}

}  // namespace illyria::sim
