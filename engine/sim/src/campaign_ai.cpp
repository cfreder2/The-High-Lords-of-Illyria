// The computer lords of the campaign (docs/GAME_DESIGN.md §4.2.5). Once a day
// each computer side looks at the whole map, as Warlords' computer players do,
// and plays by the same rules as the player:
// - it knows, for each of its cities, the enemy stacks marching on it or
//   lying within a few days' march, and the exact chance (the Warlords dice
//   odds) that they would take it; for each city it does not hold, its worth,
//   its defenders and the chance a given stack has of taking it;
// - it defends: a threatened city keeps everything inside it, and stacks
//   that can get there before the enemy are pulled back, if that can save it;
// - it gathers its armies into stacks of up to eight, merges small stacks
//   when together they could take what neither can alone, and sends them
//   where the prize is best for the risk: neutral towns early, weak enemy
//   cities later, the leader's cities when one side runs away with the war;
// - it trains what fights best for the days it takes (the quickest where a
//   city is threatened), sends what quiet rear cities train to the front
//   (LOC), keeps its upkeep inside its income, raises the walls of frontier
//   towns with the gold left over, and disbands rather than see armies desert.
// Deterministic: integers, the campaign's own seeded Rng, stable orders.
#include <algorithm>
#include <climits>
#include <map>

#include "illyria/sim/campaign.h"

namespace illyria::sim {

namespace {

// How a difficulty plays. The numbers came from playing the computer lords
// against one another over many seeds (tests/test_campaign_ai.cpp).
struct Temper {
    int32_t attackOdds;   // thousandths needed to attack
    int32_t heroOdds;     // ... with a hero in the stack
    int32_t accept;       // the enemy's chance of taking a city we can live with
    int32_t reactDays10;  // how far off (tenths of days) an enemy marching on us counts as a threat
    int32_t sittingDays10;  // ... and one sitting in its own city (the Warlord waits for it to march)
    int32_t rangeDays10;  // how far a stack will march to attack
    int32_t upkeepShare;  // percent of the income the armies may cost
    int32_t forget;       // percent chance a stack is forgotten for the day
    int32_t blunder;      // percent chance of a random target instead of the best
    int32_t whim;         // percent chance a city trains whatever takes the lord's fancy
    bool gather;          // merges small stacks into big ones for a purpose
    bool vector;          // sends what rear cities train to the front (LOC)
    bool fortify;         // raises the walls of frontier towns
    bool gangUp;          // turns on the leader late in the war
    bool thrifty;         // trains what fights best for the days (else the quickest)
    int32_t care;         // thousandths more needed to attack for each precious army risked
    int32_t mostOdds;     // ... but never more than this
};
// The Warlord: the best odds-taking we found (bolder loses armies, timider
// loses the race for the neutral towns; with armies slow to train, bolder than
// it was), six points more for each veteran or rare army a stack would risk,
// an enemy counted a threat once it marches (not while it sits in its own
// walls, which would keep every army home), and every strategy. (40% since
// the small edges made the odds finer and a town must be held while the
// banner rises: at 35% it threw armies away at the walls.)
constexpr Temper kHard{400, 450, 400, 20, 0, 45, 95, 0, 0, 0, true, true, true, true, true, 60, 850};
// The Knight: attacks at poor odds, sees an attack coming only at the gates,
// forgets a stack one day in four, picks a random target one time in three,
// trains whatever takes its fancy (the quickest with an enemy at the gates),
// never gathers or fortifies, and wastes a fifth of its income (Campaign::income).
constexpr Temper kEasy{300, 300, 650, 5, 10, 45, 95, 25, 35, 100, false, false, false, false, false, 0, 1000};

}  // namespace

class CampaignAi {
public:
    CampaignAi(Campaign& c, uint8_t side)
        : c_(c), side_(side), t_(c.sides_[side].difficulty == Difficulty::Hard ? kHard : kEasy) {}

    void run() {
        survey();
        reorganize();
        survey();  // the stacks have changed
        defend();
        attack();
        economy();
        c_.stacks_.erase(std::remove_if(c_.stacks_.begin(), c_.stacks_.end(), [&](const Campaign::Stack& s) {
                             return s.armies.empty() && s.id != c_.pending_.stack;
                         }),
                         c_.stacks_.end());
    }

