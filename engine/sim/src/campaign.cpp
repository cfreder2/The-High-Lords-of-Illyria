#include "illyria/sim/campaign.h"

#include <algorithm>
#include <climits>

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
        {{T::DwarfGrenadier, T::HeavyInfantry, T::Catapult}, 450},  // Ironhold Dwarves: rich, and engineers
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
        case T::DwarfGrenadier: return {t, 4, 24};  // where the retired legion was trained
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

Campaign::Campaign(const Continent& continent, uint64_t seed) : continent_(continent), seed_(seed), rng_(seed, 0xCA4A16), aiRng_(seed, 0xA1) {
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
    roadCell_.assign(terrain_.size(), 0);
    riverEast_.assign(terrain_.size(), 0);
    riverNorth_.assign(terrain_.size(), 0);
    std::vector<uint8_t> nearRiver(terrain_.size(), 0);
    cellHeight_.assign(terrain_.size(), 0);
    // Road cells: every cell a road passes through, walked every 4 m (the
    // samples' road distance only reaches 60 m, short of a cell's corners;
    // and a road may clip a cell's corner for a few metres).
    std::vector<uint8_t> road(terrain_.size(), 0);
    for (const Polyline& r : continent_.roads()) {
        for (size_t k = 0; k + 1 < r.points.size(); ++k) {
            const Vec3 a = r.points[k], d = r.points[k + 1] - r.points[k];
            const int32_t steps = std::max(1, static_cast<int32_t>((d.flat().length() / 4).floorToInt()));
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
                    case Ground::Rock: t = s.mountain > Continent::peakHeight(90) ? Lay::Mountain : Lay::Hill; break;
                    default: t = s.mountain > Continent::peakHeight(25) ? Lay::Hill : Lay::Plain; break;
                }
            }
            terrain_[static_cast<size_t>(y) * nx_ + x] = t;
            roadCell_[static_cast<size_t>(y) * nx_ + x] = road[static_cast<size_t>(y) * nx_ + x] && !open;  // kept under a town's cell
            nearRiver[static_cast<size_t>(y) * nx_ + x] = s.riverDist < Fixed::fromInt(58);
            cellHeight_[static_cast<size_t>(y) * nx_ + x] = static_cast<int16_t>(std::clamp<int64_t>(s.height.floorToInt(), -2000, 4000));
        }
    }
    // Land in the lakes too narrow for its cells' middles to fall on (the
    // Spit out to Abbey Isle, the only way there on foot): every cell its
    // middle line crosses dry is land.
    for (const Vec3& p : continent_.isleWalk()) {
        const int32_t x = cellOf(static_cast<int32_t>(p.x.floorToInt())), y = cellOf(static_cast<int32_t>(p.y.floorToInt()));
        if (x >= 0 && y >= 0 && x < nx_ && y < ny_ && terrain_[static_cast<size_t>(y) * nx_ + x] == Lay::Water) terrain_[static_cast<size_t>(y) * nx_ + x] = Lay::Plain;
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
            const Ground g = continent_.landGround(c.pos.x + Fixed::fromInt(67), c.pos.y, s.height, s.mountain, s.roadDist);
            const UnitType hills[] = {T::DwarfGrenadier, T::HeavyInfantry, T::Pikemen}, woods[] = {T::Archers, T::WolfRiders, T::Warrior},
                           plains[] = {T::HeavyInfantry, T::LightCavalry, T::Pikemen, T::HeavyCavalry};
            const int32_t extra = 1 + (roll(3) < Fixed::fromRatio(1, 2));
            for (int32_t k = 0; k < extra; ++k) {
                const Fixed r = roll(10 + k);
                UnitType t;
                if (s.mountain > Continent::peakHeight(15)) t = hills[(r * 3).floorToInt()];
                else if (g == Ground::Forest) t = woods[(r * 3).floorToInt()];
                else t = plains[(r * 4).floorToInt()];
                bool dup = false;
                for (const Option& o : st.options) dup = dup || o.type == t;
                if (!dup) st.options.push_back(trained(t));
            }
        }
        cities_.push_back(st);
    }
    cityCell_.assign(static_cast<size_t>(nx_) * ny_, -1);  // which city a cell holds, for cityAt
    for (size_t i = 0; i < cities_.size(); ++i) cityCell_[static_cast<size_t>(cities_[i].cy) * nx_ + cities_[i].cx] = static_cast<int16_t>(i);
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
    a.men.resize(static_cast<size_t>(a.soldiers));
    for (Man& m : a.men) m.id = nextId_++;
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
    if (cx < 0 || cy < 0 || cx >= nx_ || cy >= ny_) return -1;
    const int32_t i = cityCell_[static_cast<size_t>(cy) * nx_ + cx];
    return i >= 0 && !cities_[static_cast<size_t>(i)].razed ? i : -1;
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

