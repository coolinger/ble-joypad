#include "fighter.h"
#include <Arduino.h>
#include "BleGamepad.h"
#include "sound.h"
#include "gamedata.h"
#include "config.h"
#include "colors.h"

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

static void btnmatrix_event_handler(lv_event_t * e)
{
    lv_obj_t * obj = (lv_obj_t*)lv_event_get_target(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(obj);
    const uint8_t btn_to_cmd[] = {0, 1, 2, 7, 20, 3, 5, 4, 6};
    
    if (id < 9) {
        uint8_t cmd_idx = btn_to_cmd[id];
        
        if (cmd_idx == 20) {
            Serial.println("Befehle oeffnen/schliessen");
            beepClick();
            bleGamepad->press(20);
            delay(50);
            bleGamepad->release(20);
        } else if (cmd_idx < 8) {
            FighterCommand cmd;
            memcpy_P(&cmd, &commands[cmd_idx], sizeof(FighterCommand));
            
            Serial.printf("Command: %s (Button %d)\n", cmd.name, cmd.button_id);
            beepClick();
            bleGamepad->press(cmd.button_id);
            delay(50);
            bleGamepad->release(cmd.button_id);
        }
    }
}

void create_fighter_ui()
{
  fighter_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(fighter_screen, LV_COLOR_BG, 0);
  lv_scr_load(fighter_screen);

  btnmatrix = lv_btnmatrix_create(fighter_screen);
  lv_btnmatrix_set_map(btnmatrix, btnm_map);
      
  static lv_style_t style_bg;
    lv_style_init(&style_bg);
    lv_style_set_pad_all(&style_bg, 0);
    lv_style_set_pad_gap(&style_bg, 1);
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

    lv_obj_set_style_bg_color(btnmatrix, LV_COLOR_BG, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(btnmatrix, LV_COLOR_HIGHLIGHT_BG, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btnmatrix, LV_COLOR_FG, LV_PART_ITEMS);
    lv_obj_set_style_border_color(btnmatrix, LV_COLOR_GAUGE_FG, LV_PART_ITEMS);
    lv_obj_set_style_border_width(btnmatrix, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(btnmatrix, 5, LV_PART_ITEMS);
  
  lv_btnmatrix_set_btn_ctrl(btnmatrix, 4, LV_BTNMATRIX_CTRL_NO_REPEAT);
  lv_obj_add_event_cb(btnmatrix, btnmatrix_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
}
