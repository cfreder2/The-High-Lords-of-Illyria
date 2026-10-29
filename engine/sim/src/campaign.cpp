#include "illyria/sim/campaign.h"

#include <algorithm>
#include <climits>
#include <queue>

#include "illyria/sim/hash.h"
#include "illyria/sim/noise.h"

namespace illyria::sim {

namespace {

using T = UnitType;

// What each side trains at its capital (light infantry is everywhere), and
// the gold it starts with, after the empires of Warlords. Only common troops:
// each realm's rare ones march with its hero from the start, and its capital
// holds a one-off levy of more (campaign_rare.cpp).
struct Realm {
    std::vector<UnitType> specialties;
    int32_t gold;
};
const Realm& realm(uint8_t side) {
    static const Realm kRealms[] = {
        {{T::HeavyCavalry, T::HeavyInfantry, T::Catapult}, 300},  // Aurelian Throne: knights and engines
        {{T::HeavyInfantry, T::Pikemen}, 200},                   // Highcrag Giants
        {{T::Dwarves, T::HeavyInfantry, T::Catapult}, 450},      // Ironhold Dwarves: rich, and engineers
        {{T::WolfRiders, T::Warrior}, 250},                      // Orcs of Grath
        {{T::Archers, T::LightCavalry}, 250},                    // Sylvaran Elves
        {{T::LightCavalry, T::HeavyCavalry}, 300},               // Sunreach Riders
        {{T::Archers, T::Pikemen}, 300},                         // Moonward Order
        {{T::HeavyInfantry, T::WolfRiders, T::Catapult}, 350},   // Lord Malvoren
    };
    return kRealms[side % 8];
}

// Training time (days) and cost (gold): the Warlords city table made slow
// and dear (about twice the days, three to five times the gold), so that an
// army lost is not soon replaced. Rare types are never trained; a levy of
// them takes three days and five gold a point of strength to muster.
Campaign::Option trained(UnitType t) {
    switch (t) {
        case T::LightInfantry: return {t, 2, 12};
        case T::HeavyInfantry: return {t, 4, 20};
        case T::Archers: return {t, 4, 20};
        case T::Pikemen: return {t, 4, 20};
        case T::LightCavalry: return {t, 5, 28};
        case T::HeavyCavalry: return {t, 6, 36};
        case T::Dwarves: return {t, 4, 24};
        case T::WolfRiders: return {t, 5, 28};
        case T::Warrior: return {t, 4, 24};
        case T::Catapult: return {t, 6, 40};  // dear and slow to build, as Warlords II's siege engines
        default: return {t, 3, 5 * warlordsStrength(t)};  // a levy
    }
}

}  // namespace

int32_t Campaign::strength(const Army& a) {
    int32_t battle = 0;
    for (uint8_t k : a.artifacts) battle += artifacts()[k].battle;  // a hero's swords and staves
    // A hero's level is his strength, as in Warlords; a soldier's rank is a
    // small edge in the fight instead (edges.h), not more strength.
    return std::min(9, warlordsStrength(a.type) + (a.hero() ? a.rank : 0) + a.blessings + battle);
}

int32_t Campaign::squadSize(UnitType t) {
    switch (t) {
        case T::LightCavalry:
        case T::HeavyCavalry:
        case T::WolfRiders: return 8;
        case T::Giants:
        case T::Wizards:
        case T::Archons:
        case T::Devils: return 3;
        case T::Griffins:
        case T::Pegasi:
        case T::Minotaurs:
        case T::Unicorns:
        case T::Journeyman: return 4;
        case T::Elephants:
        case T::Demons: return 2;
        case T::Dragons:
        case T::Hero: return 1;
        case T::Catapult: return 2;
        case T::Dwarves:
        case T::Berserker: return 10;
        case T::Spiders:
        case T::DwarfGrenadier: return 6;
        default: return 12;
    }
}

int32_t Campaign::movesPerDay(UnitType t) {
    switch (t) {
        case T::LightInfantry: return 10;
        case T::HeavyInfantry:
        case T::Pikemen:
        case T::Warrior: return 8;
        case T::Archers: return 12;
        case T::LightCavalry: return 18;
        case T::HeavyCavalry:
        case T::Unicorns: return 16;
        case T::WolfRiders: return 14;
        case T::Dwarves:
        case T::DwarfGrenadier: return 9;
        case T::Griffins:
        case T::Pegasi: return 16;
        case T::Dragons:
        case T::Archons: return 18;
        case T::Wizards: return 20;
        case T::Hero: return 12;
        case T::Elephants:
        case T::Spiders: return 12;
        case T::Catapult: return 6;  // dragged on wheels
        default: return 10;
    }
}

int32_t Campaign::moveCost(UnitType t, Lay terrain) {
    // The Warlords movement table: road, bridge, water, forest, hill, mountain, plain, marsh, city.
    const int32_t P = kProhibited;
    static const int32_t kGiants[] = {1, 1, P, 5, 4, P, 2, 5, 1};
    static const int32_t kDwarves[] = {1, 1, P, 6, 3, P, 2, 6, 1};
    static const int32_t kHeavy[] = {1, 1, P, 4, 6, P, 2, 5, 1};
    static const int32_t kLight[] = {1, 1, P, 4, 6, P, 2, 5, 1};
    static const int32_t kElves[] = {1, 1, P, 2, 5, P, 2, 6, 1};
    static const int32_t kCavalry[] = {1, 1, P, 5, 6, P, 2, 5, 1};
    static const int32_t kWolves[] = {1, 1, P, 4, 6, P, 2, 4, 1};
    static const int32_t kFliers[] = {2, 2, 2, 2, 2, 3, 2, 2, 2};
    static const int32_t kUndead[] = {1, 1, P, 4, 5, P, 2, 4, 1};
    static const int32_t kDemons[] = {1, 1, P, 4, 5, P, 2, 5, 1};  // demons and devils; wizards walk like light infantry
    // Wheels want roads: slow through woods and up hills, never into a marsh.
    static const int32_t kWheels[] = {1, 1, P, 6, 6, P, 2, P, 1};
    const int32_t* row = kLight;
    switch (t) {
        case T::Giants:
        case T::Minotaurs: row = kGiants; break;
        case T::Dwarves:
        case T::DwarfGrenadier: row = kDwarves; break;
        case T::HeavyInfantry:
        case T::Pikemen:
        case T::Warrior: row = kHeavy; break;
        case T::Archers: row = kElves; break;
        case T::LightCavalry:
        case T::HeavyCavalry:
        case T::Elephants:
        case T::Unicorns: row = kCavalry; break;
        case T::WolfRiders:
        case T::Spiders: row = kWolves; break;
        case T::Undead: row = kUndead; break;
        case T::Demons:
        case T::Devils: row = kDemons; break;
        case T::Catapult: row = kWheels; break;
        default:
            if (flies(t)) row = kFliers;
            break;
    }
    return row[static_cast<int>(terrain)];
}

int32_t Campaign::upkeep(UnitType t) {
    // Common troops: a quarter of their training price a day. Rare ones what
    // Warlords asked for them (half their price in its city table).
    if (t == T::Hero) return 0;
    if (trainable(t)) return (trained(t).cost + 3) / 4;
    switch (t) {
        case T::Giants:
        case T::Minotaurs:
        case T::DwarfGrenadier: return 4;
        case T::Spiders:
        case T::Journeyman:
        case T::Berserker: return 3;
        case T::Griffins:
        case T::Pegasi: return 8;
        case T::Elephants: return 6;
        case T::Unicorns: return 7;
        default: return 10;  // the allies of the ruins (who mostly serve for nothing)
    }
}

Campaign::Campaign(const Continent& continent, uint64_t seed) : continent_(continent), rng_(seed, 0xCA4A16), aiRng_(seed, 0xA1) {
    nx_ = static_cast<int32_t>(Continent::kWidth / kCell);
    ny_ = static_cast<int32_t>(Continent::kHeight / kCell);
    initTerrain();
    for (const Continent::Faction& f : Continent::factions()) sides_.push_back({f.name, 0, true});
    armiesRaised_.assign(sides_.size(), 0);
    initCities();
    initRare();
    initSites();
    initSea();
    initTerritory();  // after the cities: they claim the land around them
}

void Campaign::initTerrain() {
    terrain_.assign(static_cast<size_t>(nx_) * ny_, Lay::Plain);
    riverEast_.assign(terrain_.size(), 0);
    riverNorth_.assign(terrain_.size(), 0);
    std::vector<uint8_t> nearRiver(terrain_.size(), 0);
    cellHeight_.assign(terrain_.size(), 0);
    // Road cells: every cell a road passes through, walked every 10 m (the
    // samples' road distance only reaches 60 m, short of a cell's corners;
    // and a road may clip a cell's corner for a few metres).
    std::vector<uint8_t> road(terrain_.size(), 0);
    for (const Polyline& r : continent_.roads()) {
        for (size_t k = 0; k + 1 < r.points.size(); ++k) {
            const Vec3 a = r.points[k], d = r.points[k + 1] - r.points[k];
            const int32_t steps = std::max(1, static_cast<int32_t>((d.flat().length() / 10).floorToInt()));
            for (int32_t i = 0; i <= steps; ++i) {
                const Vec3 p = a + d * Fixed::fromRatio(i, steps);
                const int32_t x = cellOf(static_cast<int32_t>(p.x.floorToInt())), y = cellOf(static_cast<int32_t>(p.y.floorToInt()));
                if (x >= 0 && y >= 0 && x < nx_ && y < ny_) road[static_cast<size_t>(y) * nx_ + x] = 1;
            }
        }
    }
    for (int32_t y = 0; y < ny_; ++y) {
        for (int32_t x = 0; x < nx_; ++x) {
            const Fixed px = Fixed::fromInt(centre(x)), py = Fixed::fromInt(centre(y));
            const Continent::Sample s = continent_.sample(px, py);
            Fixed surface;
            Lay t = Lay::Plain;
            Fixed deck;
            const bool open = (s.land < Fixed::zero() && s.height < Fixed::zero()) || (s.inLake && s.height < s.lakeSurface);
            if (road[static_cast<size_t>(y) * nx_ + x] && !open) {  // a road crosses the cell (rivers it bridges or fords)
                t = Lay::Road;
            } else if (continent_.waterSurfaceOf(s, surface) && !continent_.onBridge(px, py, deck)) {
                t = Lay::Water;
            } else {
                switch (continent_.landGround(px, py, s.height, s.mountain, s.roadDist)) {
                    case Ground::Forest: t = Lay::Forest; break;
                    case Ground::Swamp: t = Lay::Marsh; break;
                    case Ground::Rock: t = s.mountain > Fixed::fromInt(90) ? Lay::Mountain : Lay::Hill; break;
                    default: t = s.mountain > Fixed::fromInt(25) ? Lay::Hill : Lay::Plain; break;
                }
            }
            terrain_[static_cast<size_t>(y) * nx_ + x] = t;
            nearRiver[static_cast<size_t>(y) * nx_ + x] = s.riverDist < Fixed::fromInt(173);
            cellHeight_[static_cast<size_t>(y) * nx_ + x] = static_cast<int16_t>(std::clamp<int64_t>(s.height.floorToInt(), -2000, 4000));
        }
    }
    // Rivers run between cells: walking from one cell centre to the next must
    // not wade deep water, except over a bridge or through a ford.
    const auto wet = [&](Fixed x, Fixed y) {
        const Continent::Sample s = continent_.sample(x, y);
        if (s.riverDist >= s.riverHalfWidth || s.atFord) return false;
        Fixed deck;
        return !continent_.onBridge(x, y, deck);
    };
    for (int32_t y = 0; y < ny_; ++y) {
        for (int32_t x = 0; x < nx_; ++x) {
            const size_t k = static_cast<size_t>(y) * nx_ + x;
            for (int dir = 0; dir < 2; ++dir) {
                const int32_t bx = x + (dir == 0), by = y + (dir == 1);
                if (bx >= nx_ || by >= ny_) continue;
                const size_t kb = static_cast<size_t>(by) * nx_ + bx;
                if (!nearRiver[k] && !nearRiver[kb]) continue;
                bool blocked = false;
                for (int32_t i = 1; i < 10 && !blocked; ++i) {
                    const Fixed px = Fixed::fromInt(centre(x) + (bx - x) * kCell * i / 10);
                    const Fixed py = Fixed::fromInt(centre(y) + (by - y) * kCell * i / 10);
                    blocked = wet(px, py);
                }
                (dir == 0 ? riverEast_ : riverNorth_)[k] = blocked;
            }
        }
    }
}

void Campaign::initCities() {
    const uint64_t sc = noise::nextSeed(continent_.seed() ^ 0xC17E5);
    for (size_t i = 0; i < continent_.cities().size(); ++i) {
        const Continent::City& c = continent_.cities()[i];
        CityState st;
        st.owner = c.owner;
        st.cx = cellOf(static_cast<int32_t>(c.pos.x.floorToInt()));
        st.cy = cellOf(static_cast<int32_t>(c.pos.y.floorToInt()));
        terrain_[static_cast<size_t>(st.cy) * nx_ + st.cx] = Lay::City;
        const auto roll = [&](int64_t k) { return noise::lattice(sc, static_cast<int64_t>(i), k); };
        if (c.capital) {
            st.defense = 6;
            st.income = 30;
            st.options.push_back(trained(T::LightInfantry));
            for (UnitType t : realm(c.owner).specialties) st.options.push_back(trained(t));
        } else {
            st.defense = 3 + static_cast<int32_t>((roll(1) * 3).floorToInt());
            st.income = 15 + st.defense * 2 + static_cast<int32_t>((roll(2) * 6).floorToInt());
            // What the land around it raises: light infantry everywhere, then
            // one or two more after the ground (hills, woods, plains).
            st.options.push_back(trained(T::LightInfantry));
            const Continent::Sample s = continent_.sample(c.pos.x, c.pos.y);
            const Ground g = continent_.landGround(c.pos.x + Fixed::fromInt(200), c.pos.y, s.height, s.mountain, s.roadDist);
            const UnitType hills[] = {T::Dwarves, T::HeavyInfantry, T::Pikemen}, woods[] = {T::Archers, T::WolfRiders, T::Warrior},
                           plains[] = {T::HeavyInfantry, T::LightCavalry, T::Pikemen, T::HeavyCavalry};
            const int32_t extra = 1 + (roll(3) < Fixed::fromRatio(1, 2));
            for (int32_t k = 0; k < extra; ++k) {
                const Fixed r = roll(10 + k);
                UnitType t;
                if (s.mountain > Fixed::fromInt(15)) t = hills[(r * 3).floorToInt()];
                else if (g == Ground::Forest) t = woods[(r * 3).floorToInt()];
                else t = plains[(r * 4).floorToInt()];
                bool dup = false;
                for (const Option& o : st.options) dup = dup || o.type == t;
                if (!dup) st.options.push_back(trained(t));
            }
        }
        cities_.push_back(st);
    }
    // Every side: its capital, a hero and a war chest. Neutral towns: a small
    // garrison, a little stronger where the walls are.
    for (size_t i = 0; i < cities_.size(); ++i) {
        const CityState& st = cities_[i];
        const Continent::City& c = continent_.cities()[i];
        if (st.owner != kNeutral) {
            sides_[st.owner].gold = realm(st.owner).gold;
            Stack& s = newStack(st.owner, st.cx, st.cy);
            Army hero = makeArmy(T::Hero, "Lord of " + c.name);
            s.armies.push_back(hero);
            s.armies.push_back(makeArmy(T::LightInfantry, ""));
            continue;
        }
        Stack& g = newStack(kNeutral, st.cx, st.cy);
        g.garrison = true;
        const int32_t armies = st.defense - 2;
        for (int32_t k = 0; k < armies; ++k) {
            const UnitType t = k == 0 ? T::LightInfantry : k == 1 ? T::Archers : T::HeavyInfantry;
            g.armies.push_back(makeArmy(t, "Garrison of " + c.name));
        }
    }
}

Army Campaign::makeArmy(UnitType type, const std::string& name) {
    Army a;
    a.id = nextId_++;
    a.type = type;
    a.name = name;
    a.soldiers = a.fullStrength = squadSize(type);
    a.bornDay = day();
    return a;
}

Campaign::Stack& Campaign::newStack(uint8_t owner, int32_t cx, int32_t cy) {
    Stack s;
    s.id = nextId_++;
    s.owner = owner;
    s.cx = cx;
    s.cy = cy;
    stacks_.push_back(s);
    return stacks_.back();
}

const Campaign::Stack* Campaign::findStack(uint32_t id) const {
    for (const Stack& s : stacks_)
        if (s.id == id) return &s;
    return nullptr;
}

int32_t Campaign::cityAt(int32_t cx, int32_t cy) const {
    for (size_t i = 0; i < cities_.size(); ++i)
        if (cities_[i].cx == cx && cities_[i].cy == cy && !cities_[i].razed) return static_cast<int32_t>(i);
    return -1;
}

int32_t Campaign::citiesOwned(uint8_t side) const {
    int32_t n = 0;
    for (const CityState& c : cities_) n += c.owner == side && !c.razed;
    return n;
}

bool Campaign::produce(int32_t city, int32_t option) {
    if (city < 0 || city >= static_cast<int32_t>(cities_.size())) return false;
    CityState& c = cities_[static_cast<size_t>(city)];
    if (option >= static_cast<int32_t>(c.options.size())) return false;
    c.producing = option;
    c.progress = 0;
    return true;
}

int32_t Campaign::stackMoves(const Stack& s) const {
    int32_t mp = INT32_MAX;
    int32_t boots = 0;  // the best of the heroes' winged things
    for (const Army& a : s.armies) {
        mp = std::min(mp, movesPerDay(a.type));
        for (uint8_t k : a.artifacts) boots = std::max(boots, artifacts()[k].moves);
    }
    return mp == INT32_MAX ? 0 : mp + boots;
}

int32_t Campaign::stackCost(const Stack& s, int32_t cx, int32_t cy) const {
    // The dearest member sets the cost; a hero goes where the others can (on a griffin over the mountains).
    const Lay t = terrainAt(cx, cy);
    int32_t cost = 0;
    bool others = false;
    for (const Army& a : s.armies) others = others || !a.hero();
    for (const Army& a : s.armies) {
        if (a.hero() && others) continue;
        cost = std::max(cost, moveCost(a.type, t));
    }
    return cost;
}

bool Campaign::edgeOpen(int32_t ax, int32_t ay, int32_t bx, int32_t by) const {
    const auto river = [&](int32_t x, int32_t y, bool east) {
        return (east ? riverEast_ : riverNorth_)[static_cast<size_t>(y) * nx_ + x] != 0;
    };
    if (ay == by) return !river(std::min(ax, bx), ay, true);
    if (ax == bx) return !river(ax, std::min(ay, by), false);
    // Diagonals: open if either way round is.
    const bool viaX = !river(std::min(ax, bx), ay, true) && !river(bx, std::min(ay, by), false);
    const bool viaY = !river(ax, std::min(ay, by), false) && !river(std::min(ax, bx), by, true);
    return viaX || viaY;
}

bool Campaign::findPath(const Stack& s, int32_t tx, int32_t ty, std::vector<std::pair<int32_t, int32_t>>& out) const {
    out.clear();
    if (tx < 0 || ty < 0 || tx >= nx_ || ty >= ny_) return false;
    bool flier = !s.armies.empty();
    for (const Army& a : s.armies) flier = flier && (flies(a.type) || a.hero());
    bool anyFlier = false;
    for (const Army& a : s.armies) anyFlier = anyFlier || flies(a.type);
    flier = flier && anyFlier;
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
    using Node = std::pair<int32_t, int32_t>;  // (f, index)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const auto idx = [&](int32_t x, int32_t y) { return y * nx_ + x; };
    const int32_t start = idx(s.cx, s.cy), goal = idx(tx, ty);
    const auto h = [&](int32_t i) { return std::max(std::abs(i % nx_ - tx), std::abs(i / nx_ - ty)); };
    g[static_cast<size_t>(start)] = 0;
    open.push({h(start), start});
    while (!open.empty()) {
        const auto [f, cur] = open.top();
        open.pop();
        if (cur == goal) break;
        if (f - h(cur) > g[static_cast<size_t>(cur)]) continue;
        const int32_t cx = cur % nx_, cy = cur / nx_;
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int32_t x = cx + dx, y = cy + dy;
                if (x < 0 || y < 0 || x >= nx_ || y >= ny_) continue;
                if (!flier && !edgeOpen(cx, cy, x, y)) continue;
                int32_t cost = stackCost(s, x, y);
                if (cost >= kProhibited) continue;
                // Another side's stack blocks the way, except where the route ends (an attack).
                const int32_t ni = idx(x, y);
                if (ni != goal) {
                    const size_t k = static_cast<size_t>(ni);
                    if (foe[k] || ours[k] + mine > cellCapacity(x, y)) continue;
                }
                if (dx != 0 && dy != 0) cost = cost * 3 / 2;
                const int32_t ng = g[static_cast<size_t>(cur)] + cost;
                if (ng < g[static_cast<size_t>(ni)]) {
                    g[static_cast<size_t>(ni)] = ng;
                    parent[static_cast<size_t>(ni)] = cur;
                    open.push({ng + h(ni), ni});
                }
            }
        }
    }
    if (g[static_cast<size_t>(goal)] == INT32_MAX) return false;
    for (int32_t i = goal; i != start; i = parent[static_cast<size_t>(i)]) out.push_back({i % nx_, i / nx_});
    std::reverse(out.begin(), out.end());
    return true;
}

