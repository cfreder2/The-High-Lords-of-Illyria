#include "hud.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "api.h"
#include "assets.h"
#include "illyria/game/card_name.h"
#include "illyria/game/palette.h"
#include "json.h"

namespace illyria::native {

namespace {

// The eight lords, as the page's start screen lists them (game/web/title.js `lords`).
struct Lord {
    const char* name;
    const char* hero;
};
constexpr Lord kLords[8] = {
    {"The Aurelian Throne", "Kingshold"}, {"The Highcrag Giants", "Highcrag"}, {"The Ironhold Dwarves", "Deepforge"}, {"The Orcs of Grath", "Grathmaw"},
    {"The Sylvaran Elves", "Sylvaris"},   {"The Sunreach Riders", "Sunreach"}, {"The Moonward Order", "Moonhaven"},   {"Lord Malvoren", "Dreadspire"},
};

enum Row { kConquest, kFord, kSiege, kContinue, kQuit, kRows };
static_assert(kContinue == 3, "Hud::startSaved");

constexpr Colour kInk = rgba(0xF2EBD8), kDim = rgba(0xB9B0A0), kGold = rgba(0xE8C25A), kRed = rgba(0xFF7060);
// The menu and the help are laid on parchment (Overlay::paper): iron-gall ink, faded red, gold leaf (the page's --pg-* tokens).
constexpr Colour kPaperInk = rgba(0x2B1E10), kPaperDim = rgba(0x5A4326), kSeal = rgba(0x8B2B20), kLeaf = rgba(0xB58A2C), kPaperOff = rgba(0x7D6B4A), kPaperOut = rgba(0x8A7A5C);

Colour hex(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return rgba(0xCCCCCC);
    return rgba(static_cast<unsigned>(std::strtoul(s.c_str() + 1, nullptr, 16)));
}

// The palette's twelve (game/include/illyria/game/palette.h), as the menu draws them.
Colour swatchColour(int i) {
    const auto v = illyria::game::palette::swatch(i).rgb;
    return {static_cast<uint8_t>(v.x * 255 + 0.5f), static_cast<uint8_t>(v.y * 255 + 0.5f), static_cast<uint8_t>(v.z * 255 + 0.5f), 255};
}

Json read(const char* text) {
    Json j;
    Json::parse(text ? text : "{}", j);
    return j;
}

}  // namespace

// Whether the game being played is stopped (a battle's clock, or the conquest's).
static bool gamePaused() {
    Json camp = read(illyria_campaign_state());
    if (camp.find("day")) return camp.flag("paused");
    return read(illyria_hud()).flag("paused");
}
static void togglePause() {
    if (read(illyria_campaign_state()).find("day")) illyria_campaign_pause();
    else illyria_toggle_pause();
}

// A game in play is held while its menu is up, and let go when it closes: nothing steps behind a menu.
void Hud::openMenu() {
    if (menu_) return;
    menu_ = true;
    help_ = false;
    if (playing_ && !gamePaused()) {
        togglePause();
        heldByMenu_ = true;
    }
}

void Hud::closeMenu() {
    menu_ = false;
    if (heldByMenu_ && gamePaused()) togglePause();
    heldByMenu_ = false;
}

bool Hud::loadSave(std::string& text) const {
    if (savePath_.empty() || !readText(savePath_, text)) return false;
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
    return !text.empty();
}

void Hud::saveConquest() {
    const std::string text = illyria_campaign_save();
    if (text.empty() || savePath_.empty()) return;
    std::ofstream(savePath_, std::ios::binary | std::ios::trunc) << text;
}

// What is kept on this device: lines of "key=value" in the preferences folder (the page keeps the same in localStorage).
void Hud::setSettingsFile(std::string path) {
    settingsPath_ = std::move(path);
    std::string text;
    if (settingsPath_.empty() || !readText(settingsPath_, text)) return;
    std::istringstream lines(text);
    for (std::string line; std::getline(lines, line);) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq), value = line.substr(eq + 1);
        if (key == "minimap") minimapOn_ = value != "0";
        else if (key == "colour") {
            illyria::game::V3 c;
            if (!illyria::game::palette::parseHex(value.c_str(), c)) continue;
            // The swatch it is, or (a colour of the web's own, say) the nearest.
            colour_ = -1;
            float best = 1e9f;
            for (int i = 0; i < illyria::game::palette::kSwatches; ++i) {
                const float d = illyria::game::palette::distance(c, illyria::game::palette::swatch(i).rgb);
                if (d < best) best = d, colour_ = i;
            }
        }
    }
}

void Hud::saveSettings() const {
    if (settingsPath_.empty()) return;
    std::ofstream f(settingsPath_, std::ios::binary | std::ios::trunc);
    f << "minimap=" << (minimapOn_ ? 1 : 0) << "\n";
    if (colour_ >= 0) f << "colour=" << illyria::game::palette::hex(illyria::game::palette::swatch(colour_).rgb) << "\n";
}

std::string Hud::chosenColour() const {
    return colour_ >= 0 ? illyria::game::palette::hex(illyria::game::palette::swatch(colour_).rgb) : "";
}

