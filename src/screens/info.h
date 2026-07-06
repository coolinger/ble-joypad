#pragma once

#include <lvgl.h>
#include "../gamedata.h"   // MAX_PINNED_BODIES

extern lv_obj_t *logviewer_screen;
extern lv_obj_t *log_label;
extern lv_obj_t *cargo_bar;
extern lv_obj_t *header_label;
extern lv_obj_t *fuel_bar;
extern lv_obj_t *hull_bar;
extern lv_obj_t *wifi_icon;
extern lv_obj_t *websocket_icon;
extern lv_obj_t *bluetooth_icon;
extern lv_obj_t *backpack_panel;
extern lv_obj_t *medpack_label;
extern lv_obj_t *energycell_label;
extern lv_obj_t *bioscan_label;
extern lv_obj_t *bioscan_data_label;
extern lv_obj_t *jump_overlay_label;
extern lv_obj_t *status_label;   // sidebar header: current system name

// Sidebar cards for pinned body signals (filled by updatePinnedSidebar in main)
extern lv_obj_t *pin_cards[MAX_PINNED_BODIES];
extern lv_obj_t *pin_title_labels[MAX_PINNED_BODIES];
extern lv_obj_t *pin_genus_labels[MAX_PINNED_BODIES];

void create_logviewer_ui();
