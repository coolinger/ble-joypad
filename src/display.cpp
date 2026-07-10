#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <bb_captouch.h>
#include <esp_heap_caps.h>
#include <math.h>

// NV3041A over QSPI (the JC4827W543's native path, per the vendor demos).
// The panel handles rotation itself (DISPLAY_ROTATION in config.h); LVGL
// renders in logical 480x272 and the flush below pushes 1:1.
static Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    LCD_QSPI_CS, LCD_QSPI_CLK, LCD_QSPI_D0, LCD_QSPI_D1, LCD_QSPI_D2, LCD_QSPI_D3);
static Arduino_NV3041A *panel = new Arduino_NV3041A(
    bus, GFX_NOT_DEFINED /* RST */, DISPLAY_ROTATION, true /* IPS */);

// GT911 capacitive touch on its own I2C pins (bb_captouch drives Wire).
static BBCapTouch touch;

// LVGL draw buffer in PSRAM. Single, full-frame: the flush path is synchronous
// (Arduino_GFX writePixels blocks and lv_display_flush_ready fires inside the
// flush cb), so a second buffer would give zero render/transmit overlap - it
// would just cost another ~255 KB. Full-frame + full-screen invalidation on
// transitions means one writePixels per frame -> no banded-refresh flicker.
static lv_color_t *lv_buf1 = nullptr;

static void (*touchCallback)() = nullptr;  // called on every valid touch (display wake)

