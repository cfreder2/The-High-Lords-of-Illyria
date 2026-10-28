// Voiced lines: sim events and orders to throttled voice cues (voice.h).
#include "illyria/game/voice.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace illyria::game {

using sim::UnitType;

namespace {

constexpr int kTps = sim::kTicksPerSecond;
// Throttles. Every (side, situation) waits out its own cooldown; a speaker
// keeps quiet kSpeakerGap after he spoke; ambient cues (priority 2 and below)
// keep kAmbientGap apart and at most kWindowMax in any kWindow. Orders skip
// the ambient gap; grave news (4 and up) skips everything but its cooldown.
constexpr uint32_t kSpeakerGap = 5 * kTps;
constexpr uint32_t kAmbientGap = 3 * kTps / 2;
constexpr uint32_t kWindow = 10 * kTps;
constexpr size_t kWindowMax = 5;
// Hearing: a cue fades out over kHearing metres from the view centre, the
// camera's height counting against it (zoomed out, only the loudest carry).
constexpr float kHearing = 140.0f;
constexpr float kMinGain = 0.08f;
// Contact: a foe within kSighted metres; lost after kContactLost of none within twice that.
constexpr float kSighted = 55.0f;
constexpr uint32_t kContactLost = 60 * kTps;
// Idle chatter: only after kQuiet without a blow.
constexpr uint32_t kQuiet = 20 * kTps;

// Cooldown of each situation per side, in tenths of a second.
constexpr int kCooldownTenths[kSituationCount] = {
    3,    // Selected
    3,    // Move
    3,    // Attack
    60,   // Charge
    100,  // ChargeSeen
    20,   // HoldLine
    10,   // HeroRally
    80,   // Rallied
    80,   // Wavering
    60,   // Breaking
    0,    // HeroFell
    20,   // EnemyHeroFell
    70,   // FriendKilled
    60,   // Kill
    80,   // VetKill
    30,   // Promoted
    80,   // Wounded
    120,  // ArrowFire
    90,   // Flanked
    100,  // FriendlyFire
    60,   // Fuse
    100,  // Healing
    120,  // EngineLoose
    0,    // GateBroken
    0,    // EnemySighted (once per contact)
    0,    // Victory
    0,    // Defeat
    450,  // Idle
};

constexpr const char* kSetNames[kVoiceSetCount] = {"soldier", "archer", "dwarf",  "rider", "hero",   "wizard", "berserker",
                                                   "archon",  "brute",  "undead", "fiend", "beast", "engine"};
constexpr const char* kSituationNames[kSituationCount] = {
    "selected",   "move",          "attack",        "charge",  "charge_seen", "hold_line", "hero_rally",
    "rallied",    "wavering",      "breaking",      "hero_fell", "enemy_hero_fell", "friend_killed", "kill",
    "vet_kill",   "promoted",      "wounded",       "arrow_fire", "flanked",  "friendly_fire", "fuse",
    "healing",    "engine_loose",  "gate_broken",   "enemy_sighted", "victory", "defeat", "idle"};

bool nonVerbal(VoiceSet set) {
    return set == VoiceSet::Brute || set == VoiceSet::Undead || set == VoiceSet::Fiend || set == VoiceSet::Beast;
}

V3 flat(V3 v) { return {v.x, v.y, 0}; }

}  // namespace

const char* voiceSetName(VoiceSet set) { return kSetNames[static_cast<int>(set)]; }
const char* situationName(Situation s) { return kSituationNames[static_cast<int>(s)]; }

