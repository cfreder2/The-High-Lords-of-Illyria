// Heroes, ruins, temples and sages in the campaign, after Warlords.
#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "illyria/sim/campaign.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

using Kind = Continent::Site::Kind;

// Side 0's hero stack (its first stack with a hero).
const Campaign::Stack* heroStack(const Campaign& c, uint8_t side = 0) {
    for (const Campaign::Stack& s : c.stacks())
        if (s.owner == side && !s.garrison)
            for (const Army& a : s.armies)
                if (a.hero()) return &s;
    return nullptr;
}

// The nearest site of a kind the stack can walk to.
int32_t nearestSite(Campaign& c, const Campaign::Stack& s, Kind kind) {
    int32_t best = 1 << 30, which = -1;
    for (size_t k = 0; k < c.sites().size(); ++k) {
        const Campaign::SiteState& st = c.sites()[k];
        if (st.kind != kind || st.searched) continue;
        const int32_t d = std::max(std::abs(st.cx - s.cx), std::abs(st.cy - s.cy));
        if (d < best) {
            best = d;
            which = static_cast<int32_t>(k);
        }
    }
    return which;
}

// Marches a stack to a cell and waits until it is there (or the days run out).
bool march(Campaign& c, uint32_t id, int32_t cx, int32_t cy) {
    if (!c.move(id, cx, cy)) return false;
    for (int t = 0; t < Campaign::kTicksPerDay * 40; ++t) {
        const Campaign::Stack* s = c.findStack(id);
        if (!s) return false;
        if (s->cx == cx && s->cy == cy && s->path.empty()) return true;
        c.step();
    }
    return false;
}
}  // namespace

TEST(sites_cover_the_continent) {
    const Campaign c(continent(), 1);
    int ruins = 0, temples = 0, sages = 0;
    for (const Campaign::SiteState& s : c.sites()) {
        ruins += s.kind == Kind::Ruin;
        temples += s.kind == Kind::Temple;
        sages += s.kind == Kind::Sage;
        if (s.kind == Kind::Ruin) CHECK(Campaign::guardianStrength(s.guardian) >= 2);
        CHECK(c.siteAt(s.cx, s.cy) >= 0);
    }
    CHECK(ruins == 34);
    CHECK(temples == 4);
    CHECK(sages == 2);
}

TEST(artifacts_change_strength) {
    Army hero;
    hero.type = UnitType::Hero;
    const int32_t base = Campaign::strength(hero);
    CHECK(base == 4);
    CHECK(Campaign::command(hero) == 0);
    const auto find = [](const char* name) {
        for (size_t k = 0; k < Campaign::artifacts().size(); ++k)
            if (std::string(Campaign::artifacts()[k].name) == name) return static_cast<uint8_t>(k);
        return uint8_t{255};
    };
    hero.artifacts.push_back(find("Sunblade"));  // battle +2
    CHECK(Campaign::strength(hero) == base + 2);
    hero.artifacts.push_back(find("Crown of Kings"));  // command +1: the hero's own arm is no stronger
    CHECK(Campaign::strength(hero) == base + 2);
    CHECK(Campaign::command(hero) == 1);
    hero.artifacts.push_back(find("Staff of Thunders"));  // never past nine
    hero.rank = 3;
    CHECK(Campaign::strength(hero) == 9);
}

