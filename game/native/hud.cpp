#include "hud.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "api.h"
#include "assets.h"
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

constexpr Colour kInk = rgba(0xF2EBD8), kDim = rgba(0xB9B0A0), kGold = rgba(0xE8C25A), kPanel = rgba(0x15110C, 205), kRed = rgba(0xFF7060);

Colour hex(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return rgba(0xCCCCCC);
    return rgba(static_cast<unsigned>(std::strtoul(s.c_str() + 1, nullptr, 16)));
}

std::string clip(const std::string& s, size_t n) { return s.size() <= n ? s : s.substr(0, n - 1) + "."; }

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

bool Hud::start(const std::string& mode, const std::string& options) {
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
            }
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

    const float ph = kPad + titleSize + 8 + 12 + 18 + kRow * kRows + 12 + leadSize + 9 + 4 * kLordRow + 8 +
                     static_cast<float>(hints.size()) * 16 + (errors.empty() ? 0 : 6 + static_cast<float>(errors.size()) * 17) + kPad - 4;
    const float px = (w - pw) / 2, py = std::max(10.0f, (h - ph) / 2);
    o.rect(px, py, pw, ph, kPanel);
    o.frame(px, py, pw, ph, 2, kGold);

    float y = py + kPad;
    o.text(px + kPad, y, title, kGold, titleSize);
    y += titleSize + 8;
    o.text(px + kPad, y, sub, kDim, 12);
    y += 12 + 18;
    std::string save;
    for (int r = 0; r < kRows; ++r, y += kRow) {
        const bool off = r == kContinue && !loadSave(save);
        const std::string label = (r == row_ ? "> " : "  ") + std::string(kLabel[r]);
        if (r == row_) o.rect(px + 12, y - 5, pw - 24, 28, rgba(0x6A5320, 160));
        o.text(px + kPad, y, label, off ? rgba(0x70695C) : kInk, fit(o, label, 18, inner));
        hits_.push_back({px + 12, y - 5, pw - 24, 28, r});
    }
    // The conquest's choices: who you are and who else is in the war.
    y += 12 - 5;
    o.text(px + kPad, y, leadLabel, kDim, leadSize);
    y += leadSize + 9;
    const float col = inner / 2;
    for (int i = 0; i < 8; ++i) {
        const float x = px + kPad + static_cast<float>(i % 2) * col, yy = y + static_cast<float>(i / 2) * kLordRow;
        const bool me = i == lord_, foe = !me && (foes_ >> i & 1);
        const std::string name = kLords[i].name;
        o.rect(x, yy + 2, 10, 10, me ? kGold : foe ? kRed : rgba(0x50483A));
        o.text(x + 18, yy, name, me ? kGold : foe ? kInk : kDim, fit(o, name, 13, col - 24));
        hits_.push_back({x, yy - 2, 16, 18, 100 + i});  // the square: lead as them
        hits_.push_back({x + 16, yy - 2, col - 20, 18, 200 + i});  // the name: in the war, or not
    }
    y += 4 * kLordRow + 8;
    for (size_t i = 0; i < hints.size(); ++i, y += 16) {
        o.text(px + kPad, y, hints[i], kDim, 12);
        if (i < difficultyLines) hits_.push_back({px + kPad, y - 3, inner, 18, 300});
    }
    if (!errors.empty()) {
        y += 6;
        for (const std::string& l : errors) o.text(px + kPad, y, l, kRed, 13), y += 17;
    }
}

