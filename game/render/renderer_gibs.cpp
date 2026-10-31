// Body parts as pieces of the soldier's own picture: a sheet's gib cells
// (sprite_layout.h; cut from the idle pictures by tools/sprites/gibs.py),
// drawn by the unit program (kUnitVs) as small billboards that spin while
// they fly and lie where they land. The sim's flying parts and remains carry
// the type of the soldier they came from (Projectile/Remain::source); the
// body blown apart beside a fresh one still decides which of his gobbets fly
// as his sword and shield.

#include <algorithm>
#include <cmath>

#include "renderer.h"

namespace illyria::game {

namespace {

constexpr float kTau = 6.2831853f;

float hashUnit(uint32_t v) {
    v ^= v >> 16;
    v *= 0x7feb352dU;
    v ^= v >> 15;
    v *= 0x846ca68bU;
    v ^= v >> 16;
    return static_cast<float>(v & 0xFFFF) / 65535.0f;
}

sprites::Gib gibOf(sim::RemainKind k) {
    switch (k) {
        case sim::RemainKind::Head: return sprites::Gib::Head;
        case sim::RemainKind::Torso: return sprites::Gib::Torso;
        case sim::RemainKind::Arm: return sprites::Gib::Arm;
        case sim::RemainKind::Leg: return sprites::Gib::Leg;
        default: return sprites::Gib::Chunk;  // gobbets, and the bones of the bloodless
    }
}

bool isPart(sim::RemainKind k) { return k != sim::RemainKind::None && k != sim::RemainKind::Arrow; }

// Long parts come to rest lying down (turned a quarter either way from how
// they were drawn standing); the rest as they fall.
bool liesDown(int cell) {
    const int g = (cell - sprites::gibSlot(sprites::Gib::Head, 0)) / sprites::kGibVariants;
    const auto gib = static_cast<sprites::Gib>(g);
    return gib == sprites::Gib::Torso || gib == sprites::Gib::Arm || gib == sprites::Gib::Leg || gib == sprites::Gib::Weapon;
}
float wrap(float a) { return a - kTau * std::floor((a + kTau / 2) / kTau); }
float restAngle(int cell, float a) {
    if (!liesDown(cell)) return wrap(a);
    return wrap(a) >= 0 ? kTau / 4 : -kTau / 4;
}

// The dead soldier a part came from: of its side, cut or blown apart, the
// nearest within `reach` metres (and dead no more than `within` ticks, if given); -1 if none.
int sourceOf(const Battle& battle, sim::PlayerId owner, const V3& at, float reach, uint32_t within) {
    const auto& units = battle.world().units();
    const uint32_t now = battle.world().tick();
    float best = reach * reach;
    int found = -1;
    for (size_t i = 0; i < units.size(); ++i) {
        const sim::Unit& u = units[i];
        if (u.alive() || u.owner != owner || !(u.gibbed || u.death.kind == sim::DeathKind::Severed)) continue;
        const uint32_t died = battle.deathTick(i);
        if (within > 0 && (died == UINT32_MAX || now - died > within)) continue;
        const V3 p = toV3(u.pos);
        const float d = (p.x - at.x) * (p.x - at.x) + (p.y - at.y) * (p.y - at.y);
        if (d < best) best = d, found = static_cast<int>(i);
    }
    return found;
}

}  // namespace

bool Renderer::hasGibCell(int type, int cell) const {
    if (cell < 0) return false;
    const auto has = [&](const Sheet& sh) {
        return sh.tex && cell < sh.cellCount && sh.table.size() >= static_cast<size_t>(cell + 1) * 4 && sh.table[static_cast<size_t>(cell) * 4 + 2] > 0;
    };
    const auto it = sheets_.find(type);
    if (it == sheets_.end() || !has(it->second)) return false;
    const auto cu = closeups_.find(type);
    return cu == closeups_.end() || !cu->second.tex || has(cu->second);
}

Renderer::GibLook Renderer::gibLookFor(const Battle& battle, const sim::Projectile& p) {
    GibLook look;
    look.kind = p.remain;
    look.owner = p.owner;
    look.pos = toV3(p.pos);
    const auto h = static_cast<uint32_t>(p.id);
    look.spin = (5.0f + 7.0f * hashUnit(h * 7u + 3u)) * (hashUnit(h * 11u + 4u) < 0.5f ? -1.0f : 1.0f);
    // Thrown up again from where it lay (a blast): as it was.
    const auto& remains = battle.world().remains();
    for (size_t i = 0; i < remainLooks_.size() && i < remains.size(); ++i) {
        GibLook& r = remainLooks_[i];
        if (r.type < 0 || remains[i].kind != sim::RemainKind::None || r.kind != p.remain || r.owner != p.owner) continue;
        if ((r.pos - look.pos).length() > 1.0f) continue;
        look.type = r.type, look.cell = r.cell, look.mirror = r.mirror, look.angle = r.angle;
        r = GibLook{};
        return look;
    }
    look.mirror = hashUnit(h * 3u + 1u) < 0.5f;
    look.angle = hashUnit(h * 5u + 2u) * kTau;
    // Fresh: a piece of the soldier the sim says it came from; parts from
    // before that was kept, of the body cut or blown apart where it starts
    // (within a few metres; in the last two seconds).
    const int src = sourceOf(battle, p.owner, look.pos, 4.0f, 2 * sim::kTicksPerSecond);
    if (p.source == sim::kNoSource && src < 0) return look;
    const int variant = hashUnit(h) < 0.5f ? 0 : 1;
    sprites::Gib g = gibOf(p.remain);
    look.type = p.source != sim::kNoSource ? p.source : battle.world().units()[static_cast<size_t>(src)].stats.type;
    // His weapon and shield fly with the first two gobbets, if his sheet has them.
    const sim::Unit* body = src >= 0 ? &battle.world().units()[static_cast<size_t>(src)] : nullptr;
    if (g == sprites::Gib::Chunk && body && body->gibbed && body->stats.type == look.type) {
        const sim::Unit& u = *body;
        const int n = gibChunks_[u.id]++;
        if (n == 0 && hasGibCell(look.type, sprites::gibSlot(sprites::Gib::Weapon, variant))) g = sprites::Gib::Weapon;
        if (n == 1 && hasGibCell(look.type, sprites::gibSlot(sprites::Gib::Shield, variant))) g = sprites::Gib::Shield;
    }
    look.cell = sprites::gibSlot(g, variant);
    return look;
}

void Renderer::trackDebris(const Battle& battle, float dt) {
    for (auto& [id, look] : debrisLooks_) look.seen = false;
    for (const sim::Projectile& p : battle.world().projectiles()) {
        if (p.kind != sim::ProjectileKind::Debris || !isPart(p.remain)) continue;
        auto it = debrisLooks_.find(p.id);
        if (it == debrisLooks_.end()) it = debrisLooks_.emplace(p.id, gibLookFor(battle, p)).first;
        GibLook& g = it->second;
        g.seen = true;
        g.pos = toV3(p.pos);
        // Spinning as it flies, slower as it skids, settling as it stops.
        const float speed = toV3(p.vel).length();
        g.angle = wrap(g.angle + g.spin * std::min(1.0f, speed / 4.0f) * dt);
        if (speed < 3.0f && liesDown(g.cell)) {
            const float to = restAngle(g.cell, g.angle), step = 6.0f * dt;
            g.angle += std::clamp(wrap(to - g.angle), -step, step);
        }
    }
    landed_.clear();
    for (auto it = debrisLooks_.begin(); it != debrisLooks_.end();) {
        if (it->second.seen) {
            ++it;
            continue;
        }
        if (it->second.type >= 0) landed_.push_back(it->second);
        it = debrisLooks_.erase(it);
    }
}

Renderer::GibLook Renderer::restingLook(const Battle& battle, const sim::Remain& r, uint32_t slot) {
    if (!isPart(r.kind)) return GibLook{};
    const V3 at = toV3(r.pos);
    // The flying part that stopped here.
    auto best = landed_.end();
    float bestD = 9.0f;
    for (auto it = landed_.begin(); it != landed_.end(); ++it) {
        if (it->kind != r.kind || it->owner != r.owner) continue;
        const float d = (it->pos - at).length();
        if (d < bestD) bestD = d, best = it;
    }
    GibLook look;
    if (best != landed_.end()) {
        look = *best;
        landed_.erase(best);
    } else {
        // Not seen flying (the battle ran on unseen): the soldier it came from,
        // else (parts from before that was kept) the nearest body cut or blown apart.
        const int src = r.source != sim::kNoSource ? -1 : sourceOf(battle, r.owner, at, 25.0f, 0);
        if (r.source == sim::kNoSource && src < 0) return GibLook{};
        const uint32_t h = slot * 2654435761u + r.tick;
        look.type = r.source != sim::kNoSource ? r.source : battle.world().units()[static_cast<size_t>(src)].stats.type;
        look.cell = sprites::gibSlot(gibOf(r.kind), hashUnit(h) < 0.5f ? 0 : 1);
        look.mirror = hashUnit(h * 3u + 1u) < 0.5f;
        look.angle = hashUnit(h * 5u + 2u) * kTau;
        look.kind = r.kind, look.owner = r.owner;
    }
    look.pos = at;
    look.angle = restAngle(look.cell, look.angle);
    return look;
}

bool Renderer::pushGib(std::map<int, std::vector<float>>& groups, const GibLook& look, const V3& pos) {
    if (look.type < 0 || !hasGibCell(look.type, look.cell)) return false;
    std::vector<float>& g = groups[look.type * 4 + (look.owner & 3)];  // kSides groups a type (renderer.cpp)
    g.insert(g.end(), {pos.x, pos.y, pos.z, look.angle, -static_cast<float>(look.cell + 1), 0.0f, look.mirror ? 1.0f : 0.0f});
    return true;
}

}  // namespace illyria::game
