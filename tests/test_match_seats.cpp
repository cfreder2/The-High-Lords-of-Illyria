// Seats in a match (game/src/mode.cpp, mode_horde.cpp, mode_ctf.cpp): Horde's
// fallen hero coming back between waves (by himself after the next wave, or
// recalled for points) and the morale blow of his fall; and the local seat,
// which is only a view: the same inputs make the same match from any seat,
// while selection, orders, the HUD and the alerts follow the seat at the screen.
#include <cstdio>
#include <memory>

#include "illyria/game/battle.h"
#include "illyria/game/mode_ctf.h"
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
    o.difficulty = 0;
    return o;
}

void run(Battle& b, int ticks) {
    for (int i = 0; i < ticks; ++i) b.advance(1.0 / sim::kTicksPerSecond + 1e-6);
}

// The local seat's player: his fighting squads go at the nearest of the
// horde near the hill, else back up to it; the bowmen and healers keep it.
void charge(Battle& b, const HordeMode& h, float reach = 60.0f) {
    const int seat = h.localSeat();
    const V3 hill = toV3(h.hill().at());
    for (size_t q = 0; q < b.squads().size(); ++q) {
        if (h.seatOfSquad(q) != seat || static_cast<int>(q) == h.heroSquad(seat) || b.aliveCount(b.squads()[q]) == 0) continue;
        const V3 c = b.squadCentre(b.squads()[q], false);
        const sim::EntityId foe = roleOf(b.squads()[q].type) == Role::Melee ? b.nearestFoe(HordeMode::kHolders, hill, reach) : sim::kNoEntity;
        b.selectSquad(static_cast<int>(q));
        if (foe != sim::kNoEntity) b.orderAttackGround(toV3(b.world().units()[foe - 1].pos));
        else if (V3{c.x - hill.x, c.y - hill.y, 0}.length() > 12.0f) b.orderMove(hill);
    }
    b.clearSelection();
}

// Fights the current wave to its end: true once the break comes.
bool beatWave(Battle& b, HordeMode& h) {
    for (int t = 0; t < 500 * sim::kTicksPerSecond && h.phase() == HordeMode::Phase::Wave; ++t) {
        if (t % (5 * sim::kTicksPerSecond) == 0) charge(b, h);
        run(b, 1);
    }
    return h.phase() == HordeMode::Phase::Break;
}

// A Horde whose hero (given a level first) is cut down in the muster by his
// own warriors (a blow at a friend is an order like any other), then the
// first wave sets out. What he was when he fell, and the lowest morale of
// the warriors who saw it just before and just after.
struct Fallen {
    std::unique_ptr<Battle> b;
    HordeMode* h = nullptr;
    sim::EntityId hero = sim::kNoEntity;
    int32_t rank = 0, kills = 0, maxHp = 0, moraleBefore = 0, moraleAfter = 0;
};
Fallen fallInTheMuster(uint64_t seed, const sim::InputLog* replay = nullptr) {
    Fallen f;
    f.b = field(seed);
    HordeOptions o = options(seed);
    o.musterSeconds = 120;
    f.h = &HordeMode::start(*f.b, o);
    if (replay) f.h->replay(*replay);
    Battle& b = *f.b;
    f.hero = f.h->hero(b, 0);
    b.mutableWorld().levelUp(f.hero);
    const sim::Unit& was = *b.world().findUnit(f.hero);
    f.rank = was.rank;
    f.kills = was.kills;
    f.maxHp = was.stats.maxHp;
    const int warriors = f.h->heroSquad(0) + 1;  // raised just after him
    b.selectSquad(warriors);
    b.orderAttackUnit(f.hero);
    b.clearSelection();
    const auto lowest = [&] {
        int32_t m = sim::kMoraleFull;
        for (sim::EntityId id : b.squads()[static_cast<size_t>(warriors)].members) m = std::min(m, b.world().findUnit(id)->morale);
        return m;
    };
    for (int t = 0; t < 90 * sim::kTicksPerSecond && b.world().findUnit(f.hero)->alive(); ++t) {
        f.moraleBefore = lowest();
        run(b, 1);
    }
    f.moraleAfter = lowest();
    f.h->skipBreak();
    run(b, 2);
    return f;
}

