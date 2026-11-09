#pragma once

// The game's C functions the page calls through cwrap (game/app/app.cpp, ILLYRIA_API), as the desktop shell
// calls them: the same entry points for input, the HUD's state, and starting a mode. Only the ones it uses.

#include <cstdint>

extern "C" {
const char* illyria_version();
int illyria_start_mode(const char* id, const char* options);
const char* illyria_mode_error();
const char* illyria_perf();
const char* illyria_draw_counts();
void illyria_hide_parts(unsigned mask);
void illyria_set_tree_style(int style);

void illyria_pointer(int type, int id, float x, float y, int button, double timeMs);
void illyria_wheel(float deltaY, float x, float y);
void illyria_pinch_wheel(float deltaY, float x, float y);
void illyria_scroll(float dx, float dy, float x, float y);
void illyria_pinch(float factor, float radians, float x, float y);
void illyria_camera_keys(int right, int forward, int turn, int zoom);
const char* illyria_camera_state();
void illyria_north_up();
void illyria_set_camera(float x, float y, float distance, float yaw);
#ifdef ILLYRIA_DEV_HOOKS
void illyria_paths_demo();  // --paths-demo: see game/app/app.cpp
#endif
void illyria_finish_detail();
void illyria_set_shift(int down);
void illyria_set_keys(int force, int march);
void illyria_set_order_mode(int mode);
int illyria_hover(float x, float y);

void illyria_select_squad(int index);
void illyria_focus_squad(int index);
void illyria_clear_selection();
void illyria_select_heroes();
void illyria_select_on_screen();
void illyria_focus_selection();
void illyria_set_formation(int f);
void illyria_scatter();
int illyria_group(int op, int slot);
int illyria_ungroup();
const char* illyria_formations();
void illyria_stop();
void illyria_gate(int release);
void illyria_special();
void illyria_toggle_pause();
void illyria_cycle_speed();
void illyria_set_pace(float pace);
void illyria_restart();

const char* illyria_hud();
const char* illyria_labels();
const char* illyria_drag_box();

void illyria_campaign_pause();
void illyria_campaign_speed();
void illyria_campaign_select(unsigned id);
const char* illyria_campaign_state();
const char* illyria_campaign_save();
void illyria_campaign_start(int hard);
}