// C in the menu: through the twelve and back to the lord's own.
void Hud::menuCycleColour(int by) {
    if (!menu_) return;
    const int n = illyria::game::palette::kSwatches;
    // (the lord's own swatch is the lord's: it clears the choice)
    int at = colour_ < 0 ? lord_ : colour_;
    at = (at + by + n) % n;
    colour_ = at == lord_ ? -1 : at;
    saveSettings();
}

void Hud::toggleMinimap() {
    minimapOn_ = !minimapOn_;
    saveSettings();
}

void Hud::setMinimapLand(const uint8_t* rgba, int w, int h) {
    miniPixels_.assign(rgba, rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
    miniW_ = w;
    miniH_ = h;
    miniDirty_ = true;
}

bool Hud::start(const std::string& mode, const std::string& options) {
    illyria_set_side_colour(chosenColour().c_str());  // cosmetic, and the engine's: the sim never sees it
    miniLandStale_ = true;
    // The island's ground is made at the start of a conquest, a few seconds: say so first, as the page does.
    pendingMode_ = mode;
    pendingOptions_ = options;
    busyFrames_ = 2;
    notice_ = mode == "campaign" ? "Laying out the island - every wall, gate and tree, for the fights" : "Setting out the battle";
    error_.clear();
    menu_ = false;
    heldByMenu_ = false;  // (a new game starts afresh)
    return true;
}

void Hud::tick() {
    if (busyFrames_ > 0 && --busyFrames_ == 0) {
        const bool ok = illyria_start_mode(pendingMode_.c_str(), pendingOptions_.c_str()) != 0;
        notice_.clear();
        playing_ = ok;
        if (!ok) {
            error_ = illyria_mode_error();
            menu_ = true;
            illyria_start_mode("title", "{}");
        }
        savedDay_ = -1;
    }
}

void Hud::activate(int row) {
    char buf[160];
    switch (row) {
        case kConquest:
            std::snprintf(buf, sizeof buf, "{\"hard\":%d,\"player\":%d,\"enemies\":%d,\"speed\":1}", hard_ ? 1 : 0, lord_, foes_ & ~(1 << lord_) & 0xFF);
            start("campaign", buf);
            break;
        case kFord: start("skirmish", "{\"scenario\":0}"); break;
        case kSiege: start("skirmish", "{\"scenario\":1}"); break;
        case kContinue: {
            std::string save;
            if (!loadSave(save)) break;
            start("campaign", "{\"save\":\"" + save + "\",\"speed\":1}");
            break;
        }
        case kQuit: {
            SDL_Event quit{};
            quit.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&quit);
            break;
        }
        default: break;
    }
}

void Hud::menuKey(Key k) {
    if (!menu_) return;
    switch (k) {
        case Key::Up: row_ = (row_ + kRows - 1) % kRows; break;
        case Key::Down: row_ = (row_ + 1) % kRows; break;
        case Key::Left: lord_ = (lord_ + 7) % 8; foes_ &= ~(1 << lord_); break;
        case Key::Right: lord_ = (lord_ + 1) % 8; foes_ &= ~(1 << lord_); break;
        case Key::Enter: activate(row_); break;
        case Key::Back: if (playing_) closeMenu(); break;  // (back to the game, if there is one)
    }
}

void Hud::menuDigit(int digit) {
    if (!menu_ || digit < 1 || digit > 8 || digit - 1 == lord_) return;
    foes_ ^= 1 << (digit - 1);
}

bool Hud::click(float x, float y) {
    for (const Rect& r : hits_) {
        if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) continue;
        if (menu_) {
            if (r.what >= 0 && r.what < kRows) {
                row_ = r.what;
                activate(r.what);
            } else if (r.what >= 100 && r.what < 108) {
                lord_ = r.what - 100;
                foes_ &= ~(1 << lord_);
            } else if (r.what >= 200 && r.what < 208) {
                menuDigit(r.what - 200 + 1);
            } else if (r.what == 300) {
                hard_ = !hard_;
            } else if (r.what >= 400 && r.what < 400 + illyria::game::palette::kSwatches) {
                const int i = r.what - 400;
                colour_ = i == lord_ ? -1 : i;
                saveSettings();
            }
        } else if (r.what == 2000) {
            lookAt(x, y);
            return true;
        } else if (r.what <= -1 && r.what > -1000) {
            illyria_select_squad(-1 - r.what);
        } else if (r.what >= 1000) {
            illyria_campaign_select(static_cast<unsigned>(r.what - 1000));
        }
        return true;
    }
    return false;
}

namespace {

// `text` broken at spaces into lines no wider than `maxW` points at `size`.
std::vector<std::string> wrap(const Overlay& o, const std::string& text, float size, float maxW) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream words(text);
    for (std::string word; words >> word;) {
        const std::string tried = line.empty() ? word : line + " " + word;
        if (!line.empty() && o.width(tried, size) > maxW) {
            lines.push_back(line);
            line = word;
        } else {
            line = tried;
        }
    }
    if (!line.empty()) lines.push_back(line);
    return lines;
}

// The largest size up to `size` at which `text` fits in `maxW`.
float fit(const Overlay& o, const std::string& text, float size, float maxW) {
    while (size > 8 && o.width(text, size) > maxW) size -= 1;
    return size;
}

}  // namespace

