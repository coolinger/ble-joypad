
#include <Arduino.h>
#include <Wire.h>
#include <PCF8575.h>
#include <BleGamepad.h>
#include "display.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <esp_heap_caps.h>
#include "es8311.h"
#include "sound.h"
#include "config.h"
#include "gamedata.h"
#include "screens/fighter.h"
#include "screens/info.h"
#include "screens/system.h"
#ifdef ESP_IDF_VERSION
#if (ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0))
    #define USE_NEW_I2S_API 1
    #include <ESP_I2S.h>
#else
    #define USE_NEW_I2S_API 0
    #include "driver/i2s.h"
  #endif
#else
  #define USE_NEW_I2S_API 0
#endif


using namespace websockets;



#define PCF8575_ADDR 0x20
#define I2C_SDA 16
#define I2C_SCL 15
#define BUZZER_PIN 5
uint32_t* buf = nullptr;  // Will be allocated from PSRAM
#define INT_N_PIN 17
#define RST_N_PIN 18

#define I2S_MCK 4
#define I2S_BCK 5
#define I2S_DINT 6
#define I2S_DOUT 8
#define I2S_WS 7
#define I2C_SPEED 400000

// Audio clocking constants now in sound.h

#include "colors.h"

// Global flags

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
PCF8575* pcf = nullptr;
BleGamepad* bleGamepad = nullptr;
BleGamepadConfiguration bleGamepadConfig;
uint16_t lastButtonState = 0;
bool bleConnected = false;

// Display and LVGL objects
//TFT_eSPI tft = TFT_eSPI();
//static lv_display_t* disp;
Display disp;

#include "config.h"
#include "gamedata.h"

WiFiMulti wifiMulti;

// WebSocket variables for Elite Dangerous data
// If not discovered via KEEPALIVE, fallback to configured server IP
String serverIP = DEFAULT_SERVER_IP;
int websocketPort = DEFAULT_WEBSOCKET_PORT;  // Icarus terminal WS port
bool useWebSocket = false;
WebsocketsClient wsClient;

// Task handle for Core 0
TaskHandle_t msgTaskHandle = NULL;
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
const char ignoreEvent_FSSSignalDiscovered[] PROGMEM = "FSSSignalDiscovered";

const char* const ignoreJournalEvents[] PROGMEM = {
  ignoreEvent_Music,
  ignoreEvent_Fileheader,
  ignoreEvent_Commander,
  ignoreEvent_LoadGame,
  ignoreEvent_Loadout,
  ignoreEvent_Materials,
  ignoreEvent_MaterialCollected,
  ignoreEvent_ShipLocker,
  ignoreEvent_Missions,
  ignoreEvent_FSSSignalDiscovered
};
const int ignoreJournalEventsCount = sizeof(ignoreJournalEvents) / sizeof(ignoreJournalEvents[0]);

// List of journal events to hide from display but still process
const char hideEvent_Cargo[] PROGMEM = "Cargo";

const char* const hideJournalEvents[] PROGMEM = {
  hideEvent_Cargo
};
const int hideJournalEventsCount = sizeof(hideJournalEvents) / sizeof(hideJournalEvents[0]);

int currentPage = 1;  // 0 = fighter, 1 = logviewer, 2 = settings (start on page 1)
bool pendingJumpOverlay = false;
int pendingJumpValue = 0;

// Forward declarations for animation callbacks
static void jump_overlay_zoom_exec(void* obj, int32_t value);
static void jump_overlay_opa_exec(void* obj, int32_t value);
static void jump_overlay_anim_ready(lv_anim_t* anim);

// Display power management
bool displayOn = true;
uint32_t lastTouchTime = 0;
uint32_t lastBleActiveTime = 0;
uint32_t bleDisconnectedTime = 0;
const uint32_t DISPLAY_TIMEOUT = 5 * 60 * 1000; // 5 minutes in milliseconds
const uint32_t LED_TIMEOUT = 5 * 60 * 1000; // 5 minutes in milliseconds
bool ledOn = true;

// Forward declaration for display power control
void setDisplayPower(bool on);

static void handleTouchActivity()
{
  lastTouchTime = millis();
  if (!displayOn) {
    setDisplayPower(true);
  }
}

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

void setDisplayPower(bool on)
{
  if (on && !displayOn) {
    digitalWrite(45, HIGH); // Turn on backlight
    displayOn = true;
    beepShort();  // Beep when display wakes up
    Serial.println("Display ON");
  } else if (!on && displayOn) {
    digitalWrite(45, LOW); // Turn off backlight
    displayOn = false;
    Serial.println("Display OFF");
  }
}

// Helper function to print heap status including PSRAM
void printHeapStatus(const char* location) {

  //disable it for now, but leave the code here for future debugging
  return;


  size_t freeHeap = esp_get_free_heap_size();
  size_t minFreeHeap = esp_get_minimum_free_heap_size();
  size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t totalPSRAM = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  
  Serial.printf("[HEAP %s] Free: %d bytes, Min: %d bytes, Largest block: %d bytes\n",
    location, freeHeap, minFreeHeap, largestBlock);
  Serial.printf("[PSRAM %s] Free: %d / %d bytes (%.1f%%), Internal RAM: %d bytes\n",
    location, freePSRAM, totalPSRAM, (freePSRAM * 100.0 / totalPSRAM), freeInternal);
}

void updateCargoBar() {
  if (!cargo_bar) return;
  
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    int total = status.cargo.totalCapacity;
  int used = status.cargo.usedSpace;
  int drones = status.cargo.dronesCount;
  int cargo = status.cargo.cargoCount;
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

void updateBackpackDisplay() {
  if (!medpack_label || !energycell_label) return;
  
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    char buf[8];
    
    // Update medpack count
    snprintf(buf, sizeof(buf), "%d", status.backpack.healthpack);
    lv_label_set_text(medpack_label, buf);
    
    // Update energycell count
    snprintf(buf, sizeof(buf), "%d", status.backpack.energycell);
    lv_label_set_text(energycell_label, buf);
    
    // Update bioscan count if available
    if (bioscan_label) {
      snprintf(buf, sizeof(buf), "%d", status.bioscan.totalScans);
      lv_label_set_text(bioscan_label, buf);
      
      if (status.bioscan.totalScans > 0) {
        lv_obj_clear_flag(bioscan_label, LV_OBJ_FLAG_HIDDEN);
        if (bioscan_data_label) {
          lv_obj_clear_flag(bioscan_data_label, LV_OBJ_FLAG_HIDDEN);
        }
      } else {
        lv_obj_add_flag(bioscan_label, LV_OBJ_FLAG_HIDDEN);
        if (bioscan_data_label) {
          lv_obj_add_flag(bioscan_data_label, LV_OBJ_FLAG_HIDDEN);
        }
      }
    }
    
    xSemaphoreGive(lvglMutex);
  }
}
void update_wifi_icon() {
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
}
void update_bluetooth_icon() {
    // Update Bluetooth icon
  if (bluetooth_icon) {
    if (bleGamepad && bleGamepad->isConnected()) {
      lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0xFFFFFF), 0);  // white - connected
    } else {
      lv_obj_set_style_text_color(bluetooth_icon, lv_color_hex(0x000000), 0);  // black - not connected
    }
  }
}

