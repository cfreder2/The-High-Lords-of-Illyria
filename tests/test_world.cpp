#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "illyria/sim/scenario.h"
#include "illyria/sim/units.h"
#include "test.h"

using namespace illyria::sim;

namespace {

uint64_t runFord(uint64_t seed) {
    const Replay replay = scenarios::Ford::replay(seed);
    World world(replay.config);
    scenarios::Ford::setup(world);
    runReplay(world, replay, scenarios::Ford::kTicks);
    return world.checksum();
}

}  // namespace

TEST(world_is_deterministic) {
    CHECK(runFord(1234) == runFord(1234));
    CHECK(runFord(1234) != runFord(1235));
}

TEST(world_matches_golden_checksum) {
    // Pinned result of the Ford scenario. Every platform and compiler (native
    // x86-64/ARM, WebAssembly) must reproduce it bit for bit; if a deliberate
    // simulation change alters it, update the constant in the same commit.
    const uint64_t actual = runFord(1234);
    constexpr uint64_t kGolden = 0xb89f23afe4493e36;  // (was 0xa35ef681b11c0ce9 before broken men who stand (cornered, or none near to run from) no longer being passed over by their foes)
    if (actual != kGolden) std::printf("  Ford checksum: 0x%016llx\n", static_cast<unsigned long long>(actual));
    CHECK(actual == kGolden);
}

TEST(world_command_order_between_players_does_not_matter) {
    Replay a = scenarios::Ford::replay(99);
    Replay b = a;
    // Reverse the order commands from different players arrive within each tick.
    std::stable_sort(b.commands.begin(), b.commands.end(), [](const auto& x, const auto& y) {
        return x.tick != y.tick ? x.tick < y.tick : x.command.player > y.command.player;
    });
    World wa(a.config), wb(b.config);
    scenarios::Ford::setup(wa);
    scenarios::Ford::setup(wb);
    runReplay(wa, a, 300);
    runReplay(wb, b, 300);
    CHECK(wa.checksum() == wb.checksum());
}

TEST(world_arrows_kill_and_wind_pushes_them) {
    const Replay replay = scenarios::Ford::replay(1234);
    World world(replay.config);
    scenarios::Ford::setup(world);
    int ground = 0, hits = 0, aimedMisses = 0;
    double southDrift = 0;
    runReplay(world, replay, scenarios::Ford::kTicks, [&](const World& w) {
        for (const ImpactEvent& e : w.impacts()) {
            if (e.hitUnit != kNoEntity) {
                ++hits;
            } else {
                ++ground;
                // For the first 30 s the archers aim at the column's centre line, y = 128.
                if (w.tick() < 30 * kTicksPerSecond) {
                    ++aimedMisses;
                    southDrift += 128.0 - e.pos.y.toDouble();
                }
            }
        }
    });
    int dead = 0;
    for (const Unit& u : world.units()) dead += u.owner == 1 && !u.alive();
    std::printf("  %d hits, %d misses, %d footmen dead, mean miss %.1f m south\n", hits, ground,
                dead, aimedMisses ? southDrift / aimedMisses : 0.0);
    CHECK(hits > 0);
    CHECK(dead > 0);
    CHECK(ground > 0);
    CHECK(aimedMisses > 0);
    // The uncorrected crosswind blows arrows south by ~0.5 * 1.5 m/s^2 * flight_time^2.
    CHECK(southDrift / aimedMisses > 0.2);
}

TEST(world_rejects_commands_for_other_players_units) {
    World world(WorldConfig{});
    const EntityId id = world.spawnUnit(0, {}, {Fixed::one(), {}, {}}, scenarios::footman());
    const Command cmd{1, id, Command::Type::Move, {Fixed::fromInt(10), {}, {}}};
    for (int i = 0; i < 30; ++i) world.step(i == 0 ? std::span(&cmd, 1) : std::span<const Command>{});
    CHECK(world.findUnit(id)->pos == Vec3{});
}

