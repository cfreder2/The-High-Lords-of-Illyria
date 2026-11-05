// Raising a banner (engine/sim/src/campaign_capture.cpp): a town changes hands
// only when the victors' banner has risen over it while they hold it; and
// the siege battle's capture point (game/src/battle_capture.cpp).
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/sim/campaign.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// A campaign where nobody moves but by our orders.
Campaign still(uint64_t seed) {
    Campaign c(continent(), seed);
    for (uint8_t s = 0; s < Continent::kFactionCount; ++s) c.setHuman(s, true);
    return c;
}

// The neutral town of the lowest walls nearest side 0's capital.
int32_t nearTown(const Campaign& c) {
    int32_t home = -1;
    for (size_t i = 0; i < c.cities().size(); ++i)
        if (c.cities()[i].owner == 0) home = static_cast<int32_t>(i);
    const Campaign::CityState& h = c.cities()[static_cast<size_t>(home)];
    int32_t best = -1, bestD = 1 << 30;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const Campaign::CityState& t = c.cities()[i];
        const int32_t d = std::abs(t.cx - h.cx) + std::abs(t.cy - h.cy) + 1000 * (t.defense - 3);
        if (t.owner == Campaign::kNeutral && d < bestD) {
            bestD = d;
            best = static_cast<int32_t>(i);
        }
    }
    return best;
}

// A stack of side 0 one step from the town, ordered into it: from open
// ground if there is any (a quick step in and out). 0 if no cell fits.
uint32_t besiege(Campaign& c, int32_t city, const std::vector<UnitType>& types, int32_t* fromX = nullptr, int32_t* fromY = nullptr) {
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(city)];
    for (int pass = 0; pass < 2; ++pass)
    for (int32_t dy = -1; dy <= 1; ++dy)
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const int32_t x = t.cx + dx, y = t.cy + dy;
            if ((dx == 0 && dy == 0) || c.terrainAt(x, y) == Lay::Water || c.terrainAt(x, y) == Lay::Mountain || c.cityAt(x, y) >= 0) continue;
            if (pass == 0 && c.terrainAt(x, y) != Lay::Plain && c.terrainAt(x, y) != Lay::Road) continue;
            const uint32_t id = c.raise(0, x, y, types);
            if (c.move(id, t.cx, t.cy) && c.findStack(id)->path.size() == 1) {
                if (fromX) *fromX = x;
                if (fromY) *fromY = y;
                return id;
            }
            for (const Army& a : std::vector<Army>(c.findStack(id)->armies)) c.disband(id, a.id);
        }
    return 0;
}

// Side 1's capital with nobody to hold it: every army but the hero's disbanded, and the hero's stack
// sent off. The index of the city.
int32_t emptyCapital(Campaign& c) {
    int32_t cap = -1;
    for (size_t i = 0; i < c.cities().size(); ++i)
        if (c.cities()[i].owner == 1) cap = static_cast<int32_t>(i);
    for (const Campaign::Stack& s : std::vector<Campaign::Stack>(c.stacks()))
        if (s.owner == 1)
            for (const Army& a : s.armies) c.disband(s.id, a.id);
    // Heroes cannot be disbanded: send the hero's stack off a little way.
    for (const Campaign::Stack& s : std::vector<Campaign::Stack>(c.stacks()))
        if (s.owner == 1 && !s.armies.empty()) {
            const Campaign::CityState& t = c.cities()[static_cast<size_t>(cap)];
            bool sent = false;
            for (int32_t r = 8; r < 24 && !sent; ++r)
                for (const auto& d : {std::make_pair(r, 0), std::make_pair(-r, 0), std::make_pair(0, r), std::make_pair(0, -r)}) {
                    const int32_t x = t.cx + d.first, y = t.cy + d.second;
                    if (x < 0 || y < 0 || x >= c.width() || y >= c.height() || c.cityAt(x, y) >= 0) continue;
                    if (c.move(s.id, x, y)) {
                        sent = true;
                        break;
                    }
                }
        }
    // A day for him to be well away (a stack within a long bowshot of another wakes to fight it).
    for (int k = 0; k < Campaign::kTicksPerDay; ++k) c.step();
    return cap;
}