TEST(a_ruin_is_searched_once_and_its_hero_may_fall) {
    // A few campaigns: march side 0's hero to the nearest ruin and search it.
    int won = 0, fell = 0, artifacts = 0, allies = 0, gold = 0;
    for (uint64_t seed = 1; seed <= 12; ++seed) {
        Campaign c(continent(), seed);
        c.setHuman(0, true);
        const Campaign::Stack* s = heroStack(c);
        CHECK(s != nullptr);
        if (!s) return;
        const uint32_t id = s->id;
        const int32_t ruin = nearestSite(c, *s, Kind::Ruin);
        CHECK(ruin >= 0);
        const Campaign::SiteState site = c.sites()[static_cast<size_t>(ruin)];
        CHECK(!c.canSearch(id));  // nothing to search at home
        if (!march(c, id, site.cx, site.cy) || c.sites()[static_cast<size_t>(ruin)].searched) continue;  // beaten to it
        CHECK(c.canSearch(id));
        const Campaign::Stack before = *c.findStack(id);
        const int32_t goldBefore = c.sides()[0].gold;
        CHECK(c.search(id));
        const Campaign::Stack* after = c.findStack(id);
        const Army* hero = nullptr;
        for (const Army& a : after->armies)
            if (a.hero()) hero = &a;
        if (!hero) {
            // Fallen: the ruin keeps its guardian for the next hero.
            ++fell;
            CHECK(!c.sites()[static_cast<size_t>(ruin)].searched);
            CHECK(!c.canSearch(id));
            continue;
        }
        ++won;
        CHECK(c.sites()[static_cast<size_t>(ruin)].searched);
        CHECK(hero->kills > 0);
        const bool gotGold = c.sides()[0].gold > goldBefore, gotArtifact = !hero->artifacts.empty(),
                   gotAllies = after->armies.size() > before.armies.size();
        CHECK(gotGold + gotArtifact + gotAllies == 1);  // one reward
        gold += gotGold;
        artifacts += gotArtifact;
        allies += gotAllies;
        if (gotArtifact) CHECK(Campaign::strength(*hero) + Campaign::command(*hero) > 4 + hero->rank);
        for (const Army& a : after->armies)
            if (!a.hero() && a.ally) CHECK(a.type == UnitType::Undead || a.type == UnitType::Wizards || a.type == UnitType::Devils ||
                                           a.type == UnitType::Archons || a.type == UnitType::Dragons);
        // Once: the ruin is empty now.
        const int32_t g = c.sides()[0].gold;
        CHECK(!c.canSearch(id));
        CHECK(!c.search(id));
        CHECK(c.sides()[0].gold == g);
        CHECK(!c.deeds().empty());
    }
    std::printf("  ruins: %d won (%d gold, %d artifacts, %d allies), %d heroes fell\n", won, gold, artifacts, allies, fell);
    CHECK(won > 0);
}

TEST(allies_serve_without_pay) {
    Army dragons;
    dragons.type = UnitType::Dragons;
    CHECK(Campaign::armyUpkeep(dragons) > 0);
    dragons.ally = true;
    CHECK(Campaign::armyUpkeep(dragons) == 0);
}

TEST(a_temple_blesses_each_army_once) {
    Campaign c(continent(), 2);
    // Nobody else moves: a computer hero standing on the temple would make it a battle.
    for (size_t s = 0; s < c.sides().size(); ++s) c.setHuman(static_cast<uint8_t>(s), true);
    // A stack in the city nearest a temple, marched there (any stack is
    // blessed, hero or not; mountains close some routes, so try each temple).
    uint32_t id = 0;
    int32_t temple = -1;
    for (size_t k = 0; k < c.sites().size() && temple < 0; ++k) {
        const Campaign::SiteState st = c.sites()[k];
        if (st.kind != Kind::Temple) continue;
        int32_t best = 1 << 30, city = -1;
        for (size_t i = 0; i < c.cities().size(); ++i) {
            const int32_t d = std::max(std::abs(c.cities()[i].cx - st.cx), std::abs(c.cities()[i].cy - st.cy));
            if (d < best) best = d, city = static_cast<int32_t>(i);
        }
        id = c.grant(city, 0, UnitType::LightInfantry, 2);
        if (march(c, id, st.cx, st.cy)) temple = static_cast<int32_t>(k);
    }
    CHECK(temple >= 0);
    if (temple < 0) return;
    std::vector<int32_t> before;
    for (const Army& a : c.findStack(id)->armies) before.push_back(Campaign::strength(a));
    CHECK(c.canSearch(id));
    CHECK(c.search(id));
    const Campaign::Stack* after = c.findStack(id);
    CHECK(after->armies.size() == before.size());
    for (size_t k = 0; k < after->armies.size() && k < before.size(); ++k) {
        CHECK(after->armies[k].blessings == 1);
        CHECK(Campaign::strength(after->armies[k]) == std::min(9, before[k] + 1));
    }
    // Only once per temple.
    CHECK(!c.canSearch(id));
    CHECK(!c.search(id));
    for (const Army& a : c.findStack(id)->armies) CHECK(a.blessings == 1);
}