bool Campaign::move(uint32_t stackId, int32_t cx, int32_t cy) {
    for (Stack& s : stacks_) {
        if (s.id != stackId || s.garrison) continue;
        std::vector<std::pair<int32_t, int32_t>> path;
        if (!planRoute(s, cx, cy, path)) return false;  // by land, or by boat where that is quicker
        s.path = std::move(path);
        s.progress = 0;
        return true;
    }
    return false;
}

void Campaign::step() {
    if (pending_.stack) return;
    ++tick_;
    if (tick_ % kTicksPerDay == 0) newDay();
    moveStacks();
    bannerStep();  // campaign_capture.cpp
    if (tick_ % kTerritoryTicks == 0) territoryStep();  // campaign_territory.cpp
}

void Campaign::newDay() {
    seaDay();  // shipwrights and the boats' upkeep
    // Income, then upkeep; a side that cannot pay loses its newest armies.
    for (size_t s = 0; s < sides_.size(); ++s) {
        Side& side = sides_[s];
        side.gold += income(static_cast<uint8_t>(s)) - upkeepOf(static_cast<uint8_t>(s));
        while (side.gold < 0) {
            Stack* newest = nullptr;
            size_t which = 0;
            // The newest go first, green common troops before veterans and rare ones.
            for (Stack& st : stacks_) {
                if (st.owner != s) continue;
                for (size_t k = 0; k < st.armies.size(); ++k) {
                    const Army& a = st.armies[k];
                    if (a.hero() || a.ally) continue;
                    const bool later = !newest || precious(newest->armies[which]) > precious(a) ||
                                       (precious(newest->armies[which]) == precious(a) && a.bornDay >= newest->armies[which].bornDay);
                    if (later) {
                        newest = &st;
                        which = k;
                    }
                }
            }
            if (!newest) {
                side.gold = 0;
                break;
            }
            side.gold += upkeep(newest->armies[which].type);  // deserters take no pay
            newest->armies.erase(newest->armies.begin() + static_cast<long>(which));
            ++side.deserted;
        }
    }
    deliverTransits();
    questsNewDay();
    // Training: one army at a time per city, paid for when it starts.
    for (size_t i = 0; i < cities_.size(); ++i) {
        CityState& c = cities_[i];
        // An enemy raising its banner in the city: it trains nothing until they are gone.
        if (c.producing < 0 || c.owner == kNeutral || c.razed || c.banner != kNeutral) continue;
        const Option& o = c.options[static_cast<size_t>(c.producing)];
        const bool sent = c.vectorTo >= 0;
        // A full city (32 armies) trains no more until some march out.
        if (c.progress == 0 && !sent && armiesAt(c.owner, c.cx, c.cy) >= kMaxInCity) continue;
        if (c.progress == 0) {
            if (sides_[c.owner].gold < o.cost) continue;  // waits for the money
            sides_[c.owner].gold -= o.cost;
        }
        if (++c.progress < o.days) continue;
        c.progress = 0;
        const int32_t n = ++armiesRaised_[c.owner];
        const char* suffix = n % 10 == 1 && n % 100 != 11 ? "st" : n % 10 == 2 && n % 100 != 12 ? "nd" : n % 10 == 3 && n % 100 != 13 ? "rd" : "th";
        const bool levy = o.left > 0;
        Army army = levy ? makeArmy(o.type, continent_.cities()[i].name + " " + unitTypeName(o.type) + " (levy)")
                         : makeArmy(o.type, std::to_string(n) + suffix + " " + continent_.cities()[i].name + " " + unitTypeName(o.type));
        if (levy && --c.options[static_cast<size_t>(c.producing)].left == 0) {
            // The levy is spent: the option goes, and the city stops.
            c.options.erase(c.options.begin() + c.producing);
            c.producing = -1;
        }
        if (sent) {
            // LOC: it reaches the other city two days later.
            transits_.push_back({c.owner, static_cast<int32_t>(i), c.vectorTo, day() + 2, army});
            continue;
        }
        // Into a stack of its own side in the city, or a new one.
        Stack* home = nullptr;
        for (Stack& st : stacks_)
            if (st.owner == c.owner && st.cx == c.cx && st.cy == c.cy && st.armies.size() < kMaxStack && st.path.empty()) home = &st;
        if (!home) home = &newStack(c.owner, c.cx, c.cy);
        home->armies.push_back(army);
    }
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& s) { return s.armies.empty(); }), stacks_.end());
    for (size_t s = 0; s < sides_.size(); ++s)
        if (!sides_[s].human && sides_[s].alive) runAi(static_cast<uint8_t>(s));
    for (size_t s = 0; s < sides_.size(); ++s) {
        bool any = citiesOwned(static_cast<uint8_t>(s)) > 0;
        for (const Stack& st : stacks_) any = any || (st.owner == s && !st.armies.empty());
        sides_[s].alive = any;
    }
}

