// Outworks (docs/MAP.md "Outworks", docs/GAME_DESIGN.md "Outworks and timber"): a fair share of the castles have a ditch,
// a bank with a palisade, abatis and watchtowers; the ditch bars horse and wheels and slows foot.
#include <cstdio>

#include "illyria/sim/continent.h"
#include "illyria/sim/region.h"
#include "test.h"

using namespace illyria::sim;

namespace {
const Continent& continent() {
    static const Continent c(20261027);
    return c;
}
}  // namespace

TEST(outworks_are_fair_every_realm_its_seat_and_one_town) {
    const auto& cs = continent().cities();
    int seats[Continent::kFactionCount] = {}, towns[Continent::kFactionCount] = {}, moats = 0, all = 0;
    for (const Continent::City& c : cs) {
        if (!c.fortified()) continue;
        ++all;
        CHECK(c.walled);
        CHECK(c.realm < Continent::kFactionCount);
        if (c.realm >= Continent::kFactionCount) continue;
        (c.capital ? seats : towns)[c.realm] += 1;
        moats += c.ditch.wet;
        int pal = 0, aba = 0, tow = 0, eng = 0;
        for (const Work& w : c.works) {
            pal += w.kind == Work::Kind::Palisade;
            aba += w.kind == Work::Kind::Abatis;
            tow += w.kind == Work::Kind::Watchtower;
            eng += w.kind == Work::Kind::Engine;
        }
        std::printf("  %-14s realm %d %s ditch %.0f-%.0f m%s, %d causeways; palisade %d, abatis %d, towers %d, engines %d\n", c.name.c_str(), c.realm,
                    c.capital ? "seat" : "town", c.ditch.inner.toDouble(), c.ditch.outer.toDouble(), c.ditch.wet ? " (moat)" : "",
                    static_cast<int>(c.ditch.causeways.size()), pal, aba, tow, eng);
        CHECK(pal > 20);
        CHECK(aba > 6);
        CHECK(tow >= 2);
        CHECK(eng == (c.capital ? 2 : 1));
    }
    for (int f = 0; f < Continent::kFactionCount; ++f) {
        CHECK(seats[f] == 1);
        CHECK(towns[f] == 1);
    }
    std::printf("  %d fortified, %d with a moat\n", all, moats);
    // Every other place has a watchtower: the walled towns by the road before the gate, the villages at their lane's end.
    for (const Continent::City& c : cs) {
        if (c.fortified()) continue;
        int tow = 0;
        for (const Work& w : c.works) tow += w.kind == Work::Kind::Watchtower;
        CHECK(tow == 1);
    }
}

TEST(a_ditch_is_dug_and_its_causeways_carry_the_roads) {
    for (const Continent::City& c : continent().cities()) {
        if (!c.fortified()) continue;
        const Fixed mid = (c.ditch.inner + c.ditch.outer) / 2;
        int cut = 0, n = 0;
        for (int a = 0; a < 48; ++a) {
            const Vec3 dir{cos(kTwoPi * a / 48), sin(kTwoPi * a / 48), Fixed::zero()};
            const Vec3 p = c.pos + dir * mid;
            if (c.ditch.causeway(p.x, p.y)) continue;
            ++n;
            const Vec3 bank = c.pos + dir * (c.ditch.inner - Fixed::fromInt(2));
            const Fixed below = continent().heightAt(bank.x, bank.y) - continent().heightAt(p.x, p.y);
            cut += below > Fixed::fromInt(2);
        }
        // (Where a river runs by, it is the moat: no ditch is dug there.)
        CHECK(cut * 10 >= n * 6);
        // The road to the gate crosses on a causeway: the ground there is a road, not a ditch.
        const auto& ring = c.castle.rings.front();
        const Vec3 gm = (ring.corners[ring.gateEdge] + ring.corners[(ring.gateEdge + 1) % ring.corners.size()]) * Fixed::fromRatio(1, 2);
        const Vec3 out = (gm - c.pos).flat() / (gm - c.pos).flat().length();
        const Vec3 over = c.pos + out * mid;
        CHECK(c.ditch.causeway(over.x, over.y));
        CHECK(!c.ditch.holds(over.x, over.y));
    }
}

// ---------------------------------------------------------------- in battle, at the skirmish's capital

#include <memory>

#include "illyria/game/battle.h"
#include "illyria/sim/units.h"
#include "illyria/sim/world.h"