TEST(the_computer_explores_and_hires_heroes) {
    Campaign c(continent(), 4);
    for (int t = 0; t < Campaign::kTicksPerDay * 30; ++t) c.step();
    int searched = 0, hired = 0, blessed = 0, found = 0, fell = 0;
    for (const Campaign::SiteState& s : c.sites()) searched += s.kind == Kind::Ruin && s.searched;
    for (const Campaign::Deed& d : c.deeds()) {
        hired += d.text.find("took service") != std::string::npos;
        blessed += d.text.find("blessed") != std::string::npos;
        found += d.text.find(" found the ") != std::string::npos;
        fell += d.text.find(" fell to ") != std::string::npos;
    }
    std::printf("  30 days: %d ruins searched (%d artifacts), %d heroes fell, %d hired, %d blessings\n", searched, found, fell, hired, blessed);
    CHECK(searched >= 4);
    CHECK(hired >= 1);
}

TEST(the_player_is_offered_a_hero_for_gold) {
    // Side 0 takes two more towns (heroes serve lords of more than two cities
    // each), then waits for a hero to offer their sword.
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    const Campaign::Stack* home = heroStack(c);
    CHECK(home != nullptr);
    if (!home) return;
    const uint32_t id = home->id;
    for (int town = 0; town < 2; ++town) {
        const Campaign::Stack* s = c.findStack(id);
        int32_t target = -1, best = 1 << 30;
        for (size_t i = 0; i < c.cities().size(); ++i) {
            const Campaign::CityState& city = c.cities()[i];
            const int32_t d = std::abs(city.cx - s->cx) + std::abs(city.cy - s->cy);
            if (city.owner == Campaign::kNeutral && d < best && c.move(id, city.cx, city.cy)) {
                best = d;
                target = static_cast<int32_t>(i);
            }
        }
        CHECK(c.move(id, c.cities()[static_cast<size_t>(target)].cx, c.cities()[static_cast<size_t>(target)].cy));
        for (int t = 0; t < Campaign::kTicksPerDay * 20 && !c.pending(); ++t) c.step();
        CHECK(c.pending() != nullptr);
        if (!c.pending()) return;
        std::vector<Campaign::Outcome> out;  // an easy win in 3D
        for (const Army* a : c.pendingAttackers()) out.push_back({a->id, a->soldiers, 0, a->rank});
        for (const Army* a : c.pendingDefenders()) out.push_back({a->id, 0, 0, a->rank});
        c.applyBattle(out, true);
    }
    CHECK(c.citiesOwned(0) == 3);
    // The other lords hold still meanwhile (else one may retake a town).
    for (uint8_t side = 1; side < Continent::kFactionCount; ++side) c.setHuman(side, true);
    for (int t = 0; t < Campaign::kTicksPerDay * 60 && !c.offer(0).open; ++t) c.step();
    CHECK(c.offer(0).open);
    if (!c.offer(0).open) return;
    const Campaign::Offer o = c.offer(0);
    std::printf("  day %u: %s offers their sword for %d gold\n", c.day() + 1, o.name.c_str(), o.price);
    CHECK(o.price >= 250);  // dearer with a hero already on the payroll
    const auto heroes = [&] {
        int n = 0;
        for (const Campaign::Stack& s : c.stacks())
            if (s.owner == 0)
                for (const Army& a : s.armies) n += a.hero();
        return n;
    };
    const int32_t gold = c.sides()[0].gold;
    const int n = heroes();
    if (gold < o.price) {
        CHECK(!c.hire(0, true));  // cannot afford it
        CHECK(c.hire(0, false));
        CHECK(!c.offer(0).open);
        return;
    }
    CHECK(c.hire(0, true));
    CHECK(heroes() == n + 1);
    CHECK(c.sides()[0].gold == gold - o.price);
    CHECK(!c.offer(0).open);
    CHECK(!c.hire(0, true));
}

TEST(quests_are_deterministic) {
    const auto run = [] {
        Campaign c(continent(), 9);
        for (int t = 0; t < Campaign::kTicksPerDay * 25; ++t) c.step();
        std::string log;
        for (const Campaign::Deed& d : c.deeds()) log += d.text + "\n";
        return std::make_pair(c.checksum(), log);
    };
    const auto a = run(), b = run();
    CHECK(a.first == b.first);
    CHECK(a.second == b.second);
    CHECK(!a.second.empty());
}
