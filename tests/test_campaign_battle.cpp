// The war for the island is a battle the one Battle presents (game/src/battle_conquest.cpp): the player's men there,
// awake in the world and asleep in their stacks, are its squads and its selection, driven by its taps, its command bar
// and its orders as in a skirmish. A march across the island is an Order of the conquest; a fight's orders are the
// world's commands carried in Orders and applied inside the step.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/game/factions.h"
#include "illyria/game/minimap.h"
#include "illyria/sim/campaign.h"
#include "campaign_orders.h"
#include "test.h"

using namespace illyria::sim;
using illyria::game::Battle;
using illyria::game::V3;

namespace {

const Continent& continent() {
    static const Continent c(20261027);
    return c;
}

// The island with the player on side 0 and the other lords at home, presented by a Battle as the page does.
struct Island {
    Campaign c;
    std::unique_ptr<Battle> b;
    illyria::game::Camera cam;
    std::vector<Campaign::Soldier> men;

    Island() : c(continent(), 3) {
        for (uint8_t s = 0; s < 8; ++s) c.setHuman(s, true);
        c.prepareWorld();
        const Vec3 ro = c.presentedWorld()->region()->origin();
        b = std::make_unique<Battle>(1, Vec3{ro.x + Fixed::fromInt(512), ro.y + Fixed::fromInt(512), Fixed::zero()},
                                     std::shared_ptr<const Continent>(&continent(), [](const Continent*) {}));
        b->borrow(&c);
        c.onWorldStep([this] { b->beforeWorldStep(); }, [this] { b->afterWorldStep(); }, [this] { b->forgetMen(); });
        const V3 o = b->origin();
        cam.setBounds(-o.x, -o.y, Continent::kWidth - o.x, Continent::kHeight - o.y);
        cam.setViewport(1000, 700);
        cam.setHeightFn([this](float x, float y) { return b->heightAt(x, y); });
        frame();
    }
    // What the page does each frame after the steps; and before them.
    void frame() {
        c.visibleSoldiers(c.localSide(), 0, men);
        b->presentConquest(men);
    }
    void tick(int n = 1, int every = 1) {
        for (int k = 0; k < n; ++k) {
            b->flushOrders();
            c.step();
            if (k % every == 0 || k + 1 == n) frame();
        }
    }
    int squadOf(UnitType type, uint8_t owner = 0) const {
        for (size_t i = 0; i < b->squads().size(); ++i)
            if (b->squads()[i].type == type && b->squads()[i].owner == owner && b->aliveCount(b->squads()[i]) > 0) return static_cast<int>(i);
        return -1;
    }
    const Campaign::Stack* stackOfArmy(uint32_t army) const {
        for (const Campaign::Stack& s : c.stacks())
            for (const Army& a : s.armies)
                if (a.id == army) return &s;
        return nullptr;
    }
    // The ground the squad's men are on, to look at them from.
    V3 look(int squad, float distance) {
        const V3 at = b->squadCentre(b->squads()[static_cast<size_t>(squad)], false);
        cam.lookAt(at.x, at.y, distance);
        return at;
    }
};

}  // namespace

TEST(men_asleep_in_their_stack_are_the_battles_squads_with_cards_and_a_hero_level) {
    Island w;
    const int hero = w.squadOf(UnitType::Hero);
    CHECK(hero >= 0);
    if (hero < 0) return;
    const illyria::game::Squad& q = w.b->squads()[static_cast<size_t>(hero)];
    CHECK(!q.sleeping.empty());  // (the stack rests in its capital: no man of it is a unit of the world)
    CHECK(q.members.empty());
    CHECK(w.b->standing(q).level >= 1);
    CHECK(w.b->standing(q).strength >= 1);
    CHECK(w.b->controllable(q));
    // The card's other men: the stack's other army is a squad too.
    CHECK(w.squadOf(UnitType::LightInfantry) >= 0);
    // The sleepers are drawn in the one pass, one picture each.
    size_t mine = 0;
    for (const Battle::Sleeper& m : w.b->sleepers()) mine += m.owner == 0;
    CHECK(mine > 0);
}

