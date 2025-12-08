// Configuration constants moved out of main.cpp
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

// Display geometry
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define LVGL_BUFFER_PIXELS (SCREEN_WIDTH * SCREEN_HEIGHT / 10)
#define LVGL_BUFFER_SIZE (LVGL_BUFFER_PIXELS * 4)