// The panel is sized from the text it holds (measured, wrapped or shrunk to the window), so nothing passes its edge.
void Hud::drawMenu(Overlay& o, float w, float h) {
    constexpr float kPad = 24, kRow = 34, kLordRow = 22;
    const float pw = std::min(580.0f, w - 20), inner = pw - 2 * kPad;
    static const char* const kLabel[kRows] = {"Conquest of the island", "Skirmish: the Ford", "Skirmish: the siege", "Continue the conquest", "Quit"};

    const std::string title = "THE HIGH LORDS OF ILLYRIA";
    const float titleSize = fit(o, title, 28, inner);
    const std::string sub = std::string("v") + illyria_version() + " - desktop";
    const std::string leadLabel = "You lead (left / right, or click a square):";
    const float leadSize = fit(o, leadLabel, 13, inner);
    const std::string difficulty = std::string("Computer lords: keys 1-8 toggle them (red are in the war). They play ") + (hard_ ? "Hard" : "Easy") + " (D).";
    std::vector<std::string> hints = wrap(o, difficulty, 12, inner);
    const size_t difficultyLines = hints.size();
    for (const std::string& l : wrap(o, "Up / down, Enter, or click. In a battle, / shows the keys.", 12, inner)) hints.push_back(l);
    const std::vector<std::string> errors = wrap(o, error_, 13, inner);

    const std::string colourLabel = "Your colour (C, or click; the sides keep theirs unless they clash):";
    const float colourSize = fit(o, colourLabel, 13, inner);
    constexpr float kSwatch = 24;
    const float ph = kPad + titleSize + 8 + 12 + 18 + kRow * kRows + 12 + leadSize + 9 + 4 * kLordRow + 8 + colourSize + 8 + kSwatch + 14 +
                     static_cast<float>(hints.size()) * 16 + (errors.empty() ? 0 : 6 + static_cast<float>(errors.size()) * 17) + kPad - 4;
    const float px = (w - pw) / 2, py = std::max(10.0f, (h - ph) / 2);
    o.paper(px, py, pw, ph, 7);

    float y = py + kPad;
    o.text(px + kPad, y, title, kSeal, titleSize);
    y += titleSize + 8;
    o.text(px + kPad, y, sub, kPaperDim, 12);
    y += 12 + 18;
    std::string save;
    for (int r = 0; r < kRows; ++r, y += kRow) {
        const bool off = r == kContinue && !loadSave(save);
        const std::string label = (r == row_ ? "> " : "  ") + std::string(kLabel[r]);
        if (r == row_) {
            o.rect(px + 12, y - 5, pw - 24, 28, rgba(0xB89A5A, 120));
            o.frame(px + 12, y - 5, pw - 24, 28, 1.5f, kSeal);
        }
        o.text(px + kPad, y, label, off ? kPaperOff : r == row_ ? kSeal : kPaperInk, fit(o, label, 18, inner));
        hits_.push_back({px + 12, y - 5, pw - 24, 28, r});
    }
    // The conquest's choices: who you are and who else is in the war.
    y += 12 - 5;
    o.text(px + kPad, y, leadLabel, kPaperDim, leadSize);
    y += leadSize + 9;
    const float col = inner / 2;
    for (int i = 0; i < 8; ++i) {
        const float x = px + kPad + static_cast<float>(i % 2) * col, yy = y + static_cast<float>(i / 2) * kLordRow;
        const bool me = i == lord_, foe = !me && (foes_ >> i & 1);
        const std::string name = kLords[i].name;
        o.rect(x, yy + 2, 10, 10, me ? kLeaf : foe ? kSeal : rgba(0xCDB988));
        o.frame(x, yy + 2, 10, 10, 1.2f, me ? kPaperInk : foe ? kSeal : kPaperOut);
        o.text(x + 18, yy, name, me ? kSeal : foe ? kPaperInk : kPaperOut, fit(o, name, 13, col - 24));
        hits_.push_back({x, yy - 2, 16, 18, 100 + i});  // the square: lead as them
        hits_.push_back({x + 16, yy - 2, col - 20, 18, 200 + i});  // the name: in the war, or not
    }
    y += 4 * kLordRow + 8;
    // The colour you will wear: the twelve of the palette, a ring on the chosen one (the lord's own, if none).
    o.text(px + kPad, y, colourLabel, kPaperDim, colourSize);
    y += colourSize + 8;
    {
        const int n = illyria::game::palette::kSwatches;
        const float step = std::min(kSwatch + 6.0f, (inner - kSwatch) / static_cast<float>(n - 1));
        const int shown = colour_ >= 0 ? colour_ : lord_;
        for (int i = 0; i < n; ++i) {
            const float sx = px + kPad + static_cast<float>(i) * step;
            o.rect(sx, y, kSwatch, kSwatch, swatchColour(i));
            o.frame(sx, y, kSwatch, kSwatch, 1.5f, rgba(0x2B1E10, 200));
            if (i == shown) o.frame(sx - 3, y - 3, kSwatch + 6, kSwatch + 6, 2.5f, kSeal);
            hits_.push_back({sx, y, kSwatch, kSwatch, 400 + i});
        }
        y += kSwatch + 14;
    }
    for (size_t i = 0; i < hints.size(); ++i, y += 16) {
        o.text(px + kPad, y, hints[i], kPaperDim, 12);
        if (i < difficultyLines) hits_.push_back({px + kPad, y - 3, inner, 18, 300});
    }
    if (!errors.empty()) {
        y += 6;
        for (const std::string& l : errors) o.text(px + kPad, y, l, kSeal, 13), y += 17;
    }
}

