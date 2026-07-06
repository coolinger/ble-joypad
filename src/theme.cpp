#include "theme.h"

lv_style_t style_panel;
lv_style_t style_card;
lv_style_t style_btn;
lv_style_t style_btn_pressed;

void theme_init()
{
  lv_style_init(&style_panel);
  lv_style_set_bg_color(&style_panel, LV_COLOR_GAUGE_BG);
  lv_style_set_bg_opa(&style_panel, LV_OPA_COVER);
  lv_style_set_border_color(&style_panel, LV_COLOR_GAUGE_FG);
  lv_style_set_border_width(&style_panel, 1);
  lv_style_set_radius(&style_panel, 5);
  lv_style_set_pad_all(&style_panel, 6);
  lv_style_set_text_color(&style_panel, LV_COLOR_FG);

  lv_style_init(&style_card);
  lv_style_set_bg_color(&style_card, LV_COLOR_GAUGE_BG);
  lv_style_set_bg_opa(&style_card, LV_OPA_COVER);
  lv_style_set_border_width(&style_card, 0);
  lv_style_set_radius(&style_card, 4);
  lv_style_set_pad_all(&style_card, 4);
  lv_style_set_pad_row(&style_card, 2);
  lv_style_set_text_color(&style_card, LV_COLOR_FG);

  lv_style_init(&style_btn);
  lv_style_set_bg_color(&style_btn, LV_COLOR_GAUGE_BG);
  lv_style_set_bg_opa(&style_btn, LV_OPA_COVER);
  lv_style_set_border_color(&style_btn, LV_COLOR_GAUGE_FG);
  lv_style_set_border_width(&style_btn, 1);
  lv_style_set_radius(&style_btn, 5);
  lv_style_set_text_color(&style_btn, LV_COLOR_FG);

  lv_style_init(&style_btn_pressed);
  lv_style_set_bg_color(&style_btn_pressed, LV_COLOR_HIGHLIGHT_BG);
  lv_style_set_text_color(&style_btn_pressed, LV_COLOR_HIGHLIGHT_FG);
}
