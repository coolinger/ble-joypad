// Central look & feel for all screens (Elite HUD: orange on dark).
// Palette lives in colors.h; this module owns fonts and shared lv_style_t's.
#pragma once

#include <lvgl.h>
#include "colors.h"

// Fonts enabled in lv_conf.h
#define FONT_SMALL  (&lv_font_montserrat_12)
#define FONT_BODY   (&lv_font_montserrat_14)
#define FONT_BIG    (&lv_font_montserrat_20)
#define FONT_JUMBO  (&lv_font_montserrat_28)

// Custom MFD fonts (generated via lv_font_conv, see tools/fonts/; include äöüÄÖÜß)
LV_FONT_DECLARE(font_michroma_24);
LV_FONT_DECLARE(font_michroma_12);
LV_FONT_DECLARE(font_jura_16);
#define FONT_DISPLAY_BIG   (&font_michroma_24)
#define FONT_DISPLAY_LABEL (&font_michroma_12)
#define FONT_BTN           (&font_jura_16)

extern lv_style_t style_panel;        // framed panel (header, info boxes)
extern lv_style_t style_card;         // sidebar card (pinned bodies, backpack)
extern lv_style_t style_btn;          // standard button
extern lv_style_t style_btn_pressed;  // pressed state overlay

// Init all shared styles. Call once in setup() BEFORE any create_*_ui().
void theme_init();
