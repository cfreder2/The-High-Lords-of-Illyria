#pragma once

// Close-up sprite sheets (docs/SPRITE_SPEC.md): every type has a normal sheet
// (~48 px a metre, less in a big battle) and may have a close-up sheet at
// twice that. A close-up sheet is loaded only while the camera is near enough
// to that type's soldiers for the normal one to be magnified, within a texture
// budget, and dropped again when the camera pulls back. The camera may not go
// so close that the best sheet at hand is magnified beyond kMaxMagnification.
// No GL here, so the tests can check the choices.

#include <vector>

namespace illyria::game::closeup {

// Screen pixels a sheet pixel may cover at the closest zoom.
constexpr float kMaxMagnification = 1.5f;
// Load a close-up sheet when the normal one is magnified this much...
constexpr float kLoadAbove = 1.15f;
// ...and keep it until it would be shrunk below this (no flicker in between).
constexpr float kKeepAbove = 0.8f;
// Texels all close-up sheets together may hold (four bytes each, plus mipmaps:
// about 43 MB), on top of the normal sheets' budget.
constexpr double kBudget = 8e6;

struct Need {
    int type = 0;
    float pxPerMetre = 0;  // screen pixels a metre at the type's soldier nearest the camera
    float sheetPpm = 0;    // the normal sheet's pixels a metre, as loaded
    bool available = false;  // a close-up sheet exists
    bool loaded = false;     // and is held now
    double texels = 0;       // its size
};

// The types whose close-up sheets should be held now, most magnified first.
std::vector<int> choose(std::vector<Need> needs, double budget = kBudget);

// Screen pixels a metre at distance d (vertical field of view fovY, a screen
// screenPx high).
float pxPerMetre(float screenPx, float fovY, float d);
// The closest the camera may come (distance to its target) so a sheet of ppm
// pixels a metre is magnified no more than kMaxMagnification; never under floor.
float minDistance(float screenPx, float fovY, float ppm, float floor);
// The same for a soldier on screen, not at the target: the camera now cameraDistance
// from its target, the soldier shown at pxPerMetre with a sheet of ppm. Distances
// along the view scale together, so this is the camera distance at which he
// would be at kMaxMagnification.
float minDistanceFor(float cameraDistance, float pxPerMetre, float ppm);

}  // namespace illyria::game::closeup
