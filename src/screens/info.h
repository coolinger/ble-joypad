#pragma once

#include <lvgl.h>

extern lv_obj_t *logviewer_screen;
extern lv_obj_t *log_label;
extern lv_obj_t *cargo_bar;
extern lv_obj_t *header_label;
extern lv_obj_t *fuel_bar;
extern lv_obj_t *hull_bar;
extern lv_obj_t *wifi_icon;
extern lv_obj_t *websocket_icon;
extern lv_obj_t *bluetooth_icon;
extern lv_obj_t *battery_icon;
extern lv_obj_t *medpack_label;
extern lv_obj_t *energycell_label;
extern lv_obj_t *bioscan_label;
extern lv_obj_t *bioscan_data_label;
extern lv_obj_t *jump_overlay_label;
extern lv_obj_t *status_label;

void create_logviewer_ui();