std::shared_ptr<const sim::Continent> ctfContinent() { return continent(); }
const Place& ctfPlace() {
    static const Place p = battlePlaces(*continent()).front();
    return p;
}

CtfOptions ctfOptions(int local) {
    // Three teams: a person on blue (seat 0), the computer on red (seat 1),
    // a person on gold (seat 2); `local` is the one at this screen.
    CtfOptions o;
    o.centre = ctfPlace().pos;
    o.place = ctfPlace().name;
    o.teams = 3;
    o.seats[0] = {sim::SeatControl::Human, 0};
    o.seats[1] = {sim::SeatControl::Computer, 1};
    o.seats[2] = {sim::SeatControl::Human, 2};
    o.localSeat = local;
    o.budget = 20;
    o.ringM = 90;
    o.seed = 29;
    return o;
}

}  // namespace

TEST(horde_hero_fall_shakes_the_hill) {
    // As in any battle: the men who see their hero fall are shaken to breaking.
    Fallen f = fallInTheMuster(13);
    CHECK(!f.b->world().findUnit(f.hero)->alive());
    std::printf("  warriors' lowest morale %d before the hero fell, %d after\n", f.moraleBefore, f.moraleAfter);
    CHECK(f.moraleBefore - f.moraleAfter >= 400);
    CHECK(f.moraleAfter < sim::kMoraleWaver);
    CHECK(f.h->heroFallen(*f.b, 0));
}

TEST(horde_fallen_hero_returns_after_the_next_wave) {
    // Not recalled: he sits out the wave after his fall and returns when it
    // is beaten, at half his health, his level and kills kept, still his seat's.
    Fallen f = fallInTheMuster(13);
    Battle& b = *f.b;
    HordeMode& h = *f.h;
    CHECK(h.phase() == HordeMode::Phase::Wave && h.wave() == 1);
    CHECK(beatWave(b, h));
    CHECK(h.heroFallen(b, 0) && h.heroFellIn(0) == 1);
    const std::string hud = h.hudJson(b);
    CHECK(hud.find("\"name\":\"Lord Aldric\",\"alive\":false,\"level\":2") != std::string::npos);
    CHECK(hud.find("\"returnsAfter\":2") != std::string::npos);
    h.act({sim::MatchAction::Kind::Ready, 0});
    run(b, 1);
    CHECK(h.phase() == HordeMode::Phase::Wave && h.wave() == 2 && h.heroFallen(b, 0));
    const bool beaten = beatWave(b, h);
    std::printf("  wave 2 %s at tick %u\n", beaten ? "beaten" : "not beaten", b.world().tick());
    CHECK(beaten);
    CHECK(!h.heroFallen(b, 0) && h.heroFellIn(0) == 0);
    const sim::EntityId back = h.hero(b, 0);
    const sim::Unit& u = *b.world().findUnit(back);
    std::printf("  back after wave 2: level %d, %d of %d hp\n", u.rank + 1, u.hp, u.stats.maxHp);
    CHECK(back != f.hero && u.alive() && u.owner == HordeMode::kHolders);
    CHECK(u.rank == f.rank && u.kills == f.kills && u.stats.maxHp == f.maxHp);
    CHECK(u.hp == u.stats.maxHp * HordeMode::kReturnHpPct / 100);
    CHECK((u.pos - h.hill().at()).flat().length() < Fixed::fromInt(30));  // on the hill
    CHECK(b.controllable(u) && h.roster().seatOf(back) == 0 && b.squadOf(back) == h.heroSquad(0));
    CHECK(b.squads()[static_cast<size_t>(h.heroSquad(0))].members.size() == 1);
    CHECK(h.points(0) == 2 * HordeMode::kRoundPoints + 1);  // nothing paid (wave 2 brings a point more)
}