void Campaign::moveStacks() {
    for (size_t i = 0; i < stacks_.size(); ++i) {
        if (stacks_[i].path.empty() || stacks_[i].armies.empty()) continue;
        if (!boardIfSailing(i)) continue;  // stepping from a port onto the water boards a boat
        Stack& s = stacks_[i];
        s.progress += stepMoves(s);
        const auto [nx, ny] = s.path.front();
        // Into enemies or a city not ours: fight for it. An attack costs two
        // movement points, as in Warlords, whatever the ground.
        bool hostile = false;
        for (const Stack& o : stacks_) hostile = hostile || (o.owner != s.owner && o.cx == nx && o.cy == ny && !o.armies.empty());
        const int32_t city = cityAt(nx, ny);
        // (Not into a city where our own banner is going up: that is joining them.)
        const bool attack = hostile || (city >= 0 && cities_[static_cast<size_t>(city)].owner != s.owner &&
                                        cities_[static_cast<size_t>(city)].banner != s.owner);
        const int32_t cost = (attack ? 2 : stepCost(s, nx, ny)) * kTicksPerDay;
        if (s.progress < cost) continue;
        s.progress -= cost;
        if (!attack && s.path.size() > 1 && armiesAt(s.owner, nx, ny) + static_cast<int32_t>(s.armies.size()) > cellCapacity(nx, ny)) {
            // Our own armies fill the way (Warlords 9.2): halt here.
            s.path.clear();
            s.progress = 0;
            continue;
        }
        if (attack) {
            s.path.clear();
            s.progress = 0;
            if (sides_[s.owner].human) {
                // The player decides how this one is fought.
                pending_ = {s.id, nx, ny};
                return;
            }
            battle(stacks_[i], nx, ny);
            continue;
        }
        s.cx = nx;
        s.cy = ny;
        s.path.erase(s.path.begin());
        pickUp(s);
        if (s.path.empty()) arrive(s);
    }
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& s) { return s.armies.empty(); }), stacks_.end());
    syncBoats();
}