VoiceSet voiceSetOf(UnitType type) {
    switch (type) {
        case UnitType::Archers: return VoiceSet::Archer;
        case UnitType::Dwarves:
        case UnitType::DwarfGrenadier: return VoiceSet::Dwarf;
        case UnitType::LightCavalry:
        case UnitType::HeavyCavalry:
        case UnitType::WolfRiders: return VoiceSet::Rider;
        case UnitType::Hero: return VoiceSet::Hero;
        case UnitType::Wizards:
        case UnitType::Journeyman: return VoiceSet::Wizard;
        case UnitType::Berserker: return VoiceSet::Berserker;
        case UnitType::Archons: return VoiceSet::Archon;
        case UnitType::Giants:
        case UnitType::Minotaurs: return VoiceSet::Brute;
        case UnitType::Undead: return VoiceSet::Undead;
        case UnitType::Devils:
        case UnitType::Demons: return VoiceSet::Fiend;
        case UnitType::Spiders:
        case UnitType::Griffins:
        case UnitType::Pegasi:
        case UnitType::Elephants:
        case UnitType::Unicorns:
        case UnitType::Dragons: return VoiceSet::Beast;
        case UnitType::Catapult: return VoiceSet::Engine;
        default: return VoiceSet::Soldier;
    }
}

bool voices(VoiceSet set, Situation s) {
    using S = Situation;
    if (nonVerbal(set)) {
        // Calls and cries, no words: what a beast would make a sound about.
        switch (s) {
            case S::Selected:
            case S::Move:
            case S::Attack:
            case S::Charge:
            case S::Kill:
            case S::Wounded:
            case S::Victory:
            case S::Idle: return true;
            // The dead know no fear and steady nobody.
            case S::HoldLine:
            case S::Wavering:
            case S::Breaking:
            case S::Rallied: return set != VoiceSet::Undead;
            case S::EnemySighted: return set == VoiceSet::Brute;
            default: return false;
        }
    }
    switch (s) {
        case S::HeroRally: return set == VoiceSet::Hero;
        // Heroes never break, and lead rather than hold the line.
        case S::HoldLine:
        case S::Wavering:
        case S::Breaking:
        case S::Rallied: return set != VoiceSet::Hero;
        case S::Fuse: return set == VoiceSet::Dwarf;
        case S::Healing: return set == VoiceSet::Wizard;
        case S::EngineLoose: return set == VoiceSet::Engine;
        case S::Charge: return set != VoiceSet::Engine;
        default: return true;
    }
}

int priorityOf(Situation s) {
    using S = Situation;
    switch (s) {
        case S::Idle: return 0;
        case S::ArrowFire:
        case S::Flanked:
        case S::FriendKilled:
        case S::Wounded:
        case S::Wavering:
        case S::VetKill:
        case S::Healing:
        case S::EngineLoose: return 1;
        case S::Kill:
        case S::Promoted:
        case S::Breaking:
        case S::Rallied:
        case S::Charge:
        case S::ChargeSeen:
        case S::Fuse:
        case S::FriendlyFire: return 2;
        case S::Selected:
        case S::Move:
        case S::Attack:
        case S::EnemySighted: return 3;
        case S::HoldLine:
        case S::HeroRally:
        case S::EnemyHeroFell:
        case S::GateBroken: return 4;
        case S::HeroFell:
        case S::Victory:
        case S::Defeat: return 5;
        case S::Count: break;
    }
    return 0;
}

void Voice::setListener(const V3& at, const V3& right, float zoom) {
    hasListener_ = true;
    listenAt_ = at;
    listenRight_ = flat(right).normalized();
    zoom_ = zoom;
}

sim::EntityId Voice::nearestVoice(const sim::World& world, sim::PlayerId side, const V3& at, float reach, Situation s,
                                  sim::EntityId except) const {
    sim::EntityId best = sim::kNoEntity;
    float bestD2 = reach * reach;
    for (const sim::Unit& u : world.units()) {
        if (!u.alive() || u.owner != side || u.id == except) continue;
        if (!voices(voiceSetOf(static_cast<UnitType>(u.stats.type)), s)) continue;
        const V3 d = flat(toV3(u.pos) - at);
        const float d2 = d.dot(d);
        if (d2 < bestD2) {
            bestD2 = d2;
            best = u.id;
        }
    }
    return best;
}

void Voice::want(Situation s, sim::EntityId speaker, bool positional) {
    if (speaker != sim::kNoEntity) wanted_.push_back({s, speaker, positional});
}

