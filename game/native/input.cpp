#include "input.h"

#include <algorithm>
#include <cmath>

#include "api.h"
#include "json.h"

namespace illyria::native {

namespace {

// The camera keys, as the page's kPanKeys / kTurnKeys / kZoomKeys.
struct Pan {
    SDL_Scancode code;
    int across, ahead;
    bool arrow;
};
constexpr Pan kPan[] = {{SDL_SCANCODE_LEFT, -1, 0, true}, {SDL_SCANCODE_RIGHT, 1, 0, true}, {SDL_SCANCODE_UP, 0, 1, true}, {SDL_SCANCODE_DOWN, 0, -1, true},
                        {SDL_SCANCODE_A, -1, 0, false},   {SDL_SCANCODE_D, 1, 0, false},    {SDL_SCANCODE_W, 0, 1, false}, {SDL_SCANCODE_S, 0, -1, false}};

bool isCameraKey(SDL_Scancode c) {
    for (const Pan& p : kPan)
        if (p.code == c) return true;
    return c == SDL_SCANCODE_Q || c == SDL_SCANCODE_E || c == SDL_SCANCODE_R || c == SDL_SCANCODE_F;
}

// 0-9 on the main row or the keypad: the digit, or -1.
int digitOf(SDL_Scancode c) {
    if (c >= SDL_SCANCODE_1 && c <= SDL_SCANCODE_9) return c - SDL_SCANCODE_1 + 1;
    if (c == SDL_SCANCODE_0) return 0;
    if (c >= SDL_SCANCODE_KP_1 && c <= SDL_SCANCODE_KP_9) return c - SDL_SCANCODE_KP_1 + 1;
    if (c == SDL_SCANCODE_KP_0) return 0;
    return -1;
}

bool keypad(SDL_Scancode c) { return c >= SDL_SCANCODE_KP_1 && c <= SDL_SCANCODE_KP_0; }

// The desktop cursor says what a click would do, as the page's kCursors do (Battle::Intent order):
// None, Select, Attack, AttackObject, AttackGround, Cannot, Move, ForceMarch, GateSwing.
struct Shape {
    enum Kind { Line, Ring } kind;
    float a, b, c, d;  // Line: x0 y0 x1 y1. Ring: cx cy radius (d unused)
};

// Draws shapes each as a black stroke 4 wide under a colour stroke 2 wide, into a 32 x 32 cursor.
SDL_Cursor* drawnCursor(const std::vector<Shape>& shapes, Uint8 r, Uint8 g, Uint8 b) {
    SDL_Surface* s = SDL_CreateSurface(32, 32, SDL_PIXELFORMAT_RGBA32);
    if (s == nullptr) return nullptr;
    auto* px = static_cast<Uint8*>(s->pixels);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 32; ++x) {
            float best = 1e9f;
            for (const Shape& sh : shapes) {
                float d;
                if (sh.kind == Shape::Ring) {
                    d = std::abs(std::hypot(x + 0.5f - sh.a, y + 0.5f - sh.b) - sh.c);
                } else {
                    const float vx = sh.c - sh.a, vy = sh.d - sh.b;
                    const float len2 = vx * vx + vy * vy;
                    const float t = len2 > 0 ? std::clamp(((x + 0.5f - sh.a) * vx + (y + 0.5f - sh.b) * vy) / len2, 0.0f, 1.0f) : 0.0f;
                    d = std::hypot(x + 0.5f - (sh.a + t * vx), y + 0.5f - (sh.b + t * vy));
                }
                best = std::min(best, d);
            }
            Uint8* p = px + y * s->pitch + x * 4;
            if (best <= 1.0f) p[0] = r, p[1] = g, p[2] = b, p[3] = 255;
            else if (best <= 2.0f) p[0] = p[1] = p[2] = 0, p[3] = 255;
            else p[0] = p[1] = p[2] = p[3] = 0;
        }
    SDL_Cursor* c = SDL_CreateColorCursor(s, 16, 16);
    SDL_DestroySurface(s);
    return c;
}

SDL_Cursor* cursorFor(int intent) {
    static SDL_Cursor* cache[9] = {};
    static bool made[9] = {};
    if (intent < 0 || intent > 8) intent = 0;
    if (!made[intent]) {
        made[intent] = true;
        using S = Shape;
        switch (intent) {
            case 1: cache[1] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER); break;
            case 2: cache[2] = drawnCursor({{S::Line, 6, 6, 22, 22}, {S::Line, 18, 26, 26, 18}, {S::Line, 24, 24, 28, 28}}, 255, 74, 58); break;
            case 3: cache[3] = drawnCursor({{S::Ring, 16, 16, 10, 0}, {S::Ring, 16, 16, 4, 0}}, 255, 74, 58); break;
            case 4: cache[4] = drawnCursor({{S::Line, 16, 3, 16, 12}, {S::Line, 16, 20, 16, 29}, {S::Line, 3, 16, 12, 16}, {S::Line, 20, 16, 29, 16}, {S::Line, 16, 16, 16, 16}}, 255, 74, 58); break;
            case 5: cache[5] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_NOT_ALLOWED); break;
            case 7: cache[7] = drawnCursor({{S::Line, 8, 6, 18, 16}, {S::Line, 18, 16, 8, 26}, {S::Line, 16, 6, 26, 16}, {S::Line, 26, 16, 16, 26}}, 255, 211, 110); break;
            case 8: cache[8] = drawnCursor({{S::Line, 6, 26, 6, 8}, {S::Line, 6, 8, 16, 4}, {S::Line, 16, 4, 26, 8}, {S::Line, 26, 8, 26, 26}, {S::Line, 16, 4, 16, 26}}, 255, 211, 110); break;
            default: break;  // None and Move: the arrow
        }
    }
    return cache[intent] != nullptr ? cache[intent] : SDL_GetDefaultCursor();
}

}  // namespace

