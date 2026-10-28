// Common and rare troops (docs/GAME_DESIGN.md §4.2.4, "Every soldier counts").
// Cities train only common troops, slowly and dearly. Rare ones (heroes,
// beasts, monsters, the allies of the ruins, Myth's specialists) are never
// trained: they march with each realm's hero at the start, wait in a few
// cities as a one-off levy, guard the temples, ride with heroes for hire and
// join heroes from the ruins. Each that falls is gone for good.
#include <algorithm>

#include "illyria/sim/campaign.h"
#include "illyria/sim/noise.h"

namespace illyria::sim {

namespace {

using T = UnitType;

// Each realm's rare troops: those beside its hero at the start, and the levy
// its capital holds (that many armies, mustered once each).
struct RealmRare {
    std::vector<UnitType> start;
    UnitType levy;
    int32_t levies;
};
const RealmRare& realmRare(uint8_t side) {
    static const RealmRare kRealms[] = {
        {{T::Pegasi}, T::Pegasi, 1},                      // Aurelian Throne: knights on winged horses
        {{T::Giants, T::Giants}, T::Giants, 1},           // Highcrag Giants
        {{T::Griffins, T::DwarfGrenadier}, T::DwarfGrenadier, 2},  // Ironhold Dwarves: griffin riders and demolitionists
        {{T::Minotaurs, T::Berserker}, T::Berserker, 2},  // Orcs of Grath
        {{T::Unicorns, T::Pegasi}, T::Unicorns, 1},       // Sylvaran Elves
        {{T::Elephants}, T::Elephants, 1},                // Sunreach Riders
        {{T::Wizards, T::Journeyman}, T::Journeyman, 2},  // Moonward Order: mages and healers
        {{T::Spiders, T::Minotaurs}, T::Spiders, 2},      // Lord Malvoren
    };
    return kRealms[side % 8];
}

// The levies a town may hold, after the land around it; about one town in twelve has one.
constexpr UnitType kHillLevies[] = {T::Giants, T::Griffins, T::Minotaurs};
constexpr UnitType kWoodLevies[] = {T::Spiders, T::Unicorns, T::Berserker};
constexpr UnitType kPlainLevies[] = {T::Elephants, T::Pegasi, T::DwarfGrenadier};
constexpr int32_t kLevyOdds = 12;

// Who rides with a hero for hire (one time in three), and the temples' guards.
constexpr UnitType kCompanions[] = {T::Griffins, T::Pegasi, T::Unicorns, T::Wizards, T::Berserker, T::DwarfGrenadier};
constexpr int32_t kCompanionCount = static_cast<int32_t>(sizeof kCompanions / sizeof kCompanions[0]);
struct Guard {
    UnitType type;
    int32_t count;
};
constexpr Guard kTempleGuards[] = {{T::Archons, 1}, {T::Journeyman, 2}, {T::Unicorns, 1}, {T::Pegasi, 1}};
// The allies found in ruins (Campaign::search), for sources().
constexpr UnitType kRuinAllies[] = {T::Undead, T::Wizards, T::Devils, T::Archons, T::Dragons, T::Demons};

Campaign::Option levy(UnitType t, int32_t count) {
    // Three days and five gold a point of strength to muster each army.
    Campaign::Option o{t, 3, 5 * warlordsStrength(t)};
    o.left = count;
    return o;
}

}  // namespace

bool Campaign::trainable(UnitType t) {
    switch (t) {
        case T::LightInfantry:
        case T::HeavyInfantry:
        case T::Archers:
        case T::Pikemen:
        case T::LightCavalry:
        case T::HeavyCavalry:
        case T::Dwarves:
        case T::WolfRiders:
        case T::Warrior:
        case T::Catapult: return true;  // siege engines: built at three capitals, or no wall would ever fall
        default: return false;
    }
}

uint8_t Campaign::sources(UnitType t) {
    if (t == T::Hero) return kFromStart | kFromHire;
    if (trainable(t)) return 0;
    uint8_t m = 0;
    for (uint8_t s = 0; s < 8; ++s) {
        const RealmRare& r = realmRare(s);
        for (UnitType u : r.start) m |= u == t ? kFromStart : 0;
        m |= r.levy == t ? kFromLevy : 0;
    }
    for (UnitType u : kHillLevies) m |= u == t ? kFromLevy : 0;
    for (UnitType u : kWoodLevies) m |= u == t ? kFromLevy : 0;
    for (UnitType u : kPlainLevies) m |= u == t ? kFromLevy : 0;
    for (UnitType u : kRuinAllies) m |= u == t ? kFromRuin : 0;
    for (const Guard& g : kTempleGuards) m |= g.type == t ? kFromTemple : 0;
    for (UnitType u : kCompanions) m |= u == t ? kFromHire : 0;
    return m;
}

UnitType Campaign::templeGuard(uint8_t bit, int32_t& count) {
    int32_t k = 0;
    while (bit > 1) {
        bit = static_cast<uint8_t>(bit >> 1);
        ++k;
    }
    const Guard& g = kTempleGuards[k % 4];
    count = g.count;
    return g.type;
}

void Campaign::initRare() {
    const uint64_t sl = noise::nextSeed(continent_.seed() ^ 0x1E7F);
    for (size_t i = 0; i < cities_.size(); ++i) {
        CityState& st = cities_[i];
        const Continent::City& c = continent_.cities()[i];
        if (c.capital) {
            if (st.owner == kNeutral) continue;
            const RealmRare& r = realmRare(st.owner);
            for (Stack& s : stacks_) {
                if (s.owner != st.owner || s.cx != st.cx || s.cy != st.cy) continue;
                for (UnitType t : r.start) s.armies.push_back(makeArmy(t, std::string(unitTypeName(t)) + " of " + c.name));
                break;
            }
            st.options.push_back(levy(r.levy, r.levies));
            continue;
        }
        if ((noise::lattice(sl, static_cast<int64_t>(i), 1) * kLevyOdds).floorToInt() != 0) continue;
        // The land decides what the levy is, as it does the town's training.
        const Continent::Sample s = continent_.sample(c.pos.x, c.pos.y);
        const Ground g = continent_.landGround(c.pos.x + Fixed::fromInt(300), c.pos.y, s.height, s.mountain, s.roadDist);
        const int32_t pick = static_cast<int32_t>((noise::lattice(sl, static_cast<int64_t>(i), 2) * 3).floorToInt());
        const UnitType t = s.mountain > Fixed::fromInt(15) ? kHillLevies[pick] : g == Ground::Forest ? kWoodLevies[pick] : kPlainLevies[pick];
        st.options.push_back(levy(t, warlordsStrength(t) >= 7 ? 1 : 2));
    }
}

void Campaign::templeGift(Stack& s, SiteState& site, const std::string& where) {
    if (site.gifted) return;
    const Army* hero = nullptr;
    for (const Army& a : s.armies)
        if (a.hero() && !hero) hero = &a;
    if (!hero) return;
    int32_t count = 0;
    const UnitType t = templeGuard(site.bit, count);
    const std::string who = hero->name.empty() ? std::string("a hero") : hero->name;
    const int32_t n = std::min(count, kMaxStack - static_cast<int32_t>(s.armies.size()));
    if (n <= 0) return;
    site.gifted = true;
    for (int32_t k = 0; k < n; ++k) {
        Army a = makeArmy(t, std::string(unitTypeName(t)) + " of " + where);
        a.ally = true;  // they serve the cause, not for pay
        a.temples = site.bit;  // and carry their temple's blessing
        a.blessings = 1;
        s.armies.push_back(a);
    }
    deed(s.owner, "the guard of " + where + " (" + unitTypeName(t) + ") went with " + who);
}

void Campaign::offerCompanions(Offer& o) {
    o.companion = UnitType::Count;
    o.companions = 0;
    if (rng_.nextBelow(3) != 0) return;
    o.companion = kCompanions[rng_.nextBelow(static_cast<uint32_t>(kCompanionCount))];
    o.companions = 1;
    o.price += 50;
}

void Campaign::hireCompanions(Stack*& home, uint8_t side, const Offer& o, int32_t city) {
    const CityState& c = cities_[static_cast<size_t>(city)];
    for (int32_t k = 0; k < o.companions; ++k) {
        if (home->armies.size() >= static_cast<size_t>(kMaxStack)) home = &newStack(side, c.cx, c.cy);
        Army a = makeArmy(o.companion, std::string(unitTypeName(o.companion)) + " of " + o.name);
        a.ally = true;  // they follow the hero, not the purse
        home->armies.push_back(a);
    }
}

}  // namespace illyria::sim
