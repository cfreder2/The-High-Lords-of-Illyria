// Fetching pre-rendered sprite sheets (tools/blender, tools/sprites) for the
// renderer. The site's sprites/index.json maps unit type numbers to sheet
// names; for each type a battle needs, the page fetches <name>.json (the cell
// table) and <name>.webp (the atlas), decodes the picture and hands both to
// the renderer, which swaps them in for the sheet it baked meanwhile. Types
// without a sheet, or whose fetch fails, keep the baked one.
//
// sprites/index_hd.json lists the close-up sheets (<name>_hd.json/.webp, twice
// the pixels a metre) with their sizes; the renderer asks for one only while
// the camera is close to that type (renderer_sheets.cpp, closeup.h).

#include <string>
#include <vector>

#include "../render/renderer.h"
#include "platform.h"

namespace {

illyria::game::Renderer* target = nullptr;
std::vector<uint8_t> readPixels;
std::vector<float> readCells;
int readSize[2] = {0, 0};


}  // namespace

extern "C" {
// Test hooks: a type's sheet as the renderer holds it (RGBA, bottom row
// first), its size and its cells (130 x 8 floats as uCellTable holds them).
ILLYRIA_API const uint8_t* illyria_test_sheet(int type) {
    if (!target || !target->readSheet(type, readPixels, readSize[0], readSize[1], readCells)) return nullptr;
    return readPixels.data();
}
ILLYRIA_API const int* illyria_test_sheet_size() { return readSize; }
// Test hook: which sheets the renderer holds and where each came from ("Name=pre,Name=baked,...").
ILLYRIA_API const char* illyria_test_sheet_report() {
    static std::string report;
    report = target ? target->sheetReport() : std::string();
    return report.c_str();
}
// Test hook: the close-up sheets held now ("Name=WxH,...") and the camera's closest distance.
ILLYRIA_API const char* illyria_test_closeup_report() {
    static std::string report;
    report = target ? target->closeupReport() + ";min=" + std::to_string(target->closeupMinDistance()) : std::string();
    return report.c_str();
}
// Test hook: close-up sheets and the zoom limit off (0) or on (1), for before/after shots.
ILLYRIA_API void illyria_test_closeups(int on) {
    if (target) target->enableCloseups(on != 0);
}
ILLYRIA_API const float* illyria_test_sheet_cells() { return readCells.data(); }

// A sheet read by the platform: its pixels, where its cells are, and its own clips as text (sprite_clips.h parseClips;
// "" for a sheet of the fixed layout).
ILLYRIA_API int illyria_sheet_loaded3(int type, const uint8_t* rgba, int width, int height, const float* cells, int cellCount,
                                               int framesPerView, int closeup, const char* clips) {
    const illyria::game::sprites::SheetClips listed = illyria::game::sprites::parseClips(clips ? clips : "");
    if (clips && *clips && listed.empty()) return 0;  // a list, but malformed
    return target && target->loadSheet(type, rgba, width, height, cells, cellCount, framesPerView, closeup != 0, listed) ? 1 : 0;
}
ILLYRIA_API void illyria_sheet_closeup_listed(int type, int width, int height, float ppm) {
    if (target) target->listCloseup(type, width, height, ppm);
}
}

namespace illyria::game {

void fetchPrerenderedSheets(Renderer& renderer) {
    target = &renderer;
    renderer.setSheetRequest([](int type) { platform::requestSheet(type, false, sprites::sheetStandIn(type)); });
    renderer.setCloseupRequest([](int type) { platform::requestSheet(type, true, sprites::sheetStandIn(type)); });
}

}  // namespace illyria::game
