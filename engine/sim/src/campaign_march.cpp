// Marching on the campaign map at the battle's speeds (docs/ONE_WORLD.md,
// "The clock and the day"). One set of rules for both: a stack goes at the
// pace of its slowest army, and an army goes at its unit's ground speed times
// its gait's multiplier for the ground it is crossing (gait.h, the battle's
// table), slowed by climbing and quickened going down as a soldier is
// (World::walk). Each step from a cell's centre to a neighbour's is half over
// each cell's ground; a diagonal step is the square root of two cells long.
// Routes are planned by travel time with the same table, so each kind takes
// its own fastest way: cavalry round a wood that infantry cuts through, the
// road where it is quicker. A stack of fliers alone goes at the long-run pace
// of their flight cycle, over anything: a ground run, a flight as long as
// their endurance, then twice as long resting (rest restores stamina at half
// the rate flight spends it). Any walker in a stack (a hero is one) makes it
// walk. Deterministic: fixed point and integers.
#include <algorithm>
#include <array>
#include <climits>
#include <queue>

#include "illyria/sim/campaign.h"
#include "illyria/sim/nav.h"

namespace illyria::sim {

namespace {

// The eight steps from a cell, diagonals at the odd places.
constexpr int32_t kDirs[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
constexpr Fixed kSqrt2 = Fixed::fromRatio(141421356, 100000000);
// A step's length in millimetres, straight and diagonal.
constexpr int64_t kStepMm = int64_t{Campaign::kCell} * 1000;
constexpr int64_t kDiagonalMm = int64_t{Campaign::kCell} * 1414214 / 1000;

int32_t toMm(Fixed metres) { return static_cast<int32_t>((metres.raw * 1000 + Fixed::kOneRaw / 2) >> Fixed::kFracBits); }

// Time, in kTimeScale-ths of a tick, to cover mm at mm a second.
int64_t ticksFor(int64_t mm, int64_t speed) {
    return mm * Campaign::kTicksPerSecond * Campaign::kTimeScale / std::max<int64_t>(1, speed);
}
int32_t clampTime(int64_t t) { return static_cast<int32_t>(std::clamp<int64_t>(t, 1, Campaign::kClosed - 1)); }

// The most a gait makes of its speed on any ground (a road), for the route search's estimate.
Fixed bestGround(Gait gait) {
    Fixed best = Fixed::one();
    for (int g = 0; g < kGroundCount; ++g) best = max(best, gaitSpeed(gait, static_cast<Ground>(g)));
    return best;
}

}  // namespace

int32_t Campaign::direction(int32_t dx, int32_t dy) {
    for (int32_t d = 0; d < 8; ++d)
        if (kDirs[d][0] == dx && kDirs[d][1] == dy) return d;
    return -1;
}

int32_t Campaign::groundSpeed(UnitType t) {
    static const std::array<int32_t, kUnitTypeCount> kSpeed = [] {
        std::array<int32_t, kUnitTypeCount> s{};
        for (int k = 0; k < kUnitTypeCount; ++k) s[static_cast<size_t>(k)] = toMm(unitStats(static_cast<UnitType>(k)).moveSpeed);
        return s;
    }();
    const int k = static_cast<int>(t);
    return k >= 0 && k < kUnitTypeCount ? kSpeed[static_cast<size_t>(k)] : 0;
}

int32_t Campaign::flightPace(UnitType t) {
    static const std::array<int32_t, kUnitTypeCount> kPace = [] {
        std::array<int32_t, kUnitTypeCount> p{};
        for (int k = 0; k < kUnitTypeCount; ++k) {
            const UnitStats s = unitStats(static_cast<UnitType>(k));
            if (!flies(static_cast<UnitType>(k)) || s.flyAltitude <= Fixed::zero() || s.staminaTicks <= 0) continue;
            // One cycle: the take-off run (at a run's pace), the flight
            // (endurance seconds at air speed), then the rest that restores it.
            const Fixed endurance = Fixed::fromRatio(s.staminaTicks, illyria::sim::kTicksPerSecond);
            const Fixed run = s.takeoffRun / (s.moveSpeed * kTakeoffRunPace);
            const Fixed distance = s.takeoffRun + s.flySpeed * endurance;
            const Fixed time = run + endurance + endurance * static_cast<int64_t>(kFlierRestTicks);
            p[static_cast<size_t>(k)] = toMm(distance / time);
        }
        return p;
    }();
    const int k = static_cast<int>(t);
    return k >= 0 && k < kUnitTypeCount ? kPace[static_cast<size_t>(k)] : 0;
}

bool Campaign::airborne(const Stack& s) {
    if (s.armies.empty()) return false;
    for (const Army& a : s.armies)
        if (!flies(a.type)) return false;
    return true;
}

Ground Campaign::groundAt(int32_t cx, int32_t cy) const {
    const size_t k = static_cast<size_t>(cy) * nx_ + cx;
    switch (terrain_[k]) {
        case Lay::Road: return Ground::Road;
        case Lay::Bridge: return Ground::Bridge;
        case Lay::Forest: return Ground::Forest;
        case Lay::Marsh: return Ground::Swamp;
        case Lay::City: return roadCell_[k] ? Ground::Road : Ground::Plains;  // its streets
        case Lay::Hill:  // climbed (effort)
        case Lay::Plain: return Ground::Plains;
        case Lay::Water:
        case Lay::Mountain: break;  // the battle's deep water, and its cliffs
    }
    return Ground::DeepWater;
}

int32_t Campaign::effort(Gait gait, Ground from, Ground to, int32_t rise, bool diagonal) {
    const Fixed a = gaitSpeed(gait, from), b = gaitSpeed(gait, to);
    if (b <= Fixed::zero()) return kClosed;
    // Out of ground it could not have walked into (set down by a boat, or by
    // wings that have since lost a flier): at a plains pace.
    const Fixed out = a > Fixed::zero() ? a : Fixed::one();
    const Fixed length = diagonal ? Fixed::fromInt(kCell) * kSqrt2 : Fixed::fromInt(kCell);
    Fixed e = length / 2 / out + length / 2 / b;
    // The climb, as World::walk takes it: slower up, a little quicker down, and steep ground slower either way.
    const Fixed grade = Fixed::fromInt(rise) / length;
    if (grade > Fixed::zero()) e = e * (Fixed::one() + grade * kClimbCost * gaitClimbPct(gait) / 100);
    else e = e / (Fixed::one() + min(kDescentMax, -grade * kDescentGain));
    if (abs(grade) > kSteepSlope) e = e / kSteepSpeed;
    return std::min(kClosed - 1, toMm(e));
}

const std::vector<int32_t>& Campaign::efforts(Gait gait) const {
    if (efforts_.empty()) efforts_.resize(kGaitCount);
    std::vector<int32_t>& e = efforts_[static_cast<size_t>(gait)];
    if (!e.empty()) return e;
    e.assign(static_cast<size_t>(nx_) * ny_ * 8, kClosed);
    for (int32_t y = 0; y < ny_; ++y)
        for (int32_t x = 0; x < nx_; ++x)
            for (int32_t d = 0; d < 8; ++d) {
                const int32_t bx = x + kDirs[d][0], by = y + kDirs[d][1];
                if (bx < 0 || by < 0 || bx >= nx_ || by >= ny_ || !edgeOpen(x, y, bx, by)) continue;  // rivers: at the fords and bridges
                e[(static_cast<size_t>(y) * nx_ + x) * 8 + static_cast<size_t>(d)] =
                    effort(gait, groundAt(x, y), groundAt(bx, by), cellHeight(bx, by) - cellHeight(x, y), (d & 1) != 0);
            }
    return e;
}

Campaign::Pace Campaign::paceOf(const Stack& s) const {
    Pace p;
    for (const Army& a : s.armies)
        for (uint8_t k : a.artifacts) p.quicker = std::max(p.quicker, artifacts()[k].speed);
    if (airborne(s)) {
        p.air = true;
        p.flight = INT32_MAX;
        for (const Army& a : s.armies) p.flight = std::min(p.flight, flightPace(a.type));
        p.flight = std::max(1, p.flight);
        p.best = std::max<int32_t>(1, static_cast<int32_t>(int64_t{p.flight} * (100 + p.quicker) / 100));
        return p;
    }
    for (const Army& a : s.armies) {
        const Gait g = gaitOf(a.type);
        const int32_t v = std::max(1, groundSpeed(a.type));
        int32_t k = 0;
        while (k < p.gaits && p.gait[k] != g) ++k;
        if (k == p.gaits) {
            p.gait[p.gaits] = g;
            p.speed[p.gaits++] = v;
        } else {
            p.speed[k] = std::min(p.speed[k], v);
        }
    }
    // The quickest it could ever go: the slowest gait at its best, downhill.
    int64_t best = INT64_MAX;
    for (int32_t k = 0; k < p.gaits; ++k)
        best = std::min<int64_t>(best, (Fixed::fromInt(p.speed[k]) * bestGround(p.gait[k]) * (Fixed::one() + kDescentMax)).floorToInt() + 1);
    p.best = static_cast<int32_t>(std::clamp<int64_t>(best * (100 + p.quicker) / 100, 1, INT32_MAX));
    return p;
}

int32_t Campaign::marchTime(const Pace& p, int32_t cell, int32_t dir) const {
    const int32_t x = cell % nx_ + kDirs[dir][0], y = cell / nx_ + kDirs[dir][1];
    if (x < 0 || y < 0 || x >= nx_ || y >= ny_) return kClosed;
    int64_t t = 0;
    if (p.air) {
        t = ticksFor((dir & 1) ? kDiagonalMm : kStepMm, p.flight);
    } else {
        if (p.gaits == 0) return kClosed;
        for (int32_t k = 0; k < p.gaits; ++k) {
            const int32_t e = efforts(p.gait[k])[static_cast<size_t>(cell) * 8 + static_cast<size_t>(dir)];
            if (e == kClosed) return kClosed;
            t = std::max(t, ticksFor(e, p.speed[k]));  // the slowest on this ground sets the time
        }
    }
    return clampTime(t * 100 / (100 + p.quicker));
}

int32_t Campaign::seaTime(int32_t dx, int32_t dy) const {
    // From a port to its harbour may be two cells: the straight distance.
    const int64_t mm = (Fixed::fromInt(kCell) * sqrt(Fixed::fromInt(int64_t{dx} * dx + int64_t{dy} * dy)) * 1000).floorToInt();
    return clampTime(ticksFor(mm, kSeaSpeed));
}

bool Campaign::passable(const Stack& s, int32_t cx, int32_t cy) const {
    if (cx < 0 || cy < 0 || cx >= nx_ || cy >= ny_) return false;
    if (airborne(s)) return true;
    const Ground g = groundAt(cx, cy);
    for (const Army& a : s.armies)
        if (gaitSpeed(gaitOf(a.type), g) <= Fixed::zero()) return false;
    return !s.armies.empty();
}

int32_t Campaign::stepTime(const Stack& s, int32_t ax, int32_t ay, int32_t bx, int32_t by) const {
    if (s.boat) return seaTime(bx - ax, by - ay);
    const int32_t d = direction(bx - ax, by - ay);
    if (d < 0 || ax < 0 || ay < 0 || ax >= nx_ || ay >= ny_) return kClosed;
    return marchTime(paceOf(s), ay * nx_ + ax, d);
}

int32_t Campaign::legTime(const Stack& s) const {
    if (s.path.empty()) return 0;
    const auto [x, y] = s.path.front();
    // About to put out from a port: the first step is at the boat's speed.
    if (!s.boat && navigable(x, y) && !passable(s, x, y)) return seaTime(x - s.cx, y - s.cy);
    return stepTime(s, s.cx, s.cy, x, y);
}

int32_t Campaign::timeLeft(const Stack& s) const {
    if (s.path.empty()) return 0;
    const int32_t t = routeTime(s, s.path);
    return t == kClosed ? kClosed : std::max(0, t - s.progress);
}

int32_t Campaign::routeTime(const Stack& s, const std::vector<std::pair<int32_t, int32_t>>& path) const {
    // Afloat steps at the boat's speed, the rest on foot or wing.
    const Pace p = paceOf(s);
    bool afloat = s.boat != 0;
    int32_t px = s.cx, py = s.cy;
    int64_t t = 0;
    for (const auto& [x, y] : path) {
        if (!afloat && navigable(x, y) && !passable(s, x, y)) afloat = true;  // boards
        int64_t step;
        if (afloat) {
            step = seaTime(x - px, y - py);
            if (!navigable(x, y)) afloat = false;  // lands
        } else {
            const int32_t d = direction(x - px, y - py);
            step = d < 0 ? kClosed : marchTime(p, py * nx_ + px, d);
        }
        if (step == kClosed) return kClosed;
        t += step;
        px = x;
        py = y;
    }
    return static_cast<int32_t>(std::min<int64_t>(t, kClosed - 1));
}

bool Campaign::findPath(const Stack& s, int32_t tx, int32_t ty, std::vector<std::pair<int32_t, int32_t>>& out) const {
    out.clear();
    if (tx < 0 || ty < 0 || tx >= nx_ || ty >= ny_ || s.armies.empty()) return false;
    const Pace p = paceOf(s);
    const size_t n = static_cast<size_t>(nx_) * ny_;
    std::vector<int32_t> g(n, INT32_MAX), parent(n, -1);
    // Who stands where, once: another side's stack blocks the way, and so
    // does a cell of our own already too full to take this stack (Warlords
    // 9.2), except where the route ends (an attack, or joining them).
    std::vector<int16_t> ours(n, 0);
    std::vector<uint8_t> foe(n, 0);
    for (const Stack& o : stacks_) {
        if (o.armies.empty() || &o == &s) continue;
        const size_t k = static_cast<size_t>(o.cy) * nx_ + o.cx;
        if (o.owner != s.owner) foe[k] = 1;
        else ours[k] = static_cast<int16_t>(ours[k] + static_cast<int16_t>(o.armies.size()));
    }
    const int32_t mine = static_cast<int32_t>(s.armies.size());
    using Node = std::pair<int64_t, int32_t>;  // (f, index)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const int32_t start = s.cy * nx_ + s.cx, goal = ty * nx_ + tx;
    // The straight way at the best it could ever go: never more than the time.
    const auto h = [&](int32_t i) {
        const int64_t dx = std::abs(i % nx_ - tx), dy = std::abs(i / nx_ - ty);
        const int64_t mm = std::max(dx, dy) * kStepMm + std::min(dx, dy) * (kDiagonalMm - kStepMm);
        return ticksFor(mm, p.best);
    };
    g[static_cast<size_t>(start)] = 0;
    open.push({h(start), start});
    while (!open.empty()) {
        const auto [f, cur] = open.top();
        open.pop();
        if (cur == goal) break;
        if (f - h(cur) > g[static_cast<size_t>(cur)]) continue;
        for (int32_t d = 0; d < 8; ++d) {
            const int32_t x = cur % nx_ + kDirs[d][0], y = cur / nx_ + kDirs[d][1];
            if (x < 0 || y < 0 || x >= nx_ || y >= ny_) continue;
            const int32_t t = marchTime(p, cur, d);
            if (t == kClosed) continue;
            // Another side's stack blocks the way, except where the route ends (an attack).
            const int32_t ni = y * nx_ + x;
            if (ni != goal) {
                const size_t k = static_cast<size_t>(ni);
                if (foe[k] || ours[k] + mine > cellCapacity(x, y)) continue;
            }
            const int64_t ng = int64_t{g[static_cast<size_t>(cur)]} + t;
            if (ng < g[static_cast<size_t>(ni)]) {
                g[static_cast<size_t>(ni)] = static_cast<int32_t>(std::min<int64_t>(ng, INT32_MAX - 1));
                parent[static_cast<size_t>(ni)] = cur;
                open.push({ng + h(ni), ni});
            }
        }
    }
    if (g[static_cast<size_t>(goal)] == INT32_MAX) return false;
    for (int32_t i = goal; i != start; i = parent[static_cast<size_t>(i)]) out.push_back({i % nx_, i / nx_});
    std::reverse(out.begin(), out.end());
    return true;
}

const std::vector<uint16_t>& Campaign::marchField(int32_t city) const {
    if (marchFields_.empty()) marchFields_.resize(cities_.size());
    std::vector<uint16_t>& f = marchFields_[static_cast<size_t>(city)];
    if (!f.empty()) return f;
    // Dijkstra outwards from the city over a man's steps on foot: f[cell] is
    // the level plains metres worth of marching from that cell to the city.
    const std::vector<int32_t>& e = efforts(Gait::Foot);
    const size_t n = static_cast<size_t>(nx_) * ny_;
    std::vector<int64_t> mm(n, INT64_MAX);
    const CityState& c = cities_[static_cast<size_t>(city)];
    using Node = std::pair<int64_t, int32_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const int32_t start = c.cy * nx_ + c.cx;
    mm[static_cast<size_t>(start)] = 0;
    open.push({0, start});
    while (!open.empty()) {
        const auto [d, cur] = open.top();
        open.pop();
        if (d > mm[static_cast<size_t>(cur)]) continue;
        for (int32_t k = 0; k < 8; ++k) {
            const int32_t x = cur % nx_ + kDirs[k][0], y = cur / nx_ + kDirs[k][1];
            if (x < 0 || y < 0 || x >= nx_ || y >= ny_) continue;
            const size_t from = static_cast<size_t>(y) * nx_ + x;
            const int32_t step = e[from * 8 + static_cast<size_t>((k + 4) % 8)];  // from there to here
            if (step == kClosed) continue;
            const int64_t nd = d + step;
            if (nd < mm[from]) {
                mm[from] = nd;
                open.push({nd, static_cast<int32_t>(from)});
            }
        }
    }
    f.assign(n, UINT16_MAX);
    for (size_t k = 0; k < n; ++k)
        if (mm[k] != INT64_MAX) f[k] = static_cast<uint16_t>(std::min<int64_t>(mm[k] / 1000, 65000));
    return f;
}

}  // namespace illyria::sim
