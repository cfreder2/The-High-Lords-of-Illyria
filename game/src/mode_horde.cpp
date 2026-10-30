// Horde: hold the hill against waves (mode_horde.h). The match's rules (the
// hill, the waves, the breaks, the points) run on the simulation's state in
// integers and fixed point; the computer's seats and the horde think in the
// battle's floats, but their orders travel in the seats' inputs.
#include "illyria/game/mode_horde.h"

#include <algorithm>
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

const char* kHeroNames[sim::kMaxSeats] = {"Lord Aldric", "Lady Wren", "Sir Corvin", "Dame Isolde"};
const char* kSeatNames[sim::kMaxSeats] = {"Aldric's", "Wren's", "Corvin's", "Isolde's"};

// The horde's squads, by kind.
const char* hordeName(UnitType t) {
    switch (t) {
        case UnitType::Undead: return "Barrow Dead";
        case UnitType::LightInfantry: return "Red Hand Raiders";
        case UnitType::Archers: return "Red Hand Bowmen";
        case UnitType::WolfRiders: return "Wolf Riders of Gnarl";
        case UnitType::Spiders: return "Brood of Mirkwold";
        case UnitType::Berserker: return "Blood Berserks";
        case UnitType::Minotaurs: return "Minotaurs of the Maze";
        case UnitType::LightCavalry: return "Steppe Riders";
        case UnitType::Giants: return "Hill Giants";
        case UnitType::Devils: return "Pit Devils";
        case UnitType::DwarfGrenadier: return "Gnarl Bombers";
        case UnitType::Wizards: return "Witch-Kings";
        case UnitType::Demons: return "Demons of the Rift";
        case UnitType::Dragons: return "Ashen Wyrm";
        default: return sim::unitTypeName(t);
    }
}

// What the computer's seats buy first.
constexpr UnitType kComputerPrefers[] = {UnitType::Warrior,  UnitType::Archers,   UnitType::Pikemen,
                                         UnitType::Journeyman, UnitType::Dwarves, UnitType::HeavyInfantry,
                                         UnitType::Berserker, UnitType::HeavyCavalry, UnitType::Wizards};

const char* roleName(UnitType t) {
    static const char* kRoles[] = {"melee", "ranged", "bomber", "caster", "healer", "flier", "siege"};
    return kRoles[static_cast<int>(roleOf(t))];
}

}  // namespace

HordeMode& HordeMode::start(Battle& b, const HordeOptions& o) {
    auto mode = std::make_unique<HordeMode>(o);
    HordeMode& h = *mode;
    b.setMode(std::move(mode));
    h.setUp(b);
    return h;
}

Vec3 HordeMode::sector(int seat) const { return compass(seat * 2); }

int HordeMode::playingSeats() const {
    int n = 0;
    for (int s = 0; s < sim::kMaxSeats; ++s) n += seats_[static_cast<size_t>(s)].playing();
    return std::max(1, n);
}

void HordeMode::setUp(Battle& b) {
    seats_.assign(sim::kMaxSeats + 1, {});
    for (int s = 0; s < sim::kMaxSeats; ++s) seats_[static_cast<size_t>(s)] = {opt_.seats[static_cast<size_t>(s)], kHolders};
    seats_[kHordeSeat] = {sim::SeatControl::Computer, kHorde};
    bool anyone = false;
    for (int s = 0; s < sim::kMaxSeats; ++s) anyone = anyone || seats_[static_cast<size_t>(s)].playing();
    if (!anyone) seats_[0].control = sim::SeatControl::Human;
    localSeat_ = 0;
    for (int s = sim::kMaxSeats - 1; s >= 0; --s)
        if (seats_[static_cast<size_t>(s)].control == sim::SeatControl::Human) localSeat_ = s;

    // The hill: the field's high ground near its middle, flagged.
    const Vec3 mid{m(kField / 2), m(kField / 2), Fixed::zero()};
    hill_.place(highGround(b.region(), mid, 150), kHolders);
    const Vec3 hill = hill_.at();
    reached_ = reachFrom(b.region(), hill);
    // Each seat's hero and its small army hold their side of the top: the
    // line forward, pikes on its flank, bowmen behind, the healers at the crest.
    for (int s = 0; s < sim::kMaxSeats; ++s) {
        if (!seats_[static_cast<size_t>(s)].playing()) continue;
        const Vec3 d = sector(s), r = rightOf(d);
        const auto at = [&](int ahead, int right) { return toV3(firmGround(b.region(), hill + d * m(ahead) + r * m(right), 24)); };
        const V3 face = toV3(d);
        const std::string own = kSeatNames[s];
        heroes_[static_cast<size_t>(s)] = raise(b, s, kHeroNames[s], kHolders, UnitType::Hero, 1, at(6, 0), face);
        raise(b, s, own + " Warriors", kHolders, UnitType::Warrior, 8, at(13, 0), face);
        raise(b, s, own + " Pikes", kHolders, UnitType::Pikemen, 6, at(10, 13), face);
        raise(b, s, own + " Bowmen", kHolders, UnitType::Archers, 8, at(2, -9), face);
        raise(b, s, own + " Menders", kHolders, UnitType::Journeyman, 2, at(-2, 3), face);
    }
    phase_ = Phase::Muster;
    timer_ = opt_.musterSeconds * kTps;
    chooseOffers();
}

