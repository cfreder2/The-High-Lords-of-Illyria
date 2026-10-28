// Fetching pre-rendered sprite sheets (tools/blender) for the renderer. The
// site's sprites/index.json maps unit type numbers to sheet names; for each
// type a battle needs, the page fetches <name>.json (the cell table) and
// <name>.webp (the atlas), decodes the picture and hands both to the renderer,
// which swaps them in for the sheet it baked meanwhile. Types without a sheet,
// or whose fetch fails, keep the baked one.

#include <emscripten/emscripten.h>

#include <vector>

#include "../render/renderer.h"

namespace {

illyria::game::Renderer* target = nullptr;
std::vector<uint8_t> readPixels;
std::vector<float> readCells;
int readSize[2] = {0, 0};

// clang-format off
EM_JS(void, illyria_js_request_sheet, (int type), {
    const at = (path) => (Module.locateFile ? Module.locateFile(path) : path);
    if (!Module.illyriaSheetIndex)
        Module.illyriaSheetIndex = fetch(at('sprites/index.json')).then(r => (r.ok ? r.json() : {})).catch(() => ({}));
    Module.illyriaSheetIndex.then(index => {
        const name = index[type];
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
            const p = _malloc(px.length), c = _malloc(cells.length * 4);
            HEAPU8.set(px, p);
            HEAPF32.set(cells, c >> 2);
            if (!_illyria_sheet_loaded(type, p, bmp.width, bmp.height, c)) console.warn('sprite sheet ' + name + ' rejected');
            _free(p);
            _free(c);
        });
    }).catch(e => console.warn('sprite sheet ' + type + ': ' + e));
});
// clang-format on

}  // namespace

extern "C" {
// Test hooks: a type's sheet as the renderer holds it (RGBA, bottom row
// first), its size and its cells (130 x 8 floats as uCellTable holds them).
EMSCRIPTEN_KEEPALIVE const uint8_t* illyria_test_sheet(int type) {
    if (!target || !target->readSheet(type, readPixels, readSize[0], readSize[1], readCells)) return nullptr;
    return readPixels.data();
}
EMSCRIPTEN_KEEPALIVE const int* illyria_test_sheet_size() { return readSize; }
EMSCRIPTEN_KEEPALIVE const float* illyria_test_sheet_cells() { return readCells.data(); }

EMSCRIPTEN_KEEPALIVE int illyria_sheet_loaded(int type, const uint8_t* rgba, int width, int height, const float* cells) {
    return target && target->loadSheet(type, rgba, width, height, cells) ? 1 : 0;
}
}

namespace illyria::game {

void fetchPrerenderedSheets(Renderer& renderer) {
    target = &renderer;
    renderer.setSheetRequest([](int type) { illyria_js_request_sheet(type); });
}

}  // namespace illyria::game