// A stack of side 0 raised on open ground two cells from the town, with a way in; its id, and where it stands.
uint32_t raiseNear(Campaign& c, int32_t city, const std::vector<UnitType>& types, int32_t* fromX = nullptr, int32_t* fromY = nullptr) {
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(city)];
    for (int32_t r = 2; r <= 3; ++r)
        for (int32_t dy = -r; dy <= r; ++dy)
            for (int32_t dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const int32_t x = t.cx + dx, y = t.cy + dy;
                if (x < 0 || y < 0 || x >= c.width() || y >= c.height() || c.cityAt(x, y) >= 0) continue;
                if (c.terrainAt(x, y) != Lay::Plain && c.terrainAt(x, y) != Lay::Road) continue;
                const uint32_t id = c.raise(0, x, y, types);
                if (c.move(id, t.cx, t.cy)) {
                    if (fromX) *fromX = x;
                    if (fromY) *fromY = y;
                    return id;
                }
                for (const Army& a : std::vector<Army>(c.findStack(id)->armies)) c.disband(id, a.id);
            }
    return 0;
}

// Marches the stack into the town (nobody in the way: it never wakes), until it stands in it.
bool marchIn(Campaign& c, uint32_t id, int32_t city) {
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(city)];
    if (!c.move(id, t.cx, t.cy)) return false;
    for (int k = 0; k < Campaign::kTicksPerDay; ++k) {
        const Campaign::Stack* s = c.findStack(id);
        if (s && s->cx == t.cx && s->cy == t.cy && s->path.empty()) return true;
        c.step();
    }
    return false;
}

// Ticks until the town is ours (or `most`).
int32_t untilTaken(Campaign& c, int32_t city, int32_t most = Campaign::kTicksPerDay) {
    int32_t t = 0;
    while (t < most && c.cities()[static_cast<size_t>(city)].owner != 0) {
        c.step();
        ++t;
    }
    return t;
}

// Orders the stack in and times the banner: from the moment it goes up (it may already be rising before the stack
// has marched in) to the town's fall; -1 if either does not happen in two days.
int32_t bannerTime(Campaign& c, uint32_t id, int32_t city) {
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(city)];
    if (!id || !c.move(id, t.cx, t.cy)) return -1;
    int32_t first = -1, taken = -1;
    for (int32_t k = 0; k < 2 * Campaign::kTicksPerDay; ++k) {
        if (first < 0 && t.banner != Campaign::kNeutral) first = k;
        if (t.owner == 0) {
            taken = k;
            break;
        }
        c.step();
    }
    return first < 0 || taken < 0 ? -1 : taken - first;
}
}  // namespace

TEST(a_town_is_taken_only_when_the_banner_has_risen) {
    Campaign c = still(3);
    const int32_t town = emptyCapital(c);
    const UnitType hc = UnitType::HeavyCavalry;
    const uint32_t id = raiseNear(c, town, {hc, hc, hc, hc, hc, hc});
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(town)];
    CHECK(t.owner == 1);  // nobody held it, and it is not ours until the banner is up
    const int32_t armies = static_cast<int32_t>(c.findStack(id)->armies.size());
    const int32_t ticks = bannerTime(c, id, town);
    std::printf("  %s: %d armies raised the banner in %d ticks (%.1f hours)\n", continent().cities()[static_cast<size_t>(town)].name.c_str(), armies,
                ticks, ticks * 24.0 / Campaign::kTicksPerDay);
    CHECK(t.owner == 0);
    CHECK(t.banner == Campaign::kNeutral);
    // Kept up by the armies standing there: the full banner over (2 + armies) a tick, seen every half second.
    const int32_t whole = Campaign::kBannerFull / (Campaign::kBannerBase + armies);
    CHECK(std::abs(ticks - whole) <= 2 * Campaign::kContactTicks);
    bool news = false;
    for (const Campaign::Deed& d : c.deeds()) news = news || (d.side == 0 && d.text.find("Our banner flies over") != std::string::npos);
    CHECK(news);
    CHECK(c.fights().size() == 1 && c.fights().back().city == town);
}

TEST(more_troops_raise_the_banner_faster) {
    const auto raiseWith = [](int extra) {
        Campaign c = still(3);
        const int32_t town = emptyCapital(c);
        std::vector<UnitType> types{UnitType::Dragons};
        for (int k = 0; k < extra; ++k) types.push_back(UnitType::HeavyCavalry);
        return bannerTime(c, raiseNear(c, town, types), town);
    };
    const int32_t one = raiseWith(0), eight = raiseWith(7);
    std::printf("  a lone dragon raises the banner in %d ticks, a full stack in %d\n", one, eight);
    CHECK(one > 0 && eight > 0);
    CHECK(one >= eight * 3);
    CHECK(one <= Campaign::kTicksPerDay);  // within the day: a few hours
}

