#pragma once

// The desktop's sound: what game/web/sfx.js does in the browser, mixed by hand. The sound effects
// (sfx/sounds.json and its mp3 files, decoded once on a thread) play positionally with a priority-limited
// set of strips, each panned, darkened by distance, a little off pitch; the ambience layers loop and fade
// to the levels the battle gives. The voices (game/web/voice.js) are spoken by the browser's speech
// synthesis or recorded; the desktop build has neither yet, so voiceCue is heard as nothing.

namespace illyria::native::audio {

bool open(const char* assetRoot);  // false: no sound device (the game plays on in silence)
void close();

void sfxCue(const char* id, float gain, float pan, float dist, float delay, float doppler, int priority);
void ambience(float windMountain, float windValley, float swamp, float swampPan, float flies, float fliesPan, float drums, float drumsPan,
              float rain);
void setMasterVolume(float volume);  // 0..1
float masterVolume();

// The world heard from under water (game/underwater.h): the mix goes through a low-pass at `cutoffHz`, quieter by `gain`, with a
// slow wobble of pitch; `ambience` is the underwater loop's level. All already scaled by how far under (0: the air's, no change).
void underwater(float cutoffHz, float gain, float wobble, float ambience);
// The plunge in, and the surfacing out (synthesised: game/underwater_audio.h).
void underwaterCue(bool entering);

}  // namespace illyria::native::audio
