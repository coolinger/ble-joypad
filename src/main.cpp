#include <Arduino.h>
#include <Wire.h>
#include <PCF8575.h>
#include <BleGamepad.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <WiFi.h>


#define PCF8575_ADDR 0x20
#define I2C_SDA 21
#define I2C_SCL 22
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define LVGL_BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 20)
uint32_t buf[LVGL_BUFFER_SIZE / 4];
#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define CST820_I2C_ADDR 0x15

// Button mapping (PCF8575 pins)
enum ButtonIndex
{
  SILVER_LEFT = 13,
  SILVER_MID = 14,
  SILVER_RIGHT = 15,
  BLACK = 1,
  WHITE = 0,
  RED = 5,
  YELLOW = 2,
  BLUE = 3,
  GREEN = 4,
  LARGE_YELLOW = 12,
  LARGE_BLUE = 11,
  LARGE_GREEN = 10
};

const uint8_t buttonPins[12] = {
    SILVER_LEFT, SILVER_MID, SILVER_RIGHT,
    BLACK, WHITE, RED,
    YELLOW, BLUE, GREEN,
    LARGE_YELLOW, LARGE_BLUE, LARGE_GREEN};
PCF8575 pcf(PCF8575_ADDR);
BleGamepad bleGamepad("CoolJoyBLE", "leDev", 100);
BleGamepadConfiguration bleGamepadConfig;
uint16_t lastButtonState = 0;
bool bleConnected = false;

// Display and LVGL objects
//TFT_eSPI tft = TFT_eSPI();
static lv_display_t* disp;

// Fighter command structure
struct FighterCommand
{
  const char *name;
  uint8_t button_id;
};


#define WIFI_SSID "0619562e-bcbf-4bfc-97a8"
#define WIFI_PASSWORD "3869212721440634"
#define HOSTNAME "FighterController"

// Fighter commands - Store in Flash
const FighterCommand PROGMEM commands[8] = {
    {"Fighter Zurueckordern", 13},
    {"Verteidigen", 14},
    {"Feuer Frei", 15},
    {"Mein Ziel angreifen", 16},
    {"Formation halten", 17},
    {"Position halten", 18},
    {"Mir folgen", 19},
    {"Befehle oeffnen", 20}};

// UI objects
lv_obj_t *fighter_screen;
lv_obj_t *btnmatrix;

// Display power management
bool displayOn = true;
uint32_t lastTouchTime = 0;
uint32_t lastBleActiveTime = 0;
uint32_t bleDisconnectedTime = 0;
const uint32_t DISPLAY_TIMEOUT = 30 * 60 * 1000; // 30 minutes in milliseconds
const uint32_t LED_TIMEOUT = 5 * 60 * 1000; // 5 minutes in milliseconds
bool ledOn = true;

static const char * btnm_map[] = {"Zurueck", "Verteid.", "Feuer", "\n",
                                  "Folgen", "Center", "Angriff", "\n",
                                  "Position", "Formation", "Befehle", ""};
void setLedColor(uint8_t r, uint8_t g, uint8_t b)
{
  analogWrite(LED_R, 255 - r);
  analogWrite(LED_G, 255 - g);
  analogWrite(LED_B, 255 - b);
}

void setDisplayPower(bool on)
{
  if (on && !displayOn) {
    digitalWrite(27, HIGH); // Turn on backlight
    displayOn = true;
    Serial.println("Display ON");
  } else if (!on && displayOn) {
    digitalWrite(27, LOW); // Turn off backlight
    displayOn = false;
    Serial.println("Display OFF");
  }
}

