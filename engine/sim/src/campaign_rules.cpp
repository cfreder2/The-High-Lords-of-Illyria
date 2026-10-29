// The rules of the Warlords manual the campaign follows beyond moving and
// training: the combat modifiers of its appendix, the exact odds of its dice,
// LOC, BUILD and DISBAND, the gold, and how a war is won (docs/GAME_DESIGN.md
// §4.2.5 has the table). Integers only.
#include <algorithm>
#include <climits>
#include <queue>

#include "illyria/sim/campaign.h"

namespace illyria::sim {

namespace {

using T = UnitType;

// A catapult's strength against a town's walls, over its strength in the field.
constexpr int32_t kCatapultSiegeBonus = 2;

// How each empire likes the ground it fights on (the appendix's first
// terrain table), by side: road, water, shore, forest, hill, plain, marsh.
// Ours follow the Warlords archetypes: knights, giants, dwarves, orcs, elves,
// horse lords, the sea-minded order (Selentines) and the dark lord (Bane).
int32_t empireTerrain(uint8_t owner, Lay lay) {
    static const int8_t kEmpires[8][7] = {
        {0, 0, 0, 0, 0, 0, 0},     // Sirians: they don't mind where they fight
        {0, 0, 0, 0, 1, 0, -1},    // Storm Giants
        {0, 0, 0, -1, 2, 0, -1},   // Grey Dwarves
        {0, 0, 0, -1, 0, 0, 1},    // Orcs of Kor
        {0, 0, 0, 1, -1, 0, -1},   // Elvallie
        {1, 0, 0, -1, -1, 1, 0},   // Horse Lords
        {0, 1, 1, 0, 0, 0, 0},     // Selentines
        {0, 0, 0, -1, 0, 0, 1},    // Lord Bane
    };
    if (owner >= 8) return 0;  // neutral garrisons have no likes
    int col;
    switch (lay) {
        case Lay::Road:
        case Lay::Bridge: col = 0; break;
        case Lay::Water: col = 1; break;
        case Lay::Forest: col = 3; break;
        case Lay::Hill: col = 4; break;
        case Lay::Plain: col = 5; break;
        case Lay::Marsh: col = 6; break;
        default: return 0;  // mountains and cities: no effect
    }
    return kEmpires[owner][col];
}

// How each army type likes forest, hill and marsh (the second table).
int32_t typeTerrain(UnitType t, Lay lay) {
    const int col = lay == Lay::Forest ? 0 : lay == Lay::Hill ? 1 : lay == Lay::Marsh ? 2 : -1;
    if (col < 0) return 0;
    static const int8_t kGiants[] = {-1, 0, 0}, kDwarves[] = {-1, 1, 0}, kHeavy[] = {-1, -1, -1}, kLight[] = {-1, -1, 0},
                        kElves[] = {1, -1, -1}, kCavalry[] = {-1, -1, -1}, kWolves[] = {-1, -1, 0}, kPegasi[] = {1, 0, 0},
                        kGriffins[] = {0, 1, 0}, kUndead[] = {-1, 0, 1}, kDemons[] = {-1, 0, 0}, kNone[] = {0, 0, 0};
    const int8_t* row = kNone;
    switch (t) {
        case T::Giants:
        case T::Minotaurs: row = kGiants; break;
        case T::Dwarves:
        case T::DwarfGrenadier: row = kDwarves; break;
        case T::HeavyInfantry:
        case T::Pikemen:
        case T::Warrior: row = kHeavy; break;
        case T::LightInfantry:
        case T::Journeyman:
        case T::Berserker: row = kLight; break;
        case T::Archers: row = kElves; break;
        case T::LightCavalry:
        case T::HeavyCavalry:
        case T::Elephants:
        case T::Unicorns: row = kCavalry; break;
        case T::WolfRiders:
        case T::Spiders: row = kWolves; break;
        case T::Pegasi: row = kPegasi; break;
        case T::Griffins: row = kGriffins; break;
        case T::Undead: row = kUndead; break;
        case T::Demons:
        case T::Devils: row = kDemons; break;
        default: break;  // dragons, wizards, archons, heroes: no effect
    }
    return row[col];
}

}  // namespace

bool Campaign::flies(UnitType t) {
    return t == T::Pegasi || t == T::Griffins || t == T::Dragons || t == T::Archons;
}
bool Campaign::special(UnitType t) {
    return t == T::Wizards || t == T::Undead || t == T::Demons || t == T::Devils || t == T::Dragons;
}

int32_t Campaign::engines(const std::vector<const Army*>& armies) {
    int32_t n = 0;
    for (const Army* a : armies) n += a->type == T::Catapult;
    return n;
}

int32_t Campaign::forceModifier(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending, int32_t engines) const {
    // The whole points of the AFCM (or DFCM): the heroes' command artifacts,
    // one for fliers, one for special armies (a dragon is both), and the
    // city's walls for a defender. The manual's smaller things (a hero's
    // leadership, the ground, holding a temple) are the edges (armyEdges):
    // a few percent each, not a whole point.
    (void)owner;
    int32_t items = 0;
    bool flying = false, magic = false;
    for (const Army* a : armies) {
        items += command(*a);  // the heroes' command artifacts
        flying = flying || flies(a->type);
        magic = magic || special(a->type);
    }
    int32_t m = items + flying + magic;
    const int32_t city = cityAt(cx, cy);
    if (defending && city >= 0) {
        const int32_t d = cities_[static_cast<size_t>(city)].defense;
        const int32_t walls = d >= 9 ? 3 : d >= 7 ? 2 : d >= 2 ? 1 : 0;
        m += walls - std::min(walls, engines);  // each siege engine breaks a point of the walls
    }
    return m;
}

Edges Campaign::armyEdges(const Army& a, const std::vector<const Army*>& side, uint8_t owner, int32_t cx, int32_t cy, bool defending,
                          int32_t oppX, int32_t oppY) const {
    Edges e;
    const Lay lay = terrainAt(cx, cy);
    if (owner < kNeutral && landOwner(cx, cy) == owner) e.home = kEdgeHomePct;
    // The type's liking for the ground (the manual's second table, its
    // pluses only) and the empire's (the first): 4% and 2% a point.
    const int32_t likes = std::max(0, typeTerrain(a.type, lay)) * kEdgeGroundPct + std::max(0, empireTerrain(owner, lay)) * 2;
    e.ground = std::min(kEdgeGroundMaxPct, likes);
    if (!a.hero()) e.seniority = kEdgeRankPct * std::clamp(a.rank, 0, 3);
    int32_t hero = 0;
    for (const Army* o : side)
        if (o->hero()) hero = std::max(hero, strength(*o));
    e.hero = heroLeadPct(hero);
    // The high ground: ten metres or more above the other side's cell
    // (defenders above the approach, or attackers coming down on them).
    if (oppX >= 0 && oppY >= 0 && oppX < nx_ && oppY < ny_) {
        const int32_t above = defending ? cellHeight(cx, cy) - cellHeight(oppX, oppY) : cellHeight(oppX, oppY) - cellHeight(cx, cy);
        if (above >= 10) e.height = kEdgeHeightPct;
    }
    // Holding a temple, a ruin's stones or an outpost tower (the manual's +1 and +2).
    if (defending && cityAt(cx, cy) < 0 && (siteAt(cx, cy) >= 0 || hexes_[static_cast<size_t>(hexAt(cx, cy))].outpost >= 0)) e.holding = kEdgeHoldPct;
    return e;
}

Edges Campaign::stackEdges(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending, int32_t oppX,
                           int32_t oppY) const {
    Edges best;
    for (const Army* a : armies) {
        const Edges e = armyEdges(*a, armies, owner, cx, cy, defending, oppX, oppY);
        best.home = std::max(best.home, e.home);
        best.ground = std::max(best.ground, e.ground);
        best.seniority = std::max(best.seniority, e.seniority);
        best.hero = std::max(best.hero, e.hero);
        best.height = std::max(best.height, e.height);
        best.holding = std::max(best.holding, e.holding);
    }
    return best;
}

void Campaign::order(std::vector<Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending, std::vector<int32_t>& power,
                     int32_t engines, int32_t oppX, int32_t oppY) const {
    const std::vector<const Army*> view(armies.begin(), armies.end());
    const int32_t fcm = forceModifier(view, owner, cx, cy, defending, engines);
    const bool siege = !defending && cityAt(cx, cy) >= 0;  // catapults hurl their stones at walls
    // AS (DS), in hundredths: (strength + force modifier) x (1 + the edges),
    // never past 9 (and never below 1); a hero's battle artifacts are in his strength.
    const auto as = [&](const Army* a) {
        const int32_t engine = siege && a->type == T::Catapult ? kCatapultSiegeBonus : 0;
        const int32_t edge = armyEdges(*a, view, owner, cx, cy, defending, oppX, oppY).total();
        return std::clamp((strength(*a) + fcm + engine) * (100 + edge), 100, 900);
    };
    // Lined up with the most valuable on the right: heroes last, then the strongest.
    std::stable_sort(armies.begin(), armies.end(), [&](const Army* a, const Army* b) {
        if (a->hero() != b->hero()) return b->hero();
        const int32_t sa = as(a), sb = as(b);
        return sa != sb ? sa < sb : a->id < b->id;
    });
    power.clear();
    for (const Army* a : armies) power.push_back(as(a));
}

std::vector<int32_t> Campaign::lineup(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy, bool defending,
                                      int32_t engines, int32_t oppX, int32_t oppY) const {
    std::vector<Army*> list;
    for (const Army* a : armies) list.push_back(const_cast<Army*>(a));  // order() only reorders the pointers
    std::vector<int32_t> power;
    order(list, owner, cx, cy, defending, power, engines, oppX, oppY);
    return power;
}

int32_t Campaign::winChance(const std::vector<int32_t>& attack, const std::vector<int32_t>& defence) {
    // Each round both roll a d10 (a d1000 against strengths in hundredths); a roll
    // no higher than the other's strength is low; one low and one high puts a
    // hit on the low roller, two hits destroy. So the next hit lands on the
    // attacker with chance ds(1000-as) / (ds(1000-as) + as(1000-ds)). Walk every state of the battle
    // (who is fighting, and their hits) in order, carrying the probabilities.
    const size_t n = attack.size(), m = defence.size();
    if (m == 0) return 1000;
    if (n == 0) return 0;
    constexpr int64_t kOne = int64_t(1) << 28;
    // p[i][j][ha][hd]: attacker i against defender j with those hits.
    std::vector<int64_t> p(n * m * 4, 0);
    const auto at = [&](size_t i, size_t j, int ha, int hd) -> int64_t& { return p[((i * m + j) * 2 + static_cast<size_t>(ha)) * 2 + static_cast<size_t>(hd)]; };
    at(0, 0, 0, 0) = kOne;
    int64_t won = 0;
    for (size_t s = 0; s < n + m - 1; ++s) {
        for (size_t i = 0; i < n && i <= s; ++i) {
            const size_t j = s - i;
            if (j >= m) continue;
            const int64_t as = attack[i], ds = defence[j];
            const int64_t pa = ds * (1000 - as), pd = as * (1000 - ds);
            const int64_t qa = pa + pd > 0 ? pa * kOne / (pa + pd) : kOne / 2;  // the attacker takes the hit
            for (int t = 0; t <= 2; ++t) {
                for (int ha = 0; ha <= 1; ++ha) {
                    const int hd = t - ha;
                    if (hd < 0 || hd > 1) continue;
                    const int64_t here = at(i, j, ha, hd);
                    if (here == 0) continue;
                    const int64_t toA = here * qa / kOne, toD = here - toA;
                    if (ha == 1) {
                        if (i + 1 < n) at(i + 1, j, 0, hd) += toA;  // the attacker falls; the next steps up
                    } else {
                        at(i, j, 1, hd) += toA;
                    }
                    if (hd == 1) {
                        if (j + 1 < m) at(i, j + 1, ha, 0) += toD;
                        else won += toD;  // the last defender falls
                    } else {
                        at(i, j, ha, 1) += toD;
                    }
                }
            }
        }
    }
    return static_cast<int32_t>(won * 1000 / kOne);
}

std::vector<const Army*> Campaign::defendersAt(uint8_t attacker, int32_t cx, int32_t cy) const {
    std::vector<const Army*> out;
    for (const Stack& o : stacks_)
        if (o.owner != attacker && o.cx == cx && o.cy == cy)
            for (const Army& a : o.armies) out.push_back(&a);
    return out;
}

int32_t Campaign::attackOdds(const std::vector<const Army*>& armies, uint8_t owner, int32_t cx, int32_t cy) const {
    const std::vector<const Army*> def = defendersAt(owner, cx, cy);
    if (def.empty()) return 1000;
    const int32_t city = cityAt(cx, cy);
    // Whoever stands there: the town's lord, or an enemy raising its banner in it (as Campaign::battle).
    uint8_t defOwner = city >= 0 ? cities_[static_cast<size_t>(city)].owner : kNeutral;
    for (const Stack& o : stacks_)
        if (o.owner != owner && o.cx == cx && o.cy == cy && !o.armies.empty()) defOwner = o.owner;
    return winChance(lineup(armies, owner, cx, cy, false), lineup(def, defOwner, cx, cy, true, engines(armies)));
}

int32_t Campaign::fortifyCost(int32_t city) const {
    if (city < 0 || city >= static_cast<int32_t>(cities_.size())) return 0;
    return 25 * (cities_[static_cast<size_t>(city)].defense + 1);
}

bool Campaign::fortify(int32_t city) {
    if (city < 0 || city >= static_cast<int32_t>(cities_.size())) return false;
    CityState& c = cities_[static_cast<size_t>(city)];
    if (c.owner == kNeutral || c.razed || c.defense >= 9) return false;
    const int32_t cost = fortifyCost(city);
    if (sides_[c.owner].gold < cost) return false;
    sides_[c.owner].gold -= cost;
    ++c.defense;
    return true;
}

bool Campaign::disband(uint32_t stackId, uint32_t armyId) {
    for (size_t k = 0; k < stacks_.size(); ++k) {
        Stack& s = stacks_[k];
        if (s.id != stackId || s.garrison) continue;
        for (size_t i = 0; i < s.armies.size(); ++i) {
            if (s.armies[i].id != armyId || s.armies[i].hero()) continue;
            s.armies.erase(s.armies.begin() + static_cast<long>(i));
            if (s.armies.empty() && pending_.stack != s.id) stacks_.erase(stacks_.begin() + static_cast<long>(k));
            return true;
        }
    }
    return false;
}

bool Campaign::vectorProduction(int32_t city, int32_t to) {
    if (city < 0 || city >= static_cast<int32_t>(cities_.size())) return false;
    CityState& c = cities_[static_cast<size_t>(city)];
    if (to < 0 || to == city) {
        c.vectorTo = -1;
        return true;
    }
    if (to >= static_cast<int32_t>(cities_.size()) || cities_[static_cast<size_t>(to)].owner != c.owner || c.owner == kNeutral) return false;
    int32_t senders = 0;
    for (const CityState& o : cities_) senders += o.vectorTo == to && &o != &c;
    if (senders >= 4) return false;  // at most four cities may send to one
    c.vectorTo = to;
    return true;
}

void Campaign::deliverTransits() {
    std::vector<Transit> still;
    for (Transit& t : transits_) {
        if (t.arriveDay > day()) {
            still.push_back(t);
            continue;
        }
        // To the city it was sent to, if we still hold it and it has room;
        // else back home; with neither, it is lost on the road.
        int32_t where = -1;
        for (const int32_t c : {t.to, t.from}) {
            const CityState& cs = cities_[static_cast<size_t>(c)];
            if (where < 0 && cs.owner == t.owner && !cs.razed && cs.banner == kNeutral && armiesAt(t.owner, cs.cx, cs.cy) < kMaxInCity) where = c;
        }
        if (where < 0) continue;
        const CityState& cs = cities_[static_cast<size_t>(where)];
        Stack* home = nullptr;
        for (Stack& st : stacks_)
            if (st.owner == t.owner && st.cx == cs.cx && st.cy == cs.cy && st.armies.size() < kMaxStack && st.path.empty()) home = &st;
        if (!home) home = &newStack(t.owner, cs.cx, cs.cy);
        home->armies.push_back(t.army);
    }
    transits_ = std::move(still);
}

void Campaign::setComputerDifficulty(Difficulty d) {
    for (Side& s : sides_)
        if (!s.human) s.difficulty = d;
}

uint32_t Campaign::raise(uint8_t owner, int32_t cx, int32_t cy, const std::vector<UnitType>& types) {
    Stack& s = newStack(owner, cx, cy);
    for (UnitType t : types) s.armies.push_back(makeArmy(t, ""));
    return s.id;
}

int32_t Campaign::income(uint8_t side) const {
    int32_t gold = 0;
    for (const CityState& c : cities_)
        if (c.owner == side && !c.razed) gold += c.income;
    // The land pays a little (campaign_territory.cpp).
    gold += hexesHeld(side) / kHexesPerGold;
    // Warlords' Knights "deliberately waste money": an Easy lord loses a fifth.
    const Side& s = sides_[side];
    if (!s.human && s.difficulty == Difficulty::Easy) gold -= gold / 5;
    return gold;
}

int32_t Campaign::upkeepOf(uint8_t side) const {
    int32_t gold = 0;
    for (const Stack& st : stacks_)
        if (st.owner == side)
            for (const Army& a : st.armies) gold += armyUpkeep(a);
    for (const Transit& t : transits_)
        if (t.owner == side) gold += upkeep(t.army.type);
    return gold;
}

int32_t Campaign::winner() const {
    // The last side standing wins; and as in Warlords §14, the others
    // surrender to a side holding more than half the cities when no rival is
    // within fifteen of it.
    int32_t alive = 0, last = -1, best = -1, bestCities = -1, second = 0;
    for (size_t s = 0; s < sides_.size(); ++s) {
        if (!sides_[s].alive) continue;
        ++alive;
        last = static_cast<int32_t>(s);
        const int32_t n = citiesOwned(static_cast<uint8_t>(s));
        if (n > bestCities) {
            second = std::max(second, bestCities);
            bestCities = n;
            best = static_cast<int32_t>(s);
        } else {
            second = std::max(second, n);
        }
    }
    if (alive == 1) return last + 1;
    if (best >= 0 && bestCities * 2 > static_cast<int32_t>(cities_.size()) && bestCities - second > 15) return best + 1;
    return 0;
}

int32_t Campaign::armiesAt(uint8_t owner, int32_t cx, int32_t cy) const {
    int32_t n = 0;
    for (const Stack& s : stacks_)
        if (s.owner == owner && s.cx == cx && s.cy == cy) n += static_cast<int32_t>(s.armies.size());
    return n;
}

int32_t Campaign::cellCapacity(int32_t cx, int32_t cy) const { return terrainAt(cx, cy) == Lay::City ? kMaxInCity : kMaxStack; }

const std::vector<uint16_t>& Campaign::marchField(int32_t city) const {
    if (marchFields_.empty()) marchFields_.resize(cities_.size());
    std::vector<uint16_t>& f = marchFields_[static_cast<size_t>(city)];
    if (!f.empty()) return f;
    // Dijkstra outwards from the city over the light-infantry costs: f[cell]
    // is what marching from that cell to the city costs (the cost of each
    // cell entered on the way, diagonals half as much again).
    const size_t n = static_cast<size_t>(nx_) * ny_;
    f.assign(n, UINT16_MAX);
    const CityState& c = cities_[static_cast<size_t>(city)];
    using Node = std::pair<int32_t, int32_t>;
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> open;
    const int32_t start = c.cy * nx_ + c.cx;
    f[static_cast<size_t>(start)] = 0;
    open.push({0, start});
    while (!open.empty()) {
        const auto [d, cur] = open.top();
        open.pop();
        if (d > f[static_cast<size_t>(cur)]) continue;
        const int32_t ux = cur % nx_, uy = cur / nx_;
        const int32_t enter = moveCost(T::LightInfantry, terrainAt(ux, uy));
        for (int32_t dy = -1; dy <= 1; ++dy) {
            for (int32_t dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) continue;
                const int32_t x = ux + dx, y = uy + dy;
                if (x < 0 || y < 0 || x >= nx_ || y >= ny_) continue;
                if (moveCost(T::LightInfantry, terrainAt(x, y)) >= kProhibited || !edgeOpen(x, y, ux, uy)) continue;
                const int32_t nd = d + (dx != 0 && dy != 0 ? enter * 3 / 2 : enter);
                const size_t k = static_cast<size_t>(y) * nx_ + x;
                if (nd < f[k]) {
                    f[k] = static_cast<uint16_t>(std::min(nd, 65000));
                    open.push({nd, static_cast<int32_t>(k)});
                }
            }
        }
    }
    return f;
}

}  // namespace illyria::sim
