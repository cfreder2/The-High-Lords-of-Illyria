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

void Hud::openMenu() {
    menu_ = true;
    help_ = false;
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
        case Key::Back: menu_ = !playing_; break;  // (back to the game, if there is one)
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

void Hud::drawMenu(Overlay& o, float w, float h) {
    const float pw = 560, ph = 470, px = (w - pw) / 2, py = std::max(20.0f, (h - ph) / 2);
    o.rect(px, py, pw, ph, kPanel);
    o.frame(px, py, pw, ph, 2, kGold);
    o.text(px + 24, py + 20, "THE HIGH LORDS OF ILLYRIA", kGold, 28);
    o.text(px + 24, py + 56, std::string("v") + illyria_version() + "  -  desktop", kDim, 12);
    static const char* const kLabel[kRows] = {"Conquest of the island", "Skirmish: the Ford", "Skirmish: the siege", "Continue the conquest", "Quit"};
    std::string save;
    for (int r = 0; r < kRows; ++r) {
        const float y = py + 90 + static_cast<float>(r) * 34;
        const bool off = r == kContinue && !loadSave(save);
        if (r == row_) o.rect(px + 16, y - 4, pw - 32, 28, rgba(0x6A5320, 160));
        o.text(px + 28, y, (r == row_ ? "> " : "  ") + std::string(kLabel[r]), off ? rgba(0x70695C) : kInk, 18);
        hits_.push_back({px + 16, y - 4, pw - 32, 28, r});
    }
    // The conquest's choices: who you are and who else is in the war.
    float y = py + 270;
    o.text(px + 28, y, "You lead (left / right, or click a square):", kDim, 13);
    y += 22;
    for (int i = 0; i < 8; ++i) {
        const float x = px + 28 + static_cast<float>(i % 2) * 260, yy = y + static_cast<float>(i / 2) * 22;
        const bool me = i == lord_, foe = !me && (foes_ >> i & 1);
        o.rect(x, yy + 2, 10, 10, me ? kGold : foe ? kRed : rgba(0x50483A));
        o.text(x + 18, yy, std::string(kLords[i].name), me ? kGold : foe ? kInk : kDim, 13);
        hits_.push_back({x, yy - 2, 16, 18, 100 + i});  // the square: lead as them
        hits_.push_back({x + 16, yy - 2, 234, 18, 200 + i});  // the name: in the war, or not
    }
    y += 4 * 22 + 12;
    o.text(px + 28, y, std::string("Computer lords: keys 1-8 (red are in the war).  The lords play ") + (hard_ ? "Hard" : "Easy") + "  (D)", kDim, 12);
    hits_.push_back({px + 28, y - 4, pw - 56, 22, 300});
    o.text(px + 28, y + 24, "Up / down, Enter; or click.  In a battle: / shows the keys.", kDim, 12);
    if (!error_.empty()) o.text(px + 28, y + 48, error_, kRed, 13);
}

void Hud::draw(Overlay& o, float w, float h) {
    hits_.clear();
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
    o.rect(0, 0, w, 26, rgba(0x000000, 150));
    if (conquest) {
        const int day = static_cast<int>(camp.num("day"));
        std::snprintf(buf, sizeof buf, "Conquest  Day %d   Gold %d   Towns %d   Land %d   %dx%s   %s", day, static_cast<int>(camp.num("gold")),
                      static_cast<int>(camp.num("cities")), static_cast<int>(camp.num("land")), static_cast<int>(camp.num("speed")),
                      camp.flag("paused") ? " PAUSED" : "", illyria_perf());
        if (day != savedDay_) {
            savedDay_ = day;
            saveConquest();
        }
        lastDay_ = day;
    } else {
        std::snprintf(buf, sizeof buf, "%s   %s%s   %s", hud.num("scenario") == 1 ? "Siege" : "Skirmish", hud.flag("paused") ? "PAUSED   " : "",
                      hud.str("foe").empty() ? "" : ("v " + hud.str("foe")).c_str(), illyria_perf());
    }
    o.text(10, 6, buf, kInk, 14);
    const std::string hint = "Enter: menu    /: keys";
    o.text(w - o.width(hint, 12) - 10, 7, hint, kDim, 12);

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
        if (in->str("kind") == "gate")
            std::snprintf(buf, sizeof buf, "Gate  %d / %d%s", static_cast<int>(in->num("hp")), static_cast<int>(in->num("max")), in->flag("broken") ? "  broken" : "");
        else
            std::snprintf(buf, sizeof buf, "%s  hp %d / %d  morale %d", in->str("type").c_str(), static_cast<int>(in->num("hp")), static_cast<int>(in->num("max")),
                          static_cast<int>(in->num("morale")));
        o.rect(w - o.width(buf, 13) - 20, h - 98, o.width(buf, 13) + 20, 22, rgba(0x000000, 150));
        o.text(w - o.width(buf, 13) - 10, h - 94, buf, kRed, 13);
    }

    if (help_) {
        static const char* const kHelp[] = {
            "Left click: select / tap   Drag: box   Shift: add",
            "Right click: move / attack   Ctrl or Cmd + right click: attack ground",
            "Wheel / trackpad: zoom and pan   WASD, arrows: pan   Q E: turn   R F: zoom   N or Z: north",
            "G: attack mode   V: force march   T: special   X: scatter   B: halt   C: view selection",
            "1-9: groups (Alt + number saves one)   Shift + 1-9: formation   0: scatter",
            "` heroes   Ctrl+A: all on screen   Esc: let go   Space: pause   - =: slower, faster",
            "Enter: menu    /: this help"};
        const float hh = 26 + static_cast<float>(sizeof kHelp / sizeof *kHelp) * 20;
        o.rect(w / 2 - 330, 60, 660, hh, kPanel);
        o.frame(w / 2 - 330, 60, 660, hh, 2, kGold);
        for (size_t i = 0; i < sizeof kHelp / sizeof *kHelp; ++i) o.text(w / 2 - 316, 72 + static_cast<float>(i) * 20, kHelp[i], kInk, 12);
    }
}

}  // namespace illyria::native
