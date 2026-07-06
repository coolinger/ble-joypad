#include "fighter.h"
#include <Arduino.h>
#include "BleGamepad.h"
#include "sound.h"
#include "gamedata.h"
#include "config.h"
#include "colors.h"
#include "theme.h"

// External BLE gamepad instance
extern BleGamepad* bleGamepad;

// Fighter commands stored in flash
const FighterCommand PROGMEM commands[8] = {
    {"Fighter Zurueckordern", 13},
    {"Verteidigen", 14},
    {"Feuer Frei", 15},
    {"Mein Ziel angreifen", 16},
    {"Formation halten", 17},
    {"Position halten", 18},
    {"Mir folgen", 19},
    {"Befehle oeffnen", 20}};

lv_obj_t *fighter_screen = nullptr;
lv_obj_t *btnmatrix = nullptr;

static const char * btnm_map[] = {"Zurueck", "Verteid.", "Feuer", "\n",
                                  "Folgen", "Center", "Angriff", "\n",
                                  "Position", "Formation", "Befehle", ""};

// One-shot timer callback: releases the gamepad button ~50 ms after the press,
// without blocking the LVGL/render thread the way delay() did.
static void release_btn_cb(lv_timer_t * timer)
{
    uint8_t btn = (uint8_t)(uintptr_t)lv_timer_get_user_data(timer);
    if (bleGamepad) bleGamepad->release(btn);
}

// Emulate a momentary press: press now, schedule the release. The timer is
// created with repeat count 1 so LVGL auto-deletes it after it fires once.
static void pressMomentary(uint8_t btn)
{
    if (!bleGamepad) return;
    bleGamepad->press(btn);
    lv_timer_t * t = lv_timer_create(release_btn_cb, 50, (void*)(uintptr_t)btn);
    lv_timer_set_repeat_count(t, 1);
}

static void btnmatrix_event_handler(lv_event_t * e)
{
    lv_obj_t * obj = (lv_obj_t*)lv_event_get_target(e);
    uint32_t id = lv_buttonmatrix_get_selected_button(obj);
    const uint8_t btn_to_cmd[] = {0, 1, 2, 7, 20, 3, 5, 4, 6};

    if (id < 9) {
        uint8_t cmd_idx = btn_to_cmd[id];

        if (cmd_idx == 20) {
            Serial.println("Befehle oeffnen/schliessen");
            beepClick();
            pressMomentary(20);
        } else if (cmd_idx < 8) {
            FighterCommand cmd;
            memcpy_P(&cmd, &commands[cmd_idx], sizeof(FighterCommand));

            Serial.printf("Command: %s (Button %d)\n", cmd.name, cmd.button_id);
            beepClick();
            pressMomentary(cmd.button_id);
        }
    }
}

void create_fighter_ui()
{
  fighter_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(fighter_screen, LV_COLOR_BG, 0);
  lv_screen_load(fighter_screen);

  btnmatrix = lv_buttonmatrix_create(fighter_screen);
  lv_buttonmatrix_set_map(btnmatrix, btnm_map);
      
  static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_pad_all(&style_bg, 0);
    lv_style_set_pad_gap(&style_bg, 4);
    lv_style_set_clip_corner(&style_bg, true);
    lv_style_set_radius(&style_bg, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&style_bg, 0);

  // Fill entire screen with 1px padding
  lv_obj_set_size(btnmatrix, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2);
  lv_obj_set_pos(btnmatrix, 1, 1);

    lv_obj_set_style_bg_color(btnmatrix, LV_COLOR_GAUGE_BG, 0);
    lv_obj_set_style_border_color(btnmatrix, LV_COLOR_GAUGE_FG, 0);
    lv_obj_set_style_border_width(btnmatrix, 1, 0);
  lv_obj_set_style_radius(btnmatrix, 10, 0);
  lv_obj_add_style(btnmatrix, &style_bg, 0);
  lv_obj_set_style_text_font(btnmatrix, FONT_HEAD, LV_PART_ITEMS);

    lv_obj_set_style_bg_color(btnmatrix, LV_COLOR_BG, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(btnmatrix, LV_COLOR_HIGHLIGHT_BG, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btnmatrix, LV_COLOR_FG, LV_PART_ITEMS);
    lv_obj_set_style_border_color(btnmatrix, LV_COLOR_GAUGE_FG, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnmatrix, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(btnmatrix, 8, LV_PART_ITEMS);
  
  lv_buttonmatrix_set_button_ctrl(btnmatrix, 4, LV_BUTTONMATRIX_CTRL_NO_REPEAT);
  lv_obj_add_event_cb(btnmatrix, btnmatrix_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
}