void HordeMode::chooseOffers() {
    // Six of the squads on offer, a different six as the waves go by.
    const size_t n = sim::squadOffers().size();
    offers_.clear();
    for (size_t k = 0; offers_.size() < static_cast<size_t>(kOffersShown) && k < n; ++k)
        offers_.push_back(static_cast<uint8_t>((static_cast<size_t>(wave_) * 5 + k * 2) % n));
    std::sort(offers_.begin(), offers_.end());
    offers_.erase(std::unique(offers_.begin(), offers_.end()), offers_.end());
}

int HordeMode::enemiesLeft(const Battle& b) const {
    int n = 0;
    for (const sim::Unit& u : b.world().units()) n += u.owner == kHorde && sim::standsGround(u);
    return n;
}

int HordeMode::kills(const Battle& b) const {
    int n = 0;
    for (const sim::Unit& u : b.world().units()) n += u.owner == kHolders ? u.kills : 0;
    return n;
}

void HordeMode::think(Battle& b) {
    const V3 hill = toV3(hill_.at());
    int hordeIndex = 0;
    std::array<int, sim::kMaxSeats> posts{};
    for (size_t q = 0; q < b.squads().size(); ++q) {
        const int seat = seatOfSquad(q);
        if (seat == kHordeSeat) {
            // The horde: at whoever is near, else up the hill, spread over its top.
            const V3 spot = hill + toV3(compass(hordeIndex++)) * 7.0f;
            b.aiFight(q, spot, b.squadCentre(b.squads()[q], false), 55.0f);
        } else if (seat >= 0 && seat < sim::kMaxSeats && seats_[static_cast<size_t>(seat)].control == sim::SeatControl::Computer) {
            // An ally the computer plays: his side of the hill, meeting whoever comes up it.
            const int k = posts[static_cast<size_t>(seat)]++;
            const V3 d = toV3(sector(seat)), r{d.y, -d.x, 0};
            const V3 post = hill + d * (8.0f + static_cast<float>(k % 2) * 6.0f) + r * (static_cast<float>(k / 2 % 3 - 1) * 12.0f);
            b.aiFight(q, post, hill, 50.0f);
        }
    }
    // In the break the computer's seats buy what they can, then are ready.
    if (phase_ != Phase::Break) return;
    for (int s = 0; s < sim::kMaxSeats; ++s) {
        if (seats_[static_cast<size_t>(s)].control != sim::SeatControl::Computer || ready_[static_cast<size_t>(s)]) continue;
        int32_t budget = points_[static_cast<size_t>(s)];
        for (bool bought = true; bought;) {
            bought = false;
            for (UnitType want : kComputerPrefers)
                for (size_t i = 0; i < offers_.size(); ++i) {
                    const sim::Offer& o = sim::squadOffers()[offers_[i]];
                    if (o.type != want || o.cost > budget || bought) continue;
                    computerAct(s, {sim::MatchAction::Kind::Recruit, static_cast<uint8_t>(i)});
                    budget -= o.cost;
                    bought = true;
                }
        }
        computerAct(s, {sim::MatchAction::Kind::Ready, 0});
    }
}

void HordeMode::applyAction(Battle& b, int seat, const sim::MatchAction& a) {
    if (seat < 0 || seat >= sim::kMaxSeats) return;
    const size_t si = static_cast<size_t>(seat);
    if (a.kind == sim::MatchAction::Kind::Ready) {
        if (phase_ == Phase::Muster || phase_ == Phase::Break) ready_[si] = true;
        return;
    }
    if (phase_ != Phase::Break || a.option >= offers_.size()) return;
    const sim::Offer& o = sim::squadOffers()[offers_[a.option]];
    if (points_[si] < o.cost) return;
    points_[si] -= o.cost;
    // They come up behind the seat's line, three abreast, row on row.
    const int k = recruits_[si]++;
    const Vec3 d = sector(seat), r = rightOf(d);
    const Vec3 at = hill_.at() + d * m(4 + (k / 3 % 3) * 5) + r * m((k % 3 - 1) * 11);
    raise(b, seat, std::string("Fresh ") + sim::unitTypeName(o.type), kHolders, o.type, o.count,
          toV3(firmGround(b.region(), at, 24)), toV3(d));
    if (seat == localSeat_) b.voice().announce(b.world(), Situation::Reinforcements);
}

