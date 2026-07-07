#include "info.h"
#include "../config.h"
#include "../colors.h"
#include "../theme.h"
#include "shell.h"

lv_obj_t *logviewer_screen = nullptr;
lv_obj_t *log_label = nullptr;
lv_obj_t *jump_overlay_label = nullptr;
lv_obj_t *signals_rail_label = nullptr;
lv_obj_t *near_card = nullptr;
lv_obj_t *near_title_label = nullptr;
lv_obj_t *near_genus_label = nullptr;
lv_obj_t *cat_lines[3] = {nullptr};
lv_obj_t *ctx_panel = nullptr;
lv_obj_t *ctx_rail_label = nullptr;
lv_obj_t *ctx_lines[4] = {nullptr};

// No time column (static ages were useless); the freed width goes to the
// sidebar so long context lines ("HONK OK  BODIES 20/20 OK") fit.
#define EVENTS_W 230
#define SIDE_X   (EVENTS_W + 8)                   // 238
#define SIDE_W   (CONTENT_W - SIDE_X - 6)         // 202
#define CTX_H    76

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
  rail(logviewer_screen, "EVENTS", CONTENT_X + 8, CONTENT_Y + 4, EVENTS_W);

  log_label = lv_label_create(logviewer_screen);
  lv_obj_set_pos(log_label, CONTENT_X + 8, CONTENT_Y + 24);
  lv_obj_set_width(log_label, EVENTS_W);
  lv_label_set_text(log_label, " ");
  lv_label_set_long_mode(log_label, LV_LABEL_LONG_MODE_CLIP);  // 1 line per entry
  lv_obj_set_style_text_color(log_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(log_label, FONT_BODY, 0);

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

  // Dynamic header: system-wide signal tally, e.g. "SIGNALS 15  B8 G22"
  signals_rail_label = rail(side, "SIGNALS", 0, 0, SIDE_W - 8);
  lv_obj_set_pos(signals_rail_label, 0, 0);

  // One card: the body whose gravity well the player is in (ApproachBody) —
  // detail exactly where the probe scan / landing happens. Two lines: title +
  // genus chips, like the old pin cards.
  near_card = lv_obj_create(side);
  lv_obj_set_width(near_card, SIDE_W - 8);
  lv_obj_set_height(near_card, LV_SIZE_CONTENT);
  lv_obj_add_style(near_card, &style_card, 0);
  lv_obj_set_scrollbar_mode(near_card, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(near_card, LV_DIR_NONE);
  lv_obj_set_flex_flow(near_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_add_flag(near_card, LV_OBJ_FLAG_HIDDEN);

  near_title_label = lv_label_create(near_card);
  lv_obj_set_width(near_title_label, SIDE_W - 18);
  lv_label_set_long_mode(near_title_label, LV_LABEL_LONG_MODE_WRAP);
  lv_label_set_text(near_title_label, "");
  lv_obj_set_style_text_font(near_title_label, FONT_BODY, 0);
  lv_obj_set_style_text_color(near_title_label, lv_color_hex(0xffb000), 0);

  near_genus_label = lv_label_create(near_card);
  lv_obj_set_width(near_genus_label, SIDE_W - 18);
  lv_label_set_long_mode(near_genus_label, LV_LABEL_LONG_MODE_WRAP);
  lv_label_set_recolor(near_genus_label, true);
  lv_label_set_text(near_genus_label, "");
  lv_obj_set_style_text_font(near_genus_label, FONT_SMALL, 0);
  lv_obj_add_flag(near_genus_label, LV_OBJ_FLAG_HIDDEN);

  // Category lines below the card: compact body lists per signal type
  // ("BIO: 5d 5e" / "GEO: 1a 1c ..."), smaller type, tighter leading —
  // counts live in the header, these answer "which bodies".
  for (int i = 0; i < 3; i++) {
    cat_lines[i] = lv_label_create(side);
    lv_obj_set_width(cat_lines[i], SIDE_W - 8);
    lv_label_set_long_mode(cat_lines[i], LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(cat_lines[i], "");
    lv_obj_set_style_text_font(cat_lines[i], FONT_SMALL, 0);
    lv_obj_set_style_text_line_space(cat_lines[i], -2, 0);
    lv_obj_set_style_text_color(cat_lines[i], LV_COLOR_FG, 0);
    lv_obj_add_flag(cat_lines[i], LV_OBJ_FLAG_HIDDEN);
  }

  // Stack rail + card + lines with flex; context panel is bottom-anchored
  // separately.
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