TEST(horde_fallen_hero_recalled_for_points) {
    // Recalled in the break after his fall: back at once for kRecallPoints,
    // through the seat's input, so a replay brings him back the same.
    uint64_t world = 0, mode = 0;
    sim::InputLog log;
    for (int pass = 0; pass < 2; ++pass) {
        Fallen f = fallInTheMuster(17, pass ? &log : nullptr);
        Battle& b = *f.b;
        HordeMode& h = *f.h;
        CHECK(beatWave(b, h));
        CHECK(h.heroFallen(b, 0) && h.points(0) == HordeMode::kRoundPoints);
        h.act({sim::MatchAction::Kind::Recall, 0});
        run(b, 1);
        CHECK(!h.heroFallen(b, 0) && h.heroFellIn(0) == 0);
        CHECK(h.points(0) == HordeMode::kRoundPoints - HordeMode::kRecallPoints);
        const sim::Unit& u = *b.world().findUnit(h.hero(b, 0));
        CHECK(u.rank == f.rank && u.hp == u.stats.maxHp * HordeMode::kReturnHpPct / 100);
        // A second recall with him standing does nothing.
        h.act({sim::MatchAction::Kind::Recall, 0});
        run(b, 30);
        CHECK(h.points(0) == HordeMode::kRoundPoints - HordeMode::kRecallPoints);
        if (!pass) {
            world = b.world().checksum();
            mode = h.checksum();
            log = h.log();
        } else {
            CHECK(b.world().checksum() == world && h.checksum() == mode);
        }
    }
}

TEST(horde_local_seat_is_only_a_view) {
    // Two people on the hill (seats 0 and 2) and a computer ally: the match
    // played at seat 0, then watched again from seat 2 from the same inputs,
    // is the same match. Seat 2's screen orders seat 2's men, not seat 0's.
    const auto setUp = [](int local) {
        HordeOptions o = options(23);
        o.seats = {sim::SeatControl::Human, sim::SeatControl::Computer, sim::SeatControl::Human, sim::SeatControl::Empty};
        o.localSeat = local;
        o.breakSeconds = 5;
        return o;
    };
    auto a = field(23);
    HordeMode& ha = HordeMode::start(*a, setUp(0));
    for (int t = 0; t < 200 * sim::kTicksPerSecond; ++t) {
        if (ha.phase() == HordeMode::Phase::Break && ha.points(0) >= 3) ha.act({sim::MatchAction::Kind::Recruit, 0});
        if (t % (5 * sim::kTicksPerSecond) == 0 && ha.phase() == HordeMode::Phase::Wave) charge(*a, ha, 300.0f);
        run(*a, 1);
    }
    auto b = field(23);
    HordeMode& hb = HordeMode::start(*b, setUp(2));
    CHECK(ha.localSeat() == 0 && hb.localSeat() == 2 && b->localSide() == HordeMode::kHolders);
    // Seat 2's screen: its hero and men are the ones to select; seat 0's are an ally's.
    CHECK(b->controllable(*b->world().findUnit(hb.hero(*b, 2))) && !b->controllable(*b->world().findUnit(hb.hero(*b, 0))));
    b->selectSquad(hb.heroSquad(0));
    CHECK(b->selectedAlive().empty());
    b->selectSquad(hb.heroSquad(2));
    CHECK(b->selectedAlive().size() == 1);
    b->clearSelection();
    const std::string hud = hb.hudJson(*b);
    CHECK(hud.find("\"name\":\"Sir Corvin\"") < hud.find("\"offers\"") && hud.find("\"localSeat\":2") != std::string::npos);
    hb.replay(ha.log());
    for (int t = 0; t < 200 * sim::kTicksPerSecond; ++t) run(*b, 1);
    std::printf("  phase %d, %d left; seat 0 and seat 2: wave %d, world %016llx / %016llx\n", static_cast<int>(ha.phase()), ha.enemiesLeft(*a), ha.wave(), static_cast<unsigned long long>(a->world().checksum()),
                static_cast<unsigned long long>(b->world().checksum()));
    CHECK(ha.wave() >= 2);
    CHECK(a->world().checksum() == b->world().checksum() && ha.checksum() == hb.checksum());
    CHECK(ha.log().checksum() == hb.log().checksum());
    // Played from seat 2: its orders go out as seat 2's, to seat 2's men.
    auto c = field(23);
    HordeMode& hc = HordeMode::start(*c, setUp(2));
    const int warriors = hc.heroSquad(2) + 1;
    c->selectSquad(warriors);
    c->orderMove(toV3(hc.hill().at()) + V3{3, 3, 0});
    run(*c, 1);
    bool fromSeat2 = false;
    for (const sim::SeatInput& in : hc.log().inputs()) fromSeat2 = fromSeat2 || (in.seat == 2 && !in.orders.empty());
    CHECK(fromSeat2);
    CHECK(c->world().units()[c->squads()[static_cast<size_t>(warriors)].members.front() - 1].hasMoveTarget);
}

