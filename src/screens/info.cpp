#include "info.h"
#include "config.h"
#include "colors.h"
#include "gamedata.h"
#include "sound.h"

lv_obj_t *logviewer_screen = nullptr;
lv_obj_t *log_label = nullptr;
lv_obj_t *cargo_bar = nullptr;
lv_obj_t *header_label = nullptr;
lv_obj_t *fuel_bar = nullptr;
lv_obj_t *hull_bar = nullptr;
lv_obj_t *wifi_icon = nullptr;
lv_obj_t *websocket_icon = nullptr;
lv_obj_t *bluetooth_icon = nullptr;
lv_obj_t *medpack_label = nullptr;
lv_obj_t *energycell_label = nullptr;
lv_obj_t *bioscan_label = nullptr;
lv_obj_t *bioscan_data_label = nullptr;
lv_obj_t *jump_overlay_label = nullptr;
lv_obj_t *status_label = nullptr;

void create_logviewer_ui() {
  logviewer_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(logviewer_screen, LV_COLOR_BG, 0);
  
  // Header with jumps, fuel, hull (reduced height to 22px to fit bars)
  lv_obj_t* header = lv_obj_create(logviewer_screen);
  lv_obj_set_size(header, SCREEN_WIDTH, 25);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, LV_COLOR_GAUGE_BG, 0);
  lv_obj_set_style_border_width(header, 0, 0);
  lv_obj_set_style_radius(header, 0, 0);
  lv_obj_set_style_pad_all(header, 2, 0);
  lv_obj_set_scrollbar_mode(header, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(header, LV_DIR_NONE);
  
  // Jumps label
  header_label = lv_label_create(header);
  lv_label_set_text(header_label, "Jumps: 0");
  lv_obj_set_style_text_color(header_label, LV_COLOR_FG, 0);
  lv_obj_set_pos(header_label, 5, 3);
  
  // Fuel label
  lv_obj_t* fuel_label = lv_label_create(header);
  lv_label_set_text(fuel_label, "F");
  lv_obj_set_style_text_color(fuel_label, LV_COLOR_FG, 0);
  lv_obj_set_pos(fuel_label, 100, 3);
  
  // Fuel bar (reduced size)
  fuel_bar = lv_bar_create(header);
  lv_obj_set_size(fuel_bar, 55, 15);
  lv_obj_set_pos(fuel_bar, 115, 3);
  lv_bar_set_range(fuel_bar, 0, 100);
  lv_bar_set_value(fuel_bar, 100, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(fuel_bar, LV_COLOR_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_color(fuel_bar, LV_COLOR_GAUGE_FG, LV_PART_INDICATOR);
  
  // Hull label
  lv_obj_t* hull_label = lv_label_create(header);
  lv_label_set_text(hull_label, "H");
  lv_obj_set_style_text_color(hull_label, LV_COLOR_FG, 0);
  lv_obj_set_pos(hull_label, 180, 3);
  
  // Hull bar (reduced size)
  hull_bar = lv_bar_create(header);
  lv_obj_set_size(hull_bar, 55, 15);
  lv_obj_set_pos(hull_bar, 195, 3);
  lv_bar_set_range(hull_bar, 0, 100);
  lv_bar_set_value(hull_bar, 100, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(hull_bar, LV_COLOR_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_color(hull_bar, LV_COLOR_GAUGE_FG, LV_PART_INDICATOR);
  
  // Status icons (right side of header)
  // WiFi icon (rightmost)
  wifi_icon = lv_label_create(header);
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x000000), 0);
  lv_obj_set_pos(wifi_icon, 300, 2);
  
  // WebSocket icon (middle)
  websocket_icon = lv_label_create(header);
  lv_label_set_text(websocket_icon, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_color(websocket_icon, lv_color_hex(0x000000), 0);
  lv_obj_set_pos(websocket_icon, 280, 2);
  
  // Bluetooth icon (left of websocket)
  bluetooth_icon = lv_label_create(header);
  lv_label_set_text(bluetooth_icon, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0x000000), 0);
  lv_obj_set_pos(bluetooth_icon, 260, 2);
  
  // Log area (adjusted for smaller header)
  lv_obj_t* log_area = lv_obj_create(logviewer_screen);
  lv_obj_set_size(log_area, SCREEN_WIDTH, SCREEN_HEIGHT - 25 - 40);
  lv_obj_set_pos(log_area, 0, 25);
  lv_obj_set_style_bg_color(log_area, LV_COLOR_BG, 0);
  lv_obj_set_style_border_width(log_area, 0, 0);
  lv_obj_set_style_radius(log_area, 0, 0);
  lv_obj_set_scrollbar_mode(log_area, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(log_area, LV_DIR_NONE);
  lv_obj_set_style_border_color(log_area, LV_COLOR_GAUGE_FG, 0);
  lv_obj_set_style_border_width(log_area, 1, 0);
  lv_obj_set_style_pad_top(log_area, 1, 0);
  lv_obj_set_style_pad_bottom(log_area, 1, 0);
  lv_obj_set_style_pad_left(log_area, 0, 0);
  lv_obj_set_style_pad_right(log_area, 0, 0);
  lv_obj_set_style_border_width(log_area, 0, 0);

  status_label = lv_label_create(log_area);
  lv_obj_set_pos(status_label, 5, 2);
  lv_obj_set_width(status_label, SCREEN_WIDTH - 75);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
  lv_label_set_text(status_label, "Waiting for events..."); // Initial empty status
  lv_obj_set_style_text_color(status_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(status_label, &lv_font_montserrat_10, 0);

  log_label = lv_label_create(log_area);
  lv_obj_set_pos(log_label, 5, 20);
  lv_label_set_text(log_label, " ");
  lv_obj_set_style_text_color(log_label, LV_COLOR_FG, 0);
  lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(log_label, SCREEN_WIDTH - 75);
  
  // Backpack panel (right side of log area)
  lv_obj_t* backpack_panel = lv_obj_create(log_area);
  lv_obj_set_size(backpack_panel, 40, SCREEN_HEIGHT - 62);
  lv_obj_set_pos(backpack_panel, SCREEN_WIDTH - 40, 0);
  lv_obj_set_style_bg_color(backpack_panel, LV_COLOR_BG, 0);
  lv_obj_set_style_border_width(backpack_panel, 0, 0);
  lv_obj_set_scrollbar_mode(backpack_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(backpack_panel, LV_DIR_NONE);
  lv_obj_set_style_radius(backpack_panel, 0, 0);
  lv_obj_set_style_border_side(backpack_panel, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_pad_all(backpack_panel, 0, 0);
  
  lv_obj_t* backpack_title = lv_label_create(backpack_panel);
  lv_label_set_text(backpack_title, "PACK");
  lv_obj_set_style_text_color(backpack_title, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(backpack_title, &lv_font_montserrat_10, 0);
  lv_obj_align(backpack_title, LV_ALIGN_TOP_MID, 0, 0);
  
  lv_obj_t* title_line = lv_obj_create(backpack_panel);
  lv_obj_set_size(title_line, 30, 1);
  lv_obj_set_pos(title_line, 5, 20);
  lv_obj_set_style_bg_color(title_line, LV_COLOR_GAUGE_FG, 0);
  lv_obj_set_style_border_width(title_line, 0, 0);
  lv_obj_set_scrollbar_mode(title_line, LV_SCROLLBAR_MODE_OFF);
  
  lv_obj_t* medpack_m_label = lv_label_create(backpack_panel);
  lv_label_set_text(medpack_m_label, "M");
  lv_obj_set_style_text_color(medpack_m_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(medpack_m_label, &lv_font_montserrat_14, 0);
  lv_obj_align(medpack_m_label, LV_ALIGN_TOP_LEFT, 2, 25);
  
  medpack_label = lv_label_create(backpack_panel);
  lv_label_set_text(medpack_label, "m");
  lv_obj_set_style_text_color(medpack_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(medpack_label, &lv_font_montserrat_14, 0);
  lv_obj_align(medpack_label, LV_ALIGN_TOP_LEFT, 18, 25);
  
  lv_obj_t* divider_line = lv_obj_create(backpack_panel);
  lv_obj_set_size(divider_line, 30, 1);
  lv_obj_set_pos(divider_line, 5, 45);
  lv_obj_set_style_bg_color(divider_line, LV_COLOR_GAUGE_FG, 0);
  lv_obj_set_style_border_width(divider_line, 0, 0);
  lv_obj_set_scrollbar_mode(divider_line, LV_SCROLLBAR_MODE_OFF);
  
  lv_obj_t* energycell_e_label = lv_label_create(backpack_panel);
  lv_label_set_text(energycell_e_label, "E");
  lv_obj_set_style_text_color(energycell_e_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(energycell_e_label, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(energycell_e_label, 2, 50);
    
  energycell_label = lv_label_create(backpack_panel);
  lv_label_set_text(energycell_label, "e");
  lv_obj_set_style_text_color(energycell_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(energycell_label, &lv_font_montserrat_14, 0);
  lv_obj_set_pos(energycell_label, 18, 50);
  
  bioscan_data_label = lv_label_create(backpack_panel);
  lv_label_set_text(bioscan_data_label, "DATA");
  lv_obj_set_style_text_color(bioscan_data_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(bioscan_data_label, &lv_font_montserrat_10, 0);
  lv_obj_align(bioscan_data_label, LV_ALIGN_TOP_MID, 0, 75);
  
  bioscan_label = lv_label_create(backpack_panel);
  lv_label_set_text(bioscan_label, "0");
  lv_obj_set_style_text_color(bioscan_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(bioscan_label, &lv_font_montserrat_14, 0);
  lv_obj_align(bioscan_label, LV_ALIGN_TOP_MID, 0, 95);
  if (bioscanInfo.totalScans == 0) {
    lv_obj_add_flag(bioscan_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bioscan_data_label, LV_OBJ_FLAG_HIDDEN);
  }

  // Cargo bar at bottom
  cargo_bar = lv_bar_create(logviewer_screen);
  lv_obj_set_size(cargo_bar, SCREEN_WIDTH, 40);
  lv_obj_set_pos(cargo_bar, 0, SCREEN_HEIGHT - 40);
  lv_bar_set_range(cargo_bar, 0, 256);
  lv_bar_set_value(cargo_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(cargo_bar, LV_COLOR_GAUGE_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_color(cargo_bar, LV_COLOR_GAUGE_FG, LV_PART_INDICATOR);
  
  lv_obj_t* cargo_label = lv_label_create(cargo_bar);
  lv_label_set_text(cargo_label, "Cargo: 0/256");
  lv_obj_set_style_text_color(cargo_label, LV_COLOR_FG, 0);
  lv_obj_center(cargo_label);
}