void updateHeader() {
  if (!header_label) return;
  
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    char buf[64];
  snprintf(buf, sizeof(buf), "Jumps: %d", status.nav.jumpsRemaining);
  lv_label_set_text(header_label, buf);
  
  // Update fuel bar
  if (fuel_bar) {
    float fuelPercent = (fuelInfo.fuelCapacity > 0) ? 
      (status.fuel.fuelMain / status.fuel.fuelCapacity * 100.0f) : 0.0f;
    lv_bar_set_value(fuel_bar, (int)fuelPercent, LV_ANIM_OFF);
  }
  
  // Update hull bar
  if (hull_bar) {
    lv_bar_set_value(hull_bar, (int)(status.hull.hullHealth * 100.0f), LV_ANIM_OFF);
  }
  
  update_wifi_icon();  
  // Update WebSocket icon
  if (websocket_icon) {
    if (useWebSocket && wsClient.available()) {
      lv_obj_set_style_text_color(websocket_icon, lv_color_hex(0xFFFFFF), 0);  // white - connected
    } else {
      lv_obj_set_style_text_color(websocket_icon, lv_color_hex(0x000000), 0);  // black - not connected
    }
  }
  update_bluetooth_icon();
  
    
    xSemaphoreGive(lvglMutex);
  }
}

void updateStatusLine() {
  if (!status_label) return;

  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    const char* system = status.currentSystem.length() ? status.currentSystem.c_str() : "--";
    const char* station = status.currentStation.length() ? status.currentStation.c_str() : "--";
    const char* dest = status.destinationName.length() ? status.destinationName.c_str() : "--";
    const char* legal = status.legalState.length() ? status.legalState.c_str() : "--";

    const char* mode = "--";
    if (status.onFoot) {
      mode = "On Foot";
    } else if (status.inSrv) {
      mode = "SRV";
    } else if (status.inTaxi) {
      mode = "Taxi";
    } else if (status.inShip) {
      mode = "In Ship";
    }

    char buf[192];
    snprintf(buf, sizeof(buf),
      "System: %s | Station: %s | Dest: %s | Legal: %s | Shields: %.0f%% | Mode: %s",
      system, station, dest, legal, status.shieldsPercent, mode);
    lv_label_set_text(status_label, buf);
    xSemaphoreGive(lvglMutex);
  }
}

static char* logText = nullptr;  // Allocated on heap
static const int LOG_TEXT_SIZE = 2048;  // Large buffer with PSRAM for more log entries

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


void updateSystemInfo() {
  if (!sys_info_label) return;
  
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    size_t freeHeap = esp_get_free_heap_size();
    size_t minFreeHeap = esp_get_minimum_free_heap_size();
    size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    size_t freePSRAM = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t totalPSRAM = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    
    // Get CPU utilization (approximate via task runtime)
    UBaseType_t stackHighWater = uxTaskGetStackHighWaterMark(msgTaskHandle);
    
    // Get WiFi info
    String wifiStatus = "Disconnected";
    String wifiIP = "N/A";
    int rssi = 0;
    if (WiFi.status() == WL_CONNECTED) {
      wifiStatus = "Connected";
      wifiIP = WiFi.localIP().toString();
      rssi = WiFi.RSSI();
    }
    
    // Get WebSocket status
    String wsStatus = useWebSocket && wsClient.available() ? "Connected" : "Disconnected";

    const char* system = status.currentSystem.length() ? status.currentSystem.c_str() : "--";
    const char* station = status.currentStation.length() ? status.currentStation.c_str() : "--";
    const char* dest = status.destinationName.length() ? status.destinationName.c_str() : "--";
    const char* legal = status.legalState.length() ? status.legalState.c_str() : "--";

    const char* mode = "--";
    if (status.onFoot) {
      mode = "On Foot";
    } else if (status.inSrv) {
      mode = "SRV";
    } else if (status.inTaxi) {
      mode = "Taxi";
    } else if (status.inShip) {
      mode = "In Ship";
    }
    
    char buf[512];
    snprintf(buf, sizeof(buf),
      "SYSTEM INFORMATION\n\n"
      "Memory:\n"
      "  Internal RAM: %d KB\n"
      "  PSRAM: %d / %d KB (%.0f%%)\n"
      "  Largest Block: %d KB\n\n"
      "WiFi: %s\n"
      "  IP: %s\n"
      "  RSSI: %d dBm\n\n"
      "WebSocket: %s\n"
      "  Server: %s:%d\n\n"
      "Status:\n"
      "  Mode: %s\n"
      "  System: %s\n"
      "  Station: %s\n"
      "  Destination: %s\n"
      "  Legal: %s\n"
      "  Shields: %.0f%%\n"
      "  Jumps: %d\n"
      "  Fuel: %.1f / %.1f\n"
      "  Cargo: %d/%d (Drones: %d)\n"
      "  Backpack: H%d E%d\n"
      "  Bioscans: %d\n\n"
      "Task Stack Free: %d bytes\n\n"
      "Uptime: %lu sec",
      freeInternal / 1024,
      freePSRAM / 1024, totalPSRAM / 1024, (freePSRAM * 100.0 / totalPSRAM),
      largestBlock / 1024,
      wifiStatus.c_str(), wifiIP.c_str(), rssi,
      wsStatus.c_str(), serverIP.c_str(), websocketPort,
      mode,
      system,
      station,
      dest,
      legal,
      status.shieldsPercent,
      status.nav.jumpsRemaining,
      status.fuel.fuelMain, status.fuel.fuelCapacity,
      status.cargo.usedSpace, status.cargo.totalCapacity, status.cargo.dronesCount,
      status.backpack.healthpack, status.backpack.energycell,
      status.bioscan.totalScans,
      stackHighWater * 4,
      millis() / 1000);
    
    lv_label_set_text(sys_info_label, buf);
    xSemaphoreGive(lvglMutex);
  }
}