void Voice::order(Situation s, const sim::World& world, const std::vector<sim::EntityId>& units) {
    // A hero answers for those he leads; else the most senior of them.
    sim::EntityId best = sim::kNoEntity;
    int bestScore = -1;
    for (sim::EntityId id : units) {
        const sim::Unit* u = world.findUnit(id);
        if (!u || !u->alive() || u->routed) continue;
        const VoiceSet set = voiceSetOf(static_cast<UnitType>(u->stats.type));
        if (!voices(set, s)) continue;
        const int score = (set == VoiceSet::Hero ? 100 : 0) + sim::veteranGrade(*u) * 10 + (nonVerbal(set) ? 0 : 5);
        if (score > bestScore) {
            bestScore = score;
            best = id;
        }
    }
    if (best == sim::kNoEntity) return;
    tick_ = world.tick();
    want(s, best, false);
    flush(world);
}

bool Voice::gate(const sim::World& world, const Candidate& c) {
    const sim::Unit* u = world.findUnit(c.speaker);
    if (!u) return false;
    const VoiceSet set = voiceSetOf(static_cast<UnitType>(u->stats.type));
    if (!voices(set, c.situation)) return false;
    const int side = u->owner == player_ ? 0 : 1;
    const int si = static_cast<int>(c.situation);
    const int prio = priorityOf(c.situation);
    const bool isOrder = c.situation == Situation::Selected || c.situation == Situation::Move || c.situation == Situation::Attack ||
                         (!c.positional && c.situation == Situation::Charge);

    VoiceCue cue;
    cue.tick = tick_;
    cue.speaker = c.speaker;
    cue.side = u->owner;
    cue.enemy = u->owner != player_;
    cue.set = set;
    cue.situation = c.situation;
    cue.pos = toV3(u->pos);
    cue.priority = isOrder ? std::max(prio, 3) : prio;
    if (hasListener_) {
        const V3 d = flat(cue.pos - listenAt_);
        const float horiz = d.length();
        const float height = zoom_ * 0.6f;
        const float eff = std::sqrt(horiz * horiz + height * height);
        const float g = std::clamp(1.0f - eff / kHearing, 0.0f, 1.0f);
        cue.gain = std::pow(g, 1.5f);
        cue.pan = horiz > 1 ? std::clamp(d.dot(listenRight_) / std::max(horiz, eff * 0.5f), -0.8f, 0.8f) : 0.0f;
    }
    if (!c.positional) cue.gain = std::max(cue.gain, 0.85f);
    if (cue.gain < kMinGain) return false;  // out of earshot: costs no throttle

    const uint32_t cd = static_cast<uint32_t>(kCooldownTenths[si] * kTps / 10);
    if (heardSituation_[side][si] && tick_ - lastSituation_[side][si] < cd) return false;
    if (lastSpoke_.size() < world.units().size() + 1) lastSpoke_.resize(world.units().size() + 1, 0);
    const bool grave = cue.priority >= 4;
    if (!grave && !isOrder && lastSpoke_[c.speaker] != 0 && tick_ - lastSpoke_[c.speaker] < kSpeakerGap) return false;
    while (!window_.empty() && tick_ - window_.front() >= kWindow) window_.pop_front();
    if (!grave && !isOrder) {
        if (anyAmbient_ && tick_ - lastAmbient_ < kAmbientGap) return false;
        if (window_.size() >= kWindowMax) return false;
    }

    heardSituation_[side][si] = true;
    lastSituation_[side][si] = tick_;
    lastSpoke_[c.speaker] = std::max<uint32_t>(tick_, 1);
    // Orders and grave news hold back the chatter after them too.
    anyAmbient_ = true;
    lastAmbient_ = tick_;
    if (!isOrder) window_.push_back(tick_);
    out_.push_back(cue);
    log_.push_back(cue);
    if (log_.size() > kLogSize) log_.pop_front();
    ++emitted_;
    return true;
}

void Voice::flush(const sim::World& world) {
    std::stable_sort(wanted_.begin(), wanted_.end(),
                     [](const Candidate& a, const Candidate& b) { return priorityOf(a.situation) > priorityOf(b.situation); });
    for (const Candidate& c : wanted_)
        if (!gate(world, c)) ++dropped_;
    wanted_.clear();
}