TEST(a_tap_selects_a_man_asleep_like_one_awake_and_the_card_selects_his_squad) {
    Island w;
    const int hero = w.squadOf(UnitType::Hero);
    CHECK(hero >= 0);
    if (hero < 0) return;
    const V3 at = w.look(hero, 60.0f);
    float px = 0, py = 0;
    CHECK(w.cam.project(w.b->sleeperPositionOf(w.b->squads()[static_cast<size_t>(hero)].sleeping.front()) + V3{0, 0, 1.0f}, px, py));
    w.b->tap(w.cam, px, py, 1.0f);
    CHECK(w.b->selectionSize() == 1);
    CHECK(w.b->selectedSleepers().size() == 1);
    CHECK(w.b->selectedAlive().empty());
    CHECK(std::string(w.b->selectionLabel()) == unitTypeName(UnitType::Hero));
    CHECK(w.b->selectedStack() == 0);  // (until the next frame says whose stack it is)
    w.frame();
    CHECK(w.b->selectedStack() == w.b->squads()[static_cast<size_t>(hero)].stack);
    CHECK(w.b->selectedSquad() == hero);  // (a hero is the whole of his squad)
    (void)at;
    // The card: the squad of men, all selected.
    const int foot = w.squadOf(UnitType::LightInfantry);
    CHECK(foot >= 0);
    w.b->selectSquad(foot);
    CHECK(w.b->selectionSize() == static_cast<size_t>(w.b->aliveCount(w.b->squads()[static_cast<size_t>(foot)])));
    CHECK(w.b->selectedSquad() == foot);
    CHECK(w.b->selectedArmy() == w.b->squads()[static_cast<size_t>(foot)].army);
    // A box round them (the same press and box as a skirmish's) selects the sleepers inside it.
    w.look(foot, 40.0f);
    w.b->clearSelection();
    w.b->selectInRect(w.cam, 0, 0, 1000, 700);
    CHECK(w.b->selectionSize() > 0);
    // Nothing ordered by selecting: no Order is queued, nothing in the world moved.
    w.tick(2);
    CHECK(w.c.awakeStacks() == 0);
}

TEST(a_march_across_the_island_is_an_order_of_the_selected_squads_alone) {
    Island w;
    const int hero = w.squadOf(UnitType::Hero);
    CHECK(hero >= 0);
    if (hero < 0) return;
    const uint32_t army = w.b->squads()[static_cast<size_t>(hero)].army, stack = w.b->squads()[static_cast<size_t>(hero)].stack;
    const size_t before = w.c.findStack(stack)->armies.size();
    CHECK(before >= 2);
    w.b->selectSquad(hero);
    const V3 here = w.b->squadCentre(w.b->squads()[static_cast<size_t>(hero)], false);
    // A tap on far ground (a town across the island) is the same orderMove a skirmish's tap makes.
    // (the middle of the nearest neutral town across the way)
    V3 town = here;
    float far = 0;
    for (const Continent::City& c : continent().cities()) {
        const V3 p = V3{static_cast<float>(c.pos.x.toDouble()), static_cast<float>(c.pos.y.toDouble()), 0} - w.b->origin();
        const float d = std::hypot(p.x - here.x, p.y - here.y);
        if (d > 700.0f && (far == 0 || d < far)) far = d, town = p;
    }
    CHECK(far > 0);
    w.b->orderMove(town);
    w.tick(1);
    const Campaign::Stack* moving = w.stackOfArmy(army);
    CHECK(moving != nullptr);
    if (!moving) return;
    CHECK(!moving->path.empty());                       // it is on its way
    CHECK(moving->armies.size() == 1);                  // alone: the squad ordered, not the stack
    CHECK(moving->id != stack);                         // (split off, a stack of its own)
    const Campaign::Stack* rest = w.c.findStack(stack);
    CHECK(rest != nullptr && rest->armies.size() == before - 1 && rest->path.empty());
    // Hold stops it at the next cell it reaches.
    w.tick(120);
    w.b->selectSquad(w.squadOf(UnitType::Hero));
    w.b->stopSelected();
    w.tick(1);
    const Campaign::Stack* held = w.stackOfArmy(army);
    CHECK(held != nullptr && held->path.size() <= 1);
    w.tick(60 * 30);
    held = w.stackOfArmy(army);
    CHECK(held != nullptr && held->path.empty());
}

