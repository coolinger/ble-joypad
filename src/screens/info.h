#pragma once

#include <lvgl.h>
#include "../gamedata.h"   // MAX_PINNED_BODIES

extern lv_obj_t *logviewer_screen;
extern lv_obj_t *log_label;        // event texts (left part of the column)
extern lv_obj_t *log_time_label;   // right-aligned relative ages, same line grid
extern lv_obj_t *jump_overlay_label;

// Pinned body signal cards (filled by updatePinnedSidebarUnlocked in main.cpp;
// caller must hold lvglMutex)
extern lv_obj_t *pin_cards[MAX_PINNED_BODIES];
extern lv_obj_t *pin_title_labels[MAX_PINNED_BODIES];
extern lv_obj_t *pin_genus_labels[MAX_PINNED_BODIES];

// Context panel (bottom of the sidebar): BACKPACK on foot, EXPLORATION else.
// updateContextPanel (main.cpp) fills the rail + 4 lines.
extern lv_obj_t *ctx_panel;
extern lv_obj_t *ctx_rail_label;
extern lv_obj_t *ctx_lines[4];

void create_logviewer_ui();