void switchToPage(int page) {
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(200)) == pdTRUE) {
    if (page == 0) {
      if (!fighter_screen) {
        create_fighter_ui();
      }
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
      updateStatusLine();
      updateBackpackDisplay();
    } else if (page == 2) {
      if (!settings_screen) {
        create_settings_ui();
      }
      lv_scr_load(settings_screen);
      currentPage = 2;
      updateSystemInfo();
    }
    
    xSemaphoreGive(lvglMutex);
  }
  // Reset timeout and turn on display on page switch
  lastTouchTime = millis();
  if (!displayOn) {
    setDisplayPower(true);
  }
}

// LVGL logging callback
void my_print(lv_log_level_t level, const char * buf)
{
  LV_UNUSED(level);
  Serial.print("[LVGL] ");
  Serial.print(buf);
  Serial.flush();
}


void checkBleConnection()
{
  if (bleGamepad && bleGamepad->isConnected())
  {
    if (!bleConnected)
    {
      Serial.println("[BLE] Connected");
      beepConnect();  // Rising tone for connection
      bleConnected = true;
      ledOn = true;
      setDisplayPower(true);
      update_bluetooth_icon();
    }
    lastBleActiveTime = millis();
  }
  else
  {
    if (bleConnected)
    {
      Serial.println("[BLE] Disconnected");
      beepDisconnect();  // Falling tone for disconnection
      bleConnected = false;
      ledOn = true;
      bleDisconnectedTime = millis();
      update_bluetooth_icon();
    }
    
    // LED timeout tracking (LED removed on ESP32S3)
    if (ledOn && (millis() - bleDisconnectedTime) > LED_TIMEOUT) {
      ledOn = false;
      Serial.println("[LED] Timeout - LED feature disabled on ESP32S3");
    }
  }
}

// Forward declaration
void requestSummary();
void connectWebSocket();
void updateJumpsRemaining(int newValue);
void showJumpOverlay(int jumps);

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

      // Connect to Icarus terminal websocket as soon as WiFi is up
      delay(200);
      connectWebSocket();
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
void requestCmdrStatusWs();
void requestShipStatusWs();
void requestCmdrProfileWs();
void requestNavRouteWs();
static void requestAllStatusOnce();

