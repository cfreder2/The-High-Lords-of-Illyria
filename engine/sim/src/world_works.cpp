// Timber works in battle (docs/GAME_DESIGN.md, "Outworks and timber"): the palisades, abatis, watchtowers and tower engines
// of the region (continent_outworks.cpp), and the barricade a garrison throws up behind a battered gate. Timber looks like
// wood and breaks like wood: a work stands while it has hit points, stops what it bars (NavGrid's bars, World::walk) and
// catches missiles (World::obstacleHit); a boulder (World::strikeStructure), a blast or fire takes them; broken, it throws
// its timbers about as sim objects (beams, planks, splinters: the debris of world.cpp's projectiles, hurting by weight and
// speed), and the men on a watchtower are thrown off. All of it integers and Fixed, the same on every peer, one rule for
// every side, a skirmish's and a Conquest's.
#include <algorithm>

#include "gate_geometry.h"
#include "illyria/sim/ballistics.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace illyria::sim {

namespace {

constexpr Vec3 up(Fixed z) { return {Fixed::zero(), Fixed::zero(), z}; }
Vec3 unitFlat(const Vec3& v, const Vec3& fallback) {
    const Vec3 f = v.flat();
    const Fixed len = f.length();
    return len == Fixed::zero() ? fallback : f / len;
}

// A tower's railing over its platform (what shelters the men on it from a flat shot).
constexpr Fixed kRailing = Fixed::fromRatio(11, 10);
// Climbing a watchtower's ladder, up or down.
constexpr int32_t kClimbTicks = 3 * kTicksPerSecond;
// A man bound for a tower climbs once his feet are this near its foot.
constexpr Fixed kFootReach = Fixed::fromInt(4);
// What breaks timber: a boulder's blow (its energy, 100 from the air) this many times over; a blast's damage at its
// middle by kind (percent); a hand's blow as a gate takes it (a quarter).
constexpr int32_t kStoneOnTimber = 3;
constexpr int32_t kBlastStonePct = 200, kBlastBombPct = 400, kBlastFireballPct = 100;
constexpr int32_t kBlowDivisor = 4;
// Fire: hit points a second, how long before it spreads, the chance a second it does (in 8ths), how near, and a man beside it.
constexpr int32_t kBurnPerSec = 12, kSpreadAfter = 8 * kTicksPerSecond, kSpreadEighths = 3, kBurnMan = 3;
constexpr Fixed kSpreadReach = Fixed::fromInt(2), kBurnBeside = Fixed::fromRatio(3, 5);
// The pieces a work throws when it breaks, and how heavy they run (percent beams and planks).
constexpr int32_t kPiecesPalisade = 4, kPiecesAbatis = 4, kPiecesTower = 12, kPiecesBarricade = 6, kPiecesEngine = 5, kPiecesGate = 8;
constexpr int32_t kHeftPalisade = 60, kHeftAbatis = 70, kHeftTower = 60, kHeftBarricade = 50, kHeftEngine = 50, kHeftGate = 60;
// The barricade: begun when the doors are under this share of their strength, this far behind the wall's inner face, this
// much wider than the gateway, its strength, how fast it goes up for each of the garrison's foot at work (four at most,
// within this of it), and from what share it bars the way.
constexpr int32_t kBarricadeAtPct = 85, kBarricadeHp = 900, kBarricadeBuildPerSec = 12, kBarricadeBuilders = 4, kBarricadeBarPct = 33;
constexpr Fixed kBarricadeBehind = Fixed::fromRatio(3, 2), kBarricadeWider = Fixed::fromRatio(3, 2), kBuilderReach = Fixed::fromInt(14);
// A tower's engine (a ballista): its reach on level ground, its bolt, its rate, its store, and how near an engine a man is
// its crew (the engine's second mark).
constexpr Fixed kEngineReach = Fixed::fromInt(160), kBoltSpeed = Fixed::fromInt(75), kBoltSpread = Fixed::fromRatio(3, 2);
constexpr int32_t kBoltDamage = 55, kEngineReloadTicks = 7 * kTicksPerSecond;
constexpr Fixed kCrewReach = Fixed::fromInt(12);
// A stone on a tower's top: the engine there takes this share of the blow (percent), and a smash this near it.
constexpr int32_t kTowerTopPct = 150;
constexpr Fixed kTopReach = Fixed::fromInt(4);
// Forward bowmen fall back when a foe is this near, or across the ditch.
constexpr Fixed kForwardDanger = Fixed::fromInt(25);

bool isEngine(const Unit& u) { return u.stats.gait == Gait::Wheeled && u.stats.ranged.muzzleSpeed > Fixed::zero(); }

// A work's box: middle, the axis along it, half lengths along and across, and its foot (z) and top.
struct Box {
    Vec3 c, u, n;
    Fixed hu, hn, foot, top;
};
Box boxOf(const Work& w) {
    Box b;
    if (w.tower()) {
        b.c = w.a;
        b.n = w.out;
        b.u = {-w.out.y, w.out.x, Fixed::zero()};
        b.hu = b.hn = w.halfThick;
        b.foot = w.a.z;
        b.top = w.a.z + w.height + (w.kind == Work::Kind::Watchtower ? kRailing : Fixed::zero());
        return b;
    }
    b.c = (w.a + w.b) * Fixed::fromRatio(1, 2);
    b.u = unitFlat(w.b - w.a, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()});
    b.n = {-b.u.y, b.u.x, Fixed::zero()};
    b.hu = (w.b - w.a).flat().length() / 2;
    b.hn = w.halfThick;
    b.foot = min(w.a.z, w.b.z);
    b.top = max(w.a.z, w.b.z) + w.height;
    return b;
}
// How far a point lies outside the box, flat (negative: inside), with `grow` added all round.
Fixed outside(const Box& b, const Vec3& p, Fixed grow) {
    const Vec3 d = (p - b.c).flat();
    const Fixed along = abs(d.dot(b.u)) - b.hu - grow, across = abs(d.dot(b.n)) - b.hn - grow;
    return max(along, across);
}
// The bars a standing work puts on the nav grid (NavGrid::kBar*).
uint8_t barsOf(const WorkState& w) {
    if (!w.standing() || !w.barring) return 0;
    switch (w.def.kind) {
        case Work::Kind::Palisade:
        case Work::Kind::Abatis: return NavGrid::kBarMounted | NavGrid::kBarSlow;
        case Work::Kind::Watchtower:
        case Work::Kind::Barricade: return NavGrid::kBarAll;
        case Work::Kind::Engine: return 0;
    }
    return 0;
}
int32_t piecesOf(Work::Kind k, int32_t& heft) {
    switch (k) {
        case Work::Kind::Palisade: heft = kHeftPalisade; return kPiecesPalisade;
        case Work::Kind::Abatis: heft = kHeftAbatis; return kPiecesAbatis;
        case Work::Kind::Watchtower: heft = kHeftTower; return kPiecesTower;
        case Work::Kind::Barricade: heft = kHeftBarricade; return kPiecesBarricade;
        case Work::Kind::Engine: heft = kHeftEngine; return kPiecesEngine;
    }
    heft = 50;
    return 4;
}

