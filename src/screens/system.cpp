#include "system.h"
#include "config.h"
#include "sound.h"
#include "gamedata.h"
#include "colors.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoWebsockets.h>

using namespace websockets;

// externs from main
extern WiFiMulti wifiMulti;
extern WebsocketsClient wsClient;
void connectWebSocket();
void updateSystemInfo();

lv_obj_t *settings_screen = nullptr;
lv_obj_t *sys_info_label = nullptr;
lv_obj_t *amplitude_slider = nullptr;
lv_obj_t *amplitude_value_label = nullptr;

static void restart_wifi_handler(lv_event_t * e) {
  Serial.println("[SETTINGS] Restarting WiFi...");
  beepClick();
  WiFi.disconnect();
  delay(500);
  wifiMulti.run();
}

static void restart_websocket_handler(lv_event_t * e) {
  Serial.println("[SETTINGS] Restarting WebSocket...");
  beepClick();
  if (wsClient.available()) {
    wsClient.close();
  }
  delay(500);
  connectWebSocket();
}

static void reboot_handler(lv_event_t * e) {
  Serial.println("[SETTINGS] Rebooting system...");
  beepDisconnect();
  delay(500);
  ESP.restart();
}

static void updateAmplitudeDisplay(int amplitude) {
  if (!amplitude_value_label) return;
  char buf[48];
  snprintf(buf, sizeof(buf), "Tone amplitude: %d", amplitude);
  lv_label_set_text(amplitude_value_label, buf);
}

static void amplitude_slider_handler(lv_event_t * e) {
  lv_obj_t* slider = lv_event_get_target(e);
  if (!slider) return;
  int raw = lv_slider_get_value(slider);
  const int step = 500;
  int rounded = ((raw + step / 2) / step) * step;
  if (rounded < 500) rounded = 500;
  if (rounded > 12000) rounded = 12000;
  lv_slider_set_value(slider, rounded, LV_ANIM_OFF);
  AUDIO_TONE_AMPL = rounded;
  updateAmplitudeDisplay(rounded);
  Serial.printf("[AUDIO] Tone amplitude set to %d\n", rounded);
  beepClick();
}

void create_settings_ui() {
  settings_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(settings_screen, LV_COLOR_BG, 0);
  
  lv_obj_t* title = lv_label_create(settings_screen);
  lv_label_set_text(title, "SYSTEM SETTINGS");
  lv_obj_set_style_text_color(title, LV_COLOR_FG, 0);
  lv_obj_set_pos(title, 10, 10);
  
  lv_obj_t* info_area = lv_obj_create(settings_screen);
  lv_obj_set_size(info_area, SCREEN_WIDTH - 20, 90);
  lv_obj_set_pos(info_area, 10, 35);
  lv_obj_set_style_bg_color(info_area, LV_COLOR_GAUGE_BG, 0);
  lv_obj_set_style_border_width(info_area, 1, 0);
  lv_obj_set_style_border_color(info_area, LV_COLOR_GAUGE_FG, 0);
  lv_obj_set_style_radius(info_area, 5, 0);
  lv_obj_set_scrollbar_mode(info_area, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(info_area, LV_DIR_NONE);
  
  sys_info_label = lv_label_create(info_area);
  lv_label_set_text(sys_info_label, "Loading...");
  lv_obj_set_style_text_color(sys_info_label, LV_COLOR_FG, 0);
  lv_label_set_long_mode(sys_info_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(sys_info_label, SCREEN_WIDTH - 40);
  lv_obj_set_pos(sys_info_label, 5, 5);
  
  static lv_style_t btn_style;
  lv_style_init(&btn_style);
  lv_style_set_bg_color(&btn_style, LV_COLOR_GAUGE_BG);
  lv_style_set_bg_opa(&btn_style, LV_OPA_COVER);
  lv_style_set_border_color(&btn_style, LV_COLOR_GAUGE_FG);
  lv_style_set_border_width(&btn_style, 1);
  lv_style_set_radius(&btn_style, 5);
  lv_style_set_text_color(&btn_style, LV_COLOR_FG);
  
  static lv_style_t btn_pressed_style;
  lv_style_init(&btn_pressed_style);
  lv_style_set_bg_color(&btn_pressed_style, LV_COLOR_HIGHLIGHT_BG);
  lv_style_set_text_color(&btn_pressed_style, LV_COLOR_HIGHLIGHT_FG);
  
  amplitude_value_label = lv_label_create(settings_screen);
  lv_label_set_text(amplitude_value_label, "Tone amplitude: 2000");
  lv_obj_set_style_text_color(amplitude_value_label, LV_COLOR_FG, 0);
  lv_obj_set_pos(amplitude_value_label, 10, 130);

  amplitude_slider = lv_slider_create(settings_screen);
  lv_obj_set_size(amplitude_slider, SCREEN_WIDTH - 20, 12);
  lv_obj_set_pos(amplitude_slider, 10, 150);
  lv_slider_set_range(amplitude_slider, 500, 12000);
  lv_slider_set_value(amplitude_slider, AUDIO_TONE_AMPL, LV_ANIM_OFF);
  lv_obj_add_event_cb(amplitude_slider, amplitude_slider_handler, LV_EVENT_VALUE_CHANGED, NULL);
  updateAmplitudeDisplay(AUDIO_TONE_AMPL);

  lv_obj_t* btn_wifi = lv_btn_create(settings_screen);
  lv_obj_set_size(btn_wifi, 145, 35);
  lv_obj_set_pos(btn_wifi, 10, 175);
  lv_obj_add_style(btn_wifi, &btn_style, 0);
  lv_obj_add_style(btn_wifi, &btn_pressed_style, LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_wifi, restart_wifi_handler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* label_wifi = lv_label_create(btn_wifi);
  lv_label_set_text(label_wifi, "Restart WiFi");
  lv_obj_center(label_wifi);
  
  lv_obj_t* btn_ws = lv_btn_create(settings_screen);
  lv_obj_set_size(btn_ws, 145, 35);
  lv_obj_set_pos(btn_ws, 165, 175);
  lv_obj_add_style(btn_ws, &btn_style, 0);
  lv_obj_add_style(btn_ws, &btn_pressed_style, LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_ws, restart_websocket_handler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* label_ws = lv_label_create(btn_ws);
  lv_label_set_text(label_ws, "Restart WS");
  lv_obj_center(label_ws);
  
  lv_obj_t* btn_reboot = lv_btn_create(settings_screen);
  lv_obj_set_size(btn_reboot, 300, 35);
  lv_obj_set_pos(btn_reboot, 10, 215);
  lv_obj_add_style(btn_reboot, &btn_style, 0);
  lv_obj_add_style(btn_reboot, &btn_pressed_style, LV_STATE_PRESSED);
  lv_obj_add_event_cb(btn_reboot, reboot_handler, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t* label_reboot = lv_label_create(btn_reboot);
  lv_label_set_text(label_reboot, "REBOOT SYSTEM");
  lv_obj_center(label_reboot);
  
  updateSystemInfo();
}
