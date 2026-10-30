// Horde (game/src/mode_horde.cpp, engine/sim/src/mode_rules.cpp): the waves
// escalate the same way everywhere, the hill rule, the break's reinforcements,
// healing and the hero's level, and a four-seat match that a replay of its
// seats' inputs reproduces exactly.
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/game/mode_horde.h"
#include "illyria/sim/mode_rules.h"
#include "test.h"

using namespace illyria;
using namespace illyria::game;
using sim::Fixed;
using sim::UnitType;

namespace {

std::shared_ptr<const sim::Continent> continent() {
    static const auto c = std::make_shared<const sim::Continent>(Battle::kRegionSeed);
    return c;
}

// The first recommended hill of the continent.
const Place& hillPlace() {
    static const Place p = [] {
        for (const Place& pl : battlePlaces(*continent()))
            if (pl.kind == Place::Kind::Hill) return pl;
        return battlePlaces(*continent()).front();
    }();
    return p;
}

std::unique_ptr<Battle> field(uint64_t seed) { return std::make_unique<Battle>(seed, hillPlace().pos, continent()); }

HordeOptions options(uint64_t seed) {
    HordeOptions o;
    o.centre = hillPlace().pos;
    o.place = hillPlace().name;
    o.seed = seed;
    o.musterSeconds = 2;
    o.breakSeconds = 20;
    return o;
}

void run(Battle& b, int ticks) {
    for (int i = 0; i < ticks; ++i) b.advance(1.0 / sim::kTicksPerSecond + 1e-6);
}

// Each squad of the local seat but its hero goes at the nearest of the horde.
void charge(Battle& b, const HordeMode& h) {
    for (size_t q = 0; q < b.squads().size(); ++q) {
        if (h.seatOfSquad(q) != 0 || static_cast<int>(q) == h.heroSquad(0) || b.aliveCount(b.squads()[q]) == 0) continue;
        const V3 c = b.squadCentre(b.squads()[q], false);
        const sim::EntityId foe = b.nearestFoe(HordeMode::kHolders, c, 200.0f);
        if (foe == sim::kNoEntity) continue;
        b.selectSquad(static_cast<int>(q));
        b.orderAttackGround(toV3(b.world().units()[foe - 1].pos));
    }
    b.clearSelection();
}

int distinctKinds(const sim::WavePlan& p) {
    std::vector<UnitType> seen;
    for (const sim::WaveGroup& g : p.groups)
        if (std::find(seen.begin(), seen.end(), g.type) == seen.end()) seen.push_back(g.type);
    return static_cast<int>(seen.size());
}

}  // namespace

TEST(horde_waves_escalate_deterministically) {
    int prev = 0;
    for (int w = 1; w <= 15; ++w) {
        const sim::WavePlan a = sim::hordeWave(w, 1, 1, 77), b = sim::hordeWave(w, 1, 1, 77);
        sim::StateHasher ha, hb;
        sim::hashWave(ha, a);
        sim::hashWave(hb, b);
        CHECK(ha.value() == hb.value());  // the same plan from the same seed
        CHECK(a.strength > 0 && !a.groups.empty());
        // Never weaker than the last ordinary wave (a boss's comes swollen).
        if (!a.boss) {
            CHECK(a.strength > prev);
            prev = a.strength;
        }
        CHECK(a.boss == (w % 5 == 0));
        CHECK(a.elite == (w % 3 == 0 && w % 5 != 0));
        int bosses = 0;
        for (const sim::WaveGroup& g : a.groups) bosses += g.boss;
        CHECK(bosses == (a.boss ? 1 : 0));
    }
    const sim::WavePlan w1 = sim::hordeWave(1, 1, 1, 77), w5 = sim::hordeWave(5, 1, 1, 77), w10 = sim::hordeWave(10, 1, 1, 77),
                        w15 = sim::hordeWave(15, 1, 1, 77);
    CHECK(w10.strength > 2 * w1.strength && w15.strength > w10.strength);
    // More kinds as it goes: the dead alone at first.
    CHECK(distinctKinds(w1) == 1 && w1.groups.front().type == UnitType::Undead);
    CHECK(distinctKinds(w10) > distinctKinds(w5) - 1 && distinctKinds(w10) >= 4);
    CHECK(w10.groups.front().type == UnitType::Dragons && w5.groups.front().type == UnitType::Demons);
    // Seasoned later, from more edges.
    CHECK(w1.groups.front().rank == 0 && w15.groups.back().rank >= 2);
    std::vector<uint8_t> edges;
    for (const sim::WaveGroup& g : w10.groups)
        if (std::find(edges.begin(), edges.end(), g.edge) == edges.end()) edges.push_back(g.edge);
    CHECK(edges.size() >= 3);
    // Harder settings and more seats on the hill bring more.
    CHECK(sim::hordeWave(6, 2, 1, 77).strength > sim::hordeWave(6, 1, 1, 77).strength);
    CHECK(sim::hordeWave(6, 1, 1, 77).strength > sim::hordeWave(6, 0, 1, 77).strength);
    CHECK(sim::hordeWave(6, 1, 4, 77).strength > sim::hordeWave(6, 1, 1, 77).strength * 2);
    // Another seed, another order of kinds and edges, the same weight.
    sim::StateHasher h1, h2;
    sim::hashWave(h1, sim::hordeWave(8, 1, 1, 77));
    sim::hashWave(h2, sim::hordeWave(8, 1, 1, 78));
    CHECK(h1.value() != h2.value());
}

