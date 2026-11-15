#include "hud.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "api.h"
#include "assets.h"
#include "audio.h"
#include "illyria/game/card_face.h"
#include "illyria/game/card_layout.h"
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

enum Row { kConquest, kSkirmish, kMarch, kContinue, kQuit, kRows };
// Under the menu's rows, the Sound block's four: Up / Down reach them, Left / Right (or a press on - / +) turn one by a tenth.
constexpr int kSoundRows = 4, kMenuRows = kRows + kSoundRows;
constexpr int kVolumeStep = 500, kVolumeRow = 520;  // hits: kVolumeStep + 2 row (less), + 1 (more); kVolumeRow + row (focus)
constexpr const char* kVolumeName[kSoundRows] = {"Master", "Effects", "Voices", "Ambience"};
constexpr const char* kVolumeKey[kSoundRows] = {"vol_master", "vol_sfx", "vol_voice", "vol_ambience"};
int tenths(int row) { return static_cast<int>(std::lround(audio::volume(static_cast<audio::Bus>(row)) * 10)); }

// The Skirmish screen's maps (the engine's scenario numbers) and purses.
struct SkirmishMap {
    const char* name;
    const char* note;
    bool siege;
};
constexpr SkirmishMap kMaps[4] = {{"Oakhaven Ford", "", false},
                                  {"A village siege", "", true},
                                  {"A capital siege", "", true},
                                  {"Open field", "", false}};
constexpr int kPurses[] = {100, 200, 300, 450, 600, 900, 1200};
constexpr int kPurseCount = sizeof kPurses / sizeof kPurses[0];
// What a press on the open town's screen is (Hud::click): from kTownAct up, the option's number, or one of these.
constexpr int kAfterBattle = 3000;  // + Hud::After
constexpr int kCardPageBack = -2000, kCardPageOn = -2001, kCardPageMore = -2002;  // the "+N more" cell: its arrows, and its middle (the next page, round)
constexpr int kRallyHit = -3000;  // the Rally button over the cards
constexpr int kLayHit = -3001, kDetonateHit = -3002, kHoldFireHit = -3003;  // the trap's buttons beside it
constexpr int kTownAct = 7000, kTownStop = 7100, kTownSend = 7101, kTownKeep = 7102, kTownClose = 7103, kTownBoat = 7104, kTownSwallow = 7105;
constexpr int kMarchSquad = 9000, kMarchFill = 9499, kMarchOn = 9500, kMarchGiveUp = 9501, kMarchBack = 9502;  // presses on the March's camp
// The camp's buttons under the roll: between battles the purse, the road, giving up; else the one way on.
std::vector<int> marchButtons(bool plain) { return plain ? std::vector<int>{kMarchFill, kMarchOn, kMarchGiveUp, kMarchBack} : std::vector<int>{kMarchOn, kMarchBack}; }
const char* const kRankNames[4] = {"recruits", "regulars", "veterans", "elite"};

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
    Json camp = read(illyria_conquest_state());
    if (camp.find("day")) return camp.flag("paused");
    return read(illyria_hud()).flag("paused");
}
static void togglePause() {
    if (read(illyria_conquest_state()).find("day")) illyria_conquest_pause();
    else illyria_toggle_pause();
}

// A game in play is held while its menu is up, and let go when it closes: nothing steps behind a menu.
void Hud::openMenu() {
    if (menu_) return;
    menu_ = true;
    help_ = false;
    audio::uiCue("snd_ui_page_turn");
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
    const std::string text = illyria_conquest_save();
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
        bool volume = false;
        for (int i = 0; i < kSoundRows; ++i)
            if (key == kVolumeKey[i]) audio::setVolume(static_cast<audio::Bus>(i), static_cast<float>(std::atof(value.c_str()))), volume = true;
        if (volume) continue;
        if (key == "minimap") minimapOn_ = value != "0";
        else if (key == "hint") hintSeen_ = value != "0";
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
    f << "hint=" << (hintSeen_ ? 1 : 0) << "\n";
    if (colour_ >= 0) f << "colour=" << illyria::game::palette::hex(illyria::game::palette::swatch(colour_).rgb) << "\n";
    for (int i = 0; i < kSoundRows; ++i) f << kVolumeKey[i] << "=" << tenths(i) / 10.0 << "\n";
}

// A volume of the Sound block a tenth up or down, kept at once.
void Hud::volumeStep(int row, int by) {
    if (row < 0 || row >= kSoundRows) return;
    audio::setVolume(static_cast<audio::Bus>(row), static_cast<float>(std::clamp(tenths(row) + by, 0, 10)) / 10.0f);
    saveSettings();
}

