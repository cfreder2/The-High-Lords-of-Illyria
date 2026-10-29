#include <cmath>
#include <cstdio>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

#include "closeup.h"
#include "figures.h"
#include "illyria/game/camera.h"
#include "sprite_layout.h"
#include "test.h"

using namespace illyria;
using namespace illyria::game;

// docs/sprite_templates/files.txt, built in (tests/CMakeLists.txt).
extern const char* const kSpriteFiles;

namespace {

struct Files {
    std::set<std::string> files;
    std::map<int, std::string> units;  // type number -> sheet name
    std::vector<std::tuple<std::string, int, int>> clips;
};

Files readFiles() {
    Files f;
    std::istringstream in(kSpriteFiles);
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream l(line);
        std::string a, b;
        int n = 0, m = 0;
        l >> a;
        if (a == "#clip") {
            l >> b >> n >> m;
            f.clips.emplace_back(b, n, m);
        } else if (a == "#unit") {
            l >> b >> n;
            f.units[n] = b;
        } else if (!a.empty()) {
            f.files.insert(a);
        }
    }
    return f;
}

std::string cellFile(const std::string& unit, sprites::Clip c, int v, int i) {
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s/%s/v%d_f%02d.png", unit.c_str(), sprites::kClipNames[static_cast<int>(c)], v, i);
    return buf;
}

}  // namespace

// The layout the renderer reads is the one the art pipeline writes, and the
// old sheets' first 26 frames are its first 26.
TEST(sprite_layout_matches_the_manifest_and_the_old_sheets) {
    const Files f = readFiles();
    CHECK(static_cast<int>(f.clips.size()) == sprites::kClipCount);
    int first = 0;
    for (int c = 0; c < sprites::kClipCount && c < static_cast<int>(f.clips.size()); ++c) {
        CHECK(std::get<0>(f.clips[c]) == sprites::kClipNames[c]);
        CHECK(std::get<1>(f.clips[c]) == sprites::kClipFrames[c]);
        CHECK(std::get<2>(f.clips[c]) == sprites::kClipFirst[c]);
        CHECK(sprites::kClipFirst[c] == first);
        first += sprites::kClipFrames[c];
    }
    CHECK(first == sprites::kFramesPerView);
    CHECK(sprites::kCells == 240 && sprites::kLegacyCells == figures::kViews * figures::kFramesPerView);
    CHECK(sprites::kLegacyFramesPerView == figures::kFramesPerView && sprites::kViews == figures::kViews);
    for (int a = 0; a < figures::kAnimCount; ++a) {
        CHECK(sprites::kClipFirst[a] == figures::kFirstFrame[a]);
        CHECK(sprites::kClipFrames[a] == figures::kFrames[a]);
    }
    CHECK(static_cast<int>(f.units.size()) == sim::kUnitTypeCount);
}

// Every cell the renderer can ask for, for every type, has a file name in the
// manifest: each frame of each clip in each drawn view (and the death clips
// every way a soldier can die).
TEST(sprite_manifest_covers_every_cell_the_renderer_requests) {
    const Files f = readFiles();
    std::set<std::pair<int, int>> asked;  // clip, frame within it
    for (int c = 0; c < static_cast<int>(sprites::Clip::DieBack); ++c)
        for (int i = 0; i < sprites::kClipFrames[c]; ++i) asked.insert({c, i});
    for (int k = 0; k <= static_cast<int>(sprites::DeathKind::Charred); ++k)
        for (int d = 0; d <= static_cast<int>(sprites::DamageType::Fire); ++d)
            for (float along : {-1.0f, 0.0f, 0.3f, 1.0f}) {
                const sprites::Clip c = sprites::deathClip(static_cast<sprites::DeathKind>(k), static_cast<sprites::DamageType>(d), along);
                for (int i = 0; i < sprites::kClipFrames[static_cast<int>(c)]; ++i) asked.insert({static_cast<int>(c), i});
            }
    CHECK(asked.size() == static_cast<size_t>(sprites::kFramesPerView));  // every clip is played
    int missing = 0;
    for (int t = 0; t < sim::kUnitTypeCount; ++t) {
        const auto u = f.units.find(t);
        CHECK(u != f.units.end());
        if (u == f.units.end()) continue;
        for (const auto& [c, i] : asked)
            for (int v = 0; v < sprites::kViews; ++v)
                if (!f.files.count(cellFile(u->second, static_cast<sprites::Clip>(c), v, i))) ++missing;
    }
    CHECK(missing == 0);
}

// Old sheets play the new deaths with the one fall they have; frames never
// run past a sheet's end.
TEST(sprite_clips_fall_back_on_old_sheets) {
    using sprites::Clip;
    CHECK(sprites::available(Clip::DieFront, 26) == Clip::DieBack);
    CHECK(sprites::available(Clip::DieBurn, 26) == Clip::DieBack);
    CHECK(sprites::available(Clip::DieBurn, 44) == Clip::DieBurn);
    CHECK(sprites::available(Clip::Walk, 26) == Clip::Walk);
    CHECK(sprites::frameIndex(Clip::DieFront, 5, 26) == 25);
    CHECK(sprites::frameIndex(Clip::DieFront, 5, 44) == 31);
    CHECK(sprites::frameIndex(Clip::DieCrumple, 99, 44) == 37);  // the corpse stays
    CHECK(sprites::frameIndex(Clip::Idle, -3, 44) == 0);
    for (int fpv : {26, 44})
        for (int c = 0; c < sprites::kClipCount; ++c)
            for (int i = 0; i < 10; ++i) CHECK(sprites::frameIndex(static_cast<Clip>(c), i, fpv) < fpv);
    CHECK(sprites::gibSlot(sprites::Gib::Head, 0) == 220 && sprites::gibSlot(sprites::Gib::Wing, 1) == sprites::kCells - 1);
}

