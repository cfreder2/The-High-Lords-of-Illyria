// Capture the flag (game/src/mode_ctf.cpp, engine/sim/src/mode_rules.cpp):
// Myth's rule (an enemy alone on the flag takes it; a defender on it stops
// that), a fallen team out of the match for good, the last flag standing
// winning, the computer's teams playing a match to its end, and four teams
// reproduced exactly by a replay of their inputs.
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/game/mode_ctf.h"
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

const Place& firstPlace() {
    static const Place p = battlePlaces(*continent()).front();
    return p;
}

sim::Vec3 at(int x, int y) { return {Fixed::fromInt(x), Fixed::fromInt(y), Fixed::zero()}; }

std::shared_ptr<const sim::Region> flatGround() {
    return std::make_shared<const sim::Region>(sim::Region::fromGround(48, std::vector<sim::Ground>(48 * 48, sim::Ground::Plains)));
}

void run(Battle& b, int ticks) {
    for (int i = 0; i < ticks; ++i) b.advance(1.0 / sim::kTicksPerSecond + 1e-6);
}

CtfOptions computerTeams(int teams, uint64_t seed) {
    CtfOptions o;
    o.centre = firstPlace().pos;
    o.place = firstPlace().name;
    o.teams = teams;
    for (int t = 0; t < teams; ++t) o.seats[static_cast<size_t>(t)] = {sim::SeatControl::Computer, t};
    o.budget = 20;
    o.ringM = 90;
    o.seed = seed;
    return o;
}

}  // namespace

TEST(ctf_flag_capture_rule) {
    const auto ground = flatGround();
    const sim::UnitStats man = sim::unitStats(UnitType::LightInfantry);
    const sim::Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    // An enemy alone on it: it falls, to him.
    {
        sim::WorldConfig cfg;
        cfg.region = ground;
        sim::World w(cfg);
        w.spawnUnit(1, at(62, 60), east, man);       // 2 m off the flag
        w.spawnUnit(2, at(60, 60 + 20), east, man);  // near, not on it
        sim::FlagRule flags;
        flags.add(at(60, 60), 0);
        flags.add(at(150, 150), 1);
        w.step({});
        const std::vector<size_t> fell = flags.step(w);
        CHECK(fell.size() == 1 && fell.front() == 0);
        CHECK(!flags.flags()[0].standing && flags.flags()[0].takenBy == 1 && flags.flags()[0].attackers == 1);
        CHECK(flags.flags()[0].threats == 2);
        CHECK(flags.standing() == 1 && flags.lastStanding() == 1);
    }
    // A defender on it with him: it stands, however long.
    {
        sim::WorldConfig cfg;
        cfg.region = ground;
        sim::World w(cfg);
        w.spawnUnit(1, at(62, 60), east, man);
        w.spawnUnit(1, at(60, 62), east, man);
        const sim::EntityId guard = w.spawnUnit(0, at(58, 60), east, man);
        sim::FlagRule flags;
        flags.add(at(60, 60), 0);
        bool fell = false;
        for (int t = 0; t < 10 * sim::kTicksPerSecond && w.findUnit(guard)->alive(); ++t) {
            w.step({});
            fell = fell || !flags.step(w).empty();
            if (w.findUnit(guard)->alive() && (w.findUnit(guard)->pos - at(60, 60)).flat().length() < Fixed::fromInt(sim::FlagRule::kRadiusM))
                CHECK(flags.flags()[0].standing);
        }
        CHECK(flags.flags()[0].defenders >= 0);
        // Once he is cut down (two on one) the flag goes at once.
        if (!w.findUnit(guard)->alive()) {
            w.step({});
            flags.step(w);
            CHECK(!flags.flags()[0].standing);
        }
    }
    // Far enemies do not take it; the broken do not either.
    {
        sim::WorldConfig cfg;
        cfg.region = ground;
        sim::World w(cfg);
        w.spawnUnit(1, at(60, 70), east, man);
        w.spawnUnit(3, at(61, 60), east, man);
        w.breakSide(3);
        sim::FlagRule flags;
        flags.add(at(60, 60), 0);
        w.step({});
        CHECK(flags.step(w).empty() && flags.flags()[0].standing && flags.flags()[0].threats == 1);
    }
}

