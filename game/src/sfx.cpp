// Sound effects: sim events to throttled positional cues, the scene to ambience (sfx.h).
#include "illyria/game/sfx.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "illyria/game/point_grid.h"
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
constexpr float kMinGain = 0.04f;
// At most this many cues a tick (the gravest and nearest first), and this many a second of real time whatever the
// game's speed (with a small burst): the players have two dozen channels, and a fight is a texture, not a count.
constexpr int kMaxPerTick = 5;
constexpr float kCuesPerSecond = 24.0f;
constexpr float kBurst = 8.0f;
// Movement is looked at every kMoveTicks; a body of men is those of one kind in one kMoveCell.
constexpr uint32_t kMoveTicks = 5;
constexpr float kMoveCell = 24.0f;
// From this camera distance single footfalls give way to the column's loop, and the far battle's loop comes up.
constexpr float kMassZoom = 420.0f;
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
    {"snd_weap_sword_swing", 1, 4, false},
    {"snd_weap_heavy_swing", 2, 5, false},
    {"snd_weap_sword_flesh", 2, 4, false},
    {"snd_weap_sword_armor", 2, 4, false},
    {"snd_weap_blunt_crush", 2, 4, false},
    {"snd_weap_pierce_flesh", 2, 4, false},
    {"snd_weap_block_shield", 1, 4, false},
    {"snd_weap_hit_bone", 2, 4, false},
    {"snd_weap_satchel_throw", 2, 4, false},
    {"snd_weap_satchel_fuse", 3, 8, false},
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
    {"snd_state_war_horn", 5, 0, false},
    {"snd_state_alarm", 5, 150, false},
    {"snd_move_foot_march", 0, 8, false},
    {"snd_move_foot_armor", 0, 8, false},
    {"snd_move_foot_wet", 0, 8, false},
    {"snd_move_hooves", 1, 7, false},
    {"snd_move_giant_tread", 1, 10, false},
    {"snd_move_wheels", 1, 12, false},
    {"snd_move_wings", 0, 8, false},
    {"snd_weap_satchel_lay", 2, 4, false},
    {"snd_ui_order_confirm", 3, 0, false},
    {"snd_ui_order_refuse", 3, 0, false},
    {"snd_ui_toggle", 3, 0, false},
    {"snd_ui_select", 3, 0, false},
    {"snd_state_rally_horn", 5, 0, false},
    {"snd_state_deploy_begin", 5, 0, false},
    {"snd_state_results", 4, 0, false},
    {"snd_conq_town_captured", 5, 0, false},
    {"snd_conq_town_lost", 5, 0, false},
    {"snd_conq_day_tick", 3, 0, false},
    {"snd_conq_recruit", 3, 0, false},
    {"snd_conq_lord_fallen", 5, 0, false},
    {"snd_boulder_launch", 4, 6, true},
    {"snd_boulder_rumble", 3, 12, true},
    {"snd_boulder_impact", 4, 3, true},
    {"snd_boulder_crush", 3, 4, false},
    {"snd_call_help", 4, 60, false},
    {"snd_call_answer", 4, 60, false},
    {"snd_charge_impact", 4, 8, true},
    {"snd_morale_rout", 3, 90, false},
    {"snd_water_wade", 0, 18, false},
    {"snd_water_plop", 1, 3, false},
    {"snd_water_splash_body", 2, 3, false},
    {"snd_impact_stone_water", 4, 2, true},
    {"snd_water_steam", 3, 2, false},
};
static_assert(sizeof(kSpecs) / sizeof(kSpecs[0]) == kSoundCount);

bool isMovement(int sound) { return sound >= static_cast<int>(Sound::FootMarch) && sound <= static_cast<int>(Sound::Wings); }


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