// A press on the minimap, at (x, y) points: the camera over that point of the map.
void Hud::lookAt(float x, float y) {
    if (miniW0_ <= 0 || miniH0_ <= 0) return;
    const float u = std::clamp((x - miniX_) / miniW0_, 0.0f, 1.0f), v = std::clamp((y - miniY_) / miniH0_, 0.0f, 1.0f);
    illyria_minimap_look(u * mapW_, (1 - v) * mapH_);
}

bool Hud::drag(float x, float y) {
    if (!minimapOn_ || menu_ || miniW0_ <= 0) return false;
    // Only a press that began on the map keeps steering (Input hands every move of a captured press here; a card's press is far off).
    if (x < miniX_ - 40 || x > miniX_ + miniW0_ + 40 || y < miniY_ - 40 || y > miniY_ + miniH0_ + 40) return false;
    lookAt(x, y);
    return true;
}

// The map in the corner (M): the engine's lines (minimap.h) drawn on a small parchment, about 18% of the window's
// height, top right under the status bar. The conquest's island or a battle's field; only what the engine lets the
// viewer see is in the lines. A press on it takes the camera there.
void Hud::drawMinimap(Overlay& o, float w, float h) {
    miniW0_ = miniH0_ = 0;
    const int kind = illyria_minimap_kind();
    if (kind == 0 || !minimapOn_) return;
    if (kind != miniKind_ || miniLandStale_) {
        miniKind_ = kind;
        miniLandStale_ = false;
        illyria_minimap_land();  // (-> setMinimapLand)
    }
    if (miniDirty_ && miniW_ > 0) {
        if (miniTexture_) o.updateImage(miniTexture_, miniPixels_.data(), miniW_, miniH_);
        else miniTexture_ = o.makeImage(miniPixels_.data(), miniW_, miniH_);
        miniDirty_ = false;
    }
    // Ten times a second is plenty.
    if (const unsigned now = static_cast<unsigned>(SDL_GetTicks()); miniText_.empty() || now - miniAt_ > 100) {
        miniText_ = illyria_minimap();
        miniAt_ = now;
    }
    const float mh = std::clamp(h * 0.18f, 96.0f, 260.0f), mw = kind == 1 ? mh * 0.8f : mh;
    const float x0 = w - mw - 16, y0 = 44;
    miniX_ = x0, miniY_ = y0, miniW0_ = mw, miniH0_ = mh;
    // The frame: parchment, laid thin enough to see the ground through its margin, with a gold line and an inked rim.
    o.rect(x0 - 5, y0 - 5, mw + 10, mh + 10, rgba(0xE9D9B0, 168));
    o.frame(x0 - 5, y0 - 5, mw + 10, mh + 10, 2, rgba(0x7A5A2C, 235));
    o.frame(x0 - 2, y0 - 2, mw + 4, mh + 4, 1, rgba(0xB58A2C, 220));
    if (miniTexture_) o.image(miniTexture_, x0, y0, mw, mh, rgba(0xFFFFFF, 235));
    else o.rect(x0, y0, mw, mh, rgba(0x1A2A38, 200));
    // Lines to marks.
    std::istringstream lines(miniText_);
    float mapW = 1, mapH = 1;
    std::vector<std::vector<std::string>> rows;
    for (std::string line; std::getline(lines, line);) {
        std::vector<std::string> f;
        size_t from = 0;
        for (;;) {
            const size_t bar = line.find('|', from);
            f.push_back(line.substr(from, bar == std::string::npos ? std::string::npos : bar - from));
            if (bar == std::string::npos) break;
            from = bar + 1;
        }
        if (f[0] == "m" && f.size() >= 3) mapW = static_cast<float>(std::atof(f[1].c_str())), mapH = static_cast<float>(std::atof(f[2].c_str()));
        else rows.push_back(std::move(f));
    }
    mapW_ = mapW, mapH_ = mapH;
    const auto X = [&](float m) { return x0 + std::clamp(m / mapW, 0.0f, 1.0f) * mw; };
    const auto Y = [&](float m) { return y0 + mh - std::clamp(m / mapH, 0.0f, 1.0f) * mh; };
    const auto at = [&](const std::vector<std::string>& f, size_t i) { return i < f.size() ? static_cast<float>(std::atof(f[i].c_str())) : 0.0f; };
    const Colour ink = rgba(0x000000, 200);
    for (const auto& f : rows) {  // fights glow, then heaps, then men
        if (f[0] != "f") continue;
        const float r = std::min(3.0f + std::sqrt(at(f, 3)), 8.0f);
        o.rect(X(at(f, 1)) - r, Y(at(f, 2)) - r, 2 * r, 2 * r, rgba(0xFF8C28, 150));
    }
    for (const auto& f : rows) {
        if (f[0] == "s") {  // ruins, temples, sages: a pale mark
            const float sx = X(at(f, 1)), sy = Y(at(f, 2));
            o.rect(sx - 2, sy - 2, 4, 4, ink);
            o.rect(sx - 1.2f, sy - 1.2f, 2.4f, 2.4f, rgba(0xF4EAD0));
        } else if (f[0] == "k" && f.size() >= 5) {
            const float r = std::min(1.5f + std::sqrt(at(f, 4)) * 0.3f, 4.5f);
            o.rect(X(at(f, 2)) - r - 0.8f, Y(at(f, 3)) - r - 0.8f, 2 * r + 1.6f, 2 * r + 1.6f, ink);
            o.rect(X(at(f, 2)) - r, Y(at(f, 3)) - r, 2 * r, 2 * r, hex(f[1]));
        } else if (f[0] == "d" && f.size() >= 4) {
            o.rect(X(at(f, 2)) - 1.7f, Y(at(f, 3)) - 1.7f, 3.4f, 3.4f, ink);
            o.rect(X(at(f, 2)) - 1.2f, Y(at(f, 3)) - 1.2f, 2.4f, 2.4f, hex(f[1]));
        }
    }
    for (const auto& f : rows) {  // towns on top: a square in the holder's colour
        if (f[0] != "t" || f.size() < 5) continue;
        const float r = f[4] == "1" ? 3.4f : f[4] == "2" ? 1.7f : 2.6f, sx = X(at(f, 1)), sy = Y(at(f, 2));
        o.rect(sx - r - 0.8f, sy - r - 0.8f, 2 * r + 1.6f, 2 * r + 1.6f, ink);
        o.rect(sx - r, sy - r, 2 * r, 2 * r, hex(f[3]));
    }
    // Where the camera looks.
    {
        float px[4], py[4];
        const std::string view = illyria_minimap_view();
        int n = 0;
        std::istringstream corners(view);
        for (std::string c; n < 4 && corners >> c;) {
            const size_t comma = c.find(',');
            if (comma == std::string::npos) break;
            px[n] = X(static_cast<float>(std::atof(c.c_str()))), py[n] = Y(static_cast<float>(std::atof(c.c_str() + comma + 1)));
            ++n;
        }
        if (n == 4)
            for (int i = 0; i < 4; ++i) {
                o.line(px[i], py[i], px[(i + 1) % 4], py[(i + 1) % 4], 3, rgba(0x000000, 150));
                o.line(px[i], py[i], px[(i + 1) % 4], py[(i + 1) % 4], 1.4f, rgba(0xFFFFFF, 235));
            }
    }
    hits_.insert(hits_.begin(), {x0, y0, mw, mh, 2000});
}

