// Ports and boats, after Warlords' navies: "the only units that can travel
// the seas and rivers ... a fast way of transporting groups of armies. A navy
// can carry a maximum of seven other armies" and "ignores the movement points
// of the armies it carries" (docs/GAME_DESIGN.md §4.6).
//
// Cities next to navigable water are ports and lay down boats for gold. A
// stack stepping from a port onto the water boards a free boat docked there
// (up to kBoatCapacity armies; the rest stay ashore), sails at the boat's
// speed, and lands on any coast it can walk. An empty boat lies in the port it
// came home to, or rides at anchor where its stack went ashore, until a stack
// of its side boards it again. A boat whose armies all fall is lost with them;
// an empty boat in a captured port changes hands; one at anchor is burnt by an
// enemy boat that reaches it.

#include <algorithm>
#include <climits>
#include <queue>

#include "illyria/sim/campaign.h"

namespace illyria::sim {

namespace {

using T = UnitType;

// Water bodies smaller than this (cells of 200 m) are ponds and narrow reaches: no boats.
constexpr int32_t kMinWater = 24;
// Route times are in 1/kTimeScale day-moves: exact for every army's movement points.
constexpr int32_t kTimeScale = 10080;

bool flies(UnitType t) { return t == T::Pegasi || t == T::Griffins || t == T::Dragons || t == T::Archons; }

const char* const kBoatNames[] = {"Grey Swan", "Sea Hawk", "Morning Star", "Wavecutter", "Saltwind", "Black Gull",
                                  "Silver Pike", "Storm Petrel", "Kestrel", "Long Serpent", "Tidewalker", "Amberwake",
                                  "Heron", "Sea Wolf", "Mistral", "Gannet"};
constexpr int32_t kBoatNameCount = static_cast<int32_t>(sizeof kBoatNames / sizeof kBoatNames[0]);

}  // namespace

void Campaign::initSea() {
    // Navigable water: the cells of every water body big enough to sail.
    const size_t n = static_cast<size_t>(nx_) * ny_;
    navigable_.assign(n, 0);
    boatsBuilt_.assign(sides_.size(), 0);
    std::vector<int32_t> label(n, -1);
    std::vector<int32_t> body;
    for (size_t start = 0; start < n; ++start) {
        if (terrain_[start] != Lay::Water || label[start] >= 0) continue;
        body.clear();
        body.push_back(static_cast<int32_t>(start));
        label[start] = static_cast<int32_t>(start);
        for (size_t k = 0; k < body.size(); ++k) {
            const int32_t x = body[k] % nx_, y = body[k] / nx_;
            for (int32_t dy = -1; dy <= 1; ++dy)
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    const int32_t bx = x + dx, by = y + dy;
                    if (bx < 0 || by < 0 || bx >= nx_ || by >= ny_) continue;
                    const size_t b = static_cast<size_t>(by) * nx_ + bx;
                    if (terrain_[b] != Lay::Water || label[b] >= 0) continue;
                    label[b] = static_cast<int32_t>(start);
                    body.push_back(static_cast<int32_t>(b));
                }
        }
        if (static_cast<int32_t>(body.size()) < kMinWater) continue;
        for (int32_t c : body) navigable_[static_cast<size_t>(c)] = 1;
    }
    // Ports: cities with navigable water within two cells; their harbour is the nearest such cell.
    for (CityState& c : cities_) {
        int32_t best = INT32_MAX;
        for (int32_t dy = -2; dy <= 2; ++dy)
            for (int32_t dx = -2; dx <= 2; ++dx) {
                const int32_t x = c.cx + dx, y = c.cy + dy;
                if (x < 0 || y < 0 || x >= nx_ || y >= ny_ || !navigable(x, y)) continue;
                const int32_t d = dx * dx + dy * dy;
                if (d < best) {
                    best = d;
                    c.hx = x;
                    c.hy = y;
                }
            }
        c.port = best != INT32_MAX;
    }
}

const Campaign::Boat* Campaign::findBoat(uint32_t id) const {
    for (const Boat& b : boats_)
        if (b.id == id) return &b;
    return nullptr;
}

const Campaign::Boat* Campaign::boatInPort(int32_t city) const {
    if (city < 0 || city >= static_cast<int32_t>(cities_.size())) return nullptr;
    for (const Boat& b : boats_)
        if (b.port == city && !b.stack && b.owner == cities_[static_cast<size_t>(city)].owner) return &b;
    return nullptr;
}