Hearing hear(float horiz, float zoom, bool loud) {
    Hearing h;
    const float reach = (loud ? kLoudHearing : kHearing) + zoom * (loud ? 1.5f : 1.1f);
    const float lat = std::clamp(1.0f - horiz / reach, 0.0f, 1.0f);
    const float up = std::pow(std::clamp((zoom - 60.0f) / 540.0f, 0.0f, 1.0f), 0.7f);
    float level = 1.0f - (1.0f - (loud ? 0.4f : 0.22f)) * up;
    if (zoom > 600.0f) level *= 1.0f - std::clamp((zoom - 600.0f) / (loud ? 3000.0f : 900.0f), 0.0f, 1.0f) * (loud ? 0.6f : 0.85f);
    h.gain = lat * lat * level;
    h.distance = std::clamp(std::max(horiz / reach, up * 0.75f), 0.0f, 1.0f);
    return h;
}

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
    const Hearing h = hear(horiz, zoom_, kSpecs[static_cast<int>(c.sound)].loud);
    c.gain = h.gain;
    c.distance = h.distance;
    // Panned by how far across the view it is (the view is about as wide as the camera is far), not by its bearing alone:
    // a duel two metres left of centre at the close camera is nearly in the middle.
    c.pan = std::clamp(d.dot(listenRight_) / std::max(zoom_ * 0.6f, 12.0f), -0.9f, 0.9f);
}

