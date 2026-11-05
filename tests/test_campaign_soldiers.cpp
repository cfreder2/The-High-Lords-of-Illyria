// The campaign's soldiers on the land (engine/sim/src/campaign_world.cpp, docs/ONE_WORLD.md): a man for every soldier of
// every stack, placed from the campaign's state alone, along the stack's route and in
// its town's courtyard, and none of it written back into the simulation.
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "illyria/game/battle.h"
#include "illyria/sim/campaign.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// A campaign a few days old, the computer lords' stacks out on the march.
Campaign started(uint64_t seed, int days) {
    Campaign c(continent(), seed);
    c.setHuman(0, true);
    for (int t = 0; t < days * Campaign::kTicksPerDay; ++t) c.step();
    return c;
}
}  // namespace

TEST(every_living_soldier_of_every_stack_on_land_is_drawn_once_where_his_stack_is) {
    Campaign c = started(7, 3);
    std::vector<Campaign::Soldier> men;
    c.soldiers(static_cast<int32_t>(0), men);
    size_t want = 0;
    for (const Campaign::Stack& s : c.stacks())
        if (!s.boat)
            for (const Army& a : s.armies) want += static_cast<size_t>(a.soldiers);
    size_t alive = 0;
    std::set<uint32_t> ids;
    for (const Campaign::Soldier& m : men) {
        alive += m.alive;
        CHECK(ids.insert(m.id).second);  // each man once
    }
    CHECK(alive == want);
    CHECK(want > 1000);  // every town's garrison, and the lords' armies
    // Each stands near his stack's middle (a column of squads is a hundred metres at most).
    std::map<uint32_t, std::pair<double, double>> middle;
    std::map<uint32_t, int> count;
    for (const Campaign::Soldier& m : men) {
        middle[m.stack].first += m.x, middle[m.stack].second += m.y, ++count[m.stack];
    }
    for (const Campaign::Soldier& m : men) {
        const Campaign::Stack* st = c.findStack(m.stack);
        CHECK(st != nullptr);
        if (!st) continue;
        CHECK(m.owner == st->owner);
        const double dx = m.x - middle[m.stack].first / count[m.stack], dy = m.y - middle[m.stack].second / count[m.stack];
        CHECK(std::hypot(dx, dy) < 150.0);
    }
}

TEST(laying_out_the_soldiers_changes_nothing_in_the_campaign) {
    Campaign c = started(11, 2);
    const uint64_t before = c.checksum();
    std::vector<Campaign::Soldier> men;
    for (int k = 0; k < 5; ++k) c.soldiers(static_cast<int32_t>(50.0 * k), men);
    CHECK(c.checksum() == before);
    // Nor does it depend on anything but the campaign: a man's place is the same on every call.
    std::vector<Campaign::Soldier> again;
    c.soldiers(static_cast<int32_t>(25.0), men);
    c.soldiers(static_cast<int32_t>(25.0), again);
    CHECK(men.size() == again.size());
    for (size_t i = 0; i < men.size() && i < again.size(); ++i) CHECK(men[i].x == again[i].x && men[i].y == again[i].y && men[i].id == again[i].id);
}

TEST(a_stack_in_a_town_stands_in_its_courtyard_and_a_marching_one_between_its_ends) {
    Campaign c = started(13, 3);
    int garrisons = 0, marching = 0;
    std::vector<Campaign::Soldier> men;
    c.soldiers(static_cast<int32_t>(0), men);
    for (const Campaign::Stack& s : c.stacks()) {
        if (s.armies.empty() || s.boat || s.awake) continue;
        double sx = 0, sy = 0;
        int n = 0;
        for (const Campaign::Soldier& m : men)
            if (m.stack == s.id) sx += m.x, sy += m.y, ++n;
        if (n == 0) continue;
        sx /= n, sy /= n;
        const int32_t town = c.cityAt(s.cx, s.cy);
        if (s.path.empty() && town >= 0) {
            const Continent::Castle& k = c.continent().cities()[static_cast<size_t>(town)].castle;
            // (a block of squads round the courtyard's middle; several stacks stand side by side)
            CHECK(std::hypot(sx - k.courtyard.x.toDouble(), sy - k.courtyard.y.toDouble()) < 40.0);
            for (const Campaign::Soldier& m : men)
                if (m.stack == s.id) CHECK(m.speed == 0);
            ++garrisons;
        }
        if (!s.path.empty() && c.legTime(s) != Campaign::kClosed) {
            // On the line between where it set out from and the next cell, going at its leg's pace.
            int32_t ax, ay, bx, by;
            Campaign::Stack first = s, last = s;
            first.progress = 0;
            last.progress = c.legTime(s);
            c.bodyOf(first, 0, ax, ay);
            c.bodyOf(last, 0, bx, by);
            int32_t px, py;
            c.bodyOf(s, 0, px, py);
            const double whole = std::hypot(double(bx - ax), double(by - ay)) / 1000.0;
            const double there = std::hypot(double(px - ax), double(py - ay)) / 1000.0 + std::hypot(double(bx - px), double(by - py)) / 1000.0;
            CHECK_NEAR(there, whole, 0.01);
            float speed = 0;
            for (const Campaign::Soldier& m : men)
                if (m.stack == s.id) speed = m.speed;
            CHECK(speed > 0.3f && speed < 20.0f);  // a catapult's 0.8 m/s on plains to a wolf rider's 15 on a road
            ++marching;
        }
    }
    CHECK(garrisons > 60);
    CHECK(marching > 0);
}