bool Campaign::buildBoat(int32_t city) {
    if (city < 0 || city >= static_cast<int32_t>(cities_.size())) return false;
    CityState& c = cities_[static_cast<size_t>(city)];
    if (!c.port || c.razed || c.owner == kNeutral || c.boatDays > 0 || sides_[c.owner].gold < kBoatCost) return false;
    sides_[c.owner].gold -= kBoatCost;
    c.boatDays = kBoatDays;
    return true;
}

void Campaign::seaDay() {
    // The shipwrights finish their boats; every boat afloat costs its upkeep.
    for (size_t i = 0; i < cities_.size(); ++i) {
        CityState& c = cities_[i];
        if (c.boatDays <= 0) continue;
        if (c.razed || c.owner == kNeutral) {
            c.boatDays = 0;
            continue;
        }
        if (--c.boatDays > 0) continue;
        Boat b;
        b.id = nextId_++;
        b.owner = c.owner;
        const int32_t k = boatsBuilt_[c.owner]++;
        b.name = kBoatNames[(c.owner * 5 + k) % kBoatNameCount];
        if (k >= kBoatNameCount) b.name += " " + std::to_string(k / kBoatNameCount + 1);
        b.cx = c.cx;
        b.cy = c.cy;
        b.port = static_cast<int32_t>(i);
        boats_.push_back(b);
    }
    for (const Boat& b : boats_) sides_[b.owner].gold = std::max(0, sides_[b.owner].gold - kBoatUpkeep);
}

uint32_t Campaign::grant(int32_t city, uint8_t side, UnitType type, int32_t armies) {
    CityState& c = cities_[static_cast<size_t>(city)];
    c.owner = side;
    c.producing = -1;
    c.progress = 0;
    // Whoever held it is gone.
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [&](const Stack& s) { return s.cx == c.cx && s.cy == c.cy && s.owner != side; }),
                  stacks_.end());
    Stack& s = newStack(side, c.cx, c.cy);
    for (int32_t k = 0; k < std::min(armies, kMaxStack); ++k) s.armies.push_back(makeArmy(type, ""));
    return s.id;
}

int32_t Campaign::stepMoves(const Stack& s) const { return s.boat ? kSeaMoves : stackMoves(s); }
int32_t Campaign::stepCost(const Stack& s, int32_t cx, int32_t cy) const { return s.boat ? kSeaCost : stackCost(s, cx, cy); }

void Campaign::splitOff(size_t index, size_t keep) {
    // The weakest (never a hero) stay behind on the quay, in a stack of their own.
    if (stacks_[index].armies.size() <= keep) return;
    Stack left;
    left.id = nextId_++;
    left.owner = stacks_[index].owner;
    left.cx = stacks_[index].cx;
    left.cy = stacks_[index].cy;
    std::vector<Army>& armies = stacks_[index].armies;
    while (armies.size() > keep) {
        size_t weakest = 0;
        for (size_t k = 1; k < armies.size(); ++k) {
            const bool better = armies[weakest].hero() || (!armies[k].hero() && strength(armies[k]) < strength(armies[weakest]));
            if (better) weakest = k;
        }
        left.armies.push_back(armies[weakest]);
        armies.erase(armies.begin() + static_cast<long>(weakest));
    }
    stacks_.push_back(left);
}

bool Campaign::boardIfSailing(size_t index) {
    const Stack& s = stacks_[index];
    if (s.boat) return true;
    const auto [nx, ny] = s.path.front();
    if (!navigable(nx, ny) || stackCost(s, nx, ny) < kProhibited) return true;  // on foot, or on wings
    // A free boat of ours in this port, putting out to its harbour, or one at anchor alongside.
    Boat* boat = nullptr;
    const int32_t city = cityAt(s.cx, s.cy);
    if (city >= 0 && cities_[static_cast<size_t>(city)].hx == nx && cities_[static_cast<size_t>(city)].hy == ny) {
        for (Boat& b : boats_)
            if (!boat && b.port == city && !b.stack && b.owner == s.owner) boat = &b;
    }
    for (Boat& b : boats_)
        if (!boat && b.port < 0 && !b.stack && b.owner == s.owner && b.cx == nx && b.cy == ny && std::abs(nx - s.cx) <= 1 && std::abs(ny - s.cy) <= 1)
            boat = &b;
    if (!boat) {
        stacks_[index].path.clear();
        stacks_[index].progress = 0;
        return false;
    }
    splitOff(index, kBoatCapacity);
    Stack& aboard = stacks_[index];
    aboard.boat = boat->id;
    aboard.progress = 0;
    boat->stack = aboard.id;
    boat->port = -1;
    boat->cx = aboard.cx;
    boat->cy = aboard.cy;
    ++voyages_;
    return true;
}

