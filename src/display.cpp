#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <bb_captouch.h>
#include <esp_heap_caps.h>
#include "ed_logo.h"

// NV3041A over QSPI (the JC4827W543's native path, per the vendor demos).
// The panel handles rotation itself (DISPLAY_ROTATION in config.h); LVGL
// renders in logical 480x272 and the flush below pushes 1:1.
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_QSPI_CS, LCD_QSPI_CLK, LCD_QSPI_D0, LCD_QSPI_D1, LCD_QSPI_D2, LCD_QSPI_D3);
static Arduino_NV3041A *panel = new Arduino_NV3041A(
    bus, GFX_NOT_DEFINED /* RST */, DISPLAY_ROTATION, true /* IPS */);

// GT911 capacitive touch on its own I2C pins (bb_captouch drives Wire).
static BBCapTouch touch;

// Two LVGL draw buffers (double buffering) in PSRAM.
static lv_color_t *lv_buf1 = nullptr;
static lv_color_t *lv_buf2 = nullptr;

static void (*touchCallback)() = nullptr;  // called on every valid touch (display wake)

#if LV_USE_LOG != 0
static void lvgl_log_cb(lv_log_level_t level, const char *buf)
{
    LV_UNUSED(level);
    Serial.print("[LVGL] ");
    Serial.print(buf);
}
#endif

/* Blocking flush. Arduino_GFX's writePixels handles the RGB565 byte order on
   the QSPI bus itself, so LVGL renders in native order (no swap pass). */
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    panel->startWrite();
    panel->setAddrWindow(area->x1, area->y1, w, h);
    panel->writePixels((uint16_t *)px_map, w * h);
    panel->endWrite();

    lv_display_flush_ready(disp);
}

static void touchpad_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    LV_UNUSED(indev);
    TOUCHINFO ti;
    if (touch.getSamples(&ti) && ti.count > 0) {
        int x = ti.x[0];
        int y = ti.y[0];
#if DISPLAY_ROTATION == 2
        // GT911 reports panel-native coordinates; mirror for the 180deg mount.
        x = SCREEN_WIDTH - 1 - x;
        y = SCREEN_HEIGHT - 1 - y;
#endif
        if (x < 0) x = 0; if (x >= SCREEN_WIDTH)  x = SCREEN_WIDTH - 1;
        if (y < 0) y = 0; if (y >= SCREEN_HEIGHT) y = SCREEN_HEIGHT - 1;
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
        if (touchCallback) touchCallback();
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

lv_display_t* Display::init(void)
{
    if (!panel->begin()) {
        Serial.println("[GFX] ERROR: panel->begin() failed");
    }
    panel->fillScreen(RGB565_BLACK);  // clear stale GRAM surviving from before reset

    /* Boot splash (Elite Dangerous logo), centered. Stays visible while the
       rest of setup() runs, until the first LVGL frame replaces it. */
    panel->draw16bitRGBBitmap((panel->width()  - ED_LOGO_W) / 2,
                              (panel->height() - ED_LOGO_H) / 2,
                              (uint16_t *)ed_logo_map, ED_LOGO_W, ED_LOGO_H);

    int tres = touch.init(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
    Serial.printf("[TOUCH] bb_captouch init: %d, sensor type: %d\n",
                  tres, touch.sensorType());  // expect CT_TYPE_GT911

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
#if LV_USE_LOG != 0
    lv_log_register_print_cb(lvgl_log_cb);
#endif

    /* Two partial draw buffers in PSRAM (64-byte aligned for cache-coherent
       reads by the SPI master). Falls back to internal RAM if PSRAM missing. */
    const size_t buf_bytes = LVGL_BUFFER_PIXELS * 2;  // RGB565
    lv_buf1 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM);
    lv_buf2 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM);
    if (!lv_buf1 || !lv_buf2) {
        Serial.println("[LVGL] PSRAM draw buffers failed, using internal RAM");
        if (lv_buf1) { heap_caps_free(lv_buf1); lv_buf1 = nullptr; }
        if (lv_buf2) { heap_caps_free(lv_buf2); lv_buf2 = nullptr; }
        lv_buf1 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        lv_buf2 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    Serial.printf("[LVGL] draw buffers: %u px x2 @ %p / %p\n",
                  (unsigned)LVGL_BUFFER_PIXELS, lv_buf1, lv_buf2);

    lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_buffers(disp, lv_buf1, lv_buf2, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read_cb);

    return disp;
}

void Display::routine(void)
{
    lv_timer_handler();
}

void Display::setTouchCallback(void (*cb)())
{
    touchCallback = cb;
}
