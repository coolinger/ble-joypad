#pragma once

#include <lvgl.h>
#include "../gamedata.h"   // MAX_PINNED_BODIES

extern lv_obj_t *logviewer_screen;
extern lv_obj_t *log_label;        // event texts (left column)
extern lv_obj_t *jump_overlay_label;

// SIGNALS sidebar (filled by updatePinnedSidebarUnlocked in main.cpp; caller
// must hold lvglMutex): dynamic header rail with the system tally, ONE card
// for the body currently in gravity influence (status.nearBodyId), and
// compact per-category body lists (0=BIO, 1=GEO, 2=OTH).
extern lv_obj_t *signals_rail_label;
extern lv_obj_t *near_card;
extern lv_obj_t *near_title_label;
extern lv_obj_t *near_genus_label;
extern lv_obj_t *cat_lines[3];

// Context panel (bottom of the sidebar): BACKPACK on foot, EXPLORATION else.
// updateContextPanel (main.cpp) fills the rail + 4 lines.
extern lv_obj_t *ctx_panel;
extern lv_obj_t *ctx_rail_label;
extern lv_obj_t *ctx_lines[4];

void create_logviewer_ui();
