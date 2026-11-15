#pragma once

// The desktop's sound: what game/web/sfx.js and voice.js do in the browser, mixed by hand (the arithmetic is audio_mix.h).
// The sound effects (sfx/sounds.json and its mp3 files, decoded once on a thread) play positionally through a
// priority-limited set of strips, each panned, darkened by distance (a 12 dB per octave low-pass), sent to one shared reverb,
// a little off pitch and level; the ambience layers loop and fade to the levels the battle gives. The voices are the lines of
// voice/lines.json that have a file: a recording listed in voice/available.json, else the rendered placeholder (the cries of
// pain and death). There is no speech synthesis here: a line with no file is silent. The HUD's own sounds (ids beginning
// snd_ui_) go straight to the output. Everything ends in one limiter.

namespace illyria::native::audio {

bool open(const char* assetRoot);  // false: no sound device (the game plays on in silence)
void close();

// A sound effect by its id in the manifest. One whose files are still being decoded is held and played when they come, if
// that is no more than its delay + 0.4 s late. An id beginning "snd_ui_" is the HUD's: not filtered, not reverberated, from
// a small pool of its own, and heard while the game is paused.
void sfxCue(const char* id, float gain, float pan, float dist, float delay, float doppler, int priority);
void uiCue(const char* id);  // sfxCue(id, 1, 0, 0, 0, 0, 1)
// A line of a voice (game/src/voice.cpp's cue): at most three at once, 300 ms between starts unless grave.
void voiceCue(const char* set, const char* situation, bool enemy, float pan, float gain, int priority);
// One ambience loop by its id, faded to `level` (0 stops it); and the nine the battle has always sent, through the same.
void layer(const char* id, float level, float pan);
void ambience(float windMountain, float windValley, float swamp, float swampPan, float flies, float fliesPan, float drums, float drumsPan,
              float rain);
// Paused: the effects and voices in flight fade out (80 ms) and none start; the ambience drops to 35%.
void setPaused(bool paused);

// The four volumes, 0..1 (defaults 1, 0.8, 0.8, 1). They are kept by the HUD's settings file.
enum class Bus { Master, Effects, Voices, Ambience };
void setVolume(Bus bus, float volume);
float volume(Bus bus);
inline void setMasterVolume(float volume) { setVolume(Bus::Master, volume); }
inline float masterVolume() { return volume(Bus::Master); }

// A developer's log on stderr, a line for every cue, voice and change of a layer as it is handled (also: ILLYRIA_CUE_LOG=1).
void setCueLog(bool on);
bool cueLog();

// The world heard from under water (game/underwater.h): the mix goes through a low-pass at `cutoffHz`, quieter by `gain`, with a
// slow wobble of pitch; `ambience` is the underwater loop's level. All already scaled by how far under (0: the air's, no change).
void underwater(float cutoffHz, float gain, float wobble, float ambience);
// The plunge in, and the surfacing out (synthesised: game/underwater_audio.h).
void underwaterCue(bool entering);

}  // namespace illyria::native::audio