// The conquest puts a squad's men in the world to fight and takes them out again. A man taken out leaves his place, and a
// unit's place is his id less one for as long as the world lives (everything that presents the world indexes by it); the
// next man to come takes the lowest empty place, so a world with fights always going on stays as big as its biggest fight.
TEST(a_man_taken_out_of_the_world_leaves_his_place_and_the_next_man_takes_it) {
    World world(WorldConfig{});
    const UnitStats stats = unitStats(UnitType::HeavyInfantry);
    EntityId ids[5];
    for (int i = 0; i < 5; ++i) ids[i] = world.spawnUnit(0, Vec3{Fixed::fromInt(i * 3), Fixed::zero(), Fixed::zero()}, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}, stats);
    for (int i = 0; i < 5; ++i) CHECK(ids[i] == static_cast<EntityId>(i + 1));
    const uint64_t whole = world.checksum();
    world.removeUnit(ids[3]);
    world.removeUnit(ids[1]);
    CHECK(world.units().size() == 5);  // (their places stay)
    for (size_t i = 0; i < world.units().size(); ++i) CHECK(world.units()[i].id == i + 1);
    CHECK(world.findUnit(ids[1]) == nullptr && world.findUnit(ids[3]) == nullptr);
    CHECK(world.findUnit(ids[2]) != nullptr && world.findUnit(ids[2])->alive());
    CHECK(world.checksum() != whole);
    const uint32_t before = world.generation(ids[1]);
    // The next men take the lowest empty places, the lowest first; after them, new ones.
    const EntityId a = world.spawnUnit(1, Vec3{}, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}, stats);
    const EntityId b = world.spawnUnit(1, Vec3{}, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}, stats);
    const EntityId c = world.spawnUnit(1, Vec3{}, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}, stats);
    CHECK(a == ids[1] && b == ids[3] && c == 6);
    CHECK(world.generation(a) == before + 1);  // (a man afresh in an old place)
    CHECK(world.units().size() == 6);
    CHECK(world.findUnit(a) != nullptr && world.findUnit(a)->owner == 1 && world.findUnit(a)->alive());
    for (size_t i = 0; i < world.units().size(); ++i) CHECK(world.units()[i].id == i + 1);
    // Taking a man out twice is taking him out once.
    world.removeUnit(ids[2]);
    world.removeUnit(ids[2]);
    const EntityId d = world.spawnUnit(1, Vec3{}, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}, stats);
    const EntityId e = world.spawnUnit(1, Vec3{}, Vec3{Fixed::one(), Fixed::zero(), Fixed::zero()}, stats);
    CHECK(d == ids[2] && e == 7);
}

namespace {

Fixed mm(int64_t v) { return Fixed::fromInt(v); }
const Vec3 kEastward{Fixed::one(), Fixed::zero(), Fixed::zero()};
const Vec3 kWestward = -kEastward;

// A man who stands where he is put and strikes no one.
UnitStats dummy(UnitType type = UnitType::LightInfantry) {
    UnitStats s = unitStats(type);
    s.aggroRange = Fixed::zero();
    s.ranged.autoRange = Fixed::zero();
    s.shield.blockChance = Fixed::zero();
    return s;
}

// What the rest of the world is, man by man (the removed are not asked for), as a hash.
uint64_t menOf(const World& w, EntityId upTo) {
    uint64_t h = 1469598103934665603ull;
    const auto add = [&](int64_t v) { h = (h ^ static_cast<uint64_t>(v)) * 1099511628211ull; };
    for (EntityId id = 1; id <= upTo; ++id) {
        const Unit* u = w.findUnit(id);
        if (!u) continue;
        add(u->pos.x.raw), add(u->pos.y.raw), add(u->hp), add(u->morale), add(u->kills), add(u->routed), add(u->hasMoveTarget), add(u->steady), add(u->calmTicks), add(u->callCooldown);  // (the morale census among them)
    }
    return h;
}

}  // namespace