    // An enemy has marched into one of our towns and begun raising its
    // banner (the Warlord hears of it at once; the Knight only at dawn): send
    // the nearest stack that can beat them there before the banner is up, and
    // failing that the nearest that can reach the walls in time to hold it up.
    void respond(int32_t city) {
        survey();
        const C::CityState& cs = c_.cities_[static_cast<size_t>(city)];
        const std::vector<const Army*> raisers = c_.defendersAt(side_, cs.cx, cs.cy);
        if (raisers.empty()) return;
        const int32_t rate = C::kBannerBase + std::min(static_cast<int32_t>(raisers.size()), C::kMaxStack);
        const int32_t left10 = (C::kBannerFull - cs.bannerProgress) / rate * 10 / C::kTicksPerDay + 1;  // tenths of days
        std::vector<std::pair<int32_t, size_t>> near;
        for (size_t k = 0; k < c_.stacks_.size(); ++k) {
            const C::Stack& s = c_.stacks_[k];
            if (!ours(s) || s.boat || (s.cx == cs.cx && s.cy == cs.cy)) continue;
            if (s.path.empty() && ownCity(cityHere(s))) continue;  // the guards of our other towns stay where they are
            const int32_t when = eta(s, city);
            if (when <= left10 + 2) near.push_back({when, k});
        }
        std::stable_sort(near.begin(), near.end());
        for (const auto& [when, k] : near) {
            if (c_.attackOdds(view(c_.stacks_[k]), side_, cs.cx, cs.cy) < needed(c_.stacks_[k].armies)) continue;
            if (march(k, cs.cx, cs.cy)) return;
        }
        // None can win: the nearest one stands at the gates, which stops the banner.
        for (const auto& [when, k] : near) {
            const C::Stack& s = c_.stacks_[k];
            int32_t bx = -1, by = -1, best = INT32_MAX;
            for (int32_t dy = -1; dy <= 1; ++dy)
                for (int32_t dx = -1; dx <= 1; ++dx) {
                    const int32_t x = cs.cx + dx, y = cs.cy + dy;
                    if ((dx == 0 && dy == 0) || x < 0 || y < 0 || x >= c_.nx_ || y >= c_.ny_) continue;
                    if (c_.stackCost(s, x, y) >= C::kProhibited || !c_.defendersAt(side_, x, y).empty() || c_.cityAt(x, y) >= 0) continue;
                    const int32_t d = std::max(std::abs(x - s.cx), std::abs(y - s.cy));
                    if (d < best) best = d, bx = x, by = y;
                }
            if (bx >= 0 && march(k, bx, by)) return;
        }
    }

private:
    using C = Campaign;
    struct CityView {
        int32_t index = 0;
        bool frontier = false;          // an enemy city or field stack is near
        int32_t threatEta = INT32_MAX;  // tenths of days until the nearest threat arrives
        int32_t threatOdds = 0;         // the worst chance a threat has of taking it
        size_t threatStack = SIZE_MAX;  // that threat
    };

    Campaign& c_;
    uint8_t side_;
    Temper t_;
    std::vector<CityView> mine_;          // our cities
    std::map<int32_t, size_t> cityView_;  // city index -> mine_
    std::vector<uint8_t> used_;           // per stack index: has orders today
    std::vector<int32_t> claimed_;        // cities a stack is already sent at
    std::vector<uint32_t> guards_;        // stacks that stay home as garrisons today
    int32_t staging_ = -1;                // our city nearest the next prize

    bool chance(int32_t percent) { return percent > 0 && static_cast<int32_t>(c_.aiRng_.nextBelow(100)) < percent; }
    bool ours(const C::Stack& s) const { return s.owner == side_ && !s.armies.empty(); }
    bool foe(const C::Stack& s) const { return s.owner != side_ && s.owner != C::kNeutral && !s.garrison && !s.armies.empty(); }
    static std::vector<const Army*> viewOf(const std::vector<Army>& armies) {
        std::vector<const Army*> v;
        for (const Army& a : armies) v.push_back(&a);
        return v;
    }
    static std::vector<const Army*> view(const C::Stack& s) { return viewOf(s.armies); }
    static bool hasHero(const C::Stack& s) {
        for (const Army& a : s.armies)
            if (a.hero()) return true;
        return false;
    }
    bool airborne(const C::Stack& s) const {
        bool all = !s.armies.empty(), any = false;
        for (const Army& a : s.armies) {
            all = all && (C::flies(a.type) || a.hero());
            any = any || C::flies(a.type);
        }
        return all && any;
    }
    // The odds a stack needs before it attacks: better with a hero in it, and
    // better still for each veteran or rare army that could be lost for good.
    int32_t needed(const std::vector<Army>& armies) const {
        int32_t precious = 0;
        bool hero = false;
        for (const Army& a : armies) {
            hero = hero || a.hero();
            precious += !a.hero() && C::precious(a);
        }
        return std::min(t_.mostOdds, (hero ? t_.heroOdds : t_.attackOdds) + t_.care * precious);
    }
    // Tenths of days for a stack to reach a city.
    int32_t eta(const C::Stack& s, int32_t city) const {
        const C::CityState& t = c_.cities_[static_cast<size_t>(city)];
        const int32_t moves = std::max(1, c_.stackMoves(s));
        int32_t mp;
        if (airborne(s)) {
            mp = 2 * std::max(std::abs(t.cx - s.cx), std::abs(t.cy - s.cy));
        } else {
            const uint16_t f = c_.marchField(city)[static_cast<size_t>(s.cy) * c_.nx_ + s.cx];
            if (f == UINT16_MAX) return INT32_MAX;
            mp = f;
        }
        return mp * 10 / moves;
    }
    // Tenths of days for a walker (ten points a day) from one city to another.
    int32_t cityDays10(int32_t from, int32_t to) const {
        const C::CityState& f = c_.cities_[static_cast<size_t>(from)];
        const uint16_t d = c_.marchField(to)[static_cast<size_t>(f.cy) * c_.nx_ + f.cx];
        return d == UINT16_MAX ? INT32_MAX : d;
    }
    int32_t cityHere(const C::Stack& s) const { return c_.cityAt(s.cx, s.cy); }
    bool ownCity(int32_t city) const { return city >= 0 && c_.cities_[static_cast<size_t>(city)].owner == side_; }
    // Standing in a city where our banner is going up: it holds there until
    // the city is ours (leaving would throw the banner away).
    bool raising(const C::Stack& s) const {
        const int32_t city = cityHere(s);
        return s.path.empty() && city >= 0 && c_.cities_[static_cast<size_t>(city)].banner == side_;
    }
    bool threatened(const CityView& v) const { return v.threatOdds > t_.accept; }

