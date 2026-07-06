/**
 * lv_conf.h for LVGL 9.x - minimal override set. Anything not defined here
 * falls back to the lv_conf_internal.h default. Found by LVGL through the
 * -DLV_CONF_INCLUDE_SIMPLE and "-I ." build flags in platformio.ini.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

/* LVGL heap in PSRAM: widget tree for the 480x272 dashboard outgrows what we
   want to spend of internal RAM (lwIP needs headroom there, see CHANGELOG). */
#define LV_USE_STDLIB_MALLOC LV_STDLIB_BUILTIN
#define LV_MEM_SIZE (256 * 1024U)
#define LV_MEM_POOL_INCLUDE "esp_heap_caps.h"
#define LV_MEM_POOL_ALLOC(size) heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

/* Fonts for the 480x272 layout (flash budget: only what the UI uses) */
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_28 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

#endif /* LV_CONF_H */
