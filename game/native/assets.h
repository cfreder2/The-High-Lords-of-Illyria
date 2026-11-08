#pragma once

// The game's files on disk. The browser fetches them from the site (game/web/sprites, models, sfx, voice,
// copied beside the page); the desktop build reads the same folders, from the app bundle's Resources
// (macOS), an assets/ folder beside the program (Linux, Windows), or a folder named on the command line:
// there is one set of files and no second pipeline.

#include <cstdint>
#include <string>
#include <vector>

namespace illyria::native {

// Where the files are: the first of these folders holding sprites/, models/, sfx/. Empty until found.
bool findAssets(const std::vector<std::string>& candidates);
const std::string& assetRoot();

bool readFile(const std::string& path, std::vector<uint8_t>& out);
bool readText(const std::string& path, std::string& out);

// Sprite sheets and models, read and decoded on a thread and handed to the renderer (through the same
// illyria_sheet_loaded3 / illyria_model_loaded the page calls) from pump(), one a frame so a big sheet
// does not stall one.
void requestSheet(int type, bool closeup, int standIn);
void requestModel(int kind);
void pumpAssets();
void stopAssets();

}  // namespace illyria::native