// The works near a point, each once, ascending (a scratch list: the step is one thread).
template <class Grid>
const std::vector<uint32_t>& worksNear(const Grid& grid, Fixed x, Fixed y, Fixed radius) {
    static std::vector<uint32_t> out;
    out.clear();
    grid.query(x, y, radius + Fixed::fromInt(8), [&](uint32_t k) { out.push_back(k); });
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace

// ------------------------------------------------------------------------------------------------------------ standing

void World::initWorks() {
    for (size_t i = 0; i < works_.size(); ++i)
        if (works_[i].standing() && works_[i].barring) barWorkCells(i, false);
    works_.clear();
    workGrid_.clear();
    barred_.clear();
    timberLying_ = 0;
    if (!config_.region) return;
    for (const Work& w : config_.region->works()) placeWork(w);
}

size_t World::placeWork(const Work& w, PlayerId holder, int32_t gate) {
    WorkState st;
    st.def = w;
    st.hp = w.maxHp;
    st.barring = gate < 0;  // (a barricade bars once a third of it is up: raiseBarricades)
    st.holder = holder;
    st.gate = gate;
    if (w.kind == Work::Kind::Engine) st.ammo = kEngineBolts;
    works_.push_back(st);
    const size_t i = works_.size() - 1;
    const Box b = boxOf(w);
    const Fixed reach = max(b.hu, b.hn) + Fixed::one();
    for (Fixed y = b.c.y - reach; y <= b.c.y + reach + Fixed::fromInt(8); y += Fixed::fromInt(8))
        for (Fixed x = b.c.x - reach; x <= b.c.x + reach + Fixed::fromInt(8); x += Fixed::fromInt(8)) workGrid_.insert(static_cast<uint32_t>(i), min(x, b.c.x + reach), min(y, b.c.y + reach));
    barWorkCells(i, true);
    return i;
}

PlayerId World::workHolder(size_t i) const {
    if (i >= works_.size()) return GateState::kNoHolder;
    const WorkState& w = works_[i];
    if (w.gate >= 0 && static_cast<size_t>(w.gate) < gates_.size()) return gates_[static_cast<size_t>(w.gate)].holder;
    if (w.def.ring >= 0 && config_.region) {
        for (size_t g = 0; g < config_.region->gates().size() && g < gates_.size(); ++g)
            if (config_.region->gates()[g].ring == w.def.ring) return gates_[g].holder;
    }
    // A watchtower of nobody's ring: whoever stands on it.
    if (w.holder != GateState::kNoHolder) return w.holder;
    if (w.def.kind == Work::Kind::Watchtower)
        for (const Unit& u : units_)
            if (u.alive() && u.perch == static_cast<int32_t>(i)) return u.owner;
    return GateState::kNoHolder;
}

void World::barWorkCells(size_t i, bool on) {
    if (!nav_) return;
    const WorkState& w = works_[i];
    const uint8_t bars = barsOf(w);
    if (bars == 0 && on) return;
    const Box b = boxOf(w.def);
    const Fixed cell = Fixed::fromInt(nav_->cellSize());
    const Fixed reach = max(b.hu, b.hn) + cell;
    std::vector<int32_t> cells;
    for (Fixed y = b.c.y - reach; y <= b.c.y + reach; y += cell / 2)
        for (Fixed x = b.c.x - reach; x <= b.c.x + reach; x += cell / 2) {
            const int32_t c = nav_->cellAt({x, y, Fixed::zero()});
            if (c < 0) continue;
            // A cell whose middle the work covers (a tower: any part of it; a run: its line within half a cell).
            const Vec3 mid = nav_->cellCentre(c);
            if (outside(b, mid, w.def.tower() ? cell / 2 : cell / 3) > Fixed::zero()) continue;
            cells.push_back(c);
        }
    std::sort(cells.begin(), cells.end());
    cells.erase(std::unique(cells.begin(), cells.end()), cells.end());
    const uint8_t kinds[3] = {NavGrid::kBarMounted, NavGrid::kBarAll, NavGrid::kBarSlow};
    WorkState whole = w;  // (what it barred while it stood)
    whole.hp = std::max(whole.hp, 1);
    whole.barring = true;
    const uint8_t mine = on ? bars : barsOf(whole);
    for (const int32_t c : cells) {
        std::array<uint8_t, 3>& count = barred_[c];
        uint8_t now = 0;
        for (int k = 0; k < 3; ++k) {
            if (mine & kinds[k]) count[static_cast<size_t>(k)] = static_cast<uint8_t>(on ? count[static_cast<size_t>(k)] + 1 : std::max(0, count[static_cast<size_t>(k)] - 1));
            if (count[static_cast<size_t>(k)] > 0) now |= kinds[k];
        }
        if (now == 0) barred_.erase(c);
        nav_->setWorkBars(c, now);
    }
}

int32_t World::workAt(const Vec3& p, Fixed margin) const {
    int32_t best = -1;
    Fixed bestD;
    for (const uint32_t k : worksNear(workGrid_, p.x, p.y, margin)) {
        const WorkState& w = works_[k];
        if (!w.standing()) continue;
        const Fixed d = outside(boxOf(w.def), p, Fixed::zero());
        if (d > margin) continue;
        if (best < 0 || d < bestD) best = static_cast<int32_t>(k), bestD = d;
    }
    return best;
}

int32_t World::perchedOn(size_t tower) const {
    int32_t n = 0;
    for (const Unit& u : units_) n += u.alive() && (u.perch == static_cast<int32_t>(tower) || (u.climb > 0 && u.perch == -2 - static_cast<int32_t>(tower)));
    return n;
}

Fixed World::workHit(const Vec3& a, const Vec3& b, Fixed limitT, int32_t* which) const {
    if (works_.empty()) return -Fixed::one();
    const Vec3 seg = b - a;
    const Vec3 mid = a + seg * Fixed::fromRatio(1, 2);
    Fixed best = limitT;
    bool hit = false;
    for (const uint32_t k : worksNear(workGrid_, mid.x, mid.y, seg.flat().length() / 2 + Fixed::fromInt(4))) {
        const WorkState& w = works_[k];
        if (!w.standing() || !w.barring) continue;
        Box bx = boxOf(w.def);
        // (A tower's engine: its frame on the top, which a stone that strikes it meets.)
        if (w.def.kind == Work::Kind::Engine) {
            bx.hu = bx.hn = w.def.halfThick * 2;
            bx.foot = w.def.a.z - Fixed::fromInt(2);
            bx.top = w.def.a.z + Fixed::fromRatio(3, 2);
        }
        // (A missile leaving it, loosed from its platform, is not caught by it.)
        if (outside(bx, a, Fixed::zero()) <= Fixed::zero() && a.z >= bx.foot + w.def.height - Fixed::fromInt(3)) continue;
        // Where the flight enters the box (slab by slab), then whether it is under the top there.
        Fixed t0 = Fixed::zero(), t1 = Fixed::one();
        bool miss = false;
        for (int axis = 0; axis < 2 && !miss; ++axis) {
            const Vec3& ax = axis == 0 ? bx.u : bx.n;
            const Fixed h = axis == 0 ? bx.hu : bx.hn;
            const Fixed p0 = (a - bx.c).flat().dot(ax), dp = seg.flat().dot(ax);
            if (dp == Fixed::zero()) {
                miss = abs(p0) > h;
                continue;
            }
            Fixed ta = (-h - p0) / dp, tb = (h - p0) / dp;
            if (ta > tb) std::swap(ta, tb);
            t0 = max(t0, ta);
            t1 = min(t1, tb);
            miss = t0 > t1;
        }
        if (miss || t0 >= best) continue;
        Fixed t = t0;
        const Fixed z = a.z + seg.z * t;
        if (z > bx.top) {
            if (seg.z >= Fixed::zero()) continue;
            t = (a.z - bx.top) / (a.z - b.z);
            if (t > t1 || t >= best) continue;
        } else if (z < bx.foot) {
            continue;
        }
        best = t;
        hit = true;
        if (which) *which = static_cast<int32_t>(k);
    }
    return hit ? best : -Fixed::one();
}

bool World::workBars(const Unit& u, Fixed x, Fixed y) const {
    if (works_.empty()) return false;
    const bool mounted = NavGrid::mountedGait(u.stats.gait);
    for (const uint32_t k : worksNear(workGrid_, x, y, u.stats.radius)) {
        const WorkState& w = works_[k];
        const uint8_t bars = barsOf(w);
        if (bars == 0 || (!(bars & NavGrid::kBarAll) && !mounted)) continue;
        const Box b = boxOf(w.def);
        const Fixed there = outside(b, {x, y, Fixed::zero()}, u.stats.radius);
        if (there > Fixed::zero()) continue;
        // (A man already in it may go out of it, never deeper: one caught by a barricade going up is not held there.)
        if (there > outside(b, u.pos, u.stats.radius)) continue;
        return true;
    }
    return false;
}

int32_t World::workPacePct(const Unit& u) const {
    if (works_.empty() || NavGrid::mountedGait(u.stats.gait)) return 100;
    for (const uint32_t k : worksNear(workGrid_, u.pos.x, u.pos.y, u.stats.radius)) {
        const WorkState& w = works_[k];
        if (!(barsOf(w) & NavGrid::kBarSlow)) continue;
        if (outside(boxOf(w.def), u.pos, u.stats.radius) <= Fixed::zero()) return 30;  // over the stakes, through the branches
    }
    return 100;
}

bool World::ditchBars(const Unit& u, Fixed x, Fixed y) const {
    if (!config_.region || config_.region->ditches().empty() || !NavGrid::mountedGait(u.stats.gait)) return false;
    return config_.region->inDitch(x, y) && !config_.region->inDitch(u.pos.x, u.pos.y);
}

// ------------------------------------------------------------------------------------------------------------ breaking

void World::damageWork(size_t i, int32_t amount, const Vec3& at, const Vec3& dir, bool heavy) {
    if (i >= works_.size() || amount <= 0) return;
    WorkState& w = works_[i];
    if (!w.standing()) return;
    w.hp -= amount;
    impacts_.push_back({ImpactEvent::Kind::Struck, at, dir, kNoEntity, kNoEntity, w.hp <= 0, Fixed::fromInt(amount), static_cast<int32_t>(Structure::Timber)});
    if (w.hp <= 0) {
        breakWork(i, unitFlat(dir, -w.def.out), heavy ? Fixed::fromInt(std::min(amount, 450)) / 15 : Fixed::fromInt(2));
    } else if (heavy && amount >= 60) {
        // A heavy blow that leaves it standing chips it: splinters, a stave.
        throwTimber(at, unitFlat(dir, -w.def.out), Fixed::fromInt(amount) / 30, 2, 20);
    }
}

void World::breakWork(size_t i, const Vec3& dir, Fixed strength) {
    WorkState& w = works_[i];
    if (w.barring) barWorkCells(i, false);
    w.hp = 0;
    w.fire = 0;
    w.fellTick = tick_;
    w.barring = false;
    int32_t heft = 50;
    const int32_t pieces = piecesOf(w.def.kind, heft);
    const Box b = boxOf(w.def);
    // Its timbers fly from about a man's height (a tower's legs go first; its engine's from the top of its tower).
    const Vec3 from = b.c + up(w.def.kind == Work::Kind::Engine ? Fixed::one() : min((b.top - b.foot) / 2, Fixed::fromRatio(3, 2)) + (b.foot - b.c.z));
    throwTimber(from, dir, max(strength, Fixed::fromInt(2)), pieces, heft);
    // The men on a watchtower go over with it: thrown off, falling (World::updateAirborne).
    for (Unit& u : units_) {
        if (!u.present() || (u.perch != static_cast<int32_t>(i) && u.perch != -2 - static_cast<int32_t>(i))) continue;  // (the dead on it too)
        const bool up_ = u.perch == static_cast<int32_t>(i);
        u.perch = -1;
        u.climb = 0;
        if (!up_) continue;
        const Fixed sx = rng_.nextSigned(), sy = rng_.nextSigned();
        u.vel = dir * (Fixed::fromInt(2) + strength / 3) + Vec3{sx, sy, Fixed::zero()} + up(Fixed::fromInt(2));
        u.airborne = true;
    }
}

void World::throwTimber(const Vec3& from, const Vec3& dir, Fixed speed, int32_t pieces, int32_t heft) {
    const Vec3 side{-dir.y, dir.x, Fixed::zero()};
    for (int32_t k = 0; k < pieces; ++k) {
        // One draw a statement (operand order is the compiler's).
        const int32_t roll = static_cast<int32_t>(rng_.nextBelow(100));
        const RemainKind kind = roll < heft / 2 ? RemainKind::Beam : roll < heft ? RemainKind::Plank : RemainKind::Splinter;
        const Fixed out = Fixed::fromRatio(3, 5) + rng_.nextUnit() * 3 / 5;
        const Fixed across = rng_.nextSigned();
        const Fixed lift = Fixed::fromRatio(1, 10) + rng_.nextUnit() / 4;
        // Heavier timber flies slower; splinters fastest.
        const Fixed heft_ = kind == RemainKind::Beam ? Fixed::fromRatio(3, 5) : kind == RemainKind::Plank ? Fixed::fromRatio(4, 5) : Fixed::fromRatio(6, 5);
        const Fixed v = speed * heft_;
        spawnDebris(kind, GateState::kNoHolder, from + side * (across * 2), dir * (v * out) + side * (v * across / 2) + up(v * lift));
    }
}

void World::blastWorks(const Vec3& at, const Explosive& ex, ProjectileKind kind) {
    if (works_.empty()) return;
    const int32_t pct = kind == ProjectileKind::Stone ? kBlastStonePct : kind == ProjectileKind::Bomb ? kBlastBombPct : kBlastFireballPct;
    for (const uint32_t k : worksNear(workGrid_, at.x, at.y, ex.radius)) {
        WorkState& w = works_[k];
        if (!w.standing()) continue;
        const Box b = boxOf(w.def);
        // (An engine on a tower's top is reached from its height; the rest from the ground up to their tops.)
        const Fixed dz = at.z > b.top ? at.z - b.top : at.z < b.foot - Fixed::one() ? b.foot - Fixed::one() - at.z : Fixed::zero();
        const Fixed d = max(outside(b, at, Fixed::zero()), Fixed::zero()) + dz;
        if (d >= ex.radius) continue;
        const Fixed f = Fixed::one() - d / ex.radius;
        damageWork(k, static_cast<int32_t>((f * ex.damage * pct / 100).floorToInt()), at, unitFlat((b.c - at), -w.def.out), true);
        if (kind == ProjectileKind::Fireball && w.standing()) igniteWork(k);
    }
}

void World::strikeWork(const Vec3& where, const Vec3& dir, Fixed energy) {
    const int32_t k = workAt(where, Fixed::fromRatio(3, 5));
    if (k < 0) return;
    damageWork(static_cast<size_t>(k), static_cast<int32_t>((energy * kStoneOnTimber).floorToInt()), where, unitFlat(dir, -works_[static_cast<size_t>(k)].def.out), true);
}

void World::strikeTowerTop(const Vec3& where, Fixed energy) {
    // The engine on the tower struck (a stone high on it or on its top).
    for (const uint32_t k : worksNear(workGrid_, where.x, where.y, kTopReach)) {
        WorkState& w = works_[k];
        if (!w.standing() || w.def.kind != Work::Kind::Engine) continue;
        if ((where - w.def.a).flat().length() > kTopReach + w.def.halfThick * 3 || where.z < w.def.a.z - w.def.height / 3) continue;
        damageWork(k, static_cast<int32_t>((energy * kTowerTopPct / 100).floorToInt()), where, unitFlat(w.def.a - where, -w.def.out), true);
    }
}

// ------------------------------------------------------------------------------------------------------------ fire

void World::igniteWork(size_t i) {
    if (i >= works_.size()) return;
    WorkState& w = works_[i];
    if (!w.standing() || w.fire > 0 || douses(w.def.a)) return;
    w.fire = 1;
    impacts_.push_back({ImpactEvent::Kind::Burst, boxOf(w.def).c + up(Fixed::one()), up(Fixed::one()), kNoEntity, kNoEntity, false, Fixed::one(), -1, DamageType::Fire});
}

void World::igniteWorksNear(const Vec3& at, Fixed radius) {
    for (const uint32_t k : worksNear(workGrid_, at.x, at.y, radius))
        if (works_[k].standing() && outside(boxOf(works_[k].def), at, Fixed::zero()) < radius) igniteWork(k);
}

void World::arrowInWork(const Projectile& p, int32_t what) {
    if (!p.fire || what > kHitWork) return;
    const size_t k = static_cast<size_t>(kHitWork - what);
    // A fire arrow catches more often than not; tow and pitch in old dry timber.
    if (rng_.nextBelow(3) < 2) igniteWork(k);
}

// ------------------------------------------------------------------------------------------------------------ the tick

void World::updateWorks() {
    if (works_.empty() && gates_.empty()) return;
    const bool second = tick_ % kTicksPerSecond == 0;
    // Timber from what broke this tick: a gate's leaves, an engine's frame.
    for (const ImpactEvent& e : impacts_)
        if (e.kind == ImpactEvent::Kind::Gate && e.killed) throwTimber(e.pos + up(Fixed::fromInt(2)), unitFlat(e.dir, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}), Fixed::fromInt(6), kPiecesGate, kHeftGate);
    for (const DeathEvent& d : deathEvents_)
        if (d.type == static_cast<uint8_t>(UnitType::Catapult) && (d.cause == DamageType::Blunt || d.cause == DamageType::Explosive))
            throwTimber(d.pos + up(Fixed::fromInt(1)), unitFlat(d.dir, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}), Fixed::fromInt(5), kPiecesEngine, kHeftEngine);
    for (size_t i = 0; i < works_.size(); ++i) {
        WorkState& w = works_[i];
        if (!w.standing()) continue;
        if (w.def.kind == Work::Kind::Engine) engineTick(i);
        if (w.fire <= 0) continue;
        ++w.fire;
        if (!second) continue;
        const Box b = boxOf(w.def);
        // The men beside it, and on it, are burned; a burning tower is no place to stand.
        for (Unit& u : units_) {
            if (!u.alive() || u.inAir()) continue;
            if (u.perch == static_cast<int32_t>(i) || outside(b, u.pos, u.stats.radius + kBurnBeside) <= Fixed::zero())
                damage(u, kBurnMan, nullptr, {DamageType::Fire, unitFlat(u.pos - b.c, w.def.out), u.pos + up(Fixed::one()), false, false});
        }
        // It spreads to its neighbours after a while.
        if (w.fire > kSpreadAfter)
            for (const uint32_t k : worksNear(workGrid_, b.c.x, b.c.y, max(b.hu, b.hn) + kSpreadReach)) {
                if (k == i || !works_[k].standing() || works_[k].fire > 0) continue;
                if (outside(boxOf(works_[k].def), b.c, max(b.hu, b.hn) + kSpreadReach) > Fixed::zero()) continue;
                if (static_cast<int32_t>(rng_.nextBelow(8)) < kSpreadEighths) igniteWork(k);
            }
        // Burned through, it falls in on itself: charred timbers, slowly.
        w.hp -= kBurnPerSec;
        if (w.hp <= 0) breakWork(i, -w.def.out, Fixed::one());
    }
    if (tick_ % 10 == 0) raiseBarricades();
}

