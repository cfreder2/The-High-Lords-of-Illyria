// The sandbox (mode_sandbox.h): set squads down, let them fight, watch.
#include "illyria/game/mode_sandbox.h"

#include <cstdio>
#include <cstdlib>

#include "illyria/game/battle.h"

namespace illyria::game {

SandboxMode& SandboxMode::start(Battle& b, const SandboxOptions& o) {
    auto owned = std::make_unique<SandboxMode>(o);
    SandboxMode& m = *owned;
    b.setMode(std::move(owned));
    const V3 mid{Battle::kWorldSize / 2, Battle::kWorldSize / 2, 0};
    b.setZone(mid, static_cast<float>(o.radiusM), kZonePctPerSecond);
    b.setLocalSide(0);
    m.spawn(b, sim::UnitType::HeavyInfantry, 0, 12, mid + V3{-18, 0, 0});
    m.spawn(b, sim::UnitType::HeavyInfantry, 1, 12, mid + V3{18, 0, 0});
    return m;
}

int SandboxMode::spawn(Battle& b, sim::UnitType type, sim::PlayerId side, int count, V3 at) {
    // Facing the nearest squad of another side, else the middle of the field.
    V3 to = b.zoneCentre();
    float best = 1e30f;
    for (const Squad& q : b.squads()) {
        if (b.aliveCount(q) == 0 || q.owner == side) continue;
        const V3 c = b.squadCentre(q);
        const float d = (c.x - at.x) * (c.x - at.x) + (c.y - at.y) * (c.y - at.y);
        if (d < best) {
            best = d;
            to = c;
        }
    }
    V3 facing{to.x - at.x, to.y - at.y, 0};
    if (facing.length() < 0.01f) facing = {1, 0, 0};
    char name[48];
    std::snprintf(name, sizeof name, "%s %d", sim::unitTypeName(type), static_cast<int>(b.squads().size()) + 1);
    const int q = b.addSquad(name, side, type, count, at, facing);
    if (tuned_.count(static_cast<int>(type))) b.mutableWorld().retune(static_cast<uint8_t>(type), tuned_.at(static_cast<int>(type)));
    return q;
}

namespace {

// A number in a flat JSON object, or the fallback.
double numberOf(const std::string& json, const char* key, double fallback) {
    const std::string k = std::string("\"") + key + "\":";
    const size_t at = json.find(k);
    return at == std::string::npos ? fallback : std::strtod(json.c_str() + at + k.size(), nullptr);
}

double ticksToSeconds(int32_t t) { return static_cast<double>(t) / sim::kTicksPerSecond; }
int32_t secondsToTicks(double s) { return std::max<int32_t>(1, static_cast<int32_t>(s * sim::kTicksPerSecond + 0.5)); }
sim::Fixed fixedOf(double v) { return sim::Fixed::fromRatio(static_cast<int64_t>(v * 1000 + (v < 0 ? -0.5 : 0.5)), 1000); }

std::string numbers(const sim::UnitStats& s) {
    char buf[400];
    std::snprintf(buf, sizeof buf,
                  "\"hp\":%d,\"speed\":%.2f,\"melee\":%d,\"reach\":%.2f,\"swing\":%.2f,\"arrow\":%d,\"reload\":%.2f,"
                  "\"range\":%.1f,\"block\":%.0f,\"aggro\":%.1f",
                  s.maxHp, s.moveSpeed.toDouble(), s.melee.damage, s.melee.reach.toDouble(), ticksToSeconds(s.melee.cooldownTicks),
                  s.ranged.damage, ticksToSeconds(s.ranged.reloadTicks), s.ranged.maxRange.toDouble(),
                  s.shield.blockChance.toDouble() * 100, s.aggroRange.toDouble());
    return buf;
}

}  // namespace

sim::UnitStats SandboxMode::statsOf(sim::UnitType type) const {
    const auto it = tuned_.find(static_cast<int>(type));
    return it != tuned_.end() ? it->second : sim::unitStats(type);
}

std::string SandboxMode::statsJson(sim::UnitType type) const {
    return "{" + numbers(statsOf(type)) + ",\"sheet\":{" + numbers(sim::unitStats(type)) + "}}";
}

void SandboxMode::tune(Battle& b, sim::UnitType type, const std::string& json) {
    sim::UnitStats s = json.find(':') == std::string::npos ? sim::unitStats(type) : statsOf(type);
    s.maxHp = std::max(1, static_cast<int>(numberOf(json, "hp", s.maxHp)));
    s.moveSpeed = fixedOf(std::max(0.0, numberOf(json, "speed", s.moveSpeed.toDouble())));
    s.melee.damage = std::max(0, static_cast<int>(numberOf(json, "melee", s.melee.damage)));
    s.melee.reach = fixedOf(std::max(0.0, numberOf(json, "reach", s.melee.reach.toDouble())));
    s.melee.cooldownTicks = secondsToTicks(numberOf(json, "swing", ticksToSeconds(s.melee.cooldownTicks)));
    s.ranged.damage = std::max(0, static_cast<int>(numberOf(json, "arrow", s.ranged.damage)));
    if (s.ranged.reloadTicks > 0) s.ranged.reloadTicks = secondsToTicks(numberOf(json, "reload", ticksToSeconds(s.ranged.reloadTicks)));
    s.ranged.maxRange = fixedOf(std::max(0.0, numberOf(json, "range", s.ranged.maxRange.toDouble())));
    s.shield.blockChance = fixedOf(std::clamp(numberOf(json, "block", s.shield.blockChance.toDouble() * 100), 0.0, 100.0) / 100);
    s.aggroRange = fixedOf(std::max(0.0, numberOf(json, "aggro", s.aggroRange.toDouble())));
    tuned_[static_cast<int>(type)] = s;
    b.mutableWorld().retune(static_cast<uint8_t>(type), s);
}

void SandboxMode::beforeStep(Battle& b, std::vector<sim::Command>&) {
    // The player's own orders are already in the list; the sides told to
    // fight think every couple of seconds, as the computer's seats do.
    if (b.world().tick() % SeatedMode::kThinkTicks != 0) return;
    for (size_t q = 0; q < b.squads().size(); ++q) {
        const Squad& s = b.squads()[q];
        if (!fights_[s.owner & 3] || b.aliveCount(s) == 0) continue;
        const V3 c = b.squadCentre(s);
        const sim::EntityId foe = b.nearestFoe(s.owner, c, 2 * static_cast<float>(opt_.radiusM));
        const V3 goal = foe != sim::kNoEntity ? toV3(b.world().findUnit(foe)->pos) : b.zoneCentre();
        b.aiFight(q, goal, c, 2 * static_cast<float>(opt_.radiusM));
    }
}

std::string SandboxMode::hudJson(const Battle& b) const {
    std::string s = "{\"mode\":\"sandbox\",\"place\":\"" + opt_.place + "\",\"side\":" + std::to_string(b.localSide()) +
                    ",\"radius\":" + std::to_string(opt_.radiusM) + ",\"fights\":[";
    for (int i = 0; i < kSides; ++i) s += (i ? "," : "") + std::string(fights_[static_cast<size_t>(i)] ? "1" : "0");
    s += "],\"sides\":[";
    // Men standing on each side.
    std::array<int, kSides> men{};
    for (const Squad& q : b.squads()) men[q.owner & 3] += b.aliveCount(q);
    for (int i = 0; i < kSides; ++i) s += (i ? "," : "") + std::to_string(men[static_cast<size_t>(i)]);
    s += "],\"paused\":" + std::string(b.paused() ? "true" : "false") + ",\"speed\":" + std::to_string(b.speed()) +
         ",\"slow\":" + std::to_string(b.slowMotion()) + "}";
    return s;
}

}  // namespace illyria::game
