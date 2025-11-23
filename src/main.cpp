#include <Arduino.h>
#include <Wire.h>
#include <PCF8575.h>
#include <BleGamepad.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <esp_heap_caps.h>

using namespace websockets;


#define PCF8575_ADDR 0x20
#define I2C_SDA 21
#define I2C_SCL 22
#define LED_R 4
#define LED_G 16
#define LED_B 17
#define BUZZER_PIN 5
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define LVGL_BUFFER_SIZE (SCREEN_WIDTH * SCREEN_HEIGHT / 30)
uint32_t buf[LVGL_BUFFER_SIZE / 4];
#define TOUCH_SDA 33
#define TOUCH_SCL 32
#define CST820_I2C_ADDR 0x15



#include "colors.h"
/*
//definiere Farben aus HTML Farbcode
#define COLOR_BG lv_color_hex(0x250700)
#define COLOR_FG lv_color_hex(0xdc5904)
#define COLOR_BAR_FG lv_color_hex(0x7e5407)
#define COLOR_BAR_BG lv_color_hex(0x4a1504)
#define COLOR_ALERT lv_color_hex(0xff0000)
*/


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
#define UDP_PORT 12345

// Elite Dangerous data structures
struct CargoInfo {
  int totalCapacity = 256;
  int usedSpace = 0;
  int dronesCount = 0;
  int cargoCount = 0;  // non-drones cargo
};

struct FuelInfo {
  float fuelMain = 0.0f;
  float fuelCapacity = 32.0f;
};

struct HullInfo {
  float hullHealth = 1.0f;
};

struct NavRouteInfo {
  int jumpsRemaining = 0;
};

struct EventLogEntry {
  char text[55];  // Reduced to fit 8 entries in memory
  uint32_t timestamp;
  int count;  // For consolidating repeated entries
};

// Global Elite data
CargoInfo cargoInfo;
FuelInfo fuelInfo;
HullInfo hullInfo;
NavRouteInfo navRouteInfo;
EventLogEntry eventLog[9] = {};  // Show last 9 events, initialized to zero
int eventLogIndex = 0;
int eventLogCount = 0;  // Track how many events we actually have
char motherlodeMaterial[32] = "";
bool blinkScreen = false;
int blinkCount = 0;
uint32_t lastBlinkTime = 0;

// UDP - Separate sockets for receiving (Core 0) and sending (Core 1)
WiFiUDP udpReceiver;  // Core 0 - receives messages on UDP_PORT
WiFiUDP udpSender;    // Core 1 - sends SUMMARY requests
char* udpBuffer = nullptr;
const int UDP_BUFFER_SIZE = 2048;
int lastEventId = -1;  // Track last event ID for deduplication

// WebSocket variables for Elite Dangerous data
String serverIP = "";
int websocketPort = 0;
int udpControlPort = 0;
bool useWebSocket = false;
WebsocketsClient wsClient;

// Task handle for Core 0
TaskHandle_t udpTaskHandle = NULL;
SemaphoreHandle_t lvglMutex = NULL;

// List of journal events to ignore - Store in Flash to save DRAM
const char ignoreEvent_Music[] PROGMEM = "Music";
const char ignoreEvent_Fileheader[] PROGMEM = "Fileheader";
const char ignoreEvent_Commander[] PROGMEM = "Commander";
const char ignoreEvent_LoadGame[] PROGMEM = "LoadGame";
const char ignoreEvent_Loadout[] PROGMEM = "Loadout";
const char ignoreEvent_Materials[] PROGMEM = "Materials";
const char ignoreEvent_MaterialCollected[] PROGMEM = "MaterialCollected";
const char ignoreEvent_ShipLocker[] PROGMEM = "ShipLocker";
const char ignoreEvent_Missions[] PROGMEM = "Missions";

const char* const ignoreJournalEvents[] PROGMEM = {
  ignoreEvent_Music,
  ignoreEvent_Fileheader,
  ignoreEvent_Commander,
  ignoreEvent_LoadGame,
  ignoreEvent_Loadout,
  ignoreEvent_Materials,
  ignoreEvent_MaterialCollected,
  ignoreEvent_ShipLocker,
  ignoreEvent_Missions
};
const int ignoreJournalEventsCount = sizeof(ignoreJournalEvents) / sizeof(ignoreJournalEvents[0]);

// List of journal events to hide from display but still process
const char hideEvent_Cargo[] PROGMEM = "Cargo";

const char* const hideJournalEvents[] PROGMEM = {
  hideEvent_Cargo
};
const int hideJournalEventsCount = sizeof(hideJournalEvents) / sizeof(hideJournalEvents[0]);

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
lv_obj_t *logviewer_screen;
lv_obj_t *log_label;
lv_obj_t *cargo_bar;
lv_obj_t *header_label;
lv_obj_t *fuel_bar;
lv_obj_t *hull_bar;
lv_obj_t *wifi_icon;
lv_obj_t *websocket_icon;
lv_obj_t *bluetooth_icon;
int currentPage = 1;  // 0 = fighter, 1 = logviewer (start on page 2)

// Display power management
bool displayOn = true;
uint32_t lastTouchTime = 0;
uint32_t lastBleActiveTime = 0;
uint32_t bleDisconnectedTime = 0;
const uint32_t DISPLAY_TIMEOUT = 5 * 60 * 1000; // 5 minutes in milliseconds
const uint32_t LED_TIMEOUT = 5 * 60 * 1000; // 5 minutes in milliseconds
bool ledOn = true;

// WiFi reconnection management
uint32_t lastWifiCheck = 0;
const uint32_t WIFI_CHECK_INTERVAL = 10000; // Check every 10 seconds
bool wifiWasConnected = false;
uint32_t lastWifiStatusPrint = 0;
const uint32_t WIFI_STATUS_PRINT_INTERVAL = 30000; // Print every 30 seconds

// Heap monitoring
uint32_t lastHeapPrint = 0;
const uint32_t HEAP_PRINT_INTERVAL = 45000; // Print every 45 seconds

// Summary request management
bool summaryReceived = false;
uint32_t lastSummaryRequest = 0;
const uint32_t SUMMARY_REQUEST_INTERVAL = 10000; // Request every 10 seconds until first received

