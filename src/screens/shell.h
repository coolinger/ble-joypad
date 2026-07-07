// Persistent MFD frame on lv_layer_top(): metrics strip (top), page-tab rail
// (right, at the physical TTP223 pads), status footer (bottom). Created once;
// overlays every screen. Content zone for pages: 0,44 .. 446,252.
#pragma once

#include <lvgl.h>
#include "../config.h"   // SCREEN_WIDTH/SCREEN_HEIGHT

#define SHELL_STRIP_H   44
#define SHELL_RAIL_W    34
#define SHELL_FOOTER_H  20
#define CONTENT_X 0
#define CONTENT_Y SHELL_STRIP_H
#define CONTENT_W (SCREEN_WIDTH - SHELL_RAIL_W)    // 446
#define CONTENT_H (SCREEN_HEIGHT - SHELL_STRIP_H - SHELL_FOOTER_H)  // 208

extern lv_obj_t *shell_jumps_label;   // big jump count
extern lv_obj_t *shell_fuel_arc;
extern lv_obj_t *shell_fuel_label;    // cyan % value
extern lv_obj_t *shell_hull_arc;
extern lv_obj_t *shell_hull_label;
extern lv_obj_t *shell_cargo_label;   // "12/128"
extern lv_obj_t *shell_mode_label;    // footer right: ON FOOT/SHIP/...
// Relocated from the old info-screen header/sidebar (names kept so the
// existing update functions in main.cpp stay valid):
extern lv_obj_t *wifi_icon;
extern lv_obj_t *websocket_icon;
extern lv_obj_t *bluetooth_icon;
extern lv_obj_t *status_label;        // footer left: current system name

void create_shell_ui();               // call once in setup() after lv_init
void shell_set_active_tab(int page);  // 0=FTR 1=LOG 2=SYS; hold lvglMutex
