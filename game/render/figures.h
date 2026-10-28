#pragma once

// The soldiers' figures, after Myth: every unit type is a small articulated
// 3D model, posed in code for each frame of its animations. The renderer
// photographs each pose from five directions into a sprite sheet (the other
// three are mirror images), and the battle shows the frame that matches
// each soldier's facing, stride and deeds.

#include <cstdint>
#include <vector>

#include "illyria/sim/units.h"
#include "meshes.h"

namespace illyria::game::figures {

enum class Anim : uint8_t { Idle, Walk, Attack, Flinch, Die };
constexpr int kAnimCount = 5;
constexpr int kFrames[kAnimCount] = {4, 8, 6, 2, 6};
constexpr int kFirstFrame[kAnimCount] = {0, 4, 12, 18, 20};
constexpr int kFramesPerView = 26;
// Views from the front round to the back, 45 degrees apart; the three on the
// figure's other side are these mirrored.
constexpr int kViews = 5;
// The camera's height above the horizon when the sheets are made.
constexpr float kElevation = 0.52f;  // ~30 degrees

// The figure of a type posed for one frame: metres, feet at the origin,
// facing +x, +z up. Colours are linear RGB with the team's colour applied.
Mesh build(sim::UnitType type, const float team[3], Anim anim, int frame);

// The camera of view v (0 front, 2 the figure's right side, 4 its back):
// the image's right and up, and the direction toward the camera.
struct View {
    V3 right, up, toward;
};
View view(int v);
// Everything a type's sprite sheet is made from: its 26 posed meshes and,
// for each view of each, the picture's extent in metres (u across, v up,
// relative to the feet, as seen in that view).
//
// Mesh vertices here have ten floats: position, normal, colour, and a team
// mark (1 where the side's colour shows). Team areas are built in white;
// the sheet shader tints them with cloth(team).
struct Extent {
    float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
};
struct Plan {
    std::vector<float> frames[kFramesPerView];
    Extent extents[kViews][kFramesPerView];
};
Plan plan(sim::UnitType type);
// Sheet resolution: pixels a metre, but no frame more than kMaxPixels across.
constexpr float kPixelsPerMetre = 28.0f;
constexpr float kMaxPixels = 160.0f;
// The colour team-marked pixels are multiplied by (Myth's dulled colours).
V3 cloth(const float team[3]);

// Metres walked in one walk cycle (the 8 walk frames), so feet do not slide.
float stride(sim::UnitType type);
// How long the attack, flinch and death animations play, seconds.
float attackSeconds(sim::UnitType type);
constexpr float kFlinchSeconds = 0.3f;
constexpr float kDieSeconds = 0.9f;

}  // namespace illyria::game::figures