void Voice::observe(const sim::World& world, int outcome) {
    tick_ = world.tick();
    const auto& units = world.units();
    const auto unitAt = [&](sim::EntityId id) -> const sim::Unit* { return id >= 1 && id <= units.size() ? &units[id - 1] : nullptr; };
    if (!primed_) {
        track(world);
        primed_ = true;
        return;
    }
    if (seen_.size() < units.size()) seen_.resize(units.size());
    using S = Situation;

    // Rally calls, as the simulation made them.
    for (const sim::RallyCall& c : world.rallyCalls())
        want(c.kind == sim::RallyCall::Kind::Hero ? S::HeroRally : S::HoldLine, c.caller);

    // A target down: one who presses on cries out as he goes at the next
    // (going back to his friends, he goes quietly).
    for (const sim::OrderDecision& d : world.orderDecisions())
        if (d.pressOn) want(S::Charge, d.unit);

    // A hero's fall: his own side despairs (the player must hear it), the other exults.
    for (const sim::HeroDeath& d : world.heroDeaths()) {
        const V3 at = toV3(d.pos);
        want(S::HeroFell, nearestVoice(world, d.owner, at, 45, S::HeroFell), d.owner != player_);
        const sim::Unit* killer = unitAt(d.killer);
        const sim::PlayerId foe = killer ? killer->owner : static_cast<sim::PlayerId>(d.owner == player_ ? 1 - player_ : player_);
        want(S::EnemyHeroFell, killer && voices(voiceSetOf(static_cast<UnitType>(killer->stats.type)), S::EnemyHeroFell)
                                   ? killer->id
                                   : nearestVoice(world, foe, at, 45, S::EnemyHeroFell));
    }

    // Each soldier's own changes: fear, flight, rallying, kills, ranks, wounds, a charge.
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        const Seen& was = seen_[i];
        if (!u.alive() || !was.alive) continue;
        if (u.routed && !was.routed) want(S::Breaking, u.id);
        if (!u.routed && was.routed) want(S::Rallied, u.id);
        if (u.wavering() && !was.wavering && !u.routed) want(S::Wavering, u.id);
        if (u.rank > was.rank) want(S::Promoted, u.id);
        else if (u.kills > was.kills) want(was.kills == 0 ? S::Kill : sim::veteranGrade(u) >= 2 ? S::VetKill : S::Kill, u.id);
        const bool wounded = u.hp * 100 < u.stats.maxHp * 35;
        if (wounded && !was.wounded) want(S::Wounded, u.id);
        const bool galloping = u.stats.mounted && u.momentum * 5 >= u.stats.moveSpeed * 3 && u.momentum >= sim::Fixed::fromRatio(5, 2);
        if (galloping && !was.galloping && !u.routed) {
            // Riding at someone: the rider's cry, and his target's.
            const V3 at = toV3(u.pos);
            const sim::PlayerId foe = static_cast<sim::PlayerId>(u.owner == 0 ? 1 : 0);
            const sim::EntityId seenBy = nearestVoice(world, foe, at, 35, S::ChargeSeen);
            if (seenBy != sim::kNoEntity) {
                want(S::Charge, u.id);
                if (!unitAt(seenBy)->stats.mounted) want(S::ChargeSeen, seenBy);
            }
        }
    }

    // What struck whom this tick.
    const auto flightOf = [&](sim::EntityId id) -> const Flight* {
        auto it = std::lower_bound(flights_.begin(), flights_.end(), id, [](const Flight& f, sim::EntityId x) { return f.id < x; });
        return it != flights_.end() && it->id == id ? &*it : nullptr;
    };
    struct Blast {
        V3 at;
        float radius;
        int owner;  // -1 unknown
    };
    std::vector<Blast> blasts;
    for (const sim::ImpactEvent& e : world.impacts()) {
        if (e.kind != sim::ImpactEvent::Kind::Explosion) continue;
        // Whose: the bomb or fireball that was nearest there last tick.
        const V3 at = toV3(e.pos);
        int owner = -1;
        float best = 9.0f;
        for (const Flight& f : flights_) {
            if (f.kind != sim::ProjectileKind::Bomb && f.kind != sim::ProjectileKind::Fireball) continue;
            const V3 d = f.pos - at;
            if (d.dot(d) < best) {
                best = d.dot(d);
                owner = f.owner;
            }
        }
        blasts.push_back({at, static_cast<float>(e.radius.toDouble()), owner});
    }
    for (const sim::ImpactEvent& e : world.impacts()) {
        using K = sim::ImpactEvent::Kind;
        if (e.kind == K::Gate && e.killed && e.gate >= 0 && static_cast<size_t>(e.gate) < world.gates().size()) {
            const sim::PlayerId holder = world.gates()[static_cast<size_t>(e.gate)].holder;
            for (sim::PlayerId side : {sim::PlayerId(0), sim::PlayerId(1)})
                if (side != holder) want(S::GateBroken, nearestVoice(world, side, toV3(e.pos), 80, S::GateBroken));
            continue;
        }
        if (e.kind == K::Heal) {
            const sim::Unit* healed = unitAt(e.hitUnit);
            if (!healed) continue;
            // The healer: the nearest of his friends who mends.
            sim::EntityId healer = sim::kNoEntity;
            float best = 15.0f * 15.0f;
            for (const sim::Unit& h : units) {
                if (!h.alive() || h.owner != healed->owner || h.stats.healing.amount == 0 || h.id == healed->id) continue;
                const V3 d = flat(toV3(h.pos - healed->pos));
                if (d.dot(d) < best) {
                    best = d.dot(d);
                    healer = h.id;
                }
            }
            want(S::Healing, healer);
            continue;
        }
        if (e.kind != K::Hit && e.kind != K::Blocked) continue;
        const sim::Unit* victim = unitAt(e.hitUnit);
        if (!victim) continue;
        if (victim->owner == player_) lastFight_ = tick_;
        const V3 at = toV3(victim->pos);
        // A friend cut down beside him.
        if (e.kind == K::Hit && e.killed) want(S::FriendKilled, nearestVoice(world, victim->owner, at, 10, S::FriendKilled, victim->id));
        if (!victim->alive()) continue;
        if (e.projectile != sim::kNoEntity) {
            const Flight* f = flightOf(e.projectile);
            if (f && f->owner == victim->owner) want(S::FriendlyFire, victim->id);
            else if (f && f->kind == sim::ProjectileKind::Arrow) want(S::ArrowFire, victim->id);
            continue;
        }
        // No missile: a blast, or a blow.
        bool blasted = false;
        for (const Blast& b : blasts) {
            const V3 d = flat(at - b.at);
            if (d.dot(d) > (b.radius + 1) * (b.radius + 1)) continue;
            blasted = true;
            if (b.owner == victim->owner) want(S::FriendlyFire, victim->id);
        }
        if (!blasted && e.kind == K::Hit && sim::hitSide(victim->facing, e.dir) != sim::HitSide::Front) want(S::Flanked, victim->id);
    }

    // Satchels lit and stones thrown: new missiles since last tick.
    for (const sim::Projectile& p : world.projectiles()) {
        if (p.kind != sim::ProjectileKind::Bomb && p.kind != sim::ProjectileKind::Stone) continue;
        if (flightOf(p.id)) continue;
        want(p.kind == sim::ProjectileKind::Bomb ? S::Fuse : S::EngineLoose, p.shooter);
    }

    // First contact, each side: re-armed after a long quiet.
    if (tick_ % 15 == 0) {
        for (int s = 0; s < 2; ++s) {
            const sim::PlayerId side = s == 0 ? player_ : static_cast<sim::PlayerId>(1 - player_);
            sim::EntityId spotter = sim::kNoEntity;
            bool near = false;
            for (const sim::Unit& a : units) {
                if (!a.alive() || a.owner != side) continue;
                for (const sim::Unit& b : units) {
                    if (!b.alive() || b.owner == side) continue;
                    const V3 d = flat(toV3(a.pos - b.pos));
                    const float d2 = d.dot(d);
                    if (d2 < 4 * kSighted * kSighted) near = true;
                    if (d2 < kSighted * kSighted && spotter == sim::kNoEntity &&
                        voices(voiceSetOf(static_cast<UnitType>(a.stats.type)), S::EnemySighted))
                        spotter = a.id;
                }
                if (spotter != sim::kNoEntity) break;
            }
            if (near) lastContact_[s] = tick_;
            if (!contact_[s] && spotter != sim::kNoEntity) {
                contact_[s] = true;
                want(S::EnemySighted, spotter);
            } else if (contact_[s] && tick_ - lastContact_[s] > kContactLost) {
                contact_[s] = false;
            }
        }
    }

    // The end: the winners cheer, the beaten despair (the last of them, or the last to fall).
    if (outcome != 0 && lastOutcome_ == 0) {
        const sim::PlayerId winner = outcome == 1 ? player_ : static_cast<sim::PlayerId>(1 - player_);
        const sim::PlayerId loser = static_cast<sim::PlayerId>(1 - winner);
        const V3 at = hasListener_ ? listenAt_ : V3{};
        want(S::Victory, nearestVoice(world, winner, at, 1e6f, S::Victory), winner != player_);
        sim::EntityId last = nearestVoice(world, loser, at, 1e6f, S::Defeat);
        if (last == sim::kNoEntity)
            for (const sim::Unit& u : units)
                if (u.owner == loser && voices(voiceSetOf(static_cast<UnitType>(u.stats.type)), S::Defeat)) last = u.id;
        want(S::Defeat, last, loser != player_);
    }
    lastOutcome_ = outcome;

    // Idle chatter, rarely, when all is quiet: someone near the view centre.
    if (outcome == 0 && tick_ % kTps == 0 && tick_ - lastFight_ > kQuiet) {
        idleSeed_ = idleSeed_ * 1664525u + 1013904223u;
        if ((idleSeed_ >> 24) < 40) {
            const V3 at = hasListener_ ? listenAt_ : V3{};
            want(S::Idle, nearestVoice(world, player_, at, 60, S::Idle));
        }
    }

    flush(world);
    track(world);
}