TEST(an_order_is_refused_for_a_stack_or_a_man_that_is_not_the_senders) {
    Island w;
    const Campaign::Stack* foe = nullptr;
    for (const Campaign::Stack& s : w.c.stacks())
        if (s.owner == 1 && !s.garrison && !s.armies.empty()) foe = &s;
    CHECK(foe != nullptr);
    if (!foe) return;
    Campaign::Order o;
    o.kind = Campaign::Order::Kind::Move;
    o.side = 0;  // (not the stack's side)
    o.stack = foe->id;
    o.a = std::min(foe->cx + 5, w.c.width() - 1);
    o.b = foe->cy;
    w.c.submit(o);
    Campaign::Order h = o;
    h.kind = Campaign::Order::Kind::Produce;
    h.a = 0;
    for (size_t i = 0; i < w.c.cities().size(); ++i)
        if (w.c.cities()[i].owner == 1) h.a = static_cast<int32_t>(i);
    h.b = 0;
    w.c.submit(h);
    const int32_t was = w.c.cities()[static_cast<size_t>(h.a)].producing;
    w.c.step();
    CHECK(w.c.findStack(foe->id)->path.empty());
    CHECK(w.c.cities()[static_cast<size_t>(h.a)].producing == was);
}

// The fight: two stacks come to blows; the player's men in it are units, and the Battle orders them with the same
// commands a skirmish's orders are, carried into the step as Orders.
TEST(the_players_men_in_a_fight_are_ordered_as_in_a_skirmish_and_stay_selected_through_it) {
    Island w;
    const Campaign::Stack* home = nullptr;
    for (const Campaign::Stack& st : w.c.stacks())
        if (st.owner == 0) home = &st;
    CHECK(home != nullptr);
    if (!home) return;
    const int32_t x = std::min(home->cx + 8, w.c.width() - 12), y = home->cy;
    const uint32_t mine = w.c.raise(0, x, y, {UnitType::Hero, UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers});
    const uint32_t theirs = w.c.raise(1, x + 4, y, {UnitType::HeavyInfantry, UnitType::HeavyInfantry, UnitType::Archers});
    // (neither side's lord moves them: the first marches out to meet the other)
    CampaignOrders::move(w.c, mine, x + 3, y);
    CampaignOrders::move(w.c, theirs, x + 1, y);
    int guard = 0;
    while (w.c.awakeStacks() == 0 && guard++ < 3000) w.tick(1, 10);
    CHECK(w.c.awakeStacks() > 0);
    w.frame();
    const int heavy = w.squadOf(UnitType::HeavyInfantry);
    CHECK(heavy >= 0);
    if (heavy < 0) return;
    CHECK(!w.b->squads()[static_cast<size_t>(heavy)].members.empty());  // awake: units of the world
    const uint32_t army = w.b->squads()[static_cast<size_t>(heavy)].army;  // (squads are made afresh each frame: by army)
    w.b->selectSquad(heavy);
    const size_t picked = w.b->selectionSize();
    CHECK(picked > 0);
    CHECK(w.b->selectedAlive().size() == picked);
    // The command bar's orders, as the world's commands (Hold, Scatter, a formation, As they stand).
    w.b->stopSelected();
    w.b->scatter();
    w.b->setFormation(illyria::game::Formation::Box);
    w.b->keepShape();
    w.tick(2);
    const Campaign::Stack* st = w.stackOfArmy(army);
    CHECK(st != nullptr && st->commanded);  // its men stand to the player's orders, as a skirmish's do
    CHECK(st != nullptr && st->path.empty());
    // Charge: every squad at the nearest enemy, the foot with the world's attack commands.
    w.b->chargeAll();
    w.tick(2);
    int after = 0;
    for (illyria::sim::EntityId id : w.b->selectedAlive()) {
        const Unit* u = w.c.world()->findUnit(id);
        after += u && (u->hasMoveTarget || u->targetUnit != kNoEntity);
    }
    CHECK(after > 0);  // (the foot are marched at the foe, to fight whoever they meet)
    // The fight goes on, and ends; the selection of men holds through their going back to sleep.
    guard = 0;
    while ((w.c.awakeStacks() > 0 || guard < 30 * 60) && guard++ < 30 * 60 * 20) w.tick(30, 30);
    CHECK(w.c.awakeStacks() == 0);
    size_t left = 0;
    for (const illyria::game::Squad& q : w.b->squads())
        if (q.army == army) left = static_cast<size_t>(w.b->aliveCount(q));
    std::printf("  %zu of the squad's %zu men are left, asleep again, %zu of them still selected (%zu in all)\n", left, picked,
                w.b->selectedSleepers().size(), w.b->selectionSize());
    CHECK(w.b->selectedAlive().empty());
    CHECK(w.b->selectedSleepers().size() == left);  // every man who is left is still selected
    // A man tapped who is not ours, with nothing selected, is looked at: view-only, grey, no order.
    w.b->clearSelection();
    w.tick(1);
    for (const Battle::Sleeper& m : w.b->sleepers())
        if (m.owner != 0) {
            const V3 p = w.b->sleeperPositionOf(m.id);
            w.cam.lookAt(p.x, p.y, 50.0f);
            float px, py;
            if (!w.cam.project(p + V3{0, 0, 1.0f}, px, py)) continue;
            w.b->tap(w.cam, px, py, 1.0f);
            CHECK(w.b->inspected().kind == Battle::Inspected::Kind::Man);
            CHECK(w.b->selectionSize() == 0);
            const Battle::Glance g = w.b->glance();
            CHECK(g.any && !g.gate && g.owner != 0);
            break;
        }
}

