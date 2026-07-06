#pragma once
#ifndef SRC_COLORS_H
#define SRC_COLORS_H

#include <lvgl.h>

// V5 MFD palette: true black ground, one leading orange, cyan only for
// measured values, red only for warnings. (Spec 2026-07-06.)
#define LV_COLOR_BG           lv_color_hex(0x020101)  // ground
#define LV_COLOR_FG           lv_color_hex(0xff7100)  // leading orange
#define LV_COLOR_HIGHLIGHT_BG lv_color_hex(0xffa617)  // pressed/highlight fill
#define LV_COLOR_HIGHLIGHT_FG lv_color_hex(0x140801)  // text on highlight
#define LV_COLOR_GAUGE_BG     lv_color_hex(0x140801)  // panel/card fill
#define LV_COLOR_GAUGE_FG     lv_color_hex(0xff7100)  // indicator fill
#define LV_COLOR_HAIRLINE     lv_color_hex(0x3a1c00)  // separators/borders
#define LV_COLOR_DIM          lv_color_hex(0x8c4700)  // secondary labels
#define LV_COLOR_VALUE        lv_color_hex(0x35c4f0)  // measured values (cyan)

#define LV_COLOR_LED_ON     lv_color_hex(0xffa718)
#define LV_COLOR_LED_OFF    lv_color_hex(0x7b2c13)
#define LV_COLOR_LED_BORDER lv_color_hex(0xb23f0a)

#define LV_COLOR_WARNING_FG lv_color_hex(0xf30000)
#define LV_COLOR_WARNING_BG lv_color_hex(0x471711)

#define LV_COLOR_OK_FG lv_color_hex(0xc6e6dc)

#endif // SRC_COLORS_H
