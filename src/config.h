// Configuration constants: network + Guition JC4827W543 board pinout
#pragma once

#include <Arduino.h>

// WiFi and network defaults (override in code or via build flags if needed)
#define WIFI_SSID "0619562e-bcbf-4bfc-97a8"
#define WIFI_PASSWORD "3869212721440634"
#define HOSTNAME "FighterController"
#define UDP_PORT 12345

// Icarus terminal defaults
static const char DEFAULT_SERVER_IP[] = "192.168.178.85";
static const int DEFAULT_WEBSOCKET_PORT = 3300;

// ---------------- Guition JC4827W543 (ESP32-S3 N4R8) ----------------
// Full pin reference: GPIOs.md

// NV3041A display over QSPI (Arduino_GFX Arduino_ESP32QSPI bus)
#define LCD_QSPI_CS   45
#define LCD_QSPI_CLK  47
#define LCD_QSPI_D0   21
#define LCD_QSPI_D1   48
#define LCD_QSPI_D2   40
#define LCD_QSPI_D3   39
#define LCD_BL_PIN     1   // backlight LEDC PWM. NOT an amp-enable pin!

// Arduino_GFX rotation index (0..3). 2 = 180deg, matching the mounting used in
// the ESPHome test (References/jc4827w543.yaml). Touch coords flip with it.
#define DISPLAY_ROTATION 2

// GT911 capacitive touch (its own I2C bus -> Wire)
#define TOUCH_SDA  8
#define TOUCH_SCL  4
#define TOUCH_INT  3
#define TOUCH_RST 38

// PCF8575 expander with the three TTP223 pads (external I2C bus -> Wire1)
#define PCF_SDA 18
#define PCF_SCL 17
#define PCF8575_ADDR 0x22
// TTP223 pads sit right of the display, active-high on these PCF bits:
#define TTP_TOP_BIT    5  // "touch3" (top)    -> previous page
#define TTP_MID_BIT    6  // "touch2" (middle) -> display off
#define TTP_BOTTOM_BIT 7  // "touch1" (bottom) -> next page

// NS4168 mono I2S amplifier (no MCLK, no codec setup needed)
#define I2S_BCK  42
#define I2S_WS    2   // LRCLK
#define I2S_DOUT 41

// Display geometry
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 272
// LVGL partial render buffer: 1/4 screen, double-buffered in PSRAM
#define LVGL_BUFFER_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT / 4)