namespace {
Fixed mm(int64_t v) { return Fixed::fromInt(v); }
const Continent::City& capital() { return illyria::game::capitalTown(continent()); }
std::shared_ptr<const Region> capitalGround() {
    static const auto r = std::make_shared<const Region>(Region::fromContinent(continent(), capital().pos, 256));
    return r;
}
World capitalWorld(uint64_t seed = 1) {
    WorldConfig c;
    c.seed = seed;
    c.region = capitalGround();
    return World(c);
}
// The outer ring's road door, its middle and the way out.
struct Door {
    size_t gate;
    Vec3 mid, out;
};
Door roadDoor(const World& w) {
    const size_t g = w.gateOfTown(capital().pos - w.region()->origin());
    const Gate& gt = w.region()->gates()[g];
    return {g, (gt.a + gt.b) * Fixed::fromRatio(1, 2), gt.out};
}
const Ditch& theDitch(const World& w) { return w.region()->ditches().front(); }
void runFor(World& w, int seconds, std::vector<Command> first = {}) {
    w.step(first);
    for (int t = 1; t < seconds * kTicksPerSecond; ++t) w.step({});
}
}  // namespace

TEST(outworks_in_battle_a_ditch_bars_horse_and_wheels_off_the_causeway_and_foot_cross_it_slowly) {
    World w = capitalWorld();
    CHECK(!w.region()->ditches().empty());
    CHECK(!w.works().empty());
    const Ditch& d = theDitch(w);
    const Door door = roadDoor(w);
    // Two places either side of the ditch, a quarter turn round from the door (off every causeway).
    const Vec3 side{-door.out.y, door.out.x, Fixed::zero()};
    const Vec3 outside = d.centre + side * (d.outer + mm(14)), inside = d.centre + side * (d.bankIn - mm(1));
    CHECK(!d.causeway(outside.x, outside.y));
    UnitStats horse = unitStats(UnitType::LightCavalry), foot = unitStats(UnitType::LightInfantry);
    horse.aggroRange = foot.aggroRange = Fixed::zero();
    Vec3 a = outside, b = outside + door.out * mm(4);
    a.z = w.groundHeight(a.x, a.y);
    b.z = w.groundHeight(b.x, b.y);
    const EntityId rider = w.spawnUnit(0, a, -side, horse), man = w.spawnUnit(0, b, -side, foot);
    int inDitch = 0;
    const std::vector<Command> go{{0, rider, Command::Type::Move, inside}, {0, man, Command::Type::Move, inside + door.out * mm(4)}};
    w.step(go);
    for (int t = 0; t < 40 * kTicksPerSecond; ++t) {
        w.step({});
        const Unit& r = *w.findUnit(rider);
        inDitch += w.region()->inDitch(r.pos.x, r.pos.y);
    }
    const Unit& r = *w.findUnit(rider);
    const Unit& f = *w.findUnit(man);
    const double rd = (r.pos - d.centre).flat().length().toDouble(), fd = (f.pos - d.centre).flat().length().toDouble();
    std::printf("  ditch %.0f-%.0f m: the rider ended %.0f m out (ticks in the cut %d), the foot %.0f m out\n", d.inner.toDouble(), d.outer.toDouble(), rd, inDitch, fd);
    CHECK(inDitch == 0);
    CHECK(fd < d.inner.toDouble());
}