static const char * btnm_map[] = {"Zurueck", "Verteid.", "Feuer", "\n",
                                  "Folgen", "Center", "Angriff", "\n",
                                  "Position", "Formation", "Befehle", ""};
void setLedColor(uint8_t r, uint8_t g, uint8_t b)
{
  analogWrite(LED_R, 255 - r);
  analogWrite(LED_G, 255 - g);
  analogWrite(LED_B, 255 - b);
}

// Buzzer helper functions
void beepShort() {
  tone(BUZZER_PIN, 2000, 50);  // 2kHz for 50ms
}

void beepClick() {
  tone(BUZZER_PIN, 3000, 30);  // 3kHz for 30ms (short click)
}

void beepConnect() {
  tone(BUZZER_PIN, 1500, 100);  // 1.5kHz for 100ms
  delay(120);
  tone(BUZZER_PIN, 2000, 100);  // 2kHz for 100ms (rising tone)
}

void beepDisconnect() {
  tone(BUZZER_PIN, 2000, 100);  // 2kHz for 100ms
  delay(120);
  tone(BUZZER_PIN, 1500, 100);  // 1.5kHz for 100ms (falling tone)
}

void beepMotherlode() {
  for (int i = 0; i < 3; i++) {
    tone(BUZZER_PIN, 3000, 100);  // 3kHz urgent beeps
    delay(150);
  }
}

void beepBootup() {
  tone(BUZZER_PIN, 1000, 100);
  delay(120);
  tone(BUZZER_PIN, 1500, 100);
  delay(120);
  tone(BUZZER_PIN, 2000, 100);
}

void setDisplayPower(bool on)
{
  if (on && !displayOn) {
    digitalWrite(27, HIGH); // Turn on backlight
    displayOn = true;
    beepShort();  // Beep when display wakes up
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
            beepClick();  // Click sound for button press
            bleGamepad.press(20);
            delay(50);
            bleGamepad.release(20);
            setLedColor(255, 255, 0);
        } else if (cmd_idx < 8) {
            FighterCommand cmd;
            memcpy_P(&cmd, &commands[cmd_idx], sizeof(FighterCommand));
            
            Serial.printf("Command: %s (Button %d)\n", cmd.name, cmd.button_id);
            beepClick();  // Click sound for button press
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

// Helper function to print heap status
void printHeapStatus(const char* location) {

  //disable it for now, but leave the code here for future debugging
  //return;


  size_t freeHeap = esp_get_free_heap_size();
  size_t minFreeHeap = esp_get_minimum_free_heap_size();
  size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  
  Serial.printf("[HEAP %s] Free: %d bytes, Min: %d bytes, Largest block: %d bytes\n",
    location, freeHeap, minFreeHeap, largestBlock);
}

void updateCargoBar() {
  if (!cargo_bar) return;
  
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    int total = cargoInfo.totalCapacity;
  int used = cargoInfo.usedSpace;
  int drones = cargoInfo.dronesCount;
  int cargo = cargoInfo.cargoCount;
  int free = total - used;
  
  lv_bar_set_range(cargo_bar, 0, total);
  lv_bar_set_value(cargo_bar, used, LV_ANIM_OFF);
  
  // Update label
  lv_obj_t* label = lv_obj_get_child(cargo_bar, 0);
  if (label) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Cargo: %d/%d (Drones: %d)", used, total, drones);
    lv_label_set_text(label, buf);
  }
    
    xSemaphoreGive(lvglMutex);
  }
}

void updateHeader() {
  if (!header_label) return;
  
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    char buf[64];
  snprintf(buf, sizeof(buf), "Jumps: %d", navRouteInfo.jumpsRemaining);
  lv_label_set_text(header_label, buf);
  
  // Update fuel bar
  if (fuel_bar) {
    float fuelPercent = (fuelInfo.fuelCapacity > 0) ? 
      (fuelInfo.fuelMain / fuelInfo.fuelCapacity * 100.0f) : 0.0f;
    lv_bar_set_value(fuel_bar, (int)fuelPercent, LV_ANIM_OFF);
  }
  
  // Update hull bar
  if (hull_bar) {
    lv_bar_set_value(hull_bar, (int)(hullInfo.hullHealth * 100.0f), LV_ANIM_OFF);
  }
  
  // Update WiFi icon color based on signal quality
  if (wifi_icon) {
    if (WiFi.status() == WL_CONNECTED) {
      int rssi = WiFi.RSSI();
      if (rssi > -50) {
        lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x006400), 0);  // darkgreen - Excellent
      } else if (rssi > -60) {
        lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x9ACD32), 0);  // yellowgreen - Good
      } else if (rssi > -70) {
        lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0xFFA500), 0);  // orange - Fair
      } else {
        lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0xFF0000), 0);  // red - Weak
      }
    } else {
      lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x000000), 0);  // black - No connection
    }
  }
  
  // Update WebSocket icon
  if (websocket_icon) {
    if (useWebSocket && wsClient.available()) {
      lv_obj_set_style_text_color(websocket_icon, lv_color_hex(0xFFFFFF), 0);  // white - connected
    } else {
      lv_obj_set_style_text_color(websocket_icon, lv_color_hex(0x000000), 0);  // black - not connected
    }
  }
  
  // Update Bluetooth icon
  if (bluetooth_icon) {
    if (bleGamepad.isConnected()) {
      lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0xFFFFFF), 0);  // white - connected
    } else {
      lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0x000000), 0);  // black - not connected
    }
  }
    
    xSemaphoreGive(lvglMutex);
  }
}

static char* logText = nullptr;  // Allocated on heap
static const int LOG_TEXT_SIZE = 600;  // Increased buffer size for 9 entries

