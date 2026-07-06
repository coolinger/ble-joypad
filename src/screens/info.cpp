#include "info.h"
#include "config.h"
#include "colors.h"
#include "theme.h"
#include "gamedata.h"

lv_obj_t *logviewer_screen = nullptr;
lv_obj_t *log_label = nullptr;
lv_obj_t *cargo_bar = nullptr;
lv_obj_t *header_label = nullptr;
lv_obj_t *fuel_bar = nullptr;
lv_obj_t *hull_bar = nullptr;
lv_obj_t *wifi_icon = nullptr;
lv_obj_t *websocket_icon = nullptr;
lv_obj_t *bluetooth_icon = nullptr;
lv_obj_t *backpack_panel = nullptr;
lv_obj_t *medpack_label = nullptr;
lv_obj_t *energycell_label = nullptr;
lv_obj_t *bioscan_label = nullptr;
lv_obj_t *bioscan_data_label = nullptr;
lv_obj_t *jump_overlay_label = nullptr;
lv_obj_t *status_label = nullptr;
lv_obj_t *pin_cards[MAX_PINNED_BODIES] = {nullptr};
lv_obj_t *pin_title_labels[MAX_PINNED_BODIES] = {nullptr};
lv_obj_t *pin_genus_labels[MAX_PINNED_BODIES] = {nullptr};

// Dashboard geometry
#define HEADER_H  32
#define SIDEBAR_W 180
#define LOG_W     (SCREEN_WIDTH - SIDEBAR_W)   // 300
#define CARGO_H   24

