#pragma once

// The desktop's HUD and start menu, drawn by the shell over the frame (overlay.h) from the same state
// the page's HUD reads: illyria_hud, illyria_campaign_state, illyria_labels, illyria_drag_box. It is small: the
// status line, the squad cards, the campaign's banners and the start menu (which lord, which computer
// lords, skirmish or conquest). The town screens, the settings and the manual are the page's, not here yet.

#include <string>
#include <vector>

#include "overlay.h"

namespace illyria::native {

enum class Key { Up, Down, Left, Right, Enter, Back };

class Hud {
public:
    // Where the conquest is kept between runs (empty: not saved).
    void setSaveFile(std::string path) { savePath_ = std::move(path); }
    void openMenu();
    void closeMenu();
    bool start(const std::string& mode, const std::string& options);  // illyria_start_mode, with the busy notice
    void startSaved() { activate(3); }  // carry on the saved conquest
    bool menuOpen() const { return menu_; }
    bool helpOpen() const { return help_; }
    void toggleHelp() { help_ = !help_; }

    // The frame rate and where the frame went, a line, shown top left at all times.
    void setPerf(std::string line) { perf_ = std::move(line); }
    void draw(Overlay& o, float w, float h);
    // A click at (x, y) points: true if the HUD took it (a card, a banner, a menu row).
    bool click(float x, float y);
    void menuKey(Key k);
    void menuDigit(int digit);  // 1-8 toggle a computer lord
    void menuToggleDifficulty() { hard_ = !hard_; }
    void saveConquest();  // the campaign being played, to the file
    void tick();           // once a frame, before drawing: a start asked for, now begun

private:
    struct Rect {
        float x, y, w, h;
        int what;  // a menu row, or a card (-1 - squad), or a banner (1000 + stack)
    };
    void drawMenu(Overlay& o, float w, float h);
    void activate(int row);
    bool loadSave(std::string& text) const;

    std::string savePath_, perf_;
    bool menu_ = false, help_ = false, playing_ = false, heldByMenu_ = false;
    int row_ = 0;
    int lord_ = 6;      // the Moonward Order, the skirmish's blue
    int foes_ = 0xFF & ~(1 << 6);
    bool hard_ = true;
    std::string pendingMode_, pendingOptions_, notice_, error_;
    int busyFrames_ = 0;
    int savedDay_ = -1;
    std::vector<Rect> hits_;
    int lastDay_ = 0;
};

}  // namespace illyria::native
