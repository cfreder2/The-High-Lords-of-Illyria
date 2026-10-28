// Heroes, ruins, temples and sages, after Warlords (docs/GAME_DESIGN.md §4.4).
// A hero searches a ruin alone against its guardian, with Warlords' dice; the
// winner finds gold, an artifact or allies. Temples bless every army once
// each; sages reward the first hero to reach them. Now and then a hero offers
// their sword for gold. Deterministic: the seeded Rng and integers only.
#include <algorithm>
#include <climits>

#include "illyria/sim/campaign.h"
#include "illyria/sim/hash.h"
#include "illyria/sim/noise.h"

namespace illyria::sim {

namespace {

using T = UnitType;

// What waits inside a ruin, weakest first, on the Warlords scale.
struct Guardian {
    const char* name;
    int32_t strength;
};
constexpr Guardian kGuardians[] = {
    {"a pack of wolves", 2}, {"a band of brigands", 3}, {"a troll", 4},  {"the restless dead", 5},
    {"a giant spider", 5},   {"a minotaur", 6},         {"a demon", 7}, {"an ancient wyrm", 8},
};
constexpr int32_t kGuardianCount = static_cast<int32_t>(sizeof kGuardians / sizeof kGuardians[0]);

const char* const kHeroNames[] = {
    "Sir Aldric",    "Dame Isolde",      "Brannoc the Bold", "Lady Mirelle",   "Torvald Ironhand", "Sir Gawen",
    "Elowen Swiftbow", "Kaelan the Grey", "Dame Rowena",     "Hadric Stormborn", "Ysolde of the Vale", "Corwin Blackthorn",
    "Lady Seraphine", "Ulfric Bearsbane", "Thessa Dawnsworn", "Sir Maelor",
};

// How great a treasure is: 1 moderate, 2 considerable, 3 supreme.
int32_t power(const Campaign::Artifact& a) { return std::max({a.battle, a.command, a.moves > 0 ? 1 : 0, a.gold > 0 ? 1 : 0}); }

std::string plural(int32_t n, const char* type) {
    std::string t = type;
    return n == 1 ? (std::string("a band of ") + t) : (n == 2 ? "two bands of " : std::to_string(n) + " bands of ") + t;
}

}  // namespace

const std::vector<Campaign::Artifact>& Campaign::artifacts() {
    // As in Warlords: battle items for the hero's own arm, command items for
    // everyone who fights beside them; and a few for the road and the purse.
    static const std::vector<Artifact> kArtifacts = {
        {"Firebrand", 1, 0, 0, 0},          {"Frostblade", 1, 0, 0, 0},       {"Spear of Ashkar", 1, 0, 0, 0},
        {"Sunblade", 2, 0, 0, 0},           {"Shadowblade", 2, 0, 0, 0},      {"Bow of the Morning Star", 2, 0, 0, 0},
        {"Staff of Thunders", 3, 0, 0, 0},  {"Crown of Kings", 0, 1, 0, 0},   {"Sceptre of Dawn", 0, 1, 0, 0},
        {"Scarlet Banner", 0, 1, 0, 0},     {"Ring of Dominion", 0, 2, 0, 0}, {"Horn of the North", 0, 2, 0, 0},
        {"Rod of the High Lords", 0, 3, 0, 0}, {"Winged Sandals", 0, 0, 4, 0}, {"Purse of Plenty", 0, 0, 0, 10},
    };
    return kArtifacts;
}

const char* Campaign::guardianName(int32_t g) { return g >= 0 && g < kGuardianCount ? kGuardians[g].name : ""; }
int32_t Campaign::guardianStrength(int32_t g) { return g >= 0 && g < kGuardianCount ? kGuardians[g].strength : 0; }

int32_t Campaign::command(const Army& a) {
    int32_t c = 0;
    for (uint8_t k : a.artifacts) c += artifacts()[k].command;
    return c;
}

void Campaign::initSites() {
    // Guardians grow fiercer with the distance from the nearest capital, so a
    // young hero finds an easy ruin near home and a hard one in the wilds.
    const uint64_t sg = noise::nextSeed(continent_.seed() ^ 0x6A4D);
    uint8_t temples = 0;
    for (size_t i = 0; i < continent_.sites().size(); ++i) {
        const Continent::Site& site = continent_.sites()[i];
        SiteState st;
        st.kind = site.kind;
        st.cx = std::clamp(cellOf(static_cast<int32_t>(site.pos.x.floorToInt())), 0, nx_ - 1);
        st.cy = std::clamp(cellOf(static_cast<int32_t>(site.pos.y.floorToInt())), 0, ny_ - 1);
        // The site stands on dry ground even where its cell's middle is wet.
        Lay& lay = terrain_[static_cast<size_t>(st.cy) * nx_ + st.cx];
        if (lay == Lay::Water) lay = Lay::Plain;
        if (site.kind == Continent::Site::Kind::Temple) st.bit = static_cast<uint8_t>(1u << (temples++ % 8));
        if (site.kind == Continent::Site::Kind::Ruin) {
            int32_t near = INT32_MAX;
            for (const CityState& c : cities_)
                if (c.owner != kNeutral) near = std::min(near, std::max(std::abs(c.cx - st.cx), std::abs(c.cy - st.cy)));
            const int32_t band = near < 20 ? 0 : near < 30 ? 1 : near < 40 ? 2 : 3;
            const int32_t roll = static_cast<int32_t>((noise::lattice(sg, static_cast<int64_t>(i), 1) * 4).floorToInt());
            st.guardian = std::min(kGuardianCount - 1, band + roll);
        }
        sites_.push_back(st);
    }
    offers_.assign(sides_.size(), Offer{});
    artifactFound_.assign(artifacts().size(), 0);
}

int32_t Campaign::siteAt(int32_t cx, int32_t cy) const {
    for (size_t i = 0; i < sites_.size(); ++i)
        if (sites_[i].cx == cx && sites_[i].cy == cy) return static_cast<int32_t>(i);
    return -1;
}

void Campaign::deed(uint8_t side, std::string text) { deeds_.push_back({day(), side, std::move(text)}); }

namespace {
// The stack's strongest hero (the one who goes in), or -1.
int32_t bestHero(const Campaign::Stack& s) {
    int32_t best = -1;
    for (size_t k = 0; k < s.armies.size(); ++k)
        if (s.armies[k].hero() && s.armies[k].soldiers > 0 &&
            (best < 0 || Campaign::strength(s.armies[k]) > Campaign::strength(s.armies[static_cast<size_t>(best)])))
            best = static_cast<int32_t>(k);
    return best;
}
}  // namespace

bool Campaign::canSearch(uint32_t stackId) const {
    const Stack* s = findStack(stackId);
    if (!s || s->garrison || s->armies.empty() || !s->path.empty()) return false;
    const int32_t site = siteAt(s->cx, s->cy);
    if (site < 0) return false;
    const SiteState& st = sites_[static_cast<size_t>(site)];
    if (st.kind == Continent::Site::Kind::Temple) {
        for (const Army& a : s->armies)
            if (!(a.temples & st.bit)) return true;
        return !st.gifted && bestHero(*s) >= 0 && s->armies.size() < static_cast<size_t>(kMaxStack);  // its guard waits for a hero
    }
    return !st.searched && bestHero(*s) >= 0;
}

bool Campaign::search(uint32_t stackId) {
    if (pending_.stack || !canSearch(stackId)) return false;
    Stack& s = *stackById(stackId);
    const size_t site = static_cast<size_t>(siteAt(s.cx, s.cy));
    SiteState& st = sites_[site];
    const std::string& where = continent_.sites()[site].name;
    if (st.kind == Continent::Site::Kind::Temple) {
        // Every army that has not knelt here before gains a blessing (+1 strength).
        int32_t n = 0;
        for (Army& a : s.armies) {
            if (a.temples & st.bit) continue;
            a.temples |= st.bit;
            ++a.blessings;
            ++n;
        }
        if (n) deed(s.owner, where + " blessed " + (n == 1 ? std::string("one army") : std::to_string(n) + " armies"));
        templeGift(s, st, where);  // the temple's guard follows the first hero (campaign_rare.cpp)
        return true;
    }
    Army& hero = s.armies[static_cast<size_t>(bestHero(s))];
    const std::string who = hero.name.empty() ? std::string("A hero") : hero.name;
    if (st.kind == Continent::Site::Kind::Sage) {
        // The first seeker is rewarded with gold, and told of a ruin still unexplored.
        st.searched = true;
        const int32_t gold = 100 + static_cast<int32_t>(rng_.nextBelow(51));
        sides_[s.owner].gold += gold;
        std::string tale;
        int32_t best = INT32_MAX;
        for (size_t k = 0; k < sites_.size(); ++k) {
            const SiteState& r = sites_[k];
            const int32_t d = std::max(std::abs(r.cx - s.cx), std::abs(r.cy - s.cy));
            if (r.kind == Continent::Site::Kind::Ruin && !r.searched && d < best) {
                best = d;
                tale = continent_.sites()[k].name;
            }
        }
        deed(s.owner, where + " gave " + who + " " + std::to_string(gold) + " gold" + (tale.empty() ? "" : " and told of " + tale));
        return true;
    }
    // A ruin: the hero fights its guardian alone, Warlords' way. Each round
    // both roll a ten-sided die; a roll no higher than the other's strength is
    // low, and a low roll against a high one takes a hit. Two hits kill.
    const int32_t g = st.guardian, hs = strength(hero), gs = guardianStrength(g);
    int32_t heroHits = 0, guardHits = 0;
    while (heroHits < 2 && guardHits < 2) {
        const bool heroLow = static_cast<int32_t>(rng_.nextBelow(10)) + 1 <= gs;
        const bool guardLow = static_cast<int32_t>(rng_.nextBelow(10)) + 1 <= hs;
        if (heroLow != guardLow) ++(heroLow ? heroHits : guardHits);
    }
    if (heroHits >= 2) {
        // The hero is gone; what they carried lies in the ruin for the next one.
        hero.soldiers = 0;
        deed(s.owner, who + " fell to " + guardianName(g) + " in " + where);
        bury(s, s.cx, s.cy);
        return true;
    }
    hero.kills += gs;
    promote(hero);
    st.searched = true;
    const std::string slew = who + " slew " + guardianName(g) + " in " + where;
    // The treasure: gold, an artifact or allies, richer behind a fiercer guardian.
    const int32_t roll = static_cast<int32_t>(rng_.nextBelow(100));
    if (roll >= 40 && roll < 75) {
        const int32_t want = gs <= 3 ? 1 : gs <= 5 ? 2 : 3;
        std::vector<uint8_t> left, fitting;
        for (size_t k = 0; k < artifacts().size(); ++k) {
            if (artifactFound_[k]) continue;
            left.push_back(static_cast<uint8_t>(k));
            if (power(artifacts()[k]) <= want) fitting.push_back(static_cast<uint8_t>(k));
        }
        const std::vector<uint8_t>& from = fitting.empty() ? left : fitting;
        if (!from.empty()) {
            const uint8_t k = from[rng_.nextBelow(static_cast<uint32_t>(from.size()))];
            artifactFound_[k] = 1;
            hero.artifacts.push_back(k);
            deed(s.owner, who + " found the " + artifacts()[k].name + " in " + where);
            pickUp(s);
            return true;
        }
    } else if (roll >= 75 && s.armies.size() < static_cast<size_t>(kMaxStack)) {
        // Allies: special armies that join the hero's stack and want no pay.
        static const UnitType kWeak[] = {T::Undead, T::Wizards}, kStrong[] = {T::Devils, T::Archons}, kMighty[] = {T::Dragons, T::Demons};
        const uint32_t pick = rng_.nextBelow(2);
        const UnitType type = gs <= 3 ? T::Undead : gs <= 5 ? kWeak[pick] : gs <= 6 ? kStrong[pick] : kMighty[pick];
        const int32_t n = std::min<int32_t>(gs >= 5 && pick == 0 && type != T::Dragons ? 2 : 1, kMaxStack - static_cast<int32_t>(s.armies.size()));
        for (int32_t k = 0; k < n; ++k) {
            Army ally = makeArmy(type, std::string(unitTypeName(type)) + " of " + where);
            ally.ally = true;
            s.armies.push_back(ally);
        }
        deed(s.owner, slew + "; " + plural(n, unitTypeName(type)) + " joined " + who);
        pickUp(s);
        return true;
    }
    const int32_t gold = 50 + gs * 25 + static_cast<int32_t>(rng_.nextBelow(51));
    sides_[s.owner].gold += gold;
    deed(s.owner, slew + " and found " + std::to_string(gold) + " gold");
    pickUp(s);
    return true;
}

void Campaign::bury(Stack& s, int32_t cx, int32_t cy) {
    for (const Army& a : s.armies)
        if (a.soldiers <= 0)
            for (uint8_t k : a.artifacts) loot_.push_back({cx, cy, k});
    s.armies.erase(std::remove_if(s.armies.begin(), s.armies.end(), [](const Army& a) { return a.soldiers <= 0; }), s.armies.end());
    for (Army& a : s.armies) a.hits = 0;
}

void Campaign::pickUp(Stack& s) {
    if (loot_.empty()) return;
    const int32_t h = bestHero(s);
    if (h < 0) return;
    Army& hero = s.armies[static_cast<size_t>(h)];
    for (size_t k = 0; k < loot_.size();) {
        if (loot_[k].cx != s.cx || loot_[k].cy != s.cy) {
            ++k;
            continue;
        }
        hero.artifacts.push_back(loot_[k].artifact);
        deed(s.owner, hero.name + " took up the " + artifacts()[loot_[k].artifact].name);
        loot_.erase(loot_.begin() + static_cast<long>(k));
    }
}

void Campaign::questsNewDay() {
    // Purses pay their gold; then, now and then, a hero offers their sword to
    // a side with more than two cities for each hero it has, the price rising
    // with each hero on the payroll, as in Warlords. Offers last until the
    // next dawn.
    for (size_t s = 0; s < sides_.size(); ++s) {
        Side& side = sides_[s];
        int32_t heroes = 0;
        for (const Stack& st : stacks_) {
            if (st.owner != s) continue;
            for (const Army& a : st.armies) {
                heroes += a.hero();
                for (uint8_t k : a.artifacts) side.gold += artifacts()[k].gold;
            }
        }
        offers_[s].open = false;
        if (!side.alive || citiesOwned(static_cast<uint8_t>(s)) <= heroes * 2 || rng_.nextBelow(12) != 0) continue;
        std::vector<int32_t> owned;
        for (size_t i = 0; i < cities_.size(); ++i)
            if (cities_[i].owner == s && !cities_[i].razed) owned.push_back(static_cast<int32_t>(i));
        Offer& o = offers_[s];
        o.open = true;
        o.city = owned[rng_.nextBelow(static_cast<uint32_t>(owned.size()))];
        o.price = 150 + 100 * heroes + static_cast<int32_t>(rng_.nextBelow(51));
        const int32_t n = heroesHired_++;
        constexpr int32_t kNames = static_cast<int32_t>(sizeof kHeroNames / sizeof kHeroNames[0]);
        o.name = kHeroNames[n % kNames];
        if (n >= kNames) o.name += " the Younger";
        offerCompanions(o);  // some come with rare troops of their own (campaign_rare.cpp)
        if (!side.human) hire(static_cast<uint8_t>(s), side.gold >= o.price + 100);  // keeping enough to train troops
    }
}

bool Campaign::hire(uint8_t side, bool accept) {
    if (side >= offers_.size() || !offers_[side].open) return false;
    Offer& o = offers_[side];
    if (!accept) {
        o.open = false;
        return true;
    }
    CityState& c = cities_[static_cast<size_t>(o.city)];
    if (sides_[side].gold < o.price || c.owner != side || c.razed) return false;
    o.open = false;
    sides_[side].gold -= o.price;
    Stack* home = nullptr;
    for (Stack& st : stacks_)
        if (st.owner == side && st.cx == c.cx && st.cy == c.cy && st.path.empty() && st.armies.size() < kMaxStack) home = &st;
    if (!home) home = &newStack(side, c.cx, c.cy);
    home->armies.push_back(makeArmy(T::Hero, o.name));
    hireCompanions(home, side, o, o.city);
    deed(side, o.name + " took service at " + continent_.cities()[static_cast<size_t>(o.city)].name + " for " + std::to_string(o.price) + " gold");
    return true;
}

void Campaign::questAi(uint8_t side) {
    // The computer's heroes explore when it is safe: search what they stand
    // on, then make for the nearest ruin they can likely beat, a temple that
    // has not yet blessed them, or an unvisited sage, with no enemy near it.
    // A hero in a city slips out alone and leaves the rest on guard.
    struct Plan {
        size_t stack;
        std::vector<std::pair<int32_t, int32_t>> path;
        bool alone;
    };
    std::vector<Plan> plans;
    for (size_t i = 0; i < stacks_.size(); ++i) {
        Stack& s = stacks_[i];
        if (s.owner != side || s.garrison || !s.path.empty() || s.armies.empty()) continue;
        if (canSearch(s.id)) search(s.id);
        const int32_t h = bestHero(s);
        if (h < 0) continue;
        const int32_t hs = strength(s.armies[static_cast<size_t>(h)]);
        const bool inCity = cityAt(s.cx, s.cy) >= 0;
        int32_t best = INT32_MAX, target = -1;
        for (size_t k = 0; k < sites_.size(); ++k) {
            const SiteState& st = sites_[k];
            const int32_t d = std::max(std::abs(st.cx - s.cx), std::abs(st.cy - s.cy));
            if (d == 0 || d > 24 || d >= best) continue;
            bool want = false;
            if (st.kind == Continent::Site::Kind::Ruin) want = !st.searched && guardianStrength(st.guardian) < hs;
            else if (st.kind == Continent::Site::Kind::Sage) want = !st.searched;
            else if (!st.gifted) want = true;  // its guard follows the first hero to come
            else if (!inCity)
                for (const Army& a : s.armies) want = want || !(a.temples & st.bit);
            if (!want) continue;
            bool safe = true;
            for (const Stack& o : stacks_)
                safe = safe && (o.owner == side || o.garrison || o.armies.empty() || std::max(std::abs(o.cx - st.cx), std::abs(o.cy - st.cy)) > 3);
            if (!safe) continue;
            best = d;
            target = static_cast<int32_t>(k);
        }
        if (target < 0) continue;
        const bool alone = inCity && s.armies.size() > 1;
        Stack probe;
        probe.owner = s.owner;
        probe.cx = s.cx;
        probe.cy = s.cy;
        if (alone) probe.armies.push_back(s.armies[static_cast<size_t>(h)]);
        else probe.armies = s.armies;
        Plan p{i, {}, alone};
        if (!findPath(probe, sites_[static_cast<size_t>(target)].cx, sites_[static_cast<size_t>(target)].cy, p.path)) continue;
        plans.push_back(std::move(p));
    }
    for (Plan& p : plans) {
        Stack& s = stacks_[p.stack];
        if (!p.alone) {
            s.path = std::move(p.path);
            s.progress = 0;
            continue;
        }
        const size_t h = static_cast<size_t>(bestHero(s));
        Stack lone;
        lone.id = nextId_++;
        lone.owner = s.owner;
        lone.cx = s.cx;
        lone.cy = s.cy;
        lone.armies.push_back(s.armies[h]);
        s.armies.erase(s.armies.begin() + static_cast<long>(h));
        lone.path = std::move(p.path);
        stacks_.push_back(std::move(lone));  // invalidates s; nothing below uses it
    }
}

void Campaign::hashQuests(StateHasher& h) const {
    for (const SiteState& s : sites_) h.addU64(s.searched | (s.gifted ? 2u : 0u));
    for (const Loot& l : loot_) {
        h.addI64(l.cx);
        h.addI64(l.cy);
        h.addU64(l.artifact);
    }
    for (const Offer& o : offers_) {
        h.addU64(o.open);
        h.addI64(o.city);
        h.addI64(o.price);
        h.addU64(static_cast<uint64_t>(o.companion));
        h.addI64(o.companions);
    }
    for (uint8_t f : artifactFound_) h.addU64(f);
    h.addI64(heroesHired_);
}

}  // namespace illyria::sim