// Plain container without scrolling/border noise
static lv_obj_t* make_box(lv_obj_t *parent) {
  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_radius(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_set_scrollbar_mode(box, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(box, LV_DIR_NONE);
  return box;
}

static lv_obj_t* make_gauge(lv_obj_t *parent, const char *tag, int x, int w) {
  lv_obj_t *lbl = lv_label_create(parent);
  lv_label_set_text(lbl, tag);
  lv_obj_set_style_text_color(lbl, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(lbl, FONT_BODY, 0);
  lv_obj_set_pos(lbl, x, 7);

  lv_obj_t *bar = lv_bar_create(parent);
  lv_obj_set_size(bar, w, 14);
  lv_obj_set_pos(bar, x + 16, 8);
  lv_bar_set_range(bar, 0, 100);
  lv_bar_set_value(bar, 100, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(bar, LV_COLOR_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, LV_COLOR_GAUGE_FG, LV_PART_INDICATOR);
  return bar;
}

static lv_obj_t* make_icon(lv_obj_t *parent, const char *sym, int x) {
  lv_obj_t *icon = lv_label_create(parent);
  lv_label_set_text(icon, sym);
  lv_obj_set_style_text_color(icon, lv_color_hex(0x000000), 0);  // off = invisible-ish
  lv_obj_set_style_text_font(icon, FONT_HEAD, 0);
  lv_obj_set_pos(icon, x, 6);
  return icon;
}

void create_logviewer_ui() {
  logviewer_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(logviewer_screen, LV_COLOR_BG, 0);

  // ---------------- Header (full width) ----------------
  lv_obj_t *header = make_box(logviewer_screen);
  lv_obj_set_size(header, SCREEN_WIDTH, HEADER_H);
  lv_obj_set_pos(header, 0, 0);
  lv_obj_set_style_bg_color(header, LV_COLOR_GAUGE_BG, 0);

  header_label = lv_label_create(header);
  lv_label_set_text(header_label, "Jumps: 0");
  lv_obj_set_style_text_color(header_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(header_label, FONT_HEAD, 0);
  lv_obj_set_pos(header_label, 8, 6);

  fuel_bar = make_gauge(header, "F", 130, 85);
  hull_bar = make_gauge(header, "H", 250, 85);

  bluetooth_icon = make_icon(header, LV_SYMBOL_BLUETOOTH, SCREEN_WIDTH - 90);
  websocket_icon = make_icon(header, LV_SYMBOL_REFRESH,   SCREEN_WIDTH - 62);
  wifi_icon      = make_icon(header, LV_SYMBOL_WIFI,      SCREEN_WIDTH - 34);

  // ---------------- Left column: event log ----------------
  lv_obj_t *log_area = make_box(logviewer_screen);
  lv_obj_set_size(log_area, LOG_W, SCREEN_HEIGHT - HEADER_H - CARGO_H);
  lv_obj_set_pos(log_area, 0, HEADER_H);
  lv_obj_set_style_bg_color(log_area, LV_COLOR_BG, 0);
  lv_obj_set_style_pad_all(log_area, 4, 0);

  log_label = lv_label_create(log_area);
  lv_obj_set_pos(log_label, 2, 0);
  lv_obj_set_width(log_label, LOG_W - 12);
  lv_label_set_text(log_label, " ");
  lv_label_set_long_mode(log_label, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_style_text_color(log_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(log_label, FONT_BODY, 0);

  // ---------------- Cargo bar (bottom of the left column) ----------------
  cargo_bar = lv_bar_create(logviewer_screen);
  lv_obj_set_size(cargo_bar, LOG_W, CARGO_H);
  lv_obj_set_pos(cargo_bar, 0, SCREEN_HEIGHT - CARGO_H);
  lv_bar_set_range(cargo_bar, 0, 256);
  lv_bar_set_value(cargo_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_bg_color(cargo_bar, LV_COLOR_GAUGE_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_color(cargo_bar, LV_COLOR_GAUGE_FG, LV_PART_INDICATOR);

  lv_obj_t *cargo_label = lv_label_create(cargo_bar);
  lv_label_set_text(cargo_label, "Cargo: 0/256");
  lv_obj_set_style_text_color(cargo_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(cargo_label, FONT_SMALL, 0);
  lv_obj_center(cargo_label);

  // ---------------- Right sidebar ----------------
  lv_obj_t *sidebar = make_box(logviewer_screen);
  lv_obj_set_size(sidebar, SIDEBAR_W, SCREEN_HEIGHT - HEADER_H);
  lv_obj_set_pos(sidebar, LOG_W, HEADER_H);
  lv_obj_set_style_bg_color(sidebar, LV_COLOR_BG, 0);
  lv_obj_set_style_border_side(sidebar, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_color(sidebar, LV_COLOR_GAUGE_FG, 0);
  lv_obj_set_style_border_width(sidebar, 1, 0);
  lv_obj_set_style_pad_all(sidebar, 5, 0);
  lv_obj_set_style_pad_row(sidebar, 4, 0);
  lv_obj_set_flex_flow(sidebar, LV_FLEX_FLOW_COLUMN);

  // Current system name (updateStatusLine writes into this)
  status_label = lv_label_create(sidebar);
  lv_obj_set_width(status_label, SIDEBAR_W - 12);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_WRAP);
  lv_label_set_text(status_label, "Waiting for events...");
  lv_obj_set_style_text_color(status_label, LV_COLOR_HIGHLIGHT_BG, 0);
  lv_obj_set_style_text_font(status_label, FONT_HEAD, 0);

  // Pinned body signal cards (hidden until pins exist)
  for (int i = 0; i < MAX_PINNED_BODIES; i++) {
    pin_cards[i] = lv_obj_create(sidebar);
    lv_obj_set_width(pin_cards[i], SIDEBAR_W - 12);
    lv_obj_set_height(pin_cards[i], LV_SIZE_CONTENT);
    lv_obj_add_style(pin_cards[i], &style_card, 0);
    lv_obj_set_scrollbar_mode(pin_cards[i], LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(pin_cards[i], LV_DIR_NONE);
    lv_obj_set_flex_flow(pin_cards[i], LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(pin_cards[i], LV_OBJ_FLAG_HIDDEN);

    pin_title_labels[i] = lv_label_create(pin_cards[i]);
    lv_obj_set_width(pin_title_labels[i], SIDEBAR_W - 22);
    lv_label_set_long_mode(pin_title_labels[i], LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(pin_title_labels[i], "");
    lv_obj_set_style_text_font(pin_title_labels[i], FONT_BODY, 0);
    lv_obj_set_style_text_color(pin_title_labels[i], lv_color_hex(0xffb000), 0);

    pin_genus_labels[i] = lv_label_create(pin_cards[i]);
    lv_obj_set_width(pin_genus_labels[i], SIDEBAR_W - 22);
    lv_label_set_long_mode(pin_genus_labels[i], LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_recolor(pin_genus_labels[i], true);
    lv_label_set_text(pin_genus_labels[i], "");
    lv_obj_set_style_text_font(pin_genus_labels[i], FONT_SMALL, 0);
    lv_obj_add_flag(pin_genus_labels[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Backpack (on-foot only; updateBackpackDisplay toggles visibility)
  backpack_panel = lv_obj_create(sidebar);
  lv_obj_set_width(backpack_panel, SIDEBAR_W - 12);
  lv_obj_set_height(backpack_panel, LV_SIZE_CONTENT);
  lv_obj_add_style(backpack_panel, &style_card, 0);
  lv_obj_set_scrollbar_mode(backpack_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(backpack_panel, LV_DIR_NONE);
  lv_obj_add_flag(backpack_panel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *backpack_title = lv_label_create(backpack_panel);
  lv_label_set_text(backpack_title, "BACKPACK");
  lv_obj_set_style_text_color(backpack_title, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(backpack_title, FONT_SMALL, 0);
  lv_obj_set_pos(backpack_title, 0, 0);

  lv_obj_t *medpack_m_label = lv_label_create(backpack_panel);
  lv_label_set_text(medpack_m_label, "Med");
  lv_obj_set_style_text_color(medpack_m_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(medpack_m_label, FONT_SMALL, 0);
  lv_obj_set_pos(medpack_m_label, 0, 18);

  medpack_label = lv_label_create(backpack_panel);
  lv_label_set_text(medpack_label, "0");
  lv_obj_set_style_text_color(medpack_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(medpack_label, FONT_BODY, 0);
  lv_obj_set_pos(medpack_label, 36, 17);

  lv_obj_t *energycell_e_label = lv_label_create(backpack_panel);
  lv_label_set_text(energycell_e_label, "Cell");
  lv_obj_set_style_text_color(energycell_e_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(energycell_e_label, FONT_SMALL, 0);
  lv_obj_set_pos(energycell_e_label, 80, 18);

  energycell_label = lv_label_create(backpack_panel);
  lv_label_set_text(energycell_label, "0");
  lv_obj_set_style_text_color(energycell_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(energycell_label, FONT_BODY, 0);
  lv_obj_set_pos(energycell_label, 112, 17);

  bioscan_data_label = lv_label_create(backpack_panel);
  lv_label_set_text(bioscan_data_label, "Bio data");
  lv_obj_set_style_text_color(bioscan_data_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(bioscan_data_label, FONT_SMALL, 0);
  lv_obj_set_pos(bioscan_data_label, 0, 38);

  bioscan_label = lv_label_create(backpack_panel);
  lv_label_set_text(bioscan_label, "0");
  lv_obj_set_style_text_color(bioscan_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(bioscan_label, FONT_BODY, 0);
  lv_obj_set_pos(bioscan_label, 60, 37);
  if (bioscanInfo.totalScans == 0) {
    lv_obj_add_flag(bioscan_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(bioscan_data_label, LV_OBJ_FLAG_HIDDEN);
  }
}