bool Campaign::embark(uint32_t stackId) {
    size_t index = stacks_.size();
    for (size_t i = 0; i < stacks_.size(); ++i)
        if (stacks_[i].id == stackId) index = i;
    if (index == stacks_.size() || stacks_[index].garrison || stacks_[index].boat || pending_.stack) return false;
    const int32_t city = cityAt(stacks_[index].cx, stacks_[index].cy);
    const Boat* boat = boatInPort(city);
    if (!boat || cities_[static_cast<size_t>(city)].owner != stacks_[index].owner) return false;
    // Friends idle in the port come aboard too, as many as the boat carries.
    for (Stack& o : stacks_) {
        Stack& s = stacks_[index];
        if (&o == &s || o.owner != s.owner || o.cx != s.cx || o.cy != s.cy || o.boat || o.garrison || !o.path.empty()) continue;
        while (!o.armies.empty() && static_cast<int32_t>(s.armies.size()) < kBoatCapacity) {
            s.armies.push_back(o.armies.front());
            o.armies.erase(o.armies.begin());
        }
    }
    const CityState& c = cities_[static_cast<size_t>(city)];
    stacks_[index].path = {{c.hx, c.hy}};
    stacks_[index].progress = 0;
    boardIfSailing(index);
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& s) { return s.armies.empty(); }), stacks_.end());
    syncBoats();
    return true;
}

bool Campaign::land(uint32_t stackId) {
    Stack* s = stackById(stackId);
    if (!s || !s->boat || !navigable(s->cx, s->cy)) return false;
    // Ashore at the nearest friendly port, else the easiest ground alongside.
    for (size_t i = 0; i < cities_.size(); ++i) {
        const CityState& c = cities_[i];
        if (c.port && c.owner == s->owner && !c.razed && c.hx == s->cx && c.hy == s->cy) {
            s->path = {{c.cx, c.cy}};
            s->progress = 0;
            return true;
        }
    }
    int32_t best = kProhibited, bx = -1, by = -1;
    for (int32_t dy = -1; dy <= 1; ++dy)
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const int32_t x = s->cx + dx, y = s->cy + dy;
            if ((dx == 0 && dy == 0) || x < 0 || y < 0 || x >= nx_ || y >= ny_ || navigable(x, y)) continue;
            bool foe = false;
            for (const Stack& o : stacks_) foe = foe || (o.owner != s->owner && o.cx == x && o.cy == y && !o.armies.empty());
            const int32_t city = cityAt(x, y);
            if (foe || (city >= 0 && cities_[static_cast<size_t>(city)].owner != s->owner)) continue;
            const int32_t cost = stackCost(*s, x, y) - (city >= 0 ? 10 : 0);
            if (stackCost(*s, x, y) < kProhibited && cost < best) {
                best = cost;
                bx = x;
                by = y;
            }
        }
    if (bx < 0) return false;
    s->path = {{bx, by}};
    s->progress = 0;
    return true;
}

void Campaign::syncBoats() {
    std::vector<uint8_t> lost(boats_.size(), 0);
    for (size_t i = 0; i < boats_.size(); ++i) {
        Boat& b = boats_[i];
        if (b.stack) {
            Stack* s = stackById(b.stack);
            if (!s || s->armies.empty()) {
                lost[i] = 1;  // gone down with its armies
                continue;
            }
            if (s->boat != b.id) {
                b.stack = 0;
                continue;
            }
            if (navigable(s->cx, s->cy)) {
                b.cx = s->cx;
                b.cy = s->cy;
                continue;
            }
            const bool quay = s->cx == b.cx && s->cy == b.cy;
            if (quay && !s->path.empty()) continue;  // still casting off
            // Ashore: the boat ties up in a port of ours, or rides at anchor off the coast.
            s->boat = 0;
            b.stack = 0;
            const int32_t city = cityAt(s->cx, s->cy);
            if (city >= 0 && cities_[static_cast<size_t>(city)].port && cities_[static_cast<size_t>(city)].owner == b.owner) {
                b.port = city;
                b.cx = s->cx;
                b.cy = s->cy;
            }
        } else if (b.port >= 0) {
            const CityState& c = cities_[static_cast<size_t>(b.port)];
            if (c.razed) lost[i] = 1;
            else if (c.owner != b.owner && c.owner != kNeutral) b.owner = c.owner;  // taken with the port
        } else {
            for (const Stack& o : stacks_)
                if (o.owner != b.owner && o.boat && o.cx == b.cx && o.cy == b.cy && !o.armies.empty()) lost[i] = 1;  // burnt at anchor
        }
    }
    size_t k = 0;
    for (size_t i = 0; i < boats_.size(); ++i)
        if (!lost[i]) boats_[k++] = boats_[i];
    boats_.resize(k);
}