// ------------------------------------------------------------------------------------------------------------ men

bool World::worksTick(Unit& u) {
    if (u.sortie != Unit::Sortie::None && sortieStep(u)) return true;
    if (works_.empty()) return false;
    if (u.perch >= 0 || u.climb > 0) return perched(u);
    if (u.perch <= -2) return climbTower(u);
    // A bowman put on a watchtower's place (a garrison's post: World::garrisonPosts) is up on it.
    if (!u.inAir() && !u.hasMoveTarget && u.stats.ranged.kind == ProjectileKind::Arrow && u.stats.ranged.muzzleSpeed > Fixed::zero() && !u.stats.mounted)
        for (const uint32_t k : worksNear(workGrid_, u.pos.x, u.pos.y, Fixed::one())) {
            const WorkState& w = works_[k];
            if (!w.standing() || w.def.kind != Work::Kind::Watchtower || perchedOn(k) >= kPerchPlaces) continue;
            if (outside(boxOf(w.def), u.pos, Fixed::zero()) > Fixed::zero()) continue;
            u.perch = static_cast<int32_t>(k);
            u.perchSlot = 0;
            for (const Unit& o : units_)
                if (o.id != u.id && o.alive() && o.perch == u.perch && o.perchSlot == 0) u.perchSlot = 1;
            return perched(u);
        }
    if (u.attackWork >= 0) {
        const size_t k = static_cast<size_t>(u.attackWork);
        if (k >= works_.size() || !works_[k].standing()) {
            u.attackWork = -1;
            u.hasAttackTarget = false;
            u.hasMoveTarget = false;
            return false;
        }
        if (u.fireArrows > 0 && u.stats.ranged.kind == ProjectileKind::Arrow) return fireAtTimber(u);
        if (u.stats.melee.damage > 0 && !canBatter(u.stats)) return hackWork(u);
        return false;  // (engines and bombers: their standing attack on its place)
    }
    if (u.hasMoveTarget && u.stats.melee.damage > 0 && hackWork(u)) return true;
    // A bowman with fire arrows and nothing in hand takes a foe's engine, or his timber, in his reach.
    if (u.fireArrows > 0 && !hasOrder(u) && u.foe == kNoEntity && fireAtTimber(u)) return true;
    return false;
}