TEST(a_banner_does_not_rise_while_the_towns_men_stand_in_it) {
    // Storm a neutral town: at every look, its banner is up only while none of the garrison's men are in the courtyard.
    Campaign c = still(3);
    const int32_t town = nearTown(c);
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(town)];
    const Continent::City& city = continent().cities()[static_cast<size_t>(town)];
    const uint32_t id = besiege(c, town, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry});
    CHECK(id != 0);
    std::vector<Campaign::Soldier> men;
    bool raised = false, never = true;
    for (int k = 0; k < Campaign::kTicksPerDay * 4 && t.owner != 0; ++k) {
        c.step();
        if (k % Campaign::kContactTicks) continue;
        raised = raised || t.banner == 0;
        if (t.banner != 0) continue;
        c.soldiers(0, men);
        for (const Campaign::Soldier& m : men) {
            const float dx = m.x - static_cast<float>(city.castle.courtyard.x.toDouble()), dy = m.y - static_cast<float>(city.castle.courtyard.y.toDouble());
            if (m.owner == Campaign::kNeutral && m.alive && dx * dx + dy * dy < 40.0f * 40.0f) never = never && t.bannerProgress == 0;
        }
    }
    CHECK(never);
    std::printf("  banner rose over %s: %s, owner %d\n", city.name.c_str(), raised ? "yes" : "no", t.owner);
}

TEST(leaving_the_town_abandons_the_banner) {
    Campaign c = still(3);
    const int32_t town = emptyCapital(c);
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(town)];
    const UnitType hc = UnitType::HeavyCavalry;
    int32_t fx = 0, fy = 0;
    const uint32_t id = raiseNear(c, town, {hc, hc, hc}, &fx, &fy);
    CHECK(marchIn(c, id, town));
    for (int k = 0; k < 40; ++k) c.step();
    CHECK(t.bannerProgress > 0);
    CHECK(c.move(id, fx, fy));
    for (int k = 0; k < Campaign::kTicksPerDay && (c.findStack(id)->cx != fx || c.findStack(id)->cy != fy); ++k) c.step();
    for (int k = 0; k < 2 * Campaign::kContactTicks; ++k) c.step();
    CHECK(t.banner == Campaign::kNeutral);
    CHECK(t.bannerProgress == 0);
    CHECK(t.owner == 1);
    // Marching back in: nobody defends it now, so the banner starts again from nothing.
    CHECK(marchIn(c, id, town));
    for (int k = 0; k < 2 * Campaign::kContactTicks && t.banner == Campaign::kNeutral; ++k) c.step();
    CHECK(t.banner == 0);
    CHECK(untilTaken(c, town) < Campaign::kTicksPerDay);
}

TEST(a_town_being_taken_trains_nothing) {
    // Side 1's capital, emptied, with side 0 raising its banner in it.
    Campaign c = still(4);
    const int32_t cap = emptyCapital(c);
    c.produce(cap, 0);
    const UnitType hc = UnitType::HeavyCavalry;
    const uint32_t id = raiseNear(c, cap, {hc, hc, hc, hc, hc, hc, hc, hc});
    CHECK(marchIn(c, id, cap));
    for (int t = 0; t < Campaign::kTicksPerDay && c.cities()[static_cast<size_t>(cap)].banner != 0; ++t) c.step();
    const Campaign::CityState& t = c.cities()[static_cast<size_t>(cap)];
    CHECK(t.banner == 0);
    CHECK(t.owner == 1);
    CHECK(c.defendersAt(0, t.cx, t.cy).empty());  // nobody of theirs appeared in it
    const int32_t gold = c.sides()[1].gold;
    untilTaken(c, cap);
    CHECK(t.owner == 0);
    CHECK(c.sides()[1].gold <= gold);  // the treasury's share went with it
}

