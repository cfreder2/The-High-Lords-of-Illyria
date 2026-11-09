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

}  // namespace

// Ctrl (or Cmd) with a click: attack whatever is under it; Alt: force march.
void Input::modifiers(SDL_Keymod mod) { illyria_set_keys((mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) ? 1 : 0, (mod & SDL_KMOD_ALT) ? 1 : 0); }

void Input::pointer(int type, const SDL_Event& e, float x, float y, int button) {
    (void)e;
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
            if (orderMode_) setMode(0);
            else illyria_clear_selection();
            break;
        case SDL_SCANCODE_SPACE: {
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
            if (!e.motion.state) illyria_hover(e.motion.x, e.motion.y);
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