void Campaign::searchRoutes(const Stack& s, bool sail, int32_t assumePort, int32_t goal, std::vector<int32_t>& g,
                            std::vector<int32_t>& parent) const {
    // Dijkstra over (cell, afloat). Times are movement cost over movement
    // points, so walking and sailing compare fairly. A cell held by another
    // side can be entered (an attack) but not passed through.
    const size_t n = static_cast<size_t>(nx_) * ny_;
    g.assign(n * 2, INT32_MAX);
    parent.assign(n * 2, -1);
    std::vector<uint8_t> foe(n, 0), anchored(n, 0);
    for (const Stack& o : stacks_)
        if (o.owner != s.owner && !o.armies.empty()) foe[static_cast<size_t>(o.cy) * nx_ + o.cx] = 1;
    std::vector<uint8_t> docked(cities_.size(), 0);
    if (sail) {
        for (const Boat& b : boats_) {
            if (b.owner != s.owner || b.stack) continue;
            if (b.port >= 0) docked[static_cast<size_t>(b.port)] = 1;
            else anchored[static_cast<size_t>(b.cy) * nx_ + b.cx] = 1;
        }
        if (assumePort >= 0) docked[static_cast<size_t>(assumePort)] = 1;
    }
    std::vector<int32_t> cityOf(n, -1);
    std::vector<uint8_t> harbour(n, 0);
    for (size_t i = 0; i < cities_.size(); ++i) {
        if (cities_[i].razed) continue;
        cityOf[static_cast<size_t>(cities_[i].cy) * nx_ + cities_[i].cx] = static_cast<int32_t>(i);
        if (cities_[i].port) harbour[static_cast<size_t>(cities_[i].hy) * nx_ + cities_[i].hx] = 1;
    }
    const int32_t landMoves = std::max(1, stackMoves(s));
    const int32_t seaStep = kSeaCost * kTimeScale / kSeaMoves;
    using Node = std::pair<int32_t, int32_t>;  // (time, state)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const int32_t start = (s.cy * nx_ + s.cx) * 2 + (s.boat ? 1 : 0);
    g[static_cast<size_t>(start)] = 0;
    open.push({0, start});
    const auto relax = [&](int32_t from, int32_t cell, int32_t afloat, int32_t t) {
        const int32_t st = cell * 2 + afloat;
        const int32_t ng = g[static_cast<size_t>(from)] + t;
        if (ng < g[static_cast<size_t>(st)]) {
            g[static_cast<size_t>(st)] = ng;
            parent[static_cast<size_t>(st)] = from;
            open.push({ng, st});
        }
    };
    while (!open.empty()) {
        const auto [t, cur] = open.top();
        open.pop();
        if (t > g[static_cast<size_t>(cur)]) continue;
        const int32_t cell = cur / 2, afloat = cur % 2;
        if (cell == goal) break;
        if (cur != start && foe[static_cast<size_t>(cell)]) continue;  // the fight ends the route
        const int32_t cx = cell % nx_, cy = cell / nx_;
        const int32_t city = cityOf[static_cast<size_t>(cell)];
        if (afloat && harbour[static_cast<size_t>(cell)]) {
            // Into port from its harbour, which may lie two cells out.
            for (const CityState& c : cities_)
                if (c.port && !c.razed && c.hx == cx && c.hy == cy && stackCost(s, c.cx, c.cy) < kProhibited)
                    relax(cur, c.cy * nx_ + c.cx, 0, c.cx != cx && c.cy != cy ? seaStep * 3 / 2 : seaStep);
        }
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int32_t x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= nx_ || y >= ny_) continue;
                const int32_t ni = y * nx_ + x;
                const bool diagonal = dx != 0 && dy != 0;
                const int32_t sea = diagonal ? seaStep * 3 / 2 : seaStep;
                const int32_t walk = stackCost(s, x, y);
                if (afloat) {
                    if (navigable(x, y)) relax(cur, ni, 1, sea);
                    else if (walk < kProhibited) relax(cur, ni, 0, sea);  // landing
                    continue;
                }
                if (walk < kProhibited && edgeOpen(cx, cy, x, y)) {
                    const int32_t cost = diagonal ? walk * 3 / 2 : walk;
                    relax(cur, ni, 0, cost * kTimeScale / landMoves);
                } else if (sail && anchored[static_cast<size_t>(ni)]) {
                    relax(cur, ni, 1, sea);  // aboard our boat at anchor alongside
                }
            }
        }
        if (city < 0) continue;
        const CityState& c = cities_[static_cast<size_t>(city)];
        if (!c.port) continue;
        const int32_t out = c.hy * nx_ + c.hx;
        const int32_t sea = c.hx != cx && c.hy != cy ? seaStep * 3 / 2 : seaStep;
        if (!afloat && sail && c.owner == s.owner && docked[static_cast<size_t>(city)]) relax(cur, out, 1, sea);  // put out
        if (afloat) relax(cur, out, 1, sea);  // casting off from the quay
    }
}