void Sfx::want(Sound s, const V3& pos, float delay, bool positional, float doppler, float scale) {
    SfxCue c;
    c.tick = tick_;
    c.sound = s;
    c.pos = pos;
    c.positional = positional;
    c.delay = delay;
    c.doppler = doppler;
    c.priority = soundPriority(s);
    place(c);
    c.gain *= scale;
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
        // The gap is real time: at 4x a tick is a quarter as long, so four times as many must pass. At 4x and over the
        // slightest sounds (a footfall, an arrow in the dirt) are left out: the field is a blur of them.
        const uint32_t gap = static_cast<uint32_t>(std::lround(static_cast<float>(kSpecs[i].gapTicks) * speed_));
        const bool thinned = speed_ >= 4.0f && (kSpecs[i].priority == 0 || isMovement(i));
        if (thinned || (heard_[i] && tick_ - last_[i] < gap)) {
            dropped_ += w.count;
        } else {
            SfxCue c = w.best;
            c.count = w.count;
            // Many at once are one louder sound: +2 dB a doubling, to +6.
            c.gain = std::min(1.0f, c.gain * std::min(2.0f, 1.0f + 0.26f * std::log2(static_cast<float>(w.count))));
            c.delay /= speed_;
            ready.push_back(c);
        }
        w = Candidate{};
    }
    std::stable_sort(ready.begin(), ready.end(), [](const SfxCue& a, const SfxCue& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.gain > b.gain;
    });
    moveBudget_ = std::min(2.0f, moveBudget_ + 6.0f / (static_cast<float>(kTps) * speed_));
    budget_ = std::min(kBurst, budget_ + kCuesPerSecond / (static_cast<float>(kTps) * speed_));
    for (size_t k = 0; k < ready.size(); ++k) {
        const SfxCue& c = ready[k];
        // The grave ones (a blast, a gate, a horn) are always heard; the rest while the budget lasts.
        // Movement is the ground under the fight, never the fight: six a second of its own, and none of the blows' share.
        const bool move = isMovement(static_cast<int>(c.sound));
        if (c.priority < 4 && (static_cast<int>(k) >= kMaxPerTick || (move ? moveBudget_ : budget_) < 1.0f)) {
            dropped_ += c.count;
            continue;
        }
        if (move) moveBudget_ -= 1.0f;
        else if (c.priority < 4) budget_ -= 1.0f;
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

// The slosh of whoever is going through water: a few of the men a tick are looked at (all of them in a third of a second),
// and each one moving in it asks for the sound where he is; want() makes one louder slosh of many.
void Sfx::wading(const sim::World& world) {
    constexpr uint32_t kStride = 10;
    const auto& units = world.units();
    for (size_t i = tick_ % kStride; i < units.size(); i += kStride) {
        const sim::Unit& u = units[i];
        if (!u.alive() || u.inAir() || !world.wading(u)) continue;
        if (!u.hasMoveTarget && !u.engaged && u.targetUnit == sim::kNoEntity) continue;  // standing still in it
        want(Sound::Wade, toV3(u.pos));
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
    const sim::Region* region = world.region();
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
                    // Hurled by a hyena it whooshes; laid, its owner hears it set down and the match struck.
                    if (p.thrown) want(Sound::SatchelThrow, at);
                    else if (p.owner == player_) want(Sound::SatchelLay, at);
                    break;
                case sim::ProjectileKind::Molotov: want(Sound::SatchelThrow, at); break;
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
            if (e.kind != K::Explosion && e.kind != K::Smash && e.kind != K::Burst) continue;
            const V3 at = toV3(e.pos);
            blasts.push_back({at, static_cast<float>(e.radius.toDouble()) + 1.0f});
            // On water (the battle shows a column, or steam: battle_water.cpp) a stone booms and splashes, and fire hisses out.
            const bool wet = world.waterDepth(e.pos.x, e.pos.y) > sim::Fixed::zero() && region &&
                             e.pos.z <= region->waterSurfaceAt(e.pos.x, e.pos.y) + sim::Fixed::fromRatio(3, 5);
            if (e.kind == K::Smash) {
                want(wet ? Sound::StoneWater : Sound::StoneSmash, at);
                continue;
            }
            if (wet) want(Sound::Steam, at, 0.08f);
            if (e.kind == K::Burst) {
                want(Sound::FireballBlast, at);  // glass shatter and fire roar: a Molotov
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

        // When the picture shows a hand blow land: the attack clip of whoever struck it (the nearest striker of this tick
        // who is not of the struck man's side) begins now and shows the blow half way through.
        const auto blowAfter = [&](const V3& at, const sim::Unit* victim) {
            float best = 9.0f * 9.0f, after = 0.25f;
            for (const Striker& k : strikers_) {
                if (victim && k.owner == victim->owner) continue;
                const V3 d = flat(k.pos - at);
                if (d.dot(d) < best) best = d.dot(d), after = blowSeconds(k.type);
            }
            return after;
        };
        // The whoosh comes before the blow by this much (the swings' own level peaks 0.12 to 0.3 s in).
        constexpr float kSwingLead = 0.15f, kHeavyLead = 0.28f;
        for (const sim::ImpactEvent& e : world.impacts()) {
            using K = sim::ImpactEvent::Kind;
            const V3 at = toV3(e.pos);
            switch (e.kind) {
                case K::Ground: want(arrowSurface(world, e), at); break;
                // A part of a body into the water (sim world_drift.cpp): a trunk with a splash, the rest with a plop.
                case K::Splash: want(e.radius > sim::Fixed::fromRatio(7, 10) ? Sound::BodySplash : e.radius < sim::Fixed::fromRatio(3, 10) ? Sound::ArrowWater : Sound::WaterPlop, at); break;
                case K::Gate: want(e.killed ? Sound::GateBreak : Sound::GateHit, at); break;
                case K::Heal: want(Sound::Heal, at); break;
                // The boulder on the ground (sim world_boulder.cpp): its crash where it comes down hard, and against what
                // stands in its way; its rumble while it rolls is Sfx::rolling.
                case K::Bounce:
                    if (e.radius >= sim::Fixed::fromInt(3)) want(Sound::BoulderImpact, at);
                    break;
                case K::Struck:
                    // (A gate's own blow is heard as the gate's: K::Gate.)
                    if (static_cast<sim::Structure>(e.gate) == sim::Structure::Tree) want(Sound::ArrowWood, at);
                    else if (static_cast<sim::Structure>(e.gate) != sim::Structure::Gate) want(Sound::BoulderImpact, at);
                    break;
                case K::Blocked: {
                    const sim::Unit* v = unitAt(e.hitUnit);
                    if (e.projectile != sim::kNoEntity) {
                        want(v && v->stats.shield.blockChance > sim::Fixed::zero() ? Sound::ArrowShield : Sound::ArrowStone, at);
                    } else {
                        const float blow = blowAfter(at, v);
                        want(Sound::SwordSwing, at, std::max(0.0f, blow - kSwingLead));
                        want(Sound::BlockShield, at, blow);
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
                        // Under a rolling boulder: the crush.
                        if (f && f->kind == sim::ProjectileKind::Stone) want(bone ? Sound::HitBone : Sound::BoulderCrush, at);
                        if (f && f->kind != sim::ProjectileKind::Arrow) break;
                        want(bone ? Sound::HitBone : armoured(type) ? Sound::ArrowArmor : type == UnitType::Catapult ? Sound::ArrowWood : Sound::ArrowFlesh, at);
                        break;
                    }
                    if (inBlast(at) || e.cause == sim::DamageType::Explosive || e.cause == sim::DamageType::Fire) break;
                    // A hand blow: the swing, then what it met.
                    const bool heavy = e.cause == sim::DamageType::Blunt || e.cause == sim::DamageType::Cleave;
                    const float blow = blowAfter(at, v);
                    want(heavy ? Sound::HeavySwing : Sound::SwordSwing, at, std::max(0.0f, blow - (heavy ? kHeavyLead : kSwingLead)));
                    Sound hit = Sound::SwordFlesh;
                    if (bone) hit = Sound::HitBone;
                    else if (e.cause == sim::DamageType::Blunt) hit = Sound::BluntCrush;
                    else if (e.cause == sim::DamageType::Pierce) hit = armoured(type) ? Sound::SwordArmor : Sound::PierceFlesh;
                    else if (armoured(type)) hit = Sound::SwordArmor;
                    want(hit, at, blow);
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
            // Into water he goes with a splash, not a thud.
            const bool wet = world.waterDepth(d.pos.x, d.pos.y) > sim::Fixed::fromRatio(1, 10) && region && d.pos.z <= region->waterSurfaceAt(d.pos.x, d.pos.y);
            const Sound fall = wet ? Sound::BodySplash : bone ? Sound::BonesCollapse : armoured(type) || big(type) ? Sound::BodyFallArmor : Sound::BodyFall;
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
        fuses(world);
        rolling(world);
        if (tick_ % kMoveTicks == 0) movement(world);
        // The fight near the view, for the far camera's loop: blows this tick, fading.
        for (const sim::ImpactEvent& e : world.impacts()) {
            using K = sim::ImpactEvent::Kind;
            if (e.kind != K::Hit && e.kind != K::Blocked) continue;
            const V3 d = flat(toV3(e.pos) - listenAt_);
            if (d.length() > kHearing + zoom_ * 1.1f) continue;
            fighting_ += 1.0f;
            fightSide_ += d.dot(listenRight_) > 0 ? 1.0f : -1.0f;
        }
        wading(world);

        // The end of the battle.
        if (outcome != 0 && lastOutcome_ == 0) want(outcome == 1 ? Sound::Victory : Sound::Defeat, listenAt_, 1.2f, false);
    }
    lastOutcome_ = outcome;
    primed_ = true;

    if (tick_ % kAmbienceTicks == 0) updateAmbience(world, continent, origin, enemyLand);
    flush();

    flights_.clear();
    for (const sim::Projectile& p : world.projectiles())
        if (p.kind != sim::ProjectileKind::Debris) flights_.push_back({p.id, p.kind, toV3(p.pos), p.fuse});
    std::sort(flights_.begin(), flights_.end(), [](const Flight& a, const Flight& b) { return a.id < b.id; });
}

// A lit fuse hisses for as long as it burns: a satchel whose fuse is shorter than it was a tick ago (thrown, or set off by
// fire or a blast beside it), again every second and a half, each satchel in its own time.
void Sfx::fuses(const sim::World& world) {
    std::vector<std::pair<sim::EntityId, uint32_t>> still;
    for (const sim::Projectile& p : world.projectiles()) {
        if (p.kind != sim::ProjectileKind::Bomb) continue;
        auto f = std::lower_bound(flights_.begin(), flights_.end(), p.id, [](const Flight& a, sim::EntityId x) { return a.id < x; });
        const bool known = f != flights_.end() && f->id == p.id;
        const bool lit = known ? p.fuse < f->fuse : p.thrown && p.fuse > 0;
        if (!lit) continue;
        uint32_t next = 0;
        for (const auto& n : fuseNext_)
            if (n.first == p.id) next = n.second;
        if (tick_ >= next) {
            want(Sound::SatchelFuse, toV3(p.pos));
            next = tick_ + static_cast<uint32_t>(1.5f * kTps * speed_);
        }
        still.emplace_back(p.id, next);
    }
    fuseNext_.swap(still);
}

// A stone come down and still moving over the ground is rolling: its rumble, for as long as it rolls (the sound's own gap
// spaces it).
void Sfx::rolling(const sim::World& world) {
    for (const sim::Projectile& p : world.projectiles()) {
        if (p.kind != sim::ProjectileKind::Stone || !p.landed || p.resting) continue;
        const V3 v = flat(toV3(p.vel));
        if (v.dot(v) > 1.0f) want(Sound::BoulderRumble, toV3(p.pos));
    }
}

// Footfalls, hooves, a giant's tread, wheels and wings: every body of men of one kind in one cell is one textured sound,
// again at its own pace, louder the more of them; never a cue a man. From the far camera the columns' loop stands for them
// (updateAmbience), and at 4x and over they are left out (flush).
void Sfx::movement(const sim::World& world) {
    const auto& units = world.units();
    if (movedFrom_.size() != units.size()) {
        movedFrom_.resize(units.size());
        for (size_t i = 0; i < units.size(); ++i) movedFrom_[i] = toV3(units[i].pos);
        return;
    }
    struct Body {
        uint64_t key;
        Sound sound;
        V3 sum;
        int n;
        float pace;
    };
    std::vector<Body> bodies;
    const sim::Region* region = world.region();
    const float reach = kHearing + zoom_ * 1.1f;
    const float seconds = static_cast<float>(kMoveTicks) / kTps;
    float moving = 0, side = 0;
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        const V3 p = toV3(u.pos), from = movedFrom_[i];
        movedFrom_[i] = p;
        if (!u.alive() || !u.present() || u.airborne) continue;
        const V3 rel = flat(p - listenAt_);
        if (!hasListener_ || rel.length() > reach) continue;
        const float pace = flat(p - from).length() / seconds;
        if (pace < 0.5f || pace > 40.0f) continue;  // (standing; or set down somewhere else, not walked there)
        moving += 1.0f;
        side += rel.dot(listenRight_) > 0 ? 1.0f : -1.0f;
        const auto type = static_cast<UnitType>(u.stats.type);
        Sound s = armoured(type) ? Sound::FootArmor : Sound::FootMarch;
        const bool flier = u.stats.flySpeed > sim::Fixed::zero() && u.pos.z > world.groundHeight(u.pos.x, u.pos.y) + sim::Fixed::one();
        if (flier) s = Sound::Wings;
        else if (u.stats.gait == sim::Gait::Wheeled) s = Sound::Wheels;
        else if (u.stats.gait == sim::Gait::Giant) s = Sound::GiantTread;
        else if (u.stats.gait == sim::Gait::Horse || u.stats.gait == sim::Gait::Wolf) s = Sound::Hooves;
        else if (region) {
            const sim::Ground g = region->groundAt(u.pos.x, u.pos.y);
            if (g == sim::Ground::Shallows || g == sim::Ground::DeepWater) continue;  // (in water: Sfx::wading sounds them)
            if (g == sim::Ground::Swamp || g == sim::Ground::Mud) s = Sound::FootWet;
        }
        const uint64_t cx = static_cast<uint64_t>(static_cast<int64_t>(std::floor(p.x / kMoveCell)) + 100000);
        const uint64_t cy = static_cast<uint64_t>(static_cast<int64_t>(std::floor(p.y / kMoveCell)) + 100000);
        const uint64_t key = (cx << 40) | (cy << 16) | (static_cast<uint64_t>(u.owner) << 8) | static_cast<uint64_t>(s);
        auto it = std::find_if(bodies.begin(), bodies.end(), [&](const Body& b) { return b.key == key; });
        if (it == bodies.end()) bodies.push_back({key, s, p, 1, pace});
        else it->sum = it->sum + p, ++it->n, it->pace += pace;
    }
    marching_ = marching_ * 0.6f + moving * 0.4f;
    marchSide_ = moving > 0 ? side / moving : 0;
    if (zoom_ > kMassZoom) {
        treads_.clear();
        return;
    }
    std::vector<Tread> kept;
    for (const Body& b : bodies) {
        const float n = static_cast<float>(b.n), pace = b.pace / n;
        float every = 0.55f, full = 30.0f;  // seconds between sounds at a walk; how many make it full
        switch (b.sound) {
            case Sound::Hooves: every = 0.42f, full = 16.0f; break;
            case Sound::GiantTread: every = 0.8f, full = 3.0f; break;
            case Sound::Wheels: every = 0.95f, full = 2.0f; break;
            case Sound::Wings: every = 0.7f, full = 4.0f; break;
            default: break;
        }
        if (b.sound != Sound::Wheels && b.sound != Sound::Wings) every *= std::clamp(1.8f / pace, 0.6f, 1.3f);  // quicker at the run
        uint32_t next = 0;
        for (const Tread& t : treads_)
            if (t.key == b.key) next = t.next;
        if (tick_ >= next) {
            want(b.sound, b.sum * (1.0f / n), 0, true, 0, std::clamp(0.35f + 0.65f * std::sqrt(n / full), 0.35f, 1.0f));
            // (each body a little out of step with the next: by its key)
            next = tick_ + static_cast<uint32_t>(every * kTps * speed_) + static_cast<uint32_t>(b.key >> 16 & 3);
        }
        kept.push_back({b.key, next});
    }
    treads_.swap(kept);
}

float Sfx::blowSeconds(int type) const {
    if (type < 0 || type >= sim::kUnitTypeCount) return 0.3f;
    if (blow_[type] > 0) return blow_[type];
    // (half of figures::attackSeconds, the attack clip's length when a sheet names none)
    const float cooldown = static_cast<float>(sim::unitStats(static_cast<UnitType>(type)).melee.cooldownTicks) / kTps;
    return std::clamp(cooldown * 0.6f, 0.45f, 0.9f) * 0.5f;
}

void Sfx::ui(Sound s) {
    for (const SfxCue& c : out_)
        if (c.sound == s && !c.positional) return;  // (once a frame is enough)
    SfxCue c;
    c.tick = tick_;
    c.sound = s;
    c.pos = listenAt_;
    c.positional = false;
    c.priority = soundPriority(s);
    c.gain = 1;
    c.distance = 0;
    ++perSound_[static_cast<int>(s)];
    ++emitted_;
    out_.push_back(c);
    log_.push_back(c);
    if (log_.size() > kLogSize) log_.pop_front();
}

void Sfx::calm() {
    calm_ = true;
    ambience_.drums = ambience_.battle = ambience_.march = 0;
    ++ambienceVersion_;
}

void Sfx::conquest(const ConquestPulse& now) {
    if (pulsed_ && now.cityOwner.size() == pulse_.cityOwner.size() && now.side == pulse_.side) {
        int taken = 0, lost = 0;
        for (size_t i = 0; i < now.cityOwner.size(); ++i) {
            if (now.cityOwner[i] == pulse_.cityOwner[i]) continue;
            taken += now.cityOwner[i] == now.side;
            lost += pulse_.cityOwner[i] == now.side;
        }
        if (now.winner != 0 && pulse_.winner == 0) ui(now.winner == 1 ? Sound::Victory : Sound::Defeat);
        else if (now.winner == 0) {
            // The gravest of what happened since the last look: one sound, not a pile of them.
            if (now.lords < pulse_.lords) ui(Sound::LordFallen);
            else if (lost > 0) ui(Sound::TownLost);
            else if (taken > 0) ui(Sound::TownCaptured);
            else if (now.squads > pulse_.squads) ui(Sound::Recruit);
            else if (now.day != pulse_.day) ui(Sound::DayTick);
        }
    }
    pulsed_ = true;
    pulse_ = now;
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
    a.windValley = std::clamp((1.0f - high) * 0.6f + zoomLift, 0.0f, 1.0f);
    // The day's own air: never silent, a little less under the wind of the heights.
    a.bed = 0.7f - 0.3f * high;
    // The far camera: the fight is a mass and the columns a tramp, coming up as the single sounds go down.
    const float mass = std::clamp((zoom_ - 150.0f) / 350.0f, 0.0f, 1.0f);
    a.battle = calm_ ? 0.0f : std::clamp(fighting_ / 25.0f, 0.0f, 1.0f) * (0.25f + 0.75f * mass);
    a.battlePan = fighting_ > 0.5f ? std::clamp(fightSide_ / fighting_, -1.0f, 1.0f) * 0.6f : 0.0f;
    fighting_ *= 0.55f;
    fightSide_ *= 0.55f;
    const float columns = std::clamp((zoom_ - 250.0f) / 250.0f, 0.0f, 1.0f);
    a.march = calm_ ? 0.0f : std::clamp(marching_ / 60.0f, 0.0f, 1.0f) * std::max(columns, speed_ >= 4.0f ? 0.6f : 0.0f);
    a.marchPan = marchSide_ * 0.5f;
    // Fire near the view.
    {
        float n = 0, fireSide = 0;
        for (const V3& f : fires_) {
            const V3 d = flat(f - at);
            if (d.length() > 70.0f + zoom_ * 0.5f) continue;
            n += 1.0f;
            fireSide += d.dot(listenRight_) > 0 ? 1.0f : -1.0f;
        }
        a.fire = std::clamp(n / 5.0f, 0.0f, 1.0f) * hear(0, zoom_, false).gain;
        a.firePan = n > 0 ? fireSide / n * 0.6f : 0.0f;
    }

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
        if (!u.present()) continue;  // (taken out of the world: not a body)
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
    // (How near any of his comes to any of ours. Beyond 300 m the drums are silent, so no nearer is looked for than
    // that; ours are put in 32 m cells so that a foe looks only at those within the nearest found so far.)
    {
        constexpr float kCell = 32.0f, kFar = 301.0f;
        std::vector<const sim::Unit*> ours;
        std::vector<V3> where;
        for (const sim::Unit& o : world.units()) {
            if (!o.alive() || o.owner != player_) continue;
            ours.push_back(&o);
            where.push_back(toV3(o.pos));
        }
        if (!ours.empty()) {
            const PointGrid grid(where, kCell);
            contactD2 = kFar * kFar;
            for (const sim::Unit& u : world.units()) {
                if (!u.alive() || u.owner == player_) continue;
                const V3 pu = toV3(u.pos);
                grid.near(pu.x, pu.y, std::sqrt(contactD2) + 1.0f, [&](uint32_t k) {
                    const V3 d = flat(toV3(u.pos - ours[k]->pos));
                    contactD2 = std::min(contactD2, d.dot(d));
                });
            }
        }
    }
    if (foeD2 < 1e11f) {
        const float contact = std::sqrt(contactD2);
        float drums = contact > 40.0f ? std::clamp(1.0f - (contact - 40.0f) / 260.0f, 0.0f, 1.0f) : 0.0f;
        // Only while he comes on: an enemy standing off, or running away, beats no drum.
        if (contact < contact_ - 0.05f) closingUntil_ = tick_ + static_cast<uint32_t>(4 * kTps);
        contact_ = contact;
        if (tick_ >= closingUntil_) drums = 0;
        if (enemyLand && contact > 25.0f) drums = std::max(drums, 0.3f);
        a.drums = calm_ ? 0.0f : drums;
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