void updateLogDisplay() {
  if (!log_label || !logText) return;  // Check logText is allocated
  
  // Print heap status before display update
  static int updateCount = 0;
  if (updateCount++ % 10 == 0) {  // Every 10th update
    printHeapStatus("before display");
  }
  
  // Take mutex before accessing LVGL objects
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    // Clear buffer safely
    memset(logText, 0, LOG_TEXT_SIZE);
    int currentLen = 0;
  
    // Only show as many entries as we actually have
    int entriesToShow = (eventLogCount < 9) ? eventLogCount : 9;
    
    Serial.printf("[LOG] Updating display: showing %d entries (count=%d, index=%d)\n",
      entriesToShow, eventLogCount, eventLogIndex);
    
    for (int i = 0; i < entriesToShow; i++) {
      // Calculate index going backwards from most recent
      int idx = (eventLogIndex - 1 - i + 9) % 9;
      
      // Validate entry has text
      if (eventLog[idx].text[0] == 0) {
        Serial.printf("  [%d] idx=%d: <empty>\n", i, idx);
        continue;
      }
      
      Serial.printf("  [%d] idx=%d: %s\n", i, idx, eventLog[idx].text);
      
      // Calculate space needed for this entry
      int textLen = strnlen(eventLog[idx].text, sizeof(eventLog[idx].text));
      int countPrefixLen = 0;
      if (eventLog[idx].count > 1) {
        countPrefixLen = snprintf(nullptr, 0, "%dx ", eventLog[idx].count);
      }
      int totalNeeded = countPrefixLen + textLen + 2;  // +2 for \n and \0
      
      // Check if we have enough space
      int remainingSpace = LOG_TEXT_SIZE - currentLen;
      if (totalNeeded > remainingSpace) {
        Serial.printf("[LOG] Buffer full, stopping at entry %d (needed %d, have %d)\n",
          i, totalNeeded, remainingSpace);
        break;
      }
      
      // Add count prefix if entry was repeated
      if (eventLog[idx].count > 1) {
        int written = snprintf(logText + currentLen, remainingSpace, "%dx ", eventLog[idx].count);
        if (written > 0 && written < remainingSpace) {
          currentLen += written;
          remainingSpace -= written;
        } else {
          Serial.println("[LOG] ERROR: Count prefix write failed");
          break;
        }
      }
      
      // Safely copy text
      int copyLen = (textLen < remainingSpace - 2) ? textLen : (remainingSpace - 2);
      if (copyLen > 0) {
        memcpy(logText + currentLen, eventLog[idx].text, copyLen);
        currentLen += copyLen;
        logText[currentLen++] = '\n';
        logText[currentLen] = '\0';
      } else {
        Serial.println("[LOG] ERROR: No space for text copy");
        break;
      }
    }
    
    // Ensure null termination
    logText[LOG_TEXT_SIZE - 1] = '\0';
    
    // Update label - this is where LVGL might run out of memory
    Serial.printf("[LOG] Setting label text (%d bytes)\n", currentLen);
    lv_label_set_text(log_label, logText);
    
    xSemaphoreGive(lvglMutex);
    
    // Print heap after display update
    if (updateCount % 10 == 0) {
      printHeapStatus("after display");
    }
  } else {
    Serial.println("[LOG] Failed to take mutex, skipping display update");
  }
}

void addLogEntry(const char* text) {
  // Check if this is a repeat of the most recent entry
  if (eventLogCount > 0) {
    int lastIdx = (eventLogIndex - 1 + 9) % 9;
    
    // Extract base text without count prefix (e.g., "5x Scan" -> "Scan")
    const char* baseText = text;
    const char* lastBaseText = eventLog[lastIdx].text;
    
    // Skip count prefix if it exists in last entry (e.g., "5x ")
    const char* xPos = strchr(lastBaseText, 'x');
    if (xPos && xPos > lastBaseText && *(xPos + 1) == ' ') {
      lastBaseText = xPos + 2;  // Skip "Nx "
    }
    
    // Compare base text
    if (strcmp(baseText, lastBaseText) == 0) {
      // Same as last entry - increment count
      eventLog[lastIdx].count++;
      eventLog[lastIdx].timestamp = millis();
      
      Serial.printf("[LOG] Consolidated entry #%d: %dx %s\n", 
        lastIdx, eventLog[lastIdx].count, lastBaseText);
      
      updateLogDisplay();
      return;
    }
  }
  
  // New different entry - add normally
  strncpy(eventLog[eventLogIndex].text, text, sizeof(eventLog[eventLogIndex].text) - 1);
  eventLog[eventLogIndex].text[sizeof(eventLog[eventLogIndex].text) - 1] = 0;
  eventLog[eventLogIndex].timestamp = millis();
  eventLog[eventLogIndex].count = 1;
  
  Serial.print("[LOG] Added entry #");
  Serial.print(eventLogIndex);
  Serial.print(": ");
  Serial.println(eventLog[eventLogIndex].text);
  
  eventLogIndex = (eventLogIndex + 1) % 9;
  if (eventLogCount < 9) eventLogCount++;
  updateLogDisplay();
}

