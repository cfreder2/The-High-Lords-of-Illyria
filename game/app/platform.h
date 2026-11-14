#pragma once

// The one seam between the game and the machine it runs on. app.cpp (the game
// loop, the input and HUD functions the page or the native shell call), the
// renderer and the sheet and model loaders are the same code everywhere; what
// differs lives behind these functions:
//   game/web/platform_web.cpp    the browser: a canvas, the page's JavaScript
//                                (audio, minimap, title screen), fetch
//   game/native/platform_sdl.cpp a desktop window (SDL), the audio mixer,
//                                files on disk
// Nothing else in game/ may call emscripten_* or SDL_*.

#include <cstdint>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define ILLYRIA_API EMSCRIPTEN_KEEPALIVE  // callable from the page
#else
#define ILLYRIA_API  // called by the native shell
#endif

namespace illyria::platform {

double nowMs();  // a monotonic clock, in milliseconds

// The drawing surface. Sizes are in CSS pixels (points); the pixel ratio says how many device
// pixels one is.
struct Canvas {
    double cssW, cssH, pixelRatio;
};
Canvas canvas();
// Makes the surface w x h device pixels. False where the system fixes it (a window's own drawable): then
// the game draws at the pixel ratio the surface has.
bool resizeCanvas(int w, int h);

// The framebuffer the frame is drawn into: 0, the surface itself, or the platform's own when it draws smaller than the
// surface and scales up in present() (the desktop's dynamic resolution; the browser's canvas is sized instead).
unsigned screenFramebuffer();
void present();

// What the battle sounds like: a voice cue, a sound effect, and the ambience layers.
void voiceCue(const char* set, const char* situation, bool enemy, float pan, float gain, int priority, int speaker, int tick);
void sfxCue(const char* id, float gain, float pan, float dist, float delay, float doppler, int priority, int count, int tick);
void sfxAmbience(float windMountain, float windValley, float swamp, float swampPan, float flies, float fliesPan, float drums, float drumsPan,
                 float rain);

// The world heard from under water (game/underwater.h, underwater_audio.h): everything the mix plays goes through a low-pass at
// `cutoffHz`, quieter by `gain` (linear) and with a slow wobble of pitch (0..1), all already scaled by how far under it is (0:
// the air's own, 20 kHz and unity: nothing changes); `ambience` is the level of the underwater loop. Called every frame.
void sfxUnderwater(float cutoffHz, float gain, float wobble, float ambience);
// The plunge in (entering) and the surfacing out, once each, as the eye crosses the surface.
void sfxUnderwaterCue(bool entering);

// The conquest minimap's land (RGBA, rows from the north) for the page's canvas; a match begun, for the page's HUD.
void minimapLand(const uint8_t* rgba, int w, int h);
void modeStarted(const char* id);

// Pre-rendered sprite sheets and 3D models are looked for by the platform (the site's sprites/ and models/
// folders) and handed back through illyria_sheet_loaded3 / illyria_sheet_closeup_listed / illyria_model_loaded
// (sheets.cpp, models.cpp), whenever they have come. `type` is a unit type; `standIn` the type whose sheet
// serves where there is none of its own (sprite_layout.h sheetStandIn); a closeup is the high resolution sheet.
void requestSheet(int type, bool closeup, int standIn);
void requestModel(int kind);

}  // namespace illyria::platform

// The game loop (app.cpp). start() builds the game once a graphics context is current; frame() runs one frame,
// and says whether it drew (the title screen draws every other).
namespace illyria::run {
bool start();
bool frame();
}  // namespace illyria::run