TEST(ctf_elimination_and_victory) {
    // Three teams' flags: two fall in turn; the fallen side breaks and never
    // rallies; the last flag standing wins.
    const auto ground = flatGround();
    sim::WorldConfig cfg;
    cfg.region = ground;
    sim::World w(cfg);
    const sim::UnitStats man = sim::unitStats(UnitType::LightInfantry);
    const sim::Vec3 east{Fixed::one(), Fixed::zero(), Fixed::zero()};
    sim::FlagRule flags;
    flags.add(at(20, 20), 0);
    flags.add(at(100, 20), 1);
    flags.add(at(60, 150), 2);
    const sim::EntityId raider = w.spawnUnit(0, at(101, 20), east, man);
    std::vector<sim::EntityId> twos;
    for (int i = 0; i < 4; ++i) twos.push_back(w.spawnUnit(2, at(60 + i * 2, 120), east, man));
    w.step({});
    for (size_t f : flags.step(w)) w.breakSide(flags.flags()[f].team);
    CHECK(!flags.flags()[1].standing && flags.lastStanding() == sim::FlagRule::kNobody && flags.standing() == 2);
    CHECK(w.sideBroken(1) && !w.sideBroken(0));
    // Team 2 out too: its men routed, and still routed long after.
    w.spawnUnit(0, at(60, 151), east, man);
    w.step({});
    for (size_t f : flags.step(w)) w.breakSide(flags.flags()[f].team);
    CHECK(flags.lastStanding() == 0 && w.sideBroken(2));
    for (int t = 0; t < 60 * sim::kTicksPerSecond; ++t) w.step({});
    for (sim::EntityId id : twos) CHECK(w.findUnit(id)->routed);
    // Their orders are not heeded.
    sim::Command c{2, twos[0], sim::Command::Type::Move, at(10, 10)};
    w.step({&c, 1});
    CHECK(!w.findUnit(twos[0])->hasMoveTarget);
    CHECK(w.findUnit(raider)->alive());
}

TEST(ctf_computer_teams_play_to_a_winner) {
    for (int teams : {2, 3}) {
        auto b = std::make_unique<Battle>(21 + teams, firstPlace().pos, continent());
        CtfOptions o = computerTeams(teams, 21);
        o.timeLimitSeconds = 0;  // only the flags can end it
        CtfMode& m = CtfMode::start(*b, o);
        CHECK(m.flagRule().flags().size() == static_cast<size_t>(teams));
        CHECK(m.flags(*b).size() == static_cast<size_t>(teams));
        const int limit = 15 * 60 * sim::kTicksPerSecond;
        int t = 0;
        for (; t < limit && m.phase() == CtfMode::Phase::Battle; ++t) run(*b, 1);
        std::printf("  %d teams: over after %d s, winner %d\n", teams, t / sim::kTicksPerSecond, m.winner());
        CHECK(m.phase() == CtfMode::Phase::Over);
        CHECK(m.winner() != CtfMode::kNobody);
        CHECK(m.flagRule().standing() == 1);
        for (int k = 0; k < teams; ++k)
            if (k != m.winner()) CHECK(b->world().sideBroken(static_cast<sim::PlayerId>(k)));
        CHECK(m.outcome() == (m.winner() == m.localTeam() ? 1 : 2));
    }
}

TEST(ctf_time_limit) {
    // Nobody attacks (two people who never give an order): when time runs
    // out the stronger army with its flag wins.
    auto b = std::make_unique<Battle>(31, firstPlace().pos, continent());
    CtfOptions o = computerTeams(2, 31);
    o.seats[0] = {sim::SeatControl::Human, 0};
    o.seats[1] = {sim::SeatControl::Human, 1};
    o.army = {0, 0, 0, 0};  // the local seat: four squads of warriors, a stronger army than the default's
    o.ringM = 200;
    o.timeLimitSeconds = 5;
    CtfMode& m = CtfMode::start(*b, o);
    run(*b, 6 * sim::kTicksPerSecond);
    CHECK(m.phase() == CtfMode::Phase::Over && m.flagRule().standing() == 2);
    CHECK(m.winner() == (m.strength(*b, 0) > m.strength(*b, 1) ? 0 : 1));
    CHECK(m.strength(*b, 0) != m.strength(*b, 1));
}

TEST(ctf_four_teams_deterministic) {
    const auto play = [](const sim::InputLog* replay, uint64_t& world, uint64_t& mode, uint64_t& inputs) {
        auto b = std::make_unique<Battle>(41, firstPlace().pos, continent());
        CtfMode& m = CtfMode::start(*b, computerTeams(4, 41));
        if (replay) m.replay(*replay);
        run(*b, 90 * sim::kTicksPerSecond);
        world = b->world().checksum();
        mode = m.checksum();
        inputs = m.log().checksum();
        return std::make_unique<sim::InputLog>(m.log());
    };
    uint64_t w1, m1, i1, w2, m2, i2, w3, m3, i3;
    const auto log = play(nullptr, w1, m1, i1);
    play(nullptr, w2, m2, i2);
    play(log.get(), w3, m3, i3);
    std::printf("  four teams: %zu inputs, world %016llx\n", log->inputs().size(), static_cast<unsigned long long>(w1));
    CHECK(w1 == w2 && m1 == m2 && i1 == i2);
    CHECK(w1 == w3 && m1 == m3 && i1 == i3);
    bool from[4] = {};
    for (const sim::SeatInput& in : log->inputs()) from[in.seat] = true;
    CHECK(from[0] && from[1] && from[2] && from[3]);
}