void create_logviewer_ui() {
  logviewer_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(logviewer_screen, LV_COLOR_BG, 0);
  
  // Header with jumps, fuel, hull (reduced height to 22px to fit bars)
  lv_obj_t* header = lv_obj_create(logviewer_screen);
  lv_obj_set_size(header, SCREEN_WIDTH, 24);
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
  //lv_obj_set_style_bg_opa(fuel_bar, LV_OPA_COVER, LV_PART_MAIN);
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
  //lv_obj_set_style_bg_opa(hull_bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(hull_bar, LV_COLOR_GAUGE_FG, LV_PART_INDICATOR);
  
  // Status icons (right side of header)
  // WiFi icon (rightmost)
  wifi_icon = lv_label_create(header);
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_set_style_text_color(wifi_icon, lv_color_hex(0x000000), 0);  // Start with black (no connection)
  lv_obj_set_pos(wifi_icon, 300, 2);
  
  // WebSocket icon (middle)
  websocket_icon = lv_label_create(header);
  lv_label_set_text(websocket_icon, LV_SYMBOL_REFRESH);
  lv_obj_set_style_text_color(websocket_icon, lv_color_hex(0x000000), 0);  // Start with black (not connected)
  lv_obj_set_pos(websocket_icon, 280, 2);
  
  // Bluetooth icon (left of websocket)
  bluetooth_icon = lv_label_create(header);
  lv_label_set_text(bluetooth_icon, LV_SYMBOL_BLUETOOTH);
  lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0x000000), 0);  // Start with black (not connected)
  lv_obj_set_pos(bluetooth_icon, 260, 2);
  
  // Log area (adjusted for smaller header)
  lv_obj_t* log_area = lv_obj_create(logviewer_screen);
  lv_obj_set_size(log_area, SCREEN_WIDTH, SCREEN_HEIGHT - 62);
  lv_obj_set_pos(log_area, 0, 25);
  lv_obj_set_style_bg_color(log_area, LV_COLOR_BG, 0);
  lv_obj_set_style_border_width(log_area, 0, 0);
  lv_obj_set_style_radius(log_area, 0, 0);
  lv_obj_set_scrollbar_mode(log_area, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(log_area, LV_DIR_NONE);  // Disable all scrolling
  
  log_label = lv_label_create(log_area);
  lv_label_set_text(log_label, "Waiting for events...");
  lv_obj_set_style_text_color(log_label, LV_COLOR_FG, 0);
  lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(log_label, SCREEN_WIDTH - 10);
  
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

void switchToPage(int page) {
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (page == 0) {
      lv_scr_load(fighter_screen);
    currentPage = 0;
  } else if (page == 1) {
    if (!logviewer_screen) {
      create_logviewer_ui();
    }
    lv_scr_load(logviewer_screen);
    currentPage = 1;
    // Don't call updateLogDisplay here - it will update when events arrive
      updateCargoBar();
      updateHeader();
    }
    
    xSemaphoreGive(lvglMutex);
  }
  // Reset timeout and turn on display on page switch
  lastTouchTime = millis();
  if (!displayOn) {
    setDisplayPower(true);
  }
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

  //Serial.printf("Touch: raw(%d,%d) -> screen(%d,%d)\n", raw_x, raw_y, x, y);

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
  create_logviewer_ui();
  lv_scr_load(logviewer_screen);  // Start on page 2 (logviewer)
}

void checkBleConnection()
{
  if (bleGamepad.isConnected())
  {
    if (!bleConnected)
    {
      Serial.println("[BLE] Connected");
      beepConnect();  // Rising tone for connection
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
      beepDisconnect();  // Falling tone for disconnection
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

// Forward declaration
void requestSummary();

void checkDisplayTimeout()
{
  uint32_t currentTime = millis();
  
  // Turn off display after timeout regardless of BLE connection
  if (displayOn && (currentTime - lastTouchTime) > DISPLAY_TIMEOUT) {
    setDisplayPower(false);
  }
}

void checkWifiConnection() {
  uint32_t currentTime = millis();
  
  // Check WiFi status every 10 seconds
  if (currentTime - lastWifiCheck < WIFI_CHECK_INTERVAL) {
    return;
  }
  
  lastWifiCheck = currentTime;
  
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      Serial.println("[WiFi] Connected!");
      Serial.print("[WiFi] IP: ");
      Serial.println(WiFi.localIP());
      beepConnect();
      wifiWasConnected = true;
      
      // Restart UDP receiver if it wasn't running
      udpReceiver.stop();
      if (udpReceiver.begin(UDP_PORT)) {
        Serial.print("[WiFi] UDP receiver listening on port: ");
        Serial.println(UDP_PORT);
      }
      
      // Request summary after UDP is ready
      delay(500);
      requestSummary();
    }
  } else {
    if (wifiWasConnected) {
      Serial.println("[WiFi] Connection lost, attempting reconnect...");
      wifiWasConnected = false;
    }
    
    // Attempt non-blocking reconnect
    if (WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_CONNECTION_LOST) {
      Serial.println("[WiFi] Reconnecting...");
      WiFi.reconnect();
    }
  }
}

// Helper function to replace umlauts with double vowels
void replaceUmlauts(char* str) {
  String temp = String(str);
  temp.replace("ä", "ae");
  temp.replace("ö", "oe");
  temp.replace("ü", "ue");
  temp.replace("Ä", "Ae");
  temp.replace("Ö", "Oe");
  temp.replace("Ü", "Ue");
  temp.replace("ß", "ss");
  strncpy(str, temp.c_str(), 59);
  str[59] = 0;
}

// Forward declaration
void handleEliteEvent(const String& eventType, JsonDocument& doc);

// WebSocket event handlers
void onWebSocketMessage(WebsocketsMessage message) {
  // Check message size before processing to avoid out-of-memory on large payloads
  printHeapStatus("WS start");
  size_t len = message.length();
  Serial.printf("[WS] Message length: %d bytes\n", len);
  const size_t MAX_WS_MESSAGE_SIZE = 8000; // 8KB limit

  if (len > MAX_WS_MESSAGE_SIZE) {
    Serial.printf("[WS] ERROR: Message too large (%d bytes), discarding.\n", len);
    // We must still read the message to clear the buffer, but we won't process it.
    // Reading it as a string is the easiest way, even if it temporarily allocates.
    // The crash happens during JSON parsing, not just allocation of a large string.
    String temp = message.data(); 
    return;
  }

  String data = message.data();
  
  Serial.printf("[WS] Received %d bytes\n", data.length());
  
  // Parse WebSocket JSON message format: {"type": "TYPE", "id": 123, "data": {...}}
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data);
  
  if (error) {
    Serial.printf("[WS] JSON parse error: %s\n", error.c_str());
    printHeapStatus("WS error");
    return;
  }
  
  // Extract message fields
  const char* type = doc["type"];
  int id = doc["id"] | 0;
  JsonObject dataObj = doc["data"];
  
  if (!type || dataObj.isNull()) {
    Serial.println("[WS] Invalid message format");
    doc.clear();
    return;
  }
  
  String eventType = String(type);
  Serial.printf("[WS] Processing %s event ID: %d\n", eventType.c_str(), id);
  
  // Reset display timeout on message
  lastTouchTime = millis();
  if (!displayOn) {
    setDisplayPower(true);
  }
  
  // Process event - reuse the doc to avoid extra allocations
  JsonDocument eventDoc;
  eventDoc.set(dataObj);
  
  printHeapStatus("before handler");
  handleEliteEvent(eventType, eventDoc);
  printHeapStatus("after handler");
  
  // Clear docs to free memory immediately
  eventDoc.clear();
  doc.clear();
  
  printHeapStatus("WS end");
}