TEST(horde_hill_rule) {
    // A flat field: the horde alone on the hill takes it after kLoseSeconds;
    // one holder on it keeps it however many foes stand there.
    const auto ground = std::make_shared<const sim::Region>(sim::Region::fromGround(32, std::vector<sim::Ground>(32 * 32, sim::Ground::Plains)));
    for (int holder = 0; holder < 2; ++holder) {
        sim::WorldConfig cfg;
        cfg.region = ground;
        sim::World w(cfg);
        const sim::Vec3 hill{Fixed::fromInt(64), Fixed::fromInt(64), Fixed::zero()};
        // Soldiers who neither strike nor go after anyone: only where they stand counts.
        sim::UnitStats dead = sim::unitStats(UnitType::Undead);
        dead.melee.damage = 0;
        dead.aggroRange = Fixed::zero();
        sim::UnitStats bowman = sim::unitStats(UnitType::LightInfantry);
        bowman.melee.damage = 0;
        bowman.aggroRange = Fixed::zero();
        bowman.moraleShockPct = 0;  // three to one would break him
        for (int i = 0; i < 3; ++i) w.spawnUnit(1, hill + sim::Vec3{Fixed::fromInt(i * 2), Fixed::zero(), Fixed::zero()}, {Fixed::one(), {}, {}}, dead);
        // A holder far off (it does not count) or on the top (it does). Far enough that nobody fights.
        const sim::Vec3 at = holder ? hill + sim::Vec3{Fixed::zero(), Fixed::fromInt(8), Fixed::zero()}
                                    : hill + sim::Vec3{Fixed::zero(), Fixed::fromInt(50), Fixed::zero()};
        w.spawnUnit(0, at, {Fixed::one(), {}, {}}, bowman);
        sim::HillRule rule;
        rule.place(hill, 0);
        int lostAt = -1;
        for (int t = 1; t <= sim::HillRule::kLoseTicks + 5; ++t) {
            w.step({});
            rule.step(w);
            if (rule.lost() && lostAt < 0) lostAt = t;
        }
        CHECK(rule.foesIn() >= 1);
        if (holder) {
            CHECK(!rule.lost() && rule.contestedTicks() == 0 && rule.holdersIn() == 1 && rule.heldTicks() > 0);
        } else {
            CHECK(rule.lost() && lostAt == sim::HillRule::kLoseTicks && rule.holdersIn() == 0);
        }
    }
}