static void btnmatrix_event_handler(lv_event_t * e)
{
    lv_obj_t * obj = (lv_obj_t*)lv_event_get_target(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(obj);
    const uint8_t btn_to_cmd[] = {0, 1, 2, 7, 20, 3, 5, 4, 6};
    
    if (id < 9) {
        uint8_t cmd_idx = btn_to_cmd[id];
        
        if (cmd_idx == 20) {
            Serial.println("Befehle oeffnen/schliessen");
            bleGamepad.press(20);
            delay(50);
            bleGamepad.release(20);
            setLedColor(255, 255, 0);
        } else if (cmd_idx < 8) {
            FighterCommand cmd;
            memcpy_P(&cmd, &commands[cmd_idx], sizeof(FighterCommand));
            
            Serial.printf("Command: %s (Button %d)\n", cmd.name, cmd.button_id);
            bleGamepad.press(cmd.button_id);
            delay(50);
            bleGamepad.release(cmd.button_id);
            setLedColor(0, 255, 0);
        }
    }
}


void initTouch()
{
  Serial.println("Init Touch");
  Wire1.begin(TOUCH_SDA, TOUCH_SCL);
  Wire1.beginTransmission(CST820_I2C_ADDR);
  uint8_t error = Wire1.endTransmission();
  if (error == 0) {
    Serial.println("CST820 found on I2C bus");
  } else {
    Serial.printf("CST820 NOT found! Error: %d\n", error);
  }
  Wire1.end();
}

/*void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, unsigned char *color_p)
{
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  tft.startWrite();
  tft.setAddrWindow(area->x1, area->y1, w, h);
  tft.pushColors((uint16_t *)color_p, w * h, true);
  tft.endWrite();
  lv_display_flush_ready(disp);
}*/

void create_fighter_ui()
{
  fighter_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(fighter_screen, lv_color_hex(0x121212), 0);
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

  lv_obj_set_style_bg_color(btnmatrix, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_color(btnmatrix, lv_color_hex(0x555555), 0);
  lv_obj_set_style_border_width(btnmatrix, 0, 0);
  lv_obj_set_style_radius(btnmatrix, 10, 0);
  lv_obj_add_style(btnmatrix, &style_bg, 0);

  lv_obj_set_style_bg_color(btnmatrix, lv_color_hex(0x444444), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(btnmatrix, lv_color_hex(0xff9500), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btnmatrix, lv_color_hex(0xffffff), LV_PART_ITEMS);
  lv_obj_set_style_border_color(btnmatrix, lv_color_hex(0x666666), LV_PART_ITEMS);
  lv_obj_set_style_border_width(btnmatrix, 1, LV_PART_ITEMS);
  lv_obj_set_style_radius(btnmatrix, 5, LV_PART_ITEMS);
  
  lv_btnmatrix_set_btn_ctrl(btnmatrix, 4, LV_BTNMATRIX_CTRL_NO_REPEAT);
  lv_obj_add_event_cb(btnmatrix, btnmatrix_event_handler, LV_EVENT_VALUE_CHANGED, NULL);
}
static void touch_read(lv_indev_t * indev, lv_indev_data_t * data) {

  int x, y;

  uint8_t touchdata[5];
  Wire1.begin(TOUCH_SDA, TOUCH_SCL);
  Wire1.beginTransmission(CST820_I2C_ADDR);
  Wire1.write(0x02);
  Wire1.endTransmission(false);
  Wire1.requestFrom(CST820_I2C_ADDR, 5);
  for (int i = 0; i < 5; i++) {
    touchdata[i] = Wire1.read();
  }
  Wire1.end();
  
  if (touchdata[0] == 0 || touchdata[0] == 0xFF) {
    data->state = LV_INDEV_STATE_RELEASED;
    return;
  }
  
  // Touch detected - update last touch time
  lastTouchTime = millis();
  
  // If display is off, turn it on but don't process the touch as a button press
  if (!displayOn) {
    setDisplayPower(true);
    data->state = LV_INDEV_STATE_RELEASED; // Ignore this touch for button processing
    return;
  }
  
  // Read raw touch coordinates
  int raw_x = ((touchdata[1] & 0x0f) << 8) | touchdata[2];
  int raw_y = ((touchdata[3] & 0x0f) << 8) | touchdata[4];
  
  // Correct for orientation.
  // (Mostly done by LVGL, it just has the axes inverted in landscape somehow.)
  lv_display_rotation_t rotation = lv_display_get_rotation(disp);
  if (rotation == LV_DISPLAY_ROTATION_90 || rotation == LV_DISPLAY_ROTATION_270) {
    x = SCREEN_HEIGHT - raw_x;
    y = SCREEN_WIDTH - raw_y;
  } else {
    x = raw_x;
    y = raw_y;
  }

  data->point.x = x;
  data->point.y = y;
  data->state = LV_INDEV_STATE_PRESSED;

  Serial.printf("Touch: raw(%d,%d) -> screen(%d,%d)\n", raw_x, raw_y, x, y);

}

// LVGL logging callback
void my_print(lv_log_level_t level, const char * buf)
{
  LV_UNUSED(level);
  Serial.print("[LVGL] ");
  Serial.print(buf);
  Serial.flush();
}

void init_display()
{

  

  /*tft.begin();
  tft.setRotation(3);
  tft.initDMA();
  tft.fillScreen(TFT_BLACK);*/
  digitalWrite(27, HIGH);

  initTouch();

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return millis(); });
  lv_log_register_print_cb(my_print);

  //disp = lv_display_create(SCREEN_WIDTH, SCREEN_HEIGHT);
  disp = lv_tft_espi_create(SCREEN_HEIGHT, SCREEN_WIDTH, buf, sizeof(buf));
  lv_display_set_rotation(disp, LV_DISPLAY_ROTATION_270);
  //lv_display_set_flush_cb(disp, lvgl_flush_cb);
  //lv_display_set_buffers(disp, buf, NULL, LVGL_BUFFER_SIZE, LV_DISPLAY_RENDER_MODE_PARTIAL);

  /*TFT_eSPI *tft = (TFT_eSPI *) lv_display_get_driver_data(disp);

    // ST7789 needs to be inverted
    tft->invertDisplay(true);

    // gamma fix for ST7789
    tft->writecommand(0x26); //Gamma curve selected
    tft->writedata(2);
    delay(120);
    tft->writecommand(0x26); //Gamma curve selected
    tft->writedata(1);
  */



  lv_indev_t* indev = lv_indev_create();
  lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(indev, touch_read);

  create_fighter_ui();
}

void checkBleConnection()
{
  if (bleGamepad.isConnected())
  {
    if (!bleConnected)
    {
      Serial.println("[BLE] Connected");
      setLedColor(0, 0, 255);
      bleConnected = true;
      ledOn = true;
      setDisplayPower(true);
    }
    lastBleActiveTime = millis();
  }
  else
  {
    if (bleConnected)
    {
      Serial.println("[BLE] Disconnected");
      setLedColor(255, 0, 0);
      bleConnected = false;
      ledOn = true;
      bleDisconnectedTime = millis();
      setDisplayPower(false);
    }
    
    // Turn off red LED after 5 minutes of no connection
    if (ledOn && (millis() - bleDisconnectedTime) > LED_TIMEOUT) {
      setLedColor(0, 0, 0); // Turn off LED
      ledOn = false;
      Serial.println("[LED] Timeout - turning off");
    }
  }
}

void checkDisplayTimeout()
{
  uint32_t currentTime = millis();
  
  // Turn off display if no BLE connection
  if (!bleConnected && displayOn) {
    setDisplayPower(false);
    return;
  }
  
  // Turn off display after 30 minutes of no touch (only if BLE is connected)
  if (bleConnected && displayOn && (currentTime - lastTouchTime) > DISPLAY_TIMEOUT) {
    setDisplayPower(false);
  }
}

void WifiConnect()
{
  setLedColor(255, 0, 255); // Magenta during WiFi setup
  
  // Configure WiFi with auto-reconnect
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  // Set hostname
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  Serial.print("Connecting to WiFi");
  uint8_t retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) { // 10 seconds timeout
    delay(500);
    Serial.print(".");
    retries++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed, continuing without WiFi");
  }
  
  setLedColor(255, 0, 0); // Red = waiting for BLE
  
}