void onWebSocketEvent(WebsocketsEvent event, String data) {
  switch(event) {
    case WebsocketsEvent::ConnectionOpened:
      Serial.println("[WS] Connected to server");
      useWebSocket = true;
      // Request initial summary
      wsClient.send("{\"command\":\"SUMMARY\"}");
      break;
      
    case WebsocketsEvent::ConnectionClosed:
      Serial.println("[WS] Disconnected from server");
      useWebSocket = false;
      break;
      
    case WebsocketsEvent::GotPing:
      Serial.println("[WS] Got ping");
      break;
      
    case WebsocketsEvent::GotPong:
      Serial.println("[WS] Got pong");
      break;
  }
}

void connectWebSocket() {
  if (serverIP.length() > 0 && websocketPort > 0) {
    String wsUrl = "ws://" + serverIP + ":" + String(websocketPort);
    
    Serial.printf("[WS] Connecting to %s\n", wsUrl.c_str());
    
    wsClient.onMessage(onWebSocketMessage);
    wsClient.onEvent(onWebSocketEvent);
    
    if (wsClient.connect(wsUrl)) {
      Serial.println("[WS] Connection initiated");
    } else {
      Serial.println("[WS] Connection failed");
    }
  }
}

void handleEliteEvent(const String& eventType, JsonDocument& doc) {
  static char logBuf[80];
  static char tempBuf[60];
  
  if (eventType == "KEEPALIVE") {
    // Parse KEEPALIVE packet: KEEPALIVE|0|{"ip": "x.x.x.x", "websocket_port": 12347, "udp_control_port": 12346, "current_message_id": 1}
    const char* ip = doc["ip"];
    int ws_port = doc["websocket_port"] | 0;
    int udp_ctrl_port = doc["udp_control_port"] | 0;
    
    if (ip && ws_port > 0) {
      // Check if server changed
      bool serverChanged = (serverIP != String(ip) || websocketPort != ws_port);
      
      serverIP = String(ip);
      websocketPort = ws_port;
      udpControlPort = udp_ctrl_port;
      
      Serial.printf("[KEEPALIVE] WebSocket server: %s:%d, UDP Control: %d\n", 
        serverIP.c_str(), websocketPort, udpControlPort);
      
      // If server changed or not connected, reconnect WebSocket
      if (serverChanged || !wsClient.available()) {
        if (wsClient.available()) {
          Serial.println("[WS] Server changed, reconnecting...");
          wsClient.close();
        }
      }
    }
    // Do not show KEEPALIVE in display
    return;
  } else if (eventType == "SUMMARY") {
    // Parse comprehensive summary data
    Serial.println("[SUMMARY] Received state update");
    summaryReceived = true;  // Mark that we've received first summary
    
    // Update fuel (now has percent, current, and capacity)
    if (!doc["fuel"].isNull()) {
      if (doc["fuel"]["current"].is<float>()) {
        fuelInfo.fuelMain = doc["fuel"]["current"];
      }
      if (doc["fuel"]["capacity"].is<float>()) {
        fuelInfo.fuelCapacity = doc["fuel"]["capacity"];
      }
    }
    
    // Update cargo
    if (!doc["cargo"].isNull()) {
      cargoInfo.totalCapacity = doc["cargo"]["capacity"];
      cargoInfo.usedSpace = doc["cargo"]["count"].as<int>();
      cargoInfo.dronesCount = doc["cargo"]["drones"].as<int>();
      // Calculate cargo without drones
      cargoInfo.cargoCount = cargoInfo.usedSpace - cargoInfo.dronesCount;
    }
    
    // Update hull
    if (!doc["hull"].isNull()) {
      hullInfo.hullHealth = doc["hull"].as<float>() / 100.0f;
    }
    
    // Update shields
    if (!doc["shields"].isNull()) {
      // shields is a percentage (0-100)
      float shieldsPercent = doc["shields"].as<float>();
      Serial.printf("[SHIELDS] %.1f%%\n", shieldsPercent);
    }
    
    // Update route jumps (now a single integer, not an object)
    if (!doc["route"].isNull()) {
      navRouteInfo.jumpsRemaining = doc["route"].as<int>();
    }
    
    // Update balance
    if (!doc["balance"].is<nullptr_t>()) {
      long balance = doc["balance"];
      Serial.printf("[BALANCE] Credits: %ld\n", balance);
    }
    
    // Update legal state
    if (doc["legal_state"].is<const char*>()) {
      const char* legalState = doc["legal_state"];
      Serial.printf("[LEGAL] State: %s\n", legalState);
    }
    
    // Update location
    if (!doc["location"].isNull()) {
      const char* system = doc["location"]["system"];
      const char* station = doc["location"]["station"];
      if (system) {
        Serial.printf("[LOCATION] System: %s", system);
        if (station) {
          Serial.printf(", Station: %s\n", station);
        } else {
          Serial.println();
        }
      }
    }
    
    // Update destination
    if (!doc["destination"].isNull() && !doc["destination"].is<nullptr_t>()) {
      const char* destName = doc["destination"]["Name"];
      if (destName) {
        Serial.printf("[DESTINATION] %s\n", destName);
      }
    }
    
    // Update all displays
    updateHeader();
    updateCargoBar();
    
    Serial.printf("[SUMMARY] Fuel: %.2f/%.2f (%.1f%%), Cargo: %d/%d (Drones: %d), Hull: %.1f%%, Jumps: %d\n",
      fuelInfo.fuelMain, fuelInfo.fuelCapacity,
      (fuelInfo.fuelCapacity > 0) ? (fuelInfo.fuelMain / fuelInfo.fuelCapacity * 100.0f) : 0.0f,
      cargoInfo.usedSpace, cargoInfo.totalCapacity,
      cargoInfo.dronesCount,
      hullInfo.hullHealth * 100.0f,
      navRouteInfo.jumpsRemaining);
  } else if (eventType == "JOURNAL") {
    String event = doc["event"].as<String>();
    
    // Check if event should be ignored (read from PROGMEM)
    char ignoreBuf[32];
    for (int i = 0; i < ignoreJournalEventsCount; i++) {
      strcpy_P(ignoreBuf, (char*)pgm_read_ptr(&ignoreJournalEvents[i]));
      if (event == ignoreBuf) {
        Serial.print("[JOURNAL] Ignored event: ");
        Serial.println(event);
        return;
      }
    }
    
    // Check if event should be hidden from display but still processed
    bool hideFromDisplay = false;
    for (int i = 0; i < hideJournalEventsCount; i++) {
      strcpy_P(ignoreBuf, (char*)pgm_read_ptr(&hideJournalEvents[i]));
      if (event == ignoreBuf) {
        hideFromDisplay = true;
        Serial.print("[JOURNAL] Hidden event (processing): ");
        Serial.println(event);
        break;
      }
    }
    
    if (event == "CommunityGoal") {
      // Show player's percentile band for community goals
      if (!doc["CurrentGoals"].isNull()) {
        JsonArray goals = doc["CurrentGoals"];
        for (JsonObject goal : goals) {
          if (!goal["PlayerPercentileBand"].isNull()) {
            int percentile = goal["PlayerPercentileBand"];
            snprintf(logBuf, sizeof(logBuf), "CommunityGoal: Top %d%%", percentile);
            addLogEntry(logBuf);
            Serial.printf("[CG] Player percentile: Top %d%%\n", percentile);
          }
        }
      }
    } else if (event == "FSDTarget") {
      // Update remaining jumps from FSD target
      if (!doc["RemainingJumpsInRoute"].isNull()) {
        navRouteInfo.jumpsRemaining = doc["RemainingJumpsInRoute"];
        updateHeader();
        Serial.printf("[NAV] Jumps remaining: %d\n", navRouteInfo.jumpsRemaining);
      }
    } else if (event == "HullDamage") {
      // Update hull health from damage event
      if (!doc["Health"].isNull()) {
        hullInfo.hullHealth = doc["Health"];
        updateHeader();
        Serial.printf("[HULL] Health: %.1f%%\n", hullInfo.hullHealth * 100.0f);
      }
    } else if (event == "ProspectedAsteroid") {
      // Check for motherlode material
      if (doc["MotherlodeMaterial"].is<const char*>()) {
        const char* material = doc["MotherlodeMaterial"];
        strncpy(motherlodeMaterial, material, sizeof(motherlodeMaterial) - 1);
        
        const char* localised = doc["MotherlodeMaterial_Localised"].is<const char*>() ? 
          doc["MotherlodeMaterial_Localised"].as<const char*>() : material;
        
        strncpy(tempBuf, localised, sizeof(tempBuf) - 1);
        replaceUmlauts(tempBuf);
        snprintf(logBuf, sizeof(logBuf), "*** %s ***", tempBuf);
        addLogEntry(logBuf);
        
        // Trigger screen blink and urgent beeps
        blinkScreen = true;
        blinkCount = 6;  // 3 blinks = 6 toggles
        lastBlinkTime = millis();
        beepMotherlode();  // Urgent triple beep
        Serial.printf("MOTHERLODE: %s\n", tempBuf);
      } else {
        // Regular asteroid - format: "Asteroid: Quality\nMat1, Mat2, Mat3"
        const char* content = doc["Content_Localised"].is<const char*>() ? 
          doc["Content_Localised"].as<const char*>() : "";
        
        // Extract quality (remove "Materialgehalt: " prefix if present)
        String contentStr = String(content);
        int colonPos = contentStr.indexOf(':');
        String quality = (colonPos > 0) ? contentStr.substring(colonPos + 2) : contentStr;
        
        // Build materials list
        String materials = "";
        if (!doc["Materials"].isNull()) {
          JsonArray mats = doc["Materials"];
          int count = 0;
          for (JsonObject mat : mats) {
            if (count > 0) materials += ", ";
            const char* matName = mat["Name_Localised"].is<const char*>() ? 
              mat["Name_Localised"].as<const char*>() : mat["Name"].as<const char*>();
            strncpy(tempBuf, matName, sizeof(tempBuf) - 1);
            replaceUmlauts(tempBuf);
            materials += String(tempBuf);
            count++;
            if (count >= 3) break;  // Limit to 3 materials
          }
        }
        
        snprintf(logBuf, sizeof(logBuf), "Asteroid: %s", quality.c_str());
        addLogEntry(logBuf);
        if (materials.length() > 0) {
          snprintf(logBuf, sizeof(logBuf), "%s", materials.c_str());
          addLogEntry(logBuf);
        }
      }
    } else {
      // Generic journal event - only add to display if not hidden
      if (!hideFromDisplay) {
        addLogEntry(event.c_str());
      }
    }
  } else if (eventType == "STATUS") {
    // Update fuel from STATUS (has FuelMain and FuelReservoir)
    if (!doc["Fuel"].isNull()) {
      if (doc["Fuel"]["FuelMain"].is<float>()) {
        fuelInfo.fuelMain = doc["Fuel"]["FuelMain"];
      }
      // Note: FuelReservoir is the small reserve tank, we track main tank
    }
    
    // Update cargo count from STATUS event (it's an absolute count)
    if (!doc["Cargo"].isNull()) {
      cargoInfo.usedSpace = doc["Cargo"].as<int>();
      Serial.printf("[CARGO] Updated from STATUS: %d/%d (%.1f%%)\n", 
        cargoInfo.usedSpace, cargoInfo.totalCapacity,
        (float)cargoInfo.usedSpace / cargoInfo.totalCapacity * 100.0f);
    }
    
    // Update balance
    if (!doc["Balance"].is<nullptr_t>()) {
      long balance = doc["Balance"];
      // Balance updated, but not displayed currently
    }
    
    // Update legal state
    if (doc["LegalState"].is<const char*>()) {
      const char* legalState = doc["LegalState"];
      // Legal state updated (e.g., "Clean", "Wanted", "Speeding")
    }
    
    // Update destination from STATUS (same format as SUMMARY)
    if (!doc["Destination"].isNull() && !doc["Destination"].is<nullptr_t>()) {
      const char* destName = doc["Destination"]["Name"];
      if (destName) {
        Serial.printf("[STATUS] Destination: %s\n", destName);
      }
    }
    
    updateHeader();
    updateCargoBar();
  } else if (eventType == "CARGO") {
    // Parse cargo inventory
    cargoInfo.usedSpace = doc["Count"];
    cargoInfo.dronesCount = 0;
    cargoInfo.cargoCount = 0;
    
    if (!doc["Inventory"].isNull()) {
      JsonArray inventory = doc["Inventory"];
      for (JsonObject item : inventory) {
        String name = item["Name"].as<String>();
        int count = item["Count"];
        
        if (name == "drones") {
          cargoInfo.dronesCount = count;
        } else {
          cargoInfo.cargoCount += count;
        }
      }
    }
    
    updateCargoBar();
  } else if (eventType == "NAVROUTE") {
    // Count route entries
    if (!doc["Route"].isNull()) {
      JsonArray route = doc["Route"];
      navRouteInfo.jumpsRemaining = route.size();
    }
    updateHeader();
  } else if (eventType == "MISSIONSTATUS") {
    // Display missions in current system
    if (!doc["MissionsInSystem"].isNull()) {
      int missionsInSystem = doc["MissionsInSystem"].as<int>();
      snprintf(logBuf, sizeof(logBuf), "Mission in System: %d", missionsInSystem);
      addLogEntry(logBuf);
      Serial.printf("[MISSION] Missions in system: %d\n", missionsInSystem);
    }
  } else {
    Serial.print("[UDP] Unhandled event type: ");
    Serial.println(eventType);
  }
}