void HordeMode::startWave(Battle& b) {
    ++wave_;
    plan_ = sim::hordeWave(wave_, opt_.difficulty, playingSeats(), opt_.seed);
    waveSoldiers_ = 0;
    // Each group forms up at the edge of the field it comes from, the
    // groups from one edge side by side, then a rank behind.
    std::array<int, 8> fromEdge{};
    for (const sim::WaveGroup& g : plan_.groups) {
        const Vec3 d = compass(g.edge), r = rightOf(d);
        const int k = fromEdge[g.edge]++;
        const int side = k == 0 ? 0 : (k % 2 ? 1 : -1) * ((k + 1) / 2);
        const Vec3 want = insideField(hill_.at() + d * m(kSpawnM + (k / 3) * 14) + r * m(side * 20), kField, 30);
        // On ground that leads up to the hill (not across a cliff or a lake from it).
        Vec3 at = nearestReached(b.region(), reached_, want, 90, 9);
        if (at == want) at = firmGround(b.region(), nearestReached(b.region(), reached_, want, 60), 12);
        const std::string name = g.boss ? (g.type == UnitType::Dragons ? "The Ashen Wyrm" : "Vashgar of the Rift") : hordeName(g.type);
        raise(b, kHordeSeat, name, kHorde, g.type, g.count, toV3(at), toV3(-d), g.rank);
        waveSoldiers_ += g.count;
    }
    phase_ = Phase::Wave;
    ready_.fill(false);
    contestedSeen_ = 0;
    b.voice().announce(b.world(), plan_.boss ? Situation::BossWave : Situation::WaveIncoming);
    b.sfx().alert(Sound::WarHorn);
}

void HordeMode::endWave(Battle& b) {
    survived_ = wave_;
    if (opt_.waves > 0 && wave_ >= opt_.waves) {
        phase_ = Phase::Won;
        return;
    }
    // The break: the wounded mend, the heroes rise, the points come in.
    sim::World& w = b.mutableWorld();
    for (const sim::Unit& u : b.world().units())
        if (u.owner == kHolders && u.alive() && u.hp < u.stats.maxHp) w.heal(u.id, (u.stats.maxHp - u.hp) * kHealPct / 100);
    for (int s = 0; s < sim::kMaxSeats; ++s) {
        if (!seats_[static_cast<size_t>(s)].playing()) continue;
        if (const int h = heroes_[static_cast<size_t>(s)]; h >= 0)
            for (sim::EntityId id : b.squads()[static_cast<size_t>(h)].members) w.levelUp(id);
        points_[static_cast<size_t>(s)] += kRoundPoints + wave_ / 2;
    }
    chooseOffers();
    ready_.fill(false);
    timer_ = opt_.breakSeconds * kTps;
    phase_ = Phase::Break;
    b.voice().announce(b.world(), Situation::WaveCleared);
}

void HordeMode::afterStep(Battle& b) {
    switch (phase_) {
        case Phase::Muster:
        case Phase::Break: {
            // Every person at the table ready: no need to wait out the clock.
            bool humans = false, allReady = true;
            for (int s = 0; s < sim::kMaxSeats; ++s)
                if (seats_[static_cast<size_t>(s)].control == sim::SeatControl::Human) {
                    humans = true;
                    allReady = allReady && ready_[static_cast<size_t>(s)];
                }
            hill_.count(b.world());
            if (--timer_ <= 0 || (humans && allReady)) startWave(b);
            break;
        }
        case Phase::Wave: {
            hill_.step(b.world());
            if (hill_.contestedTicks() > 0 && contestedSeen_ == 0) {
                b.voice().announce(b.world(), Situation::HillContested);
                b.sfx().alert(Sound::Alarm);
            }
            contestedSeen_ = hill_.contestedTicks();
            int holders = 0;
            for (const sim::Unit& u : b.world().units()) holders += u.owner == kHolders && u.alive();
            if (hill_.lost() || holders == 0) {
                phase_ = Phase::Lost;
                break;
            }
            if (enemiesLeft(b) == 0) endWave(b);
            break;
        }
        case Phase::Lost:
        case Phase::Won: break;
    }
}