void Voice::track(const sim::World& world) {
    const auto& units = world.units();
    seen_.resize(units.size());
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        Seen& s = seen_[i];
        s.alive = u.alive();
        s.routed = u.routed;
        s.wavering = u.wavering();
        s.wounded = u.hp * 100 < u.stats.maxHp * 35;
        s.galloping = u.stats.mounted && u.momentum * 5 >= u.stats.moveSpeed * 3 && u.momentum >= sim::Fixed::fromRatio(5, 2);
        s.rank = u.rank;
        s.kills = u.kills;
    }
    flights_.clear();
    for (const sim::Projectile& p : world.projectiles()) flights_.push_back({p.id, p.shooter, p.owner, p.kind, toV3(p.pos)});
    std::sort(flights_.begin(), flights_.end(), [](const Flight& a, const Flight& b) { return a.id < b.id; });
}

std::vector<VoiceCue> Voice::take() {
    std::vector<VoiceCue> out;
    out.swap(out_);
    return out;
}

std::string Voice::logJson(size_t n) const {
    std::string s = "[";
    const size_t start = log_.size() > n ? log_.size() - n : 0;
    char buf[256];
    for (size_t i = start; i < log_.size(); ++i) {
        const VoiceCue& c = log_[i];
        std::snprintf(buf, sizeof buf,
                      "%s{\"tick\":%u,\"t\":%.2f,\"set\":\"%s\",\"situation\":\"%s\",\"speaker\":%u,\"enemy\":%s,"
                      "\"priority\":%d,\"gain\":%.2f,\"pan\":%.2f,\"x\":%.1f,\"y\":%.1f}",
                      i > start ? "," : "", c.tick, static_cast<double>(c.tick) / kTps, voiceSetName(c.set),
                      situationName(c.situation), c.speaker, c.enemy ? "true" : "false", c.priority, c.gain, c.pan, c.pos.x,
                      c.pos.y);
        s += buf;
    }
    return s + "]";
}

}  // namespace illyria::game