bool World::climbTower(Unit& u) {
    const int32_t t = -2 - u.perch;
    if (t < 0 || static_cast<size_t>(t) >= works_.size() || !works_[static_cast<size_t>(t)].standing()) {
        u.perch = -1;
        return false;
    }
    const WorkState& w = works_[static_cast<size_t>(t)];
    const Fixed off = outside(boxOf(w.def), u.pos, Fixed::zero());
    if (off > kFootReach) {
        // On his way there (his move); stopped short of it with nowhere nearer to go, he gives it up.
        if (!u.hasMoveTarget) u.perch = -1;
        return false;
    }
    if (perchedOn(static_cast<size_t>(t)) - (u.climb > 0 ? 1 : 0) >= kPerchPlaces) {
        u.perch = -1;  // no room: he stands at its foot
        u.hasMoveTarget = false;
        return false;
    }
    // At its foot: up the ladder.
    u.hasMoveTarget = false;
    u.path.clear();
    u.climb = kClimbTicks;
    return true;
}

bool World::perched(Unit& u) {
    // Climbing: up (bound for a tower) or down (off it), standing at the ladder all the while.
    if (u.climb > 0) {
        if (--u.climb > 0) return true;
        if (u.perch <= -2) {
            // Up: the free place of the two.
            u.perch = -2 - u.perch;
            u.perchSlot = 0;
            for (const Unit& o : units_)
                if (o.id != u.id && o.alive() && o.perch == u.perch && o.perchSlot == 0) u.perchSlot = 1;
        } else if (u.perch >= 0) {
            // Down at its foot, on the side he is going.
            const WorkState& w = works_[static_cast<size_t>(u.perch)];
            const Vec3 toward = u.hasMoveTarget ? unitFlat(u.moveTarget - w.def.a, -w.def.out) : -w.def.out;
            u.pos = w.def.a + toward * (w.def.halfThick * 3 / 2 + u.stats.radius + Fixed::fromRatio(1, 2));
            u.pos.z = standingHeight(u.pos.x, u.pos.y, w.def.a.z + Fixed::one());
            u.perch = -1;
            if (u.hasMoveTarget) planPath(u, u.moveTarget);
            return false;
        }
    }
    const size_t t = static_cast<size_t>(u.perch);
    if (t >= works_.size() || !works_[t].standing()) {
        u.perch = -1;
        return false;
    }
    const WorkState& w = works_[t];
    const Vec3 side{-w.def.out.y, w.def.out.x, Fixed::zero()};
    u.pos = w.def.a + w.def.out * (w.def.halfThick / 3) + side * (u.perchSlot == 0 ? -Fixed::fromRatio(3, 5) : Fixed::fromRatio(3, 5));
    u.pos.z = w.def.a.z + w.def.height;
    // Ordered off it (a move), or the timber under him alight: down the ladder.
    if (u.hasMoveTarget || (w.fire > 2 * kTicksPerSecond && !u.routed)) {
        if (!u.hasMoveTarget) {
            u.hasMoveTarget = true;
            u.moveTarget = w.def.a - w.def.out * Fixed::fromInt(4);
        }
        u.climb = kClimbTicks * 2 / 3;
        return true;
    }
    // From up there he shoots: his mark if he was given one, else whoever is in his reach; nobody comes up to him.
    if (u.targetUnit != kNoEntity) {
        const Unit* mark = findUnit(u.targetUnit);
        if (mark != nullptr && mark->alive() && (mark->pos - u.pos).flat().length() <= shotRange(u, u.stats.ranged.maxRange, mark->pos)) {
            u.facing = unitFlat(mark->pos - u.pos, u.facing);
            if (readyToLoose(u)) fireAt(u, mark->pos);
            return true;
        }
        u.targetUnit = kNoEntity;
    }
    if (u.hasAttackTarget) {
        u.facing = unitFlat(u.attackTarget - u.pos, u.facing);
        if (readyToLoose(u)) fireAt(u, u.attackTarget);
        return true;
    }
    // The nearest foe in his reach and out of the dead ground under the platform (the men hugging its legs it hides).
    const Unit* best = nullptr;
    Fixed bestD;
    const Fixed dead = w.def.halfThick + Fixed::fromInt(4);
    nearbyNotOf(u.owner, u.pos.x, u.pos.y, u.stats.ranged.autoRange + Fixed::fromInt(20), [&](uint32_t k) {
        const Unit& e = units_[k];
        if (!e.alive() || e.owner == u.owner || concealed(e, u.owner)) return;
        if ((e.pos - w.def.a).flat().length() < dead) return;
        const Fixed d = (e.pos - u.pos).flat().length();
        if (d > shotRange(u, u.stats.ranged.autoRange, e.pos) || (best != nullptr && d >= bestD)) return;
        best = &e;
        bestD = d;
    });
    if (best == nullptr) {
        u.facing = w.def.out;
        return true;
    }
    u.facing = unitFlat(best->pos - u.pos, u.facing);
    if (readyToLoose(u)) fireAt(u, best->pos);
    return true;
}

