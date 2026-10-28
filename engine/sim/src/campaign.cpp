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
// the gold it starts with, after the empires of Warlords.
struct Realm {
    std::vector<UnitType> specialties;
    int32_t gold;
};
const Realm& realm(uint8_t side) {
    static const Realm kRealms[] = {
        {{T::HeavyCavalry, T::HeavyInfantry, T::Pegasi}, 300},  // Aurelian Throne: knights
        {{T::Giants, T::HeavyInfantry}, 200},                    // Highcrag Giants
        {{T::Dwarves, T::Griffins, T::DwarfGrenadier}, 450},     // Ironhold Dwarves: rich
        {{T::WolfRiders, T::Minotaurs, T::Berserker}, 250},      // Orcs of Grath
        {{T::Archers, T::Pegasi, T::Unicorns}, 250},             // Sylvaran Elves
        {{T::LightCavalry, T::HeavyCavalry}, 300},               // Sunreach Riders
        {{T::Archers, T::Journeyman, T::Pikemen}, 300},          // Moonward Order
        {{T::Spiders, T::Minotaurs, T::Giants}, 350},            // Lord Malvoren
    };
    return kRealms[side % 8];
}

// Training time (days) and cost (gold), as in the Warlords city table.
Campaign::Option trained(UnitType t) {
    switch (t) {
        case T::LightInfantry: return {t, 1, 4};
        case T::HeavyInfantry: return {t, 2, 5};
        case T::Archers: return {t, 3, 4};
        case T::Pikemen: return {t, 2, 5};
        case T::LightCavalry: return {t, 3, 6};
        case T::HeavyCavalry: return {t, 4, 8};
        case T::Dwarves: return {t, 2, 4};
        case T::WolfRiders: return {t, 3, 8};
        case T::Giants: return {t, 4, 4};
        case T::Minotaurs: return {t, 4, 8};
        case T::Spiders: return {t, 3, 6};
        case T::Griffins: return {t, 6, 16};
        case T::Pegasi: return {t, 6, 16};
        case T::Elephants: return {t, 5, 12};
        case T::Unicorns: return {t, 5, 14};
        case T::DwarfGrenadier: return {t, 3, 8};
        case T::Journeyman: return {t, 3, 6};
        case T::Warrior: return {t, 3, 6};
        case T::Berserker: return {t, 3, 6};
        default: return {t, 6, 20};
    }
}

bool flies(UnitType t) {
    return t == T::Pegasi || t == T::Griffins || t == T::Dragons || t == T::Archons;
}
bool special(UnitType t) {
    return t == T::Wizards || t == T::Undead || t == T::Demons || t == T::Devils || t == T::Dragons;
}

}  // namespace

int32_t Campaign::strength(const Army& a) {
    return std::min(9, warlordsStrength(a.type) + a.rank + a.blessings);
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
    static const int32_t kDark[] = {1, 1, P, 4, 5, P, 2, 4, 1};
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
        case T::Undead:
        case T::Demons:
        case T::Devils:
        case T::Wizards: row = kDark; break;
        default:
            if (flies(t)) row = kFliers;
            break;
    }
    return row[static_cast<int>(terrain)];
}

int32_t Campaign::upkeep(UnitType t) {
    if (t == T::Hero) return 0;
    return (trained(t).cost + 1) / 2;
}

Campaign::Campaign(const Continent& continent, uint64_t seed) : continent_(continent), rng_(seed, 0xCA4A16) {
    nx_ = static_cast<int32_t>(Continent::kWidth / kCell);
    ny_ = static_cast<int32_t>(Continent::kHeight / kCell);
    initTerrain();
    for (const Continent::Faction& f : Continent::factions()) sides_.push_back({f.name, 0, true});
    armiesRaised_.assign(sides_.size(), 0);
    initCities();
}