void checkMessages() {
  // Check WebSocket connection and handle messages
  if (serverIP.length() > 0 && websocketPort > 0) {
    // Try to connect if not connected
    if (!wsClient.available()) {
      static uint32_t lastConnectAttempt = 0;
      uint32_t currentTime = millis();
      
      if (currentTime - lastConnectAttempt > 5000) {  // Try every 5 seconds
        connectWebSocket();
        lastConnectAttempt = currentTime;
      }
    }
    
    // Poll WebSocket for incoming messages
    if (wsClient.available()) {
      wsClient.poll();
      // Skip UDP when WebSocket is active
      return;
    }
  }
  // Check for UDP messages (only when TCP is not enabled)
  int packetSize = udpReceiver.parsePacket();
  
  if (packetSize > 0) {
    Serial.printf("[UDP] Detected packet: %d bytes\n", packetSize);
    
    if (packetSize >= UDP_BUFFER_SIZE) {
      Serial.printf("[UDP] ERROR: Packet too large (%d bytes), buffer is %d bytes - DISCARDING\n", 
        packetSize, UDP_BUFFER_SIZE);
      udpReceiver.flush();  // Clear the buffer
      return;
    }
    
    int len = udpReceiver.read(udpBuffer, UDP_BUFFER_SIZE - 1);
    if (len > 0) {
      udpBuffer[len] = 0;
      
      Serial.print("[UDP Core ");
      Serial.print(xPortGetCoreID());
      Serial.print("] Received ");
      Serial.print(len);
      Serial.print(" bytes: ");
      
      // For large packets, only print first 100 chars
      if (len > 100) {
        char preview[101];
        strncpy(preview, udpBuffer, 100);
        preview[100] = 0;
        Serial.print(preview);
        Serial.println("... (truncated)");
      } else {
        Serial.println(udpBuffer);
      }
      
      // Reset timeout and turn on display on message received
      lastTouchTime = millis();
      if (!displayOn) {
        setDisplayPower(true);
      }
      
      String message = String(udpBuffer);
      
      // Parse format: EVENTTYPE|ID|{json}
      int firstSep = message.indexOf('|');
      if (firstSep > 0) {
        String eventType = message.substring(0, firstSep);
        
        int secondSep = message.indexOf('|', firstSep + 1);
        if (secondSep > 0) {
          String eventIdStr = message.substring(firstSep + 1, secondSep);
          String jsonData = message.substring(secondSep + 1);
          
          int eventId = eventIdStr.toInt();
          
          // Deduplicate: skip if we've already seen this ID (except KEEPALIVE)
          if (eventId == lastEventId && eventType != "KEEPALIVE") {
            Serial.printf("[UDP] Duplicate %s ID %d - skipping\n", eventType.c_str(), eventId);
            return;
          }
          
          if (eventType != "KEEPALIVE") {
            lastEventId = eventId;
          }
          Serial.printf("[UDP] Processing %s event ID: %d\n", eventType.c_str(), eventId);
          
          // Parse JSON with reduced buffer
          JsonDocument doc;
          DeserializationError error = deserializeJson(doc, jsonData);
          
          if (!error) {
            handleEliteEvent(eventType, doc);
          } else {
            Serial.print("JSON Parse Error: ");
            Serial.println(error.c_str());
          }
        } else {
          Serial.println("[UDP] ERROR: No second separator found");
        }
      } else {
        Serial.println("[UDP] ERROR: No first separator found");
      }
    }
  }
}

