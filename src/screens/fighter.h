#pragma once

// Forward declarations and state for fighter screen
#include <lvgl.h>

struct FighterCommand {
	const char *name;
	uint8_t button_id;
};

extern lv_obj_t *fighter_screen;
extern lv_obj_t *btnmatrix;

void create_fighter_ui();
