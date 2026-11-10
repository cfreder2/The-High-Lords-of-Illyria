// The campaign's soldiers on the land (engine/sim/src/campaign_world.cpp, docs/ONE_WORLD.md): a man for every soldier of
// every stack, placed from the campaign's state alone, along the stack's route and in
// its town's courtyard, and none of it written back into the simulation.
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

#include "illyria/game/battle.h"
#include "illyria/sim/campaign.h"
#include "campaign_orders.h"
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
    // (But a stack that has fought: its broken men run, and a village's levy has no wall to run behind.)
    std::set<uint32_t> fought;
    for (const Campaign::Soldier& m : men)
        if (m.fightTick != 0 || m.hurtTick != 0) fought.insert(m.stack);
    for (const Campaign::Soldier& m : men) {
        const Campaign::Stack* st = c.findStack(m.stack);
        CHECK(st != nullptr);
        if (!st) continue;
        CHECK(m.owner == st->owner);
        if (fought.count(m.stack)) continue;
        const double dx = m.x - middle[m.stack].first / count[m.stack], dy = m.y - middle[m.stack].second / count[m.stack];
        if (std::hypot(dx, dy) >= 150.0) std::printf("  a man of stack %u (owner %d, cell %d, %d) stands %.0f m from its middle\n", st->id, st->owner, st->cx, st->cy, std::hypot(dx, dy));
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
    int garrisons = 0, marching = 0, garrisonMen = 0, walking = 0;
    std::vector<Campaign::Soldier> men;
    c.soldiers(static_cast<int32_t>(0), men);
    for (const Campaign::Stack& s : c.stacks()) {
        if (s.armies.empty() || s.boat) continue;
        double sx = 0, sy = 0;
        int n = 0;
        for (const Campaign::Soldier& m : men)
            if (m.stack == s.id) sx += m.x, sy += m.y, ++n;
        if (n == 0) continue;
        sx /= n, sy /= n;
        const int32_t town = c.cityAt(s.cx, s.cy);
        bool fought = false;  // (a stack whose men are in a fight, or have been, follows it out of the courtyard: it is the rest that stands)
        for (const Campaign::Soldier& m : men) fought = fought || (m.stack == s.id && m.fightTick != 0);
        if (s.path.empty() && town >= 0 && !fought) {
            const Continent::Castle& k = c.continent().cities()[static_cast<size_t>(town)].castle;
            // (a block of squads round the courtyard's middle; several stacks stand side by side)
            CHECK(std::hypot(sx - k.courtyard.x.toDouble(), sy - k.courtyard.y.toDouble()) < 40.0);
            for (const Campaign::Soldier& m : men)
                if (m.stack == s.id) ++garrisonMen, walking += m.speed >= 0.5f;  // (standing: a few shuffle into their places)
            ++garrisons;
        }
        if (!s.path.empty() && c.legTime(s) != Campaign::kClosed) {
            float speed = 0;
            // (A man struck in the last few seconds may be flying from the blow: that is no march.)
            for (const Campaign::Soldier& m : men)
                if (m.stack == s.id && (m.hurtTick == 0 || c.tick() - m.hurtTick > 90)) speed = std::max(speed, m.speed);
            if (c.tick() > 60) CHECK(speed < 24.0f);  // a wolf rider's 15 on a road is the quickest, and a little more downhill and in his realm's own country
            ++marching;
        }
    }
    CHECK(garrisons > 60);
    CHECK(walking * 5 < garrisonMen);  // (most of those at rest stand still)
    (void)marching;
}