void Campaign::initTerrain() {
    terrain_.assign(static_cast<size_t>(nx_) * ny_, Lay::Plain);
    riverEast_.assign(terrain_.size(), 0);
    riverNorth_.assign(terrain_.size(), 0);
    std::vector<uint8_t> nearRiver(terrain_.size(), 0);
    for (int32_t y = 0; y < ny_; ++y) {
        for (int32_t x = 0; x < nx_; ++x) {
            const Fixed px = Fixed::fromInt(centre(x)), py = Fixed::fromInt(centre(y));
            const Continent::Sample s = continent_.sample(px, py);
            Fixed surface;
            Lay t = Lay::Plain;
            Fixed deck;
            if (s.roadDist < Fixed::fromInt(kCell / 2)) {  // a road crosses the cell
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
            nearRiver[static_cast<size_t>(y) * nx_ + x] = s.riverDist < Fixed::fromInt(260);
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
            const Ground g = continent_.landGround(c.pos.x + Fixed::fromInt(300), c.pos.y, s.height, s.mountain, s.roadDist);
            const UnitType hills[] = {T::Dwarves, T::Giants, T::Griffins}, woods[] = {T::Archers, T::WolfRiders, T::Spiders},
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

int32_t Campaign::winner() const {
    for (size_t s = 0; s < sides_.size(); ++s)
        if (citiesOwned(static_cast<uint8_t>(s)) * 3 >= static_cast<int32_t>(cities_.size()) * 2) return static_cast<int32_t>(s) + 1;
    return 0;
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
    for (const Army& a : s.armies) mp = std::min(mp, movesPerDay(a.type));
    return mp == INT32_MAX ? 0 : mp;
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
                    bool blocked = false;
                    for (const Stack& o : stacks_) blocked = blocked || (o.owner != s.owner && o.cx == x && o.cy == y && !o.armies.empty());
                    if (blocked) continue;
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
        if (!findPath(s, cx, cy, path)) return false;
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
}

void Campaign::newDay() {
    // Income, then upkeep; a side that cannot pay loses its newest armies.
    for (size_t s = 0; s < sides_.size(); ++s) {
        Side& side = sides_[s];
        for (const CityState& c : cities_)
            if (c.owner == s && !c.razed) side.gold += c.income;
        for (const Stack& st : stacks_)
            if (st.owner == s)
                for (const Army& a : st.armies) side.gold -= upkeep(a.type);
        while (side.gold < 0) {
            Stack* newest = nullptr;
            size_t which = 0;
            for (Stack& st : stacks_) {
                if (st.owner != s) continue;
                for (size_t k = 0; k < st.armies.size(); ++k) {
                    if (st.armies[k].hero()) continue;
                    if (!newest || st.armies[k].bornDay >= newest->armies[which].bornDay) {
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
        }
    }
    // Training: one army at a time per city, paid for when it starts.
    for (size_t i = 0; i < cities_.size(); ++i) {
        CityState& c = cities_[i];
        if (c.producing < 0 || c.owner == kNeutral || c.razed) continue;
        const Option& o = c.options[static_cast<size_t>(c.producing)];
        if (c.progress == 0) {
            if (sides_[c.owner].gold < o.cost) continue;  // waits for the money
            sides_[c.owner].gold -= o.cost;
        }
        if (++c.progress < o.days) continue;
        c.progress = 0;
        // Into a stack of its own side in the city, or a new one.
        Stack* home = nullptr;
        for (Stack& st : stacks_)
            if (st.owner == c.owner && st.cx == c.cx && st.cy == c.cy && st.armies.size() < kMaxStack) home = &st;
        if (!home) home = &newStack(c.owner, c.cx, c.cy);
        const int32_t n = ++armiesRaised_[c.owner];
        const char* suffix = n % 10 == 1 && n % 100 != 11 ? "st" : n % 10 == 2 && n % 100 != 12 ? "nd" : n % 10 == 3 && n % 100 != 13 ? "rd" : "th";
        home->armies.push_back(makeArmy(o.type, std::to_string(n) + suffix + " " + continent_.cities()[i].name + " " + unitTypeName(o.type)));
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

void Campaign::runAi(uint8_t side) {
    // A plain Warlord, as Warlords advises: keep every city training its
    // quickest troops, and send every idle stack at the nearest city it does
    // not hold, neutral towns first, once it is strong enough to try.
    for (size_t i = 0; i < cities_.size(); ++i) {
        CityState& c = cities_[i];
        if (c.owner != side || c.razed || c.producing >= 0) continue;
        int32_t best = 0;
        for (size_t k = 1; k < c.options.size(); ++k)
            if (c.options[k].days < c.options[static_cast<size_t>(best)].days) best = static_cast<int32_t>(k);
        c.producing = best;
        c.progress = 0;
    }
    for (Stack& s : stacks_) {
        if (s.owner != side || s.garrison || !s.path.empty() || s.armies.empty()) continue;
        const int32_t here = cityAt(s.cx, s.cy);
        // Leave one army to hold a city; march with the rest once there are three.
        if (here >= 0 && s.armies.size() < 3) continue;
        int32_t power = 0;
        for (const Army& a : s.armies) power += strength(a);
        int32_t target = -1, best = INT32_MAX;
        for (size_t i = 0; i < cities_.size(); ++i) {
            const CityState& c = cities_[i];
            if (c.owner == side || c.razed) continue;
            int32_t guard = 0;
            for (const Stack& o : stacks_)
                if (o.cx == c.cx && o.cy == c.cy && o.owner != side)
                    for (const Army& a : o.armies) guard += strength(a);
            if (guard * 3 > power * 2 + c.defense) continue;  // too strong for us yet
            const int32_t d = std::max(std::abs(c.cx - s.cx), std::abs(c.cy - s.cy)) + (c.owner == kNeutral ? 0 : 6);
            if (d < best) {
                best = d;
                target = static_cast<int32_t>(i);
            }
        }
        if (target < 0) continue;
        std::vector<std::pair<int32_t, int32_t>> path;
        if (!findPath(s, cities_[static_cast<size_t>(target)].cx, cities_[static_cast<size_t>(target)].cy, path)) continue;
        if (here >= 0 && s.armies.size() > 1) {
            // Split off the strongest to march; the weakest stays behind on guard.
            Stack left;
            left.id = nextId_++;
            left.owner = side;
            left.cx = s.cx;
            left.cy = s.cy;
            size_t weakest = 0;
            for (size_t k = 1; k < s.armies.size(); ++k)
                if (!s.armies[k].hero() && (s.armies[weakest].hero() || strength(s.armies[k]) < strength(s.armies[weakest]))) weakest = k;
            left.armies.push_back(s.armies[weakest]);
            s.armies.erase(s.armies.begin() + static_cast<long>(weakest));
            s.path = std::move(path);
            s.progress = 0;
            stacks_.push_back(left);
            return;  // the stack list moved; the rest wait for tomorrow
        }
        s.path = std::move(path);
        s.progress = 0;
    }
}

void Campaign::moveStacks() {
    for (size_t i = 0; i < stacks_.size(); ++i) {
        if (stacks_[i].path.empty() || stacks_[i].armies.empty()) continue;
        Stack& s = stacks_[i];
        s.progress += stackMoves(s);
        const auto [nx, ny] = s.path.front();
        const int32_t cost = stackCost(s, nx, ny) * kTicksPerDay;
        if (s.progress < cost) continue;
        s.progress -= cost;
        // Into enemies or a city not ours: fight for it.
        bool hostile = false;
        for (const Stack& o : stacks_) hostile = hostile || (o.owner != s.owner && o.cx == nx && o.cy == ny && !o.armies.empty());
        const int32_t city = cityAt(nx, ny);
        if (hostile || (city >= 0 && cities_[static_cast<size_t>(city)].owner != s.owner)) {
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
        if (s.path.empty()) arrive(s);
    }
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& s) { return s.armies.empty(); }), stacks_.end());
}

void Campaign::arrive(Stack& s) {
    // Join a friendly stack already here, as far as the limit of eight allows.
    for (Stack& o : stacks_) {
        if (&o == &s || o.owner != s.owner || o.cx != s.cx || o.cy != s.cy || !o.path.empty()) continue;
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
    fight.defender = city >= 0 ? cities_[static_cast<size_t>(city)].owner : defenders.empty() ? kNeutral : defenders.front()->owner;
    fight.city = city;
    fight.day = day();

    // The modifiers every army of a side adds (Warlords' AFCM and DFCM).
    const auto modifier = [&](const std::vector<Army*>& side, bool defending) {
        int32_t m = 0, hero = 0;
        bool flying = false, magic = false;
        for (const Army* a : side) {
            if (a->hero()) hero = std::max(hero, std::min(9, 4 + a->rank + a->blessings));
            flying = flying || flies(a->type);
            magic = magic || special(a->type);
        }
        m += hero >= 9 ? 3 : hero >= 7 ? 2 : hero >= 4 ? 1 : 0;
        m += flying + magic;
        if (defending && city >= 0) {
            const int32_t d = cities_[static_cast<size_t>(city)].defense;
            m += d >= 9 ? 3 : d >= 7 ? 2 : d >= 2 ? 1 : 0;
        }
        return m;
    };
    std::vector<Army*> att, def;
    for (Army& a : attacker.armies) att.push_back(&a);
    for (Stack* d : defenders)
        for (Army& a : d->armies) def.push_back(&a);
    const int32_t am = modifier(att, false), dm = modifier(def, true);
    const auto byValue = [](const Army* a, const Army* b) {
        const int32_t sa = strength(*a) + (a->hero() ? 10 : 0), sb = strength(*b) + (b->hero() ? 10 : 0);
        return sa != sb ? sa < sb : a->id < b->id;
    };
    std::stable_sort(att.begin(), att.end(), byValue);
    std::stable_sort(def.begin(), def.end(), byValue);
    for (Army* a : att) a->hits = 0;
    for (Army* a : def) a->hits = 0;
    size_t ai = 0, di = 0;
    while (ai < att.size() && di < def.size()) {
        Army& a = *att[ai];
        Army& d = *def[di];
        const int32_t as = std::min(9, strength(a) + am), ds = std::min(9, strength(d) + dm);
        const bool aLow = static_cast<int32_t>(rng_.nextBelow(10)) + 1 <= ds;
        const bool dLow = static_cast<int32_t>(rng_.nextBelow(10)) + 1 <= as;
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
    const auto bury = [](std::vector<Army>& armies) {
        armies.erase(std::remove_if(armies.begin(), armies.end(), [](const Army& a) { return a.soldiers <= 0; }), armies.end());
        for (Army& a : armies) a.hits = 0;
    };
    bury(attacker.armies);
    for (Stack* d : defenders) bury(d->armies);
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
}

void Campaign::capture(Stack& attacker, int32_t cx, int32_t cy, Fight& fight) {
    const int32_t city = cityAt(cx, cy);
    if (city >= 0) {
        CityState& c = cities_[static_cast<size_t>(city)];
        const uint8_t loser = c.owner;
        if (loser != kNeutral) {
            const int32_t owned = std::max(1, citiesOwned(loser));
            const int32_t lost = sides_[loser].gold / owned;
            sides_[loser].gold -= lost;
            sides_[attacker.owner].gold += lost / 2;
        }
        c.owner = attacker.owner;
        c.producing = -1;
        c.progress = 0;
        fight.captured = true;
    }
    attacker.cx = cx;
    attacker.cy = cy;
}

void Campaign::applyBattle(const std::vector<Outcome>& outcomes, bool attackerWon) {
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
        st.armies.erase(std::remove_if(st.armies.begin(), st.armies.end(), [](const Army& a) { return a.soldiers <= 0; }), st.armies.end());
    }
    if (fight.city >= 0 && fight.defender == 0 && cities_[static_cast<size_t>(fight.city)].owner != att->owner)
        fight.defender = cities_[static_cast<size_t>(fight.city)].owner;
    bool defendersLeft = false;
    for (const Stack& st : stacks_)
        defendersLeft = defendersLeft || (st.cx == p.cx && st.cy == p.cy && st.owner != att->owner && !st.armies.empty());
    fight.attackerWon = attackerWon && !defendersLeft && !att->armies.empty();
    if (fight.attackerWon) capture(*att, p.cx, p.cy, fight);
    fights_.push_back(fight);
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& st) { return st.armies.empty(); }), stacks_.end());
}

uint64_t Campaign::checksum() const {
    StateHasher h;
    h.addU64(tick_);
    for (const Side& s : sides_) h.addI64(s.gold);
    for (const CityState& c : cities_) {
        h.addU64(c.owner);
        h.addI64(c.producing);
        h.addI64(c.progress);
    }
    for (const Stack& s : stacks_) {
        h.addU64(s.id);
        h.addU64(s.owner);
        h.addI64(s.cx);
        h.addI64(s.cy);
        h.addI64(s.progress);
        for (const Army& a : s.armies) {
            h.addU64(a.id);
            h.addU64(static_cast<uint64_t>(a.type));
            h.addI64(a.soldiers);
            h.addI64(a.kills);
            h.addI64(a.rank);
        }
    }
    for (Lay t : terrain_) h.addU64(static_cast<uint64_t>(t));
    return h.value();
}

}  // namespace illyria::sim
