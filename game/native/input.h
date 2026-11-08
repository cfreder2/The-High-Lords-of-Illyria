#pragma once

// The desktop's mouse and keyboard, turned into the calls the page's script makes (api.pointer, api.wheel,
// api.cameraKeys, api.stop ...: game/web/index.html). The key map is the page's, key for key
// (see Hud's help for the list). Touch is not handled.

#include <SDL3/SDL.h>

#include <set>
#include <string>
#include <vector>

#include "hud.h"

namespace illyria::native {

class Input {
public:
    explicit Input(Hud& hud) : hud_(hud) {}
    void handle(const SDL_Event& e);

private:
    void pointer(int type, const SDL_Event& e, float x, float y, int button);
    void key(const SDL_KeyboardEvent& k);
    void sendCameraKeys(bool shift);
    void setMode(int mode);
    void pickFormation(int n);
    void stepPace(int by);
    static void modifiers(SDL_Keymod mod);

    Hud& hud_;
    std::set<SDL_Scancode> held_;  // camera keys down
    bool captured_ = false;        // a press the HUD took: its moves and release are not the game's
    int orderMode_ = 0;
    int lastGroup_ = -1;
    Uint64 lastGroupMs_ = 0;
    bool trackpad_ = false;
    Uint64 wheelMs_ = 0;
    std::string formations_;
    std::vector<int> formationIds_;
};

}  // namespace illyria::native
