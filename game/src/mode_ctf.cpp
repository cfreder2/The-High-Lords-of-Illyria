// Capture the flag (mode_ctf.h). The flags' rule and the verdict run on the
// simulation's state in integers and fixed point; the computer's teams think
// in the battle's floats, their orders travelling in their seats' inputs.
#include "illyria/game/mode_ctf.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "illyria/game/battle.h"

namespace illyria::game {

using sim::Fixed;
using sim::UnitType;
using sim::Vec3;

namespace {

constexpr int32_t kTps = sim::kTicksPerSecond;
constexpr int32_t kField = static_cast<int32_t>(Battle::kWorldSize);

Fixed m(int64_t metres) { return Fixed::fromInt(metres); }

// The computer's (and the default) army, bought in this order while the points last.
constexpr UnitType kArmyOrder[] = {UnitType::Warrior,   UnitType::Archers,      UnitType::Pikemen,
                                   UnitType::HeavyInfantry, UnitType::Journeyman, UnitType::Dwarves,
                                   UnitType::Berserker, UnitType::LightCavalry, UnitType::Archers,
                                   UnitType::Warrior,   UnitType::DwarfGrenadier, UnitType::HeavyCavalry};

int offerOf(UnitType t) {
    const auto& offers = sim::squadOffers();
    for (size_t i = 0; i < offers.size(); ++i)
        if (offers[i].type == t) return static_cast<int>(i);
    return -1;
}

std::string hexColour(sim::PlayerId team) {
    const V3 c = teamColour(team);
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x", static_cast<int>(c.x * 255), static_cast<int>(c.y * 255), static_cast<int>(c.z * 255));
    return buf;
}

}  // namespace

std::vector<uint8_t> CtfMode::affordable(const std::vector<uint8_t>& picks, int budget) {
    std::vector<uint8_t> out;
    for (uint8_t p : picks) {
        if (p >= sim::squadOffers().size() || sim::squadOffers()[p].cost > budget) continue;
        budget -= sim::squadOffers()[p].cost;
        out.push_back(p);
    }
    return out;
}

std::vector<uint8_t> CtfMode::defaultArmy(int budget, int variant) {
    // Round the list from a different place for each team, so they differ a little.
    std::vector<uint8_t> out;
    const size_t n = sizeof kArmyOrder / sizeof kArmyOrder[0];
    for (size_t k = 0, misses = 0; misses < n; ++k) {
        const int o = offerOf(kArmyOrder[(k + static_cast<size_t>(variant) * 3) % n]);
        if (o < 0 || sim::squadOffers()[static_cast<size_t>(o)].cost > budget) {
            ++misses;
            continue;
        }
        misses = 0;
        budget -= sim::squadOffers()[static_cast<size_t>(o)].cost;
        out.push_back(static_cast<uint8_t>(o));
    }
    return out;
}

CtfMode& CtfMode::start(Battle& b, const CtfOptions& o) {
    auto mode = std::make_unique<CtfMode>(o);
    CtfMode& c = *mode;
    b.setMode(std::move(mode));
    c.setUp(b);
    return c;
}

int CtfMode::placeOf(int team) const {
    // The places asked for, when they are each of 0..teams-1 once.
    std::array<bool, 4> seen{};
    for (int t = 0; t < teams_; ++t) {
        const int p = opt_.starts[static_cast<size_t>(t)];
        if (p < 0 || p >= teams_ || seen[static_cast<size_t>(p)]) return team;
        seen[static_cast<size_t>(p)] = true;
    }
    return opt_.starts[static_cast<size_t>(team)];
}

Vec3 CtfMode::startOf(int t) const {
    // Evenly round the middle: opposite for two, a third of a turn apart for
    // three, the corners for four (the pairs across from each other).
    const int team = placeOf(t);
    const Fixed h = Fixed::fromRatio(1, 2), s = Fixed::fromRatio(8660, 10000), z = Fixed::zero();
    Vec3 d;
    if (teams_ == 2) d = compass(team == 0 ? 4 : 0);
    else if (teams_ == 3) d = team == 0 ? Vec3{-Fixed::one(), z, z} : team == 1 ? Vec3{h, s, z} : Vec3{h, -s, z};
    else d = compass(team == 0 ? 5 : team == 1 ? 1 : team == 2 ? 3 : 7);
    return Vec3{m(kField / 2), m(kField / 2), z} + d * m(opt_.ringM);
}

void CtfMode::setUp(Battle& b) {
    teams_ = std::clamp(opt_.teams, 2, 4);
    bool given = false;
    for (const CtfOptions::SeatSpec& s : opt_.seats) given = given || s.control != sim::SeatControl::Empty;
    seats_.assign(sim::kMaxSeats, {});
    for (int k = 0; k < sim::kMaxSeats; ++k) {
        const CtfOptions::SeatSpec& spec = opt_.seats[static_cast<size_t>(k)];
        if (given) seats_[static_cast<size_t>(k)] = {spec.control, static_cast<sim::PlayerId>(std::clamp(spec.team, 0, teams_ - 1))};
        else if (k < teams_) seats_[static_cast<size_t>(k)] = {k == 0 ? sim::SeatControl::Human : sim::SeatControl::Computer, static_cast<sim::PlayerId>(k)};
    }
    // Every team needs someone: the computer sits in for a team nobody took.
    for (int t = 0; t < teams_; ++t) {
        bool taken = false;
        for (const sim::Seat& s : seats_) taken = taken || (s.playing() && s.team == t);
        if (taken) continue;
        for (sim::Seat& s : seats_)
            if (!s.playing()) {
                s = {sim::SeatControl::Computer, static_cast<sim::PlayerId>(t)};
                break;
            }
    }
    setLocalSeat(b, seatFor(opt_.localSeat));

    // The flags, and beside each its team's army facing the middle.
    const Vec3 mid{m(kField / 2), m(kField / 2), Fixed::zero()};
    for (int t = 0; t < teams_; ++t) {
        const Vec3 flag = firmGround(b.region(), insideField(startOf(t), kField, 60), 40);
        flags_.add(flag, static_cast<sim::PlayerId>(t));
        Vec3 in = (mid - flag).flat();
        const Fixed len = in.length();
        in = len > Fixed::zero() ? in / len : compass(0);
        const Vec3 r = rightOf(in);
        int seatsOn = 0;
        for (const sim::Seat& s : seats_) seatsOn += s.playing() && s.team == t;
        int k = 0, nth = 0;
        for (int seat = 0; seat < sim::kMaxSeats; ++seat) {
            const sim::Seat& s = seats_[static_cast<size_t>(seat)];
            if (!s.playing() || s.team != t) continue;
            const int share = opt_.budget / seatsOn + (nth++ == 0 ? opt_.budget % seatsOn : 0);
            const std::vector<uint8_t> army = seat == localSeat_ && s.control == sim::SeatControl::Human && !opt_.army.empty()
                                                  ? affordable(opt_.army, share)
                                                  : defaultArmy(share, t + seat);
            for (uint8_t pick : army) {
                const sim::Offer& o = sim::squadOffers()[pick];
                const Vec3 at = flag + in * m(9 + (k / 3) * 11) + r * m((k % 3 - 1) * 15);
                const std::string name = std::string(teamName(static_cast<sim::PlayerId>(t))) + " " + sim::unitTypeName(o.type);
                const int q = raise(b, seat, name, static_cast<sim::PlayerId>(t), o.type, o.count, toV3(firmGround(b.region(), at, 24)), toV3(in));
                // The computer keeps its missile troops, healers and every
                // third squad of the line back to guard the flag.
                const Role role = roleOf(o.type);
                if (defends_.size() <= static_cast<size_t>(q)) defends_.resize(static_cast<size_t>(q) + 1, 0);
                defends_[static_cast<size_t>(q)] = role != Role::Melee && role != Role::Flier ? 1 : (k % 3 == 2 ? 1 : 0);
                ++k;
            }
        }
    }
}

int32_t CtfMode::strength(const Battle& b, sim::PlayerId team) const {
    int32_t hp = 0;
    for (const sim::Unit& u : b.world().units())
        if (u.owner == team && sim::standsGround(u)) hp += u.hp;
    return hp;
}

void CtfMode::think(Battle& b) {
    // A team whose attack is spent sends its guards too (a flag is not taken
    // by waiting), and after kAllOut everyone goes.
    constexpr int32_t kAllOut = 6 * 60 * kTps;
    std::array<int, 4> attacking{};
    for (size_t q = 0; q < b.squads().size() && q < defends_.size(); ++q)
        if (!defends_[q] && b.aliveCount(b.squads()[q]) > 0) ++attacking[b.squads()[q].owner & 3];
    for (size_t q = 0; q < b.squads().size(); ++q) {
        const int seat = seatOfSquad(q);
        if (seat < 0 || seat >= static_cast<int>(seats_.size()) || seats_[static_cast<size_t>(seat)].control != sim::SeatControl::Computer) continue;
        const sim::PlayerId team = seats_[static_cast<size_t>(seat)].team;
        const sim::FlagRule::Flag* own = flags_.flagOf(team);
        if (!own || !own->standing) continue;  // out of the match: broken, beyond orders
        const V3 home = toV3(own->at);
        const Squad& s = b.squads()[q];
        const V3 c = b.squadCentre(s, false);
        if (q < defends_.size() && defends_[q] && attacking[team & 3] > 0 && elapsed_ < kAllOut) {
            // Guards: round their flag, meeting whoever comes near it.
            const V3 post = home + toV3(compass(static_cast<int>(q))) * 7.0f;
            b.aiFight(q, post, home, 45.0f);
            continue;
        }
        // The rest: at the nearest enemy flag still standing.
        const sim::FlagRule::Flag* target = nullptr;
        float best = 1e30f;
        for (const sim::FlagRule::Flag& f : flags_.flags()) {
            if (!f.standing || f.team == team) continue;
            const V3 d = toV3(f.at) - home;
            const float dist = d.x * d.x + d.y * d.y;
            if (dist < best) {
                best = dist;
                target = &f;
            }
        }
        if (!target) continue;
        const V3 at = toV3(target->at);
        const float dist = V3{at.x - c.x, at.y - c.y, 0}.length();
        if (dist > 40.0f) {
            b.aiFight(q, at, c, 25.0f);  // on the way, they meet whoever crosses them
            continue;
        }
        // At the flag: cut down its guards on it, else stand on it.
        const sim::EntityId guard = b.nearestFoe(team, at, 12.0f);
        if (guard != sim::kNoEntity) b.aiAttack(q, guard);
        else b.aiForceMarch(q, at);
    }
}

void CtfMode::afterStep(Battle& b) {
    if (phase_ == Phase::Over) return;
    ++elapsed_;
    const sim::PlayerId local = localTeam();
    for (size_t i : flags_.step(b.world())) {
        // Out of the match: the army breaks and flees, and never rallies.
        const sim::PlayerId team = flags_.flags()[i].team;
        b.mutableWorld().breakSide(team);
        out_.push_back(team);
        b.voice().announce(b.world(), team == local ? Situation::FlagLost : Situation::EnemyFlagTaken);
    }
    // The local flag in danger: the alarm, and the herald.
    if (const sim::FlagRule::Flag* f = flags_.flagOf(local); f && f->standing) {
        const bool threatened = f->threats > 0;
        if (threatened && !threatened_) {
            alertTick_ = static_cast<int32_t>(b.world().tick());
            b.voice().announce(b.world(), Situation::FlagAttacked);
            b.sfx().alert(Sound::Alarm);
        }
        threatened_ = threatened;
    }
    if (const sim::PlayerId last = flags_.lastStanding(); last != kNobody) {
        winner_ = last;
        phase_ = Phase::Over;
    } else if (flags_.standing() == 0) {
        phase_ = Phase::Over;  // the last two fell together: a draw
    } else if (opt_.timeLimitSeconds > 0 && elapsed_ >= opt_.timeLimitSeconds * kTps) {
        // Time: of the teams with their flags, the strongest wins (a tie is a draw).
        int32_t best = -1;
        bool tie = false;
        for (const sim::FlagRule::Flag& f : flags_.flags()) {
            if (!f.standing) continue;
            const int32_t st = strength(b, f.team);
            if (st > best) {
                best = st;
                winner_ = f.team;
                tie = false;
            } else if (st == best) {
                tie = true;
            }
        }
        if (tie) winner_ = kNobody;
        phase_ = Phase::Over;
    }
}

int CtfMode::outcome() const {
    if (phase_ != Phase::Over) return 0;
    return winner_ == localTeam() ? 1 : 2;
}

uint64_t CtfMode::checksum() const {
    sim::StateHasher h;
    hashSeats(h);
    h.addI64(teams_);
    h.addU64(static_cast<uint64_t>(phase_));
    flags_.hash(h);
    h.addU64(winner_);
    h.addI64(elapsed_);
    h.addU64(out_.size());
    for (sim::PlayerId t : out_) h.addU64(t);
    return h.value();
}

std::string CtfMode::hudJson(const Battle& b) const {
    char buf[512];
    const sim::PlayerId local = localTeam();
    const sim::FlagRule::Flag* own = flags_.flagOf(local);
    const bool alert = own && own->standing && own->threats > 0;
    std::snprintf(buf, sizeof buf,
                  "{\"mode\":\"ctf\",\"phase\":\"%s\",\"place\":\"%s\",\"time\":%d,\"limit\":%d,\"localTeam\":%d,\"alert\":%s,"
                  "\"winner\":%d,\"draw\":%s,\"outcome\":%d,\"budget\":%d,\"teams\":[",
                  phase_ == Phase::Over ? "over" : "battle", opt_.place.c_str(), elapsed_ / kTps, opt_.timeLimitSeconds, local,
                  alert ? "true" : "false", winner_ == kNobody ? -1 : winner_, draw() ? "true" : "false", outcome(), opt_.budget);
    std::string s = buf;
    for (int t = 0; t < teams_; ++t) {
        const sim::FlagRule::Flag* f = flags_.flagOf(static_cast<sim::PlayerId>(t));
        int alive = 0;
        for (const sim::Unit& u : b.world().units()) alive += u.owner == t && sim::standsGround(u);
        int computer = 0, human = 0;
        for (const sim::Seat& seat : seats_)
            if (seat.playing() && seat.team == t) (seat.control == sim::SeatControl::Human ? human : computer) += 1;
        std::snprintf(buf, sizeof buf,
                      "%s{\"team\":%d,\"name\":\"%s\",\"colour\":\"%s\",\"flag\":%s,\"takenBy\":%d,\"alive\":%d,\"defenders\":%d,"
                      "\"attackers\":%d,\"threats\":%d,\"humans\":%d,\"computers\":%d,\"local\":%s}",
                      t ? "," : "", t, teamName(static_cast<sim::PlayerId>(t)), hexColour(static_cast<sim::PlayerId>(t)).c_str(),
                      f && f->standing ? "true" : "false", f && !f->standing ? f->takenBy : -1, alive, f ? f->defenders : 0,
                      f ? f->attackers : 0, f ? f->threats : 0, human, computer, t == local ? "true" : "false");
        s += buf;
    }
    return s + "]}";
}

std::vector<ModeFlag> CtfMode::flags(const Battle& b) const {
    // Each team's flag on its pole; a fallen one low, the taker's colours above it.
    std::vector<ModeFlag> out;
    for (const sim::FlagRule::Flag& f : flags_.flags()) {
        V3 base = toV3(f.at);
        base.z = b.heightAt(base.x, base.y);
        if (f.standing) {
            out.push_back({base, 1, teamColour(f.team), 11, 2.8f});
        } else {
            out.push_back({base, 0.08f, teamColour(f.team), 11, 2.8f});
            out.push_back({base, 1, teamColour(f.takenBy), 11, 2.8f});
        }
    }
    return out;
}

}  // namespace illyria::game