    // What we hold and what threatens it.
    void survey() {
        mine_.clear();
        cityView_.clear();
        for (size_t i = 0; i < c_.cities_.size(); ++i) {
            const C::CityState& cs = c_.cities_[i];
            if (cs.owner != side_ || cs.razed) continue;
            CityView v;
            v.index = static_cast<int32_t>(i);
            for (size_t j = 0; j < c_.cities_.size() && !v.frontier; ++j) {
                const C::CityState& o = c_.cities_[j];
                v.frontier = o.owner != side_ && o.owner != C::kNeutral && !o.razed && cityDays10(static_cast<int32_t>(j), v.index) <= 30;
            }
            for (size_t k = 0; k < c_.stacks_.size(); ++k) {
                const C::Stack& e = c_.stacks_[k];
                if (!foe(e)) continue;
                const int32_t when = eta(e, v.index);
                const bool garrisoned = e.path.empty() && c_.cityAt(e.cx, e.cy) >= 0;  // sitting in its own walls
                if (!garrisoned && when <= 20) v.frontier = true;
                // Marching on us, or idle in the field within reach: a threat.
                // A garrison next door, or a stack marching elsewhere: only
                // when it is all but at the gates.
                const bool atUs = !e.path.empty() && e.path.back() == std::make_pair(cs.cx, cs.cy);
                const int32_t reach = atUs || (e.path.empty() && !garrisoned) ? t_.reactDays10 : garrisoned ? t_.sittingDays10 : 5;
                if (when > reach) continue;
                const int32_t odds = c_.attackOdds(view(e), e.owner, cs.cx, cs.cy);
                if (odds > v.threatOdds || (odds == v.threatOdds && when < v.threatEta)) {
                    v.threatOdds = odds;
                    v.threatStack = k;
                }
                v.threatEta = std::min(v.threatEta, when);
            }
            cityView_[v.index] = mine_.size();
            mine_.push_back(v);
        }
        used_.assign(c_.stacks_.size(), 0);
    }