void Campaign::arrive(Stack& s) {
    // Join a friendly stack already here, as far as the limit of eight allows.
    for (Stack& o : stacks_) {
        if (&o == &s || o.owner != s.owner || o.cx != s.cx || o.cy != s.cy || !o.path.empty() || o.boat || s.boat) continue;
        while (!s.armies.empty() && o.armies.size() < kMaxStack) {
            o.armies.push_back(s.armies.back());
            s.armies.pop_back();
        }
    }
}

void Campaign::promote(Army& a) {
    // As in battle: kills per soldier make a soldier a regular, a veteran,
    // elite; a hero's kills raise their level.
    if (a.hero()) {
        static const int32_t kLevels[] = {3, 7, 12, 18, 25, 33, 42, 52};
        while (a.rank < 8 && a.kills >= kLevels[a.rank]) ++a.rank;
        return;
    }
    static const int32_t kRanks[] = {2, 5, 10};
    while (a.rank < 3 && a.kills >= kRanks[a.rank] * a.fullStrength) ++a.rank;
}

void Campaign::battle(Stack& attacker, int32_t cx, int32_t cy) {
    // Warlords' combat: each side's armies line up weakest first and fight
    // one on one; each round both roll a ten-sided die, and a roll no higher
    // than the opponent's strength is "low". If one rolls low and the other
    // high, the low roller takes a hit; two hits destroy an army.
    std::vector<Stack*> defenders;
    for (Stack& o : stacks_)
        if (o.owner != attacker.owner && o.cx == cx && o.cy == cy && !o.armies.empty()) defenders.push_back(&o);
    const int32_t city = cityAt(cx, cy);
    Fight fight;
    fight.attacker = attacker.owner;
    // Whoever stands there (a town's lord, or the enemy raising a banner in it).
    fight.defender = !defenders.empty() ? defenders.front()->owner : city >= 0 ? cities_[static_cast<size_t>(city)].owner : kNeutral;
    fight.city = city;
    fight.day = day();

    // Every modifier of the manual's appendix (AFCM, DFCM, the empire's and
    // the army type's liking for the ground), and the order they fight in.
    std::vector<Army*> att, def;
    for (Army& a : attacker.armies) att.push_back(&a);
    for (Stack* d : defenders)
        for (Army& a : d->armies) def.push_back(&a);
    std::vector<int32_t> asOf, dsOf;
    order(att, attacker.owner, cx, cy, false, asOf, 0, attacker.cx, attacker.cy);
    order(def, fight.defender, cx, cy, true, dsOf, engines(std::vector<const Army*>(att.begin(), att.end())), attacker.cx, attacker.cy);
    // Armies fighting from a boat's deck fight at a penalty (campaign_sea.cpp).
    int32_t defAfloat = 0;
    for (const Stack* d : defenders) defAfloat = std::max(defAfloat, afloatPenalty(*d));
    for (int32_t& p : asOf) p = std::clamp(p - 100 * afloatPenalty(attacker), 100, 900);
    for (int32_t& p : dsOf) p = std::clamp(p - 100 * defAfloat, 100, 900);
    for (Army* a : att) a->hits = 0;
    for (Army* a : def) a->hits = 0;
    size_t ai = 0, di = 0;
    while (ai < att.size() && di < def.size()) {
        Army& a = *att[ai];
        Army& d = *def[di];
        const int32_t as = asOf[ai], ds = dsOf[di];
        // Strengths are in hundredths (the small edges make them fractional), so
        // the die has a thousand faces: a roll of 1-1000 against 100-900.
        const bool aLow = static_cast<int32_t>(rng_.nextBelow(1000)) + 1 <= ds;
        const bool dLow = static_cast<int32_t>(rng_.nextBelow(1000)) + 1 <= as;
        if (aLow == dLow) continue;
        Army& hurt = aLow ? a : d;
        Army& striker = aLow ? d : a;
        if (++hurt.hits < 2) continue;
        striker.kills += hurt.soldiers;
        promote(striker);
        hurt.soldiers = 0;
        if (aLow) {
            ++ai;
            ++fight.attackerLost;
        } else {
            ++di;
            ++fight.defenderLost;
        }
    }
    fight.attackerWon = di >= def.size();
    // The fallen are gone; the wounded heal before the next fight.
    bury(attacker, cx, cy);
    for (Stack* d : defenders) bury(*d, cx, cy);
    for (Stack* d : defenders) pickUp(*d);  // the spoils of the fallen, to a hero holding the field
    // The city is taken, and its treasure: the loser loses a share of the war
    // chest in proportion to its cities, and the victor gets half.
    if (fight.attackerWon && !attacker.armies.empty()) capture(attacker, cx, cy, fight);
    fights_.push_back(fight);
}