TEST(horde_reinforcements_between_rounds) {
    auto b = field(5);
    HordeMode& h = HordeMode::start(*b, options(5));
    // The hill, flagged, and one seat's hero and army on it.
    CHECK(h.phase() == HordeMode::Phase::Muster);
    CHECK(h.heroSquad(0) >= 0 && h.heroSquad(1) < 0);
    CHECK(h.flags(*b).size() == 1);
    int holders = 0;
    for (const sim::Unit& u : b->world().units()) holders += u.owner == HordeMode::kHolders;
    CHECK(holders == 1 + 8 + 6 + 8 + 2);
    run(*b, 3 * sim::kTicksPerSecond);
    CHECK(h.phase() == HordeMode::Phase::Wave && h.wave() == 1 && h.enemiesLeft(*b) == h.enemiesThisWave() && h.enemiesThisWave() > 0);
    // Fight it out; watch the wounded and the hero as the wave ends.
    const sim::EntityId hero = b->squads()[static_cast<size_t>(h.heroSquad(0))].members.front();
    std::vector<int32_t> hpBefore;
    int32_t heroRank = 0;
    for (int t = 0; t < 400 * sim::kTicksPerSecond && h.phase() == HordeMode::Phase::Wave; ++t) {
        hpBefore.clear();
        for (const sim::Unit& u : b->world().units()) hpBefore.push_back(u.hp);
        heroRank = b->world().findUnit(hero)->rank;
        // The player: every squad but the hero at the nearest foe now and then.
        if (t % (5 * sim::kTicksPerSecond) == 0) charge(*b, h);
        run(*b, 1);
    }
    CHECK(h.phase() == HordeMode::Phase::Break);
    CHECK(h.wavesSurvived() == 1 && h.enemiesLeft(*b) == 0);
    CHECK(h.points(0) == HordeMode::kRoundPoints && h.offers().size() == static_cast<size_t>(HordeMode::kOffersShown));
    CHECK(b->world().findUnit(hero)->rank == heroRank + 1);  // a level for the round
    int mended = 0;
    for (size_t i = 0; i < hpBefore.size(); ++i) {
        const sim::Unit& u = b->world().units()[i];
        if (u.owner != HordeMode::kHolders || !u.alive() || u.stats.type == static_cast<uint8_t>(UnitType::Hero)) continue;
        const int32_t want = hpBefore[i] + (u.stats.maxHp - hpBefore[i]) * HordeMode::kHealPct / 100;
        CHECK(u.hp >= want);
        mended += hpBefore[i] < u.stats.maxHp;
    }
    std::printf("  wave 1 beaten at tick %u, %d wounded mended, %d kills\n", b->world().tick(), mended, h.kills(*b));
    // A pick: the points spent, the squad on the hill, the local seat's.
    const size_t squads = b->squads().size();
    const sim::Offer& o = sim::squadOffers()[h.offers()[0]];
    h.act({sim::MatchAction::Kind::Recruit, 0});
    h.act({sim::MatchAction::Kind::Recruit, 0});
    h.act({sim::MatchAction::Kind::Recruit, 0});  // the third is more than the points
    run(*b, 1);
    CHECK(b->squads().size() == squads + 2);
    CHECK(h.points(0) == HordeMode::kRoundPoints - 2 * o.cost);
    CHECK(b->squads().back().type == o.type && static_cast<int>(b->squads().back().members.size()) == o.count);
    CHECK(h.seatOfSquad(squads) == 0);
    CHECK(b->controllable(b->world().units()[b->squads().back().members.front() - 1]));
    // Ready: the next wave comes at once, stronger.
    h.act({sim::MatchAction::Kind::Ready, 0});
    run(*b, 1);
    CHECK(h.phase() == HordeMode::Phase::Wave && h.wave() == 2);
}

TEST(horde_hill_lost_when_abandoned) {
    // Everyone marches off the hill while the horde comes: lost after kLoseSeconds of the horde alone on it.
    auto b = field(9);
    HordeMode& h = HordeMode::start(*b, options(9));
    run(*b, 3 * sim::kTicksPerSecond);
    CHECK(h.phase() == HordeMode::Phase::Wave);
    std::vector<sim::EntityId> all;
    for (const sim::Unit& u : b->world().units())
        if (u.owner == HordeMode::kHolders) all.push_back(u.id);
    b->select(all);
    // The far side from where the first group comes.
    const sim::Vec3 away = h.hill().at() - compass(h.plan().groups.front().edge) * Fixed::fromInt(110);
    b->orderForceMarch(toV3(away));
    for (int t = 0; t < 500 * sim::kTicksPerSecond && h.phase() == HordeMode::Phase::Wave; ++t) run(*b, 1);
    CHECK(h.phase() == HordeMode::Phase::Lost && h.hill().lost() && b->outcome() == 2);
    CHECK(h.hill().contestedTicks() == sim::HillRule::kLoseTicks);
}