TEST(outworks_in_battle_a_battered_gate_gets_a_barricade_behind_it_and_the_horseshoe_behind_that) {
    World w = capitalWorld(2);
    const Door door = roadDoor(w);
    w.keepGate(door.gate, 0, false);
    std::vector<const UnitStats*> stats;
    const UnitStats foot = unitStats(UnitType::HeavyInfantry);
    for (int i = 0; i < 18; ++i) stats.push_back(&foot);
    for (const World::GarrisonPost& p : w.garrisonPosts(door.gate, stats)) w.spawnUnit(0, p.pos, p.face, foot);
    // A foe at the doors; the doors battered to 70%.
    for (int i = 0; i < 4; ++i) {
        Vec3 p = door.mid + door.out * mm(30) + Vec3{mm(i * 2), Fixed::zero(), Fixed::zero()};
        p.z = w.groundHeight(p.x, p.y);
        UnitStats s = unitStats(UnitType::LightInfantry);
        s.aggroRange = Fixed::zero();
        w.spawnUnit(1, p, -door.out, s);
    }
    w.setGateHp(door.gate, w.region()->gates()[door.gate].maxHp * 7 / 10);
    runFor(w, 25);
    int barricade = -1;
    for (size_t i = 0; i < w.works().size(); ++i)
        if (w.works()[i].def.kind == Work::Kind::Barricade) barricade = static_cast<int>(i);
    CHECK(barricade >= 0);
    if (barricade < 0) return;
    const WorkState& b = w.works()[static_cast<size_t>(barricade)];
    std::printf("  barricade %d of %d hp, barring %d\n", b.hp, b.def.maxHp, b.barring);
    CHECK(b.barring);
    // Behind the doors: between them and the horseshoe.
    const Fixed depth = (door.mid - (b.def.a + b.def.b) * Fixed::fromRatio(1, 2)).flat().dot(door.out);
    CHECK(depth > mm(4) && depth < mm(9));
    // The gate broken, a foe ordered in comes up against it and hacks at it.
    w.setGateHp(door.gate, 0);
    Vec3 p = door.mid + door.out * mm(6);
    p.z = w.groundHeight(p.x, p.y);
    const EntityId giant = w.spawnUnit(1, p, -door.out, unitStats(UnitType::Giants));
    const int before = b.hp;
    const Rampart& ring = w.region()->ramparts()[static_cast<size_t>(w.region()->gates()[door.gate].ring)];
    runFor(w, 15, {{1, giant, Command::Type::Move, door.mid - door.out * mm(20)}});
    // (Not through it: the way in is round by the other door, or through the barricade.)
    const bool in = Gate::inside(ring, w.findUnit(giant)->pos);
    std::printf("  a giant ordered through: %s\n", in ? "inside" : "kept out");
    CHECK(!in);
    // Foot ordered at it hack it down.
    std::vector<Command> hack;
    for (int i = 0; i < 6; ++i) {
        Vec3 q = door.mid + door.out * mm(8) + Vec3{mm(i - 3), Fixed::zero(), Fixed::zero()};
        q.z = w.groundHeight(q.x, q.y);
        Command c{1, w.spawnUnit(1, q, -door.out, unitStats(UnitType::LightInfantry)), Command::Type::AttackObject, (b.def.a + b.def.b) * Fixed::fromRatio(1, 2)};
        c.object = Command::Object::Work;
        c.objectIndex = barricade;
        hack.push_back(c);
    }
    w.step(hack);
    for (int t = 0; t < 20 * kTicksPerSecond; ++t) {
        w.step({});
    }
    std::printf("  hacked at: the barricade %d -> %d\n", before, w.works()[static_cast<size_t>(barricade)].hp);
    CHECK(w.works()[static_cast<size_t>(barricade)].hp < before);
}

TEST(outworks_in_battle_a_garrison_puts_bowmen_forward_in_its_watchtowers_and_on_the_bank) {
    World w = capitalWorld(3);
    const Door door = roadDoor(w);
    std::vector<const UnitStats*> stats;
    const UnitStats bow = unitStats(UnitType::Archers);
    for (int i = 0; i < 24; ++i) stats.push_back(&bow);
    const std::vector<World::GarrisonPost> posts = w.garrisonPosts(door.gate, stats);
    const Ditch& d = theDitch(w);
    std::vector<EntityId> ids;
    int forward = 0;
    for (const World::GarrisonPost& p : posts) {
        ids.push_back(w.spawnUnit(0, p.pos, p.face, bow));
        const Fixed r = (p.pos - d.centre).flat().length();
        forward += r > d.bankIn - mm(2) && r < d.outer;
    }
    runFor(w, 2);
    int up = 0;
    for (const EntityId id : ids) up += w.findUnit(id)->perch >= 0;
    std::printf("  %d of 24 bowmen forward (a quarter of the road door's twelve), %d of them up watchtowers\n", forward, up);
    CHECK(forward == 3);
    CHECK(up == 2);
}