TEST(banners_are_deterministic) {
    const auto run = [] {
        Campaign c(continent(), 9);
        for (uint8_t s = 0; s < Continent::kFactionCount; ++s) c.setHuman(s, true);
        const int32_t town = emptyCapital(c);
        const UnitType hc = UnitType::HeavyCavalry;
        const uint32_t id = raiseNear(c, town, {hc, hc, hc, hc});
        marchIn(c, id, town);
        for (int k = 0; k < 40; ++k) c.step();
        return std::make_pair(c.checksum(), c.cities()[static_cast<size_t>(town)].bannerProgress);
    };
    const auto a = run(), b = run();
    CHECK(a == b);
    CHECK(a.second > 0);
}

// ---------------------------------------------------------------- the 3D capture point

namespace {
using illyria::game::CapturePoint;
Fixed m(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEast{Fixed::one(), Fixed::zero(), Fixed::zero()};

// Soldiers who stand still where they are put.
UnitStats statue() {
    UnitStats s = unitStats(UnitType::LightInfantry);
    s.aggroRange = Fixed::zero();
    return s;
}
}  // namespace

TEST(the_side_with_more_men_in_the_courtyard_raises_its_flag) {
    World w(WorldConfig{});
    CapturePoint cap;
    cap.place({m(50), m(50), m(0)}, 1, 0);
    // Six attackers in the courtyard, two defenders: the flag climbs 2 + 4 a tick.
    for (int k = 0; k < 6; ++k) w.spawnUnit(0, {m(46 + k), m(48), m(0)}, kEast, statue());
    std::vector<EntityId> def;
    for (int k = 0; k < 2; ++k) def.push_back(w.spawnUnit(1, {m(48 + k), m(53), m(0)}, kEast, statue()));
    w.spawnUnit(1, {m(80), m(50), m(0)}, kEast, statue());  // one far off: he does not count
    w.step({});
    cap.step(w);
    CHECK(cap.attackersIn() == 6 && cap.defendersIn() == 2);
    CHECK(cap.progress() == CapturePoint::kBase + 4);
    int ticks = 1;
    while (!cap.taken() && ticks < 1000) {
        w.step({});
        cap.step(w);
        ++ticks;
    }
    std::printf("  six against two: the courtyard is taken in %.1f s\n", ticks / 10.0);
    CHECK(cap.taken());
    CHECK(ticks == (CapturePoint::kFull + 5) / 6);
}

TEST(even_numbers_hold_the_flag_and_more_defenders_pull_it_down) {
    World w(WorldConfig{});
    CapturePoint cap;
    cap.place({m(50), m(50), m(0)}, 1, 0);
    std::vector<EntityId> att;
    for (int k = 0; k < 4; ++k) att.push_back(w.spawnUnit(0, {m(46 + k), m(48), m(0)}, kEast, statue()));
    for (int t = 0; t < 50; ++t) cap.step(w);
    const int32_t up = cap.progress();
    CHECK(up == 50 * (CapturePoint::kBase + 4));
    for (int k = 0; k < 4; ++k) w.spawnUnit(1, {m(46 + k), m(53), m(0)}, kEast, statue());
    w.step({});
    for (int t = 0; t < 50; ++t) cap.step(w);
    CHECK(cap.progress() == up);  // four and four: held
    for (int k = 0; k < 3; ++k) w.spawnUnit(1, {m(50 + k), m(53), m(0)}, kEast, statue());
    w.step({});
    for (int t = 0; t < 20; ++t) cap.step(w);
    CHECK(cap.progress() == up - 20 * (CapturePoint::kBase + 3));
    CHECK(!cap.taken());
}

TEST(a_siege_has_a_capture_point_in_the_courtyard) {
    illyria::game::Battle siege(5, illyria::game::Scenario::Siege);
    const CapturePoint& cap = siege.capturePoint();
    CHECK(cap.active());
    CHECK(cap.progress() == 0);
    CHECK(siege.outcome() == 0);
    // In the courtyard: within the town's walls, at its courtyard.
    const Continent::City& town = illyria::game::siegeTown(siege.continent());
    const Vec3 local = town.castle.courtyard - siege.region().origin();
    CHECK(((cap.at() - local).flat()).length() < m(1));
    // Its defenders hold it to begin with.
    siege.advance(0.5);
    CHECK(cap.defendersIn() > 0 && cap.attackersIn() == 0);
    illyria::game::Battle ford(5, illyria::game::Scenario::Ford);
    CHECK(!ford.capturePoint().active());
}