uint64_t HordeMode::checksum() const {
    sim::StateHasher h;
    hashSeats(h);
    h.addU64(static_cast<uint64_t>(phase_));
    h.addI64(wave_);
    h.addI64(survived_);
    h.addI64(timer_);
    h.addI64(waveSoldiers_);
    hill_.hash(h);
    sim::hashWave(h, plan_);
    for (int s = 0; s < sim::kMaxSeats; ++s) {
        h.addI64(points_[static_cast<size_t>(s)]);
        h.addU64(ready_[static_cast<size_t>(s)]);
        h.addI64(heroes_[static_cast<size_t>(s)]);
        h.addI64(recruits_[static_cast<size_t>(s)]);
    }
    h.addU64(offers_.size());
    for (uint8_t o : offers_) h.addU64(o);
    return h.value();
}

std::string HordeMode::hudJson(const Battle& b) const {
    static const char* kPhases[] = {"muster", "wave", "break", "lost", "won"};
    const int local = localSeat_;
    char buf[1024];
    const int heroLevel = [&] {
        const int h = heroSquad(local);
        return h >= 0 ? b.standing(b.squads()[static_cast<size_t>(h)]).level * (b.aliveCount(b.squads()[static_cast<size_t>(h)]) > 0 ? 1 : -1) : 0;
    }();
    std::snprintf(buf, sizeof buf,
                  "{\"mode\":\"horde\",\"phase\":\"%s\",\"place\":\"%s\",\"difficulty\":%d,\"wave\":%d,\"waves\":%d,"
                  "\"boss\":%s,\"elite\":%s,\"survived\":%d,\"enemies\":%d,\"enemiesTotal\":%d,\"toWave\":%d,"
                  "\"hill\":{\"holders\":%d,\"foes\":%d,\"contested\":%.1f,\"lose\":%d,\"lost\":%s},"
                  "\"held\":%d,\"kills\":%d,\"points\":%d,\"ready\":%s,\"heroLevel\":%d,\"outcome\":%d,\"offers\":[",
                  kPhases[static_cast<int>(phase_)], opt_.place.c_str(), opt_.difficulty, wave_, opt_.waves,
                  plan_.boss && wave_ > 0 ? "true" : "false", plan_.elite && wave_ > 0 ? "true" : "false", survived_,
                  enemiesLeft(b), waveSoldiers_, (timer_ + kTps - 1) / kTps, hill_.holdersIn(), hill_.foesIn(),
                  static_cast<double>(hill_.contestedTicks()) / kTps, sim::HillRule::kLoseSeconds,
                  hill_.lost() ? "true" : "false", hill_.heldTicks() / kTps, kills(b), points(local),
                  local >= 0 && local < sim::kMaxSeats && ready_[static_cast<size_t>(local)] ? "true" : "false", heroLevel,
                  outcome());
    std::string s = buf;
    for (size_t i = 0; i < offers_.size(); ++i) {
        const sim::Offer& o = sim::squadOffers()[offers_[i]];
        std::snprintf(buf, sizeof buf, "%s{\"i\":%zu,\"type\":\"%s\",\"role\":\"%s\",\"count\":%d,\"cost\":%d}", i ? "," : "", i,
                      sim::unitTypeName(o.type), roleName(o.type), o.count, o.cost);
        s += buf;
    }
    s += "],\"seats\":[";
    for (int k = 0; k < sim::kMaxSeats; ++k) {
        const sim::Seat& seat = seats_[static_cast<size_t>(k)];
        int alive = 0;
        for (size_t q = 0; q < b.squads().size(); ++q)
            if (seatOfSquad(q) == k) alive += b.aliveCount(b.squads()[q]);
        static const char* kControl[] = {"empty", "human", "computer"};
        std::snprintf(buf, sizeof buf, "%s{\"control\":\"%s\",\"hero\":\"%s\",\"alive\":%d,\"points\":%d,\"local\":%s}", k ? "," : "",
                      kControl[static_cast<int>(seat.control)], kHeroNames[k], alive, points(k), k == local ? "true" : "false");
        s += buf;
    }
    return s + "]}";
}

std::vector<ModeFlag> HordeMode::flags(const Battle& b) const {
    // The holders' banner on the hilltop, coming down while the horde holds
    // it alone, their red going up in its place.
    V3 base = toV3(hill_.at());
    base.z = b.heightAt(base.x, base.y);
    const float lost = hill_.lost() ? 1.0f
                                    : std::min(1.0f, static_cast<float>(hill_.contestedTicks()) / sim::HillRule::kLoseTicks);
    std::vector<ModeFlag> out;
    out.push_back({base, 1 - lost, teamColour(kHolders), 14, 3.4f});
    if (lost > 0) out.push_back({base, lost, teamColour(kHorde), 14, 3.4f});
    return out;
}

}  // namespace illyria::game