extern bool displayOn;  // defined in main.cpp

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
    static bool swallowGesture = false;
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
        if (!displayOn) swallowGesture = true;  // gesture began on a dark screen
        if (touchCallback) touchCallback();      // wake
        if (swallowGesture) {
            // Wake-only gesture: swallow the whole thing so on-glass touches
            // (tab rail, fighter buttons) don't also register as a click.
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        swallowGesture = false;  // finger lifted: next gesture is live
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

/* -------- ED loader ring (boot splash) ----------------------------------
   24 triangles from EDLoader1.svg (viewBox 0..40 x 0..32). Only opacity
   pulses (base -> 1 -> base, 1000ms), staggered per delay-class N so a bright
   wave travels around the ring. Geometry is fixed, so each frame just redraws
   all 24 triangles in their current orange shade over the black screen — no
   per-frame clear, no flicker. */
struct BootTri {
    uint8_t x0, y0, x1, y1, x2, y2;  // viewBox-unit vertices (0..40 / 0..32)
    float   base;                    // pulse floor: 0.3 outer ring, 0.4 inner
    uint8_t delayN;                  // delay class N (phase = N*1000/19 ms)
};

static const BootTri kBootTris[24] = {
    // outer ring (l1), base 0.3
    { 5, 8,10,16,15, 8, 0.3f,  1}, { 5, 8,10, 0,15, 8, 0.3f,  2},
    {10, 0,15, 8,20, 0, 0.3f,  3}, {15, 8,20, 0,25, 8, 0.3f,  4},
    {20, 0,25, 8,30, 0, 0.3f,  5}, {25, 8,30, 0,35, 8, 0.3f,  6},
    {25, 8,30,16,35, 8, 0.3f,  7}, {30,16,35, 8,40,16, 0.3f,  8},
    {30,16,35,24,40,16, 0.3f,  9}, {25,24,30,16,35,24, 0.3f, 10},
    {25,24,30,32,35,24, 0.3f, 11}, {20,32,25,24,30,32, 0.3f, 13},
    {15,24,20,32,25,24, 0.3f, 14}, {10,32,15,24,20,32, 0.3f, 15},
    { 5,24,10,32,15,24, 0.3f, 16}, { 5,24,10,16,15,24, 0.3f, 17},
    { 0,16, 5,24,10,16, 0.3f, 18}, { 0,16, 5, 8,10,16, 0.3f, 20},
    // inner ring (l2), base 0.4
    {10,16,15, 8,20,16, 0.4f,  0}, {15, 8,20,16,25, 8, 0.4f,  3},
    {20,16,25, 8,30,16, 0.4f,  6}, {20,16,25,24,30,16, 0.4f,  9},
    {15,24,20,16,25,24, 0.4f, 12}, {10,16,15,24,20,16, 0.4f, 15},
};

static int16_t bootTriPx[24][6];   // scaled display-px vertices
static bool    bootLoaderReady = false;

// phase in [0,1): rise base->1 over first 20%, linear fall 1->base over rest.
static inline float bootPulse(float phase, float base) {
    if (phase < 0.2f) return base + (1.0f - base) * (phase / 0.2f);
    return 1.0f - (1.0f - base) * ((phase - 0.2f) / 0.8f);
}

void Display::bootLoaderInit() {
    if (bootLoaderReady) return;
    const float   s  = BOOT_LOADER_SCALE;
    const int16_t ox = (int16_t)((panel->width()  - (int)(40 * s)) / 2);
    const int16_t oy = (int16_t)((panel->height() - (int)(32 * s)) / 2);
    for (int i = 0; i < 24; i++) {
        const BootTri& t = kBootTris[i];
        bootTriPx[i][0] = ox + (int16_t)(t.x0 * s);
        bootTriPx[i][1] = oy + (int16_t)(t.y0 * s);
        bootTriPx[i][2] = ox + (int16_t)(t.x1 * s);
        bootTriPx[i][3] = oy + (int16_t)(t.y1 * s);
        bootTriPx[i][4] = ox + (int16_t)(t.x2 * s);
        bootTriPx[i][5] = oy + (int16_t)(t.y2 * s);
    }
    bootLoaderReady = true;
}

void Display::drawBootLoaderFrame(uint32_t elapsedMs) {
    if (!bootLoaderReady) return;
    for (int i = 0; i < 24; i++) {
        const BootTri& t = kBootTris[i];
        float delayMs = t.delayN * (1000.0f / 19.0f);
        float phase = fmodf((float)elapsedMs - delayMs, 1000.0f);
        if (phase < 0) phase += 1000.0f;
        phase /= 1000.0f;
        float   b = bootPulse(phase, t.base);
        uint8_t r = (uint8_t)(0xFF * b + 0.5f);
        uint8_t g = (uint8_t)(0x71 * b + 0.5f);
        uint16_t color = RGB565(r, g, 0);
        panel->fillTriangle(bootTriPx[i][0], bootTriPx[i][1],
                            bootTriPx[i][2], bootTriPx[i][3],
                            bootTriPx[i][4], bootTriPx[i][5], color);
    }
}

lv_display_t* Display::init(void)
{
    if (!panel->begin()) {
        Serial.println("[GFX] ERROR: panel->begin() failed");
    }
    panel->fillScreen(RGB565_BLACK);  // clear stale GRAM surviving from before reset

    /* Boot splash: ED loader ring, centered. Frame 0 shows immediately; the
       WiFi-wait loop in setup() animates it. First LVGL frame later replaces it. */
    bootLoaderInit();
    drawBootLoaderFrame(0);

    int tres = touch.init(TOUCH_SDA, TOUCH_SCL, TOUCH_RST, TOUCH_INT);
    Serial.printf("[TOUCH] bb_captouch init: %d, sensor type: %d\n",
                  tres, touch.sensorType());  // expect CT_TYPE_GT911

    lv_init();
    lv_tick_set_cb([]() -> uint32_t { return millis(); });
#if LV_USE_LOG != 0
    lv_log_register_print_cb(lvgl_log_cb);
#endif

    /* One full-frame draw buffer, 64-byte aligned so the SPI master reads it
       cache-coherently. Bytes-per-pixel comes from LVGL's own color-format
       size so it tracks LV_COLOR_DEPTH (lv_conf.h) automatically. */
    const size_t bpp = lv_color_format_get_size(LV_COLOR_FORMAT_NATIVE);
    size_t buf_px = LVGL_BUFFER_PIXELS;
    size_t buf_bytes = buf_px * bpp;
    lv_buf1 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM);
    if (!lv_buf1) {
        // PSRAM full-frame failed (fragmentation / degraded PSRAM). A full
        // frame (~255 KB) can NEVER fit the S3's internal SRAM, so degrade to a
        // 1/4-screen buffer in internal RAM: banded refresh returns (some
        // transition flicker) but the device stays usable instead of bricking.
        buf_px = (size_t)SCREEN_WIDTH * SCREEN_HEIGHT / 4;
        buf_bytes = buf_px * bpp;
        Serial.println("[LVGL] PSRAM full-frame buffer failed, using 1/4-screen internal RAM");
        lv_buf1 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }
    Serial.printf("[LVGL] draw buffer: %u px @ %p\n", (unsigned)buf_px, lv_buf1);

    lv_display_t *disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_display_set_flush_cb(disp, disp_flush_cb);
    if (!lv_buf1) {
        // Both allocations failed: passing NULL to lv_display_set_buffers trips
        // an LV_ASSERT whose default handler is while(1) - a silent black brick.
        // Nothing left to render into; skip buffer setup and leave the panel on
        // the boot splash rather than hang. (Realistically unreachable: 1/4
        // screen is ~65 KB of internal RAM.)
        Serial.println("[LVGL] FATAL: no draw buffer available, UI disabled");
        return disp;
    }
    // Single-flush (flicker-free) transitions rely on TWO things together: this
    // full-frame buffer AND the page-switch animation invalidating the whole
    // screen each frame. A transition that dirties only disjoint regions would
    // fall back to multi-chunk banded flushes.
    lv_display_set_buffers(disp, lv_buf1, NULL, buf_bytes,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touchpad_read_cb);

    return disp;
}

void Display::setTouchCallback(void (*cb)())
{
    touchCallback = cb;
}