TEST(stacks_standing_in_one_town_stand_side_by_side_not_on_each_other) {
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    const Campaign::CityState& home = c.cities()[0];
    const uint32_t a = c.raise(0, home.cx, home.cy, {UnitType::HeavyInfantry});
    const uint32_t b = c.raise(0, home.cx, home.cy, {UnitType::HeavyInfantry});
    std::vector<Campaign::Soldier> men;
    c.soldiers(static_cast<int32_t>(0), men);
    double ax = 0, ay = 0, bx = 0, by = 0;
    int na = 0, nb = 0;
    for (const Campaign::Soldier& m : men) {
        if (m.stack == a) ax += m.x, ay += m.y, ++na;
        if (m.stack == b) bx += m.x, by += m.y, ++nb;
    }
    // A squad of twelve is a block of four files and three ranks.
    CHECK(na == 12 && nb == 12);
    if (na && nb) CHECK(std::hypot(ax / na - bx / nb, ay / na - by / nb) > 6.0);
}

// What is drawn is continuous: sampled twice a tick (a frame at 60 a second against the clock's 30), no man jumps
// further than his pace can carry him, across a march, a wake (the stack meets a town's garrison), the fight, and
// the sleep after it. `worst` is the biggest jump of any man beyond what his speed explains.
TEST(a_mans_drawn_place_is_continuous_across_a_march_a_wake_and_a_sleep) {
    Campaign c(continent(), 3);
    for (uint8_t s = 0; s < 8; ++s) c.setHuman(s, true);  // the lords stay home: only the scripted march moves
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) home = &st;
    int target = -1, best = 1 << 30;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const auto& city = c.cities()[i];
        if (city.owner != Campaign::kNeutral || city.defense > 4) continue;
        const int d = std::abs(city.cx - home->cx) + std::abs(city.cy - home->cy);
        if (d < best) best = d, target = static_cast<int>(i);
    }
    const auto& town = c.cities()[static_cast<size_t>(target)];
    c.raise(0, home->cx, home->cy, {UnitType::Hero, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers});
    for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
        if (st.owner == 0 && !st.garrison) c.move(st.id, town.cx, town.cy);
    std::map<uint32_t, Campaign::Soldier> last;
    std::vector<Campaign::Soldier> now;
    double worst = 0, worstMetres = 0;
    uint32_t worstTick = 0, worstId = 0;
    int jumps = 0, wakeJumps = 0, sleepJumps = 0, awakeJumps = 0, marchJumps = 0;
    bool woke = false, slept = false, wasAwake = false;
    const double dt = 1.0 / 60.0;
    for (int k = 0; k < 24000; ++k) {
        for (const int32_t ahead : {0, static_cast<int32_t>(Campaign::kTimeScale / 2)}) {
            c.soldiers(static_cast<int32_t>(ahead), now);
            for (const Campaign::Soldier& m : now) {
                if (!m.alive) continue;
                const auto it = last.find(m.id);
                if (it != last.end()) {
                    const double jump = std::hypot(m.x - it->second.x, m.y - it->second.y);
                    const double allowed = std::max<double>(m.speed, it->second.speed) * dt * 2.0 + 0.25;
                    if (jump > allowed) {
                        ++jumps;
                        if (!it->second.awake && m.awake) ++wakeJumps;
                        else if (it->second.awake && !m.awake) ++sleepJumps;
                        else if (m.awake) ++awakeJumps;
                        else {
                            ++marchJumps;
                            if (marchJumps <= 6 || jump > 6.0) {
                                const Campaign::Stack* st = c.findStack(m.stack);
                                std::printf("    asleep jump %.2f m (allowed %.2f) man %u stack %u tick %u path %zu owner %d cell %d,%d\n", jump, allowed, m.id, m.stack, c.tick(),
                                            st ? st->path.size() : 0, m.owner, st ? st->cx : -1, st ? st->cy : -1);
                            }
                        }
                    }
                    if (jump - allowed > worst) worst = jump - allowed, worstMetres = jump, worstTick = c.tick(), worstId = m.id;
                }
                last[m.id] = m;
            }
        }
        const bool awake = c.awakeStacks() > 0;
        woke = woke || awake;
        slept = slept || (wasAwake && !awake);
        wasAwake = awake;
        c.step();
    }
    std::printf("  worst jump beyond pace: %.2f m (a jump of %.2f m, man %u, tick %u); woke %d, slept again %d\n", worst, worstMetres, worstId, worstTick,
                woke, slept);
    std::printf("  %d jumps: %d at a wake, %d at a sleep, %d awake, %d asleep\n", jumps, wakeJumps, sleepJumps, awakeJumps, marchJumps);
    CHECK(woke);
    CHECK(worst <= 0.1);  // (a tenth of a metre of rounding: the sim's millimetres and the clock's halves of a tick)
}

