#include "info.h"
#include "../config.h"
#include "../colors.h"
#include "../theme.h"
#include "shell.h"

lv_obj_t *logviewer_screen = nullptr;
lv_obj_t *log_label = nullptr;
lv_obj_t *log_time_label = nullptr;
lv_obj_t *jump_overlay_label = nullptr;
lv_obj_t *pin_cards[MAX_PINNED_BODIES] = {nullptr};
lv_obj_t *pin_title_labels[MAX_PINNED_BODIES] = {nullptr};
lv_obj_t *pin_genus_labels[MAX_PINNED_BODIES] = {nullptr};
lv_obj_t *ctx_panel = nullptr;
lv_obj_t *ctx_rail_label = nullptr;
lv_obj_t *ctx_lines[4] = {nullptr};

#define EVENTS_W 226
#define TIME_W   38
#define SIDE_X   (EVENTS_W + TIME_W + 8)          // 272
#define SIDE_W   (CONTENT_W - SIDE_X - 6)         // 168
#define CTX_H    72

static lv_obj_t* rail(lv_obj_t *parent, const char *txt, int x, int y, int w) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, FONT_DISPLAY_LABEL, 0);
  lv_obj_set_style_text_color(l, LV_COLOR_DIM, 0);
  lv_obj_set_style_border_side(l, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(l, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(l, 1, 0);
  lv_obj_set_style_pad_bottom(l, 2, 0);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, w);
  return l;
}

void create_logviewer_ui() {
  logviewer_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(logviewer_screen, LV_COLOR_BG, 0);

  // ---- events column (left) ----
  rail(logviewer_screen, "EVENTS", CONTENT_X + 8, CONTENT_Y + 4, EVENTS_W + TIME_W);

  log_label = lv_label_create(logviewer_screen);
  lv_obj_set_pos(log_label, CONTENT_X + 8, CONTENT_Y + 24);
  lv_obj_set_width(log_label, EVENTS_W);
  lv_label_set_text(log_label, " ");
  lv_label_set_long_mode(log_label, LV_LABEL_LONG_MODE_CLIP);  // 1 line per entry
  lv_obj_set_style_text_color(log_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(log_label, FONT_BODY, 0);

  log_time_label = lv_label_create(logviewer_screen);
  lv_obj_set_pos(log_time_label, CONTENT_X + 8 + EVENTS_W, CONTENT_Y + 24);
  lv_obj_set_width(log_time_label, TIME_W);
  lv_label_set_text(log_time_label, " ");
  lv_obj_set_style_text_align(log_time_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(log_time_label, LV_COLOR_DIM, 0);
  lv_obj_set_style_text_font(log_time_label, FONT_BODY, 0);

  // ---- sidebar (right) ----
  lv_obj_t *side = lv_obj_create(logviewer_screen);
  lv_obj_set_size(side, SIDE_W + 6, CONTENT_H);
  lv_obj_set_pos(side, SIDE_X, CONTENT_Y);
  lv_obj_set_style_bg_color(side, LV_COLOR_BG, 0);
  lv_obj_set_style_border_side(side, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_color(side, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(side, 1, 0);
  lv_obj_set_style_radius(side, 0, 0);
  lv_obj_set_style_pad_all(side, 4, 0);
  lv_obj_set_style_pad_row(side, 3, 0);
  lv_obj_set_scrollbar_mode(side, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(side, LV_DIR_NONE);

  lv_obj_t *sig_rail = rail(side, "SIGNALS", 0, 0, SIDE_W - 8);
  lv_obj_set_pos(sig_rail, 0, 0);

  for (int i = 0; i < MAX_PINNED_BODIES; i++) {
    pin_cards[i] = lv_obj_create(side);
    lv_obj_set_width(pin_cards[i], SIDE_W - 8);
    lv_obj_set_height(pin_cards[i], LV_SIZE_CONTENT);
    lv_obj_set_pos(pin_cards[i], 0, 20);   // stacked by flex below
    lv_obj_add_style(pin_cards[i], &style_card, 0);
    lv_obj_set_scrollbar_mode(pin_cards[i], LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(pin_cards[i], LV_DIR_NONE);
    lv_obj_set_flex_flow(pin_cards[i], LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(pin_cards[i], LV_OBJ_FLAG_HIDDEN);

    pin_title_labels[i] = lv_label_create(pin_cards[i]);
    lv_obj_set_width(pin_title_labels[i], SIDE_W - 18);
    lv_label_set_long_mode(pin_title_labels[i], LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(pin_title_labels[i], "");
    lv_obj_set_style_text_font(pin_title_labels[i], FONT_BODY, 0);
    lv_obj_set_style_text_color(pin_title_labels[i], lv_color_hex(0xffb000), 0);

    pin_genus_labels[i] = lv_label_create(pin_cards[i]);
    lv_obj_set_width(pin_genus_labels[i], SIDE_W - 18);
    lv_label_set_long_mode(pin_genus_labels[i], LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_recolor(pin_genus_labels[i], true);
    lv_label_set_text(pin_genus_labels[i], "");
    lv_obj_set_style_text_font(pin_genus_labels[i], FONT_SMALL, 0);
    lv_obj_add_flag(pin_genus_labels[i], LV_OBJ_FLAG_HIDDEN);
  }
  // Stack rail + cards with flex; context panel is bottom-anchored separately.
  lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);

  // ---- context panel (bottom of sidebar, fixed) ----
  ctx_panel = lv_obj_create(logviewer_screen);
  lv_obj_set_size(ctx_panel, SIDE_W, CTX_H);
  lv_obj_set_pos(ctx_panel, SIDE_X + 5, CONTENT_Y + CONTENT_H - CTX_H - 2);
  lv_obj_add_style(ctx_panel, &style_card, 0);
  lv_obj_set_scrollbar_mode(ctx_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(ctx_panel, LV_DIR_NONE);

  ctx_rail_label = lv_label_create(ctx_panel);
  lv_label_set_text(ctx_rail_label, "EXPLORATION");
  lv_obj_set_style_text_font(ctx_rail_label, FONT_DISPLAY_LABEL, 0);
  lv_obj_set_style_text_color(ctx_rail_label, LV_COLOR_DIM, 0);
  lv_obj_set_pos(ctx_rail_label, 0, 0);

  for (int i = 0; i < 4; i++) {
    ctx_lines[i] = lv_label_create(ctx_panel);
    lv_label_set_text(ctx_lines[i], "");
    lv_obj_set_style_text_font(ctx_lines[i], FONT_SMALL, 0);
    lv_obj_set_style_text_color(ctx_lines[i], LV_COLOR_FG, 0);
    lv_obj_set_pos(ctx_lines[i], 0, 15 + i * 13);
  }
}