TEST(ctf_local_team_follows_the_seat) {
    // The person at seat 2 plays gold: his flag at the first place round the
    // field, his men to select, his flag's alarm; the same match from seat 0
    // (blue) replays identically.
    auto a = std::make_unique<Battle>(29, ctfPlace().pos, ctfContinent());
    CtfOptions oa = ctfOptions(0);
    CtfMode& ma = CtfMode::start(*a, oa);
    auto b = std::make_unique<Battle>(29, ctfPlace().pos, ctfContinent());
    CtfOptions ob = ctfOptions(2);
    ob.starts = {1, 2, 0, 3};  // gold's flag where blue's would stand
    CtfMode& mb = CtfMode::start(*b, ob);
    CHECK(ma.localTeam() == 0 && mb.localSeat() == 2 && mb.localTeam() == 2 && b->localSide() == 2);
    CHECK(mb.flagRule().flagOf(2)->at == ma.flagRule().flagOf(0)->at);
    CHECK(mb.flagRule().flagOf(0)->at == ma.flagRule().flagOf(1)->at);
    // Selection: gold's squads, not blue's (another person's) nor red's.
    int gold = -1, blue = -1;
    for (size_t q = 0; q < b->squads().size(); ++q) {
        if (mb.seatOfSquad(q) == 2 && gold < 0) gold = static_cast<int>(q);
        if (mb.seatOfSquad(q) == 0 && blue < 0) blue = static_cast<int>(q);
    }
    CHECK(gold >= 0 && blue >= 0);
    b->selectSquad(blue);
    CHECK(b->selectedAlive().empty());
    b->selectSquad(gold);
    CHECK(!b->selectedAlive().empty());
    const V3 goal = b->squadCentre(b->squads()[static_cast<size_t>(gold)], false) + V3{4, 0, 0};
    b->orderMove(goal);
    run(*b, 1);
    bool gave = false;
    for (const sim::SeatInput& in : mb.log().inputs())
        if (in.seat == 2 && !in.orders.empty()) gave = true;
    CHECK(gave);
    CHECK(b->world().units()[b->squads()[static_cast<size_t>(gold)].members.front() - 1].hasMoveTarget);
    // The alarm is gold's flag's: quiet, until red men come near it.
    run(*b, 2);
    CHECK(mb.alertTick() < 0 && mb.hudJson(*b).find("\"alert\":false") != std::string::npos);
    const V3 flag = toV3(mb.flagRule().flagOf(2)->at);
    b->addSquad("Red Raiders", 1, UnitType::LightInfantry, 3, flag + V3{20, 0, 0}, V3{-1, 0, 0});
    run(*b, 2);
    CHECK(mb.alertTick() >= 0 && mb.hudJson(*b).find("\"localTeam\":2,\"alert\":true") != std::string::npos);
    CHECK(mb.hudJson(*b).find("\"team\":2,\"name\":\"Gold\"") != std::string::npos);

    // Seat 0 then seat 2 at the same inputs: the same match.
    for (int t = 0; t < 60 * sim::kTicksPerSecond; ++t) run(*a, 1);
    auto c = std::make_unique<Battle>(29, ctfPlace().pos, ctfContinent());
    CtfMode& mc = CtfMode::start(*c, ctfOptions(2));
    mc.replay(ma.log());
    for (int t = 0; t < 60 * sim::kTicksPerSecond; ++t) run(*c, 1);
    CHECK(mc.localTeam() == 2 && ma.localTeam() == 0);
    CHECK(a->world().checksum() == c->world().checksum() && ma.checksum() == mc.checksum());
    CHECK(ma.log().checksum() == mc.log().checksum());
}