void Hud::toggleHelp() {
    help_ = !help_;
    if (help_) audio::uiCue("snd_ui_page_turn");
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
    notice_ = mode == "conquest" ? "Laying out the island" : "Setting out the battle";
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
            start("conquest", buf);
            break;
        case kSkirmish:
            skMarch_ = false;
            skirmishOpen();
            break;
        case kMarch: marchOpen(); break;
        case kContinue: {
            std::string save;
            if (!loadSave(save)) break;
            start("conquest", "{\"save\":\"" + save + "\",\"speed\":1}");
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

void Hud::startSaved() { activate(kContinue); }

bool Hud::deploying() const { return !menu_ && read(illyria_hud()).flag("deploy"); }
bool Hud::resultOpen() const { return !menu_ && read(illyria_hud()).find("result") != nullptr; }

void Hud::afterBattle(After what) {
    if (const Json hud = read(illyria_hud()); hud.find("result") && hud.find("result")->flag("conquest")) {
        illyria_result_dismiss();  // a Conquest's fight: the sheet is put away and the war goes on
        return;
    }
    if (read(illyria_hud()).find("march")) {  // a march's battle is fought once: on to its camp (or its end), or the menu
        if (what == After::ChangeArmy) return;  // (its army is bought once)
        openMenu();
        if (what == After::Again) marchOpen();
        return;
    }
    if (what == After::Again) {
        illyria_restart();
        return;
    }
    openMenu();
    if (what == After::ChangeArmy) {  // the Skirmish screen as it was left (or filled, if this battle did not come from it)
        if (offers_.empty()) skirmishOpen();
        skirmishPage_ = true;
    }
}

void Hud::saveMarch() const {
    if (marchPath_.empty()) return;
    const std::string text = illyria_march_save();
    if (text.empty()) return;
    std::ofstream(marchPath_, std::ios::binary | std::ios::trunc) << text;
}

void Hud::marchOpen() {
    error_.clear();
    if (!read(illyria_march_state()).find("stage")) {  // none in the book: the one kept on this device
        std::string text;
        if (!marchPath_.empty() && readText(marchPath_, text)) {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
            illyria_march_load(text.c_str());
        }
    }
    const Json st = read(illyria_march_state());
    if (!st.find("stage")) {  // no march: buy the army for one
        skMarch_ = true;
        skirmishOpen();
        return;
    }
    marchPage_ = true;
    audio::uiCue("snd_ui_page_turn");  // the camp's page
    skirmishPage_ = false;
    mRow_ = static_cast<int>(st.list("squads").size()) + (!st.flag("over") && !st.flag("fighting") ? 1 : 0);  // on "March on"
}

// What a row of the camp does: a squad is filled from the purse; then March on, Give up, Back.
void Hud::marchAct(int what) {
    const Json st = read(illyria_march_state());
    const bool over = st.flag("over"), fighting = st.flag("fighting");
    if (what >= kMarchSquad && what < kMarchOn) {
        if (over || fighting) return;
        if (illyria_march_reinforce(what == kMarchFill ? -1 : what - kMarchSquad) > 0) saveMarch();
        else error_ = st.num("purse") <= 0 ? "The purse is empty." : what == kMarchFill ? "No squad can be filled." : "That squad cannot be filled.";
    } else if (what == kMarchOn) {
        if (fighting) {  // (back to the battle being fought)
            marchPage_ = false;
            closeMenu();
        } else if (over) {  // the march is put away
            illyria_march_end();
            if (!marchPath_.empty()) std::remove(marchPath_.c_str());
            marchPage_ = false;
        } else {
            marchPage_ = false;
            start("march", "{\"deploy\":1}");
        }
    } else if (what == kMarchGiveUp) {
        if (over || fighting) return;
        illyria_march_end();
        if (!marchPath_.empty()) std::remove(marchPath_.c_str());
        marchPage_ = false;
    } else if (what == kMarchBack) {
        marchPage_ = false;
    }
}

void Hud::marchKey(Key k) {
    const Json st = read(illyria_march_state());
    const int squads = static_cast<int>(st.list("squads").size());
    const bool plain = !st.flag("over") && !st.flag("fighting");
    const std::vector<int> buttons = marchButtons(plain);
    const int rows = squads + static_cast<int>(buttons.size());
    error_.clear();
    switch (k) {
        case Key::Up: mRow_ = (mRow_ + rows - 1) % rows; break;
        case Key::Down: mRow_ = (mRow_ + 1) % rows; break;
        case Key::Left: break;
        case Key::Right:
        case Key::Enter:
            if (mRow_ < squads) {
                if (k == Key::Enter || plain) marchAct(kMarchSquad + mRow_);
            } else if (k == Key::Enter && mRow_ < rows) {
                marchAct(buttons[static_cast<size_t>(mRow_ - squads)]);
            }
            break;
        case Key::Back: marchPage_ = false; break;
    }
}

void Hud::skirmishOpen() {
    if (offers_.empty()) {
        Json list;
        Json::parse(illyria_skirmish_offers(), list);
        for (const Json& j : list.items) offers_.push_back({static_cast<int>(j.num("type")), j.str("name"), static_cast<int>(j.num("price")), static_cast<int>(j.num("men")), static_cast<int>(j.num("cap"))});
        bought_.assign(offers_.size(), 0);
    }
    skirmishPage_ = true;
    skRow_ = 0;
    skirmishFill();
}

void Hud::skirmishFill() {
    bought_.assign(offers_.size(), 0);
    std::istringstream army(illyria_skirmish_buy(kPurses[skGold_], 1));
    for (std::string item; std::getline(army, item, ',');) {
        const size_t colon = item.find(':');
        if (colon == std::string::npos) continue;
        for (size_t i = 0; i < offers_.size(); ++i)
            if (offers_[i].type == std::atoi(item.c_str())) bought_[i] = std::atoi(item.c_str() + colon + 1);
    }
}

int Hud::skirmishSpent() const {
    int gold = 0;
    for (size_t i = 0; i < offers_.size(); ++i) gold += bought_[i] * offers_[i].price;
    return gold;
}

void Hud::skirmishStep(int row, int by) {
    const int units = static_cast<int>(offers_.size());
    if (skMarch_ && row < 2) return;  // (a march's five battles and their sides are the march's)
    if (row == 0) {
        skMap_ = (skMap_ + by + 4) % 4;
        if (!kMaps[skMap_].siege) skDefend_ = false;
    } else if (row == 1) {
        if (kMaps[skMap_].siege) skDefend_ = !skDefend_;
    } else if (row == 2) {
        skGold_ = std::clamp(skGold_ + by, 0, kPurseCount - 1);
        skirmishFill();
    } else if (row == 3) {
        skSkill_ = (skSkill_ + (by < 0 ? 2 : 1)) % 3;
    } else if (row < 4 + units) {
        const size_t i = static_cast<size_t>(row - 4);
        if (by > 0 && skirmishSpent() + offers_[i].price <= kPurses[skGold_] && (offers_[i].cap == 0 || bought_[i] < offers_[i].cap)) ++bought_[i];
        else if (by < 0 && bought_[i] > 0) --bought_[i];
    }
}

void Hud::skirmishStart() {
    std::string army;
    for (size_t i = 0; i < offers_.size(); ++i)
        if (bought_[i] > 0) army += (army.empty() ? "" : ",") + std::to_string(offers_[i].type) + ":" + std::to_string(bought_[i]);
    if (army.empty()) {
        error_ = "Buy at least one squad.";
        return;
    }
    char buf[400];
    skirmishPage_ = false;
    if (skMarch_) {
        std::snprintf(buf, sizeof buf, "{\"hard\":%d,\"deploy\":1,\"army\":\"%s\"}", hard_ ? 1 : 0, army.c_str());
        marchSaved_ = -1;
        start("march", buf);
        return;
    }
    std::snprintf(buf, sizeof buf, "{\"scenario\":%d,\"budget\":%d,\"defend\":%d,\"hard\":%d,\"deploy\":1,\"army\":\"%s\"}", skMap_, kPurses[skGold_], skDefend_ ? 1 : 0, skSkill_, army.c_str());
    start("skirmish", buf);
}

void Hud::skirmishKey(Key k) {
    const int rows = skirmishRows(), units = static_cast<int>(offers_.size());
    error_.clear();
    switch (k) {
        case Key::Up: skRow_ = (skRow_ + rows - 1) % rows; break;
        case Key::Down: skRow_ = (skRow_ + 1) % rows; break;
        case Key::Left: skirmishStep(skRow_, -1); break;
        case Key::Right: skirmishStep(skRow_, 1); break;
        case Key::Enter:
            if (skRow_ == 4 + units) skirmishStart();
            else if (skRow_ == 5 + units) skirmishPage_ = false;
            else skirmishStep(skRow_, 1);
            break;
        case Key::Back: skirmishPage_ = false; break;
    }
}

void Hud::menuKey(Key k) {
    if (!menu_) return;
    if (k != Key::Back || playing_ || skirmishPage_ || marchPage_) audio::uiCue("snd_ui_click");
    if (marchPage_) return marchKey(k);
    if (skirmishPage_) return skirmishKey(k);
    const bool sound = row_ >= kRows;  // on a row of the Sound block: left / right turn it
    switch (k) {
        case Key::Up: row_ = (row_ + kMenuRows - 1) % kMenuRows; break;
        case Key::Down: row_ = (row_ + 1) % kMenuRows; break;
        case Key::Left: if (sound) volumeStep(row_ - kRows, -1); else lord_ = (lord_ + 7) % 8, foes_ &= ~(1 << lord_); break;
        case Key::Right: if (sound) volumeStep(row_ - kRows, 1); else lord_ = (lord_ + 1) % 8, foes_ &= ~(1 << lord_); break;
        case Key::Enter: activate(row_); break;
        case Key::Back: if (playing_) closeMenu(); break;  // (back to the game, if there is one)
    }
}

void Hud::menuDigit(int digit) {
    if (!menu_ || digit < 1 || digit > 8 || digit - 1 == lord_) return;
    foes_ ^= 1 << (digit - 1);
}

bool Hud::wheel(float x, float y, float notches) {
    if (menu_ || !stripPaged_ || x < stripX_ || y < stripY_ || x >= stripX_ + stripW_ || y >= stripY_ + stripH_ || notches == 0) return false;
    cardPage_ = std::clamp(cardPage_ + (notches > 0 ? -1 : 1), 0, std::max(0, cardPages_ - 1));  // (a roll away from you: back a page)
    return true;
}

// A press on the open town's screen: the same calls the page's panel makes (game/web/index.html, the city panel).
void Hud::townAction(int what) {
    if (town_ < 0) return;
    if (what >= kTownAct && what < kTownStop) illyria_conquest_produce(town_, what - kTownAct);
    else if (what == kTownStop) illyria_conquest_produce(town_, -1);
    else if (what == kTownSend) illyria_conquest_vector(town_, 1);
    else if (what == kTownKeep) illyria_conquest_vector(town_, 0);
    else if (what == kTownBoat) illyria_conquest_build_boat(town_);
    else if (what == kTownClose) illyria_conquest_close_city();
}

bool Hud::click(float x, float y, bool left) {
    for (const Rect& r : hits_) {
        if (x < r.x || y < r.y || x >= r.x + r.w || y >= r.y + r.h) continue;
        if (r.what >= 1000 && !left) continue;  // (a right click on a town, or on the flag of the men in it, is an order, not a tap)
        if (menu_) {
            if (marchPage_) {
                if (r.what >= kMarchSquad && r.what <= kMarchBack) marchAct(r.what);
            } else if (skirmishPage_ && r.what >= 600 && r.what < 800) {  // a row of the Skirmish screen (the last two are Start and Back)
                skRow_ = r.what - 600;
                const int units = static_cast<int>(offers_.size());
                if (skRow_ == 4 + units) skirmishStart();
                else if (skRow_ == 5 + units) skirmishPage_ = false;
            } else if (skirmishPage_ && r.what >= 800 && r.what < 1000) {  // an arrow of a row: 800 + 2 row (less), 801 + 2 row (more)
                skRow_ = (r.what - 800) / 2;
                skirmishStep(skRow_, (r.what - 800) % 2 ? 1 : -1);
            } else if (r.what >= 0 && r.what < kRows) {
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
            } else if (r.what >= kVolumeStep && r.what < kVolumeStep + 2 * kSoundRows) {
                row_ = kRows + (r.what - kVolumeStep) / 2;
                volumeStep(row_ - kRows, (r.what - kVolumeStep) % 2 ? 1 : -1);
            } else if (r.what >= kVolumeRow && r.what < kVolumeRow + kSoundRows) {
                row_ = kRows + r.what - kVolumeRow;
            }
        } else if (r.what == 2000) {
            lookAt(x, y);
            return true;
        } else if (r.what == kCardPageBack || r.what == kCardPageOn || r.what == kCardPageMore) {  // the "+N more" cell of a long strip of cards
            illyria::game::CardLayout strip;
            strip.pages = cardPages_;
            using Hit = illyria::game::CardHit;
            cardPage_ = illyria::game::cardPageAfter(strip, cardPage_, r.what == kCardPageBack ? Hit::Back : r.what == kCardPageOn ? Hit::On : Hit::More);
        } else if (r.what == kLayHit) {
            illyria_special();
        } else if (r.what == kDetonateHit) {
            illyria_detonate();
        } else if (r.what == kHoldFireHit) {
            illyria_hold_fire();
        } else if (r.what == kRallyHit) {
            illyria_rally();
        } else if (r.what <= -1 && r.what > -1000) {
            illyria_select_squad(-1 - r.what);
        } else if (r.what >= kAfterBattle && r.what < kAfterBattle + 3) {
            afterBattle(static_cast<After>(r.what - kAfterBattle));
        } else if (r.what >= kTownAct) {
            townAction(r.what);
        } else if (r.what >= 5000) {
            illyria_conquest_open_city(r.what - 5000);
        } else if (r.what >= 1000) {
            illyria_conquest_select(static_cast<unsigned>(r.what - 1000));
        }
        if (r.what != kTownSwallow) audio::uiCue("snd_ui_click");  // a press the HUD took: a row, a card, a button
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
    if (marchPage_) return drawMarch(o, w, h);
    if (skirmishPage_) return drawSkirmish(o, w, h);
    constexpr float kPad = 24, kRow = 34, kLordRow = 22;
    const float pw = std::min(580.0f, w - 20), inner = pw - 2 * kPad;
    static const char* const kLabel[kRows] = {"Conquest of the island", "Skirmish: set up a battle", "The March: five battles, one army", "Continue the Conquest", "Quit"};

    const std::string title = "THE HIGH LORDS OF ILLYRIA";
    const float titleSize = fit(o, title, 28, inner);
    const std::string sub = std::string("v") + illyria_version() + " - desktop";
    const std::string leadLabel = "You lead (left / right, or click a square):";
    const float leadSize = fit(o, leadLabel, 13, inner);
    const std::string difficulty = std::string("Computer lords: keys 1-8 toggle them (red are in the war): ") + (hard_ ? "Hard" : "Easy") + " (D).";
    std::vector<std::string> hints = wrap(o, difficulty, 12, inner);
    const size_t difficultyLines = hints.size();
    for (const std::string& l : wrap(o, "Up / down, Enter, or click. In a battle, / shows the keys.", 12, inner)) hints.push_back(l);
    const std::vector<std::string> errors = wrap(o, error_, 13, inner);

    const std::string colourLabel = "Your colour (C, or click):";
    const float colourSize = fit(o, colourLabel, 13, inner);
    constexpr float kSwatch = 24, kVolume = 20;
    const float soundH = 13 + 6 + kVolume * kSoundRows + 8;
    const float ph = soundH + kPad + titleSize + 8 + 12 + 18 + kRow * kRows + 12 + leadSize + 9 + 4 * kLordRow + 8 + colourSize + 8 + kSwatch + 14 +
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
        if (off) {  // (why it is grey)
            const std::string why = "No saved Conquest yet";
            o.text(px + kPad + o.width(label, 18) + 14, y + 5, why, kPaperDim, 12);
        }
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
    // The sound: four volumes, each ten pips between a - and a +.
    o.text(px + kPad, y, "Sound (- / +, or left / right on a row):", kPaperDim, fit(o, "Sound (- / +, or left / right on a row):", 13, inner));
    y += 13 + 6;
    for (int i = 0; i < kSoundRows; ++i, y += kVolume) {
        const bool at = row_ == kRows + i;
        const int level = tenths(i);
        if (at) {
            o.rect(px + 12, y - 2, pw - 24, kVolume, rgba(0xB89A5A, 120));
            o.frame(px + 12, y - 2, pw - 24, kVolume, 1.5f, kSeal);
        }
        o.text(px + kPad, y + 1, kVolumeName[i], at ? kSeal : kPaperInk, 13);
        const float pip = std::clamp((inner - 96 - 2 * 24 - 48) / 10, 6.0f, 14.0f);
        const float less = px + kPad + 96, pips = less + 24, more = pips + pip * 10 + 4;
        o.frame(less, y, 18, 16, 1.2f, kPaperInk);
        o.text(less + 6, y + 1, "-", kPaperInk, 13);
        for (int k = 0; k < 10; ++k) {
            o.rect(pips + pip * static_cast<float>(k), y + 3, pip - 3, 10, k < level ? kSeal : rgba(0xCDB988));
            o.frame(pips + pip * static_cast<float>(k), y + 3, pip - 3, 10, 1, kPaperOut);
        }
        o.frame(more, y, 18, 16, 1.2f, kPaperInk);
        o.text(more + 5, y + 1, "+", kPaperInk, 13);
        o.text(more + 26, y + 1, std::to_string(level * 10) + "%", kPaperDim, 12);
        hits_.push_back({less - 3, y - 2, 24, kVolume, kVolumeStep + 2 * i});
        hits_.push_back({more - 3, y - 2, 24, kVolume, kVolumeStep + 2 * i + 1});
        hits_.push_back({px + 12, y - 2, pw - 24, kVolume, kVolumeRow + i});
    }
    y += 8;
    for (size_t i = 0; i < hints.size(); ++i, y += 16) {
        o.text(px + kPad, y, hints[i], kPaperDim, 12);
        if (i < difficultyLines) hits_.push_back({px + kPad, y - 3, inner, 18, 300});
    }
    if (!errors.empty()) {
        y += 6;
        for (const std::string& l : errors) o.text(px + kPad, y, l, kSeal, 13), y += 17;
    }
}

// What the computer does at each setting, under the Skirmish rows (Battle::kEasyPursePct, kEasyWaitTicks, aiFight).
constexpr const char* kSkillLine = "Easy: less gold, late and piecemeal. Normal: your equal; bows first, then one wing. Hard: a quarter more gold, and quicker.";

// The Skirmish screen, on the same parchment: a Conquest with a fence, so the army is bought at the Conquest's prices.
void Hud::drawSkirmish(Overlay& o, float w, float h) {
    constexpr float kPad = 24, kRow = 21;
    const float pw = std::min(620.0f, w - 20), inner = pw - 2 * kPad;
    const int units = static_cast<int>(offers_.size()), rows = skirmishRows();
    const std::vector<std::string> errors = wrap(o, error_, 13, inner);
    constexpr float kSwatch = 22;
    const float ph = kPad + 26 + 10 + kRow * static_cast<float>(rows) + 4 + 48 + 16 + 4 + 13 + 8 + kSwatch + 12 + 16 + (errors.empty() ? 0 : 6 + static_cast<float>(errors.size()) * 17) + kPad - 4;
    const float px = (w - pw) / 2, py = std::max(6.0f, (h - ph) / 2);
    o.paper(px, py, pw, ph, 7);
    float y = py + kPad;
    o.text(px + kPad, y, skMarch_ ? "THE MARCH" : "SKIRMISH", kSeal, 24);
    y += 26 + 10;
    const SkirmishMap& map = kMaps[skMap_];
    const int purse = kPurses[skGold_], left = purse - skirmishSpent();
    const std::string side = !map.siege ? "" : skDefend_ ? "Defend" : "Attack";
    for (int r = 0; r < rows; ++r, y += kRow) {
        std::string label, value, info;
        bool arrows = r < 4 + units, dim = false;
        if (skMarch_ && r == 0) label = "Battles", value = "Field, village, Ford, defence, capital", arrows = false;
        else if (skMarch_ && r == 1) label = "Army", value = "bought once; the living go on", arrows = false;
        else if (r == 0) label = "Map", value = map.name;
        else if (r == 1) label = "Side", value = side, dim = !map.siege;
        else if (r == 2) label = "Gold", value = std::to_string(purse);
        else if (r == 3) label = "Computer plays", value = skSkill_ == 0 ? "Easy" : skSkill_ == 1 ? "Normal" : "Hard";
        else if (r < 4 + units) {
            const Offer& of = offers_[static_cast<size_t>(r - 4)];
            label = of.name;
            info = std::to_string(of.men) + (of.men == 1 ? " man, " : " men, ") + std::to_string(of.price) + " gold" + (of.cap > 0 ? ", " + std::to_string(of.cap) + " at most" : "");
            value = std::to_string(bought_[static_cast<size_t>(r - 4)]);
        } else label = r == 4 + units ? (skMarch_ ? "Begin the march" : "Start the battle") : "Back";
        const bool on = r == skRow_;
        if (on) {
            o.rect(px + 12, y - 3, pw - 24, kRow, rgba(0xB89A5A, 120));
            o.frame(px + 12, y - 3, pw - 24, kRow, 1.5f, kSeal);
        }
        const Colour ink = dim ? kPaperOut : on ? kSeal : kPaperInk;
        const float split = px + kPad + inner * (info.empty() ? 0.40f : 0.74f);
        o.text(px + kPad, y, label, ink, fit(o, label, 14, inner * 0.38f));
        if (!info.empty()) o.text(px + kPad + inner * 0.40f, y + 1, info, kPaperDim, 12);
        if (!arrows && !value.empty()) o.text(split, y, value, kPaperDim, fit(o, value, 13, px + pw - kPad - split));
        if (arrows) {
            o.text(split, y, "<", kPaperDim, 14);
            o.text(split + 18, y, value, ink, fit(o, value, 14, px + pw - kPad - 20 - split - 18));
            o.text(px + pw - kPad - 8, y, ">", kPaperDim, 14);
            hits_.push_back({split - 4, y - 3, 18, kRow, 800 + 2 * r});
            hits_.push_back({px + pw - kPad - 12, y - 3, 18, kRow, 801 + 2 * r});
        }
        hits_.push_back({px + 12, y - 3, pw - 24, kRow, 600 + r});
    }
    y += 4;
    const std::string purseLine = skMarch_ ? "Gold left " + std::to_string(left) + " of " + std::to_string(purse) + ". A lord and three magi march with you. They, and dwarves, are never replaced."
                                           : std::string(map.name) + ": gold left " + std::to_string(left) + " of " + std::to_string(purse) + ".";
    for (const std::string& l : wrap(o, purseLine, 12, inner)) o.text(px + kPad, y, l, kPaperDim, 12), y += 16;
    o.text(px + kPad, y, kSkillLine, kPaperDim, fit(o, kSkillLine, 12, inner));  // what Easy and Normal do, in one line
    y += 16;
    y += 4;
    o.text(px + kPad, y, "Your colour (click):", kPaperDim, 13);
    y += 13 + 8;
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
        y += kSwatch + 12;
    }
    o.text(px + kPad, y, "Up / down, left / right to change, Enter, Esc to go back.", kPaperDim, 12);
    y += 16;
    if (!errors.empty()) {
        y += 6;
        for (const std::string& l : errors) o.text(px + kPad, y, l, kSeal, 13), y += 17;
    }
}

// The March's camp between battles, and its end (illyria_march_state): the battle to come, the purse, every squad with
// its men, rank, kills and battles and what filling it costs, the squads gone; then March on, Give up, Back.
void Hud::drawMarch(Overlay& o, float w, float h) {
    const Json st = read(illyria_march_state());
    const std::vector<Json>& squads = st.list("squads");
    const bool over = st.flag("over"), won = st.flag("won"), fighting = st.flag("fighting"), plain = !over && !fighting;
    const int stage = static_cast<int>(st.num("stage")), of = static_cast<int>(st.num("of")), purse = static_cast<int>(st.num("purse"));
    constexpr float kPad = 24, kRow = 19;
    const float pw = std::min(680.0f, w - 20), inner = pw - 2 * kPad;
    char buf[200];
    std::vector<std::string> head;  // under the title
    if (won) head.push_back("All five battles are won. These came through:");
    else if (over) head.push_back("The march ended at battle " + std::to_string(std::min(stage + 1, of)) + " of " + std::to_string(of) + ": " + st.list("stages")[static_cast<size_t>(std::min(stage, of - 1))].string + ".");
    else head.push_back("Battle " + std::to_string(stage + 1) + " of " + std::to_string(of) + ": " + st.str("next") + (fighting ? " (being fought)" : ""));
    // (The last battle's toll is told in the session that fought it: a march carried on from its save has none to tell.)
    if (const Json* last = st.find("last"); last && (stage > 0 || over) && last->num("survivors") + last->num("lost") > 0) {
        std::snprintf(buf, sizeof buf, "Last battle: %d lost, %d came through, %d promoted.", static_cast<int>(last->num("lost")), static_cast<int>(last->num("survivors")), static_cast<int>(last->num("promoted")));
        head.push_back(buf);
    }
    if (plain) {
        std::snprintf(buf, sizeof buf, "Purse: %d gold. Enter on a squad fills its gaps with recruits.", purse);
        head.push_back(buf);
    }
    std::string goneLine;
    for (const Json& g : st.list("gone")) goneLine += (goneLine.empty() ? "Gone for good: " : ", ") + g.string;
    const std::vector<std::string> gone = wrap(o, goneLine, 12, inner), errors = wrap(o, error_, 13, inner);
    const std::vector<int> acts = marchButtons(plain);
    const int buttons = static_cast<int>(acts.size());
    mRows_ = static_cast<int>(squads.size()) + buttons;
    mRow_ = std::clamp(mRow_, 0, mRows_ - 1);
    const float ph = kPad + 26 + 8 + static_cast<float>(head.size()) * 18 + 8 + 18 + kRow * static_cast<float>(squads.size()) + 8 + static_cast<float>(gone.size()) * 16 + 10 + 30 * static_cast<float>(buttons) + 18 +
                     (errors.empty() ? 0 : static_cast<float>(errors.size()) * 17) + kPad - 6;
    const float px = (w - pw) / 2, py = std::max(6.0f, (h - ph) / 2);
    o.paper(px, py, pw, ph, 53);
    float y = py + kPad;
    o.text(px + kPad, y, won ? "THE MARCH IS WON" : over ? "THE MARCH IS OVER" : "THE MARCH", won ? kLeaf : kSeal, 24);
    y += 26 + 8;
    for (const std::string& l : head) o.text(px + kPad, y, l, kPaperInk, fit(o, l, 14, inner)), y += 18;
    y += 8;
    // The roll: squad, men, rank, kills, battles, and what filling it costs.
    const float cMen = px + kPad + inner * 0.33f, cRank = px + kPad + inner * 0.48f, cKills = px + kPad + inner * 0.68f, cBattles = px + kPad + inner * 0.78f, cFill = px + kPad + inner * 0.90f;
    o.text(px + kPad, y, "Squad", kSeal, 12);
    o.text(cMen, y, "Men", kSeal, 12);
    o.text(cRank, y, "Rank", kSeal, 12);
    o.text(cKills, y, "Kills", kSeal, 12);
    o.text(cBattles, y, "Battles", kSeal, 12);
    if (plain) o.text(cFill, y, "Fill", kSeal, 12);
    o.line(px + kPad, y + 15, px + pw - kPad, y + 15, 1, kPaperOut);
    y += 18;
    for (size_t i = 0; i < squads.size(); ++i, y += kRow) {
        const Json& q = squads[i];
        const bool on = static_cast<int>(i) == mRow_;
        if (on) {
            o.rect(px + 12, y - 3, pw - 24, kRow, rgba(0xB89A5A, 120));
            o.frame(px + 12, y - 3, pw - 24, kRow, 1.5f, kSeal);
        }
        const Colour ink = on ? kSeal : kPaperInk;
        std::string name = q.str("name");
        // (What it carries of its satchels and missiles, of a full load: made up with the same key, for gold.)
        if (q.num("loadFull") > 0) name += "  (" + std::to_string(static_cast<int>(q.num("load"))) + " of " + std::to_string(static_cast<int>(q.num("loadFull"))) + " left)";
        o.text(px + kPad, y, name, ink, fit(o, name, 13, cMen - px - kPad - 8));
        std::snprintf(buf, sizeof buf, "%d / %d", static_cast<int>(q.num("men")), static_cast<int>(q.num("full")));
        o.text(cMen, y, buf, q.num("men") < q.num("full") ? kSeal : ink, 13);
        const int rank = static_cast<int>(q.num("rank")), veterans = static_cast<int>(q.num("veterans"));
        std::string rk = q.str("type") == "Hero" ? "level " + std::to_string(rank + 1) : kRankNames[std::clamp(rank, 0, 3)];
        if (q.str("type") != "Hero" && veterans > 0) rk = std::to_string(veterans) + (veterans == 1 ? " veteran" : " veterans");
        o.text(cRank, y, rk, rank >= 2 ? kLeaf : kPaperDim, 12);
        o.text(cKills, y, std::to_string(static_cast<int>(q.num("kills"))), ink, 13);
        o.text(cBattles, y, std::to_string(static_cast<int>(q.num("battles"))), ink, 13);
        if (plain) {
            const int cost = static_cast<int>(q.num("cost"));
            const std::string fill = q.flag("fixed") ? (q.num("men") < q.num("full") ? "never" : "") : cost > 0 ? std::to_string(cost) + " g" : "";
            o.text(cFill, y, fill, cost > purse || q.flag("fixed") ? kPaperOff : kPaperInk, 13);
        }
        hits_.push_back({px + 12, y - 3, pw - 24, kRow, kMarchSquad + static_cast<int>(i)});
    }
    y += 8;
    for (const std::string& l : gone) o.text(px + kPad, y, l, kSeal, 12), y += 16;
    y += 10;
    const char* const first = fighting ? "Back to the battle" : over ? "Put the march away" : "March on";
    for (int b = 0; b < buttons; ++b, y += 30) {
        const int what = acts[static_cast<size_t>(b)];
        const char* const label = what == kMarchFill ? "Fill what the purse can" : what == kMarchOn ? first : what == kMarchGiveUp ? "Give up the march" : "Back";
        const bool on = mRow_ == static_cast<int>(squads.size()) + b;
        o.rect(px + kPad, y, inner, 25, on ? rgba(0xB89A5A, 120) : rgba(0x2B1E10, 22));
        o.frame(px + kPad, y, inner, 25, 1.5f, on ? kSeal : kPaperOut);
        o.text(px + kPad + 10, y + 5, label, on ? kSeal : kPaperInk, 14);
        hits_.push_back({px + kPad, y, inner, 25, what});
    }
    o.text(px + kPad, y + 2, "Up / down, Enter, Esc to go back.", kPaperDim, 12);
    y += 18;
    for (const std::string& l : errors) o.text(px + kPad, y, l, kSeal, 13), y += 17;
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
    for (const auto& f : rows) {  // the pole fought for: a mast, the flag on it as high as it has climbed, in its side's colour
        if (f[0] != "p" || f.size() < 5) continue;
        const float sx = X(at(f, 1)), sy = Y(at(f, 2)), up = 10.0f, raised = std::clamp(at(f, 4) / 100.0f, 0.0f, 1.0f);
        o.rect(sx - 1.4f, sy - up - 1, 2.8f, up + 2, ink);
        o.rect(sx - 0.6f, sy - up, 1.2f, up, rgba(0xF4EAD0));
        if (raised > 0) {
            const float fy = sy - 4.0f - (up - 4.0f) * raised;
            o.rect(sx + 0.2f, fy - 0.8f, 7.6f, 5.6f, ink);
            o.rect(sx + 1.0f, fy, 6.0f, 4.0f, hex(f[3]));
        }
    }
    for (const auto& f : rows) {  // a call for help: a white ring that beats
        if (f[0] != "c" || f.size() < 3) continue;
        const float r = 5.0f + 3.0f * static_cast<float>(std::fmod(SDL_GetTicks() / 500.0, 1.0)), sx = X(at(f, 1)), sy = Y(at(f, 2));
        o.frame(sx - r, sy - r, 2 * r, 2 * r, 1.5f, rgba(0xFFFFFF, 235));
        o.frame(sx - r - 1.5f, sy - r - 1.5f, 2 * r + 3, 2 * r + 3, 1, rgba(0xC8281E, 235));
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

// The open town on parchment, left of the field: who stands in it, what it earns, what it trains and at what price, where
// its new armies go. The numbers are the page's panel's (illyria_conquest_state's "city"); a press is one of the page's calls.
void Hud::drawTown(Overlay& o, const Json& city, int gold, float w) {
    constexpr float kSize = 12, kLine = 18, kPad = 14, kRow = 22;
    const bool mine = city.flag("mine");
    const float pw = std::min(w - 20, 430.0f);
    const float inner = pw - 2 * kPad;
    town_ = static_cast<int>(city.num("index"));
    char buf[160];
    // What goes on the sheet, top to bottom: a line of text, or a button row (left text, right text, what a press is).
    struct Line {
        std::string left, right;
        Colour ink;
        int what;  // -1: text; else a press
        bool on;   // the town is training it
        bool dim;  // cannot be afforded
    };
    std::vector<Line> lines;
    const auto text = [&](const std::string& t, Colour ink = kPaperInk) {
        for (const std::string& l : wrap(o, t, kSize, inner)) lines.push_back({l, "", ink, -1, false, false});
    };
    std::snprintf(buf, sizeof buf, "Walls %d  -  %d gold a day  -  treasury %d", static_cast<int>(city.num("defense")), static_cast<int>(city.num("income")), gold);
    text(buf, kPaperDim);
    const int banner = static_cast<int>(city.num("banner"));
    if (banner >= 0) {
        std::snprintf(buf, sizeof buf, "%s over %s: %d%%", banner == 0 ? "Your banner" : (city.str("bannerBy") + "'s banner").c_str(), city.str("name").c_str(), static_cast<int>(city.num("bannerP")));
        text(buf, kSeal);
        text(banner == 0 ? "Stay in the town until it is up; leaving gives it up." : "Drive them out before it is up. The town trains nothing while they hold it.", kPaperDim);
    }
    if (!mine) {
        text("Not yet yours.", kPaperDim);
    } else {
        text("In the town", kSeal);
        const std::vector<Json>& garrison = city.list("garrison");
        if (garrison.empty()) text("Nobody. Train armies below.", kPaperDim);
        for (const Json& g : garrison) {
            int men = 0, armies = 0;
            for (const Json& a : g.list("armies")) men += static_cast<int>(a.num("men")), ++armies;
            std::snprintf(buf, sizeof buf, "%d men in %d %s", men, armies, armies == 1 ? "army" : "armies");
            lines.push_back({buf, "send out", kPaperInk, 1000 + static_cast<int>(g.num("stack")), false, false});
            for (const Json& a : g.list("armies")) {
                // Each squad with its history: rank (a hero's level), kills, battles.
                const int rank = static_cast<int>(a.num("rank")), battles = static_cast<int>(a.num("battles"));
                char hist[96];
                std::snprintf(hist, sizeof hist, "%s%s, %d %s, %d %s", a.flag("hero") ? "level " : "", a.flag("hero") ? std::to_string(rank + 1).c_str() : kRankNames[std::clamp(rank, 0, 3)],
                              static_cast<int>(a.num("kills")), a.num("kills") == 1 ? "kill" : "kills", battles, battles == 1 ? "battle" : "battles");
                std::snprintf(buf, sizeof buf, "   %s  %d/%d", a.str("name").c_str(), static_cast<int>(a.num("men")), static_cast<int>(a.num("of")));
                lines.push_back({buf, hist, rank >= 2 ? kLeaf : kPaperDim, -2, false, false});
            }
        }
        const int producing = static_cast<int>(city.num("producing"));
        const std::vector<Json>& options = city.list("options");
        if (producing >= 0 && producing < static_cast<int>(options.size())) {
            std::snprintf(buf, sizeof buf, "Training %s: day %d of %d", options[static_cast<size_t>(producing)].str("type").c_str(), static_cast<int>(city.num("progress")),
                          static_cast<int>(options[static_cast<size_t>(producing)].num("days")));
            text(buf, kSeal);
        } else {
            text("Training nothing. Choose an army:", kSeal);
        }
        for (size_t i = 0; i < options.size(); ++i) {
            const Json& op = options[i];
            const bool levy = op.num("levy") > 0;
            std::string name = op.str("type") + " (" + std::to_string(static_cast<int>(op.num("strength"))) + ")";
            if (levy) name += " levy x" + std::to_string(static_cast<int>(op.num("levy")));
            char cost[64];
            std::snprintf(cost, sizeof cost, "%dd  %dg  %d/day", static_cast<int>(op.num("days")), static_cast<int>(op.num("cost")), static_cast<int>(op.num("upkeep")));
            lines.push_back({name, cost, levy ? rgba(0x5B2A86) : kPaperInk, kTownAct + static_cast<int>(i), static_cast<int>(i) == producing, gold < op.num("cost")});
        }
        lines.push_back({"Stop training", "", kPaperInk, kTownStop, false, false});
        if (city.flag("choosing")) {
            text("Click one of your towns to send its new armies there.", kSeal);
        } else {
            const std::string to = city.str("vectorTo");
            lines.push_back({to.empty() ? "Send new armies to..." : "New armies march to " + to, to.empty() ? "" : "change", kPaperInk, kTownSend, false, false});
            if (!to.empty()) lines.push_back({"Keep new armies here", "", kPaperInk, kTownKeep, false, false});
        }
        if (city.flag("port")) {
            const int boats = static_cast<int>(city.num("boats")), days = static_cast<int>(city.num("boatDays"));
            std::string port = "Port: " + std::to_string(boats) + (boats == 1 ? " boat" : " boats") + " in harbour";
            if (days) port += ", one on the slips, " + std::to_string(days) + " days to go";
            text(port, kPaperDim);
            if (!days) {
                std::snprintf(buf, sizeof buf, "%dd  %dg  %d/day", static_cast<int>(city.num("boatTime")), static_cast<int>(city.num("boatCost")), static_cast<int>(city.num("boatUpkeep")));
                lines.push_back({"Build a boat (7 armies)", buf, kPaperInk, kTownBoat, false, gold < city.num("boatCost")});
            }
        }
        text("Rare troops are never trained: levies (violet), ruins, temples and heroes for hire bring them.", kPaperOff);
    }
    lines.push_back({"Close", "", kPaperInk, kTownClose, false, false});
    float ph = 2 * kPad + 22;
    for (const Line& l : lines) ph += l.what < 0 ? kLine : kRow;
    const float x0 = 10, y0 = 96;
    o.paper(x0, y0, pw, ph, 31);
    o.text(x0 + kPad, y0 + kPad - 2, city.str("name"), kSeal, 18);
    float y = y0 + kPad + 22;
    std::vector<Rect> panel;
    for (const Line& l : lines) {
        if (l.what < 0) {
            o.text(x0 + kPad, y + 2, l.left, l.what == -2 ? kPaperDim : l.ink, kSize);
            if (!l.right.empty()) o.text(x0 + pw - kPad - o.width(l.right, kSize), y + 2, l.right, l.ink, kSize);  // (a squad's history)
            y += kLine;
            continue;
        }
        if (l.on) o.rect(x0 + kPad - 4, y - 1, inner + 8, kRow - 2, rgba(0xB58A2C, 70));
        o.frame(x0 + kPad - 4, y - 1, inner + 8, kRow - 2, 1, kPaperOut);
        const Colour ink = l.dim ? kPaperOff : l.ink;
        o.text(x0 + kPad, y + 3, l.left, ink, kSize);
        if (!l.right.empty()) o.text(x0 + pw - kPad - o.width(l.right, kSize), y + 3, l.right, l.dim ? kPaperOff : kPaperDim, kSize);
        panel.push_back({x0 + kPad - 4, y - 1, inner + 8, kRow - 2, l.what});
        y += kRow;
    }
    panel.push_back({x0, y0, pw, ph, kTownSwallow});  // (the sheet takes the click: nothing is ordered through it)
    hits_.insert(hits_.begin(), panel.begin(), panel.end());
}

// The fight has stopped (illyria_hud: result): the outcome, how it came, the time and the toll, each squad's count,
// who broke, the heroes fallen and the best squad, on parchment, with what to do next.
void Hud::drawResult(Overlay& o, const Json& hud, float w, float h) {
    const Json* r = hud.find("result");
    if (!r) return;
    const bool won = r->num("outcome") == 1;
    const std::string line = r->str("line");
    const std::vector<Json>& rows = r->list("rows");
    constexpr float kRow = 17, kPad = 22;
    const size_t room = static_cast<size_t>(std::max(4.0f, (h - 360) / kRow));
    const size_t shown = std::min(rows.size(), room);
    std::string fallen;
    for (const Json& f : r->list("fallen")) fallen += (fallen.empty() ? "" : ", ") + f.string;
    // What carries over: the men promoted, and in a march who goes on and who is gone for good.
    const Json* march = hud.find("march");
    const bool settled = march && march->flag("settled");
    const float pw = std::min(w - 20, 640.0f);
    std::vector<std::string> carry;
    char cbuf[200];
    if (settled) {
        const Json* last = march->find("last");
        const bool ended = march->flag("over") && !march->flag("won");  // (a lost battle: nobody goes on)
        std::snprintf(cbuf, sizeof cbuf, ended ? "Battle %d of %d. %d men in %d squads were left." : march->flag("won") ? "Battle %d of %d. %d men in %d squads came through. %d promoted." : "Battle %d of %d. %d men in %d squads go on. %d promoted.", static_cast<int>(march->num("battle")),
                      static_cast<int>(march->num("of")), static_cast<int>(march->num("men")), static_cast<int>(march->num("squads")), static_cast<int>(last ? last->num("promoted") : 0));
        carry.push_back(cbuf);
        std::string goneLine;
        if (last)
            for (const Json& g : last->list("gone")) goneLine += (goneLine.empty() ? "Gone for good: " : ", ") + g.string;
        for (const std::string& l : wrap(o, goneLine, 13, pw - 44)) carry.push_back(l);
        if (march->flag("won")) carry.push_back("The march is won.");
        else if (march->flag("over")) carry.push_back("The march ends here.");
        else if (last) carry.push_back("Next: " + march->str("next") + ". " + std::to_string(static_cast<int>(last->num("gold"))) + " gold for the purse.");
    } else if (const int promoted = static_cast<int>(r->num("promoted")); promoted > 0) {
        // (A skirmish's men each have a rank; a Conquest's squad has one.)
        carry.push_back(std::to_string(promoted) + (r->flag("conquest") ? (promoted == 1 ? " squad promoted." : " squads promoted.") : promoted == 1 ? " man promoted." : " men promoted."));
    }
    const float ph = 232 + static_cast<float>(shown) * kRow + (rows.size() > shown ? kRow : 0) + (fallen.empty() ? 0 : 20) + static_cast<float>(carry.size()) * 18;
    const float x0 = (w - pw) / 2, y0 = std::max(40.0f, (h - ph) / 2 - 20);
    o.paper(x0, y0, pw, ph, 41);
    const std::string title = won ? "VICTORY" : "DEFEAT";
    o.text(x0 + (pw - o.width(title, 30)) / 2, y0 + kPad, title, won ? kLeaf : kSeal, 30);
    o.text(x0 + (pw - o.width(line, 14)) / 2, y0 + kPad + 40, line, kPaperInk, 14);
    char buf[160];
    const int secs = static_cast<int>(r->num("seconds"));
    std::snprintf(buf, sizeof buf, "Time %d:%02d     Killed %d     Lost %d", secs / 60, secs % 60, static_cast<int>(r->num("killed")), static_cast<int>(r->num("lost")));
    o.text(x0 + (pw - o.width(buf, 14)) / 2, y0 + kPad + 64, buf, kPaperInk, 14);
    // The squads: name, men left of its strength, killed, and whether it broke.
    const float cName = x0 + kPad, cLeft = x0 + pw - 362, cKilled = x0 + pw - 268, cRank = x0 + pw - 208, cBroke = x0 + pw - 72;
    float y = y0 + kPad + 96;
    o.text(cName, y, "Squad", kSeal, 12);
    o.text(cLeft, y, "Left", kSeal, 12);
    o.text(cKilled, y, "Killed", kSeal, 12);
    o.text(cRank, y, "Rank", kSeal, 12);
    o.line(x0 + kPad, y + 16, x0 + pw - kPad, y + 16, 1, kPaperOut);
    y += 20;
    for (size_t i = 0; i < shown; ++i, y += kRow) {
        const Json& q = rows[i];
        const bool other = q.num("total") == 0;  // (the dead no squad is credited with: no men of its own)
        const bool gone = q.num("alive") == 0 && !other;
        const std::string name = q.str("name");
        o.text(cName, y, name, gone ? kPaperOff : kPaperInk, fit(o, name, 12, cLeft - cName - 10));
        std::snprintf(buf, sizeof buf, "%d / %d", static_cast<int>(q.num("alive")), static_cast<int>(q.num("total")));
        if (!other) o.text(cLeft, y, buf, gone ? kSeal : kPaperInk, 12);
        std::snprintf(buf, sizeof buf, "%d", static_cast<int>(q.num("kills")));
        o.text(cKilled, y, buf, kPaperInk, 12);
        if (!gone) {  // the survivors' rank, and how many of them rose in this battle
            const int rank = static_cast<int>(q.num("rank")), up = static_cast<int>(q.num("promoted"));
            std::string rk = q.flag("hero") ? "level " + std::to_string(rank) : std::string(kRankNames[std::clamp(rank, 0, 3)]);
            // (A skirmish's men each have a rank: how many rose. A Conquest's squad has one: it rose.)
            if (up > 0) rk += r->flag("conquest") ? " (promoted)" : (rk.empty() ? "+" : "  +") + std::to_string(up);
            o.text(cRank, y, rk, up > 0 ? kLeaf : kPaperDim, 12);
        }
        if (q.flag("broke")) o.text(cBroke, y, "broke", kSeal, 12);
    }
    if (rows.size() > shown) {
        std::snprintf(buf, sizeof buf, "and %zu more", rows.size() - shown);
        o.text(cName, y, buf, kPaperDim, 12);
        y += kRow;
    }
    y += 8;
    if (const std::string best = r->str("best"); !best.empty()) {
        int kills = 0;
        for (const Json& q : rows)
            if (q.str("name") == best) kills = std::max(kills, static_cast<int>(q.num("kills")));
        std::snprintf(buf, sizeof buf, "Best squad: %s, %d killed", best.c_str(), kills);
        o.text(cName, y, buf, kPaperInk, 13);
    }
    y += 20;
    if (!fallen.empty()) {
        const std::string t = "Heroes fallen: " + fallen;
        o.text(cName, y, t, kSeal, fit(o, t, 13, pw - 2 * kPad));
        y += 20;
    }
    for (const std::string& l : carry) {
        o.text(cName, y, l, kPaperInk, fit(o, l, 13, pw - 2 * kPad));
        y += 18;
    }
    // What next: fight it again, change the army, or the menu. A march's battle is fought once: on to the next (its camp), or to its end.
    const bool ends = march && march->flag("over"), war = r->flag("conquest");
    const char* const kActs[3] = {war ? "Carry on (Enter)" : march ? (ends ? "The march's end (Enter)" : "Next battle (Enter)") : "Again (Enter)", march || war ? "" : "Change army (C)", war ? "" : "Menu (Esc)"};
    const float bw = (pw - 2 * kPad - 20) / 3, by = y0 + ph - kPad - 26;
    int acts = 0;
    for (int k = 0; k < 3; ++k) {
        if (!*kActs[k]) continue;
        ++acts;
        const float bx = x0 + kPad + static_cast<float>(k) * (bw + 10);
        o.rect(bx, by, bw, 28, rgba(0x2B1E10, 28));
        o.frame(bx, by, bw, 28, 1.5f, k == 0 ? kSeal : kPaperOut);
        const float size = fit(o, kActs[k], 13, bw - 12);
        o.text(bx + (bw - o.width(kActs[k], size)) / 2, by + 7, kActs[k], k == 0 ? kSeal : kPaperInk, size);
        hits_.insert(hits_.begin(), {bx, by, bw, 28, kAfterBattle + k});
    }
    hits_.insert(hits_.begin() + acts, {x0, y0, pw, ph, kTownSwallow});  // (a press on the sheet is not one on the field under it)
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
    const Json camp = read(illyria_conquest_state());
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
        const int map = static_cast<int>(hud.num("scenario"));  // 0 the Ford, 1 a town's siege, 2 a capital's, 3 open ground
        const bool siege = map == 1 || map == 2, defend = hud.flag("defend");
        std::string foe = hud.str("foe");
        const size_t of = foe.find(" of ");
        const std::string town = siege && of != std::string::npos ? foe.substr(of + 4) : "";
        const std::string where = siege ? std::string(map == 2 ? "Siege of the capital" : "Siege") + (town.empty() ? "" : ": " + town)
                                        : map == 3 ? "Skirmish: open ground" : "Skirmish: Oakhaven Ford";
        const std::string goal = siege ? (defend ? "hold the walls" : map == 2 ? "take the courtyard and the keep" : "take the courtyard")
                                 : foe.empty() ? "" : (map == 0 ? "raise your flag on the isle, or rout the " : "rout the ") + foe;
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
    const std::string hint = w < 560 ? "/: keys" : hud.flag("deploy") ? "Enter: begin    /: keys" : "Enter: menu    /: keys";
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

    // A battle's start and end: the deployment and its one-line briefing; a host chased off the field.
    if (hud.flag("deploy")) {
        const std::string t = "Place your line. Enter to begin", brief = hud.str("brief");
        const float bw = std::max(o.width(t, 22), o.width(brief, 14)) + 40;
        o.rect((w - bw) / 2, 78, bw, brief.empty() ? 40 : 64, rgba(0x000000, 170));
        o.text((w - o.width(t, 22)) / 2, 86, t, kGold, 22);
        if (!brief.empty()) o.text((w - o.width(brief, 14)) / 2, 118, brief, kInk, 14);
    } else if (const int routed = static_cast<int>(hud.num("routed", -1)); routed >= 0 && !hud.find("result")) {
        const bool mine = routed == static_cast<int>(hud.num("side"));
        const std::string t = mine ? "Your host has quit the field" : "The enemy has quit the field";
        // The pursuit: the horse and the light foot ride the runners down until it is called.
        const int left = static_cast<int>(hud.num("pursuit"));
        const std::string u = left <= 0 ? "" : (mine ? "They are riding your men down: " : "Ride them down: ") + std::to_string(left) + " s";
        const float bw = std::max(o.width(t, 24), o.width(u, 15)) + 28;
        o.rect((w - bw) / 2, h * 0.24f - 8, bw, u.empty() ? 42 : 66, rgba(0x000000, 170));
        o.text((w - o.width(t, 24)) / 2, h * 0.24f, t, mine ? kRed : kGold, 24);
        if (!u.empty()) o.text((w - o.width(u, 15)) / 2, h * 0.24f + 34, u, kInk, 15);
    }
    if (const Json* cap = hud.find("capture"); !conquest && cap && cap->flag("on")) {
        // A siege's clock, beside the minimap: each door's strength, and the flag (what blocks it, or how far it has climbed).
        const std::vector<Json>& gates = cap->list("gates");
        const float mh = std::clamp(h * 0.18f, 96.0f, 260.0f);
        constexpr float kRowH = 22, kBarW = 110, kPad = 8;
        const std::string line = cap->str("text");
        float nameW = 0;
        for (const Json& g : gates) nameW = std::max(nameW, o.width(g.str("name") + " 100%", 14));
        const float pw = std::max(nameW + kBarW + 3 * kPad, o.width(line, 14) + 2 * kPad);
        const float ph = kRowH * static_cast<float>(gates.size() + 2) + kPad, px = w - mh - 30 - pw, py = 42;
        o.rect(px, py, pw, ph, rgba(0x000000, 150));
        float y = py + kPad;
        const auto bar = [&](float x, float fill, Colour c) {
            o.rect(x, y + 3, kBarW, 10, rgba(0x000000, 200));
            o.rect(x + 1, y + 4, (kBarW - 2) * std::clamp(fill, 0.0f, 1.0f), 8, c);
        };
        for (const Json& g : gates) {
            const int hp = static_cast<int>(g.num("hp"));
            o.text(px + kPad, y, g.str("name") + (hp > 0 ? " " + std::to_string(hp) + "%" : " down"), hp > 0 ? kInk : kRed, 14);
            bar(px + pw - kPad - kBarW, static_cast<float>(hp) / 100.0f, hp > 30 ? kGold : kRed);
            y += kRowH;
        }
        o.text(px + kPad, y, line, cap->flag("blocked") ? kRed : kInk, 14);
        y += kRowH;
        bar(px + kPad, static_cast<float>(cap->num("p")) / 100.0f, cap->flag("mine") ? kGold : kRed);  // (whose flag is climbing)
    }
    if (conquest && camp.num("winner") != 0) {
        const std::string t = "The war is decided";
        o.text((w - o.width(t, 32)) / 2, h * 0.3f, t, kGold, 32);
    }
    const std::string notice = hud.str("notice");
    if (!notice.empty()) o.text((w - o.width(notice, 16)) / 2, 40, notice, kRed, 16);
    if (conquest && camp.flag("noArmy") && camp.num("winner") == 0) {  // no army anywhere: say so, and what to do
        const std::string t = "Your army has been destroyed";
        const std::string u = camp.num("cities") > 0 ? "Click a town you hold and train new armies there." : "You hold no town to train armies in.";
        const float bw = std::max(o.width(t, 32), o.width(u, 16)) + 40;
        o.rect((w - bw) / 2, h * 0.3f - 12, bw, 82, rgba(0x000000, 170));
        o.text((w - o.width(t, 32)) / 2, h * 0.3f, t, kRed, 32);
        o.text((w - o.width(u, 16)) / 2, h * 0.3f + 44, u, kInk, 16);
    }
    {  // What happened to our own: a few short lines, newest first, fading as they age (Battle::events).
        const std::vector<Json>& events = hud.list("events");
        float ey = 44;
        int shown = 0;
        for (size_t k = events.size(); k-- > 0 && shown < 3; ++shown, ey += 20) {
            const double sec = events[k].num("age") / 30.0;  // (ticks of the world: 30 a second)
            const uint8_t alpha = static_cast<uint8_t>(255 * std::clamp((7.0 - sec) / 2.0, 0.0, 1.0));
            const std::string text = events[k].str("text");
            o.rect(6, ey - 2, o.width(text, 14) + 12, 20, rgba(0x000000, alpha * 150 / 255));
            o.text(12, ey, text, {kRed.r, kRed.g, kRed.b, alpha}, 14);
        }
    }

    // The ground under the pointer, for the squads selected ("ford", "woods", "higher ground"): a word by the cursor.
    if (pointerX_ >= 0 && !menu_) {
        const std::string ground = illyria_ground_at(pointerX_, pointerY_);
        if (!ground.empty() && pointerY_ > 40 && pointerY_ < h - 120) {
            const float gw = o.width(ground, 12);
            o.rect(pointerX_ + 14, pointerY_ + 12, gw + 8, 17, rgba(0x000000, 150));
            o.text(pointerX_ + 18, pointerY_ + 13, ground, rgba(0xF4EAD0), 12);
        }
    }
    // Position told: a word over the men it happened to, rising and fading (Battle::floats): gold for the viewer's gain, red for his loss.
    std::vector<std::pair<float, float>> floated;  // where a word already stands: the same ground seen from far is one word, not a pile
    for (const Json& f : hud.list("floats")) {
        const double sec = f.num("age") / 30.0;
        const uint8_t alpha = static_cast<uint8_t>(255 * std::clamp((3.0 - sec) / 1.0, 0.0, 1.0));
        const std::string text = f.str("text");
        const float fw = o.width(text, 15), fx = static_cast<float>(f.num("x")) - fw / 2, fy = static_cast<float>(f.num("y")) - 18 - static_cast<float>(sec) * 10;
        if (fy < 38 || fx < 4 || fx + fw > w - 4) continue;
        if (std::any_of(floated.begin(), floated.end(), [&](const std::pair<float, float>& p) { return std::abs(p.first - fx) < fw + 12 && std::abs(p.second - fy) < 22; })) continue;
        floated.push_back({fx, fy});
        const Colour c = f.flag("good") ? kGold : kRed;
        o.rect(fx - 5, fy - 2, fw + 10, 20, rgba(0x000000, static_cast<uint8_t>(alpha * 140 / 255)));
        o.text(fx, fy, text, {c.r, c.g, c.b, alpha}, 15);
    }

    // Names of towns and places when the view is high.
    const size_t sel = static_cast<size_t>(hud.num("selCount"));
    const std::vector<Json>& squads = hud.list("squads");
    const bool squadCards = !squads.empty();
    // The squad cards: every squad has one (card_layout.h: one row of full cards, then in a wide window two rows of
    // compact ones, then pages), so the strip's height depends on how many there are. Its top is what the selection
    // line, the names of places and the inspect line keep above. Their order is by role (cardOrder), the rare ones
    // from the right-hand end.
    namespace ig = illyria::game;
    std::vector<int> cardRanks;
    for (const Json& q : squads) cardRanks.push_back(ig::cardRoleRank(q.str("type")));
    const std::vector<int> cardOrder = ig::cardOrder(cardRanks);
    const int cardCount = static_cast<int>(squads.size());
    const int cardRare = static_cast<int>(std::count_if(cardRanks.begin(), cardRanks.end(), [](int r) { return r >= ig::kCardRareRank; }));
    const ig::CardLayout cards = ig::layoutCards(cardCount, w - 16, ig::cardRowsFor(w));
    const float cardsTop = h - 8 - (squadCards ? ig::cardStripHeight(cards) : ig::kCardH);
    cardPage_ = cards.paged ? std::clamp(cardPage_, 0, cards.pages - 1) : 0;
    // A squad newly the selection (Tab, J, a box on the field) on another page: the strip turns to it.
    if (const int now = static_cast<int>(hud.num("selected")); now != cardSelected_) {
        cardSelected_ = now;
        if (cards.paged && now >= 0)
            for (int k = 0; k < cardCount; ++k)
                if (static_cast<int>(squads[static_cast<size_t>(cardOrder[static_cast<size_t>(k)])].num("i")) == now) cardPage_ = k / cards.perPage;
    }
    stripX_ = 0, stripY_ = squadCards ? cardsTop : 0, stripW_ = squadCards ? w : 0, stripH_ = squadCards ? h - cardsTop : 0;
    stripPaged_ = cards.paged;
    cardPages_ = cards.pages;
    std::vector<Rect> townHits;  // taps on towns' markers and names, added after the armies' flags (which come first where they overlap)
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
            const int townId = std::atoi(f[4].c_str());
            const float x = static_cast<float>(std::atof(f[2].c_str())), y = static_cast<float>(std::atof(f[3].c_str()));
            if (y < 38) continue;  // (under the status bar)
            // A name is drawn whole or not at all: it fades over the last stretch before a screen edge or a strip
            // of the HUD (the bar above; below, the selection line and the cards), and goes when it would be cut.
            const float size = f[0] == "1" ? 13.0f : 11.0f;
            const float nw = name.empty() ? 0.0f : o.width(name, size);
            const float stripTop = squadCards ? cardsTop - (sel > 0 ? 34.0f : 6.0f) : h - (sel > 0 ? 104.0f : 12.0f);
            const bool markerShown = y + 2 < stripTop && x > 6 && x < w - 6;
            const Colour c = hex(f[1]);
            if (conquest && markerShown && townId >= 0 && (f[0] == "0" || f[0] == "1")) townHits.push_back({x - 8, y, 16, 16, 5000 + townId});  // (the marker is a tap on its town)
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
            if (conquest && townId >= 0 && (f[0] == "0" || f[0] == "1") && fade > 0.5f) townHits.push_back({lx, y + 2, nw, size + 2, 5000 + townId});  // (and so is its name)
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
            std::snprintf(buf, sizeof buf, "%d%s", static_cast<int>(s.num("men")), s.flag("hero") ? "*" : "");  // the men in it, a garrison's too
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
        for (const Rect& r : townHits) hits_.push_back(r);
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

    // The march of the selected stack: when it gets there.
    if (conquest) {
        if (const Json* m = camp.find("march")) {
            const double ticks = m->num("ticks"), perDay = std::max(1.0, m->num("perDay")), perSecond = std::max(1.0, m->num("perSecond"));
            const int speed = camp.flag("paused") ? 1 : std::max(1, static_cast<int>(camp.num("speed")));
            // Days and hours of the game's calendar (a day is perDay ticks), rounded up to the hour so a stack never "arrives in 0h".
            const int hours = std::max(1, static_cast<int>(std::ceil(ticks / perDay * 24.0)));
            const int secs = std::max(1, static_cast<int>(std::lround(ticks / perSecond / speed)));
            char when[64], clock[48];
            if (hours >= 24) std::snprintf(when, sizeof when, "%dd %dh", hours / 24, hours % 24);
            else std::snprintf(when, sizeof when, "%dh", hours);
            if (secs < 60) std::snprintf(clock, sizeof clock, "%d s", secs);
            else if (secs < 600 && secs % 60) std::snprintf(clock, sizeof clock, "%d min %d s", secs / 60, secs % 60);
            else std::snprintf(clock, sizeof clock, "%d min", secs / 60);
            std::snprintf(buf, sizeof buf, "On the march: arrives in %s  (%s at %dx)", when, clock, speed);
            const float mw = o.width(buf, 14), my = !notice.empty() || hud.flag("planning") ? 94.0f : 40.0f;
            o.rect((w - mw) / 2 - 10, my - 4, mw + 20, 24, rgba(0x000000, 170));
            o.text((w - mw) / 2, my, buf, kGold, 14);
        }
    }

    // What waits where the selection is going (illyria_conquest_state: forecast), under the march's line when there is one.
    if (conquest) {
        if (const std::string foe = camp.str("forecast"); !foe.empty()) {
            const float fw = o.width(foe, 14), fy = (camp.find("march") ? (!notice.empty() || hud.flag("planning") ? 94.0f : 40.0f) + 26 : (!notice.empty() || hud.flag("planning") ? 94.0f : 40.0f)) - 4;
            o.rect((w - fw) / 2 - 10, fy, fw + 20, 24, rgba(0x000000, 170));
            o.text((w - fw) / 2, fy + 4, foe, kInk, 14);
        }
    }

    // The selection and the squad cards, bottom.
    float selEnd = 0;
    if (sel > 0) {
        // One squad selected: its history too (rank or a hero's level, kills, the battles it has been in).
        std::string history;
        for (const Json& q : squads) {
            if (static_cast<int>(q.num("i")) != static_cast<int>(hud.num("selected")) || hud.num("selected") < 0) continue;
            const int battles = static_cast<int>(q.num("battles")), kills = static_cast<int>(q.num("kills"));
            history = "  -  " + (q.num("level") > 0 ? "level " + std::to_string(static_cast<int>(q.num("level"))) : std::string(kRankNames[std::clamp(static_cast<int>(q.num("rank")), 0, 3)])) + ", " +
                      std::to_string(kills) + (kills == 1 ? " kill" : " kills") + (battles > 0 ? ", " + std::to_string(battles) + (battles == 1 ? " battle" : " battles") : "");
        }
        std::snprintf(buf, sizeof buf, "%zu selected  %s%s%s", sel, hud.str("selLabel").c_str(), history.c_str(), hud.flag("holding") ? "  HOLD (K: support)" : "");
        selEnd = o.width(buf, 13) + 20;
        o.rect(0, cardsTop - 28, selEnd, 22, rgba(0x000000, 150));
        o.text(10, cardsTop - 24, buf, kGold, 13);
    }
    // The first-order hint, one line for a battle and a Conquest, until the first order is given (then it fades and
    // is not shown again on this device).
    if (!hintSeen_) {
        if (hud.num("orders") > 0 && hintFade_ < 0) hintFade_ = 45;
        if (hintFade_ > 0 && --hintFade_ == 0) {
            hintSeen_ = true;
            saveSettings();
        }
        if (!hintSeen_) {
            const std::string hint = conquest ? "Right-click a town to march there. Hold the right button, then drag, to face." : "Right-click to march or attack. Hold the right button, then drag, to face.";
            const float hw = o.width(hint, 14), alpha = hintFade_ < 0 ? 1.0f : static_cast<float>(hintFade_) / 45.0f;
            const float hx = std::max((w - hw) / 2, selEnd + 20), hy = cardsTop - 26;
            o.rect(hx - 10, hy - 3, hw + 20, 24, rgba(0x000000, static_cast<uint8_t>(170 * alpha)));
            o.text(hx, hy, hint, rgba(0xF2E6B8, static_cast<uint8_t>(255 * alpha)), 14);
        }
    }
    const Colour side = hex(hud.str("colour"));  // the player's colour (palette.h), the bars of the cards
    bool tipDrawn = false;
    const float cw = cards.width, ch = cards.height;
    const std::vector<Json>& marksLow = hud.list("marksLow");
    // The lord's Rally button stands over the cards' right end (below): the tip keeps clear of it.
    float rallyRoom = 0;
    if (const Json* rally = hud.find("rally"); rally && rally->flag("lord") && squadCards) rallyRoom = o.width("Rally 00 broken  (Y)", 13) + 20 + 8;
    for (int k = 0; k < cardCount; ++k) {
        const int cell = ig::cardCell(cards, cardCount, cardRare, cardPage_, k);
        if (cell < 0) continue;  // (on another page)
        const Json& q = squads[static_cast<size_t>(cardOrder[static_cast<size_t>(k)])];
        const ig::CardRect at = ig::cardCellRect(cards, cell, 8, cardsTop);
        const float x = at.x, y = at.y;
        const int i = static_cast<int>(q.num("i"));
        const int sq = static_cast<int>(q.num("sel"));
        const bool mine = q.flag("mine");
        // A card over one of the selection's ground markers (illyria_hud: marksLow) lets the ground show through.
        const bool overMark = std::any_of(marksLow.begin(), marksLow.end(), [&](const Json& m) { return at.has(static_cast<float>(m.num("x")), static_cast<float>(m.num("y"))); });
        o.rect(x, y, cw, ch, rgba(0x0E0B08, overMark ? 50 : 190));
        o.frame(x, y, cw, ch, sq == 2 ? 3.0f : 1.5f, sq ? kGold : mine ? rgba(0x7C6E50) : rgba(0x4A4A4A));
        // The card's slots (card_face.h): the fields of the squad's state that are there are drawn, each in its own place.
        ig::CardFacts facts;
        facts.rallyIn = static_cast<int>(q.num("rallyIn"));
        facts.calling = q.flag("calling"), facts.answering = q.flag("answering"), facts.hold = q.flag("hold"), facts.idle = q.flag("idle");
        facts.holdFire = q.flag("holdFire");
        facts.sat = static_cast<int>(q.num("sat")), facts.satMax = static_cast<int>(q.num("satMax"));
        facts.ammo = static_cast<int>(q.num("ammo")), facts.ammoMax = static_cast<int>(q.num("ammoMax"));
        facts.rank = q.num("level") > 0 ? 0 : static_cast<int>(q.num("rank"));  // (a hero's is his level: the selection line says it)
        // The counter slot: what it needs.
        const bool showSat = ig::cardShowsSatchels(facts), showAmmo = ig::cardShowsAmmo(facts);
        const std::string satText = std::to_string(facts.sat), ammoText = std::to_string(facts.ammo);
        float counterW = 0;
        if (facts.holdFire) counterW += 6;
        if (showSat) counterW += (counterW > 0 ? 4 : 0) + 7 + o.width(satText, 9);
        if (showAmmo) counterW += (counterW > 0 ? 4 : 0) + 4 + o.width(ammoText, 9);
        const int pips = ig::cardRankPips(facts);
        const float pipsW = pips > 0 ? static_cast<float>(pips * 4 + (pips - 1) * 2) : 0.0f;
        const ig::CardFace face = ig::cardFace(cw, ch, cards.compact, counterW, pipsW);
        // The name whole if it fits, else over two lines (the full card), else a step smaller, and only then cut, a
        // squad's number kept (card_name.h).
        std::vector<std::string> nameLines;
        float nameSize = 10;
        for (; nameSize >= 9; nameSize -= 1)
            if (ig::fitCardNameNumbered(q.str("name"), [&](const std::string& t) { return o.width(t, nameSize); }, face.nameW, static_cast<size_t>(face.nameLines), nameLines)) break;
        if (nameSize < 9) nameSize = 9;
        float ny = y + face.nameY;
        for (const std::string& l : nameLines) {
            o.text(x + face.nameX, ny, l, mine ? kInk : kDim, nameSize);
            ny += face.lineH;
        }
        if (counterW > 0) {
            float px = x + face.counterX;
            const float py = y + face.counterY;
            if (facts.holdFire) {  // two gold bars: holding its fire
                o.rect(px, py + 1, 2, 8, kGold);
                o.rect(px + 4, py + 1, 2, 8, kGold);
                px += 6 + 4;
            }
            if (showSat) {  // satchels: an amber square and how many
                o.rect(px, py + 3, 5, 5, facts.sat ? rgba(0xE0A030) : rgba(0x4A4032));
                px += 7;
                o.text(px, py, satText, facts.sat ? kInk : kRed, 9);
                px += o.width(satText, 9) + 4;
            }
            if (showAmmo) {  // the missiles of its store: a red bar and how many
                o.rect(px, py + 1, 2, 8, facts.ammo ? rgba(0xFF6030) : rgba(0x4A3A32));
                px += 4;
                o.text(px, py, ammoText, facts.ammo ? kInk : kRed, 9);
            }
        }
        // The count's line: the count, then the squad's ground (or on the full card its role) when there is room, and
        // the status slot at the right: a white flag and the seconds until a broken squad rallies ("--" while a foe is on
        // it), else HELP, ANSWER, HOLD, IDLE (no order and nothing to fight: the J key steps through these).
        const float rowY = y + face.rowY + (cards.compact ? 0.0f : (static_cast<float>(nameLines.size()) - 1) * face.lineH);
        const ig::CardStatus status = ig::cardStatus(facts);
        const std::string mark = ig::cardStatusWord(facts);
        const bool broken = status == ig::CardStatus::Broken;
        const float markW = mark.empty() ? 0.0f : o.width(mark, 10) + (broken ? 13.0f : 0.0f);
        std::snprintf(buf, sizeof buf, "%d/%d", static_cast<int>(q.num("alive")), static_cast<int>(q.num("total")));
        std::string count = buf;
        // (A selected squad's ground, Battle::groundTag: "high ground", "in the ford", "hidden in woods": whole when there
        // is room, else its one word; a squad on plain ground shows its role on the full card.)
        const std::string ground = q.str("ground");
        const float rowRoom = cw - 12 - (markW > 0 ? markW + 6 : 0);
        for (const std::string& after : {ground, ig::cardGroundTag(ground), ground.empty() && !cards.compact ? q.str("role") : std::string()})
            if (!after.empty() && o.width(count + "  " + after, 10) <= rowRoom) {
                count += "  " + after;
                break;
            }
        o.text(x + 6, rowY, count, kDim, 10);
        if (!mark.empty()) {
            const float mx = x + cw - 6 - o.width(mark, 10);
            o.text(mx, rowY, mark, broken ? rgba(0xFFFFFF) : status == ig::CardStatus::Calling ? kRed : status == ig::CardStatus::Idle ? rgba(0x9FD0FF) : kGold, 10);
            if (broken) {
                o.rect(mx - 13, rowY - 1, 1.5f, 12, rgba(0xFFFFFF));     // the pole
                o.rect(mx - 11.5f, rowY - 1, 8, 6, rgba(0xFFFFFF));      // the white flag
            }
        }
        const double total = std::max(1.0, q.num("total"));
        o.rect(x + face.barX, y + face.barY, face.barW, face.barH, rgba(0xFFFFFF, 28));
        o.rect(x + face.barX, y + face.barY, face.barW * static_cast<float>(q.num("alive") / total), face.barH, q.num("broken") ? kRed : side);
        for (int p = 0; p < pips; ++p) o.rect(x + face.pipsX + static_cast<float>(p) * 6, y + face.barY, 4, face.barH, kGold);  // rank: one pip a step
        hits_.push_back({x, y, cw, ch, -1 - i});
        // What its kind is for, over the cards, the first time the kind is picked (illyria_hud: tip).
        if (const std::string tip = hud.str("tip"); sq && !tip.empty() && !tipDrawn) {
            tipDrawn = true;
            const float tw = o.width(tip, 13) + 16, tx = std::clamp(x, 8.0f, std::max(8.0f, w - 8 - tw - rallyRoom));  // (left of the Rally button)
            o.rect(tx, cardsTop - 56, tw, 22, rgba(0x000000, 200));
            o.frame(tx, cardsTop - 56, tw, 22, 1, kGold);
            o.text(tx + 8, cardsTop - 52, tip, kInk, 13);
        }
    }
    if (cards.paged) {  // "+N more": the last cell. Its ends are the arrows; a press on its middle turns to the next page, round to the first.
        const ig::CardRect at = ig::cardPagerRect(cards, 8, cardsTop);
        const float x = at.x, y = at.y, arrow = ig::kCardPagerArrow;
        o.rect(x, y, cw, ch, rgba(0x0E0B08, 190));
        o.frame(x, y, cw, ch, 1.5f, kGold);
        o.rect(x + 3, y + 3, arrow - 5, ch - 6, rgba(0x3A3226, 220));
        o.rect(x + cw - arrow + 2, y + 3, arrow - 5, ch - 6, rgba(0x3A3226, 220));
        o.text(x + 3 + (arrow - 5 - o.width("<", 14)) / 2, y + ch / 2 - 8, "<", cardPage_ > 0 ? kGold : kDim, 14);
        o.text(x + cw - arrow + 2 + (arrow - 5 - o.width(">", 14)) / 2, y + ch / 2 - 8, ">", cardPage_ + 1 < cards.pages ? kGold : kDim, 14);
        std::snprintf(buf, sizeof buf, "+%d more", ig::cardsElsewhere(cards, cardCount, cardPage_));
        o.text(x + (cw - o.width(buf, 10)) / 2, y + ch / 2 - (cards.compact ? 13.0f : 15.0f), buf, kInk, 10);
        std::snprintf(buf, sizeof buf, "%d / %d", cardPage_ + 1, cards.pages);
        o.text(x + (cw - o.width(buf, 10)) / 2, y + ch / 2 + (cards.compact ? 0.0f : 2.0f), buf, kDim, 10);
        hits_.push_back({x, y, arrow, ch, kCardPageBack});
        hits_.push_back({x + cw - arrow, y, arrow, ch, kCardPageOn});
        hits_.push_back({x, y, cw, ch, kCardPageMore});  // (after the arrows: the first rectangle a press is in takes it)
    }

    // The lord's Rally, a button over the cards' right end (key Y): ready with broken men in earshot, or counting down.
    if (const Json* rally = hud.find("rally"); rally && rally->flag("lord") && squadCards) {
        const int cd = static_cast<int>(rally->num("cooldown")), heard = static_cast<int>(rally->num("heard"));
        if (cd > 0) std::snprintf(buf, sizeof buf, "Rally  %d s", cd);
        else if (heard > 0) std::snprintf(buf, sizeof buf, "Rally %d broken  (Y)", heard);
        else std::snprintf(buf, sizeof buf, "Rally  (Y)");
        const bool ready = cd == 0 && heard > 0;
        const float bw = o.width(buf, 13) + 20, bx = w - bw - 8, by = cardsTop - 56;
        o.rect(bx, by, bw, 22, rgba(0x0E0B08, ready ? 230 : 170));
        o.frame(bx, by, bw, 22, ready ? 2.0f : 1.0f, ready ? rgba(0xFFFFFF) : rgba(0x7C6E50));
        o.text(bx + 10, by + 4, buf, ready ? rgba(0xFFFFFF) : kDim, 13);
        hits_.push_back({bx, by, bw, 22, kRallyHit});
    }

    // The trap's buttons over the cards, left of the Rally: shown with a man selected who lays, sparks or carries a store.
    if (squadCards && sel > 0 && (hud.flag("lays") || hud.flag("sparks") || hud.flag("stores"))) {
        const int mode = static_cast<int>(hud.num("mode"));
        struct Btn {
            const char* label;
            int hit;
            bool show, lit;
        };
        const Btn btns[] = {{"Lay satchel (T)", kLayHit, hud.flag("lays"), mode == 3},
                            {"Detonate (L)", kDetonateHit, hud.flag("sparks"), mode == 4},
                            {hud.flag("holdFire") ? "Holding fire (Shift+K)" : "Hold fire (Shift+K)", kHoldFireHit, hud.flag("stores"), hud.flag("holdFire")}};
        float bx = 8.0f;
        const float by = cardsTop - 84;  // (above the selection's line, the first-order hint and a card's tip)
        for (const Btn& b : btns) {
            if (!b.show) continue;
            const float bw = o.width(b.label, 13) + 20;
            o.rect(bx, by, bw, 22, rgba(0x0E0B08, 220));
            o.frame(bx, by, bw, 22, b.lit ? 2.0f : 1.0f, b.lit ? kGold : rgba(0x7C6E50));
            o.text(bx + 10, by + 4, b.label, b.lit ? kGold : kInk, 13);
            hits_.push_back({bx, by, bw, 22, b.hit});
            bx += bw + 6;
        }
        if (mode == 3 || mode == 4) {
            const char* what = mode == 3 ? "Click to lay. Shift+click: a line." : "Click a satchel. Red ring: your men are in it.";
            o.rect(bx + 6, by, o.width(what, 13) + 16, 22, rgba(0x000000, 190));
            o.text(bx + 14, by + 4, what, kGold, 13);
        }
    }

    drawMinimap(o, w, h);
    town_ = -1;
    if (conquest && !menu_) {
        if (const Json* city = camp.find("city")) drawTown(o, *city, static_cast<int>(camp.num("gold")), w);
    }
    // A sheet laid down: the town's screen, the results.
    const bool townUp = town_ >= 0, resultUp = hud.find("result") != nullptr;
    if ((townUp && !townWas_) || (resultUp && !resultWas_)) audio::uiCue("snd_ui_page_turn");
    townWas_ = townUp;
    resultWas_ = resultUp;

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
        o.rect(w - o.width(buf, 13) - 20, cardsTop - 28, o.width(buf, 13) + 20, 22, rgba(0x000000, 150));
        o.text(w - o.width(buf, 13) - 10, cardsTop - 24, buf, kRed, 13);
    }

    if (const Json* march = hud.find("march"); march && march->flag("settled") && marchSaved_ != static_cast<int>(march->num("battle"))) {
        marchSaved_ = static_cast<int>(march->num("battle"));
        saveMarch();  // the book as the battle left it: what "The March" carries on from
    }
    drawResult(o, hud, w, h);

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
              {"Right hold, drag", "hold still, then drag: march, facing the drag"},
              {"Shift + right click", "queue it after the order in hand"},
              {"Ctrl or Cmd + click", "attack, even a friend"},
              {"Alt + click", "force march"},
              {"Right drag", "turn the view (start moving at once)"},
              {"Middle drag", "pan"},
              {"Wheel", "zoom (trackpad: swipe pans, pinch zooms)"}}},
            {"Keys",
             {{"W A S D, arrows", "pan"},
              {"Q E, R F, N", "turn, zoom, north"},
              {"M", "show or hide the map (click or drag it to look)"},
              {"Z, V", "attack mode, force march (one order)"},
              {"Space, B", "stop the march (halt)"},
              {"K", "hold: keep their place (again: support friends near)"},
              {"J", "the next squad standing idle"},
              {". (full stop)", "send the nearest free squad to a call for help"},
              {"Y", "the lord rallies broken men within 40 m"},
              {"T, X, C", "special, scatter, find selection"},
              {"T + click", "lay a satchel (Shift: a line)"},
              {"L + click", "detonate a satchel"},
              {"Shift + K", "hold fire"},
              {"O, Shift + O", "open or shut a gate of yours, let it go"},
              {"1-9, Alt + 1-9", "pick a group, save one"},
              {"Shift + 1-9, 0", "formation, scatter"},
              {"H, Ctrl + A", "heroes, all on screen"},
              {"Tab, Shift + Tab", "next, previous group of units"},
              {"`, Backspace", "plan: hold orders, send them / drop them"},
              {"Esc", "pause and bring up the menu (or cancel a mode)"},
              {"P, - =", "pause, slower and faster"},
              {"Enter, /", "begin the battle (else the menu), this help"},
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
        // The first minute, under the two columns: the path from a click on your men to a march facing the way you want.
        static const char* const kFirst[] = {
            "1  Click one of your men, or drag a box round them. Click him again: all of his kind in view.",
            "2  Right-click the ground (in a Conquest, a town): they march there. Right-click a foe: they attack.",
            "3  To face them too, hold the right button still for a moment, then drag the way they should face.",
            "4  Space halts them, P pauses, Enter is the menu. In a Conquest the bar under the top line says how soon they arrive."};
        std::vector<std::string> first;
        for (const char* l : kFirst)
            for (const std::string& part : wrap(o, l, kSize, pw - 2 * kPad)) first.push_back(part);
        const float ph = 2 * kPad + (static_cast<float>(rowsMax) + 1) * kLine + 4 + 10 + (static_cast<float>(first.size()) + 1) * kLine;
        const float x0 = w / 2 - pw / 2, y0 = 60;
        o.paper(x0, y0, pw, ph, 19);
        {
            float fy = y0 + kPad + (static_cast<float>(rowsMax) + 1) * kLine + 14;
            o.text(x0 + kPad, fy, "The first minute", kSeal, kSize);
            for (const std::string& l : first) {
                fy += kLine;
                o.text(x0 + kPad, fy, l, kPaperInk, kSize);
            }
        }
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