void Hud::draw(Overlay& o, float w, float h) {
    hits_.clear();
    if (!notice_.empty() || menu_) o.text(6, 4, perf_, kGold, 10);  // (in play, the status bar carries it)
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

    // The status line.
    o.rect(0, 0, w, 34, rgba(0x000000, 150));
    o.text(6, 4, perf_, kGold, 10);
    if (conquest) {
        const int day = static_cast<int>(camp.num("day"));
        std::snprintf(buf, sizeof buf, "Conquest  Day %d   Gold %d   Towns %d   Land %d   %dx%s", day, static_cast<int>(camp.num("gold")),
                      static_cast<int>(camp.num("cities")), static_cast<int>(camp.num("land")), static_cast<int>(camp.num("speed")),
                      camp.flag("paused") ? " PAUSED" : "");
        if (day != savedDay_) {
            savedDay_ = day;
            saveConquest();
        }
        lastDay_ = day;
    } else {
        std::snprintf(buf, sizeof buf, "%s   %s%s%s", hud.num("scenario") == 1 ? "Siege" : "Skirmish", hud.flag("paused") ? "PAUSED   " : "",
                      hud.str("foe").empty() ? "" : ("v " + hud.str("foe")).c_str(), "");
    }
    o.text(6, 18, buf, kInk, 13);
    const std::string hint = "Enter: menu    /: keys";
    o.text(w - o.width(hint, 12) - 10, 18, hint, kDim, 12);

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
    {
        std::istringstream lines(illyria_labels());
        std::string line;
        while (std::getline(lines, line)) {
            std::vector<std::string> f;
            size_t from = 0;
            for (int i = 0; i < 4; ++i) {
                const size_t bar = line.find('|', from);
                if (bar == std::string::npos) break;
                f.push_back(line.substr(from, bar - from));
                from = bar + 1;
            }
            if (f.size() < 4) continue;
            const std::string name = line.substr(from);
            const float x = static_cast<float>(std::atof(f[2].c_str())), y = static_cast<float>(std::atof(f[3].c_str()));
            if (y < 38) continue;  // (under the status bar)
            const Colour c = hex(f[1]);
            if (f[0] == "0" || f[0] == "1" || f[0] == "7") o.rect(x - 4, y + 4, 8, 8, c);
            o.text(x + 8, y + 2, name, kInk, 11);
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
            o.rect(x - 9, y - 8, 18, 14, rgba(0x000000, 190));
            o.rect(x - 8, y - 7, 16, 12, c);
            std::snprintf(buf, sizeof buf, "%d%s", static_cast<int>(s.num("count")), s.flag("hero") ? "*" : "");
            o.text(x - 6, y - 5, buf, c.r + c.g + c.b > 380 ? rgba(0x000000) : rgba(0xFFFFFF), 9);
            if (id == selected) o.frame(x - 11, y - 10, 22, 18, 2, kGold);
            hits_.push_back({x - 11, y - 10, 22, 18, 1000 + static_cast<int>(id)});
        }
        for (const Json& b : camp.list("banners")) {
            std::snprintf(buf, sizeof buf, "%s %d%%", b.str("name").c_str(), static_cast<int>(b.num("p")));
            o.text(static_cast<float>(b.num("x")) - 30, static_cast<float>(b.num("y")) - 24, buf, hex(b.str("byColor")), 11);
        }
    }

    // The selection and the squad cards, bottom.
    const size_t sel = static_cast<size_t>(hud.num("selCount"));
    if (sel > 0) {
        std::snprintf(buf, sizeof buf, "%zu selected  %s  %s", sel, hud.str("selLabel").c_str(), hud.str("formation").c_str());
        o.rect(0, h - 98, o.width(buf, 13) + 20, 22, rgba(0x000000, 150));
        o.text(10, h - 94, buf, kGold, 13);
    }
    float x = 8;
    const float cw = 132, ch = 56, y = h - ch - 8;
    for (const Json& q : hud.list("squads")) {
        const int i = static_cast<int>(q.num("i"));
        const int sq = static_cast<int>(q.num("sel"));
        const bool mine = q.flag("mine");
        if (x + cw > w - 8) break;
        o.rect(x, y, cw, ch, rgba(0x0E0B08, 190));
        o.frame(x, y, cw, ch, sq == 2 ? 3.0f : 1.5f, sq ? kGold : mine ? rgba(0x7C6E50) : rgba(0x4A4A4A));
        o.text(x + 6, y + 5, clip(q.str("name"), 15), mine ? kInk : kDim, 10);
        std::snprintf(buf, sizeof buf, "%d/%d  %s", static_cast<int>(q.num("alive")), static_cast<int>(q.num("total")), q.str("role").c_str());
        o.text(x + 6, y + 22, buf, kDim, 10);
        const double total = std::max(1.0, q.num("total"));
        o.rect(x + 6, y + 42, (cw - 12) * static_cast<float>(q.num("alive") / total), 5, q.num("broken") ? kRed : rgba(0x5CB85C));
        hits_.push_back({x, y, cw, ch, -1 - i});
        x += cw + 6;
    }

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
              {"Ctrl or Cmd + click", "attack, even a friend"},
              {"Alt + click", "force march"},
              {"Right drag", "turn the view"},
              {"Middle drag", "pan"},
              {"Wheel", "zoom (trackpad: swipe pans, pinch zooms)"}}},
            {"Keys",
             {{"W A S D, arrows", "pan"},
              {"Q E, R F, N", "turn, zoom, north"},
              {"Z, V", "attack mode, force march (one order)"},
              {"T, X, B, C", "special, scatter, halt, find selection"},
              {"O, Shift + O", "open or shut a gate of yours, let it go"},
              {"1-9, Alt + 1-9", "pick a group, save one"},
              {"Shift + 1-9, 0", "formation, scatter"},
              {"`, Ctrl + A, Esc", "heroes, all on screen, let go"},
              {"Space, - =", "pause, slower and faster"},
              {"Enter, /", "menu, this help"}}}};
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
        o.rect(x0, y0, pw, ph, kPanel);
        o.frame(x0, y0, pw, ph, 2, kGold);
        float x = x0 + kPad;
        for (int c = 0; c < 2; ++c) {
            o.text(x, y0 + kPad, kHelp[c].title, kGold, kSize);
            float y = y0 + kPad + kLine + 4;
            for (const Row& r : kHelp[c].rows) {
                o.text(x, y, r.keys, kGold, kSize);
                o.text(x + keyW[c] + kGap, y, r.what, kInk, kSize);
                y += kLine;
            }
            x += colW[c] + kColGap;
        }
    }
}

}  // namespace illyria::native