void Hud::draw(Overlay& o, float w, float h) {
    hits_.clear();
    if (showStats_ && (!notice_.empty() || menu_)) o.text(6, 4, perf_, kGold, 10);  // (in play, the status bar carries it)
    if (!notice_.empty()) {
        o.rect(0, 0, w, h, rgba(0x000000, 190));
        const float tw = o.width(notice_, 20);
        o.text((w - tw) / 2, h / 2 - 10, notice_, kInk, 20);
        return;
    }
    if (menu_) {
        drawMenu(o, w, h);
        return;
    }
    const Json hud = read(illyria_hud());
    const Json camp = read(illyria_campaign_state());
    const bool conquest = camp.find("day") != nullptr;
    char buf[256];
    std::vector<std::string> status;  // the bar's words, the fullest first; the first that fits is shown

    // The status line.
    o.rect(0, 0, w, 34, rgba(0x000000, 150));
    if (showStats_) o.text(6, 4, perf_, kGold, 10);
    if (conquest) {
        const int day = static_cast<int>(camp.num("day"));
        const int gold = static_cast<int>(camp.num("gold")), towns = static_cast<int>(camp.num("cities")), land = static_cast<int>(camp.num("land"));
        const int speed = static_cast<int>(camp.num("speed"));
        const std::string held = camp.flag("paused") ? " PAUSED" : "";
        const std::string sp = std::to_string(speed) + "x" + held;
        status = {"Conquest  Day " + std::to_string(day) + "   Gold " + std::to_string(gold) + "   Towns " + std::to_string(towns) + "   Land " + std::to_string(land) + "   " + sp,
                  "Day " + std::to_string(day) + "  Gold " + std::to_string(gold) + "  Towns " + std::to_string(towns) + "  " + sp,
                  "Day " + std::to_string(day) + "  Gold " + std::to_string(gold) + held};
        if (day != savedDay_) {
            savedDay_ = day;
            saveConquest();
        }
        lastDay_ = day;
    } else {
        // A battle on its own, in a player's words: where, what wins it, how many of the foe are left.
        const bool siege = hud.num("scenario") == 1;
        std::string foe = hud.str("foe");
        std::string town = foe.rfind("Garrison of ", 0) == 0 ? foe.substr(12) : "";
        const std::string where = siege ? "Siege" + (town.empty() ? "" : ": " + town) : "Skirmish: the Ford";
        const std::string goal = siege ? "take the courtyard" : foe.empty() ? "" : "rout the " + foe;
        const std::string held = hud.flag("paused") ? "PAUSED   " : "";
        const std::string count = hud.num("foeTotal") > 0 ? "Foe " + std::to_string(static_cast<int>(hud.num("foeAlive"))) + "/" + std::to_string(static_cast<int>(hud.num("foeTotal"))) : "";
        status = {held + where + (goal.empty() ? "" : "   Objective: " + goal) + (count.empty() ? "" : "   " + count),
                  held + where + (count.empty() ? "" : "   " + count), held + where};
    }
    if (hud.flag("planning")) {
        char plan[160];
        std::snprintf(plan, sizeof plan, "PLANNING  %d held   `: send   Backspace: drop", static_cast<int>(hud.num("planned")));
        const float pw = o.width(plan, 14);
        o.rect((w - pw) / 2 - 10, 64, pw + 20, 24, rgba(0x000000, 170));
        o.text((w - pw) / 2, 68, plan, rgba(0xD680FF, 255), 14);
    }
    // (With the developer line on, the status sits under it; else it is centred in the bar. In a narrow window it shrinks to leave the hint room.)
    const std::string hint = w < 560 ? "/: keys" : "Enter: menu    /: keys";
    const float hintX = w - o.width(hint, 12) - 10, statusSize = showStats_ ? 13.0f : 14.0f;
    {
        const float room = std::max(40.0f, hintX - 16);
        const std::string* pick = &status.back();
        for (const std::string& v : status)
            if (o.width(v, statusSize) <= room) {
                pick = &v;
                break;
            }
        o.text(6, showStats_ ? 18 : 10, *pick, kInk, fit(o, *pick, statusSize, room));
    }
    o.text(hintX, showStats_ ? 18 : 11, hint, kDim, 12);

    const int outcome = static_cast<int>(hud.num("outcome"));
    if (!conquest && outcome != 0) {
        const std::string t = outcome > 0 ? "VICTORY" : "DEFEAT";
        o.text((w - o.width(t, 40)) / 2, h * 0.3f, t, outcome > 0 ? kGold : kRed, 40);
    }
    if (conquest && camp.num("winner") != 0) {
        const std::string t = "The war is decided";
        o.text((w - o.width(t, 32)) / 2, h * 0.3f, t, kGold, 32);
    }
    const std::string notice = hud.str("notice");
    if (!notice.empty()) o.text((w - o.width(notice, 16)) / 2, 40, notice, kRed, 16);

    // Names of towns and places when the view is high.
    const size_t sel = static_cast<size_t>(hud.num("selCount"));
    const bool squadCards = !hud.list("squads").empty();
    {
        std::istringstream lines(illyria_labels());
        std::string line;
        while (std::getline(lines, line)) {
            std::vector<std::string> f;
            size_t from = 0;
            for (int i = 0; i < 5; ++i) {  // kind|colour|x|y|town|name
                const size_t bar = line.find('|', from);
                if (bar == std::string::npos) break;
                f.push_back(line.substr(from, bar - from));
                from = bar + 1;
            }
            if (f.size() < 5) continue;
            const std::string name = line.substr(from);
            const float x = static_cast<float>(std::atof(f[2].c_str())), y = static_cast<float>(std::atof(f[3].c_str()));
            if (y < 38) continue;  // (under the status bar)
            // A name is drawn whole or not at all: it fades over the last stretch before a screen edge or a strip
            // of the HUD (the bar above; below, the selection line and the cards), and goes when it would be cut.
            const float size = f[0] == "1" ? 13.0f : 11.0f;
            const float nw = name.empty() ? 0.0f : o.width(name, size);
            const float stripTop = h - (sel > 0 ? 104.0f : squadCards ? 76.0f : 12.0f);
            const bool markerShown = y + 2 < stripTop && x > 6 && x < w - 6;
            const Colour c = hex(f[1]);
            if (markerShown && (f[0] == "0" || f[0] == "1" || f[0] == "7")) {  // a marker: the holder's colour in a dark frame with a light top edge
                o.rect(x - 6, y + 2, 12, 12, rgba(0x000000, 200));
                o.rect(x - 5, y + 3, 10, 10, c);
                o.rect(x - 5, y + 3, 10, 2, rgba(0xFFFFFF, 70));
            }
            if (name.empty()) continue;
            float lx = x + 8;
            if (conquest && (f[0] == "0" || f[0] == "1")) {  // a town's name clears the flag of an army standing in it
                for (const Json& st : camp.list("stacks")) {
                    const float sx = static_cast<float>(st.num("x")), sy = static_cast<float>(st.num("y"));
                    if (sy + 9 > y + 2 && sy - 11 < y + 15 && sx - 14 < lx + nw && sx + 14 > lx) lx = std::max(lx, sx + 16);
                }
            }
            if (lx + nw > w - 6 && x - 8 - nw >= 6) lx = x - 8 - nw;  // no room on the right: the left of the marker
            const float room = std::min({lx - 6, w - 6 - (lx + nw), stripTop - (y + 2 + size + 2), y + 2 - 36});
            if (room < 0) continue;
            if (miniW0_ > 0 && lx < miniX_ + miniW0_ + 6 && lx + nw > miniX_ - 6 && y + 2 < miniY_ + miniH0_ + 6 && y + 2 + size > miniY_ - 6) continue;  // (under the minimap)
            const float fade = std::min(1.0f, room / 24.0f);
            const auto alpha = [fade](uint8_t v) { return static_cast<uint8_t>(v * fade); };
            // names carry a dark shadow, so they read over forest, field and water alike
            const Colour ink = f[0] == "2" || f[0] == "7" ? kDim : kInk;
            static const float kOutline[5][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {1, 1}};
            for (const auto& d : kOutline) o.text(lx + d[0], y + 2 + d[1], name, rgba(0x000000, alpha(190)), size);
            o.text(lx, y + 2, name, {ink.r, ink.g, ink.b, alpha(ink.a)}, size);
        }
    }

    // The selection box being dragged.
    {
        float x0, y0, x1, y1;
        if (std::sscanf(illyria_drag_box(), "%f %f %f %f", &x0, &y0, &x1, &y1) == 4)
            o.frame(std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0), 1.5f, rgba(0xFFFFFF, 220));
    }

    if (conquest) {
        // Each army on the map: a flag with its strength, to click.
        const unsigned selected = static_cast<unsigned>(camp.num("selected"));
        for (const Json& s : camp.list("stacks")) {
            const float x = static_cast<float>(s.num("x")), y = static_cast<float>(s.num("y"));
            const unsigned id = static_cast<unsigned>(s.num("id"));
            const Colour c = hex(s.str("color"));
            // A small pennant on a pole: dark edge, the holder's colour, a lit top edge, the count centred (a star for a hero).
            std::snprintf(buf, sizeof buf, "%d%s", static_cast<int>(s.num("count")), s.flag("hero") ? "*" : "");
            const float tw = o.width(buf, 10);
            const float bw = std::max(16.0f, tw + 8);
            o.rect(x - 1, y - 8, 2, 16, rgba(0x000000, 200));  // the pole
            o.rect(x - bw / 2 - 1, y - 11, bw + 2, 16, rgba(0x000000, 210));
            o.rect(x - bw / 2, y - 10, bw, 14, c);
            o.rect(x - bw / 2, y - 10, bw, 2, rgba(0xFFFFFF, 80));
            const bool light = c.r + c.g + c.b > 380;
            o.text(x - tw / 2 + 0.5f, y - 8 + 0.5f, buf, light ? rgba(0xFFFFFF, 90) : rgba(0x000000, 130), 10);
            o.text(x - tw / 2, y - 8, buf, light ? rgba(0x000000) : rgba(0xFFFFFF), 10);
            if (id == selected) o.frame(x - bw / 2 - 3, y - 13, bw + 6, 20, 2, kGold);
            hits_.push_back({x - bw / 2 - 3, y - 13, bw + 6, 20, 1000 + static_cast<int>(id)});
        }
        for (const Json& b : camp.list("banners")) {
            std::snprintf(buf, sizeof buf, "%s %d%%", b.str("name").c_str(), static_cast<int>(b.num("p")));
            // A pill under the town's marker: the raiser's colour, a bar for how far the banner has risen, and the name's shadow.
            const float bx = static_cast<float>(b.num("x")), by = static_cast<float>(b.num("y")) - 26;
            const float bwid = o.width(buf, 11) + 12;
            const Colour bc = hex(b.str("byColor"));
            o.rect(bx - bwid / 2, by - 2, bwid, 17, rgba(0x000000, 170));
            o.rect(bx - bwid / 2, by + 13, bwid * static_cast<float>(std::clamp(b.num("p") / 100.0, 0.0, 1.0)), 2, bc);
            o.text(bx - bwid / 2 + 6, by, buf, bc, 11);
        }
    }

    // The selection and the squad cards, bottom.
    if (sel > 0) {
        std::snprintf(buf, sizeof buf, "%zu selected  %s  %s", sel, hud.str("selLabel").c_str(), hud.str("formation").c_str());
        o.rect(0, h - 98, o.width(buf, 13) + 20, 22, rgba(0x000000, 150));
        o.text(10, h - 94, buf, kGold, 13);
    }
    float x = 8;
    const Colour side = hex(hud.str("colour"));  // the player's colour (palette.h), the bars of the cards
    const float cw = 132, ch = 62, y = h - ch - 8;
    for (const Json& q : hud.list("squads")) {
        const int i = static_cast<int>(q.num("i"));
        const int sq = static_cast<int>(q.num("sel"));
        const bool mine = q.flag("mine");
        if (x + cw > w - 8) break;
        o.rect(x, y, cw, ch, rgba(0x0E0B08, 190));
        o.frame(x, y, cw, ch, sq == 2 ? 3.0f : 1.5f, sq ? kGold : mine ? rgba(0x7C6E50) : rgba(0x4A4A4A));
        // The name whole if it fits, else over two lines, else a step smaller, and only then cut (card_name.h).
        std::vector<std::string> nameLines;
        float nameSize = 10;
        for (; nameSize >= 9; nameSize -= 1)
            if (illyria::game::fitCardName(q.str("name"), [&](const std::string& t) { return o.width(t, nameSize); }, cw - 12, 2, nameLines)) break;
        if (nameSize < 9) nameSize = 9;
        float ny = y + 5;
        for (const std::string& l : nameLines) {
            o.text(x + 6, ny, l, mine ? kInk : kDim, nameSize);
            ny += 12;
        }
        std::snprintf(buf, sizeof buf, "%d/%d  %s", static_cast<int>(q.num("alive")), static_cast<int>(q.num("total")), q.str("role").c_str());
        o.text(x + 6, ny + 4, buf, kDim, 10);
        const double total = std::max(1.0, q.num("total"));
        o.rect(x + 6, y + ch - 14, (cw - 12) * static_cast<float>(q.num("alive") / total), 5, q.num("broken") ? kRed : side);
        hits_.push_back({x, y, cw, ch, -1 - i});
        x += cw + 6;
    }

    drawMinimap(o, w, h);

    // Looking at a man or gate that is not ours.
    if (const Json* in = hud.find("inspect")) {
        if (in->str("kind") == "gate") {
            const int swing = static_cast<int>(in->num("swing")), pin = static_cast<int>(in->num("pin"));
            const char* state = swing == 0 ? "shut" : swing == 1000 ? "open" : "swinging";
            if (in->flag("ours"))
                std::snprintf(buf, sizeof buf, "Your gate  %d / %d  %s%s  (O: open or shut, Shift+O: let go)", static_cast<int>(in->num("hp")), static_cast<int>(in->num("max")), state,
                              pin == 1 ? ", held open" : pin == 2 ? ", held shut" : "");
            else
                std::snprintf(buf, sizeof buf, "Gate  %d / %d%s", static_cast<int>(in->num("hp")), static_cast<int>(in->num("max")), in->flag("broken") ? "  broken" : "");
        }
        else
            std::snprintf(buf, sizeof buf, "%s  hp %d / %d  morale %d", in->str("type").c_str(), static_cast<int>(in->num("hp")), static_cast<int>(in->num("max")),
                          static_cast<int>(in->num("morale")));
        o.rect(w - o.width(buf, 13) - 20, h - 98, o.width(buf, 13) + 20, 22, rgba(0x000000, 150));
        o.text(w - o.width(buf, 13) - 10, h - 94, buf, kRed, 13);
    }

    if (help_) {
        // Two columns, each a title and rows of "keys  what they do", sized to the words.
        struct Row { const char* keys; const char* what; };
        struct Section { const char* title; std::vector<Row> rows; };
        static const Section kHelp[] = {
            {"Mouse",
             {{"Click", "select (again: all of his kind in view)"},
              {"Shift + click", "add or drop one"},
              {"Drag", "a box (Shift adds)"},
              {"Right click", "march there / attack what is there"},
              {"Right hold, drag", "march there, facing the drag"},
              {"Shift + right click", "queue it after the order in hand"},
              {"Ctrl or Cmd + click", "attack, even a friend"},
              {"Alt + click", "force march"},
              {"Right drag", "turn the view"},
              {"Middle drag", "pan"},
              {"Wheel", "zoom (trackpad: swipe pans, pinch zooms)"}}},
            {"Keys",
             {{"W A S D, arrows", "pan"},
              {"Q E, R F, N", "turn, zoom, north"},
              {"M", "show or hide the map (click or drag it to look)"},
              {"Z, V", "attack mode, force march (one order)"},
              {"Space, B", "stop the march (halt)"},
              {"T, X, C", "special, scatter, find selection"},
              {"O, Shift + O", "open or shut a gate of yours, let it go"},
              {"1-9, Alt + 1-9", "pick a group, save one"},
              {"Shift + 1-9, 0", "formation, scatter"},
              {"H, Ctrl + A", "heroes, all on screen"},
              {"Tab, Shift + Tab", "next, previous group of units"},
              {"`, Backspace", "plan: hold orders, send them / drop them"},
              {"Esc", "pause and bring up the menu (or cancel a mode)"},
              {"P, - =", "pause, slower and faster"},
              {"Enter, /", "menu, this help"},
              {"I", "show or hide the developer line (frame rate)"}}}};
        constexpr float kSize = 12, kLine = 18, kPad = 14, kGap = 14, kColGap = 28;
        float colW[2] = {};
        float keyW[2] = {};
        size_t rowsMax = 0;
        for (int c = 0; c < 2; ++c) {
            float actW = 0;
            for (const Row& r : kHelp[c].rows) {
                keyW[c] = std::max(keyW[c], o.width(r.keys, kSize));
                actW = std::max(actW, o.width(r.what, kSize));
            }
            colW[c] = keyW[c] + kGap + actW;
            rowsMax = std::max(rowsMax, kHelp[c].rows.size());
        }
        const float pw = std::min(w - 20, colW[0] + kColGap + colW[1] + 2 * kPad);
        const float ph = 2 * kPad + (static_cast<float>(rowsMax) + 1) * kLine + 4;
        const float x0 = w / 2 - pw / 2, y0 = 60;
        o.paper(x0, y0, pw, ph, 19);
        float x = x0 + kPad;
        for (int c = 0; c < 2; ++c) {
            o.text(x, y0 + kPad, kHelp[c].title, kSeal, kSize);
            float y = y0 + kPad + kLine + 4;
            for (const Row& r : kHelp[c].rows) {
                o.text(x, y, r.keys, kSeal, kSize);
                o.text(x + keyW[c] + kGap, y, r.what, kPaperInk, kSize);
                y += kLine;
            }
            x += colW[c] + kColGap;
        }
    }
}

}  // namespace illyria::native
