// Fetching the 3D models made by tools/models for the renderer: the site's
// models/<name>.bin (the mesh) and models/<name>.webp (its texture), decoded
// and handed to the renderer (renderer_models.cpp). Until a model arrives, or
// if its fetch fails, the renderer draws its stand-in built in code.

#include <emscripten/emscripten.h>

#include "../render/renderer.h"

namespace {

illyria::game::Renderer* target = nullptr;

// clang-format off
EM_JS(void, illyria_js_request_model, (int kind), {
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

extern "C" {
EMSCRIPTEN_KEEPALIVE int illyria_model_loaded(int kind, const uint8_t* mesh, int meshBytes, const uint8_t* rgba, int width, int height) {
    return target && target->loadModel(static_cast<illyria::game::Renderer::ModelKind>(kind), mesh, static_cast<size_t>(meshBytes), rgba,
                                       width, height)
               ? 1
               : 0;
}
// Test hook: whether a model has come (0: the ruin).
EMSCRIPTEN_KEEPALIVE int illyria_test_model(int kind) {
    return target && target->modelLoaded(static_cast<illyria::game::Renderer::ModelKind>(kind)) ? 1 : 0;
}
// Test hook: the models off (0: the sites as before them) or on (1), for before/after shots.
EMSCRIPTEN_KEEPALIVE void illyria_test_models(int on) {
    if (target) target->enableModels(on != 0);
}
}

namespace illyria::game {

void fetchModels(Renderer& renderer) {
    target = &renderer;
    illyria_js_request_model(static_cast<int>(Renderer::ModelKind::Ruin));
}

}  // namespace illyria::game
