// Sound effects: sim events to throttled positional cues, the scene to ambience (sfx.h).
#include "illyria/game/sfx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "illyria/sim/units.h"

namespace illyria::game {

using sim::UnitType;

namespace {

constexpr int kTps = sim::kTicksPerSecond;
// Hearing, as for the voices: a sound fades out over kHearing metres from the
// view centre, the camera's height counting against it. Loud ones (blasts,
// the gate) carry kLoudHearing.
constexpr float kHearing = 160.0f;
constexpr float kLoudHearing = 420.0f;
constexpr float kMinGain = 0.03f;
// At most this many cues a tick (the gravest and nearest first).
constexpr int kMaxPerTick = 6;
// Arrows whistle when they pass within kWhistleReach of the listener's ear,
// while the camera is low enough to be among them.
constexpr float kWhistleReach = 7.0f;
constexpr float kWhistleZoom = 140.0f;
// The ambience is worked out every kAmbienceTicks.
constexpr uint32_t kAmbienceTicks = kTps / 2;
// Carrion flies find a body after this long.
constexpr uint32_t kFliesAfter = 12 * kTps;

struct Spec {
    const char* name;
    int priority;
    int gapTicks;  // the least time between two cues of this sound
    bool loud;
};
constexpr Spec kSpecs[kSoundCount] = {
    {"snd_weap_arrow_bowstring_release", 1, 3, false},
    {"snd_weap_arrow_whistle", 2, 6, false},
    {"snd_weap_sword_swing", 1, 3, false},
    {"snd_weap_heavy_swing", 2, 4, false},
    {"snd_weap_sword_flesh", 2, 2, false},
    {"snd_weap_sword_armor", 2, 2, false},
    {"snd_weap_blunt_crush", 2, 3, false},
    {"snd_weap_pierce_flesh", 2, 3, false},
    {"snd_weap_block_shield", 1, 3, false},
    {"snd_weap_hit_bone", 2, 3, false},
    {"snd_weap_satchel_throw", 2, 4, false},
    {"snd_weap_satchel_fuse", 2, 6, false},
    {"snd_weap_fireball_cast", 3, 4, false},
    {"snd_weap_catapult_release", 3, 6, true},
    {"snd_impact_arrow_dirt", 0, 2, false},
    {"snd_impact_arrow_wood", 1, 3, false},
    {"snd_impact_arrow_stone", 1, 3, false},
    {"snd_impact_arrow_shield", 1, 3, false},
    {"snd_impact_arrow_armor", 1, 3, false},
    {"snd_impact_arrow_flesh", 2, 2, false},
    {"snd_impact_arrow_water", 0, 3, false},
    {"snd_impact_stone_smash", 4, 2, true},
    {"snd_impact_gate_wood", 3, 4, true},
    {"snd_impact_gate_break", 5, 0, true},
    {"snd_expl_satchel", 5, 1, true},
    {"snd_expl_fireball", 4, 1, true},
    {"snd_gore_dismember_snap", 3, 2, false},
    {"snd_gore_gib_burst", 4, 2, false},
    {"snd_gore_blood_rain", 2, 6, false},
    {"snd_gore_body_fall", 1, 3, false},
    {"snd_gore_body_fall_armor", 1, 3, false},
    {"snd_gore_bones_collapse", 2, 3, false},
    {"snd_gore_flesh_burn", 2, 6, false},
    {"snd_magic_heal", 1, 6, false},
    {"snd_state_victory_fanfare", 5, 0, false},
    {"snd_state_defeat_drone", 5, 0, false},
    {"snd_amb_thunder", 3, 0, false},
    {"snd_amb_swamp_frogs", 0, 20, false},
};

V3 flat(V3 v) { return {v.x, v.y, 0}; }

// Mail and plate ring; the rest is flesh (or bone, or timber).
bool armoured(UnitType t) {
    switch (t) {
        case UnitType::HeavyInfantry:
        case UnitType::HeavyCavalry:
        case UnitType::Dwarves:
        case UnitType::Hero:
        case UnitType::Warrior:
        case UnitType::Archons: return true;
        default: return false;
    }
}
bool big(UnitType t) {
    switch (t) {
        case UnitType::Giants:
        case UnitType::Minotaurs:
        case UnitType::Elephants:
        case UnitType::Dragons:
        case UnitType::Devils:
        case UnitType::Demons: return true;
        default: return false;
    }
}

}  // namespace

const char* soundName(Sound s) { return kSpecs[static_cast<int>(s)].name; }
int soundPriority(Sound s) { return kSpecs[static_cast<int>(s)].priority; }

void Sfx::setListener(const V3& at, const V3& right, float zoom) {
    hasListener_ = true;
    listenAt_ = at;
    listenRight_ = flat(right).normalized();
    zoom_ = zoom;
}

void Sfx::place(SfxCue& c) const {
    if (!c.positional || !hasListener_) {
        c.gain = c.positional ? 0.6f : 1.0f;
        return;
    }
    const V3 d = flat(c.pos - listenAt_);
    const float horiz = d.length();
    const float height = zoom_ * 0.6f;
    const float eff = std::sqrt(horiz * horiz + height * height);
    const float hearing = kSpecs[static_cast<int>(c.sound)].loud ? kLoudHearing : kHearing;
    const float g = std::clamp(1.0f - eff / hearing, 0.0f, 1.0f);
    c.gain = g * g;
    c.distance = std::clamp(eff / hearing, 0.0f, 1.0f);
    c.pan = horiz > 1 ? std::clamp(d.dot(listenRight_) / std::max(horiz, eff * 0.5f), -0.9f, 0.9f) : 0.0f;
}

void Sfx::want(Sound s, const V3& pos, float delay, bool positional, float doppler) {
    SfxCue c;
    c.tick = tick_;
    c.sound = s;
    c.pos = pos;
    c.positional = positional;
    c.delay = delay;
    c.doppler = doppler;
    c.priority = soundPriority(s);
    place(c);
    if (c.gain < kMinGain) return;  // out of earshot: costs nothing
    Candidate& w = wanted_[static_cast<int>(s)];
    ++w.count;
    if (!w.any || c.gain > w.best.gain) w.best = c;
    w.any = true;
}

void Sfx::flush() {
    // Each sound once a tick at most, the loudest of its kind standing for the
    // rest (a volley is one louder twang); then the gravest and nearest few.
    std::vector<SfxCue> ready;
    for (int i = 0; i < kSoundCount; ++i) {
        Candidate& w = wanted_[i];
        if (!w.any) continue;
        const uint32_t gap = static_cast<uint32_t>(kSpecs[i].gapTicks);
        if (heard_[i] && tick_ - last_[i] < gap) {
            dropped_ += w.count;
        } else {
            SfxCue c = w.best;
            c.count = w.count;
            c.gain = std::min(1.0f, c.gain * (1.0f + 0.18f * static_cast<float>(std::min(w.count - 1, 4))));
            ready.push_back(c);
        }
        w = Candidate{};
    }
    std::stable_sort(ready.begin(), ready.end(), [](const SfxCue& a, const SfxCue& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.gain > b.gain;
    });
    for (size_t k = 0; k < ready.size(); ++k) {
        const SfxCue& c = ready[k];
        if (static_cast<int>(k) >= kMaxPerTick) {
            dropped_ += c.count;
            continue;
        }
        const int i = static_cast<int>(c.sound);
        heard_[i] = true;
        last_[i] = tick_;
        ++perSound_[i];
        ++emitted_;
        out_.push_back(c);
        log_.push_back(c);
        if (log_.size() > kLogSize) log_.pop_front();
    }
}

Sound Sfx::arrowSurface(const sim::World& world, const sim::ImpactEvent& e) const {
    const sim::Region* region = world.region();
    const sim::Fixed ground = world.groundHeight(e.pos.x, e.pos.y);
    if (region) {
        const sim::Ground g = region->groundAt(e.pos.x, e.pos.y);
        if (g == sim::Ground::Shallows || g == sim::Ground::DeepWater) return Sound::ArrowWater;
        if (e.pos.z > ground + sim::Fixed::fromRatio(3, 10)) {
            // Struck something standing: a wall's stone, or a tree's wood.
            for (const sim::WallSeg& w : region->walls()) {
                const V3 a = toV3(w.a), b = toV3(w.b), p = toV3(e.pos);
                const V3 ab = flat(b - a);
                const float len2 = std::max(ab.dot(ab), 1e-3f);
                const float t = std::clamp(flat(p - a).dot(ab) / len2, 0.0f, 1.0f);
                const V3 q = flat(p - (a + ab * t));
                const float reach = static_cast<float>(w.halfThick.toDouble()) + 0.8f;
                if (q.dot(q) < reach * reach) return Sound::ArrowStone;
            }
            return Sound::ArrowWood;
        }
        if (g == sim::Ground::Rock) return Sound::ArrowStone;
    }
    return Sound::ArrowDirt;
}

void Sfx::observe(const sim::World& world, int outcome, const sim::Continent& continent, const V3& origin, bool enemyLand) {
    tick_ = world.tick();
    const auto& units = world.units();
    const auto unitAt = [&](sim::EntityId id) -> const sim::Unit* { return id >= 1 && id <= units.size() ? &units[id - 1] : nullptr; };
    const auto flightOf = [&](sim::EntityId id) -> const Flight* {
        auto it = std::lower_bound(flights_.begin(), flights_.end(), id, [](const Flight& f, sim::EntityId x) { return f.id < x; });
        return it != flights_.end() && it->id == id ? &*it : nullptr;
    };

    if (primed_) {
        // New missiles: the loosing.
        for (const sim::Projectile& p : world.projectiles()) {
            if (p.kind == sim::ProjectileKind::Debris || flightOf(p.id)) continue;
            const V3 at = toV3(p.pos);
            switch (p.kind) {
                case sim::ProjectileKind::Arrow: want(Sound::BowRelease, at); break;
                case sim::ProjectileKind::Bomb:
                    want(Sound::SatchelThrow, at);
                    want(Sound::SatchelFuse, at, 0.25f);
                    break;
                case sim::ProjectileKind::Fireball: want(Sound::FireballCast, at); break;
                case sim::ProjectileKind::Stone: want(Sound::CatapultRelease, at); break;
                default: break;
            }
        }

        // Blasts first: the blows they deal are heard in the blast itself.
        struct Blast {
            V3 at;
            float radius;
        };
        std::vector<Blast> blasts;
        for (const sim::ImpactEvent& e : world.impacts()) {
            using K = sim::ImpactEvent::Kind;
            if (e.kind != K::Explosion && e.kind != K::Smash) continue;
            const V3 at = toV3(e.pos);
            blasts.push_back({at, static_cast<float>(e.radius.toDouble()) + 1.0f});
            if (e.kind == K::Smash) {
                want(Sound::StoneSmash, at);
                continue;
            }
            // Whose: the bomb or fireball nearest there last tick.
            bool fire = false;
            float best = 16.0f;
            for (const Flight& f : flights_) {
                if (f.kind != sim::ProjectileKind::Bomb && f.kind != sim::ProjectileKind::Fireball) continue;
                const V3 d = f.pos - at;
                if (d.dot(d) < best) {
                    best = d.dot(d);
                    fire = f.kind == sim::ProjectileKind::Fireball;
                }
            }
            want(fire ? Sound::FireballBlast : Sound::SatchelBlast, at);
        }
        const auto inBlast = [&](const V3& p) {
            for (const Blast& b : blasts) {
                const V3 d = flat(p - b.at);
                if (d.dot(d) < b.radius * b.radius) return true;
            }
            return false;
        };

        for (const sim::ImpactEvent& e : world.impacts()) {
            using K = sim::ImpactEvent::Kind;
            const V3 at = toV3(e.pos);
            switch (e.kind) {
                case K::Ground: want(arrowSurface(world, e), at); break;
                case K::Gate: want(e.killed ? Sound::GateBreak : Sound::GateHit, at); break;
                case K::Heal: want(Sound::Heal, at); break;
                case K::Blocked: {
                    const sim::Unit* v = unitAt(e.hitUnit);
                    if (e.projectile != sim::kNoEntity) {
                        want(v && v->stats.shield.blockChance > sim::Fixed::zero() ? Sound::ArrowShield : Sound::ArrowStone, at);
                    } else {
                        want(Sound::SwordSwing, at);
                        want(Sound::BlockShield, at, 0.06f);
                    }
                    break;
                }
                case K::Hit: {
                    const sim::Unit* v = unitAt(e.hitUnit);
                    if (!v) break;
                    const auto type = static_cast<UnitType>(v->stats.type);
                    const bool bone = !v->stats.bleeds && type != UnitType::Catapult;
                    if (e.projectile != sim::kNoEntity) {
                        const Flight* f = flightOf(e.projectile);
                        if (f && f->kind != sim::ProjectileKind::Arrow) break;
                        want(bone ? Sound::HitBone : armoured(type) ? Sound::ArrowArmor : type == UnitType::Catapult ? Sound::ArrowWood : Sound::ArrowFlesh, at);
                        break;
                    }
                    if (inBlast(at) || e.cause == sim::DamageType::Explosive || e.cause == sim::DamageType::Fire) break;
                    // A hand blow: the swing, then what it met.
                    const bool heavy = e.cause == sim::DamageType::Blunt || e.cause == sim::DamageType::Cleave;
                    want(heavy ? Sound::HeavySwing : Sound::SwordSwing, at);
                    Sound hit = Sound::SwordFlesh;
                    if (bone) hit = Sound::HitBone;
                    else if (e.cause == sim::DamageType::Blunt) hit = Sound::BluntCrush;
                    else if (e.cause == sim::DamageType::Pierce) hit = armoured(type) ? Sound::SwordArmor : Sound::PierceFlesh;
                    else if (armoured(type)) hit = Sound::SwordArmor;
                    want(hit, at, 0.07f);
                    break;
                }
                default: break;
            }
        }

        // The deaths: how they died is how they sound.
        for (const sim::DeathEvent& d : world.deathEvents()) {
            const V3 at = toV3(d.pos);
            const auto type = static_cast<UnitType>(d.type);
            const sim::Unit* u = unitAt(d.unit);
            const bool bone = u && !u->stats.bleeds && type != UnitType::Catapult;
            const Sound fall = bone ? Sound::BonesCollapse : armoured(type) || big(type) ? Sound::BodyFallArmor : Sound::BodyFall;
            switch (d.kind) {
                case sim::DeathKind::Severed:
                    want(Sound::DismemberSnap, at, 0.05f);
                    want(fall, at, 0.55f);
                    break;
                case sim::DeathKind::Gibbed:
                    want(Sound::GibBurst, at, 0.03f);
                    if (!bone) want(Sound::BloodRain, at, 0.45f);
                    break;
                case sim::DeathKind::Charred:
                    want(Sound::FleshBurn, at, 0.2f);
                    want(fall, at, 0.6f);
                    break;
                default: want(fall, at, 0.6f); break;
            }
        }

        whistles(world);

        // The end of the battle.
        if (outcome != 0 && lastOutcome_ == 0) want(outcome == 1 ? Sound::Victory : Sound::Defeat, listenAt_, 1.2f, false);
    }
    lastOutcome_ = outcome;
    primed_ = true;

    if (tick_ % kAmbienceTicks == 0) updateAmbience(world, continent, origin, enemyLand);
    flush();

    flights_.clear();
    for (const sim::Projectile& p : world.projectiles())
        if (p.kind != sim::ProjectileKind::Debris) flights_.push_back({p.id, p.kind, toV3(p.pos)});
    std::sort(flights_.begin(), flights_.end(), [](const Flight& a, const Flight& b) { return a.id < b.id; });
}

void Sfx::whistles(const sim::World& world) {
    if (!hasListener_ || zoom_ > kWhistleZoom) return;
    // The ear: a little above the view centre, higher as the camera rises.
    const V3 ear = listenAt_ + V3{0, 0, std::min(zoom_ * 0.25f, 12.0f) + 1.5f};
    std::vector<sim::EntityId> still;
    for (const sim::Projectile& p : world.projectiles()) {
        if (p.kind != sim::ProjectileKind::Arrow) continue;
        const bool heard = std::binary_search(whistled_.begin(), whistled_.end(), p.id);
        if (heard) {
            still.push_back(p.id);
            continue;
        }
        const V3 pos = toV3(p.pos), vel = toV3(p.vel);
        const V3 rel = pos - ear;
        if (rel.dot(rel) > kWhistleReach * kWhistleReach) continue;
        const float speed = std::max(vel.length(), 1.0f);
        // Closing speed: positive while it is still coming nearer.
        const float closing = -rel.dot(vel) / std::max(rel.length(), 0.5f);
        want(Sound::ArrowWhistle, pos, 0, true, std::clamp(closing, -speed, speed));
        still.push_back(p.id);
    }
    std::sort(still.begin(), still.end());
    whistled_.swap(still);
}

void Sfx::updateAmbience(const sim::World& world, const sim::Continent& continent, const V3& origin, bool enemyLand) {
    Ambience a;
    const V3 at = hasListener_ ? listenAt_ : V3{};
    const float zoomLift = std::clamp((zoom_ - 40.0f) / 400.0f, 0.0f, 0.4f);

    // Wind, by the land under the view: the high passes howl, the lowlands sweep.
    const sim::Continent::Sample s = continent.sample(toFixed(origin.x + at.x), toFixed(origin.y + at.y));
    const float mountain = static_cast<float>(s.mountain.toDouble());
    const float height = static_cast<float>(s.height.toDouble());
    const float high = std::clamp(mountain / 120.0f + std::max(0.0f, height - 150.0f) / 600.0f, 0.0f, 1.0f);
    a.windMountain = std::clamp(high * 0.9f + zoomLift * high, 0.0f, 1.0f);
    a.windValley = std::clamp((1.0f - high) * 0.45f + zoomLift, 0.0f, 1.0f);

    // Marsh about the view: a grid of samples, the insects panned towards the wet.
    if (const sim::Region* region = world.region()) {
        int wet = 0, n = 0;
        float side = 0;
        for (int j = -3; j <= 3; ++j)
            for (int i = -3; i <= 3; ++i) {
                const V3 p = at + V3{i * 15.0f, j * 15.0f, 0};
                const sim::Ground g = region->groundAt(toFixed(p.x), toFixed(p.y));
                ++n;
                if (g == sim::Ground::Swamp || g == sim::Ground::Mud) {
                    ++wet;
                    side += (p - at).dot(listenRight_);
                }
            }
        a.swamp = std::clamp(static_cast<float>(wet) / static_cast<float>(n) * 2.5f, 0.0f, 1.0f) * (1.0f - zoomLift);
        a.swampPan = wet > 0 ? std::clamp(side / static_cast<float>(wet) / 45.0f, -0.8f, 0.8f) : 0.0f;
    }

    // Flies over the dead near the view, louder the more there lie.
    float bodies = 0, side = 0;
    V3 nearestFoe;
    float foeD2 = 1e12f, contactD2 = 1e12f;
    for (const sim::Unit& u : world.units()) {
        const V3 p = toV3(u.pos);
        if (!u.alive()) {
            if (!u.stats.bleeds || tick_ - u.death.tick < kFliesAfter) continue;
            const V3 d = flat(p - at);
            if (d.dot(d) > 45.0f * 45.0f) continue;
            const float w = u.gibbed ? 0.6f : 1.0f;
            bodies += w;
            side += d.dot(listenRight_) * w;
            continue;
        }
        if (u.owner == player_) continue;
        // The enemy: how near to the view, and to any of ours.
        const V3 d = flat(p - at);
        if (d.dot(d) < foeD2) {
            foeD2 = d.dot(d);
            nearestFoe = p;
        }
    }
    a.flies = std::clamp(bodies / 14.0f, 0.0f, 1.0f) * (1.0f - zoomLift * 2.0f);
    a.fliesPan = bodies > 0 ? std::clamp(side / bodies / 30.0f, -0.8f, 0.8f) : 0.0f;

    // War drums: the enemy coming on (beyond bowshot of us, not yet at grips), or his land.
    for (const sim::Unit& u : world.units()) {
        if (!u.alive() || u.owner == player_) continue;
        for (const sim::Unit& o : world.units()) {
            if (!o.alive() || o.owner != player_) continue;
            const V3 d = flat(toV3(u.pos - o.pos));
            contactD2 = std::min(contactD2, d.dot(d));
        }
    }
    if (foeD2 < 1e11f) {
        const float contact = std::sqrt(contactD2);
        float drums = contact > 40.0f ? std::clamp(1.0f - (contact - 40.0f) / 260.0f, 0.0f, 1.0f) : 0.0f;
        if (enemyLand && contact > 25.0f) drums = std::max(drums, 0.3f);
        a.drums = drums;
        const V3 d = flat(nearestFoe - at);
        const float len = d.length();
        a.drumsPan = len > 1 ? std::clamp(d.dot(listenRight_) / len, -0.8f, 0.8f) * 0.8f : 0.0f;
    }

    // Weather (a hook until there is weather): rain, and now and then thunder.
    a.rain = std::clamp(rain_, 0.0f, 1.0f);
    if (a.rain > 0.5f) {
        if (nextThunder_ == 0) nextThunder_ = tick_ + 8 * kTps;
        if (tick_ >= nextThunder_) {
            seed_ = seed_ * 1664525u + 1013904223u;
            // The flash is far off: the rumble comes 3 to 8 seconds after it.
            want(Sound::Thunder, at, 3.0f + static_cast<float>(seed_ >> 24) / 255.0f * 5.0f, false);
            nextThunder_ = tick_ + static_cast<uint32_t>((20 + (seed_ >> 27)) * kTps);
        }
    }
    // Now and then a frog, out in the marsh.
    if (a.swamp > 0.2f) {
        seed_ = seed_ * 1664525u + 1013904223u;
        if ((seed_ >> 24) < 90) {
            const float ang = static_cast<float>((seed_ >> 8) & 0xFF) / 255.0f * 6.2832f;
            want(Sound::Frog, at + V3{std::cos(ang) * 25.0f, std::sin(ang) * 25.0f, 0} + listenRight_ * (a.swampPan * 20.0f));
        }
    }
    ambience_ = a;
    ++ambienceVersion_;
}

std::vector<SfxCue> Sfx::take() {
    std::vector<SfxCue> out;
    out.swap(out_);
    return out;
}

std::string Sfx::logJson(size_t n) const {
    std::string s = "[";
    const size_t start = log_.size() > n ? log_.size() - n : 0;
    char buf[256];
    for (size_t i = start; i < log_.size(); ++i) {
        const SfxCue& c = log_[i];
        std::snprintf(buf, sizeof buf,
                      "%s{\"tick\":%u,\"t\":%.2f,\"sound\":\"%s\",\"count\":%d,\"priority\":%d,\"gain\":%.2f,\"pan\":%.2f,"
                      "\"dist\":%.2f,\"delay\":%.2f,\"doppler\":%.1f,\"x\":%.1f,\"y\":%.1f}",
                      i > start ? "," : "", c.tick, static_cast<double>(c.tick) / kTps, soundName(c.sound), c.count, c.priority,
                      c.gain, c.pan, c.distance, c.delay, c.doppler, c.pos.x, c.pos.y);
        s += buf;
    }
    return s + "]";
}

}  // namespace illyria::game