// Ctrl (or Cmd) with a click: attack whatever is under it; Alt: force march.
void Input::modifiers(SDL_Keymod mod) { illyria_set_keys((mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) ? 1 : 0, (mod & SDL_KMOD_ALT) ? 1 : 0); }

void Input::pointer(int type, const SDL_Event& e, float x, float y, int button) {
    (void)e;
    illyria_set_mouse(1);  // left selects, right orders, middle pans (Gestures)
    illyria_pointer(type, 1, x, y, button, static_cast<double>(SDL_GetTicks()));
}

void Input::sendCameraKeys(bool shift) {
    int across = 0, ahead = 0, turn = 0, zoom = 0;
    for (SDL_Scancode code : held_) {
        bool panned = false;
        for (const Pan& p : kPan) {
            if (p.code != code) continue;
            panned = true;
            // Shift turns the view with the arrows (W A S D keep panning: shift is the box's).
            if (shift && p.arrow) turn -= p.across;
            else across += p.across;
            ahead += p.ahead;
        }
        if (panned) continue;
        if (code == SDL_SCANCODE_Q) turn += 1;
        if (code == SDL_SCANCODE_E) turn -= 1;
        if (code == SDL_SCANCODE_R) zoom += 1;
        if (code == SDL_SCANCODE_F) zoom -= 1;
    }
    const auto one = [](int v) { return std::clamp(v, -1, 1); };
    illyria_camera_keys(one(across), one(ahead), one(turn), one(zoom));
}

void Input::setMode(int mode) {
    illyria_set_order_mode(mode);
    orderMode_ = mode;
}

void Input::pickFormation(int n) {
    if (formationIds_.empty()) {
        Json f;
        if (Json::parse(illyria_formations(), f))
            for (const Json& x : f.items) formationIds_.push_back(static_cast<int>(x.num("id")));
    }
    if (n == 0) illyria_scatter();
    else if (static_cast<size_t>(n) <= formationIds_.size()) illyria_set_formation(formationIds_[static_cast<size_t>(n) - 1]);
}