bool World::hackWork(Unit& u) {
    // The work in his way: the one he was ordered at, or a barring one of his foes' (or nobody's) across his route.
    int32_t k = u.attackWork;
    if (k < 0) {
        if (!u.hasMoveTarget) return false;
        const Vec3 ahead = u.pos + unitFlat(u.moveTarget - u.pos, u.facing) * (u.stats.radius + u.stats.melee.reach + Fixed::one());
        for (const uint32_t j : worksNear(workGrid_, ahead.x, ahead.y, Fixed::one())) {
            const WorkState& w = works_[j];
            if (!w.standing() || !(barsOf(w) & NavGrid::kBarAll) || w.def.kind == Work::Kind::Engine) continue;
            const PlayerId holder = workHolder(j);
            if (holder == u.owner) continue;
            if (outside(boxOf(w.def), ahead, Fixed::zero()) > Fixed::zero()) continue;
            k = static_cast<int32_t>(j);
            break;
        }
        if (k < 0) return false;
    }
    const WorkState& w = works_[static_cast<size_t>(k)];
    const Box b = boxOf(w.def);
    const Fixed gap = outside(b, u.pos, Fixed::zero()) - u.stats.radius;
    if (gap > u.stats.melee.reach + Fixed::fromRatio(1, 2)) {
        // Near it: straight up to it (the nav's cells are coarser than a stake, and a barricade's close the gateway it is in).
        if (u.attackWork >= 0 && gap < Fixed::fromInt(16) && (nav_ == nullptr || nav_->clearLine(u.pos, b.c + (u.pos - b.c).flat() * Fixed::fromRatio(1, 2), Gait::Foot) || gap < Fixed::fromInt(5))) {
            u.hasMoveTarget = false;
            u.path.clear();
            const Vec3 rel = (u.pos - b.c).flat();
            const Fixed span = max(b.hu - (w.gate >= 0 ? kBarricadeWider + Fixed::one() : Fixed::zero()), Fixed::zero());
            const Fixed lat = clamp(rel.dot(b.u), -span, span);
            const Fixed side = rel.dot(b.n) >= Fixed::zero() ? Fixed::one() : -Fixed::one();
            walk(u, b.c + b.u * lat + b.n * (side * (b.hn + u.stats.radius + Fixed::fromRatio(1, 5))));
            u.pos.z = standingHeight(u.pos.x, u.pos.y, u.pos.z);
            return true;
        }
        // Ordered at it and not there yet: his walk there is the move.
        if (u.attackWork >= 0 && !u.hasMoveTarget) {
            // Its face on his side, the point of it nearest him.
            const Vec3 rel = (u.pos - b.c).flat();
            // (A barricade's ends are in the stone either side of the gateway: up to it within the passage.)
            const Fixed span = max(b.hu - (w.gate >= 0 ? kBarricadeWider + Fixed::fromRatio(3, 2) : Fixed::one()), Fixed::zero());
            const Fixed lat = clamp(rel.dot(b.u), -span, span);
            Fixed side = rel.dot(b.n) >= Fixed::zero() ? Fixed::one() : -Fixed::one();
            // (A barricade's: the gateway's side, for a man outside the walls.)
            if (w.gate >= 0 && config_.region) {
                const Gate& g = config_.region->gates()[static_cast<size_t>(w.gate)];
                const bool in = Gate::inside(config_.region->ramparts()[static_cast<size_t>(g.ring)], u.pos);
                side = (b.n.dot(g.out) >= Fixed::zero()) != in ? Fixed::one() : -Fixed::one();
            }
            Vec3 face = b.c + b.u * lat + b.n * (side * (b.hn + max(u.stats.radius, Fixed::fromInt(2)) + Fixed::one()));
            // (Out of the cells it bars, or the way there is no way.)
            for (int k = 0; k < 4 && nav_ && (nav_->barsAt(face) & NavGrid::kBarAll); ++k) face = face + b.n * (side * Fixed::fromInt(2));
            beginMove(u, {face.x, face.y, groundHeight(face.x, face.y)}, Fixed::zero(), {});
            planPath(u, u.moveTarget);
        }
        return false;
    }
    u.facing = unitFlat(b.c - u.pos, u.facing);
    u.engaged = true;
    if (u.meleeCooldown > 0) return true;
    u.meleeCooldown = attackTicks(u, u.stats.melee.cooldownTicks);
    const int32_t amount = std::max<int32_t>(1, u.stats.melee.damage * prowessPct(u) / 100 / kBlowDivisor);
    damageWork(static_cast<size_t>(k), amount, u.pos + u.facing * u.stats.radius + up(u.stats.height / 2), u.facing, false);
    return true;
}