    // In each of our cities: the weakest army stays as the garrison (all of
    // them, under threat), the rest form strike stacks of up to eight,
    // strongest first, the hero leading the first.
    void reorganize() {
        guards_.clear();
        std::map<int32_t, std::vector<size_t>> byCell;  // idle stacks of ours in our cities
        for (size_t k = 0; k < c_.stacks_.size(); ++k) {
            const C::Stack& s = c_.stacks_[k];
            if (!ours(s) || !s.path.empty() || s.boat || !ownCity(cityHere(s))) continue;
            byCell[s.cy * c_.nx_ + s.cx].push_back(k);
        }
        for (const auto& [cell, list] : byCell) {
            const int32_t cx = cell % c_.nx_, cy = cell / c_.nx_;
            const CityView& v = mine_[cityView_[c_.cityAt(cx, cy)]];
            std::vector<Army> pool;
            for (size_t k : list)
                for (const Army& a : c_.stacks_[k].armies) pool.push_back(a);
            // Green common troops first: they garrison; veterans and rare
            // troops (precious: nobody can replace them) ride out last.
            std::stable_sort(pool.begin(), pool.end(), [](const Army& a, const Army& b) {
                if (C::precious(a) != C::precious(b)) return C::precious(b);
                const int32_t sa = C::strength(a), sb = C::strength(b);
                return sa != sb ? sa < sb : a.id < b.id;
            });
            // As many guards as it takes to hold off the threat at the odds
            // we accept (all of them when even that is not enough), else one.
            size_t keep = 1;
            if (threatened(v)) {
                // Everyone stays, unless all but one guard could go out and
                // destroy the threat itself (the best defence, when the odds are good).
                keep = pool.size();
                if (t_.gather && pool.size() > 1) {
                    const C::Stack& e = c_.stacks_[v.threatStack];
                    std::vector<Army> out(pool.begin() + 1, pool.end());
                    if (c_.attackOdds(viewOf(out), side_, e.cx, e.cy) >= needed(out)) keep = 1;
                }
            } else if (v.threatStack != SIZE_MAX && pool.size() > 1) {
                const C::Stack& e = c_.stacks_[v.threatStack];
                const std::vector<int32_t> att = c_.lineup(view(e), e.owner, cx, cy, false);
                std::vector<const Army*> held;
                keep = pool.size();
                for (size_t n = 0; n < pool.size(); ++n) {
                    if (pool[n].hero() || pool[n].type == UnitType::Catapult) continue;
                    held.push_back(&pool[n]);
                    if (C::winChance(att, c_.lineup(held, side_, cx, cy, true)) <= t_.accept) {
                        keep = held.size();
                        break;
                    }
                }
            }
            std::vector<Army> guard, strike;
            // Siege engines are for taking towns, not for holding them.
            for (const Army& a : pool) {
                if (!a.hero() && a.type != UnitType::Catapult && guard.size() < keep) guard.push_back(a);
                else strike.push_back(a);
            }
            // A hero (or an engine) never rides out alone: with nobody to fight beside, he stays.
            if (strike.size() == 1 && (strike.front().hero() || strike.front().type == UnitType::Catapult)) {
                guard.push_back(strike.front());
                strike.clear();
            }
            std::reverse(strike.begin(), strike.end());  // strongest (the hero) first
            // ... and the engines with the first stack, which goes for the walls.
            std::stable_partition(strike.begin(), strike.end(), [](const Army& a) { return a.hero() || a.type == UnitType::Catapult; });
            std::vector<std::vector<Army>> stacks;
            for (size_t g = 0; g < guard.size(); g += C::kMaxStack)
                stacks.emplace_back(guard.begin() + static_cast<long>(g), guard.begin() + static_cast<long>(std::min(guard.size(), g + C::kMaxStack)));
            for (size_t g = 0; g < strike.size(); g += C::kMaxStack)
                stacks.emplace_back(strike.begin() + static_cast<long>(g), strike.begin() + static_cast<long>(std::min(strike.size(), g + C::kMaxStack)));
            // Reuse the stacks already there (ids stay put), new ones for the rest.
            for (size_t k = 0; k < stacks.size(); ++k) {
                C::Stack& st = k < list.size() ? c_.stacks_[list[k]] : c_.newStack(side_, cx, cy);
                st.armies = std::move(stacks[k]);
                if (k * C::kMaxStack < guard.size()) guards_.push_back(st.id);
            }
            for (size_t k = stacks.size(); k < list.size(); ++k) c_.stacks_[list[k]].armies.clear();
        }
        c_.stacks_.erase(std::remove_if(c_.stacks_.begin(), c_.stacks_.end(), [&](const C::Stack& s) {
                             return s.armies.empty() && s.id != c_.pending_.stack;
                         }),
                         c_.stacks_.end());
    }

    // Is this stack a garrison, set to stay home today?
    bool isGarrison(size_t k) const {
        const C::Stack& s = c_.stacks_[k];
        return s.path.empty() && std::find(guards_.begin(), guards_.end(), s.id) != guards_.end();
    }
    // In a threatened city of ours: it does not leave, except to strike the threat itself.
    bool pinned(size_t k) const {
        const int32_t home = cityHere(c_.stacks_[k]);
        return ownCity(home) && threatened(mine_[cityView_.at(home)]);
    }

    bool march(size_t k, int32_t cx, int32_t cy) {
        C::Stack& s = c_.stacks_[k];
        std::vector<std::pair<int32_t, int32_t>> path;
        if (!c_.planRoute(s, cx, cy, path) || path.empty()) return false;  // on foot, or by boat
        s.path = std::move(path);
        s.progress = 0;
        used_[k] = 1;
        return true;
    }

