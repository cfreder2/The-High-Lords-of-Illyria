// The browser's side of platform.h: the canvas, the page's JavaScript for
// audio (voice.js, sfx.js), the minimap and the match HUD, and fetch for the
// sprite sheets and models. Also the entry point.

#include <emscripten/emscripten.h>
#include <emscripten/html5.h>

#include <vector>

#include "../app/platform.h"
#include "illyria/game/underwater_audio.h"

namespace {

// Hands a voice cue to the page (game/web/voice.js), which plays it.
EM_JS(void, js_voice_cue, (const char* set, const char* situation, int enemy, float pan, float gain, int priority, int speaker, int tick), {
    if (window.IllyriaVoice) window.IllyriaVoice.cue(UTF8ToString(set), UTF8ToString(situation), !!enemy, pan, gain, priority, speaker, tick);
});

// Hands a sound effect cue, and the ambience, to the page (game/web/sfx.js).
EM_JS(void, js_sfx_cue, (const char* id, float gain, float pan, float dist, float delay, float doppler, int priority, int count, int tick), {
    if (window.IllyriaSfx) window.IllyriaSfx.cue(UTF8ToString(id), gain, pan, dist, delay, doppler, priority, count, tick);
});
EM_JS(void, js_sfx_ambience, (float wm, float wv, float sw, float swp, float fl, float flp, float dr, float drp, float rain), {
    if (window.IllyriaSfx) window.IllyriaSfx.ambience(wm, wv, sw, swp, fl, flp, dr, drp, rain);
});

// The world heard from under water, and its two cues (game/web/sfx.js, audio.js). The sounds themselves are synthesised here
// (game/underwater_audio.h, the one recipe the desktop plays too) and handed over as stereo float buffers.
EM_JS(void, js_sfx_underwater, (float cutoff, float gain, float wobble, float ambience), {
    if (window.IllyriaSfx && window.IllyriaSfx.underwater) window.IllyriaSfx.underwater(cutoff, gain, wobble, ambience);
});
EM_JS(void, js_sfx_synth, (const char* id, const float* stereo, int frames, int rate, int loop), {
    if (window.IllyriaSfx && window.IllyriaSfx.synth)
        window.IllyriaSfx.synth(UTF8ToString(id), HEAPF32.slice(stereo >> 2, (stereo >> 2) + frames * 2), rate, !!loop);
});
EM_JS(void, js_sfx_underwater_cue, (int entering), {
    if (window.IllyriaSfx && window.IllyriaSfx.underwaterCue) window.IllyriaSfx.underwaterCue(!!entering);
});

// The minimap's land (RGBA, rows from the north) to the page's canvas.
EM_JS(void, js_minimap_land, (const uint8_t* rgba, int w, int h), {
    if (window.IllyriaMinimap) window.IllyriaMinimap.land(HEAPU8.slice(rgba, rgba + w * h * 4), w, h);
});

// A match begun (startOtherMode): its HUD on the page (modes.js).
EM_JS(void, js_mode_started, (const char* id), {
    if (window.IllyriaMatch) window.IllyriaMatch.started(UTF8ToString(id));
});

// The site's sprites/index.json maps unit type numbers to sheet names; for each type a battle needs, the page
// fetches <name>.json (the cell table) and <name>.webp (the atlas), decodes the picture and hands both to the
// renderer, which swaps them in for the sheet it baked meanwhile. Types without a sheet, or whose fetch fails,
// keep the baked one. sprites/index_hd.json lists the close-up sheets with their sizes.
// clang-format off
EM_JS(void, js_request_sheet, (int type, int closeup, int standIn), {
    const at = (path) => (Module.locateFile ? Module.locateFile(path) : path);
    if (!Module.illyriaSheetIndex)
        Module.illyriaSheetIndex = fetch(at('sprites/index.json')).then(r => (r.ok ? r.json() : {})).catch(() => ({}));
    if (!Module.illyriaCloseupIndex) {
        // Tell the renderer which close-up sheets exist, and how big they are.
        Module.illyriaCloseupIndex = fetch(at('sprites/index_hd.json')).then(r => (r.ok ? r.json() : {})).catch(() => ({}));
        Module.illyriaCloseupIndex.then(index => {
            for (const k in index) _illyria_sheet_closeup_listed(+k, index[k].size[0], index[k].size[1], index[k].ppm);
        });
    }
    (closeup ? Module.illyriaCloseupIndex : Module.illyriaSheetIndex).then(index => {
        // Its own sheet once the index lists it, else its stand-in's (sprite_layout.h sheetStandIn).
        const entry = index[type] !== undefined ? index[type] : index[standIn];
        const name = closeup ? (entry && entry.name) : entry;
        if (!name) return;
        return Promise.all([
            fetch(at('sprites/' + name + '.json')).then(r => r.json()),
            fetch(at('sprites/' + name + '.webp')).then(r => r.blob()).then(b => createImageBitmap(b, {premultiplyAlpha: 'none', colorSpaceConversion: 'none'})),
        ]).then(([meta, bmp]) => {
            const canvas = document.createElement('canvas');
            canvas.width = bmp.width;
            canvas.height = bmp.height;
            const g = canvas.getContext('2d', {willReadFrequently: true});
            g.drawImage(bmp, 0, 0);
            const px = g.getImageData(0, 0, bmp.width, bmp.height).data;
            const cells = new Float32Array(meta.cells.flat());
            // A sheet of its own clips lists them (tools/sprites/import.py): a clip a line,
            // "name first frames seconds loop stride release [times...]" (sprite_clips.h parseClips).
            const num = (v, none) => (typeof v === 'number' && isFinite(v) ? v : none);
            const clips = (meta.clips || []).map(k => [k.name, k.first, k.frames, num(k.seconds, 0), k.loop ? 1 : 0, num(k.stride, 0),
                                                      num(k.release, -1)].concat(k.times || []).join(' ')).join('\n');
            const text = new TextEncoder().encode(clips + '\0');
            const p = _malloc(px.length), c = _malloc(cells.length * 4), t = _malloc(text.length);
            HEAPU8.set(px, p);
            HEAPF32.set(cells, c >> 2);
            HEAPU8.set(text, t);
            // Old sheets have 26 frames a view and say nothing; new ones say 44 (sprite_layout.h), or as their clips list.
            if (!_illyria_sheet_loaded3(type, p, bmp.width, bmp.height, c, meta.cells.length, meta.framesPerView || 26, closeup, t))
                console.warn('sprite sheet ' + name + ' rejected');
            _free(p);
            _free(c);
            _free(t);
        });
    }).catch(e => console.warn('sprite sheet ' + type + ': ' + e));
});
// clang-format on

// The 3D models made by tools/models: the site's models/<name>.bin (the mesh) and models/<name>.webp (its
// texture), decoded and handed to the renderer (renderer_models.cpp). Until a model arrives, or if its fetch
// fails, the renderer draws its stand-in built in code.
// clang-format off
EM_JS(void, js_request_model, (int kind), {
    const name = ['ruin'][kind];
    if (!name) return;
    const at = (path) => (Module.locateFile ? Module.locateFile(path) : path);
    const ok = (r) => { if (!r.ok) throw new Error(r.status + ' ' + r.url); return r; };
    Promise.all([
        fetch(at('models/' + name + '.bin')).then(ok).then(r => r.arrayBuffer()),
        fetch(at('models/' + name + '.webp')).then(ok).then(r => r.blob())
            .then(b => createImageBitmap(b, {premultiplyAlpha: 'none', colorSpaceConversion: 'none'})),
    ]).then(([bin, bmp]) => {
        const canvas = document.createElement('canvas');
        canvas.width = bmp.width;
        canvas.height = bmp.height;
        const g = canvas.getContext('2d', {willReadFrequently: true});
        g.drawImage(bmp, 0, 0);
        const px = g.getImageData(0, 0, bmp.width, bmp.height).data;
        const mesh = new Uint8Array(bin);
        const m = _malloc(mesh.length), p = _malloc(px.length);
        HEAPU8.set(mesh, m);
        HEAPU8.set(px, p);
        if (!_illyria_model_loaded(kind, m, mesh.length, p, bmp.width, bmp.height)) console.warn('model ' + name + ' rejected');
        _free(m);
        _free(p);
    }).catch(e => console.warn('model ' + name + ': ' + e));
});
// clang-format on

}  // namespace