Campaign::Stack* Campaign::stackById(uint32_t id) {
    for (Stack& s : stacks_)
        if (s.id == id) return &s;
    return nullptr;
}

std::vector<const Army*> Campaign::pendingAttackers() const {
    std::vector<const Army*> out;
    if (const Stack* s = findStack(pending_.stack))
        for (const Army& a : s->armies) out.push_back(&a);
    return out;
}

std::vector<const Army*> Campaign::pendingDefenders() const {
    std::vector<const Army*> out;
    const Stack* att = findStack(pending_.stack);
    if (!att) return out;
    for (const Stack& o : stacks_)
        if (o.owner != att->owner && o.cx == pending_.cx && o.cy == pending_.cy)
            for (const Army& a : o.armies) out.push_back(&a);
    return out;
}

void Campaign::resolvePending() {
    Stack* s = stackById(pending_.stack);
    const Pending p = pending_;
    pending_ = {};
    if (s) battle(*s, p.cx, p.cy);
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& st) { return st.armies.empty(); }), stacks_.end());
    syncBoats();
}

void Campaign::capture(Stack& attacker, int32_t cx, int32_t cy, Fight& fight) {
    // The field is won: the victors march in. A city is not yet theirs: their
    // banner must rise over it first (campaign_capture.cpp).
    (void)fight;
    attacker.cx = cx;
    attacker.cy = cy;
    pickUp(attacker);
    const int32_t city = cityAt(cx, cy);
    if (city < 0) return;
    CityState& c = cities_[static_cast<size_t>(city)];
    if (c.owner == attacker.owner || c.banner == attacker.owner) return;
    c.banner = attacker.owner;
    c.bannerProgress = 0;
    const std::string& name = continent_.cities()[static_cast<size_t>(city)].name;
    deed(attacker.owner, "Our banner goes up over " + name);
    if (c.owner != kNeutral) deed(c.owner, sides_[attacker.owner].name + " raise their banner over " + name + "!");
    bannerAlarm(city);  // its lord may send help at once (campaign_ai.cpp)
}