// Message handling task running on Core 0 for non-blocking UDP and WebSocket reception
void loop2(void* parameter) {
  Serial.print("[MSG] Task started on core: ");
  Serial.println(xPortGetCoreID());
  
  while (1) {
    // Check messages (WebSocket polling or UDP)
    checkMessages();
    
    // Poll frequently for WebSocket or UDP
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms polling
  }
}

void requestSummary() {
  if (WiFi.status() == WL_CONNECTED) {
    IPAddress broadcastIP = WiFi.localIP();
    broadcastIP[3] = 255;  // Change last octet to 255 for broadcast
    
    Serial.println("[SUMMARY] Requesting summary data...");
    
    // Send to broadcast address using sender socket
    udpSender.beginPacket(broadcastIP, UDP_PORT + 1);
    udpSender.write((const uint8_t*)"SUMMARY", 7);
    udpSender.endPacket();
    
    Serial.printf("[SUMMARY] Sent request to %s:%d\n", 
      broadcastIP.toString().c_str(), UDP_PORT + 1);
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
    wifiWasConnected = true;
    beepConnect();  // Rising tone for WiFi connection
    
    // Start UDP receiver (Core 0 will use this)
    if (udpReceiver.begin(UDP_PORT)) {
      Serial.print("UDP receiver listening on port: ");
      Serial.println(UDP_PORT);
    } else {
      Serial.println("ERROR: Failed to start UDP receiver!");
    }
    
    // Initialize UDP sender (Core 1 will use this)
    Serial.println("UDP sender initialized for outgoing packets");
    
    // Request initial summary data
    delay(500);  // Give UDP stack time to initialize
    requestSummary();
  } else {
    Serial.println("\nWiFi connection failed, continuing without WiFi");
  }
  
  setLedColor(255, 0, 0); // Red = waiting for BLE
  
}