// A man taken out of the world is nobody: not a body (no corpse to lie there, to be looted, to be thrown or torn), not a
// shape the world's blasts, arrows and blows find, and not a part of its state (the hash). Every loop over the men asks
// Unit::present() before it treats the fallen.
TEST(a_man_taken_out_of_the_world_leaves_no_trace_in_it) {
    // The step is the same with him gone as in a world that never had him: two lines fighting, a third man where he was.
    const auto fight = [](bool withHim, bool hero) {
        World w(WorldConfig{});
        std::vector<EntityId> line;
        for (int i = 0; i < 4; ++i) line.push_back(w.spawnUnit(0, {mm(0), mm(i * 2), mm(0)}, kEastward, unitStats(UnitType::HeavyInfantry)));
        for (int i = 0; i < 4; ++i) line.push_back(w.spawnUnit(1, {mm(3), mm(i * 2), mm(0)}, kWestward, unitStats(UnitType::HeavyInfantry)));
        if (withHim) {
            // He stands among them, a hero and hurt, or a plain man: whichever he was, he is gone before the first tick.
            const EntityId him = w.spawnUnit(0, {mm(1), mm(3), mm(0)}, kEastward, unitStats(hero ? UnitType::Hero : UnitType::Archers));
            w.removeUnit(him);
        }
        for (int t = 0; t < 20 * kTicksPerSecond; ++t) {
            w.step({});
            for (const DeathEvent& d : w.deathEvents()) CHECK(d.unit <= line.size());  // (nobody of his is among the dead)
        }
        return menOf(w, static_cast<EntityId>(line.size()));
    };
    const uint64_t never = fight(false, false);
    CHECK(fight(true, false) == never);  // the men of both sides are as they are in a world that never had him
    CHECK(fight(true, true) == never);   // (and it matters not who he was)

    // The hash of the world is the same whoever the man was who left the place: what is empty is empty.
    const auto place = [](UnitType who, bool dead) {
        World w(WorldConfig{});
        UnitStats frail = dummy(who);
        if (dead) frail.maxHp = 1;
        const EntityId a = w.spawnUnit(0, {mm(0), mm(0), mm(0)}, kEastward, dummy());
        const EntityId x = w.spawnUnit(1, {mm(9), mm(9), mm(0)}, kWestward, frail);
        const EntityId b = w.spawnUnit(0, {mm(4), mm(0), mm(0)}, kWestward, dummy());
        (void)a, (void)b;
        if (dead) {  // a head flung at a man with one hit point left, and he lies a corpse
            w.launchPart(RemainKind::Head, 0, {mm(9) - mm(10), mm(9), Fixed::fromRatio(6, 5)}, {mm(14), mm(0), Fixed::one()});
            for (int t = 0; t < 3 * kTicksPerSecond; ++t) w.step({});
            CHECK(!w.findUnit(x)->alive());
        }
        w.removeUnit(x);
        for (int t = 0; t < 2 * kTicksPerSecond; ++t) w.step({});
        return w.checksum();
    };
    // (They differ in tick counts, not in who was removed: the same two worlds, one with a hero's place and one with a
    // corpse's, hold the same men.)
    CHECK(place(UnitType::Hero, false) == place(UnitType::Archers, false));
    CHECK(place(UnitType::LightInfantry, true) == place(UnitType::Archers, true));
}