// The same Orders over the same game give the same checksum on every machine, and a player's order to men in the
// world is one of them (it is applied inside the step, in its place, like any other).
TEST(a_fights_commands_are_orders_and_two_games_given_the_same_ones_are_one_game) {
    const auto play = [](uint64_t& sum) {
        Island w;
        const Campaign::Stack* home = nullptr;
        for (const Campaign::Stack& st : w.c.stacks())
            if (st.owner == 0) home = &st;
        const int32_t x = std::min(home->cx + 8, w.c.width() - 12), y = home->cy;
        const uint32_t mine = w.c.raise(0, x, y, {UnitType::HeavyInfantry, UnitType::HeavyInfantry});
        const uint32_t theirs = w.c.raise(1, x + 4, y, {UnitType::HeavyInfantry, UnitType::HeavyInfantry});
        CampaignOrders::move(w.c, mine, x + 3, y);
        CampaignOrders::move(w.c, theirs, x + 1, y);
        int guard = 0;
        while (w.c.awakeStacks() == 0 && guard++ < 3000) w.c.step();
        w.frame();
        const int heavy = w.squadOf(UnitType::HeavyInfantry);
        w.b->selectSquad(heavy);
        w.b->orderMove(w.b->squadCentre(w.b->squads()[static_cast<size_t>(heavy)], false) + V3{20, 0, 0});
        w.b->flushOrders();
        for (int k = 0; k < 600; ++k) w.c.step();
        sum = w.c.checksum();
        return w.c.tick();
    };
    uint64_t a = 0, b = 0;
    CHECK(play(a) == play(b));
    CHECK(a == b);
    CHECK(a != 0);
}

// Everything shown of another side goes through the one seam of sight (Campaign::canSee), with the viewer a parameter.
// Hide sides 1 and 2 and look at the surfaces that tell: the minimap's text, the man under a finger, the fight the
// camera is taken to, the towns' holders.
namespace {
bool hides12(uint8_t viewer, uint8_t owner, float, float) { return owner == viewer || (owner != 1 && owner != 2); }
bool seesOnlyItsOwn(uint8_t viewer, uint8_t owner, float, float) { return owner == viewer; }

size_t linesWith(const std::string& text, const std::string& what) {
    size_t n = 0, at = 0;
    while ((at = text.find(what, at)) != std::string::npos) ++n, at += what.size();
    return n;
}
}  // namespace

