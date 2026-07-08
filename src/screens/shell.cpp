#include "shell.h"
#include "../config.h"
#include "../colors.h"
#include "../theme.h"

lv_obj_t *shell_jumps_label = nullptr;
lv_obj_t *shell_fuel_arc = nullptr;
lv_obj_t *shell_fuel_label = nullptr;
lv_obj_t *shell_hull_arc = nullptr;
lv_obj_t *shell_hull_label = nullptr;
lv_obj_t *shell_cargo_arc = nullptr;
lv_obj_t *shell_drones_arc = nullptr;
lv_obj_t *shell_cargo_label = nullptr;
lv_obj_t *shell_mode_label = nullptr;
lv_obj_t *wifi_icon = nullptr;
lv_obj_t *websocket_icon = nullptr;
lv_obj_t *bluetooth_icon = nullptr;
lv_obj_t *status_label = nullptr;

static lv_obj_t *tab_buttons[3] = {nullptr, nullptr, nullptr};

// Page switches must not run inside an LVGL event callback (the callback runs
// under lvglMutex; switchToPage takes it again -> timeout no-op). Defer to
// loop() via this flag, exactly like reqRestartWifi.
extern volatile int reqPageSwitch;

static void tab_event_cb(lv_event_t *e) {
  int page = (int)(intptr_t)lv_event_get_user_data(e);
  reqPageSwitch = page;
}

static lv_obj_t* make_zone(lv_obj_t *parent) {
  lv_obj_t *z = lv_obj_create(parent);
  lv_obj_set_style_bg_color(z, LV_COLOR_BG, 0);
  lv_obj_set_style_bg_opa(z, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(z, 0, 0);
  lv_obj_set_style_radius(z, 0, 0);
  lv_obj_set_style_pad_all(z, 0, 0);
  lv_obj_set_scrollbar_mode(z, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(z, LV_DIR_NONE);
  lv_obj_remove_flag(z, LV_OBJ_FLAG_CLICKABLE);  // click-through for touches
  return z;
}

static lv_obj_t* dim_label(lv_obj_t *parent, const char *txt, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, LV_COLOR_DIM, 0);
  lv_obj_set_style_text_font(l, FONT_DISPLAY_LABEL, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

static lv_obj_t* value_label(lv_obj_t *parent, const char *txt, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, LV_COLOR_VALUE, 0);
  lv_obj_set_style_text_font(l, FONT_SMALL, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

static lv_obj_t* make_arc(lv_obj_t *parent, int x) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, 34, 34);
  lv_obj_set_pos(a, x, 4);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 100);
  lv_arc_set_value(a, 100);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);           // read-only look
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, LV_COLOR_GAUGE_BG, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, LV_COLOR_FG, LV_PART_INDICATOR);
  return a;
}