TEST(a_man_taken_out_of_the_world_is_not_scavenged_not_shot_and_not_blown_apart) {
    // Scavenged: a swordsman who fell is a blade to a hyena, unless he was taken out of the world.
    const auto raid = [](bool removeHim) {
        World w(WorldConfig{});
        UnitStats frail = dummy();
        frail.maxHp = 1;
        const EntityId dead = w.spawnUnit(1, {mm(4), mm(0), mm(0)}, kWestward, frail);
        w.launchPart(RemainKind::Head, 0, {mm(-1), mm(0), Fixed::fromRatio(6, 5)}, {mm(14), mm(0), Fixed::one()});
        for (int i = 0; i < 2 * kTicksPerSecond; ++i) w.step({});
        CHECK(!w.findUnit(dead)->alive());
        if (removeHim) w.removeUnit(dead);
        const EntityId hyena = w.spawnUnit(0, {mm(0), mm(-1), mm(0)}, kEastward, unitStats(UnitType::Hyena));
        UnitStats man = dummy();
        man.melee.damage = 0;
        w.spawnUnit(1, {mm(4), mm(12), mm(0)}, kWestward, man);
        Command c{0, hyena, Command::Type::Raid, {}};
        w.step(std::span(&c, 1));
        bool tookBlade = false;
        for (int i = 0; i < 6 * kTicksPerSecond; ++i) {
            w.step({});
            tookBlade = tookBlade || w.findUnit(hyena)->scav.held == RemainKind::Blade;
        }
        return tookBlade;
    };
    CHECK(raid(false));   // (the fallen man's blade is taken: this is a scene where he would be)
    CHECK(!raid(true));   // taken out of the world, he is nobody's blade

    // Shot: archers find a block of men standing in their range and hit them; ordered to loose at the ground where the
    // block stood (and is not), they loose and find no one.
    const auto volley = [](bool removeThem) {
        World w(WorldConfig{});
        std::vector<EntityId> archers, block;
        for (int i = 0; i < 5; ++i) archers.push_back(w.spawnUnit(1, {mm(0), mm(i), mm(0)}, kEastward, unitStats(UnitType::Archers)));
        for (int i = 0; i < 9; ++i) block.push_back(w.spawnUnit(0, {mm(20 + i / 3), mm(i % 3), mm(0)}, kWestward, dummy(UnitType::HeavyInfantry)));
        if (removeThem)
            for (EntityId id : block) w.removeUnit(id);
        std::vector<Command> cs;
        for (EntityId a : archers) cs.push_back({1, a, Command::Type::AttackGround, {mm(21), mm(1), mm(0)}});
        int hits = 0, shots = 0;
        for (int i = 0; i < 12 * kTicksPerSecond; ++i) {
            w.step(i == 0 && removeThem ? std::span<const Command>(cs) : std::span<const Command>{});
            for (const ImpactEvent& e : w.impacts()) {
                hits += std::find(block.begin(), block.end(), e.hitUnit) != block.end();
                shots += e.kind == ImpactEvent::Kind::Ground;
            }
            for (const DeathEvent& d : w.deathEvents()) CHECK(!removeThem || std::find(block.begin(), block.end(), d.unit) == block.end());
        }
        return std::make_pair(hits, shots);
    };
    CHECK(volley(false).first > 0);
    const auto blind = volley(true);
    CHECK(blind.second > 0 && blind.first == 0);  // arrows came down where they stood, and found no one

    // Blown apart: a blast over a place he left throws nothing, hurts no one, and tells of no death.
    const auto blast = [](bool removeHim) {
        World w(WorldConfig{});
        UnitStats frail = dummy(UnitType::HeavyInfantry);
        frail.maxHp = 1;
        const EntityId man = w.spawnUnit(0, {mm(0), mm(0), mm(0)}, kEastward, frail);
        if (removeHim) w.removeUnit(man);
        Explosive ex;
        ex.radius = mm(8);
        ex.damage = 1000;
        ex.impulse = mm(30);
        w.placeSatchel(1, {mm(1), mm(0), mm(0)}, ex, 1);
        int told = 0;
        for (int i = 0; i < 4 * kTicksPerSecond; ++i) {
            w.step({});
            for (const ImpactEvent& e : w.impacts()) told += e.hitUnit == man;
            told += static_cast<int>(w.deathEvents().size());
        }
        const Unit* u = w.findUnit(man);
        return std::make_pair(told, u != nullptr);
    };
    CHECK(blast(false).first > 0);
    CHECK(blast(true).first == 0);
    CHECK(!blast(true).second);
}