// How a soldier died picks his fall.
TEST(sprite_death_clip_by_how_he_died) {
    using sprites::Clip;
    using K = sprites::DeathKind;
    using D = sprites::DamageType;
    CHECK(sprites::deathClip(K::None, D::Slash, 1.0f) == Clip::DieBack);
    CHECK(sprites::deathClip(K::Pierced, D::Pierce, -1.0f) == Clip::DieBack);   // an arrow in the chest
    CHECK(sprites::deathClip(K::Pierced, D::Pierce, 1.0f) == Clip::DieFront);   // in the back
    CHECK(sprites::deathClip(K::Fell, D::Blunt, 0.9f) == Clip::DieFront);
    CHECK(sprites::deathClip(K::Fell, D::Slash, -0.5f) == Clip::DieCrumple);
    CHECK(sprites::deathClip(K::Severed, D::Cleave, 1.0f) == Clip::DieCrumple);
    CHECK(sprites::deathClip(K::Charred, D::Fire, 0.0f) == Clip::DieBurn);
    CHECK(sprites::deathClip(K::Fell, D::Explosive, -0.2f) == Clip::DieBack);
}

// Close-up sheets: loaded when the normal sheet would be magnified, kept a
// little past that (no flicker), within the budget, the most magnified first;
// and the camera's closest distance keeps the sharpest sheet at 1.5x at most.
TEST(closeup_sheets_load_near_and_drop_far) {
    using closeup::Need;
    const auto need = [](int type, float px, bool loaded, double texels = 3.2e6) {
        Need n;
        n.type = type, n.pxPerMetre = px, n.sheetPpm = 48, n.available = true, n.loaded = loaded, n.texels = texels;
        return n;
    };
    CHECK(closeup::choose({need(1, 40, false)}).empty());       // far: the normal sheet is enough
    CHECK(closeup::choose({need(1, 70, false)}).size() == 1);   // close: magnified 1.46x
    CHECK(closeup::choose({need(1, 48, false)}).empty());       // 1x: not worth loading...
    CHECK(closeup::choose({need(1, 48, true)}).size() == 1);    // ...but kept if held
    CHECK(closeup::choose({need(1, 30, true)}).empty());        // pulled back: dropped
    Need none = need(2, 200, false);
    none.available = false;
    CHECK(closeup::choose({none}).empty());
    const auto three = closeup::choose({need(1, 60, false), need(2, 150, false), need(3, 90, true)});
    CHECK(three.size() == 2 && three[0] == 2 && three[1] == 3);  // two fit the budget: the most magnified
    CHECK(closeup::choose({need(1, 100, false, 9e6)}).empty());  // too big for the budget at all

    const float screen = 844 * 2.0f;  // a phone's height in CSS pixels, at two device pixels each
    const float d48 = closeup::minDistance(screen, Camera::kFovY, 48, Camera::kMinDistance);
    const float d96 = closeup::minDistance(screen, Camera::kFovY, 96, Camera::kMinDistance);
    CHECK_NEAR(closeup::pxPerMetre(screen, Camera::kFovY, d48), 48 * closeup::kMaxMagnification, 0.01);
    CHECK_NEAR(d48, 2 * d96, 0.01);
    CHECK(d96 > 10 && d96 < 20);
    CHECK(closeup::minDistance(screen, Camera::kFovY, 2000, Camera::kMinDistance) == Camera::kMinDistance);
    CHECK(closeup::minDistance(screen, Camera::kFovY, 0, Camera::kMinDistance) == Camera::kMinDistance);
    // A soldier on screen shown at 144 px a metre with a 48 px/m sheet: three times too close.
    CHECK_NEAR(closeup::minDistanceFor(20, 144, 48), 40, 0.01);
    CHECK_NEAR(closeup::minDistanceFor(20, 72, 48), 20, 0.01);
}

// The camera keeps out of that distance when pinched, and eases out to it.
TEST(camera_min_distance_from_the_sprites) {
    Camera c;
    c.setViewport(390, 844);
    c.lookAt(512, 512, 40);
    c.setMinDistance(30);
    for (int i = 0; i < 20; ++i) c.zoomAt(1.5f, 195, 422);
    CHECK_NEAR(c.distance(), 30, 0.01);
    c.setMinDistance(60);  // a coarser sheet in view: ease out
    for (int i = 0; i < 80; ++i) c.setMinDistance(60);
    CHECK_NEAR(c.distance(), 60, 0.5);
    c.zoomAt(1.5f, 195, 422);
    CHECK(c.distance() >= 59.0f);
    c.setMinDistance(0);  // never below the camera's own limit
    CHECK(c.minDistance() == Camera::kMinDistance);
}