void World::orderAtWork(Unit& u, const Command& cmd) {
    if (cmd.objectIndex < 0 || static_cast<size_t>(cmd.objectIndex) >= works_.size()) return;
    const size_t k = static_cast<size_t>(cmd.objectIndex);
    const WorkState& w = works_[k];
    if (!w.standing()) return;
    const Box b = boxOf(w.def);
    u.hasMoveTarget = false;
    u.attackGate = -1;
    if (canBatter(u.stats)) {
        // A stone, a satchel's man or a fireball at it: on its middle (a tower's engine, its top).
        u.hasAttackTarget = true;
        u.attackTarget = w.def.kind == Work::Kind::Engine ? w.def.a : Vec3{b.c.x, b.c.y, groundHeight(b.c.x, b.c.y)};
        u.attackWork = static_cast<int32_t>(k);
        return;
    }
    if (u.stats.ranged.kind == ProjectileKind::Arrow && u.stats.ranged.muzzleSpeed > Fixed::zero()) {
        if (u.fireArrows <= 0) return;  // (plain arrows do nothing to timber)
        u.attackWork = static_cast<int32_t>(k);
        return;
    }
    if (u.stats.melee.damage > 0 && w.def.kind != Work::Kind::Engine) u.attackWork = static_cast<int32_t>(k);
}

bool World::fireAtTimber(Unit& u) {
    // His mark: the work he was ordered at, else a foe's engine (a catapult), or a foe's work, in his reach.
    Vec3 mark;
    bool found = false;
    const Fixed reach = u.attackWork >= 0 ? u.stats.ranged.maxRange : u.stats.ranged.autoRange;
    if (u.attackWork >= 0) {
        const Box b = boxOf(works_[static_cast<size_t>(u.attackWork)].def);
        mark = b.c + up((b.top - b.foot) / 2 + (b.foot - b.c.z));
        found = true;
        if ((mark - u.pos).flat().length() > shotRange(u, reach, mark)) {
            // Into reach first.
            if (!u.hasMoveTarget) {
                const Vec3 to = mark + unitFlat(u.pos - mark, -u.facing) * (reach - Fixed::fromInt(6));
                beginMove(u, {to.x, to.y, groundHeight(to.x, to.y)}, Fixed::zero(), {});
                planPath(u, u.moveTarget);
            }
            return false;
        }
        u.hasMoveTarget = false;
    } else {
        Fixed best;
        nearbyNotOf(u.owner, u.pos.x, u.pos.y, reach, [&](uint32_t i) {
            const Unit& e = units_[i];
            if (!e.alive() || !isEngine(e) || e.burning > 0) return;
            const Fixed d = (e.pos - u.pos).flat().length();
            if (d > shotRange(u, reach, e.pos) || (found && d >= best)) return;
            best = d;
            mark = e.pos + up(e.stats.height / 2);
            found = true;
        });
        if (!found) return false;
    }
    u.facing = unitFlat(mark - u.pos, u.facing);
    if (!readyToLoose(u)) return true;
    const size_t before = projectiles_.size();
    if (!fire(u, mark) || projectiles_.size() == before) return true;
    projectiles_.back().fire = true;
    u.reloadRemaining = attackTicks(u, u.stats.ranged.reloadTicks);
    if (--u.fireArrows <= 0 && u.attackWork >= 0) {
        u.attackWork = -1;  // (none left: his order is done)
    }
    return true;
}

// ------------------------------------------------------------------------------------------------------------ engines