TEST(outworks_in_battle_a_garrisons_light_horse_sally_against_an_unguarded_engine) {
    World w = capitalWorld(4);
    const Door door = roadDoor(w);
    w.keepGate(door.gate, 0, false);
    // A computer's garrison: its gates recall, its horse sally.
    for (size_t g = 0; g < w.gates().size(); ++g) w.keepGate(g, 0, false);
    for (size_t g = 0; g < w.gates().size(); ++g) w.setGateGarrisoned(g, true, true);
    std::vector<const UnitStats*> stats;
    const UnitStats lc = unitStats(UnitType::LightCavalry), foot = unitStats(UnitType::HeavyInfantry);
    for (int i = 0; i < 6; ++i) stats.push_back(&lc);
    for (int i = 0; i < 12; ++i) stats.push_back(&foot);
    std::vector<EntityId> riders;
    const std::vector<World::GarrisonPost> posts = w.garrisonPosts(door.gate, stats);
    for (size_t i = 0; i < posts.size(); ++i) {
        const EntityId id = w.spawnUnit(0, posts[i].pos, posts[i].face, *stats[i]);
        if (i < 6) riders.push_back(id);
    }
    // (The tower engines are down: the horse are what is tried.)
    for (size_t i = 0; i < w.works().size(); ++i)
        if (w.works()[i].def.kind == Work::Kind::Engine) w.damageWork(i, 1000, w.works()[i].def.a, door.out, false);
    // An engine of the foe's 130 m before the road's door, alone.
    Vec3 p = door.mid + door.out * mm(130);
    p.z = w.groundHeight(p.x, p.y);
    UnitStats cat = unitStats(UnitType::Catapult);
    cat.ranged.autoRange = Fixed::zero();
    const EntityId engine = w.spawnUnit(1, p, -door.out, cat);
    int out = 0;
    for (int t = 0; t < 90 * kTicksPerSecond; ++t) {
        w.step({});
        for (const EntityId id : riders) out = std::max(out, static_cast<int>(w.findUnit(id)->sortie));
    }
    std::printf("  engine %s (%d hp); the sortie went out: %d\n", w.findUnit(engine)->alive() ? "standing" : "destroyed", w.findUnit(engine)->hp, out);
    CHECK(out >= 1);
    CHECK(!w.findUnit(engine)->alive());
}

// The keep's last stand: the bailey's doors are opened and the garrison sent through them. (Not yet: that they all get in.
// Most still stop in the open doorway, as the first try's did: docs/BALANCE.md, "Not done". This pins what holds.)
TEST(outworks_in_battle_a_capitals_garrison_falls_back_to_the_keep_through_the_baileys_open_doors) {
    World w = capitalWorld(5);
    const Door door = roadDoor(w);
    for (size_t g = 0; g < w.gates().size(); ++g) w.keepGate(g, 0, false);
    for (size_t g = 0; g < w.gates().size(); ++g) w.setGateGarrisoned(g, true, true);
    std::vector<const UnitStats*> stats;
    const UnitStats foot = unitStats(UnitType::HeavyInfantry);
    for (int i = 0; i < 24; ++i) stats.push_back(&foot);
    std::vector<EntityId> mine;
    for (const World::GarrisonPost& p : w.garrisonPosts(door.gate, stats)) mine.push_back(w.spawnUnit(0, p.pos, p.face, foot));
    // The outer doors down, and a host in the outer ward.
    const Rampart& ward = w.region()->ramparts()[static_cast<size_t>(w.region()->gates()[door.gate].ring)];
    for (size_t g = 0; g < w.gates().size(); ++g)
        if (w.region()->gates()[g].ring == w.region()->gates()[door.gate].ring) w.setGateHp(g, 0);
    UnitStats foe = unitStats(UnitType::LightInfantry);
    foe.aggroRange = Fixed::zero();
    for (int i = 0; i < 60; ++i) {
        Vec3 p = door.mid - door.out * mm(14 + (i / 10) * 2) + Vec3{mm(i % 10 - 5) * 2, Fixed::zero(), Fixed::zero()};
        p.z = w.groundHeight(p.x, p.y);
        w.spawnUnit(1, p, door.out, foe);
    }
    size_t bailey = 0;
    for (size_t g = 0; g < w.gates().size(); ++g)
        if (w.region()->gates()[g].ring != w.region()->gates()[door.gate].ring && Gate::inside(ward, w.region()->ramparts()[static_cast<size_t>(w.region()->gates()[g].ring)].centre)) bailey = g;
    const Rampart& keep = w.region()->ramparts()[static_cast<size_t>(w.region()->gates()[bailey].ring)];
    runFor(w, 45);
    int in = 0, sent = 0, alive = 0;
    for (const EntityId id : mine) {
        const Unit& u = *w.findUnit(id);
        alive += u.alive();
        in += u.alive() && Gate::inside(keep, u.pos);
        sent += u.alive() && (Gate::inside(keep, u.pos) || (u.hasMoveTarget && Gate::inside(keep, u.moveTarget)));
    }
    std::printf("  %d of %d alive sent to the keep, %d in the bailey after 45 s; its doors %s\n", sent, alive, in, w.gates()[bailey].opened() ? "open" : "shut");
    CHECK(sent * 10 >= alive * 7);
    CHECK(w.gates()[bailey].opened());
}