void Campaign::applyBattle(const std::vector<Outcome>& outcomes, bool attackerWon, bool bannerRaised) {
    // The 3D battle has been fought: every army that took part comes out with
    // its survivors, its new kills and its rank.
    Stack* att = stackById(pending_.stack);
    const Pending p = pending_;
    pending_ = {};
    if (!att) return;
    Fight fight;
    fight.attacker = att->owner;
    fight.city = cityAt(p.cx, p.cy);
    fight.day = day();
    for (Stack& st : stacks_) {
        if (st.cx == p.cx && st.cy == p.cy && st.owner != att->owner) fight.defender = st.owner;
        const bool inBattle = &st == att || (st.cx == p.cx && st.cy == p.cy && st.owner != att->owner);
        if (!inBattle) continue;
        for (Army& a : st.armies) {
            for (const Outcome& o : outcomes) {
                if (o.army != a.id) continue;
                const bool lost = o.soldiers <= 0;
                (&st == att ? fight.attackerLost : fight.defenderLost) += lost;
                a.soldiers = o.soldiers;
                a.kills += o.kills;
                a.rank = std::max(0, o.rank);
            }
        }
        bury(st, p.cx, p.cy);
    }
    if (fight.city >= 0 && fight.defender == 0 && cities_[static_cast<size_t>(fight.city)].owner != att->owner)
        fight.defender = cities_[static_cast<size_t>(fight.city)].owner;
    // Taken by the capture point in 3D: the banner rose in the battle, so the
    // defenders still standing fall back out of the town.
    if (attackerWon && bannerRaised && fight.city >= 0 && !att->armies.empty())
        for (Stack& st : stacks_)
            if (st.cx == p.cx && st.cy == p.cy && st.owner != att->owner && !st.armies.empty()) fallBack(st);
    bool defendersLeft = false;
    for (const Stack& st : stacks_)
        defendersLeft = defendersLeft || (st.cx == p.cx && st.cy == p.cy && st.owner != att->owner && !st.armies.empty());
    fight.attackerWon = attackerWon && !defendersLeft && !att->armies.empty();
    if (fight.attackerWon) capture(*att, p.cx, p.cy, fight);
    if (fight.attackerWon && bannerRaised && fight.city >= 0 && cities_[static_cast<size_t>(fight.city)].owner != att->owner) {
        transfer(fight.city, att->owner);
        fight.captured = true;
    }
    fights_.push_back(fight);
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& st) { return st.armies.empty(); }), stacks_.end());
    syncBoats();
}