bool World::counterBattery(Unit& u) {
    // An engine with no order and a foe's tower engine within its throw: that first (it outranges men, and it hunts engines).
    if (works_.empty() || !isEngine(u) || hasOrder(u)) return false;
    int32_t best = -1;
    Fixed bestD;
    for (size_t i = 0; i < works_.size(); ++i) {
        const WorkState& w = works_[i];
        if (!w.standing() || w.def.kind != Work::Kind::Engine) continue;
        const PlayerId holder = workHolder(i);
        if (holder == GateState::kNoHolder || holder == u.owner) continue;
        const Fixed d = (w.def.a - u.pos).flat().length();
        if (d > u.stats.ranged.maxRange || d < u.stats.ranged.minRange || (best >= 0 && d >= bestD)) continue;
        best = static_cast<int32_t>(i);
        bestD = d;
    }
    if (best < 0) return false;
    const Vec3 at = works_[static_cast<size_t>(best)].def.a;
    u.facing = unitFlat(at - u.pos, u.facing);
    if (readyToLoose(u)) fireAt(u, at);
    return true;
}

void World::engineTick(size_t i) {
    WorkState& w = works_[i];
    if (w.reload > 0) --w.reload;
    if (w.ammo <= 0 || w.reload > 0 || (tick_ + i) % 5 != 0) return;
    const PlayerId holder = workHolder(i);
    if (holder == GateState::kNoHolder) return;
    // Its reach: the height over its mark tells, as a bowman's does (World::shotRange).
    const auto reachTo = [&](const Vec3& p) { return kEngineReach + clamp(w.def.a.z - p.z, -kEngineReach / 4, kEngineReach / 2); };
    // Engines first, the nearest; then the men by an engine (its crew and escort).
    const Unit* mark = nullptr;
    Fixed best;
    bool engineMark = false;
    nearbyNotOf(holder, w.def.a.x, w.def.a.y, kEngineReach * 3 / 2, [&](uint32_t k) {
        const Unit& e = units_[k];
        if (!e.alive() || e.owner == holder || e.inAir()) return;
        const Fixed d = (e.pos - w.def.a).flat().length();
        if (d > reachTo(e.pos) || d < Fixed::fromInt(15)) return;
        const bool engine = isEngine(e);
        if (engineMark && !engine) return;
        if (!engine) {
            bool crew = false;
            nearby(e.pos.x, e.pos.y, kCrewReach, [&](uint32_t j) {
                const Unit& o = units_[j];
                crew = crew || (o.alive() && o.owner == e.owner && isEngine(o) && (o.pos - e.pos).flat().length() <= kCrewReach);
            });
            if (!crew) return;
        }
        if (mark != nullptr && engine == engineMark && d >= best) return;
        mark = &e;
        best = d;
        engineMark = engine;
    });
    if (mark == nullptr) return;
    w.mark = mark->id;
    // A bolt, flat and quick, from the top of the tower; a little scatter at long range.
    const Vec3 origin = w.def.a + up(Fixed::fromRatio(17, 10));  // (over its own frame)
    Fixed dx, dy;
    do {
        dx = rng_.nextSigned();
        dy = rng_.nextSigned();
    } while (dx * dx + dy * dy >= Fixed::one());
    const Fixed spread = kBoltSpread * best / 100;
    const Vec3 aim = mark->pos + up(mark->stats.height / 2) + Vec3{dx * spread, dy * spread, Fixed::zero()};
    const LaunchSolution s = solveLowArc(origin, aim, kBoltSpeed, config_.gravity);
    if (!s.inRange) return;
    Projectile p;
    p.id = nextId_++;
    p.owner = holder;
    p.kind = ProjectileKind::Arrow;
    p.pos = origin;
    p.vel = s.velocity;
    p.damage = kBoltDamage * heightShotPct(origin.z - aim.z) / 100;
    p.cause = DamageType::Pierce;
    projectiles_.push_back(p);
    impacts_.push_back({ImpactEvent::Kind::Struck, origin, s.velocity, p.id, kNoEntity, false, Fixed::zero(), -1, DamageType::Pierce});
    --w.ammo;
    w.reload = kEngineReloadTicks;
}

// ------------------------------------------------------------------------------------------------------------ barricades

void World::raiseBarricades() {
    if (!config_.region) return;
    const Region& rg = *config_.region;
    for (size_t g = 0; g < gates_.size() && g < rg.gates().size(); ++g) {
        const GateState& st = gates_[g];
        if (!st.garrisoned || st.holder == GateState::kNoHolder) continue;
        // Its barricade, if one is up or going up.
        int32_t have = -1;
        for (size_t i = 0; i < works_.size(); ++i)
            if (works_[i].gate == static_cast<int32_t>(g) && works_[i].fellTick == 0) have = static_cast<int32_t>(i);
        const Gate& gate = rg.gates()[g];
        const Rampart& ring = rg.ramparts()[static_cast<size_t>(gate.ring)];
        const Vec3 mid = (gate.a + gate.b) * Fixed::fromRatio(1, 2);
        if (have < 0) {
            if (st.broken() || st.hp * 100 >= rg.gates()[g].maxHp * kBarricadeAtPct) continue;
            // Behind the doors, across the gateway's inner mouth.
            const Vec3 along = unitFlat(gate.b - gate.a, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()});
            const Vec3 at = mid - gate.out * (ring.halfThick * 2 + kBarricadeBehind);
            Work w;
            w.kind = Work::Kind::Barricade;
            const Fixed half = (gate.b - gate.a).flat().length() / 2 + kBarricadeWider;
            w.a = at - along * half;
            w.b = at + along * half;
            w.a.z = groundHeight(w.a.x, w.a.y);
            w.b.z = groundHeight(w.b.x, w.b.y);
            w.halfThick = Fixed::fromRatio(9, 10);
            w.height = Fixed::fromRatio(9, 5);
            w.out = gate.out;
            w.maxHp = kBarricadeHp;
            w.ring = gate.ring;
            const size_t i = placeWork(w, GateState::kNoHolder, static_cast<int32_t>(g));
            works_[i].hp = 1;
            have = static_cast<int32_t>(i);
        }
        WorkState& w = works_[static_cast<size_t>(have)];
        if (!w.standing() || w.hp >= w.def.maxHp) continue;
        // The garrison's foot near it build (not with a foe at it).
        const Box b = boxOf(w.def);
        int32_t builders = 0;
        bool foeAt = false;
        nearby(b.c.x, b.c.y, kBuilderReach, [&](uint32_t k) {
            const Unit& m = units_[k];
            if (!m.alive() || m.routed || m.inAir()) return;
            const Fixed d = outside(b, m.pos, Fixed::zero());
            if (m.owner != st.holder) foeAt = foeAt || d < Fixed::fromInt(3);
            else if (d < kBuilderReach && m.stats.melee.damage > 0 && !m.stats.mounted && m.stats.ranged.muzzleSpeed == Fixed::zero() && m.foe == kNoEntity) ++builders;
        });
        if (foeAt || builders == 0) continue;
        w.hp = std::min(w.def.maxHp, w.hp + std::min(builders, kBarricadeBuilders) * kBarricadeBuildPerSec / 3);
        if (!w.barring && w.hp * 100 >= w.def.maxHp * kBarricadeBarPct) {
            w.barring = true;
            barWorkCells(static_cast<size_t>(have), true);
            impacts_.push_back({ImpactEvent::Kind::Struck, b.c, gate.out, kNoEntity, kNoEntity, false, Fixed::zero(), static_cast<int32_t>(Structure::Timber)});
        }
        (void)mid;
    }
}