void create_shell_ui() {
  lv_obj_t *top = lv_layer_top();
  lv_obj_remove_flag(top, LV_OBJ_FLAG_CLICKABLE);

  // ---- metrics strip ----
  lv_obj_t *strip = make_zone(top);
  lv_obj_set_size(strip, SCREEN_WIDTH, SHELL_STRIP_H);
  lv_obj_set_pos(strip, 0, 0);
  lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(strip, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(strip, 1, 0);

  shell_jumps_label = lv_label_create(strip);
  lv_label_set_text(shell_jumps_label, "0");
  lv_obj_set_style_text_color(shell_jumps_label, LV_COLOR_FG, 0);
  // Strip grid: value row on top, caption row at y=22 (one shared baseline
  // with FUEL/HULL/CARGO). Michroma 18 fits three digits left of the fuel arc
  // and stays the biggest element on the strip.
  lv_obj_set_style_text_font(shell_jumps_label, FONT_DISPLAY_MID, 0);
  lv_obj_set_pos(shell_jumps_label, 4, 3);
  dim_label(strip, "JUMPS", 4, 22);

  shell_fuel_arc = make_arc(strip, 108);
  shell_fuel_label = value_label(strip, "--%", 148, 7);
  dim_label(strip, "FUEL", 148, 22);

  shell_hull_arc = make_arc(strip, 212);
  shell_hull_label = value_label(strip, "--%", 252, 7);
  dim_label(strip, "HULL", 252, 22);

  // ---- cargo: two-colour donut, same size/row as FUEL/HULL. The ring fills
  //      dark-yellow for the limpet/drone share first, then orange for the
  //      rest of the cargo; remainder stays dark. Readout "used(drones)/total"
  //      + CARGO caption on the shared baseline. Icons moved to the footer.
  shell_cargo_arc = make_arc(strip, 310);      // bottom: total cargo (orange)
  lv_arc_set_value(shell_cargo_arc, 0);

  // Overlay: limpet/drone share in dark yellow; transparent ring so the
  // orange cargo shows through beyond the drone count.
  shell_drones_arc = lv_arc_create(strip);
  lv_obj_set_size(shell_drones_arc, 34, 34);
  lv_obj_set_pos(shell_drones_arc, 310, 4);
  lv_arc_set_rotation(shell_drones_arc, 270);
  lv_arc_set_bg_angles(shell_drones_arc, 0, 360);
  lv_arc_set_range(shell_drones_arc, 0, 100);
  lv_arc_set_value(shell_drones_arc, 0);
  lv_obj_remove_style(shell_drones_arc, NULL, LV_PART_KNOB);
  lv_obj_remove_flag(shell_drones_arc, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_opa(shell_drones_arc, LV_OPA_TRANSP, LV_PART_MAIN);
  lv_obj_set_style_arc_width(shell_drones_arc, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(shell_drones_arc, lv_color_hex(0xbf9000), LV_PART_INDICATOR);

  shell_cargo_label = value_label(strip, "0(0)/0", 348, 7);
  dim_label(strip, "CARGO", 348, 22);

  // ---- tab rail ----  (stops above the footer so the icons can sit bottom-right)
  static const char *tab_names[3] = {"FTR", "LOG", "SYS"};
  const int railH = SCREEN_HEIGHT - SHELL_STRIP_H - SHELL_FOOTER_H;  // 208
  const int tabH = railH / 3;                                        // 69
  lv_obj_t *rail = make_zone(top);
  lv_obj_set_size(rail, SHELL_RAIL_W, railH);
  lv_obj_set_pos(rail, SCREEN_WIDTH - SHELL_RAIL_W, SHELL_STRIP_H);
  lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_color(rail, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(rail, 1, 0);

  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = lv_button_create(rail);
    tab_buttons[i] = b;
    // last tab absorbs the rounding remainder
    lv_obj_set_size(b, SHELL_RAIL_W - 1, (i < 2) ? tabH : railH - 2 * tabH);
    lv_obj_set_pos(b, 1, i * tabH);
    lv_obj_set_style_bg_color(b, LV_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(b, LV_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, tab_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, tab_names[i]);
    lv_obj_set_style_text_font(l, FONT_DISPLAY_LABEL, 0);
    lv_obj_set_style_text_color(l, LV_COLOR_DIM, 0);
    lv_obj_center(l);
    // Rotate the tab caption 90deg (reads top-to-bottom on the narrow rail)
    lv_obj_set_style_transform_pivot_x(l, lv_pct(50), 0);
    lv_obj_set_style_transform_pivot_y(l, lv_pct(50), 0);
    lv_obj_set_style_transform_rotation(l, 900, 0);
  }

  // ---- footer ----  (full width now: spans under the shortened rail)
  lv_obj_t *footer = make_zone(top);
  lv_obj_set_size(footer, SCREEN_WIDTH, SHELL_FOOTER_H);
  lv_obj_set_pos(footer, 0, SCREEN_HEIGHT - SHELL_FOOTER_H);
  lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(footer, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(footer, 1, 0);

  status_label = lv_label_create(footer);
  lv_label_set_text(status_label, "Waiting for events...");
  lv_obj_set_style_text_color(status_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(status_label, FONT_SMALL, 0);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_DOTS);
  lv_obj_set_width(status_label, 250);
  lv_obj_set_pos(status_label, 8, 3);

  shell_mode_label = lv_label_create(footer);
  lv_label_set_text(shell_mode_label, "");
  lv_obj_set_style_text_color(shell_mode_label, LV_COLOR_VALUE, 0);
  lv_obj_set_style_text_font(shell_mode_label, FONT_SMALL, 0);
  lv_obj_set_style_text_align(shell_mode_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(shell_mode_label, 262, 3);
  lv_obj_set_width(shell_mode_label, 130);  // ends ~392, before the icons

  // Connection icons: bottom-right of the footer (were top-right of the strip)
  bluetooth_icon = lv_label_create(footer);
  lv_label_set_text(bluetooth_icon, LV_SYMBOL_BLUETOOTH);
  websocket_icon = lv_label_create(footer);
  lv_label_set_text(websocket_icon, LV_SYMBOL_REFRESH);
  wifi_icon = lv_label_create(footer);
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_t *icons[3] = {bluetooth_icon, websocket_icon, wifi_icon};
  for (int i = 0; i < 3; i++) {
    lv_obj_set_style_text_color(icons[i], LV_COLOR_HAIRLINE, 0);  // off state
    lv_obj_set_style_text_font(icons[i], FONT_SMALL, 0);
    lv_obj_set_pos(icons[i], 404 + i * 24, 3);
  }
}

void shell_set_active_tab(int page) {
  for (int i = 0; i < 3; i++) {
    if (!tab_buttons[i]) return;
    lv_obj_t *label = lv_obj_get_child(tab_buttons[i], 0);
    if (i == page) {
      lv_obj_set_style_bg_color(tab_buttons[i], LV_COLOR_FG, 0);
      lv_obj_set_style_text_color(label, LV_COLOR_HIGHLIGHT_FG, 0);
    } else {
      lv_obj_set_style_bg_color(tab_buttons[i], LV_COLOR_BG, 0);
      lv_obj_set_style_text_color(label, LV_COLOR_DIM, 0);
    }
  }
}