bool Campaign::routeTo(const Stack& s, int32_t cell, const std::vector<int32_t>& g, const std::vector<int32_t>& parent,
                       std::vector<std::pair<int32_t, int32_t>>& out) const {
    out.clear();
    const int32_t a = cell * 2, b = cell * 2 + 1;
    const int32_t end = g[static_cast<size_t>(a)] <= g[static_cast<size_t>(b)] ? a : b;
    if (g[static_cast<size_t>(end)] == INT32_MAX) return false;
    const int32_t start = (s.cy * nx_ + s.cx) * 2 + (s.boat ? 1 : 0);
    for (int32_t st = end; st != start; st = parent[static_cast<size_t>(st)]) {
        if (st < 0) return false;
        out.push_back({(st / 2) % nx_, (st / 2) / nx_});
    }
    std::reverse(out.begin(), out.end());
    return !out.empty();
}

int32_t Campaign::routeTime(const Stack& s, const std::vector<std::pair<int32_t, int32_t>>& path) const {
    // The same measure as searchRoutes, for a route already planned.
    const int32_t landMoves = std::max(1, stackMoves(s));
    const int32_t seaStep = kSeaCost * kTimeScale / kSeaMoves;
    bool afloat = s.boat != 0;
    int32_t px = s.cx, py = s.cy, t = 0;
    for (const auto& [x, y] : path) {
        const bool diagonal = x != px && y != py;
        const int32_t walk = stackCost(s, x, y);
        if (!afloat && navigable(x, y) && walk >= kProhibited) afloat = true;  // boards
        if (afloat) {
            t += diagonal ? seaStep * 3 / 2 : seaStep;
            if (!navigable(x, y)) afloat = false;  // lands
        } else {
            t += (diagonal ? walk * 3 / 2 : walk) * kTimeScale / landMoves;
        }
        px = x;
        py = y;
    }
    return t;
}

bool Campaign::boardsBoat(const Stack& s, const std::vector<std::pair<int32_t, int32_t>>& path) const {
    if (s.boat) return true;
    for (const auto& [x, y] : path)
        if (navigable(x, y) && stackCost(s, x, y) >= kProhibited) return true;
    return false;
}

bool Campaign::planRoute(const Stack& s, int32_t tx, int32_t ty, std::vector<std::pair<int32_t, int32_t>>& out) const {
    out.clear();
    if (tx < 0 || ty < 0 || tx >= nx_ || ty >= ny_) return false;
    // Fliers fly; without a boat of our own anywhere, the road is the only way.
    bool flier = !s.armies.empty(), anyFlier = false;
    for (const Army& a : s.armies) {
        flier = flier && (flies(a.type) || a.hero());
        anyFlier = anyFlier || flies(a.type);
    }
    bool boat = s.boat != 0;
    for (const Boat& b : boats_) boat = boat || (b.owner == s.owner && !b.stack);
    if ((flier && anyFlier) || !boat) return findPath(s, tx, ty, out);
    std::vector<std::pair<int32_t, int32_t>> walk;
    const bool byLand = !s.boat && findPath(s, tx, ty, walk);
    std::vector<int32_t> g, parent;
    searchRoutes(s, true, -1, ty * nx_ + tx, g, parent);
    std::vector<std::pair<int32_t, int32_t>> sail;
    const bool bySea = routeTo(s, ty * nx_ + tx, g, parent, sail) && boardsBoat(s, sail);
    if (bySea && (!byLand || routeTime(s, sail) < routeTime(s, walk))) {
        out = std::move(sail);
        return true;
    }
    if (byLand) {
        out = std::move(walk);
        return true;
    }
    return false;
}