void Input::stepPace(int by) {
    static const float kPaces[] = {0.25f, 0.5f, 1, 2, 4};
    Json hud;
    Json::parse(illyria_hud(), hud);
    const float slow = static_cast<float>(hud.num("slow", 1));
    const float pace = slow < 0.999f ? slow : static_cast<float>(hud.num("speed", 1));
    size_t at = 0;
    for (size_t i = 0; i < 5; ++i)
        if (std::abs(kPaces[i] - pace) < std::abs(kPaces[at] - pace)) at = i;
    illyria_set_pace(kPaces[std::clamp(static_cast<int>(at) + by, 0, 4)]);
}

void Input::key(const SDL_KeyboardEvent& k) {
    const SDL_Scancode c = k.scancode;
    const bool shift = (k.mod & SDL_KMOD_SHIFT) != 0;
    const bool ctrl = (k.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, alt = (k.mod & SDL_KMOD_ALT) != 0;

    if (!k.down) {
        if (c == SDL_SCANCODE_LSHIFT || c == SDL_SCANCODE_RSHIFT) illyria_set_shift(0);
        if (c == SDL_SCANCODE_LCTRL || c == SDL_SCANCODE_RCTRL || c == SDL_SCANCODE_LGUI || c == SDL_SCANCODE_RGUI || c == SDL_SCANCODE_LALT ||
            c == SDL_SCANCODE_RALT)
            modifiers(k.mod);
        if (c == SDL_SCANCODE_LGUI || c == SDL_SCANCODE_RGUI) held_.clear();  // (a Mac sends no key up for a key let go while Cmd is down)
        if (held_.erase(c) || c == SDL_SCANCODE_LSHIFT || c == SDL_SCANCODE_RSHIFT || c == SDL_SCANCODE_LGUI || c == SDL_SCANCODE_RGUI) sendCameraKeys(shift);
        return;
    }

    if (hud_.menuOpen()) {
        if (k.repeat && c != SDL_SCANCODE_UP && c != SDL_SCANCODE_DOWN) return;
        switch (c) {
            case SDL_SCANCODE_UP: hud_.menuKey(Key::Up); break;
            case SDL_SCANCODE_DOWN: hud_.menuKey(Key::Down); break;
            case SDL_SCANCODE_LEFT: hud_.menuKey(Key::Left); break;
            case SDL_SCANCODE_RIGHT: hud_.menuKey(Key::Right); break;
            case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: hud_.menuKey(Key::Enter); break;
            case SDL_SCANCODE_ESCAPE: hud_.menuKey(Key::Back); break;
            case SDL_SCANCODE_D: hud_.menuToggleDifficulty(); break;
            default:
                if (const int d = digitOf(c); d >= 1) hud_.menuDigit(d);
        }
        return;
    }

    if (c == SDL_SCANCODE_LSHIFT || c == SDL_SCANCODE_RSHIFT) {
        illyria_set_shift(1);
        sendCameraKeys(true);
    }
    if (c == SDL_SCANCODE_LCTRL || c == SDL_SCANCODE_RCTRL || c == SDL_SCANCODE_LGUI || c == SDL_SCANCODE_RGUI || c == SDL_SCANCODE_LALT ||
        c == SDL_SCANCODE_RALT)
        modifiers(k.mod);
    if (isCameraKey(c) && !ctrl && !alt) {
        held_.insert(c);
        sendCameraKeys(shift);
        return;
    }
    if (c == SDL_SCANCODE_N && !k.repeat && !ctrl && !alt) {
        illyria_north_up();
        return;
    }
    if (k.repeat) return;
    if (c == SDL_SCANCODE_RETURN || c == SDL_SCANCODE_KP_ENTER) {
        hud_.openMenu();
        return;
    }
    if (c == SDL_SCANCODE_SLASH) {
        hud_.toggleHelp();
        return;
    }
    if (const int d = digitOf(c); d >= 0) {
        if (keypad(c) || shift) pickFormation(d);
        else if (alt || ctrl) illyria_group(1, d);
        else if (illyria_group(0, d) >= 0) {
            const Uint64 now = SDL_GetTicks();
            if (lastGroup_ == d && now - lastGroupMs_ < 450) illyria_focus_selection();
            lastGroup_ = d;
            lastGroupMs_ = now;
        }
        return;
    }
    if (c == SDL_SCANCODE_A && ctrl && !alt) {
        illyria_select_on_screen();
        return;
    }
    if (ctrl || alt) return;  // (the system's shortcuts)
    switch (c) {
        case SDL_SCANCODE_X: illyria_scatter(); break;
        case SDL_SCANCODE_Z: setMode(orderMode_ == 1 ? 0 : 1); break;
        case SDL_SCANCODE_G: illyria_group(2, 0); break;
        case SDL_SCANCODE_V: case SDL_SCANCODE_M: setMode(orderMode_ == 2 ? 0 : 2); break;
        case SDL_SCANCODE_T: case SDL_SCANCODE_L: illyria_special(); break;
        case SDL_SCANCODE_B: case SDL_SCANCODE_H: illyria_stop(); break;
        case SDL_SCANCODE_O: illyria_gate(shift ? 1 : 0); break;
        case SDL_SCANCODE_C: illyria_focus_selection(); break;
        case SDL_SCANCODE_U: illyria_ungroup(); break;
        case SDL_SCANCODE_GRAVE: illyria_select_heroes(); break;
        case SDL_SCANCODE_ESCAPE:
            // Cancels an order mode in hand; else pauses the game and brings up the menu (Esc again: back).
            if (orderMode_) setMode(0);
            else hud_.openMenu();
            break;
        case SDL_SCANCODE_SPACE: illyria_stop(); break;  // the march stops: the selection halts
        case SDL_SCANCODE_P: {
            Json camp;
            Json::parse(illyria_campaign_state(), camp);
            if (camp.find("day")) illyria_campaign_pause();
            else illyria_toggle_pause();
            break;
        }
        case SDL_SCANCODE_MINUS: case SDL_SCANCODE_KP_MINUS: stepPace(-1); break;
        case SDL_SCANCODE_EQUALS: case SDL_SCANCODE_KP_PLUS: stepPace(1); break;
        default: break;
    }
}

void Input::handle(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            if (e.button.which == SDL_TOUCH_MOUSEID) break;
            if (hud_.click(e.button.x, e.button.y)) {
                captured_ = true;
                break;
            }
            if (hud_.menuOpen()) break;
            modifiers(SDL_GetModState());
            pointer(0, e, e.button.x, e.button.y, e.button.button - 1);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_UP: {
            if (captured_) {
                captured_ = false;
                break;
            }
            if (hud_.menuOpen()) break;
            pointer(2, e, e.button.x, e.button.y, e.button.button - 1);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION: {
            if (captured_ || hud_.menuOpen()) break;
            pointer(1, e, e.motion.x, e.motion.y, 0);
            if (!e.motion.state) SDL_SetCursor(cursorFor(illyria_hover(e.motion.x, e.motion.y)));
            else if (e.motion.state & SDL_BUTTON_RMASK) SDL_SetCursor(SDL_GetDefaultCursor());
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL: {
            if (hud_.menuOpen()) break;
            const float x = e.wheel.mouse_x, y = e.wheel.mouse_y;
            const Uint64 now = SDL_GetTicks();
            // A mouse's wheel zooms; a trackpad's two-finger swipe pans (the page tells them apart the same way: whole notches or not).
            const bool fine = e.wheel.x != 0 || std::abs(e.wheel.y - std::round(e.wheel.y)) > 1e-3f;
            if (fine || now - wheelMs_ > 300) trackpad_ = fine;
            wheelMs_ = now;
            if (trackpad_) illyria_scroll(e.wheel.x * 10.0f, -e.wheel.y * 10.0f, x, y);
            else illyria_wheel(-e.wheel.y * 100.0f, x, y);
            break;
        }
        case SDL_EVENT_PINCH_UPDATE: {
            if (hud_.menuOpen()) break;
            float x = 0, y = 0;
            SDL_GetMouseState(&x, &y);
            illyria_pinch(e.pinch.scale, 0, x, y);
            break;
        }
        case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP: key(e.key); break;
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            illyria_set_shift(0);
            held_.clear();
            sendCameraKeys(false);
            break;
        default: break;
    }
}

}  // namespace illyria::native