namespace illyria::platform {

double nowMs() { return emscripten_get_now(); }

Canvas canvas() {
    Canvas c{0, 0, emscripten_get_device_pixel_ratio()};
    emscripten_get_element_css_size("#canvas", &c.cssW, &c.cssH);
    return c;
}

bool resizeCanvas(int w, int h) {
    int curW = 0, curH = 0;
    emscripten_get_canvas_element_size("#canvas", &curW, &curH);
    if (curW != w || curH != h) emscripten_set_canvas_element_size("#canvas", w, h);
    return true;
}

unsigned screenFramebuffer() { return 0; }
void present() {}

void voiceCue(const char* set, const char* situation, bool enemy, float pan, float gain, int priority, int speaker, int tick) {
    js_voice_cue(set, situation, enemy, pan, gain, priority, speaker, tick);
}
void sfxCue(const char* id, float gain, float pan, float dist, float delay, float doppler, int priority, int count, int tick) {
    js_sfx_cue(id, gain, pan, dist, delay, doppler, priority, count, tick);
}
void sfxAmbience(float wm, float wv, float sw, float swp, float fl, float flp, float dr, float drp, float rain) {
    js_sfx_ambience(wm, wv, sw, swp, fl, flp, dr, drp, rain);
}
void sfxUnderwater(float cutoffHz, float gain, float wobble, float ambience) {
    // The page makes its sounds from the one recipe, the first time the water is entered.
    static bool sent = false;
    if (!sent && ambience > 0.01f) {
        sent = true;
        const std::vector<float> loop = illyria::game::synthUnderwaterAmbience(44100), plunge = illyria::game::synthPlunge(44100), surfacing = illyria::game::synthSurfacing(44100);
        js_sfx_synth("underwater_loop", loop.data(), static_cast<int>(loop.size() / 2), 44100, 1);
        js_sfx_synth("underwater_in", plunge.data(), static_cast<int>(plunge.size() / 2), 44100, 0);
        js_sfx_synth("underwater_out", surfacing.data(), static_cast<int>(surfacing.size() / 2), 44100, 0);
    }
    js_sfx_underwater(cutoffHz, gain, wobble, ambience);
}
void sfxUnderwaterCue(bool entering) { js_sfx_underwater_cue(entering ? 1 : 0); }
void minimapLand(const uint8_t* rgba, int w, int h) { js_minimap_land(rgba, w, h); }
void modeStarted(const char* id) { js_mode_started(id); }
void requestSheet(int type, bool closeup, int standIn) { js_request_sheet(type, closeup ? 1 : 0, standIn); }
void requestModel(int kind) { js_request_model(kind); }

}  // namespace illyria::platform

int main() {
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = false;
    attrs.antialias = true;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    const EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0) {
        EM_ASM({ if (window.illyriaFatal) window.illyriaFatal("WebGL 2 is not available on this device."); });
        return 1;
    }
    emscripten_webgl_make_context_current(ctx);
    if (!illyria::run::start()) {
        EM_ASM({ if (window.illyriaFatal) window.illyriaFatal("Graphics initialisation failed."); });
        return 1;
    }
    EM_ASM({ if (window.illyriaReady) window.illyriaReady(); });
    emscripten_set_main_loop([] { illyria::run::frame(); }, 0, false);
    return 0;
}