void Campaign::runSeaAi(uint8_t side) {
    // The computer takes to the boats where a city is out of reach on foot,
    // or much quicker to reach by water: it builds a boat in a port that has
    // such a city across the water, sails its strong stacks from there, and
    // sends any stack aboard at the nearest city it can take.
    const auto attackable = [&](const CityState& c, int32_t power) {
        int32_t guard = 0;
        for (const Stack& o : stacks_)
            if (o.cx == c.cx && o.cy == c.cy && o.owner != side)
                for (const Army& a : o.armies) guard += strength(a);
        return guard * 3 <= power * 2 + c.defense;
    };
    const auto power = [](const Stack& s) {
        int32_t p = 0;
        for (const Army& a : s.armies) p += strength(a);
        return p;
    };
    // The best city to sail for from where a stack stands: the quickest, if
    // the water saves enough over walking (or walking cannot get there).
    std::vector<int32_t> gs, ps, gl, pl;
    const auto bestVoyage = [&](const Stack& s, int32_t assumePort, int32_t strengthNeeded, bool needSaving) {
        searchRoutes(s, true, assumePort, -1, gs, ps);
        if (needSaving) searchRoutes(s, false, -1, -1, gl, pl);
        int32_t target = -1, best = INT32_MAX;
        for (size_t i = 0; i < cities_.size(); ++i) {
            const CityState& c = cities_[i];
            if (c.owner == side || c.razed || !attackable(c, strengthNeeded)) continue;
            const size_t cell = static_cast<size_t>(c.cy) * nx_ + c.cx;
            const int32_t sea = std::min(gs[cell * 2], gs[cell * 2 + 1]);
            if (sea == INT32_MAX) continue;
            if (needSaving) {
                const int32_t walk = std::min(gl[cell * 2], gl[cell * 2 + 1]);
                if (walk != INT32_MAX && static_cast<int64_t>(sea) * 5 >= static_cast<int64_t>(walk) * 3) continue;
            }
            if (sea < best) {
                best = sea;
                target = static_cast<int32_t>(i);
            }
        }
        return target;
    };
    // Shipwrights: a boat where it opens a way, while none waits in port
    // (or on the slips), and no more than three in all.
    int32_t fleet = 0, ready = 0;
    for (const Boat& b : boats_) {
        fleet += b.owner == side;
        ready += b.owner == side && (b.port >= 0 || b.stack);
    }
    for (const CityState& c : cities_) ready += c.owner == side && c.boatDays > 0;
    if (ready == 0 && fleet < 3 && sides_[side].gold >= kBoatCost + 20) {
        int32_t tried = 0;
        for (size_t i = 0; i < cities_.size() && tried < 3; ++i) {
            const CityState& c = cities_[i];
            if (c.owner != side || c.razed || !c.port) continue;
            ++tried;
            Stack probe;
            probe.owner = side;
            probe.cx = c.cx;
            probe.cy = c.cy;
            probe.armies.push_back(Army{});  // a light infantry's pace
            if (bestVoyage(probe, static_cast<int32_t>(i), 12, true) < 0) continue;
            buildBoat(static_cast<int32_t>(i));
            break;
        }
    }
    // Sailors.
    std::vector<uint32_t> ids;
    for (const Stack& s : stacks_)
        if (s.owner == side && !s.garrison && s.path.empty() && !s.armies.empty()) ids.push_back(s.id);
    for (uint32_t id : ids) {
        size_t index = stacks_.size();
        for (size_t i = 0; i < stacks_.size(); ++i)
            if (stacks_[i].id == id) index = i;
        if (index == stacks_.size()) continue;
        const Stack& s = stacks_[index];
        const int32_t here = cityAt(s.cx, s.cy);
        if (!s.boat && (here < 0 || !boatInPort(here) || s.armies.size() < 3)) continue;
        const int32_t target = bestVoyage(s, -1, power(s), !s.boat);
        if (target < 0) continue;
        std::vector<std::pair<int32_t, int32_t>> path;
        const CityState& c = cities_[static_cast<size_t>(target)];
        if (!routeTo(s, c.cy * nx_ + c.cx, gs, ps, path)) continue;
        if (!s.boat && !boardsBoat(s, path)) continue;
        if (here >= 0 && !s.boat) splitOff(index, s.armies.size() - 1);  // one stays to hold the port
        stacks_[index].path = std::move(path);
        stacks_[index].progress = 0;
    }
}

}  // namespace illyria::sim