    // Save the cities that can be saved, the capital first: bring home the
    // stacks that can get there before the enemy does, as many as it takes.
    void defend() {
        std::vector<size_t> order;
        for (size_t i = 0; i < mine_.size(); ++i)
            if (threatened(mine_[i])) order.push_back(i);
        const auto value = [&](size_t i) {
            const int32_t c = mine_[i].index;
            return c_.cities_[static_cast<size_t>(c)].income + (c_.continent_.cities()[static_cast<size_t>(c)].capital ? 100 : 0);
        };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return value(a) > value(b); });
        for (size_t i : order) {
            const CityView& v = mine_[i];
            const C::CityState& cs = c_.cities_[static_cast<size_t>(v.index)];
            const C::Stack& threat = c_.stacks_[v.threatStack];
            std::vector<std::pair<int32_t, size_t>> help;  // (eta, stack), nearest first
            for (size_t k = 0; k < c_.stacks_.size(); ++k) {
                const C::Stack& s = c_.stacks_[k];
                if (!ours(s) || used_[k] || (s.cx == cs.cx && s.cy == cs.cy && s.path.empty()) || raising(s)) continue;
                if (s.path.empty() && (isGarrison(k) || pinned(k))) continue;  // another city's guard stays
                const int32_t when = eta(s, v.index);
                if (when <= v.threatEta + 5) help.push_back({when, k});
            }
            std::stable_sort(help.begin(), help.end());
            std::vector<const Army*> def = c_.defendersAt(threat.owner, cs.cx, cs.cy);
            const std::vector<int32_t> att = c_.lineup(view(threat), threat.owner, cs.cx, cs.cy, false);
            int32_t odds = v.threatOdds;
            size_t take = 0;
            while (take < help.size() && odds > t_.accept) {
                for (const Army& a : c_.stacks_[help[take].second].armies) def.push_back(&a);
                odds = C::winChance(att, c_.lineup(def, side_, cs.cx, cs.cy, true));
                ++take;
            }
            // Lost anyway? Then do not throw good armies after it.
            if (odds > 600 && odds > v.threatOdds - 150) continue;
            for (size_t h = 0; h < take; ++h) march(help[h].second, cs.cx, cs.cy);
        }
    }

    // What a city is worth to us.
    int32_t worth(int32_t city, int32_t leader) const {
        const C::CityState& t = c_.cities_[static_cast<size_t>(city)];
        int32_t v = 10 + t.income / 2;
        const uint32_t day = c_.day();
        if (t.owner == C::kNeutral) {
            v += day < 30 ? 8 : 2;  // the race for the neutral towns comes first
        } else {
            v += day < 30 ? 0 : 5;  // then the enemy's
            if (c_.continent_.cities()[static_cast<size_t>(city)].capital) v += 10;
            if (t_.gangUp && t.owner == leader) v += 15;
        }
        // Position: a town among our own is easy to hold.
        for (const CityView& m : mine_)
            if (cityDays10(m.index, city) <= 20) v += 3;
        // Land: a town brings the hexes round it, and those are worth a
        // little (gold, and the home-land edge): one point for every two
        // not yet ours.
        std::vector<int32_t> ring{c_.hexAt(t.cx, t.cy)};
        C::hexNeighbours(ring.front(), ring);
        int32_t land = 0;
        for (int32_t h : ring) land += c_.hexes_[static_cast<size_t>(h)].land && c_.hexes_[static_cast<size_t>(h)].owner != side_;
        v += land / 2;
        return v;
    }

    // The side running away with the war, if any (not us): at least twelve
    // cities, and half as many again as the next.
    int32_t leader() const {
        int32_t best = -1, bestN = 0, second = 0;
        for (size_t s = 0; s < c_.sides_.size(); ++s) {
            const int32_t n = c_.citiesOwned(static_cast<uint8_t>(s));
            if (n > bestN) {
                second = bestN;
                bestN = n;
                best = static_cast<int32_t>(s);
            } else {
                second = std::max(second, n);
            }
        }
        if (best == side_ || bestN < 12 || bestN * 2 < second * 3) return -1;
        return best;
    }

    void attack() {
        const int32_t lead = leader();
        for (size_t k = 0; k < c_.stacks_.size(); ++k) {
            const C::Stack& s = c_.stacks_[k];
            if (!ours(s) || s.path.empty()) continue;
            const int32_t city = c_.cityAt(s.path.back().first, s.path.back().second);
            if (city >= 0 && !ownCity(city)) claimed_.push_back(city);  // already on its way
        }
        // Our city nearest the best prize: where rear cities send their recruits.
        int32_t prize = -1, prizeScore = -1;
        for (size_t i = 0; i < c_.cities_.size(); ++i) {
            const C::CityState& t = c_.cities_[i];
            if (t.owner == side_ || t.razed) continue;
            int32_t near = INT32_MAX;
            for (const CityView& m : mine_) near = std::min(near, cityDays10(m.index, static_cast<int32_t>(i)));
            if (near == INT32_MAX) continue;
            const int32_t score = worth(static_cast<int32_t>(i), lead) * 100 / (near + 10);
            if (score > prizeScore) {
                prizeScore = score;
                prize = static_cast<int32_t>(i);
            }
        }
        staging_ = -1;
        if (prize >= 0) {
            int32_t best = INT32_MAX;
            for (const CityView& m : mine_) {
                const int32_t d = cityDays10(m.index, prize);
                if (d < best) {
                    best = d;
                    staging_ = m.index;
                }
            }
        }
        // Strike stacks, biggest first.
        std::vector<size_t> strikers;
        for (size_t k = 0; k < c_.stacks_.size(); ++k) {
            const C::Stack& s = c_.stacks_[k];
            if (ours(s) && s.path.empty() && !used_[k] && !isGarrison(k) && !raising(s)) strikers.push_back(k);
        }
        std::stable_sort(strikers.begin(), strikers.end(), [&](size_t a, size_t b) { return c_.stacks_[a].armies.size() > c_.stacks_[b].armies.size(); });
        for (size_t k : strikers) {
            if (used_[k]) continue;
            if (chance(t_.forget)) continue;  // an Easy lord forgets what he was doing
            if (strikeFrom(k, lead)) continue;
            if (pinned(k)) continue;  // stays to hold a threatened city
            if (t_.gather && gather(k, lead)) continue;
            goHome(k);
        }
    }

    // The cities worth marching on from where a stack stands, best first:
    // (-rough score, city), the odds still to be weighed.
    std::vector<std::pair<int32_t, int32_t>> prizes(const C::Stack& s, int32_t lead, size_t most) const {
        std::vector<std::pair<int32_t, int32_t>> cand;
        for (size_t i = 0; i < c_.cities_.size(); ++i) {
            const C::CityState& t = c_.cities_[i];
            if (t.owner == side_ || t.razed) continue;
            if (std::find(claimed_.begin(), claimed_.end(), static_cast<int32_t>(i)) != claimed_.end()) continue;
            const int32_t when = eta(s, static_cast<int32_t>(i));
            if (when > t_.rangeDays10) continue;
            cand.push_back({-(worth(static_cast<int32_t>(i), lead) * 100 / (when + 10)), static_cast<int32_t>(i)});
        }
        std::stable_sort(cand.begin(), cand.end());
        if (cand.size() > most) cand.resize(most);
        return cand;
    }

    // A stack of ours marching to join this one, there within a day.
    bool reinforced(size_t k) const {
        const C::Stack& s = c_.stacks_[k];
        const int32_t here = cityHere(s);
        if (!ownCity(here)) return false;
        for (const C::Stack& o : c_.stacks_)
            if (ours(o) && !o.path.empty() && o.path.back() == std::make_pair(s.cx, s.cy) && eta(o, here) <= 10) return true;
        return false;
    }

    // Send a strike stack at the best prize it can take at good odds: a
    // city, or an enemy stack in the field near our cities.
    bool strikeFrom(size_t k, int32_t lead) {
        const C::Stack& s = c_.stacks_[k];
        struct Option {
            int32_t score, cx, cy, city, odds;
        };
        std::vector<Option> options;
        const int32_t need = needed(s.armies);
        if (!pinned(k)) {
            for (const auto& [rough, city] : prizes(s, lead, 8)) {
                const C::CityState& t = c_.cities_[static_cast<size_t>(city)];
                const int32_t odds = c_.attackOdds(view(s), side_, t.cx, t.cy);
                if (odds >= need) options.push_back({-rough * odds / 1000, t.cx, t.cy, city, odds});
            }
        } else if (t_.gather) {
            // Held at home by a threat: strike the threat itself, in a city or not.
            const CityView& m = mine_[cityView_.at(cityHere(s))];
            const C::Stack& e = c_.stacks_[m.threatStack];
            const int32_t odds = c_.attackOdds(view(s), side_, e.cx, e.cy);
            if (odds >= need) options.push_back({1000, e.cx, e.cy, c_.cityAt(e.cx, e.cy), odds});
        }
        if (t_.reactDays10 >= 10) {
            for (const CityView& m : mine_) {
                if (m.threatStack == SIZE_MAX) continue;
                const C::Stack& e = c_.stacks_[m.threatStack];
                if (c_.cityAt(e.cx, e.cy) >= 0) continue;  // a stack in a city is a city target
                const int32_t d = std::max(std::abs(e.cx - s.cx), std::abs(e.cy - s.cy));
                if (d > 12) continue;
                const int32_t odds = c_.attackOdds(view(s), side_, e.cx, e.cy);
                if (odds >= need) options.push_back({3000 / (d + 10) * odds / 1000 + 20, e.cx, e.cy, -1, odds});
            }
        }
        if (options.empty()) return false;
        std::stable_sort(options.begin(), options.end(), [](const Option& a, const Option& b) { return a.score > b.score; });
        // Reinforcements a day off: unless the prize is sure, wait for them.
        if (t_.gather && options.front().odds < 850 && reinforced(k)) {
            used_[k] = 1;
            return true;
        }
        if (options.size() > 1 && chance(t_.blunder)) std::swap(options[0], options[c_.aiRng_.nextBelow(static_cast<uint32_t>(options.size()))]);
        for (size_t o = 0; o < options.size() && o < 3; ++o) {
            if (!march(k, options[o].cx, options[o].cy)) continue;
            if (options[o].city >= 0) claimed_.push_back(options[o].city);
            if (t_.gather) spare(k, options[o].cx, options[o].cy);
            return true;
        }
        return false;
    }

    // A stack setting out from one of our cities leaves its veterans and
    // rare troops behind, the best first, when the attack does not need
    // them: those who are not there cannot fall.
    void spare(size_t k, int32_t tx, int32_t ty) {
        if (!ownCity(cityHere(c_.stacks_[k]))) return;
        std::vector<Army> go = c_.stacks_[k].armies, stay;
        std::vector<size_t> order;
        for (size_t a = 0; a < go.size(); ++a)
            if (!go[a].hero() && C::precious(go[a])) order.push_back(a);
        if (order.empty()) return;
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return C::strength(go[a]) > C::strength(go[b]); });
        std::vector<uint8_t> left(go.size(), 0);
        for (size_t a : order) {
            std::vector<Army> rest;
            bool soldiers = false;
            left[a] = 1;
            for (size_t b = 0; b < go.size(); ++b)
                if (!left[b]) {
                    rest.push_back(go[b]);
                    soldiers = soldiers || !go[b].hero();
                }
            if (!soldiers || c_.attackOdds(viewOf(rest), side_, tx, ty) < needed(rest)) left[a] = 0;
        }
        std::vector<Army> rest;
        for (size_t b = 0; b < go.size(); ++b) (left[b] ? stay : rest).push_back(go[b]);
        if (stay.empty()) return;
        c_.stacks_[k].armies = std::move(rest);
        const int32_t cx = c_.stacks_[k].cx, cy = c_.stacks_[k].cy;
        C::Stack& home = c_.newStack(side_, cx, cy);  // (may move the stacks: nothing above is kept)
        home.armies = std::move(stay);
        used_.push_back(1);
    }

    // The best odds a stack has on its own against the prizes near it.
    int32_t bestAlone(const C::Stack& s, int32_t lead) const {
        int32_t best = 0;
        for (const auto& [rough, target] : prizes(s, lead, 4)) {
            const C::CityState& t = c_.cities_[static_cast<size_t>(target)];
            best = std::max(best, c_.attackOdds(view(s), side_, t.cx, t.cy));
        }
        return best;
    }

    // Too weak to attack alone: join a bigger strike stack of ours nearby
    // when, together, they could take a prize neither can take alone.
    bool gather(size_t k, int32_t lead) {
        const C::Stack& s = c_.stacks_[k];
        for (size_t j = 0; j < c_.stacks_.size(); ++j) {
            const C::Stack& o = c_.stacks_[j];
            if (j == k || !ours(o) || !o.path.empty() || used_[j] || isGarrison(j)) continue;
            if (o.armies.size() + s.armies.size() > C::kMaxStack) continue;
            if (o.armies.size() < s.armies.size() || (o.armies.size() == s.armies.size() && o.id > s.id)) continue;
            const int32_t city = cityHere(o);
            if (!ownCity(city) || eta(s, city) > 15) continue;
            std::vector<const Army*> both = view(o);
            for (const Army& a : s.armies) both.push_back(&a);
            std::vector<Army> together = o.armies;
            together.insert(together.end(), s.armies.begin(), s.armies.end());
            const int32_t need = std::max(needed(together), bestAlone(o, lead) + 1);
            for (const auto& [rough, target] : prizes(o, lead, 4)) {
                const C::CityState& t = c_.cities_[static_cast<size_t>(target)];
                if (c_.attackOdds(both, side_, t.cx, t.cy) < need) continue;
                if (!march(k, o.cx, o.cy)) break;
                used_[j] = 1;  // it waits for us
                return true;
            }
        }
        return false;
    }

    // A stack idle in the field goes to the nearest city of ours.
    void goHome(size_t k) {
        const C::Stack& s = c_.stacks_[k];
        if (ownCity(cityHere(s))) return;
        int32_t best = -1, bestD = INT32_MAX;
        for (const CityView& m : mine_) {
            const int32_t d = eta(s, m.index);
            if (d < bestD) {
                bestD = d;
                best = m.index;
            }
        }
        if (best >= 0) march(k, c_.cities_[static_cast<size_t>(best)].cx, c_.cities_[static_cast<size_t>(best)].cy);
    }

    // Gold: train by need, keep the upkeep inside the income, disband rather
    // than see armies desert, raise the walls where the enemy is near.
    void economy() {
        C::Side& side = c_.sides_[side_];
        const int32_t income = c_.income(side_);
        int32_t upkeep = c_.upkeepOf(side_);
        // Tomorrow's pay would all but empty the chest (and a city lost would
        // empty it): send the weakest home first.
        while (upkeep > 0 && side.gold + income - upkeep < std::max(25, income / 3)) {
            size_t bestStack = SIZE_MAX, bestArmy = 0;
            int32_t weakest = INT32_MAX;
            for (size_t k = 0; k < c_.stacks_.size(); ++k) {
                const C::Stack& s = c_.stacks_[k];
                if (!ours(s)) continue;
                for (size_t a = 0; a < s.armies.size(); ++a) {
                    if (s.armies[a].hero() || s.armies[a].ally) continue;  // allies cost nothing
                    // Green common troops go first: veterans and rare troops cannot be had again.
                    const int32_t w = (C::precious(s.armies[a]) ? 1000 : 0) + C::strength(s.armies[a]) * 100 - C::upkeep(s.armies[a].type);
                    if (w < weakest) {
                        weakest = w;
                        bestStack = k;
                        bestArmy = a;
                    }
                }
            }
            if (bestStack == SIZE_MAX) break;
            C::Stack& s = c_.stacks_[bestStack];
            upkeep -= C::upkeep(s.armies[bestArmy].type);
            s.armies.erase(s.armies.begin() + static_cast<long>(bestArmy));
        }
        const int32_t allowed = income * t_.upkeepShare / 100 + side.gold / 20;
        // Siege engines, for the walled enemy towns of the later war (from day
        // 40): about one for every eight cities. They are slow and dear; more
        // cost the race for towns more than they win at the walls.
        int32_t engines = 0;
        for (const C::Stack& s : c_.stacks_)
            if (ours(s))
                for (const Army& a : s.armies) engines += a.type == UnitType::Catapult;
        for (const CityView& v : mine_) {
            const C::CityState& cs = c_.cities_[static_cast<size_t>(v.index)];
            engines += cs.producing >= 0 && cs.options[static_cast<size_t>(cs.producing)].type == UnitType::Catapult;
        }
        bool wantEngines = t_.thrifty && engines * 8 < static_cast<int32_t>(mine_.size()) && c_.day() >= 40;
        int32_t fortified = 0;
        for (const CityView& v : mine_) {
            C::CityState& cs = c_.cities_[static_cast<size_t>(v.index)];
            const bool danger = threatened(v);
            const int32_t home = c_.armiesAt(side_, cs.cx, cs.cy);
            int32_t want = -1, bestScore = INT32_MIN;
            for (size_t o = 0; o < cs.options.size(); ++o) {
                const C::Option& op = cs.options[o];
                // What an army is worth in the dice: against a strength-5
                // foe it lands s(10-5) hits for every 5(10-s) it takes. Now
                // that training is slow, the days count twice over: in the
                // race for the towns an army today beats a better one next
                // week. (A levy of rare troops, quick to muster and strong,
                // comes out on top.)
                const int32_t s = warlordsStrength(op.type), w = s * 100 / (10 - std::min(s, 9));
                const int32_t score = danger || !t_.thrifty ? 1000 - op.days * 100 + s  // the quickest
                                                            : w * 20 / (op.days * op.days) - op.cost;  // the best fighting for the days
                if (score > bestScore) {
                    bestScore = score;
                    want = static_cast<int32_t>(o);
                }
            }
            // An Easy lord's fancy: whatever, as long as no enemy is at the gates.
            if (!danger && !cs.options.empty() && (cs.progress == 0 || cs.producing < 0) && chance(t_.whim))
                want = static_cast<int32_t>(c_.aiRng_.nextBelow(static_cast<uint32_t>(cs.options.size())));
            if (wantEngines && !danger && v.frontier) {
                for (size_t o = 0; o < cs.options.size(); ++o)
                    if (cs.options[o].type == UnitType::Catapult) {
                        want = static_cast<int32_t>(o);
                        wantEngines = cs.producing == want;  // one more a day at most
                    }
            }
            // Can we afford to keep another? (A city with nobody home always trains.)
            if (want >= 0 && !danger && home >= 1) {
                const C::Option& op = cs.options[static_cast<size_t>(want)];
                if (upkeep + C::upkeep(op.type) > allowed || side.gold < op.cost) want = -1;
            }
            if (want != cs.producing && (cs.progress == 0 || cs.producing < 0)) {
                cs.producing = want;  // only before it is paid for: nothing is wasted
                cs.progress = 0;
            }
            if (want >= 0) upkeep += C::upkeep(cs.options[static_cast<size_t>(want)].type);
            // LOC: a quiet rear city sends what it trains to the front.
            int32_t to = -1;
            if (t_.vector && !v.frontier && !danger && staging_ >= 0 && staging_ != v.index && home >= 1 && cityDays10(v.index, staging_) > 20) to = staging_;
            if (to != cs.vectorTo && !c_.vectorProduction(v.index, to)) c_.vectorProduction(v.index, -1);
            // BUILD: the gold left over raises the walls of threatened and frontier towns.
            if (t_.fortify && fortified < 2 && (danger || v.frontier) && cs.defense < 8) {
                const int32_t cost = c_.fortifyCost(v.index);
                if (side.gold >= cost + 400 && c_.fortify(v.index)) ++fortified;  // only from a full chest: troops come first
            }
        }
    }
};

void Campaign::bannerAlarm(int32_t city) {
    const uint8_t side = cities_[static_cast<size_t>(city)].owner;
    if (side >= kNeutral || sides_[side].human || !sides_[side].alive || sides_[side].difficulty != Difficulty::Hard) return;
    CampaignAi(*this, side).respond(city);
}

void Campaign::runAi(uint8_t side) {
    questAi(side);   // heroes go exploring when it is safe (campaign_quests.cpp)
    runSeaAi(side);  // boats first: targets across the water (campaign_sea.cpp)
    CampaignAi(*this, side).run();  // then the war on land
}

}  // namespace illyria::sim