// WebSocket event handlers
void onWebSocketMessage(WebsocketsMessage message) {
  // Check message size before processing to avoid out-of-memory on large payloads
  printHeapStatus("WS start");
  size_t len = message.length();
  //Serial.printf("[WS] Message length: %d bytes\n", len);
  const size_t MAX_WS_MESSAGE_SIZE = 400000; // Allow larger journal payloads from Icarus terminal

  if (len > MAX_WS_MESSAGE_SIZE) {
    Serial.printf("[WS] ERROR: Message too large (%d bytes), discarding.\n", len);
    return;
  }

  String data = message.data();
  
  //Serial.printf("[WS] Received %d bytes\n", data.length());
  
  // Parse WebSocket JSON message format: {"type": "TYPE", "id": 123, "data": {...}}
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, data, DeserializationOption::NestingLimit(40));
  
  if (error) {
    Serial.printf("[WS] JSON parse error: %s\n", error.c_str());
    printHeapStatus("WS error");
    return;
  }
  
  // Icarus Terminal format: {"name":"newLogEntry", "message":{...}}
  if (doc["name"].is<const char*>()) {
    const char* name = doc["name"];
    JsonVariant msg = doc["message"];

    // Reset timeout and wake display on any message
    lastTouchTime = millis();
    if (!displayOn) {
      setDisplayPower(true);
    }

    if (name && strcmp(name, "newLogEntry") == 0 && !msg.isNull()) {
      String eventType = "JOURNAL";  // Reuse existing journal handler

      JsonDocument eventDoc;
      eventDoc.set(msg);

      printHeapStatus("before handler");
      handleEliteEvent(eventType, eventDoc);
      printHeapStatus("after handler");
    } else if (name && strcmp(name, "getCmdrStatus") == 0 && !msg.isNull()) {
      summaryReceived = true;  // Treat as initial state received
      Serial.println("[WS] Received CmdrStatus response");

      // Extract fuel
      if (!msg["fuel"].isNull()) {
        if (msg["fuel"]["FuelMain"].is<float>()) {
          status.fuel.fuelMain = msg["fuel"]["FuelMain"].as<float>();
        }
        // FuelReservoir is ignored for now
      }

      // Extract used cargo count (capacity unknown in this message)
      if (msg["cargo"].is<int>()) {
        status.cargo.usedSpace = msg["cargo"].as<int>();
        status.cargo.cargoCount = status.cargo.usedSpace - status.cargo.dronesCount;
      }

      // Location and state flags
      if (msg["system"].is<const char*>()) {
        status.currentSystem = msg["system"].as<const char*>();
      }
      if (msg["station"].is<const char*>()) {
        status.currentStation = msg["station"].as<const char*>();
      }
      if (msg["credits"].is<long>()) {
        status.credits = msg["credits"].as<long>();
      }
      if (msg["docked"].is<bool>()) status.docked = msg["docked"];
      if (msg["landed"].is<bool>()) status.landed = msg["landed"];
      if (msg["inSpace"].is<bool>()) status.inSpace = msg["inSpace"];
      if (msg["onFoot"].is<bool>()) status.onFoot = msg["onFoot"];
      if (msg["inShip"].is<bool>()) status.inShip = msg["inShip"];
      if (msg["inSrv"].is<bool>()) status.inSrv = msg["inSrv"];
      if (msg["inTaxi"].is<bool>()) status.inTaxi = msg["inTaxi"];
      if (msg["inMulticrew"].is<bool>()) status.inMulticrew = msg["inMulticrew"];

      updateHeader();
      updateCargoBar();
    } else if (name && strcmp(name, "gameStateChange") == 0) {
      Serial.println("[WS] gameStateChange broadcast received, refreshing state");
      requestShipStatusWs();
    } else if (name && strcmp(name, "getShipStatus") == 0 && !msg.isNull()) {
      summaryReceived = true;  // Treat as initial state received
      Serial.println("[WS] Received ShipStatus response");

      // Fuel levels
      if (msg["fuelLevel"].is<float>()) {
        status.fuel.fuelMain = msg["fuelLevel"].as<float>();
      }
      if (msg["fuelCapacity"].is<float>()) {
        status.fuel.fuelCapacity = msg["fuelCapacity"].as<float>();
      }
      if (msg["onBoard"].is<bool>()) {
        status.inShip = msg["onBoard"];
      }
      if (msg["currentSystem"].is<const char*>()) {
        status.currentSystem = msg["currentSystem"].as<const char*>();
      }

      // Cargo and limpet drones
      if (msg["cargo"].is<JsonObject>()) {
        JsonObject cargo = msg["cargo"].as<JsonObject>();
        if (cargo["capacity"].is<int>()) {
          status.cargo.totalCapacity = cargo["capacity"].as<int>();
        }
        if (cargo["count"].is<int>()) {
          status.cargo.usedSpace = cargo["count"].as<int>();
        }

        // Find drones in inventory list (case-insensitive symbol match)
        int drones = 0;
        if (cargo["inventory"].is<JsonArrayConst>() || cargo["inventory"].is<JsonArray>()) {
          JsonArrayConst inv = cargo["inventory"].as<JsonArrayConst>();
          for (JsonVariantConst item : inv) {
            const char* symbol = item["symbol"];
            if (symbol && (strcmp(symbol, "drones") == 0 || String(symbol).equalsIgnoreCase("drones"))) {
              drones = item["count"].as<int>();
              break;
            }
          }
        }
        status.cargo.dronesCount = drones;
        status.cargo.cargoCount = status.cargo.usedSpace - status.cargo.dronesCount;
        if (status.cargo.cargoCount < 0) status.cargo.cargoCount = 0;
      }

      // Hull health from armour module (health is 0-1, but guard percent inputs)
      float hull = status.hull.hullHealth;
      if (msg["modules"].is<JsonObject>()) {
        JsonObject mods = msg["modules"].as<JsonObject>();
        JsonVariant armour = mods["Armour"];
        if (!armour.isNull() && armour["health"].is<float>()) {
          hull = armour["health"].as<float>();
        }
      }
      if (hull > 1.0f) {
        hull /= 100.0f;
      }
      status.hull.hullHealth = hull;

      if (msg["type"].is<const char*>()) {
        status.hull.hullType = msg["type"].as<const char*>();
      }

      updateHeader();
      updateCargoBar();
    } else if (name && strcmp(name, "getNavRoute") == 0 && !msg.isNull()) {
      Serial.println("[WS] Received NavRoute response");
      if (msg["jumpsToDestination"].is<int>()) {
        updateJumpsRemaining(msg["jumpsToDestination"].as<int>());
      } else if (msg["route"].is<JsonArray>()) {
        JsonArray route = msg["route"].as<JsonArray>();
        updateJumpsRemaining(route.size());
      }
      updateHeader();
    } else if (name && strcmp(name, "getCmdr") == 0 && !msg.isNull()) {
      Serial.println("[WS] Received Cmdr response");
      if (msg["credits"].is<long>()) {
        status.credits = msg["credits"].as<long>();
      }
      // Commander info (name/rank) could be parsed here if needed
    } else {
      Serial.printf("[WS] Unhandled name message: %s\n", name ? name : "(null)");
      Serial.println(data);
    }

    doc.clear();
    printHeapStatus("WS end");
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
      // Request initial ship status from Icarus terminal
      requestShipStatusWs();
      break;
      
    case WebsocketsEvent::ConnectionClosed:
      Serial.printf("[WS] Disconnected from server (info: %s)\n", data.c_str());
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
  if ( WiFi.status() != WL_CONNECTED) {
    Serial.println("[WS] Cannot connect - WiFi not connected");
    return;
  }
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
    // Parse KEEPALIVE packet: KEEPALIVE|0|{"ip": "x.x.x.x", "websocket_port": 12347, "current_message_id": 1}
    const char* ip = doc["ip"];
    int ws_port = doc["websocket_port"] | 0;
    
    if (ip && ws_port > 0) {
      // Check if server changed
      bool serverChanged = (serverIP != String(ip) || websocketPort != ws_port);
      
      serverIP = String(ip);
      websocketPort = ws_port;
      
      Serial.printf("[KEEPALIVE] WebSocket server: %s:%d\n", 
        serverIP.c_str(), websocketPort);
      
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
  } else if (eventType == "BACKPACK") {
    // Parse backpack consumables
    Serial.println("[BACKPACK] Processing backpack update");
    
    if (!doc["Consumables"].isNull()) {
      JsonArray consumables = doc["Consumables"];
      
      // Reset counts
      status.backpack.healthpack = 0;
      status.backpack.energycell = 0;
      
      for (JsonObject item : consumables) {
        const char* name = item["Name"];
        int count = item["Count"] | 0;
        
        if (strcmp(name, "healthpack") == 0) {
          backpackInfo.healthpack = count;
          Serial.printf("[BACKPACK] Healthpack: %d\n", count);
        } else if (strcmp(name, "energycell") == 0) {
          status.backpack.energycell = count;
          Serial.printf("[BACKPACK] Energycell: %d\n", count);
        }
      }
      
      updateBackpackDisplay();
    }
    return;
  } else if (eventType == "BIOSCAN") {
    // Parse bioscan data and accumulate counts
    Serial.println("[BIOSCAN] Processing bioscan");
    
    int count = doc["count"] | 0;
    const char* variant = doc["variant"];
    
    if (count > 0) {
      // Just track total count, ignore variants
      status.bioscan.totalScans = count;
      Serial.printf("[BIOSCAN] Total scans: %d\n", status.bioscan.totalScans);
      updateBackpackDisplay();
    }
    return;
  } else if (eventType == "SUMMARY") {
    // Parse comprehensive summary data
    Serial.println("[SUMMARY] Received state update");
    summaryReceived = true;  // Mark that we've received first summary
    
    // Update fuel (now has percent, current, and capacity)
    if (!doc["fuel"].isNull()) {
      if (doc["fuel"]["current"].is<float>()) {
        status.fuel.fuelMain = doc["fuel"]["current"];
      }
      if (doc["fuel"]["capacity"].is<float>()) {
        status.fuel.fuelCapacity = doc["fuel"]["capacity"];
      }
    }
    
    // Update cargo
    if (!doc["cargo"].isNull()) {
      status.cargo.totalCapacity = doc["cargo"]["capacity"];
      status.cargo.usedSpace = doc["cargo"]["count"].as<int>();
      status.cargo.dronesCount = doc["cargo"]["drones"].as<int>();
      // Calculate cargo without drones
      status.cargo.cargoCount = status.cargo.usedSpace - status.cargo.dronesCount;
    }
    
    // Update hull
    if (!doc["hull"].isNull()) {
      status.hull.hullHealth = doc["hull"].as<float>() / 100.0f;
    }
    
    // Update shields
    if (!doc["shields"].isNull()) {
      // shields is a percentage (0-100)
      float shieldsPercent = doc["shields"].as<float>();
      status.shieldsPercent = shieldsPercent;
      Serial.printf("[SHIELDS] %.1f%%\n", shieldsPercent);
    }

    // Update commander/ship state flags if present
    if (doc["onFoot"].is<bool>()) status.onFoot = doc["onFoot"];
    if (doc["inShip"].is<bool>()) status.inShip = doc["inShip"];
    if (doc["docked"].is<bool>()) status.docked = doc["docked"];
    if (doc["inSpace"].is<bool>()) status.inSpace = doc["inSpace"];
    if (doc["landed"].is<bool>()) status.landed = doc["landed"];
    if (doc["inSrv"].is<bool>()) status.inSrv = doc["inSrv"];
    if (doc["inTaxi"].is<bool>()) status.inTaxi = doc["inTaxi"];
    if (doc["inMulticrew"].is<bool>()) status.inMulticrew = doc["inMulticrew"];
    
    // Update route jumps (now a single integer, not an object)
    if (!doc["route"].isNull()) {
      updateJumpsRemaining(doc["route"].as<int>());
    }
    
    // Update balance
    if (!doc["balance"].is<nullptr_t>()) {
      long balance = doc["balance"];
      status.credits = balance;
      Serial.printf("[BALANCE] Credits: %ld\n", balance);
    }
    
    // Update legal state
    if (doc["legal_state"].is<const char*>()) {
      const char* legalState = doc["legal_state"];
      status.legalState = legalState;
      Serial.printf("[LEGAL] State: %s\n", legalState);
    }
    
    // Update location
    if (!doc["location"].isNull()) {
      const char* system = doc["location"]["system"];
      const char* station = doc["location"]["station"];
      if (system) {
        status.currentSystem = system;
        Serial.printf("[LOCATION] System: %s", system);
        if (station) {
          status.currentStation = station;
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
        status.destinationName = destName;
        Serial.printf("[DESTINATION] %s\n", destName);
      }
    }
    
    // Update backpack from summary if available
    if (!doc["backpack"].isNull()) {
      if (doc["backpack"]["healthpack"].is<int>()) {
        status.backpack.healthpack = doc["backpack"]["healthpack"];
      }
      if (doc["backpack"]["energycell"].is<int>()) {
        status.backpack.energycell = doc["backpack"]["energycell"];
      }
      Serial.printf("[BACKPACK] Healthpack: %d, Energycell: %d\n",
        status.backpack.healthpack, status.backpack.energycell);
    }
    
    // Update bioscans from summary if available
    if (!doc["bioscans"].isNull()) {
      JsonObject bioscans = doc["bioscans"];
      bioscanInfo.totalScans = 0;
      status.bioscan.totalScans = 0;
      
      // Sum all bioscan counts across all variants
      for (JsonPair kv : bioscans) {
        int count = kv.value().as<int>();
        status.bioscan.totalScans += count;
      }
      
      Serial.printf("[BIOSCAN] Total scans from summary: %d\n", status.bioscan.totalScans);
    }
    
    // Update all displays
    updateHeader();
    updateStatusLine();
    updateCargoBar();
    updateBackpackDisplay();
    
    Serial.printf("[SUMMARY] Fuel: %.2f/%.2f (%.1f%%), Cargo: %d/%d (Drones: %d), Hull: %.1f%%, Jumps: %d\n",
      status.fuel.fuelMain, status.fuel.fuelCapacity,
      (status.fuel.fuelCapacity > 0) ? (status.fuel.fuelMain / status.fuel.fuelCapacity * 100.0f) : 0.0f,
      status.cargo.usedSpace, status.cargo.totalCapacity,
      status.cargo.dronesCount,
      status.hull.hullHealth * 100.0f,
      status.nav.jumpsRemaining);
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
        status.communityGoals.clear();
        for (JsonObject goal : goals) {
          CommunityGoal cg;
          if (goal["Title"].is<const char*>()) {
            cg.title = goal["Title"].as<const char*>();
          } else if (goal["Name"].is<const char*>()) {
            cg.title = goal["Name"].as<const char*>();
          }
          if (goal["PlayerPercentileBand"].is<int>()) {
            cg.percentile = goal["PlayerPercentileBand"];
          }
          if (goal["TierReached"].is<int>()) {
            cg.tier = goal["TierReached"];
          }
          if (goal["Contributors"].is<int>()) {
            cg.contributors = goal["Contributors"];
          }
          status.communityGoals.push_back(cg);
          if (!goal["PlayerPercentileBand"].isNull()) {
            int percentile = goal["PlayerPercentileBand"];
            snprintf(logBuf, sizeof(logBuf), "CommunityGoal: Top %d%%", percentile);
            addLogEntry(logBuf);
            Serial.printf("[CG] Player percentile: Top %d%%\n", percentile);
          }
        }
      }
    } else if (event == "FSDTarget") {
      // Update remaining jumps from FSD target journal entries
      if (doc["RemainingJumpsInRoute"].is<int>()) {
        updateJumpsRemaining(doc["RemainingJumpsInRoute"].as<int>());
        updateHeader();
        Serial.printf("[NAV] Jumps remaining: %d\n", status.nav.jumpsRemaining);
      }
    } else if (event == "FSDJump") {
      // Update fuel level after a jump
      if (doc["FuelLevel"].is<float>()) {
        status.fuel.fuelMain = doc["FuelLevel"].as<float>();
        updateHeader();
        Serial.printf("[FUEL] FSDJump FuelLevel: %.2f\n", status.fuel.fuelMain);
      }
    } else if (event == "NavRouteClear") {
      // Journal event when route is cleared
      updateJumpsRemaining(0);
      updateHeader();
      Serial.println("[NAV] Route cleared -> Jumps set to 0");
    } else if (event == "BuyDrones") {
      // After buying limpets, refresh ship status to update cargo/drones
      Serial.println("[JOURNAL] BuyDrones -> requesting ship status");
      requestShipStatusWs();
    } else if (event == "HullDamage") {
      // Update hull health from damage event
      if (!doc["Health"].isNull()) {
        status.hull.hullHealth = doc["Health"];
        updateHeader();
        Serial.printf("[HULL] Health: %.1f%%\n", status.hull.hullHealth * 100.0f);
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
      status.credits = balance;
      // Balance updated, but not displayed currently
    }

    // Update state flags if present
    if (doc["OnFoot"].is<bool>()) status.onFoot = doc["OnFoot"];
    if (doc["InShip"].is<bool>()) status.inShip = doc["InShip"];
    if (doc["Docked"].is<bool>()) status.docked = doc["Docked"];
    if (doc["InSpace"].is<bool>()) status.inSpace = doc["InSpace"];
    if (doc["Landed"].is<bool>()) status.landed = doc["Landed"];
    if (doc["InSRV"].is<bool>()) status.inSrv = doc["InSRV"];
    if (doc["InTaxi"].is<bool>()) status.inTaxi = doc["InTaxi"];
    if (doc["InMulticrew"].is<bool>()) status.inMulticrew = doc["InMulticrew"];

    if (doc["Flags"].is<JsonObject>()) {
      JsonObject flags = doc["Flags"].as<JsonObject>();
      if (flags["onFoot"].is<bool>()) status.onFoot = flags["onFoot"];
      if (flags["inShip"].is<bool>()) status.inShip = flags["inShip"];
      if (flags["docked"].is<bool>()) status.docked = flags["docked"];
      if (flags["inSpace"].is<bool>()) status.inSpace = flags["inSpace"];
      if (flags["landed"].is<bool>()) status.landed = flags["landed"];
      if (flags["inSrv"].is<bool>()) status.inSrv = flags["inSrv"];
      if (flags["inTaxi"].is<bool>()) status.inTaxi = flags["inTaxi"];
      if (flags["inMulticrew"].is<bool>()) status.inMulticrew = flags["inMulticrew"];
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
  } else if (eventType == "NavRouteClear") {
    // Route cleared -> zero out jumps
    updateJumpsRemaining(0);
    updateHeader();
  } else if (eventType == "NAVROUTE") {
    // Count route entries
    if (!doc["Route"].isNull()) {
      JsonArray route = doc["Route"];
      updateJumpsRemaining(route.size());
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
    Serial.print("[WS] Unhandled event type: ");
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
      static uint32_t lastPing = 0;
      uint32_t now = millis();
      if (now - lastPing > 15000) {  // Send ping every 15s to keep connection alive
        wsClient.ping();
        lastPing = now;
      }
    }
  }
}

// Coalesced status requests with a short cooldown to avoid flooding the server
static void requestAllStatusOnce() {
  static uint32_t lastRequestBatch = 0;
  uint32_t now = millis();
  if (now - lastRequestBatch < 1500) {
    return;  // too soon, skip this batch
  }
  lastRequestBatch = now;

  requestCmdrStatusWs();
  requestShipStatusWs();
  requestCmdrProfileWs();
  requestNavRouteWs();
}

// Message handling task running on Core 0 for non-blocking WebSocket reception
void loop2(void* parameter) {
  Serial.print("[MSG] Task started on core: ");
  Serial.println(xPortGetCoreID());
  
  while (1) {
    // Check messages (WebSocket polling)
    checkMessages();
    
    // Poll frequently for WebSocket
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms polling
  }
}

void onWifiConnect(const WiFiEvent_t event, const WiFiEventInfo_t info)
{
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    wifiWasConnected = true;
    beepConnect();  // Rising tone for WiFi connection

    update_wifi_icon();

    // Connect to WebSocket immediately when WiFi comes up
    connectWebSocket();
  } else {
    Serial.println("\nWiFi connection failed, continuing without WiFi");
  }
}