TEST(a_side_hidden_from_the_viewer_is_on_no_surface_the_viewer_reads) {
    Island w;
    const std::string colour1 = [] {  // the colours the minimap writes for sides 1 and 2
        char b[8];
        const V3 c1 = illyria::game::factionColor(1);
        std::snprintf(b, sizeof b, "#%02x%02x%02x", static_cast<int>(c1.x * 255), static_cast<int>(c1.y * 255), static_cast<int>(c1.z * 255));
        return std::string(b);
    }();
    const std::string seen = illyria::game::minimapText(w.c, 0);
    const size_t before = linesWith(seen, "|" + colour1 + "|");
    CHECK(before > 0);  // (its capital, its men)
    w.c.setSight(hides12);
    const std::string hidden = illyria::game::minimapText(w.c, 0);
    CHECK(linesWith(hidden, "|" + colour1 + "|") == 0);
    // The towns held by sides 1 and 2 are nobody's to the viewer; its own are its own.
    for (size_t i = 0; i < w.c.cities().size(); ++i) {
        const uint8_t owner = w.c.cities()[i].owner;
        if (owner == 1 || owner == 2) CHECK(w.c.knownOwner(0, static_cast<int32_t>(i)) == Campaign::kNeutral);
        if (owner == 0) CHECK(w.c.knownOwner(0, static_cast<int32_t>(i)) == 0);
        if (owner == 1) CHECK(w.c.knownOwner(1, static_cast<int32_t>(i)) == 1);  // (a side sees its own)
    }
    // Nobody of side 1 or 2 is picked under a finger (the Battle presents the conquest and asks it).
    w.frame();
    bool tried = false;
    for (const Campaign::Soldier& m : w.men) CHECK(m.owner != 1 && m.owner != 2);
    w.c.setSight({});
    w.frame();
    for (const Battle::Sleeper& m : w.b->sleepers()) {
        if (m.owner != 1) continue;
        const V3 p = w.b->sleeperPositionOf(m.id);
        w.cam.lookAt(p.x, p.y, 40.0f);
        float px, py;
        if (!w.cam.project(p + V3{0, 0, 1.0f}, px, py)) continue;
        CHECK(w.b->manAt(w.cam, px, py, 20.0f).kind == Battle::ManPick::Kind::Sleeper);
        w.c.setSight(hides12);
        w.frame();
        CHECK(w.b->manAt(w.cam, px, py, 20.0f).kind == Battle::ManPick::Kind::None);
        tried = true;
        break;
    }
    CHECK(tried);
}

TEST(the_fight_the_camera_is_taken_to_is_one_the_viewer_may_see) {
    // Two lords' stacks fight far from the player; the hot spot finds it while it can be seen and not when it cannot.
    const auto fight = [](bool hidden) {
        Island w;
        if (hidden) w.c.setSight(seesOnlyItsOwn);  // (a town's garrison is a side's too: it would show the fight)
        const Campaign::Stack* home = nullptr;
        for (const Campaign::Stack& st : w.c.stacks())
            if (st.owner == 1 && !st.garrison) home = &st;
        const int32_t x = std::min(home->cx + 6, w.c.width() - 14), y = home->cy;
        const uint32_t a = w.c.raise(1, x, y, {UnitType::HeavyInfantry, UnitType::HeavyInfantry});
        const uint32_t b = w.c.raise(2, x + 4, y, {UnitType::HeavyInfantry, UnitType::HeavyInfantry});
        CampaignOrders::move(w.c, a, x + 3, y);
        CampaignOrders::move(w.c, b, x + 1, y);
        for (int k = 0; k < 2400; ++k) w.c.step();
        V3 at;
        return std::make_pair(w.c.awakeStacks() > 0 || w.b->hotSpot(at), w.b->hotSpot(at));
    };
    const auto open = fight(false);
    const auto shut = fight(true);
    CHECK(open.second);   // blows were struck, and seen
    CHECK(!shut.second);  // the same blows, unseen (by one who sees only its own): the camera is taken nowhere
}

// What the seam costs. The sight is a plain function of (viewer, owner, x, y) until fog is built; these are the costs of
// asking it of every man a frame (visibleSoldiers) and of one man (canSee), without and with a stub answering.
TEST(the_cost_of_asking_the_seam_of_sight) {
    Island w;
    std::vector<Campaign::Soldier> men;
    const auto time = [&](const char* what, int reps) {
        const auto start = std::chrono::steady_clock::now();
        size_t kept = 0;
        for (int k = 0; k < reps; ++k) {
            w.c.visibleSoldiers(0, 0, men);
            kept += men.size();
        }
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / reps;
        std::printf("  visibleSoldiers %s: %.3f ms for %zu men (%.0f ns a man)\n", what, ms, kept / static_cast<size_t>(reps), ms * 1e6 / static_cast<double>(std::max<size_t>(1, kept / static_cast<size_t>(reps))));
        return ms;
    };
    const double open = time("with everything seen", 20);
    w.c.setSight(hides12);
    const double shut = time("through a stub hiding two sides", 20);
    volatile int sink = 0;
    const auto start = std::chrono::steady_clock::now();
    constexpr int kCalls = 2000000;
    for (int k = 0; k < kCalls; ++k) sink = sink + w.c.canSee(0, static_cast<uint8_t>(k & 7), static_cast<float>(k), 0.0f);
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / kCalls;
    std::printf("  canSee through the stub: %.1f ns a call\n", ns);
    CHECK(shut < 10.0 + open * 4);  // a frame's worth of men through the seam is a small share of a frame, not a new cost
    CHECK(ns < 1000.0);
}