bool Campaign::move(uint32_t stackId, int32_t cx, int32_t cy) {
    for (Stack& s : stacks_) {
        if (s.id != stackId || s.garrison) continue;
        std::vector<std::pair<int32_t, int32_t>> path;
        if (!planRoute(s, cx, cy, path)) return false;  // by land, or by boat where that is quicker
        // Still bound for the same next cell: the way already made there counts.
        if (s.path.empty() || path.empty() || s.path.front() != path.front()) s.progress = 0;
        s.path = std::move(path);
        return true;
    }
    return false;
}

void Campaign::step() {
    ++tick_;
    if (tick_ % kTicksPerDay == 0) newDay();
    if (tick_ % kContactTicks == 0) contactStep();  // campaign_world.cpp: who is near an enemy wakes, who is not goes to sleep
    moveStacks();
    stepWorld();  // the men who are awake, as in any battle
    if (tick_ % kContactTicks == 0) bannerStep();  // campaign_capture.cpp
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
            settle(*newest);
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
        settle(*home);
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
        if (stacks_[i].path.empty() || stacks_[i].armies.empty() || stacks_[i].awake) continue;  // (the awake march with their men: stepWorld)
        if (!boardIfSailing(i)) continue;  // stepping from a port onto the water boards a boat
        Stack& s = stacks_[i];
        // A tick on the way, at the pace of the slowest army over this ground
        // (its armies may have changed since it set out: the time is taken now).
        s.progress += kTimeScale;
        const auto [nx, ny] = s.path.front();
        const int32_t cost = stepTime(s, s.cx, s.cy, nx, ny);
        if (cost == kClosed) {
            // The way is closed to it now (a walker joined fliers): it halts.
            s.path.clear();
            s.progress = 0;
            continue;
        }
        // It faces the way it goes, turning gradually.
        {
            int32_t wx = (nx - s.cx) * 1000, wy = (ny - s.cy) * 1000;
            if (wx && wy) wx = wx * 707 / 1000, wy = wy * 707 / 1000;
            s.hx += (wx - s.hx) / 12;
            s.hy += (wy - s.hy) / 12;
            const int64_t len = static_cast<int64_t>(std::sqrt(static_cast<double>(int64_t{s.hx} * s.hx + int64_t{s.hy} * s.hy)));
            if (len > 0) s.hx = static_cast<int32_t>(int64_t{s.hx} * 1000 / len), s.hy = static_cast<int32_t>(int64_t{s.hy} * 1000 / len);
        }
        if (s.progress < cost) continue;
        s.progress -= cost;
        // (A hostile stack or town in the next cell is not met here: the men
        // meet whoever is near in the world, long before they are in it.)
        s.cx = nx;
        s.cy = ny;
        s.path.erase(s.path.begin());
        pickUp(s);
        if (s.path.empty()) {
            s.progress = 0;
            arrive(s);
        }
    }
    stacks_.erase(std::remove_if(stacks_.begin(), stacks_.end(), [](const Stack& s) { return s.armies.empty(); }), stacks_.end());
    syncBoats();
}

void Campaign::arrive(Stack& s) {
    // Join a friendly stack already standing here, as a group of eight at most
    // (a group is what marches together; any number may stand in a place).
    for (Stack& o : stacks_) {
        if (&o == &s || o.owner != s.owner || o.cx != s.cx || o.cy != s.cy || !o.path.empty() || o.boat || s.boat || o.awake) continue;
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

Campaign::Stack* Campaign::stackById(uint32_t id) {
    for (Stack& s : stacks_)
        if (s.id == id) return &s;
    return nullptr;
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
        h.addU64(s.awake);
        h.addI64(s.hx);
        h.addI64(s.hy);
        if (s.awake) {
            h.addI64(s.px);
            h.addI64(s.py);
        }
        for (const Army& a : s.armies) {
            h.addU64(a.id);
            h.addU64(static_cast<uint64_t>(a.type));
            h.addI64(a.soldiers);
            h.addI64(a.kills);
            h.addI64(a.rank);
            h.addI64(a.blessings);
            h.addU64(a.temples | (a.ally ? 0x100u : 0u));
            for (uint8_t k : a.artifacts) h.addU64(k);
            for (const Man& m : a.men) {
                h.addU64(m.id);
                h.addI64(m.hp);
                h.addI64(m.kills);
                h.addI64(m.rank);
                h.addU64(m.placed);
                if (m.placed) {
                    h.addI64(m.ox);
                    h.addI64(m.oy);
                }
            }
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
    {
        std::vector<int32_t> gates;
        gateHps(gates);
        for (int32_t g : gates) h.addI64(g);
        if (awakeStacks_ > 0 && world_) h.addU64(world_->checksum());  // (between fights the world is only its gates)
    }
    for (Lay t : terrain_) h.addU64(static_cast<uint64_t>(t));
    for (uint8_t w : navigable_) h.addU64(w);
    return h.value();
}

}  // namespace illyria::sim
