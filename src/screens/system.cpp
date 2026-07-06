#include "system.h"
#include "config.h"
#include "sound.h"
#include "gamedata.h"
#include "colors.h"
#include "theme.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

// externs from main
extern WiFiMulti wifiMulti;
extern WebsocketsClient wsClient;
extern bool reqRestartWifi;
extern bool reqRestartWebSocket;
extern uint8_t BL_DUTY_FULL;
extern bool displayOn;
extern bool displayDimmed;
void connectWebSocket();
void updateSystemInfo();

lv_obj_t *settings_screen = nullptr;
lv_obj_t *sys_info_label = nullptr;
lv_obj_t *amplitude_slider = nullptr;
lv_obj_t *amplitude_value_label = nullptr;

static lv_obj_t *brightness_value_label = nullptr;

#define RIGHT_X 310
#define RIGHT_W (SCREEN_WIDTH - RIGHT_X - 10)   // 160

static void restart_wifi_handler(lv_event_t *e) {
  beepClick();
  reqRestartWifi = true;  // deferred to loop() so the UI doesn't block
}

static void restart_websocket_handler(lv_event_t *e) {
  beepClick();
  reqRestartWebSocket = true;  // deferred to loop() so the UI doesn't block
}

static void reboot_handler(lv_event_t *e) {
  Serial.println("[SETTINGS] Rebooting system...");
  beepDisconnect();
  delay(500);
  ESP.restart();
}

static void updateAmplitudeDisplay(int amplitude) {
  if (!amplitude_value_label) return;
  char buf[48];
  snprintf(buf, sizeof(buf), "Volume: %d", amplitude);
  lv_label_set_text(amplitude_value_label, buf);
}

static void amplitude_slider_handler(lv_event_t *e) {
  lv_obj_t *slider = (lv_obj_t*)lv_event_get_target(e);
  if (!slider) return;
  int raw = lv_slider_get_value(slider);
  const int step = 500;
  int rounded = ((raw + step / 2) / step) * step;
  if (rounded < 500) rounded = 500;
  if (rounded > 12000) rounded = 12000;
  lv_slider_set_value(slider, rounded, LV_ANIM_OFF);
  AUDIO_TONE_AMPL = rounded;
  updateAmplitudeDisplay(rounded);
  beepClick();
}

static void updateBrightnessDisplay(int duty) {
  if (!brightness_value_label) return;
  char buf[48];
  snprintf(buf, sizeof(buf), "Brightness: %d%%", duty * 100 / 255);
  lv_label_set_text(brightness_value_label, buf);
}

static void brightness_slider_handler(lv_event_t *e) {
  lv_obj_t *slider = (lv_obj_t*)lv_event_get_target(e);
  if (!slider) return;
  int v = lv_slider_get_value(slider);
  BL_DUTY_FULL = (uint8_t)v;
  if (displayOn && !displayDimmed) ledcWrite(LCD_BL_PIN, BL_DUTY_FULL);
  updateBrightnessDisplay(v);
}

static lv_obj_t* make_button(lv_obj_t *parent, const char *text, int y,
                             lv_event_cb_t cb) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_set_size(btn, RIGHT_W, 34);
  lv_obj_set_pos(btn, RIGHT_X, y);
  lv_obj_add_style(btn, &style_btn, 0);
  lv_obj_add_style(btn, &style_btn_pressed, LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, FONT_BODY, 0);
  lv_obj_center(label);
  return btn;
}

void create_settings_ui() {
  settings_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(settings_screen, LV_COLOR_BG, 0);

  lv_obj_t *title = lv_label_create(settings_screen);
  lv_label_set_text(title, "SYSTEM");
  lv_obj_set_style_text_color(title, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(title, FONT_BIG, 0);
  lv_obj_set_pos(title, 10, 8);

  // Left: diagnostics panel (filled by updateSystemInfo in main.cpp)
  lv_obj_t *info_area = lv_obj_create(settings_screen);
  lv_obj_set_size(info_area, 290, SCREEN_HEIGHT - 50);
  lv_obj_set_pos(info_area, 10, 40);
  lv_obj_add_style(info_area, &style_panel, 0);
  lv_obj_set_scrollbar_mode(info_area, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(info_area, LV_DIR_NONE);

  sys_info_label = lv_label_create(info_area);
  lv_label_set_text(sys_info_label, "Loading...");
  lv_obj_set_style_text_color(sys_info_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(sys_info_label, FONT_SMALL, 0);
  lv_label_set_long_mode(sys_info_label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_width(sys_info_label, 270);
  lv_obj_set_pos(sys_info_label, 2, 2);

  // Right column: brightness, volume, actions
  brightness_value_label = lv_label_create(settings_screen);
  lv_obj_set_style_text_color(brightness_value_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(brightness_value_label, FONT_SMALL, 0);
  lv_obj_set_pos(brightness_value_label, RIGHT_X, 40);

  lv_obj_t *brightness_slider = lv_slider_create(settings_screen);
  lv_obj_set_size(brightness_slider, RIGHT_W, 12);
  lv_obj_set_pos(brightness_slider, RIGHT_X, 60);
  lv_slider_set_range(brightness_slider, 20, 255);
  lv_slider_set_value(brightness_slider, BL_DUTY_FULL, LV_ANIM_OFF);
  lv_obj_add_event_cb(brightness_slider, brightness_slider_handler,
                      LV_EVENT_VALUE_CHANGED, NULL);
  updateBrightnessDisplay(BL_DUTY_FULL);

  amplitude_value_label = lv_label_create(settings_screen);
  lv_obj_set_style_text_color(amplitude_value_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(amplitude_value_label, FONT_SMALL, 0);
  lv_obj_set_pos(amplitude_value_label, RIGHT_X, 85);

  amplitude_slider = lv_slider_create(settings_screen);
  lv_obj_set_size(amplitude_slider, RIGHT_W, 12);
  lv_obj_set_pos(amplitude_slider, RIGHT_X, 105);
  lv_slider_set_range(amplitude_slider, 500, 12000);
  lv_slider_set_value(amplitude_slider, AUDIO_TONE_AMPL, LV_ANIM_OFF);
  lv_obj_add_event_cb(amplitude_slider, amplitude_slider_handler,
                      LV_EVENT_VALUE_CHANGED, NULL);
  updateAmplitudeDisplay(AUDIO_TONE_AMPL);

  make_button(settings_screen, "Restart WiFi", 130, restart_wifi_handler);
  make_button(settings_screen, "Restart WS", 172, restart_websocket_handler);
  lv_obj_t *reboot = make_button(settings_screen, "REBOOT", 222, reboot_handler);
  lv_obj_set_style_border_color(reboot, LV_COLOR_WARNING_FG, 0);

  // No updateSystemInfo() here: our only caller (switchToPage) holds lvglMutex
  // and refreshes the panel itself right after releasing it.
}