void setup()
{
  Serial.begin(115200);
  udpBuffer = (char*)malloc(UDP_BUFFER_SIZE);  // Allocates from HEAP
  logText = (char*)malloc(LOG_TEXT_SIZE);  // Allocates from HEAP
  if (!udpBuffer || !logText) {
    Serial.println("ERROR: Failed to allocate buffers!");
    while(1);
  }
  delay(500);
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  
  beepBootup();  // Play three startup tones
  delay(200);
  
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
  
  // Create mutex for LVGL thread safety
  lvglMutex = xSemaphoreCreateMutex();
  if (!lvglMutex) {
    Serial.println("[ERROR] Failed to create LVGL mutex!");
    while(1);
  }
  
  // Start message handler task on Core 0 (UDP + WebSocket)
  xTaskCreatePinnedToCore(
    loop2,              // Task function
    "MSG_Handler",      // Task name
    16384,              // Stack size (16KB - increased for WebSocket + JSON parsing)
    NULL,               // Parameters
    2,                  // Priority (2 = higher than default)
    &udpTaskHandle,     // Task handle
    0                   // Core 0 (PRO_CPU)
  );
  
  Serial.print("[MAIN] Loop running on core: ");
  Serial.println(xPortGetCoreID());
  Serial.println("BLE Joypad with Fighter Commands ready!");
}

void loop()
{
  static uint32_t lastTick = 0;
  
  //uint32_t currentTime = millis();
  //lv_tick_inc(currentTime - lastTick);
  //lastTick = currentTime;
  
  // Protect LVGL rendering with mutex
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    lv_timer_handler();
    xSemaphoreGive(lvglMutex);
  }
  checkBleConnection();
  checkDisplayTimeout();
  checkWifiConnection();
  // checkMessages();  // Now handled by loop2 on Core 0
  
  uint32_t currentTime = millis();
  
  // Print heap status periodically
  if (currentTime - lastHeapPrint > HEAP_PRINT_INTERVAL) {
    printHeapStatus("main loop");
    
    // Print task stack high water marks
    UBaseType_t stackHighWater = uxTaskGetStackHighWaterMark(udpTaskHandle);
    Serial.printf("[STACK] MSG_Handler high water mark: %d bytes free\n", stackHighWater * 4);
    
    lastHeapPrint = currentTime;
  }
  
  // Request SUMMARY every 10 seconds until first one is received
  if (!summaryReceived && WiFi.status() == WL_CONNECTED && 
      (currentTime - lastSummaryRequest) > SUMMARY_REQUEST_INTERVAL) {
    requestSummary();
    lastSummaryRequest = currentTime;
  }
  
  // Print WiFi signal quality every 30 seconds
  if (WiFi.status() == WL_CONNECTED && (currentTime - lastWifiStatusPrint) > WIFI_STATUS_PRINT_INTERVAL) {
    int rssi = WiFi.RSSI();
    Serial.printf("[WIFI STATUS] Signal: %d dBm", rssi);
    updateHeader();  // Update WiFi icon color
    
    // Add quality interpretation
    if (rssi > -50) {
      Serial.println(" (Excellent)");
    } else if (rssi > -60) {
      Serial.println(" (Good)");
    } else if (rssi > -70) {
      Serial.println(" (Fair)");
    } else {
      Serial.println(" (Weak)");
    }
    
    lastWifiStatusPrint = currentTime;
  }
  
  // Handle screen blinking for motherlode detection
  if (blinkScreen && blinkCount > 0) {
    if (millis() - lastBlinkTime > 200) {  // Blink every 200ms
      if (displayOn) {
        digitalWrite(27, LOW);
        displayOn = false;
      } else {
        digitalWrite(27, HIGH);
        displayOn = true;
      }
      blinkCount--;
      lastBlinkTime = millis();
      
      if (blinkCount == 0) {
        blinkScreen = false;
        digitalWrite(27, HIGH);
        displayOn = true;
      }
    }
  }
  
  // Read PCF8575 state
  uint16_t pcfState = pcf.read16();
  bool anyPressed = false;
  
  // Check SILVER buttons for page switching (first 3 buttons) - always available
  static bool silverLeftWasPressed = false;
  static bool silverMidWasPressed = false;
  static bool silverRightWasPressed = false;
  
  bool silverLeftPressed = !(pcfState & (1 << SILVER_LEFT));
  bool silverMidPressed = !(pcfState & (1 << SILVER_MID));
  bool silverRightPressed = !(pcfState & (1 << SILVER_RIGHT));
  
  // Handle SILVER_LEFT - switch to page 0 (Fighter)
  if (silverLeftPressed && !silverLeftWasPressed) {
    switchToPage(0);
    Serial.println("Page: Fighter (0)");
  }
  silverLeftWasPressed = silverLeftPressed;
  
  // Handle SILVER_MID - switch to page 1 (Log Viewer)
  if (silverMidPressed && !silverMidWasPressed) {
    switchToPage(1);
    Serial.println("Page: Log Viewer (1)");
  }
  silverMidWasPressed = silverMidPressed;
  
  // Handle SILVER_RIGHT - reserved for page 2 (future)
  if (silverRightPressed && !silverRightWasPressed) {
    // Reserved for third page
    Serial.println("Page: Reserved (2)");
  }
  silverRightWasPressed = silverRightPressed;
  
  // Handle remaining buttons (indices 3-11) for gamepad - only when BLE connected
  if (bleConnected) {
    for (uint8_t i = 3; i < 12; i++) {
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