TEST(horde_four_seats_deterministic) {
    // One person (who orders, picks and readies) and three computer seats;
    // twice, then replayed from the seats' inputs alone.
    const auto play = [](const sim::InputLog* replay, uint64_t& world, uint64_t& mode, uint64_t& inputs, int& waves) {
        auto b = field(11);
        HordeOptions o = options(11);
        o.seats = {sim::SeatControl::Human, sim::SeatControl::Computer, sim::SeatControl::Computer, sim::SeatControl::Computer};
        o.breakSeconds = 5;
        o.waves = 0;
        o.difficulty = 0;
        HordeMode& h = HordeMode::start(*b, o);
        if (replay) h.replay(*replay);
        for (int t = 0; t < 200 * sim::kTicksPerSecond; ++t) {
            if (!replay && t == 4 * sim::kTicksPerSecond) {
                b->selectSquad(1);
                b->orderMove(toV3(h.hill().at()) + V3{6, 4, 0});
            }
            if (!replay && h.phase() == HordeMode::Phase::Break && h.points(0) >= 3) h.act({sim::MatchAction::Kind::Recruit, 0});
            run(*b, 1);
        }
        world = b->world().checksum();
        mode = h.checksum();
        inputs = h.log().checksum();
        waves = h.wave();
        int seats = 0;
        for (int s = 0; s < 4; ++s) seats += h.heroSquad(s) >= 0;
        CHECK(seats == 4);
        return std::make_unique<sim::InputLog>(h.log());
    };
    uint64_t w1, m1, i1, w2, m2, i2, w3, m3, i3;
    int waves1, waves2, waves3;
    const auto log = play(nullptr, w1, m1, i1, waves1);
    play(nullptr, w2, m2, i2, waves2);
    play(log.get(), w3, m3, i3, waves3);
    std::printf("  four seats: wave %d, %zu inputs, world %016llx\n", waves1, log->inputs().size(), static_cast<unsigned long long>(w1));
    CHECK(waves1 >= 2);  // through a break, its picks in the inputs
    CHECK(w1 == w2 && m1 == m2 && i1 == i2);
    CHECK(w1 == w3 && m1 == m3 && i1 == i3);  // the inputs are the whole of it
    // Every seat played: inputs from each of the four and the horde.
    bool from[5] = {};
    for (const sim::SeatInput& in : log->inputs()) from[in.seat] = true;
    CHECK(from[0] && from[1] && from[2] && from[3] && from[HordeMode::kHordeSeat]);
}

TEST(horde_break_mends_and_raises) {
    // The break's rules on the soldiers (world_match.cpp): mending up to the
    // full, a hero's level and its hit points, a soldier's rank to elite.
    sim::World w(sim::WorldConfig{});
    const sim::Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    const sim::EntityId hero = w.spawnUnit(0, {}, east, sim::unitStats(UnitType::Hero));
    const sim::EntityId man = w.spawnUnit(0, {Fixed::fromInt(5), Fixed::zero(), Fixed::zero()}, east, sim::unitStats(UnitType::Warrior));
    const int32_t full = w.findUnit(hero)->stats.maxHp;
    w.heal(hero, 50);
    CHECK(w.findUnit(hero)->hp == full);  // never past the full
    w.levelUp(hero);
    CHECK(w.findUnit(hero)->rank == 1 && w.findUnit(hero)->stats.maxHp > full && w.findUnit(hero)->hp == w.findUnit(hero)->stats.maxHp);
    for (int i = 0; i < 5; ++i) w.levelUp(man);
    CHECK(w.findUnit(man)->rank == 3);
    const uint64_t before = w.checksum();
    w.breakSide(1);  // nobody of side 1: nothing but the record
    CHECK(w.sideBroken(1) && !w.sideBroken(0) && w.checksum() != before);
}
