#include "display.h"
#include "config.h"
#include "TFT_eSPI.h"
#include "FT6336U.h"
#include <esp_heap_caps.h>

// Screen dimensions after rotation (swapped for 90° rotation)
static const uint16_t screenWidth  = 240;
static const uint16_t screenHeight = 320;

// Flush path: 0 = blocking pushColors (default), 1 = SPI DMA (pushPixelsDMA).
//
// The DMA path is kept for reference but DISABLED: once the i2s_std driver's
// GDMA channel is active (audio), LVGL stalls mid-frame after ~3 DMA flushes
// (exact spi_master/GDMA interaction unresolved). The blocking path is also
// simply faster here: pushPixelsDMA needs an extra in-place byte-swap pass
// over the PSRAM buffer before every transfer, while pushColors streams and
// swaps in a single pass.
#define DISPLAY_FLUSH_DMA 0

// Two LVGL draw buffers (double buffering) allocated in PSRAM.
// See Display::init() for allocation.
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *lv_buf1 = nullptr;
static lv_color_t *lv_buf2 = nullptr;

static void (*touchCallback)() = nullptr;  // optional callback when a valid touch is read

TFT_eSPI tft = TFT_eSPI(screenWidth, screenHeight); /* TFT instance */
FT6336U ft6336u(I2C_SDA, I2C_SCL, RST_N_PIN, INT_N_PIN); 
FT6336U_TouchPointType tp; 


#if LV_USE_LOG != 0
/* Serial debugging */
void my_print(const char * buf)
{
    Serial.printf(buf);
    Serial.flush();
}
#endif

/* Display flushing (path selected by DISPLAY_FLUSH_DMA, see top of file) */
void my_disp_flush( lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p )
{
    uint32_t w = ( area->x2 - area->x1 + 1 );
    uint32_t h = ( area->y2 - area->y1 + 1 );

    tft.startWrite();
    tft.setAddrWindow( area->x1, area->y1, w, h );
#if DISPLAY_FLUSH_DMA
    tft.pushPixelsDMA( (uint16_t*)&color_p->full, w * h );
#else
    tft.pushColors( (uint16_t*)&color_p->full, w * h, true );
#endif
    tft.endWrite();

    lv_disp_flush_ready( disp );
}

/*Read the touchpad*/
void my_touchpad_read( lv_indev_drv_t * indev_driver, lv_indev_data_t * data )
{
    uint16_t touchX, touchY;

    //bool touched = tft.getTouch( &touchX, &touchY, 600 );
    tp = ft6336u.scan(); 
    int touched = tp.touch_count;

    if( !touched )
    {
        data->state = LV_INDEV_STATE_REL;
    }
    else
    {
        int x = tp.tp[0].x;
        int y = tp.tp[0].y;
        if(x >= 0 && x < screenWidth && y >= 0 && y < screenHeight)
        {
            data->state = LV_INDEV_STATE_PR;
            data->point.x = tp.tp[0].x;
            data->point.y = tp.tp[0].y;
            if (touchCallback) {
                touchCallback();
            }
        }
    }
}

lv_disp_t* Display::init(void)
{
    lv_disp_t* _disp;
    ft6336u.begin(); 
#if LV_USE_LOG != 0
    lv_log_register_print_cb( my_print ); /* register print function for debugging */
#endif
    lv_init();
    tft.begin();          /* TFT init */
    tft.setRotation( TFT_DIRECTION ); /* No rotation at TFT level - LVGL handles rotation */
#if DISPLAY_FLUSH_DMA
    bool dma_ok = tft.initDMA();   /* attach the SPI DMA engine to the bus */
    Serial.printf("[TFT] initDMA: %s\n", dma_ok ? "OK" : "FAILED (or already enabled)");
    tft.setSwapBytes(true); /* RGB565 byte order for pushPixelsDMA (LV_COLOR_16_SWAP == 0) */
#endif

    /* Allocate two draw buffers in PSRAM as the DMA source. 64-byte (cache line)
       alignment keeps the SPI master's cache write-back coherent when it streams
       the buffer out of PSRAM. Falls back to internal DMA RAM if PSRAM is absent. */
    const size_t buf_bytes = LVGL_BUFFER_PIXELS * sizeof(lv_color_t);
    lv_buf1 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM);
    lv_buf2 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_SPIRAM);
    if (!lv_buf1 || !lv_buf2) {
        Serial.println("[LVGL] PSRAM draw buffers failed, using internal DMA RAM");
        if (lv_buf1) { heap_caps_free(lv_buf1); lv_buf1 = nullptr; }
        if (lv_buf2) { heap_caps_free(lv_buf2); lv_buf2 = nullptr; }
        lv_buf1 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
        lv_buf2 = (lv_color_t*)heap_caps_aligned_alloc(64, buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    }
    Serial.printf("[LVGL] draw buffers: %u px x2 @ %p / %p\n",
                  (unsigned)LVGL_BUFFER_PIXELS, lv_buf1, lv_buf2);

    lv_disp_draw_buf_init( &draw_buf, lv_buf1, lv_buf2, LVGL_BUFFER_PIXELS );

    /*Initialize the display*/
    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init( &disp_drv );
    /*Change the following line to your display resolution*/
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
    _disp=lv_disp_drv_register( &disp_drv );
    lv_disp_set_rotation(_disp, LV_DISP_ROT_90);  // 270° = 90° counterclockwise

    /*Initialize the (dummy) input device driver*/
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init( &indev_drv );
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register( &indev_drv );
    return _disp;
}

void Display::routine(void)
{
    lv_task_handler();
}

void Display::setTouchCallback(void (*cb)())
{
    touchCallback = cb;
}