// ------------------------------------------------------------------------------------------------------------ forward

const Ditch* World::ditchOf(const Rampart& ring) const {
    if (!config_.region) return nullptr;
    for (const Ditch& d : config_.region->ditches())
        if ((d.centre - ring.centre).flat().length() < Fixed::fromInt(20)) return &d;
    return nullptr;
}

bool World::onForwardPost(const Unit& u, const Rampart& ring) const {
    if (u.perch != -1) return true;
    const Ditch* d = ditchOf(ring);
    if (d == nullptr || Gate::inside(ring, u.pos)) return false;
    const Fixed r = (u.pos - d->centre).flat().length();
    return r >= d->bankIn - Fixed::fromInt(2) && r <= d->outer;
}

std::vector<World::GarrisonPost> World::forwardPosts(size_t gate, size_t count, const std::vector<Vec3>& held) const {
    // On a castle with outworks, before its main door: the watchtowers' places first, then the bank behind the palisade,
    // either side of the causeway outward along it, a few paces apart, every man facing out over the ditch.
    std::vector<GarrisonPost> out;
    if (!config_.region || count == 0) return out;
    const Region& rg = *config_.region;
    const Gate& g = rg.gates()[gate];
    if (g.side) return out;
    const Rampart& ring = rg.ramparts()[static_cast<size_t>(g.ring)];
    const Ditch* d = ditchOf(ring);
    if (d == nullptr) return out;
    const auto free = [&](const Vec3& p) {
        for (const Vec3& q : held)
            if ((q - p).flat().lengthSq() < Fixed::fromInt(2)) return false;
        for (const GarrisonPost& q : out)
            if ((q.pos - p).flat().lengthSq() < Fixed::fromInt(2)) return false;
        return true;
    };
    // (The watchtowers take half of them at most: the rest line the bank.)
    const size_t towers = (count + 1) / 2;
    for (size_t i = 0; i < works_.size() && out.size() < towers; ++i) {
        const WorkState& w = works_[i];
        if (!w.standing() || w.def.kind != Work::Kind::Watchtower || w.def.ring != g.ring) continue;
        if ((w.def.a - (g.a + g.b) * Fixed::fromRatio(1, 2)).flat().length() > Fixed::fromInt(60)) continue;
        bool taken = false;
        for (const Vec3& q : held) taken = taken || (q - w.def.a).flat().lengthSq() < Fixed::fromInt(4);
        for (int32_t s = taken ? kPerchPlaces : perchedOn(i); s < kPerchPlaces && out.size() < towers; ++s) out.push_back({w.def.a, w.def.out, GarrisonRole::Wall});
    }
    const Vec3 mid = (g.a + g.b) * Fixed::fromRatio(1, 2);
    const Vec3 dir = unitFlat(mid - d->centre, g.out);
    const Fixed r = d->inner - Fixed::fromRatio(11, 5);
    const Vec3 side{-dir.y, dir.x, Fixed::zero()};
    for (int32_t step = 0; step < 40 && out.size() < count; ++step) {
        const Fixed lateral = Fixed::fromInt(8) + Fixed::fromRatio(5, 2) * (step / 2);
        const Vec3 guess = d->centre + dir * r + side * (step % 2 == 0 ? lateral : -lateral);
        const Vec3 rad = unitFlat(guess - d->centre, dir);
        Vec3 p = d->centre + rad * r;
        p.z = groundHeight(p.x, p.y);
        if (d->causeway(p.x, p.y) || !standable(p) || workAt(p, Fixed::fromRatio(1, 2)) >= 0 || !free(p)) continue;
        out.push_back({p, rad, GarrisonRole::Wall});
    }
    return out;
}

void World::recallForward(const std::vector<size_t>& doors) {
    // A computer's or a neutral town's bowmen on the bank before the walls, or up a watchtower, fall back inside when a foe
    // comes within kForwardDanger of them or crosses the ditch between them and the walls, or a door is down (the player's
    // are his to keep or bring back: GateState::recalls).
    if (!config_.region || doors.empty() || config_.region->ditches().empty()) return;
    const Region& rg = *config_.region;
    const GateState& lead = gates_[doors.front()];
    if (!lead.recalls) return;
    const Gate& g0 = rg.gates()[doors.front()];
    const Rampart& ring = rg.ramparts()[static_cast<size_t>(g0.ring)];
    const Ditch* ditch = ditchOf(ring);
    if (ditch == nullptr) return;
    bool breach = false;
    for (const size_t d : doors) breach = breach || gates_[d].broken();
    nearby(ditch->centre.x, ditch->centre.y, ditch->outer + Fixed::fromInt(10), [&](uint32_t k) {
        Unit& u = units_[k];
        if (!u.alive() || u.owner != lead.holder || u.stats.ranged.kind != ProjectileKind::Arrow || u.stats.ranged.muzzleSpeed == Fixed::zero()) return;
        const Fixed r = (u.pos - ditch->centre).flat().length();
        if (Gate::inside(ring, u.pos) || r < ditch->bankIn - Fixed::fromInt(2) || r > ditch->outer) return;
        if (u.hasMoveTarget || u.climb > 0) return;
        bool danger = breach;
        nearbyNotOf(u.owner, u.pos.x, u.pos.y, kForwardDanger, [&](uint32_t j) {
            const Unit& f = units_[j];
            if (!f.alive() || f.routed || f.inAir() || f.owner == u.owner) return;
            const Fixed fr = (f.pos - ditch->centre).flat().length();
            danger = danger || (f.pos - u.pos).flat().length() < kForwardDanger || fr < ditch->inner;
        });
        if (!danger) return;
        // In by the nearest door, to the courtyard behind it.
        size_t best = doors.front();
        for (const size_t d : doors)
            if (((rg.gates()[d].a + rg.gates()[d].b) * Fixed::fromRatio(1, 2) - u.pos).flat().lengthSq() < ((rg.gates()[best].a + rg.gates()[best].b) * Fixed::fromRatio(1, 2) - u.pos).flat().lengthSq()) best = d;
        const Gate& g = rg.gates()[best];
        Vec3 in = (g.a + g.b) * Fixed::fromRatio(1, 2) - g.out * Fixed::fromInt(14);
        in.z = groundHeight(in.x, in.y);
        u.routeFrom = u.pos;
        beginMove(u, in, Fixed::zero(), g.out);
        if (u.perch < 0) planPath(u, in);
        u.stationed = true;
    });
}

}  // namespace illyria::sim