uint64_t Campaign::checksum() const {
    StateHasher h;
    h.addU64(tick_);
    for (const Side& s : sides_) {
        h.addI64(s.gold);
        h.addU64(static_cast<uint64_t>(s.difficulty));
        h.addI64(s.deserted);
        h.addU64(s.alive);
    }
    for (const CityState& c : cities_) {
        h.addU64(c.owner);
        h.addI64(c.producing);
        h.addI64(c.progress);
        h.addI64(c.defense);
        h.addI64(c.vectorTo);
        h.addU64(c.port);
        h.addI64(c.boatDays);
        h.addU64(c.banner);
        h.addI64(c.bannerProgress);
        h.addU64(c.options.size());
        for (const Option& o : c.options) h.addI64(o.left);
    }
    for (const Transit& t : transits_) {
        h.addU64(t.owner);
        h.addI64(t.to);
        h.addU64(t.arriveDay);
        h.addU64(t.army.id);
    }
    h.addU64(rng_.state());
    h.addU64(aiRng_.state());
    for (const Stack& s : stacks_) {
        h.addU64(s.id);
        h.addU64(s.owner);
        h.addI64(s.cx);
        h.addI64(s.cy);
        h.addI64(s.progress);
        h.addU64(s.boat);
        for (const Army& a : s.armies) {
            h.addU64(a.id);
            h.addU64(static_cast<uint64_t>(a.type));
            h.addI64(a.soldiers);
            h.addI64(a.kills);
            h.addI64(a.rank);
            h.addI64(a.blessings);
            h.addU64(a.temples | (a.ally ? 0x100u : 0u));
            for (uint8_t k : a.artifacts) h.addU64(k);
        }
    }
    hashQuests(h);
    hashTerritory(h);
    for (const Boat& b : boats_) {
        h.addU64(b.id);
        h.addU64(b.owner);
        h.addI64(b.cx);
        h.addI64(b.cy);
        h.addI64(b.port);
        h.addU64(b.stack);
    }
    h.addI64(voyages_);
    for (Lay t : terrain_) h.addU64(static_cast<uint64_t>(t));
    for (uint8_t w : navigable_) h.addU64(w);
    return h.value();
}

}  // namespace illyria::sim