// There is one battle system: the Battle presents the campaign's fight as it presents its own, so everything the battle
// has (blood on the ground, the deaths of its men, its sounds and shouts) appears in the campaign's fights with no
// copy of any of it. The field is made on the island's origin; the campaign tells it round every step of the world.
TEST(the_battle_presents_the_campaigns_fight_as_its_own) {
    Campaign c(continent(), 3);
    for (uint8_t s = 0; s < 8; ++s) c.setHuman(s, true);
    c.prepareWorld();
    World* world = c.presentedWorld();
    const Vec3 ro = world->region()->origin();
    const Vec3 centre{ro.x + Fixed::fromInt(512), ro.y + Fixed::fromInt(512), Fixed::zero()};
    illyria::game::Battle field(1, centre, std::shared_ptr<const Continent>(&continent(), [](const Continent*) {}));
    CHECK(field.origin().x == static_cast<float>(ro.x.toDouble()));  // the world's places are the battle's
    CHECK(field.origin().y == static_cast<float>(ro.y.toDouble()));
    field.borrow(world);
    CHECK(field.borrowing());
    c.onWorldStep([&] { field.beforeWorldStep(); }, [&] { field.afterWorldStep(); }, [&] { field.forgetMen(); });
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& st : c.stacks())
        if (st.owner == 0) home = &st;
    int target = -1, best = 1 << 30;
    for (size_t i = 0; i < c.cities().size(); ++i) {
        const auto& city = c.cities()[i];
        if (city.owner != Campaign::kNeutral || city.defense > 4) continue;
        const int d = std::abs(city.cx - home->cx) + std::abs(city.cy - home->cy);
        if (d < best) best = d, target = static_cast<int>(i);
    }
    const auto& town = c.cities()[static_cast<size_t>(target)];
    c.raise(0, home->cx, home->cy, {UnitType::Hero, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers});
    for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
        if (st.owner == 0 && !st.garrison) c.move(st.id, town.cx, town.cy);
    size_t cues = 0, said = 0;
    for (int k = 0; k < 9000; ++k) {
        c.step();
        cues += field.sfx().take().size();
        said += field.voice().take().size();
    }
    size_t dead = 0, struck = 0;
    for (size_t i = 0; i < world->units().size(); ++i) {
        dead += field.deathTick(i) != UINT32_MAX;
        struck += field.hitTick(i) != UINT32_MAX || field.attackTick(i) != UINT32_MAX;
    }
    std::printf("  the battle shows the campaign's fight: %zu men struck or striking, %zu dead, %zu ground marks, %zu sounds, %zu shouts\n", struck, dead,
                field.groundMarks().size(), cues, said);
    CHECK(struck > 0);
    CHECK(field.groundMarks().size() > 0);  // blood on the ground
    CHECK(cues > 0);                        // the battle's sounds
}

// Sight has one seam (Campaign::canSee): what a side is shown that is not its own goes through it. With a stub that
// hides side 2, no list of men, no picking and no drawing reports a man of side 2; the side's own men still show.
TEST(a_side_hidden_by_sight_is_in_no_list_of_men_and_cannot_be_picked) {
    Campaign c(continent(), 3);
    for (uint8_t s = 0; s < 8; ++s) c.setHuman(s, true);
    std::vector<Campaign::Soldier> all, seen;
    c.soldiers(0, all);
    c.visibleSoldiers(0, 0, seen);
    CHECK(seen.size() == all.size());  // until sight is built, everyone is seen
    c.setSight([](uint8_t viewer, uint8_t owner, float, float) { return owner == viewer || owner != 2; });
    c.visibleSoldiers(0, 0, seen);
    size_t two = 0, mine = 0, hidden = 0;
    for (const Campaign::Soldier& m : all) two += m.owner == 2;
    for (const Campaign::Soldier& m : seen) {
        hidden += m.owner == 2;
        mine += m.owner == 0;
    }
    CHECK(two > 0);
    CHECK(hidden == 0);
    CHECK(mine > 0);
    CHECK(!c.canSee(0, 2, 100.0f, 100.0f));
    CHECK(c.canSee(2, 2, 100.0f, 100.0f));
    CHECK(c.canSee(0, 1, 100.0f, 100.0f));
    // The field the page draws and picks from asks the same: a man of side 2 on its sleeper list is not picked.
    const Vec3 ro = Vec3{Fixed::fromInt(-378), Fixed::fromInt(-22), Fixed::zero()};
    illyria::game::Battle field(1, Vec3{ro.x + Fixed::fromInt(512), ro.y + Fixed::fromInt(512), Fixed::zero()},
                                std::shared_ptr<const Continent>(&continent(), [](const Continent*) {}));
    field.setSight([&](uint8_t owner, float x, float y) { return c.canSee(0, owner, x + field.origin().x, y + field.origin().y); });
    CHECK(field.sees(0, 10, 10));
    CHECK(!field.sees(2, 10, 10));
}
