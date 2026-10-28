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
    int32_t rangeDays10;  // how far a stack will march to attack
    int32_t upkeepShare;  // percent of the income the armies may cost
    int32_t forget;       // percent chance a stack is forgotten for the day
    int32_t blunder;      // percent chance of a random target instead of the best
    bool gather;          // merges small stacks into big ones for a purpose
    bool vector;          // sends what rear cities train to the front (LOC)
    bool fortify;         // raises the walls of frontier towns
    bool gangUp;          // turns on the leader late in the war
    bool thrifty;         // trains what fights best for the days (else the quickest)
};
// The Warlord: the best odds-taking we found (bolder loses armies, timider
// loses the race for the neutral towns), and every strategy.
constexpr Temper kHard{450, 500, 300, 20, 45, 95, 0, 0, true, true, true, true, true};
// The Knight: attacks at poor odds, sees an attack coming only at the gates,
// forgets a stack one day in four, picks a random target one time in three,
// trains whatever is quickest, never gathers or fortifies (and wastes a fifth
// of its income: Campaign::income).
constexpr Temper kEasy{300, 300, 650, 5, 45, 95, 25, 35, false, false, false, false, false};

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
    static std::vector<const Army*> view(const C::Stack& s) {
        std::vector<const Army*> v;
        for (const Army& a : s.armies) v.push_back(&a);
        return v;
    }
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
                const int32_t reach = atUs || (e.path.empty() && !garrisoned) ? t_.reactDays10 : garrisoned ? 10 : 5;
                if (when > std::max(reach, 5)) continue;
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
            std::stable_sort(pool.begin(), pool.end(), [](const Army& a, const Army& b) {
                const int32_t sa = C::strength(a), sb = C::strength(b);
                return sa != sb ? sa < sb : a.id < b.id;
            });
            const size_t keep = threatened(v) ? pool.size() : 1;
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
                if (!ours(s) || used_[k] || (s.cx == cs.cx && s.cy == cs.cy && s.path.empty())) continue;
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
            if (ours(s) && s.path.empty() && !used_[k] && !isGarrison(k)) strikers.push_back(k);
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
        const int32_t need = hasHero(s) ? t_.heroOdds : t_.attackOdds;
        if (!pinned(k)) {
            for (const auto& [rough, city] : prizes(s, lead, 8)) {
                const C::CityState& t = c_.cities_[static_cast<size_t>(city)];
                const int32_t odds = c_.attackOdds(view(s), side_, t.cx, t.cy);
                if (odds >= need) options.push_back({-rough * odds / 1000, t.cx, t.cy, city, odds});
            }
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
            return true;
        }
        return false;
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
            const int32_t need = std::max(hasHero(s) || hasHero(o) ? t_.heroOdds : t_.attackOdds, bestAlone(o, lead) + 1);
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
        while (upkeep > 0 && side.gold + income - upkeep < std::max(10, income / 4)) {
            size_t bestStack = SIZE_MAX, bestArmy = 0;
            int32_t weakest = INT32_MAX;
            for (size_t k = 0; k < c_.stacks_.size(); ++k) {
                const C::Stack& s = c_.stacks_[k];
                if (!ours(s)) continue;
                for (size_t a = 0; a < s.armies.size(); ++a) {
                    if (s.armies[a].hero() || s.armies[a].ally) continue;  // allies cost nothing
                    const int32_t w = C::strength(s.armies[a]) * 100 - C::upkeep(s.armies[a].type);
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
                // foe it lands s(10-5) hits for every 5(10-s) it takes.
                const int32_t s = warlordsStrength(op.type), w = s * 100 / (10 - std::min(s, 9));
                const int32_t score = danger || !t_.thrifty ? 1000 - op.days * 100 + s  // the quickest
                                                            : w * 10 / op.days - op.cost;  // the best fighting for the days
                if (score > bestScore) {
                    bestScore = score;
                    want = static_cast<int32_t>(o);
                }
            }
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
                if (side.gold >= cost + 150 && c_.fortify(v.index)) ++fortified;
            }
        }
    }
};

void Campaign::runAi(uint8_t side) {
    questAi(side);   // heroes go exploring when it is safe (campaign_quests.cpp)
    runSeaAi(side);  // boats first: targets across the water (campaign_sea.cpp)
    CampaignAi(*this, side).run();  // then the war on land
}

}  // namespace illyria::sim