// A place he leaves goes to the next man: and the new man is a man, not the old one with a new coat. Nothing of the one who
// stood there is his (state, orders, whom he fought and who fought him), and the world's hash is that of a world whose man
// came to a place that was empty.
TEST(a_place_reused_gives_the_new_man_nothing_of_the_old) {
    World w(WorldConfig{});
    const EntityId foe = w.spawnUnit(1, {mm(5), mm(0), mm(0)}, kWestward, unitStats(UnitType::HeavyInfantry));
    const EntityId old = w.spawnUnit(0, {mm(0), mm(0), mm(0)}, kEastward, unitStats(UnitType::Hero));
    Command c{0, old, Command::Type::AttackUnit, {}};
    c.targetUnit = foe;
    w.step(std::span(&c, 1));
    for (int t = 0; t < 5 * kTicksPerSecond; ++t) w.step({});
    {
        Unit* u = const_cast<Unit*>(w.findUnit(old));
        u->kills = 7;
        u->rank = 3;
        u->routed = true;
        u->gibbed = true;
        u->looted = true;
        u->satchels = 2;
        u->morale = 5;
    }
    w.restoreUnit(old, 3, 7, 5);
    // Others had him as their foe, their rally, their mark: the id they keep is the place's.
    const_cast<Unit*>(w.findUnit(foe))->targetUnit = old;
    const_cast<Unit*>(w.findUnit(foe))->engageFoe = old;
    const_cast<Unit*>(w.findUnit(foe))->lastAttacker = old;
    const uint32_t gen = w.generation(old);
    w.removeUnit(old);
    CHECK(w.findUnit(foe)->targetUnit == kNoEntity && w.findUnit(foe)->engageFoe == kNoEntity && w.findUnit(foe)->lastAttacker == kNoEntity);

    const EntityId next = w.spawnUnit(1, {mm(20), mm(20), mm(0)}, kEastward, unitStats(UnitType::Archers));
    CHECK(next == old);
    CHECK(w.generation(next) == gen + 1);
    const Unit* u = w.findUnit(next);
    CHECK(u != nullptr && u->present() && u->alive());
    CHECK(u->owner == 1 && u->stats.type == static_cast<uint8_t>(UnitType::Archers));
    CHECK(u->hp == u->stats.maxHp && u->kills == 0 && u->rank == 0 && !u->routed && !u->gibbed && !u->looted && !u->removed);
    CHECK(u->satchels == unitStats(UnitType::Archers).satchels.count);
    CHECK(!u->hasMoveTarget && !u->hasAttackTarget && u->targetUnit == kNoEntity && u->foe == kNoEntity && !u->engaged);
    CHECK(u->death.kind == DeathKind::None && u->vel == Vec3{});
    CHECK(u->morale == kMoraleFull);
    CHECK(w.findUnit(foe)->targetUnit == kNoEntity);

    // And the hash: a world whose man came to a place that was empty, and one that never had the other, are one world.
    const auto build = [&](bool reuse) {
        World a(WorldConfig{});
        a.spawnUnit(1, {mm(5), mm(0), mm(0)}, kWestward, unitStats(UnitType::HeavyInfantry));
        if (reuse) {
            const EntityId was = a.spawnUnit(0, {mm(0), mm(0), mm(0)}, kEastward, unitStats(UnitType::Hero));
            a.removeUnit(was);
        }
        a.spawnUnit(1, {mm(20), mm(20), mm(0)}, kEastward, unitStats(UnitType::Archers));
        return a.checksum();
    };
    CHECK(build(true) == build(false));
}

TEST(men_taken_out_of_a_world_are_no_part_of_its_hash_and_the_places_they_left_are) {
    const auto make = [](UnitType who) {
        World w(WorldConfig{});
        w.spawnUnit(0, {mm(0), mm(0), mm(0)}, kEastward, unitStats(UnitType::LightInfantry));
        const EntityId x = w.spawnUnit(0, {mm(3), mm(0), mm(0)}, kEastward, unitStats(who));
        w.spawnUnit(0, {mm(6), mm(0), mm(0)}, kEastward, unitStats(UnitType::LightInfantry));
        w.removeUnit(x);
        return w;
    };
    World a = make(UnitType::HeavyInfantry), b = make(UnitType::Hero);
    CHECK(a.checksum() == b.checksum());  // who he was is no part of it
    World c(WorldConfig{});
    c.spawnUnit(0, {mm(0), mm(0), mm(0)}, kEastward, unitStats(UnitType::LightInfantry));
    c.spawnUnit(0, {mm(3), mm(0), mm(0)}, kEastward, unitStats(UnitType::LightInfantry));
    c.spawnUnit(0, {mm(6), mm(0), mm(0)}, kEastward, unitStats(UnitType::LightInfantry));
    CHECK(a.checksum() != c.checksum());  // but the empty place is: it says whose id the next man has
    const EntityId na = a.spawnUnit(0, {mm(3), mm(0), mm(0)}, kEastward, unitStats(UnitType::LightInfantry));
    CHECK(na == 2 && a.checksum() == c.checksum());  // and filled, it is the world that never had him (generation is not state)
    CHECK(a.generation(2) != c.generation(2));
}
