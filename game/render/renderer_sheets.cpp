// Sprite sheets pre-rendered offline (tools/blender: modelled, rigged and
// animated figures rendered with Cycles; or the artist's pictures packed by
// tools/sprites/import.py) in place of the runtime bake. A sheet is the same
// thing either way: one RGBA texture whose alpha codes opaque, team-coloured
// and outline pixels (kResolveFs), and the table of where each cell lies and
// what it covers in metres (kUnitVs; layout in sprite_layout.h).
//
// Close-up sheets (closeup.h): a type's sheet at twice the pixels a metre,
// held only while the camera is near that type's soldiers.

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "closeup.h"
#include "renderer.h"
#include "unit_deeds.h"

namespace illyria::game {

namespace {

// A sheet at half its size, keeping the alpha codes whole: each 2x2 block
// becomes the figure's colour where any of it is figure (the side's colour if
// most of that is), else outline, else empty. The cells' pixel rectangles
// halve with it; their extents in metres stay.
void halveSheet(std::vector<uint8_t>& px, int& width, int& height, std::vector<float>& table, int cells) {
    const int w2 = (width + 1) / 2, h2 = (height + 1) / 2;
    std::vector<uint8_t> out(static_cast<size_t>(w2) * h2 * 4, 0);
    for (int y = 0; y < h2; ++y)
        for (int x = 0; x < w2; ++x) {
            int rgb[3] = {0, 0, 0}, solid = 0, team = 0, outline = -1;
            for (int dy = 0; dy < 2; ++dy)
                for (int dx = 0; dx < 2; ++dx) {
                    const int sx = std::min(width - 1, 2 * x + dx), sy = std::min(height - 1, 2 * y + dy);
                    const uint8_t* p = &px[(static_cast<size_t>(sy) * width + sx) * 4];
                    if (p[3] > 170) {
                        for (int k = 0; k < 3; ++k) rgb[k] += p[k];
                        ++solid;
                        if (p[3] < 223) ++team;
                    } else if (p[3] > 100) {
                        outline = static_cast<int>((static_cast<size_t>(sy) * width + sx) * 4);
                    }
                }
            uint8_t* o = &out[(static_cast<size_t>(y) * w2 + x) * 4];
            if (solid) {
                for (int k = 0; k < 3; ++k) o[k] = static_cast<uint8_t>(rgb[k] / solid);
                o[3] = 2 * team > solid ? 191 : 255;
            } else if (outline >= 0) {
                std::copy(&px[static_cast<size_t>(outline)], &px[static_cast<size_t>(outline)] + 4, o);
            }
        }
    for (int c = 0; c < cells; ++c) {
        float* r = &table[static_cast<size_t>(c) * 4];
        if (r[2] <= 0) continue;  // a gib part the sheet does not have
        const float x0 = std::floor(r[0] / 2), y0 = std::floor(r[1] / 2);
        const float x1 = std::ceil((r[0] + r[2]) / 2), y1 = std::ceil((r[1] + r[3]) / 2);
        r[0] = x0, r[1] = y0, r[2] = std::max(1.0f, x1 - x0), r[3] = std::max(1.0f, y1 - y0);
    }
    px.swap(out);
    width = w2, height = h2;
}

// Pixels a metre of a sheet: its first cell's width in pixels over its width in metres.
float ppmOf(const std::vector<float>& table, int cells) {
    return table[(static_cast<size_t>(cells)) * 4 + 2] > 0 ? table[2] / table[static_cast<size_t>(cells) * 4 + 2] : figures::kPixelsPerMetre;
}

// How a soldier died, once the sim records it (the death matrix, sim::Unit::death:
// its kind, the killing blow's damage type and travel). Until a build's Unit has
// that, every death plays the old fall.
template <class U>
sprites::Clip deathClipOf(const U& u) {
    if constexpr (requires { u.death.kind; u.death.cause; u.death.dir; }) {
        const float along = static_cast<float>((u.death.dir.x * u.facing.x + u.death.dir.y * u.facing.y).toDouble());
        return sprites::deathClip(static_cast<sprites::DeathKind>(u.death.kind), static_cast<sprites::DamageType>(u.death.cause), along);
    } else {
        return sprites::Clip::DieBack;
    }
}

}  // namespace

bool Renderer::loadSheet(int type, const uint8_t* rgba, int width, int height, const float* cells, int cellCount, int framesPerView,
                         bool closeup, const sprites::SheetClips& clips) {
    if (type < 0 || type >= sim::kUnitTypeCount || !rgba || !cells || width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    if (!clips.empty()) {
        // Its own clips (sprite_clips.h): framesPerView frames a view, as they list, then any gib parts.
        const int views = sprites::kViews * framesPerView;
        if (framesPerView < 1 || views > 4000 || !clips.valid(framesPerView) ||
            !(cellCount == views || cellCount == views + sprites::kGibCount * sprites::kGibVariants))
            return false;
    } else {
        // The old layout (130 cells) or the new (sprite_layout.h: the views' cells, then any gib parts).
        const bool legacy = framesPerView == sprites::kLegacyFramesPerView && cellCount == sprites::kLegacyCells;
        if (!legacy && !(framesPerView == sprites::kFramesPerView && (cellCount == sprites::kViews * sprites::kFramesPerView || cellCount == sprites::kCells)))
            return false;
    }
    const int viewCells = sprites::kViews * framesPerView;
    for (int c = 0; c < cellCount; ++c) {
        const float* r = &cells[c * 8];
        if (c >= viewCells && r[2] == 0 && r[3] == 0) continue;  // no such gib part
        if (r[0] < 0 || r[1] < 0 || r[2] < 1 || r[3] < 1 || r[0] + r[2] > width || r[1] + r[3] > height || r[6] <= 0 || r[7] <= 0) return false;
    }
    Sheet& sh = closeup ? closeups_[type] : sheets_[type];
    if (sh.tex) dropSheet(sh, closeup);
    sh.lift = 0.5f + static_cast<float>(sim::unitStats(static_cast<sim::UnitType>(type)).radius.toDouble()) * 1.2f;
    sh.prerendered = true;
    sh.framesPerView = framesPerView;
    sh.cellCount = cellCount;
    sh.clips = clips;
    // GL keeps the bottom row first.
    std::vector<uint8_t> flipped(static_cast<size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y)
        std::copy(rgba + static_cast<size_t>(height - 1 - y) * width * 4, rgba + static_cast<size_t>(height - y) * width * 4, flipped.begin() + static_cast<size_t>(y) * width * 4);
    std::vector<float> table(static_cast<size_t>(cellCount) * 2 * 4);
    for (int c = 0; c < cellCount; ++c)
        for (int k = 0; k < 4; ++k) {
            table[static_cast<size_t>(c) * 4 + k] = cells[c * 8 + k];
            table[(static_cast<size_t>(cellCount) + c) * 4 + k] = cells[c * 8 + 4 + k];
        }
    sh.fullTexels = static_cast<double>(width) * height;
    // The same texture budget as the bake's (the baked stand-ins are replaced
    // as the sheets come, so only these count): in a battle of many kinds (the
    // Ford has every one) the sheets that would not fit come at half size, as
    // the bake would have drawn them coarser. Never less than half: a little
    // over the budget is better than a blur. Close-up sheets have their own
    // budget (closeup::kBudget) and are never halved.
    int w = width, h = height;
    const bool halved = !closeup && prerenderedTexels_ + static_cast<double>(w) * h > kSheetBudget && w > 256 && h > 64;
    if (halved) halveSheet(flipped, w, h, table, cellCount);
    sh.texW = w, sh.texH = h;
    sh.ppm = ppmOf(table, cellCount);
    uploadSheet(sh, flipped.data(), table.data());
    if (closeup) {
        sheetTexels_ -= static_cast<double>(w) * h;  // not the bake's to share
    } else {
        prerenderedTexels_ += static_cast<double>(w) * h;
    }
    std::printf("sprite sheet %s: %s, %dx%d, %d frames a view%s, %.0f px/m%s\n", sim::unitTypeName(static_cast<sim::UnitType>(type)),
                closeup ? "close-up" : "pre-rendered", w, h, framesPerView, clips.empty() ? "" : " in its own clips", sh.ppm,
                halved ? " (halved to fit)" : "");
    return true;
}

void Renderer::dropSheet(Sheet& sh, bool closeup) {
    if (!sh.tex) return;
    glDeleteTextures(1, &sh.tex);
    glDeleteTextures(1, &sh.cells);
    sh.tex = sh.cells = 0;
    sh.clips = {};
    if (!closeup) {
        sheetTexels_ -= static_cast<double>(sh.texW) * sh.texH;
        if (sh.prerendered) prerenderedTexels_ -= static_cast<double>(sh.texW) * sh.texH;
    }
}

void Renderer::uploadSheet(Sheet& sh, const uint8_t* rgba, const float* table) {
    const int cells = sh.cellCount;
    glGenTextures(1, &sh.tex);
    glBindTexture(GL_TEXTURE_2D, sh.tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, sh.texW, sh.texH, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glGenerateMipmap(GL_TEXTURE_2D);
    // As baked sheets: crisp close up, averaged far off.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenTextures(1, &sh.cells);
    glBindTexture(GL_TEXTURE_2D, sh.cells);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, cells, 2, 0, GL_RGBA, GL_FLOAT, table);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
    sheetTexels_ += static_cast<double>(sh.texW) * sh.texH;
    sh.table.assign(table, table + static_cast<size_t>(cells) * 8);
}

const Renderer::Sheet& Renderer::sheetFor(int type) {
    const auto c = closeups_.find(type);
    if (c != closeups_.end() && c->second.tex) return c->second;
    return sheets_[type];
}

int Renderer::clipFrame(int type, sprites::Clip c, int i) {
    const auto it = sheets_.find(type);
    // The table drawn with is the close-up one when held; both are checked so
    // a frame is never past the end of either.
    int frames = it != sheets_.end() ? it->second.framesPerView : sprites::kLegacyFramesPerView;
    const auto cu = closeups_.find(type);
    if (cu != closeups_.end() && cu->second.tex) frames = std::min(frames, cu->second.framesPerView);
    return sprites::frameIndex(c, i, frames);
}

const sprites::SheetClips* Renderer::clipsFor(int type) const {
    // The sheet drawn with (sheetFor): the close-up one when held.
    const Sheet* sh = nullptr;
    if (const auto c = closeups_.find(type); c != closeups_.end() && c->second.tex) sh = &c->second;
    else if (const auto n = sheets_.find(type); n != sheets_.end() && n->second.tex) sh = &n->second;
    return sh && !sh->clips.empty() ? &sh->clips : nullptr;
}

std::vector<std::string> Renderer::clipNames(int type) const {
    std::vector<std::string> names;
    if (const sprites::SheetClips* clips = clipsFor(type)) {
        for (const sprites::SheetClip& c : clips->clips) names.push_back(c.name);
    } else {
        for (const char* n : sprites::kClipNames) names.emplace_back(n);
    }
    return names;
}

void Renderer::playClip(const std::vector<sim::EntityId>& ids, int clip, bool loop) {
    playClip(ids, clip < 0 || clip >= sprites::kClipCount ? std::string() : std::string(sprites::kClipNames[clip]), loop);
}

void Renderer::playClip(const std::vector<sim::EntityId>& ids, const std::string& clip, bool loop) {
    for (sim::EntityId id : ids) {
        if (clip.empty()) played_.erase(id);
        else played_[id] = {clip, loop, gameSeconds_};
    }
}

namespace {

// How far a dying man is down, 0 standing .. 1 lying, `t` through his fall:
// a burning man stays on his feet most of his clip.
float downAt(sprites::Clip clip, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return clip == sprites::Clip::DieBurn ? t * t : t;
}

// The fixed layout's clip played for a name (sprite_clips.h fallbackOf: a
// celebration the idle, a run the walk, a throw the attack), or -1.
int fixedClip(std::string_view name) {
    for (int k = 0; k < 8 && !name.empty(); ++k, name = sprites::fallbackOf(name))
        for (int c = 0; c < sprites::kClipCount; ++c)
            if (name == sprites::kClipNames[c]) return c;
    return -1;
}

// Seconds a fall of a sheet's own clips takes when it does not say: the fixed layout's.
float fallSeconds(const sprites::SheetClip& c) {
    return c.length(c.name == "die_burn" ? sprites::kBurnSeconds : sprites::kDieSeconds);
}

}  // namespace

std::string Renderer::clipPlayed(int type, std::string_view name) const {
    if (const sprites::SheetClips* clips = clipsFor(type)) {
        const sprites::SheetClip* c = clips->play(name);
        return c ? c->name : std::string();
    }
    const int c = fixedClip(name);
    return c < 0 ? std::string() : std::string(sprites::kClipNames[c]);
}

int Renderer::playedFrame(int type, sim::EntityId id, float gameSeconds, float* down) {
    const auto it = played_.find(id);
    if (it == played_.end()) return -1;
    const Played& p = it->second;
    if (const sprites::SheetClips* clips = clipsFor(type)) {
        // A sheet of its own clips: the one of that name (or what stands in for it), as long as it says.
        const sprites::SheetClip* c = clips->play(p.clip);
        if (!c) {
            played_.erase(it);
            return -1;
        }
        const bool dies = c->name.rfind("die_", 0) == 0;
        float len = c->length(2.2f);
        if (dies) len = fallSeconds(*c);
        else if (c->name == "attack" || c->name == "throw" || c->name == "pickup" || c->name == "slash")
            len = c->length(figures::attackSeconds(static_cast<sim::UnitType>(type)));
        else if (c->name == "walk" || c->name == "run") len = c->length(sprites::kMoveLoopSeconds);
        else if (c->name == "flinch") len = c->length(figures::kFlinchSeconds);
        const float t = std::max(0.0f, gameSeconds - p.start);
        const float through = (p.loop ? t - len * std::floor(t / len) : t) / len;
        if (down) *down = dies ? downAt(c->name == "die_burn" ? sprites::Clip::DieBurn : sprites::Clip::DieBack, through) : 0.0f;
        if (!p.loop && t >= len && !dies) {
            played_.erase(it);
            return -1;
        }
        return c->first + c->frameAt(t, len, p.loop);
    }
    const int fixed = fixedClip(p.clip);
    if (fixed < 0) {
        played_.erase(it);
        return -1;
    }
    const auto clip = static_cast<sprites::Clip>(fixed);
    // As long as the game plays it: an idle breath, a stride, the type's
    // stroke, a flinch, a fall.
    float seconds = 2.2f;
    switch (clip) {
        case sprites::Clip::Idle: seconds = 2.2f; break;
        case sprites::Clip::Walk: seconds = 1.0f; break;
        case sprites::Clip::Attack: seconds = figures::attackSeconds(static_cast<sim::UnitType>(type)); break;
        case sprites::Clip::Flinch: seconds = figures::kFlinchSeconds; break;
        case sprites::Clip::DieBurn: seconds = sprites::kBurnSeconds; break;
        default: seconds = sprites::kDieSeconds; break;
    }
    const int n = sprites::kClipFrames[fixed];
    const float t = std::max(0.0f, gameSeconds - p.start) / seconds;
    int i = static_cast<int>(t * static_cast<float>(n));
    const bool dies = clip >= sprites::Clip::DieBack;
    if (down) *down = dies ? downAt(clip, p.loop ? t - std::floor(t) : t) : 0.0f;
    if (p.loop) {
        i %= n;
    } else if (i >= n) {
        if (!dies) {
            played_.erase(it);
            return -1;
        }
        i = n - 1;
    }
    return clipFrame(type, clip, i);
}

int Renderer::deathFrame(int type, const sim::Unit& u, float secondsDead, float* down) {
    const sprites::Clip clip = deathClipOf(u);
    if (const sprites::SheetClips* clips = clipsFor(type)) {
        // A sheet of its own clips: the fall by name (or the one standing in for it), as long as it says.
        if (const sprites::SheetClip* c = clips->play(sprites::kClipNames[static_cast<int>(clip)])) {
            const float seconds = fallSeconds(*c);
            const float t = u.airborne ? seconds / 3 : secondsDead;
            if (down) *down = u.airborne ? 0.0f : downAt(clip, secondsDead / seconds);
            return c->first + c->frameAt(t, seconds, false);
        }
    }
    const float seconds = clip == sprites::Clip::DieBurn ? sprites::kBurnSeconds : sprites::kDieSeconds;
    const int n = sprites::kClipFrames[static_cast<int>(clip)];
    // Thrown through the air by a blow, he stays mid-fall until he lands.
    const int i = u.airborne ? 2 : static_cast<int>(secondsDead / seconds * static_cast<float>(n));
    if (down) *down = u.airborne ? 0.0f : downAt(clip, secondsDead / seconds);
    return clipFrame(type, clip, i);
}

int Renderer::listedFrame(const Battle& battle, size_t i, const sprites::SheetClips& clips, float gameSeconds, float moved, float dt,
                          uint32_t victors) {
    // How often (ticks) whether a missile soldier will loose is worked out again while he draws.
    constexpr uint32_t kEngagedTicks = 6;
    const sim::Unit& u = battle.world().units()[i];
    const auto type = static_cast<sim::UnitType>(u.stats.type);
    sprites::ClipMemory& m = clipMemory_[i];
    const float tps = static_cast<float>(sim::kTicksPerSecond);
    const uint32_t tick = battle.world().tick();
    const float alpha = battle.alpha();
    const uint32_t attackTick = battle.attackTick(i);
    const auto since = [&](uint32_t t) { return t == UINT32_MAX ? -1.0f : gameSeconds - static_cast<float>(t) / tps; };
    sprites::Deeds d;
    d.now = gameSeconds;
    d.sinceAttack = since(attackTick);
    d.sinceHit = since(battle.hitTick(i));
    d.moving = battle.unitMoving(i);
    d.inAir = u.inAir();
    d.stepMetres = moved;
    d.celebrating = u.owner < 32 && ((victors >> u.owner) & 1u) != 0;
    d.idlePhase = static_cast<float>(u.id % 13) * 0.37f;  // as the fixed layout's idle
    d.attackSeconds = figures::attackSeconds(type);
    d.flinchSeconds = figures::kFlinchSeconds;
    d.stride = figures::stride(type);
    // His pace over the ground, smoothed, for walking or running.
    if (dt > 0) m.speed = sprites::easeSpeed(m.speed, moved / dt, dt);
    if (u.stats.scavenge.sight > sim::Fixed::zero()) {
        // A scavenger (sim::Unit::scav, read-only): what he took up and threw since the last look.
        const sim::Scavenger& s = u.scav;
        const bool striking = s.holding() && s.mind == sim::Scavenger::Mind::Strike && s.timer > 0;
        sprites::observeScavenger(m, s.holding(), striking, s.mind == sim::Scavenger::Mind::Retreat, s.timer, tick, attackTick, tps);
        d.sincePickup = m.pickupAt >= 0 ? gameSeconds - m.pickupAt : -1.0f;
        // Drawn back: it leaves his hand in the tick his wind-up runs out (World::strike).
        d.toThrow = striking ? sprites::secondsToRelease(s.timer, alpha, tps) : -1.0f;
        d.sinceThrow = m.throwAt >= 0 ? gameSeconds - m.throwAt : -1.0f;
        if (attackTick != UINT32_MAX && attackTick == m.claimed) d.attackFrom = m.claimedBy;
    } else if (const sprites::SheetClip* a = clips.play("attack");
               a && a->release >= 0 && u.stats.ranged.muzzleSpeed > sim::Fixed::zero()) {
        // Missile troops: the draw in the last `release` seconds before his shot,
        // which comes in the tick both his reload and his draw at this mark are
        // done (sim::World::readyToLoose); a mark he did not hold last tick is drawn afresh.
        const int32_t draw = u.stats.ranged.drawTicks;
        const int32_t drawLeft = draw == 0 ? 0 : u.drawTick + 1 == tick ? draw - u.drawn : draw;
        const float toRelease = sprites::secondsToRelease(std::max(u.reloadRemaining, drawLeft), alpha, tps);
        if (toRelease >= 0 && toRelease <= a->release) {
            if (m.engagedTick == UINT32_MAX || tick < m.engagedTick || tick - m.engagedTick >= kEngagedTicks) {
                m.engaged = deeds::willLoose(battle, i);
                m.engagedTick = tick;
            }
            if (m.engaged) d.toRelease = toRelease;
        } else {
            m.engagedTick = UINT32_MAX;  // worked out afresh at his next draw
        }
    }
    return sprites::chooseFrame(clips, d, m).frame;
}

float Renderer::deathSeconds(int type, const sim::Unit& u) const {
    if (const sprites::SheetClips* clips = clipsFor(type))
        if (const sprites::SheetClip* c = clips->play(sprites::kClipNames[static_cast<int>(deathClipOf(u))])) return fallSeconds(*c);
    return figures::kDieSeconds;
}

void Renderer::listCloseup(int type, int width, int height, float ppm) {
    if (type < 0 || type >= sim::kUnitTypeCount || width <= 0 || height <= 0 || ppm <= 0) return;
    closeupListed_[type] = {static_cast<double>(width) * height, ppm};
}

void Renderer::updateCloseups(const Battle& battle, const Camera& camera) {
    if (!showUnits_) return;
    if (!closeupsOn_) {
        for (auto& [type, sh] : closeups_) dropSheet(sh, true);
        closeupsRequested_.clear();
        closeupMinDistance_ = Camera::kMinDistance;
        return;
    }
    const float screen = closeupScreen_ > 0 ? closeupScreen_ : camera.height();
    const V3 eye = camera.eye();
    // The soldiers to size the sheets for: a battle's, or the campaign's near the camera (renderer_map.cpp).
    std::vector<std::pair<int, V3>> seen = men_.seen;  // (the sleepers who were sprites last frame, with the men of the world's)
    {
        const auto& units = battle.world().units();
        for (size_t i = 0; i < units.size(); ++i)
            if (units[i].present() && (units[i].alive() || !units[i].gibbed)) seen.push_back({units[i].stats.type, battle.unitPosition(i)});
    }
    // Each type's soldier nearest the camera; and, for the zoom limit, how far
    // the camera must stay so no soldier on screen is drawn past
    // closeup::kMaxMagnification of the sharpest sheet his type can have.
    std::map<int, float> nearest;
    float limit = Camera::kMinDistance;
    for (const auto& [type, p] : seen) {
        const float d = (p - eye).length();
        auto [it, fresh] = nearest.emplace(type, d);
        if (!fresh) it->second = std::min(it->second, d);
        float sx = 0, sy = 0;
        if (!camera.project(p + V3{0, 0, 1}, sx, sy) || sx < 0 || sy < 0 || sx > camera.width() || sy > camera.height()) continue;
        const auto sh = sheets_.find(type);
        if (sh == sheets_.end() || !sh->second.tex) continue;
        const auto listed = closeupListed_.find(type);
        const float best = listed != closeupListed_.end() ? std::max(listed->second.ppm, sh->second.ppm) : sh->second.ppm;
        limit = std::max(limit, closeup::minDistanceFor(camera.distance(), closeup::pxPerMetre(screen, Camera::kFovY, d), best));
    }
    closeupMinDistance_ = limit;
    std::vector<closeup::Need> needs;
    for (const auto& [type, d] : nearest) {
        const auto sh = sheets_.find(type);
        if (sh == sheets_.end() || !sh->second.tex) continue;
        const auto listed = closeupListed_.find(type);
        const auto held = closeups_.find(type);
        closeup::Need n;
        n.type = type;
        n.pxPerMetre = closeup::pxPerMetre(screen, Camera::kFovY, d);
        n.sheetPpm = sh->second.ppm;
        n.available = listed != closeupListed_.end();
        n.loaded = held != closeups_.end() && held->second.tex;
        n.texels = n.available ? listed->second.texels : 0;
        needs.push_back(n);
    }
    const std::vector<int> keep = closeup::choose(needs);
    for (auto& [type, sh] : closeups_)
        if (sh.tex && std::find(keep.begin(), keep.end(), type) == keep.end()) {
            dropSheet(sh, true);
            closeupsRequested_.erase(type);
            std::printf("sprite sheet %s: close-up dropped\n", sim::unitTypeName(static_cast<sim::UnitType>(type)));
        }
    for (int type : keep)
        if (!closeups_[type].tex && closeupRequest_ && closeupsRequested_.insert(type).second) closeupRequest_(type);
}

std::string Renderer::closeupReport() const {
    std::string out;
    for (const auto& [type, sh] : closeups_) {
        if (!sh.tex) continue;
        if (!out.empty()) out += ',';
        out += sim::unitTypeName(static_cast<sim::UnitType>(type));
        out += '=' + std::to_string(sh.texW) + 'x' + std::to_string(sh.texH);
    }
    return out;
}

std::string Renderer::sheetReport() const {
    std::string out;
    for (const auto& [type, sh] : sheets_) {
        if (!sh.tex) continue;
        if (!out.empty()) out += ',';
        out += sim::unitTypeName(static_cast<sim::UnitType>(type));
        out += sh.prerendered ? "=pre" : "=baked";
    }
    return out;
}

int Renderer::sheetSource(int type) const {
    const auto it = sheets_.find(type);
    if (it == sheets_.end() || !it->second.tex) return -1;
    return it->second.prerendered ? 1 : 0;
}

bool Renderer::readSheet(int type, std::vector<uint8_t>& rgba, int& width, int& height, std::vector<float>& table) {
    if (type < 0 || type >= sim::kUnitTypeCount) return false;
    Sheet& sh = sheets_[type];
    if (!sh.tex) bakeSheet(sh, static_cast<sim::UnitType>(type), figures::plan(static_cast<sim::UnitType>(type)), 1.0f);
    width = sh.texW, height = sh.texH;
    rgba.resize(static_cast<size_t>(width) * height * 4);
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sh.tex, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    table = sh.table;
    return true;
}

}  // namespace illyria::game