void setup()
{
  Serial.begin(115200);
  delay(500);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  WifiConnect();
  delay(500);
  // Initialize display power management
  lastTouchTime = millis();
  lastBleActiveTime = millis();
  bleDisconnectedTime = millis();

  Wire.begin(I2C_SDA, I2C_SCL);
  pcf.begin();

  bleGamepadConfig.setButtonCount(20);
  bleGamepadConfig.setIncludeRxAxis(false);
  bleGamepadConfig.setWhichAxes(false, false, false, false, false, false, false, false);
  bleGamepadConfig.setHatSwitchCount(0);

  bleGamepad.begin(&bleGamepadConfig);
  init_display();
  Serial.println("BLE Joypad with Fighter Commands ready!");
}

void loop()
{
  static uint32_t lastTick = 0;
  
  //uint32_t currentTime = millis();
  //lv_tick_inc(currentTime - lastTick);
  //lastTick = currentTime;
  
  lv_timer_handler();
  checkBleConnection();
  checkDisplayTimeout();
  
  if (bleConnected) {
    uint16_t pcfState = pcf.read16();
    bool anyPressed = false;
    for (uint8_t i = 0; i < 12; i++) {
      bool pressed = !(pcfState & (1 << buttonPins[i]));
      bool lastPressed = (lastButtonState & (1 << i)) != 0;
      
      if (pressed != lastPressed) {
        if (pressed) {
          bleGamepad.press(i + 1);
          Serial.printf("Button %d pressed\n", i + 1);
          anyPressed = true;
          lastButtonState |= (1 << i);
        } else {
          bleGamepad.release(i + 1);
          Serial.printf("Button %d released\n", i + 1);
          lastButtonState &= ~(1 << i);
        }
      }
    }
    bleGamepad.sendReport();
    if (anyPressed)
      setLedColor(0, 255, 0);
    else
      setLedColor(0, 0, 255);
  }
  
  delay(5);
}