namespace {
// Whether a point (metres on the continent) is on a building of a town's defences: its keep, a tower, a wall or a house.
bool onBuilding(const Continent::Castle& k, double x, double y) {
    const auto d = [](const Vec3& a) { return std::pair<double, double>{a.x.toDouble(), a.y.toDouble()}; };
    {
        const auto [kx, ky] = d(k.keep);
        const auto [ax, ay] = d(k.keepAxis);
        const double len = std::max(1e-9, std::hypot(ax, ay)), u = ((x - kx) * ax + (y - ky) * ay) / len, v = (-(x - kx) * ay + (y - ky) * ax) / len;
        const double h = k.keepHalf.toDouble() + 0.5;
        if (std::fabs(u) < h && std::fabs(v) < h) return true;
    }
    for (const Continent::Castle::Tower& t : k.towers) {
        const auto [tx, ty] = d(t.pos);
        if (std::hypot(x - tx, y - ty) < t.radius.toDouble() + 0.3) return true;
    }
    for (const Continent::Castle::House& h : k.houses) {
        const auto [hx, hy] = d(h.pos);
        if (std::fabs(x - hx) < h.halfX.toDouble() + 0.3 && std::fabs(y - hy) < h.halfY.toDouble() + 0.3) return true;
    }
    for (const Continent::Castle::Ring& r : k.rings)
        for (size_t i = 0; i < r.corners.size(); ++i) {
            const auto [ax, ay] = d(r.corners[i]);
            const auto [bx, by] = d(r.corners[(i + 1) % r.corners.size()]);
            const double ex = bx - ax, ey = by - ay, t = std::clamp(((x - ax) * ex + (y - ay) * ey) / std::max(1e-9, ex * ex + ey * ey), 0.0, 1.0);
            if (std::hypot(x - (ax + ex * t), y - (ay + ey * t)) < r.thick.toDouble() / 2 + 0.3) return true;
        }
    return false;
}
// Whether a point is within the corners of the outer wall.
bool insideOuter(const Continent::Castle& k, double x, double y) {
    // (A village has no wall: on and about its green.)
    if (k.rings.empty()) return std::hypot(x - k.courtyard.x.toDouble(), y - k.courtyard.y.toDouble()) < 40.0;
    const auto& c = k.rings.front().corners;
    bool in = false;
    for (size_t i = 0, j = c.size() - 1; i < c.size(); j = i++) {
        const double xi = c[i].x.toDouble(), yi = c[i].y.toDouble(), xj = c[j].x.toDouble(), yj = c[j].y.toDouble();
        if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
}
}  // namespace

// A man asleep in a town stands in the open, inside the walls, never in a keep, a tower, a wall or a house: the
// start of every faction, at every town of the island (a capital's inner bailey once ran along its courtyard's edge).
TEST(men_asleep_in_a_town_stand_in_open_ground_inside_its_walls) {
    int men = 0, towns = 0, bad = 0;
    for (uint8_t player = 0; player < 8; ++player) {
        Campaign c(continent(), 5, Campaign::Setup{player, 0xFF});
        c.setHuman(0, true);
        c.prepareWorld();
        std::vector<Campaign::Soldier> all;
        c.soldiers(0, all);
        for (const Campaign::Soldier& m : all) {
            const Campaign::Stack* s = c.findStack(m.stack);
            const int32_t town = s ? c.cityAt(s->cx, s->cy) : -1;
            if (!s || !s->path.empty() || town < 0) continue;
            const Continent::City& city = c.continent().cities()[static_cast<size_t>(town)];
            ++men;
            const bool wrong = onBuilding(city.castle, m.x, m.y) || !insideOuter(city.castle, m.x, m.y);
            if (wrong) ++bad;
            int here = 0;
            for (const Campaign::Stack& o : c.stacks()) here += o.path.empty() && o.cx == s->cx && o.cy == s->cy;
            if (wrong && bad <= 6) std::printf("  %s (faction %d, %d stacks there): a man at %.1f, %.1f (courtyard %.1f, %.1f r %.1f) is in a building (%d) or outside the walls\n", city.name.c_str(), player, here, m.x, m.y, city.castle.courtyard.x.toDouble(), city.castle.courtyard.y.toDouble(), city.castle.courtyardRadius.toDouble(), onBuilding(city.castle, m.x, m.y));
        }
        towns += static_cast<int>(c.cities().size());
    }
    std::printf("  %d men in %d towns, %d out of the open\n", men, towns, bad);
    CHECK(towns == 8 * 80);
    CHECK(men > 8 * 80 * 12);
    CHECK(bad == 0);
}

TEST(stacks_standing_in_one_town_stand_side_by_side_not_on_each_other) {
    Campaign c(continent(), 3);
    c.setHuman(0, true);
    const Campaign::CityState& home = c.cities()[0];
    const uint32_t a = c.raise(0, home.cx, home.cy, {UnitType::HeavyInfantry});
    const uint32_t b = c.raise(0, home.cx, home.cy, {UnitType::HeavyInfantry});
    c.prepareWorld();
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
// further than his pace can carry him, across a march and the fight at the end of it. `worst` is the biggest jump
// of any man beyond what his speed explains.
TEST(a_mans_drawn_place_is_continuous_across_a_march_and_a_fight) {
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
        if (st.owner == 0 && !st.garrison) CampaignOrders::move(c, st.id, town.cx, town.cy);
    std::map<uint32_t, Campaign::Soldier> last;
    std::vector<Campaign::Soldier> now;
    double worst = 0, worstMetres = 0;
    uint32_t worstTick = 0, worstId = 0;
    int jumps = 0;
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
                    if (jump > allowed) ++jumps;
                    if (jump - allowed > worst) worst = jump - allowed, worstMetres = jump, worstTick = c.tick(), worstId = m.id;
                }
                last[m.id] = m;
            }
        }
        c.step();
    }
    std::printf("  worst jump beyond pace: %.2f m (a jump of %.2f m, man %u, tick %u); %d jumps\n", worst, worstMetres, worstId, worstTick, jumps);
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
    field.borrow(&c);
    CHECK(field.borrowing());
    c.onWorldStep([&] { field.beforeWorldStep(); }, [&] { field.afterWorldStep(); }, [&] { field.forgetMen(); });
    // The lord whose nearest weak neutral town is the nearest to the battlefield's middle (the borrowed field is the
    // world's middle kilometre: a fight elsewhere is not one it can show).
    const Campaign::Stack* home = nullptr;
    int target = -1;
    uint8_t owner = 0;
    double nearest = 1e18;
    for (uint8_t lord = 0; lord < 8; ++lord) {
        const Campaign::Stack* h = nullptr;
        for (const Campaign::Stack& st : c.stacks())
            if (st.owner == lord) h = &st;
        if (!h) continue;
        int t = -1, best = 1 << 30;
        for (size_t i = 0; i < c.cities().size(); ++i) {
            const auto& city = c.cities()[i];
            if (city.owner != Campaign::kNeutral || city.defense > 4) continue;
            // (One on the field itself, clear of its edge: a fight beyond it is not one the field can show.)
            const Vec3 on = continent().cities()[i].pos - ro;
            if (on.x < Fixed::fromInt(80) || on.y < Fixed::fromInt(80) || on.x > Fixed::fromInt(940) || on.y > Fixed::fromInt(940)) continue;
            const int d = std::abs(city.cx - h->cx) + std::abs(city.cy - h->cy);
            if (d < best) best = d, t = static_cast<int>(i);
        }
        if (t < 0) continue;
        const Vec3 at = continent().cities()[static_cast<size_t>(t)].pos;
        const double away = std::hypot((at.x - centre.x).toDouble(), (at.y - centre.y).toDouble());
        if (away < nearest) nearest = away, home = h, target = t, owner = lord;
    }
    const auto& town = c.cities()[static_cast<size_t>(target)];
    c.raise(owner, home->cx, home->cy, {UnitType::Hero, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers});
    for (const Campaign::Stack& st : std::vector<Campaign::Stack>(c.stacks()))
        if (st.owner == owner && !st.garrison) CampaignOrders::move(c, st.id, town.cx, town.cy);
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
    c.prepareWorld();
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
    // The field the page draws and picks from asks the same (it presents the conquest): a man of side 2 is not picked.
    c.prepareWorld();
    const Vec3 ro = c.presentedWorld()->region()->origin();
    illyria::game::Battle field(1, Vec3{ro.x + Fixed::fromInt(512), ro.y + Fixed::fromInt(512), Fixed::zero()},
                                std::shared_ptr<const Continent>(&continent(), [](const Continent*) {}));
    field.borrow(&c);
    CHECK(field.sees(0, 10, 10));
    CHECK(!field.sees(2, 10, 10));
}