void WifiConnect()
{
  
  // Configure WiFi with auto-reconnect
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  // Set hostname
  WiFi.setHostname(HOSTNAME);
  //WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiMulti.addAP(WIFI_SSID, WIFI_PASSWORD);
  wifiMulti.run();

  WiFi.onEvent(onWifiConnect, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent([](const WiFiEvent_t event, const WiFiEventInfo_t info) {
    Serial.println("\nWiFi disconnected");
    wifiWasConnected = false;
    beepDisconnect();  // Falling tone for WiFi disconnection
  }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  
  Serial.print("Connecting to WiFi");
  uint8_t retries = 0;
  /*while (WiFi.status() != WL_CONNECTED && retries < 20) { // 10 seconds timeout
    delay(500);
    Serial.print(".");
    retries++;
  }*/
  
}

void setup()
{
  Serial.begin(115200);
  delay(100);
  Serial.println("\n\n[BOOT] Starting ESP32S3 BLE Joypad...");
  
  // Check PSRAM availability
  Serial.printf("[PSRAM] Available: %s\n", psramFound() ? "YES" : "NO");
  if (psramFound()) {
    Serial.printf("[PSRAM] Total: %d bytes\n", ESP.getPsramSize());
    Serial.printf("[PSRAM] Free: %d bytes\n", ESP.getFreePsram());
  }
  
  // Allocate large buffers from PSRAM (fallback to internal if PSRAM not available)
  Serial.println("[MEM] Allocating buffers...");
  buf = (uint32_t*)heap_caps_malloc(LVGL_BUFFER_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) {
    Serial.println("[MEM] PSRAM allocation failed, trying internal RAM...");
    buf = (uint32_t*)heap_caps_malloc(LVGL_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  
  logText = (char*)heap_caps_malloc(LOG_TEXT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!logText) {
    Serial.println("[MEM] PSRAM allocation failed for log buffer, trying internal RAM...");
    logText = (char*)heap_caps_malloc(LOG_TEXT_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  
  if (!buf || !logText) {
    Serial.println("ERROR: Failed to allocate buffers!");
    Serial.printf("buf: %p, logText: %p\n", buf, logText);
    while(1) delay(1000);
  }
  
  Serial.printf("[MEM] Buffers allocated - LVGL: %d, Log: %d bytes\n", 
                LVGL_BUFFER_SIZE, LOG_TEXT_SIZE);
  Serial.printf("[HEAP] Free internal RAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  if (psramFound()) {
    Serial.printf("[PSRAM] Free PSRAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  }
  delay(100);
  // pinMode(BUZZER_PIN, OUTPUT);  // Disabled - buzzer uses LEDC on ESP32S3
  
  // beepBootup();  // Disabled - tone() uses LEDC
  
  Serial.println("[WIFI] Connecting...");
  WifiConnect();
  
  // Initialize display power management
  Serial.println("[INIT] Setting up display power management...");
  lastTouchTime = millis();
  lastBleActiveTime = millis();
  bleDisconnectedTime = millis();

  Serial.println("[I2C] Initializing I2C bus...");
  Wire.begin(I2C_SDA, I2C_SCL, I2C_SPEED);
  //Audio pins:
  //GPIO1: Audio Power amplifier IC enable Pin, low level enable
  //GPIO4: Audio I2S bus master clock signal
  //GPIO5: Audio I2S bus bit clock signal
  //GPIO6: Audio I2S bus bit output data signal
  //GPIO7: Audio I2S bus left and right channel selection signal. High level: right channel; low level: left channel
  //GPIO8: I2S bus bit input data signal
  //GPIO16: I2C bus data signal
  //GPIO17: I2C bus clock signal

  Serial.println("[I2S] Initializing I2S audio...");
  // Enable audio amplifier (GPIO1, active LOW)
  pinMode(1, OUTPUT);
  digitalWrite(1, LOW);  // Enable amplifier
  delay(100);  // Wait for amplifier to stabilize
  
  // Initialize ES8311 codec FIRST
  Serial.println("[ES8311] Initializing codec...");
  if (es8311_codec_init() != ESP_OK) {
    Serial.println("[ES8311] ERROR: Codec initialization failed!");
  } else {
    Serial.println("[ES8311] Codec initialized successfully");
  }
  
  // Initialize I2S driver with pin configuration
  Serial.println("[I2S] Configuring I2S driver...");
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = AUDIO_SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // stereo frames
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = AUDIO_MCLK_HZ
  };

  i2s_pin_config_t pin_config = {
    .mck_io_num = I2S_MCK,    // GPIO4
    .bck_io_num = I2S_BCK,      // GPIO5
    .ws_io_num = I2S_WS,        // GPIO7
    .data_out_num = I2S_DOUT,   // GPIO6
    .data_in_num = -1            // unused (playback only)
  };

  esp_err_t i2s_err = i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  if (i2s_err != ESP_OK) {
    Serial.printf("[I2S] ERROR: Driver install failed: %d\n", i2s_err);
  } else {
    if (i2s_set_pin(I2S_NUM_0, &pin_config) != ESP_OK) {
      Serial.println("[I2S] ERROR: Pin configuration failed");
    } else {
      Serial.println("[I2S] Driver and pins configured successfully");
    }
    if (i2s_set_clk(I2S_NUM_0, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO) != ESP_OK) {
      Serial.println("[I2S] ERROR: Clock configuration failed");
    } else {
      Serial.println("[I2S] Clock configured successfully");
    }
    if (i2s_zero_dma_buffer(I2S_NUM_0) != ESP_OK) {
      Serial.println("[I2S] ERROR: DMA buffer zeroing failed");
    } else {
      Serial.println("[I2S] DMA buffer zeroed successfully");
    }
    soundSetInitialized(true);  // Mark I2S as ready for tones
    Serial.println("[I2S] Audio driver initialized successfully");
  }

  Serial.println("[PCF8575] Creating button controller...");
  pcf = new PCF8575(PCF8575_ADDR);
  pcf->begin();

  Serial.println("[BLE] Creating and configuring gamepad...");
  bleGamepad = new BleGamepad("CoolJoyBLE", "leDev", 100);
  bleGamepadConfig.setButtonCount(20);
  bleGamepadConfig.setIncludeRxAxis(false);
  bleGamepadConfig.setWhichAxes(false, false, false, false, false, false, false, false);
  bleGamepadConfig.setHatSwitchCount(0);

  Serial.println("[BLE] Starting BLE gamepad...");
  bleGamepad->begin(&bleGamepadConfig);
  
  Serial.println("[DISPLAY] Initializing display and LVGL...");
  //init_display();
  disp.init();
  disp.setTouchCallback(handleTouchActivity);

  Serial.println("[UI] Creating fighter UI...");
  create_fighter_ui();
  Serial.println("[UI] Creating logviewer UI...");
  create_logviewer_ui();
  Serial.println("[UI] Loading logviewer screen...");
  lv_scr_load(logviewer_screen);
  Serial.println("[UI] Initialization complete!");
  
  // Play startup tone
  Serial.println("[AUDIO] Playing startup tone...");
  beepBootup();
  delay(200);
  
  // Create mutex for LVGL thread safety
  lvglMutex = xSemaphoreCreateMutex();
  if (!lvglMutex) {
    Serial.println("[ERROR] Failed to create LVGL mutex!");
    while(1);
  }
  
  // Start message handler task on Core 0 (WebSocket polling)
  xTaskCreatePinnedToCore(
    loop2,              // Task function
    "MSG_Handler",      // Task name
    32768,              // Stack size (32KB - increased with PSRAM for WebSocket + JSON parsing)
    NULL,               // Parameters
    2,                  // Priority (2 = higher than default)
    &msgTaskHandle,     // Task handle
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
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    lv_timer_handler();
    xSemaphoreGive(lvglMutex);
  }
  
  // Process pending jump overlay
  if (pendingJumpOverlay) {
    pendingJumpOverlay = false;
    showJumpOverlay(pendingJumpValue);
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
    UBaseType_t stackHighWater = uxTaskGetStackHighWaterMark(msgTaskHandle);
    Serial.printf("[STACK] MSG_Handler high water mark: %d bytes free\n", stackHighWater * 4);
    
    // Update system info if on settings page
    if (currentPage == 2) {
      updateSystemInfo();
    }
    
    lastHeapPrint = currentTime;
  }
  
  // Request initial state every 10 seconds until first one is received
  if (!summaryReceived && (currentTime - lastSummaryRequest) > SUMMARY_REQUEST_INTERVAL) {
    if (wsClient.available()) {
      requestCmdrStatusWs();
      requestShipStatusWs();
      requestCmdrProfileWs();
      requestNavRouteWs();
    } else {
      // Try to connect WS if not already
      connectWebSocket();
    }
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
        digitalWrite(45, LOW);
        displayOn = false;
      } else {
        digitalWrite(45, HIGH);
        displayOn = true;
      }
      blinkCount--;
      lastBlinkTime = millis();
      
      if (blinkCount == 0) {
        blinkScreen = false;
        digitalWrite(45, HIGH);
        displayOn = true;
      }
    }
  }
  
  // Read PCF8575 state
  uint16_t pcfState = pcf->read16();
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
  
  // Handle SILVER_RIGHT - switch to page 2 (System Settings)
  if (silverRightPressed && !silverRightWasPressed) {
    switchToPage(2);
    Serial.println("Page: System Settings (2)");
  }
  silverRightWasPressed = silverRightPressed;
  
  // Handle remaining buttons (indices 3-11) for gamepad - only when BLE connected
  if (bleConnected) {
    for (uint8_t i = 3; i < 12; i++) {
      bool pressed = !(pcfState & (1 << buttonPins[i]));
      bool lastPressed = (lastButtonState & (1 << i)) != 0;
      
      if (pressed != lastPressed) {
        if (pressed) {
          bleGamepad->press(i + 1);
          Serial.printf("Button %d pressed\n", i + 1);
          anyPressed = true;
          lastButtonState |= (1 << i);
        } else {
          bleGamepad->release(i + 1);
          Serial.printf("Button %d released\n", i + 1);
          lastButtonState &= ~(1 << i);
        }
      }
    }
    bleGamepad->sendReport();
  }
  
  delay(5);
}

static void jump_overlay_zoom_exec(void* obj, int32_t value) {
  lv_obj_set_style_transform_zoom(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
}

static void jump_overlay_opa_exec(void* obj, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)value, LV_PART_MAIN);
}

static void jump_overlay_anim_ready(lv_anim_t* anim) {
  lv_obj_t* obj = static_cast<lv_obj_t*>(anim->var);
  if (obj) {
    lv_obj_del_async(obj);
    if (obj == jump_overlay_label) {
      jump_overlay_label = nullptr;
    }
  }
}

void showJumpOverlay(int jumps) {
  if (!lvglMutex) return;
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (jump_overlay_label) {
      lv_obj_del(jump_overlay_label);
      jump_overlay_label = nullptr;
    }
    lv_obj_t* parent = lv_scr_act();
    if (!parent) {
      xSemaphoreGive(lvglMutex);
      return;
    }
    jump_overlay_label = lv_label_create(parent);
    lv_label_set_text_fmt(jump_overlay_label, "%d", jumps);
    lv_obj_set_style_text_color(jump_overlay_label, LV_COLOR_FG, 0);
    lv_obj_set_style_text_font(jump_overlay_label, LV_FONT_DEFAULT, 0);
    lv_obj_center(jump_overlay_label);
    lv_obj_set_style_opa(jump_overlay_label, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_zoom(jump_overlay_label, 768, LV_PART_MAIN);
    lv_obj_update_layout(jump_overlay_label);
    lv_coord_t pivotX = lv_obj_get_width(jump_overlay_label) / 2;
    lv_coord_t pivotY = lv_obj_get_height(jump_overlay_label) / 2;
    lv_obj_set_style_transform_pivot_x(jump_overlay_label, pivotX, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(jump_overlay_label, pivotY, LV_PART_MAIN);

    lv_anim_t zoom_anim;
    lv_anim_init(&zoom_anim);
    lv_anim_set_var(&zoom_anim, jump_overlay_label);
    lv_anim_set_values(&zoom_anim, 768, 0);
    lv_anim_set_time(&zoom_anim, 1000);
    lv_anim_set_path_cb(&zoom_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&zoom_anim, jump_overlay_zoom_exec);
    lv_anim_set_ready_cb(&zoom_anim, jump_overlay_anim_ready);
    lv_anim_start(&zoom_anim);

    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, jump_overlay_label);
    lv_anim_set_values(&fade_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&fade_anim, 1000);
    lv_anim_set_path_cb(&fade_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&fade_anim, jump_overlay_opa_exec);
    lv_anim_start(&fade_anim);

    xSemaphoreGive(lvglMutex);
  }
}

void updateJumpsRemaining(int newValue) {
  if (newValue < 0) newValue = 0;
  bool changed = status.nav.jumpsRemaining != newValue;
  status.nav.jumpsRemaining = newValue;
  if (changed) {
    pendingJumpOverlay = true;
    pendingJumpValue = newValue;
  }
}

// Request commander status via WebSocket (Icarus terminal)
void requestCmdrStatusWs() {
  if (!wsClient.available()) {
    return;
  }

  static uint32_t lastSent = 0;
  uint32_t now = millis();
  if (now - lastSent < 1000) {
    return; // rate limit: 1 Hz
  }
  lastSent = now;

  // Build a lightweight request with a simple requestId
  String reqId = String("esp32-") + String(millis());
  String payload = String("{\"requestId\":\"") + reqId + "\",\"name\":\"getCmdrStatus\",\"message\":{}}";
  wsClient.send(payload);
  Serial.println("[WS] Sent getCmdrStatus request");
}

// Request ship status via WebSocket (Icarus terminal)
void requestShipStatusWs() {
  if (!wsClient.available()) {
    return;
  }

  String reqId = String("esp32-ship-") + String(millis());
  String payload = String("{\"requestId\":\"") + reqId + "\",\"name\":\"getShipStatus\",\"message\":{}}";
  wsClient.send(payload);
  Serial.println("[WS] Sent getShipStatus request");
}

// Request commander profile (credits, name)
void requestCmdrProfileWs() {
  if (!wsClient.available()) {
    return;
  }

  String reqId = String("esp32-cmdr-") + String(millis());
  String payload = String("{\"requestId\":\"") + reqId + "\",\"name\":\"getCmdr\",\"message\":{}}";
  wsClient.send(payload);
  Serial.println("[WS] Sent getCmdr request");
}

// Request nav route information (jumps, destination)
void requestNavRouteWs() {
  if (!wsClient.available()) {
    return;
  }

  static uint32_t lastSent = 0;
  uint32_t now = millis();
  if (now - lastSent < 1000) {
    return; // rate limit: 1 Hz
  }
  lastSent = now;

  String reqId = String("esp32-nav-") + String(millis());
  String payload = String("{\"requestId\":\"") + reqId + "\",\"name\":\"getNavRoute\",\"message\":{}}";
  wsClient.send(payload);
  Serial.println("[WS] Sent getNavRoute request");
}

#if defined(ESP_IDF_VERSION)
// ESP-IDF entry point that boots the Arduino runtime and forwards into the existing sketch
extern "C" void app_main(void) {
  initArduino();
  setup();

  while (true) {
    loop();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
#endif