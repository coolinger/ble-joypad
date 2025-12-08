#pragma once

#include <lvgl.h>

extern lv_obj_t *settings_screen;
extern lv_obj_t *sys_info_label;
extern lv_obj_t *amplitude_slider;
extern lv_obj_t *amplitude_value_label;

void create_settings_ui();
