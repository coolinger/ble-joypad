
#include <Arduino.h>
#include <Wire.h>
#include <PCF8575.h>
#include <BleGamepad.h>
#include "display.h"
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ArduinoJson.h>
#include <ArduinoWebsockets.h>
#include <ArduinoOTA.h>
#include <esp_heap_caps.h>
#include <algorithm>
#include "sound.h"
#include "config.h"
#include "gamedata.h"
#include "screens/fighter.h"
#include "screens/info.h"
#include "screens/system.h"
#include "screens/shell.h"
// I2S: new IDF "std" driver (driver/i2s_std.h). Migrated off the legacy
// driver/i2s.h so the legacy<->new "CONFLICT" runtime error can't occur and the
// deprecation warnings are gone. The TX channel handle (i2s_tx_chan) is created
// in setup() and used by sound.cpp for playback.
#include "driver/i2s_std.h"


using namespace websockets;



// All board pins live in config.h (Guition JC4827W543). The PCF8575 with the
// TTP223 pads sits on Wire1 (PCF_SDA/PCF_SCL); GT911 touch owns Wire.
// 50 kHz on Wire1: the PCF8575 hangs off jumper wires with only the ESP32's
// weak internal pull-ups — 400 kHz gets no ACK. 50 kHz is the ESPHome default
// the same hardware was verified with.
#define I2C_SPEED 50000

// TX channel handle for the new i2s_std driver (created in setup(), used by sound.cpp)
i2s_chan_handle_t i2s_tx_chan = NULL;

#define DR_REG_USB_SERIAL_JTAG_BASE             0x60038000

// Audio clocking constants now in sound.h

#include "colors.h"
#include "theme.h"

// Global flags

PCF8575* pcf = nullptr;
bool pcfAvailable = false;  // begin() succeeded; loop() retries every 5 s if not

// Boot-time diagnostic: probe every I2C address on the given bus.
static void scanI2CBus(TwoWire &bus, const char* name) {
  Serial.printf("[I2C] %s scan:", name);
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    bus.beginTransmission(a);
    if (bus.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  Serial.printf(" -> %d device(s)\n", found);
}
BleGamepad* bleGamepad = nullptr;
BleGamepadConfiguration bleGamepadConfig;
bool bleConnected = false;

// Display and LVGL objects
//static lv_display_t* disp;
Display disp;

WiFiMulti wifiMulti;

// WebSocket variables for Elite Dangerous data
// If not discovered via KEEPALIVE, fallback to configured server IP
String serverIP = DEFAULT_SERVER_IP;
int websocketPort = DEFAULT_WEBSOCKET_PORT;  // Icarus terminal WS port
bool useWebSocket = false;
WebsocketsClient wsClient;
bool wsConnecting = false;
uint32_t wsNextReconnect = 0;
uint32_t wsReconnectDelayMs = 3000;           // start with 3s backoff
const uint32_t WS_RECONNECT_DELAY_MAX = 60000; // cap at 60s
uint32_t wsLastPing = 0;
uint32_t wsLastPong = 0;
bool otaInitialized = false;

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
const uint32_t LED_TIMEOUT = 5 * 60 * 1000; // 5 minutes in milliseconds
bool ledOn = true;

// While a BLE host is connected the display may idle this long (with the dim
// stage before the end); without one the controller is not in use: 5 s.
const uint32_t DISPLAY_TIMEOUT_ACTIVE_MS = 5 * 60 * 1000;
const uint32_t DISPLAY_TIMEOUT_NO_BLE_MS = 5 * 1000;

// Backlight (GPIO45) is driven by LEDC PWM so we can dim it. Two-stage timeout:
// full brightness while active, 30% for the last DISPLAY_DIM_LEAD_MS, then off.
#define BL_PIN LCD_BL_PIN
uint8_t BL_DUTY_FULL = 255;  // adjustable from the system screen
const uint8_t  BL_DUTY_DIM  = 77;           // ~30%
const uint32_t DISPLAY_DIM_LEAD_MS = 15000; // dim this long before the final off
bool displayDimmed = false;                 // true while at BL_DUTY_DIM
// Set when the display is switched off via the BLACK button: suppresses the
// WS-message auto-wake so the screen stays dark until a deliberate action
// (touch / page button / BLE reconnect) wakes it through wakeDisplay().
bool displayManualOff = false;

// FSSBodySignals: one short beep per detected signal. Incremented by the WS
// task (handleEliteEvent), drained evenly spaced by loop() on core 1.
volatile int pendingSignalBeeps = 0;
// Set by the WS task when a Scan yields a first discovery; loop() plays the chime.
volatile bool pendingFirstDiscBeep = false;

// History replay (getLogEntries after boot): rebuilds log/pins/system state
// from the most recent journal entries. While replayingHistory is set, the
// event handler suppresses side effects (beeps, blinking, jump overlay) and
// addLogEntry skips per-entry display updates.
bool historyRequested = false;   // getLogEntries sent (once per boot)
bool historyLoaded = false;      // replay finished
bool replayingHistory = false;

// Forward declarations for display power control
void setDisplayPower(bool on);
void setDisplayDim();
void wakeDisplay();

static void handleTouchActivity()
{
  wakeDisplay();
}

// WiFi reconnection management
uint32_t lastWifiCheck = 0;
const uint32_t WIFI_CHECK_INTERVAL = 10000; // Check every 10 seconds
bool wifiWasConnected = false;
// Deferred requests from the settings UI. Set in LVGL event callbacks (which run
// under the lvglMutex) and serviced in loop() outside the mutex, so the blocking
// WiFi/WebSocket calls never stall rendering or the core-0 message task.
bool reqRestartWifi = false;
bool reqRestartWebSocket = false;
volatile int reqPageSwitch = -1;  // set by the shell tab rail (LVGL cb), serviced in loop()
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
  if (on) {
    // Wake from full-off OR restore from the dimmed stage.
    if (!displayOn || displayDimmed) {
      bool wasOff = !displayOn;
      ledcWrite(BL_PIN, BL_DUTY_FULL);
      displayOn = true;
      displayDimmed = false;
      if (wasOff) {
        beepShort();  // beep only on a real wake from off, not on un-dim
        Serial.println("Display ON");
      }
    }
  } else if (displayOn) {
    ledcWrite(BL_PIN, 0); // Turn off backlight
    displayOn = false;
    displayDimmed = false;
    Serial.println("Display OFF");
  }
}

// Stage 1 of the timeout: drop to 30% brightness (display stays on).
void setDisplayDim()
{
  if (displayOn && !displayDimmed) {
    ledcWrite(BL_PIN, BL_DUTY_DIM);
    displayDimmed = true;
    Serial.println("Display DIM (30%)");
  }
}

// Any user/system activity: reset the idle timer and bring the backlight back
// to full from either the dimmed stage or full-off. Idempotent when already on.
void wakeDisplay()
{
  lastTouchTime = millis();
  displayManualOff = false;
  setDisplayPower(true);
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

// Context panel: BACKPACK while on foot, EXPLORATION otherwise.
void updateContextPanel() {
  if (!ctx_panel) return;

  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    // NOTE: ctx_lines use Montserrat (ASCII only) - keep these strings ASCII
    // (no "·"/"—" typographic characters, they would render as blanks).
    char line[48];
    if (status.onFoot) {
      lv_label_set_text(ctx_rail_label, "BACKPACK / ON FOOT");
      snprintf(line, sizeof(line), "Med %d   Cell %d",
               status.backpack.healthpack, status.backpack.energycell);
      lv_label_set_text(ctx_lines[0], line);
      snprintf(line, sizeof(line), "Bio-Daten %d", status.bioscan.totalScans);
      lv_label_set_text(ctx_lines[1], line);
      lv_label_set_text(ctx_lines[2], "");
      lv_label_set_text(ctx_lines[3], "");
      lv_obj_set_style_text_color(ctx_lines[2], LV_COLOR_FG, 0);
    } else {
      ExplorationInfo &x = status.exploration;
      lv_label_set_text(ctx_rail_label, "EXPLORATION");

      if (!x.honked) snprintf(line, sizeof(line), "HONK -   BODIES -");
      else if (x.allFound) snprintf(line, sizeof(line), "HONK OK  BODIES %d/%d OK", x.bodyCount, x.bodyCount);
      else snprintf(line, sizeof(line), "HONK OK  BODIES %d", x.bodyCount);
      lv_label_set_text(ctx_lines[0], line);

      snprintf(line, sizeof(line), "SCAN %d   MAP %d", x.scanned, x.mapped);
      lv_label_set_text(ctx_lines[1], line);

      snprintf(line, sizeof(line), "FIRST: DISC %d  MAP %d", x.firstDiscovered, x.firstMapped);
      lv_label_set_text(ctx_lines[2], line);
      lv_obj_set_style_text_color(ctx_lines[2],
          (x.firstDiscovered + x.firstMapped) > 0 ? LV_COLOR_HIGHLIGHT_BG : LV_COLOR_DIM, 0);

      int st = x.stationsL + x.stationsM + x.carriers;
      if (st == 0) snprintf(line, sizeof(line), "STATIONS -");
      else {
        int l2 = snprintf(line, sizeof(line), "STATIONS");
        if (x.stationsL) l2 += snprintf(line + l2, sizeof(line) - l2, " %dL", x.stationsL);
        if (x.stationsM) l2 += snprintf(line + l2, sizeof(line) - l2, " %dM", x.stationsM);
        if (x.carriers)  l2 += snprintf(line + l2, sizeof(line) - l2, " %dFC", x.carriers);
      }
      lv_label_set_text(ctx_lines[3], line);
    }
    xSemaphoreGive(lvglMutex);
  }
}
// Unlocked variants: caller must already hold lvglMutex (e.g. updateHeader()).
void update_wifi_icon_unlocked() {
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
      lv_obj_set_style_text_color(wifi_icon, LV_COLOR_HAIRLINE, 0);  // off - No connection
    }
  }
}
void update_bluetooth_icon_unlocked() {
    // Update Bluetooth icon
  if (bluetooth_icon) {
    if (bleGamepad && bleGamepad->isConnected()) {
      lv_obj_set_style_text_color(bluetooth_icon, LV_COLOR_FG, 0);  // connected
    } else {
      lv_obj_set_style_text_color(bluetooth_icon, LV_COLOR_HAIRLINE, 0);  // off - not connected
    }
  }
}

// Locked wrappers: for call sites that do NOT already hold lvglMutex
// (checkBleConnection() in loop(), onWifiConnect() on the WiFi event task).
void update_wifi_icon() {
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    update_wifi_icon_unlocked();
    xSemaphoreGive(lvglMutex);
  }
}
void update_bluetooth_icon() {
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    update_bluetooth_icon_unlocked();
    xSemaphoreGive(lvglMutex);
  }
}

void updateHeader() {
  if (!shell_jumps_label) return;

  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    lv_label_set_text_fmt(shell_jumps_label, "%d", status.nav.jumpsRemaining);

    int fuelPct = (status.fuel.fuelCapacity > 0)
        ? (int)(status.fuel.fuelMain / status.fuel.fuelCapacity * 100.0f) : 0;
    lv_arc_set_value(shell_fuel_arc, fuelPct);
    lv_label_set_text_fmt(shell_fuel_label, "%d%%", fuelPct);

    int hullPct = (int)(status.hull.hullHealth * 100.0f);
    lv_arc_set_value(shell_hull_arc, hullPct);
    lv_label_set_text_fmt(shell_hull_label, "%d%%", hullPct);
    lv_obj_set_style_text_color(shell_hull_label,
        hullPct <= 25 ? LV_COLOR_WARNING_FG : LV_COLOR_VALUE, 0);

    lv_label_set_text_fmt(shell_cargo_label, "%d/%d",
        status.cargo.usedSpace, status.cargo.totalCapacity);

    update_wifi_icon_unlocked();
    if (websocket_icon) {
      lv_obj_set_style_text_color(websocket_icon,
          (useWebSocket && wsClient.available()) ? LV_COLOR_FG : LV_COLOR_HAIRLINE, 0);
    }
    update_bluetooth_icon_unlocked();

    xSemaphoreGive(lvglMutex);
  }
}

void updateStatusLine() {
  if (!status_label) return;

  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    const char* system = status.currentSystem.length()
                             ? status.currentSystem.c_str()
                             : "Waiting for events...";
    lv_label_set_text(status_label, system);

    const char* mode = "";
    if (status.onFoot) mode = "ON FOOT";
    else if (status.inSrv) mode = "SRV";
    else if (status.inTaxi) mode = "TAXI";
    else if (status.docked) mode = "DOCKED";
    else if (status.inShip) mode = "SHIP";
    if (shell_mode_label) lv_label_set_text(shell_mode_label, mode);

    xSemaphoreGive(lvglMutex);
  }
}

static char* logText = nullptr;  // Allocated on heap
static const int LOG_TEXT_SIZE = 2048;  // Large buffer with PSRAM for more log entries

// Fill the sidebar pin cards from pinnedBodies[]. Caller MUST hold lvglMutex
// (the mutex is not recursive - never call this from outside updateLogDisplay
// without taking the mutex yourself).
static void updatePinnedSidebarUnlocked() {
  for (int i = 0; i < MAX_PINNED_BODIES; i++) {
    if (!pin_cards[i]) return;
    if (i >= pinnedBodyCount) {
      lv_obj_add_flag(pin_cards[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    PinnedBody &pb = pinnedBodies[i];
    lv_obj_remove_flag(pin_cards[i], LV_OBJ_FLAG_HIDDEN);

    bool bioComplete = (pb.bio > 0 && pb.bioDone >= pb.bio);
    char t[64];
    int l = snprintf(t, sizeof(t), "%s", pb.label);
    if (pb.bio > 0 && l < (int)sizeof(t))
      l += snprintf(t + l, sizeof(t) - l, "  Bio %d/%d", pb.bioDone, pb.bio);
    if (pb.geo > 0 && l < (int)sizeof(t))
      l += snprintf(t + l, sizeof(t) - l, "  Geo %d", pb.geo);
    if (pb.other > 0 && l < (int)sizeof(t))
      l += snprintf(t + l, sizeof(t) - l, "  Sig %d", pb.other);
    lv_label_set_text(pin_title_labels[i], t);
    lv_obj_set_style_text_color(pin_title_labels[i],
        bioComplete ? lv_color_hex(0xc6e6dc) : lv_color_hex(0xffb000), 0);

    if (pb.genusCount > 0) {
      // Genus states: green = to scan, purple = in the sampler, blue = done.
      char g[256];
      int gl = 0;
      for (int gi = 0; gi < pb.genusCount && gl < (int)sizeof(g) - 32; gi++) {
        const char *col = "00c060";
        if (pb.genuses[gi].state == BIO_SCANNING) col = "c85aff";
        else if (pb.genuses[gi].state == BIO_DONE) col = "4169e1";
        gl += snprintf(g + gl, sizeof(g) - gl, "#%s %s# ", col, pb.genuses[gi].name);
      }
      lv_label_set_text(pin_genus_labels[i], g);
      lv_obj_remove_flag(pin_genus_labels[i], LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(pin_genus_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
  }
}

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

    // Pins render as sidebar cards now, not as log lines.
    updatePinnedSidebarUnlocked();

    // Only show as many entries as we actually have
    int entriesToShow = (eventLogCount < 9) ? eventLogCount : 9;
    
    Serial.printf("[LOG] Updating display: showing %d entries (count=%d, index=%d)\n",
      entriesToShow, eventLogCount, eventLogIndex);
    
    for (int i = 0; i < entriesToShow; i++) {
      // Calculate index going backwards from most recent
      int idx = (eventLogIndex - 1 - i + 9) % 9;
      
      // Validate entry has text
      if (eventLog[idx].text[0] == 0) {
        continue;
      }

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
  // During the boot-time history replay the display is refreshed once at the
  // end instead of after every replayed entry.
  if (!replayingHistory) updateLogDisplay();
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
    
    char buf[640];
    snprintf(buf, sizeof(buf),
      "FW: " __DATE__ " " __TIME__ "\n\n"
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
      "  Expl: %s B:%d S:%d M:%d 1st:%d/%d St:%dL %dM %dFC\n\n"
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
      status.exploration.honked ? (status.exploration.allFound ? "HONK+ALL" : "HONK") : "-",
      status.exploration.bodyCount, status.exploration.scanned, status.exploration.mapped,
      status.exploration.firstDiscovered, status.exploration.firstMapped,
      status.exploration.stationsL, status.exploration.stationsM, status.exploration.carriers,
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
#if PAGE_FADE_MS > 0
      lv_screen_load_anim(fighter_screen, LV_SCR_LOAD_ANIM_FADE_IN, PAGE_FADE_MS, 0, false);
#else
      lv_screen_load(fighter_screen);
#endif
      currentPage = 0;
    } else if (page == 1) {
      if (!logviewer_screen) {
        create_logviewer_ui();
      }
#if PAGE_FADE_MS > 0
      lv_screen_load_anim(logviewer_screen, LV_SCR_LOAD_ANIM_FADE_IN, PAGE_FADE_MS, 0, false);
#else
      lv_screen_load(logviewer_screen);
#endif
      currentPage = 1;
    } else if (page == 2) {
      if (!settings_screen) {
        create_settings_ui();
      }
#if PAGE_FADE_MS > 0
      lv_screen_load_anim(settings_screen, LV_SCR_LOAD_ANIM_FADE_IN, PAGE_FADE_MS, 0, false);
#else
      lv_screen_load(settings_screen);
#endif
      currentPage = 2;
    }

    shell_set_active_tab(page);
    xSemaphoreGive(lvglMutex);
  }
  // Populate the page's widgets after releasing lvglMutex: these update
  // functions each take the (non-recursive) mutex themselves, so calling
  // them while still holding it above would deadlock-timeout and silently
  // no-op (~100-200ms frozen UI per call, updates never applied).
  if (page == 1) {
    // Don't call updateLogDisplay here - it will update when events arrive
    updateHeader();
    updateStatusLine();
    updateContextPanel();
  } else if (page == 2) {
    updateSystemInfo();
  }
  // Reset timeout and turn on display on page switch
  wakeDisplay();
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
      wakeDisplay();  // wake AND reset the idle timer
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
      lastTouchTime = millis();  // start the short no-BLE display grace period
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
static void startOtaIfNeeded();

void checkDisplayTimeout()
{
  if (!displayOn) return;

  // No BLE host connected -> nothing to control, allow only a short on-time
  // (grace starts at the disconnect: checkBleConnection resets lastTouchTime).
  uint32_t limit = bleConnected ? DISPLAY_TIMEOUT_ACTIVE_MS : DISPLAY_TIMEOUT_NO_BLE_MS;

  uint32_t idle = millis() - lastTouchTime;
  bool inDimWindow = limit > DISPLAY_DIM_LEAD_MS &&
                     idle > (limit - DISPLAY_DIM_LEAD_MS);

  if (idle > limit) {
    setDisplayPower(false);      // stage 2: fully off
  } else if (inDimWindow && !displayDimmed) {
    setDisplayDim();             // stage 1: 30% for the final DISPLAY_DIM_LEAD_MS
  } else if (!inDimWindow && displayDimmed) {
    setDisplayPower(true);       // idle timer reset below the window → restore full
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

      startOtaIfNeeded();

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

static const char* wifiStatusToString(wl_status_t st) {
  switch (st) {
    case WL_IDLE_STATUS: return "IDLE";
    case WL_NO_SSID_AVAIL: return "NO_SSID";
    case WL_SCAN_COMPLETED: return "SCAN_DONE";
    case WL_CONNECTED: return "CONNECTED";
    case WL_CONNECT_FAILED: return "CONNECT_FAILED";
    case WL_CONNECTION_LOST: return "CONNECTION_LOST";
    case WL_DISCONNECTED: return "DISCONNECTED";
    default: return "UNKNOWN";
  }
}

static void logWsDiagnostics(const char* tag) {
  wl_status_t st = WiFi.status();
  int rssi = (st == WL_CONNECTED) ? WiFi.RSSI() : 0;
  size_t heapFree = esp_get_free_heap_size();
  size_t heapInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t heapPsram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  Serial.printf("[WS][diag] %s | wifi=%s rssi=%d dBm heap=%u int=%u psram=%u\n",
                tag, wifiStatusToString(st), rssi, (unsigned)heapFree,
                (unsigned)heapInternal, (unsigned)heapPsram);
}

static bool cargoHasSymbol(const char* symbol) {
  if (!symbol || !*symbol) return false;
  for (const auto& entry : status.cargo.inventory) {
    if (entry.name.equalsIgnoreCase(symbol)) return true;
  }
  return false;
}

static void startOtaIfNeeded() {
  if (otaInitialized) return;
  if (WiFi.status() != WL_CONNECTED) return;

  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.onStart([]() {
    Serial.println("[OTA] Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("[OTA] End");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    static unsigned int lastPct = 0;
    unsigned int pct = (progress * 100U) / total;
    if (pct != lastPct) {
      Serial.printf("[OTA] %u%%\n", pct);
      lastPct = pct;
    }
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error %u\n", error);
  });

  ArduinoOTA.begin();
  otaInitialized = true;
  Serial.println("[OTA] Ready (use WiFi IP above)");
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
void requestLoadingStatusWs();
void requestLogEntriesWs(int count);
void requestSystemWs();
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

    // Reset timeout and wake display on game traffic — but only while a BLE
    // host is connected (otherwise WS chatter keeps the display alive all
    // night) and not manually switched off via the BLACK button.
    if (bleConnected && !displayManualOff) wakeDisplay();

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
        updateStatusLine();  // replace "Waiting for events..." right away
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
      updateContextPanel();  // onFoot may have changed -> panel content (backpack/exploration)
    } else if (name && strcmp(name, "gameStateChange") == 0) {
      Serial.println("[WS] gameStateChange broadcast received, refreshing state");
      requestShipStatusWs();
    } else if (name && strcmp(name, "getLoadingStatus") == 0 && !msg.isNull()) {
      // Icarus journal import status: once complete, fetch the recent history
      bool complete = msg["loadingComplete"] | false;
      Serial.printf("[WS] LoadingStatus: complete=%d inProgress=%d\n",
                    complete, (bool)(msg["loadingInProgress"] | false));
      if (complete && !historyRequested && !historyLoaded) {
        historyRequested = true;
        // 50, not 100 like the web client: the ~100 KB response of count=100
        // crashed the network stack (abort on core 0, lwIP/String assembly in
        // internal RAM). 50 (~50 KB) is proven stable. Trade-off: a spammy
        // on-foot session can push FSSBodySignals out of the window - re-scan
        // the body (FSS/DSS) to re-pin it.
        requestLogEntriesWs(50);
      }
    } else if (name && strcmp(name, "getSystem") == 0 && !msg.isNull()) {
      // Current-system fallback: only adopt the name while we know nothing —
      // live journal events stay the authoritative source.
      const char* sysName = msg["name"] | "";
      if (sysName[0] && strcmp(sysName, "Unknown") != 0 &&
          status.currentSystem.length() == 0) {
        status.currentSystem = sysName;
        Serial.printf("[WS] getSystem: current system = %s\n", sysName);
        updateStatusLine();
      }
    } else if (name && strcmp(name, "getLogEntries") == 0 && msg.is<JsonArray>()) {
      // Recent journal entries, newest first. Replay oldest-first so the final
      // log/pins/system state reflects the present; side effects suppressed.
      JsonArray entries = msg.as<JsonArray>();
      int n = entries.size();
      Serial.printf("[WS] Replaying %d journal history entries\n", n);
      replayingHistory = true;
      for (int i = n - 1; i >= 0; i--) {
        JsonDocument eventDoc;
        eventDoc.set(entries[i]);
        handleEliteEvent("JOURNAL", eventDoc);
      }
      replayingHistory = false;
      historyLoaded = true;
      updateLogDisplay();  // single refresh after the whole replay
      printHeapStatus("after history replay");
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
        updateStatusLine();  // replace "Waiting for events..." right away
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

        status.cargo.inventory.clear();

        // Find drones in inventory list (case-insensitive symbol match)
        int drones = 0;
        int cargoNonDrone = 0;
        if (cargo["inventory"].is<JsonArrayConst>() || cargo["inventory"].is<JsonArray>()) {
          JsonArrayConst inv = cargo["inventory"].as<JsonArrayConst>();
          for (JsonVariantConst item : inv) {
            const char* symbol = item["symbol"];
            int count = item["count"].is<int>() ? item["count"].as<int>() : 0;
            if (symbol) {
              CargoEntry entry;
              entry.name = symbol;
              entry.count = count;
              status.cargo.inventory.push_back(entry);
              if (String(symbol).equalsIgnoreCase("drones")) {
                drones = count;
              } else {
                cargoNonDrone += count;
              }
            }
          }
        }
        status.cargo.dronesCount = drones;
        status.cargo.cargoCount = cargoNonDrone;
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
  
  // Reset display timeout on message (only while a BLE host is connected and
  // the display wasn't manually switched off)
  if (bleConnected && !displayManualOff) wakeDisplay();
  
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
      wsConnecting = false;
      wsReconnectDelayMs = 3000;   // reset backoff
      wsNextReconnect = 0;
      wsLastPong = millis();      // initialize pong timer on connect
      wsLastPing = wsLastPong;    // sync ping timer
      // Request initial ship status from Icarus terminal
      requestShipStatusWs();
      break;
      
    case WebsocketsEvent::ConnectionClosed:
      Serial.printf("[WS] Disconnected from server (info: %s)\n", data.c_str());
      logWsDiagnostics("closed");
      useWebSocket = false;
      wsConnecting = false;
      wsLastPong = 0;
      wsLastPing = 0;
      wsNextReconnect = millis() + wsReconnectDelayMs;
      wsReconnectDelayMs = std::min(WS_RECONNECT_DELAY_MAX, wsReconnectDelayMs * 2);
      break;
      
    case WebsocketsEvent::GotPing:
      Serial.println("[WS] Got ping");
      break;
      
    case WebsocketsEvent::GotPong:
      Serial.println("[WS] Got pong");
      wsLastPong = millis();
      break;
  }
}

void connectWebSocket() {
  if (wsConnecting) {
    return; // already trying
  }
  if (wsClient.available()) {
    return; // already connected
  }
  if ( WiFi.status() != WL_CONNECTED) {
    Serial.println("[WS] Cannot connect - WiFi not connected");
    return;
  }
  if (serverIP.length() > 0 && websocketPort > 0) {
    String wsUrl = "ws://" + serverIP + ":" + String(websocketPort);
    
    Serial.printf("[WS] Connecting to %s\n", wsUrl.c_str());
    
    wsClient.onMessage(onWebSocketMessage);
    wsClient.onEvent(onWebSocketEvent);
        wsConnecting = true;
        wsLastPing = 0;
        wsLastPong = millis(); // start fresh timer at connect attempt
        logWsDiagnostics("connect");
    if (wsClient.connect(wsUrl)) {
      Serial.println("[WS] Connection initiated");
          // Wait for onEvent to clear wsConnecting
    } else {
      Serial.println("[WS] Connection failed");
          wsConnecting = false;
          wsLastPong = 0;
          wsLastPing = 0;
          wsNextReconnect = millis() + wsReconnectDelayMs;
          wsReconnectDelayMs = std::min(WS_RECONNECT_DELAY_MAX, wsReconnectDelayMs * 2);
    }
  }
}

// Body identifier within the current system: "Synuefe EN-H d11-96 A 1" -> "A 1".
// Falls back to the full body name if it doesn't start with the system name.
static void shortBodyLabel(const char* bodyName, char* out, size_t outLen) {
  const char* p = bodyName;
  const char* sys = status.currentSystem.c_str();
  size_t sysLen = strlen(sys);
  if (sysLen > 0 && strncasecmp(bodyName, sys, sysLen) == 0) {
    p = bodyName + sysLen;
    while (*p == ' ') p++;
    if (*p == '\0') p = bodyName;  // the star itself: keep the full name
  } else {
    // System name unknown or mismatching: keep the distinctive TAIL of the
    // body name — a truncated head would make all bodies look identical.
    size_t len = strlen(bodyName);
    if (len >= outLen) p = bodyName + (len - (outLen - 1));
  }
  snprintf(out, outLen, "%s", p);
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
      
      updateContextPanel();
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
      updateContextPanel();
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
    updateContextPanel();  // onFoot may have changed -> panel content (backpack/exploration)
    
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
    updateContextPanel();
    
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

    // Exploration progress (data only; these events still get logged below)
    bool explTouched = false;
    if (event == "FSSDiscoveryScan") {
      explorationHonk(doc["BodyCount"] | 0);
      explTouched = true;
    } else if (event == "FSSAllBodiesFound") {
      explorationAllFound();
      explTouched = true;
    } else if (event == "Scan") {
      int prevFirst = status.exploration.firstDiscovered;
      explorationScan(doc["BodyID"] | -1,
                      doc["WasDiscovered"] | true,
                      doc["WasMapped"] | true);
      // First discovery (nobody scanned this body before): celebratory chime.
      if (status.exploration.firstDiscovered > prevFirst && !replayingHistory)
        pendingFirstDiscBeep = true;
      explTouched = true;
    } else if (event == "SAAScanComplete") {
      explorationMapped(doc["BodyID"] | -1);
      explTouched = true;
    }
    // Only refresh the panel when one of the exploration events above
    // actually touched its state - avoids a refresh for every journal
    // event (incl. high-volume FSSSignalDiscovered and replay entries).
    if (explTouched) updateContextPanel();

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
      // Arrived in a new system: this is the authoritative system name (the
      // WS status messages can lag behind), needed for the body pin labels.
      if (doc["StarSystem"].is<const char*>()) {
        status.currentSystem = doc["StarSystem"].as<const char*>();
        status.currentStation = "";
        updateStatusLine();
      }
      // Left the old system: pinned body signals are no longer relevant
      clearPinnedBodies();
      explorationReset();
      updateContextPanel();
      updateLogDisplay();
    } else if (event == "StartJump") {
      // Hyperspace charge engaged: the old system's data is stale NOW. The
      // event already names the target system, so show it right away.
      // (StartJump with JumpType "Supercruise" changes nothing.)
      if (strcmp(doc["JumpType"] | "", "Hyperspace") == 0) {
        clearPinnedBodies();
        explorationReset();
        updateContextPanel();
        if (doc["StarSystem"].is<const char*>()) {
          status.currentSystem = doc["StarSystem"].as<const char*>();
          status.currentStation = "";
          updateStatusLine();
        }
        snprintf(logBuf, sizeof(logBuf), "Jump: %s", doc["StarSystem"] | "?");
        addLogEntry(logBuf);  // also refreshes the (cleared) pinned lines
      }
    } else if (event == "Location") {
      // Login/relog: refresh the system name (does NOT clear the pins)
      if (doc["StarSystem"].is<const char*>()) {
        status.currentSystem = doc["StarSystem"].as<const char*>();
        updateStatusLine();
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
        
        // Trigger screen blink and urgent beeps (not for replayed history)
        if (!replayingHistory) {
          blinkScreen = true;
          blinkCount = 6;  // 3 blinks = 6 toggles
          lastBlinkTime = millis();
          beepMotherlode();  // Urgent triple beep
        }
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
            const char* symbol = mat["Name"].is<const char*>() ? mat["Name"].as<const char*>() : "";
            const char* matName = mat["Name_Localised"].is<const char*>() ? 
              mat["Name_Localised"].as<const char*>() : mat["Name"].as<const char*>();
            strncpy(tempBuf, matName, sizeof(tempBuf) - 1);
            replaceUmlauts(tempBuf);
            String display = String(tempBuf);

            // Highlight if already in cargo
            if (cargoHasSymbol(symbol)) {
              materials += "*";
            }
            materials += display;
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
    } else if (event == "FSSSignalDiscovered") {
      // Silent, high-volume: count landable stations by largest pad, no log line.
      if ((doc["IsStation"] | false) &&
          explorationStation(doc["SignalName"] | "", doc["SignalType"] | "")) {
        updateContextPanel();
      }
      return;  // consume: never reaches the generic log
    } else if (event == "FSSBodySignals" || event == "SAASignalsFound") {
      // Append signal counts per type, e.g. "FSSBodySignals Bi:1 Ge:2"
      // Label = first two chars of the type name ("$SAA_SignalType_Xxx;" -> "Xx")
      int len = snprintf(logBuf, sizeof(logBuf), "FSSBodySignals");
      int totalSignals = 0;
      int bioCount = 0, geoCount = 0, otherCount = 0;
      for (JsonObject sig : doc["Signals"].as<JsonArray>()) {
        int count = sig["Count"] | 0;
        if (count <= 0) continue;
        totalSignals += count;
        const char* type = sig["Type"] | "";
        if (strstr(type, "Biological")) bioCount += count;
        else if (strstr(type, "Geological")) geoCount += count;
        else otherCount += count;
        const char* p = strstr(type, "SignalType_");
        const char* name = p ? p + 11 : (sig["Type_Localised"] | type);
        char label[3] = {0, 0, 0};
        for (int j = 0; j < 2 && name[j] && name[j] != ';'; j++) label[j] = name[j];
        if (!label[0]) label[0] = '?';
        if (len < (int)sizeof(logBuf))
          len += snprintf(logBuf + len, sizeof(logBuf) - len, " %s:%d", label, count);
      }
      Serial.printf("[BIOSIG] '%s' (Signals is array: %d, size: %d)\n",
        logBuf, doc["Signals"].is<JsonArray>(), (int)doc["Signals"].as<JsonArray>().size());
      // Pin the body (by BodyID) with its signals at the top of the log view;
      // a later SAASignalsFound for the same body just refreshes the counts.
      if (totalSignals > 0 && !doc["BodyName"].isNull()) {
        char bodyLabel[20];
        shortBodyLabel(doc["BodyName"] | "", bodyLabel, sizeof(bodyLabel));
        pinBodySignals(doc["BodyID"] | -1, bodyLabel, bioCount, geoCount, otherCount);
        // DSS mapping reveals WHICH bio genuses live here -> genus detail line
        if (doc["Genuses"].is<JsonArray>()) {
          for (JsonObject g : doc["Genuses"].as<JsonArray>()) {
            const char* gl = g["Genus_Localised"] | "";
            if (gl[0]) addPinnedGenus(doc["BodyID"] | -1, gl);
          }
        }
      }
      addLogEntry(logBuf);  // also refreshes the pinned lines
      // One short beep per detected signal (any category). Queued here and
      // played evenly spaced by loop() so this WS task never blocks on audio.
      // Only for a live FSS discovery — DSS re-announcements and the boot-time
      // history replay stay silent.
      if (event == "FSSBodySignals" && !replayingHistory) {
        pendingSignalBeeps += totalSignals;
        if (pendingSignalBeeps > 12) pendingSignalBeeps = 12;  // cap the serenade
      }
    } else if (event == "ScanOrganic") {
      // Genetic sampler progress: "Log" = 1st sample of a new organism,
      // "Sample" = 2nd/3rd, "Analyse" = complete -> checks off one Bio.
      const char* scanType = doc["ScanType"] | "";
      const char* genus = doc["Genus_Localised"] | "";
      if (!genus[0]) genus = doc["Genus"] | "";
      organicScanProgress(doc["Body"] | -1, genus, scanType);
      if (strcmp(scanType, "Log") == 0) {
        if (!replayingHistory) beepClick();
        snprintf(logBuf, sizeof(logBuf), "Organic found: %s", genus);
        addLogEntry(logBuf);  // also refreshes the pinned lines
      } else if (strcmp(scanType, "Analyse") == 0) {
        if (!replayingHistory) beepClick();
        snprintf(logBuf, sizeof(logBuf), "Organic done: %s", genus);
        addLogEntry(logBuf);
      } else if (!replayingHistory) {
        updateLogDisplay();  // 2nd sample: genus state may have changed
      }
    } else if (event == "Backpack") {
      // Authoritative on-foot backpack snapshot (fires on disembark etc.).
      // The old eventType=="BACKPACK" branch was the dead UDP protocol.
      status.backpack.healthpack = 0;
      status.backpack.energycell = 0;
      for (JsonObject item : doc["Consumables"].as<JsonArray>()) {
        const char* n = item["Name"] | "";
        int count = item["Count"] | 0;
        if (strcmp(n, "healthpack") == 0) status.backpack.healthpack = count;
        else if (strcmp(n, "energycell") == 0) status.backpack.energycell = count;
      }
      Serial.printf("[BACKPACK] H:%d E:%d\n",
                    status.backpack.healthpack, status.backpack.energycell);
      updateContextPanel();
    } else if (event == "BackpackChange") {
      for (JsonObject item : doc["Added"].as<JsonArray>()) {
        const char* n = item["Name"] | "";
        int count = item["Count"] | 0;
        if (strcmp(n, "healthpack") == 0) status.backpack.healthpack += count;
        else if (strcmp(n, "energycell") == 0) status.backpack.energycell += count;
      }
      for (JsonObject item : doc["Removed"].as<JsonArray>()) {
        const char* n = item["Name"] | "";
        int count = item["Count"] | 0;
        if (strcmp(n, "healthpack") == 0) status.backpack.healthpack -= count;
        else if (strcmp(n, "energycell") == 0) status.backpack.energycell -= count;
      }
      if (status.backpack.healthpack < 0) status.backpack.healthpack = 0;
      if (status.backpack.energycell < 0) status.backpack.energycell = 0;
      updateContextPanel();
    } else if (event == "UseConsumable") {
      const char* n = doc["Name"] | "";
      if (strcmp(n, "healthpack") == 0 && status.backpack.healthpack > 0)
        status.backpack.healthpack--;
      else if (strcmp(n, "energycell") == 0 && status.backpack.energycell > 0)
        status.backpack.energycell--;
      updateContextPanel();
    } else if (event == "Disembark") {
      status.onFoot = true;
      updateContextPanel();  // switch panel to BACKPACK / ON FOOT
      addLogEntry("Disembark");
    } else if (event == "Embark") {
      status.onFoot = false;
      updateContextPanel();  // switch panel back to EXPLORATION
      addLogEntry("Embark");
    } else if (event == "Shutdown" || event == "CarrierJump") {
      // Session over / carrier moved: pinned body signals are stale
      clearPinnedBodies();
      explorationReset();
      updateContextPanel();
      addLogEntry(event.c_str());
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
      updateContextPanel();  // onFoot may have changed -> panel content (backpack/exploration)
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
  } else if (eventType == "CARGO") {
    // Parse cargo inventory
    cargoInfo.usedSpace = doc["Count"];
    cargoInfo.dronesCount = 0;
    cargoInfo.cargoCount = 0;
    status.cargo.inventory.clear();
    
    if (!doc["Inventory"].isNull()) {
      JsonArray inventory = doc["Inventory"];
      for (JsonObject item : inventory) {
        String name = item["Name"].as<String>();
        int count = item["Count"];
        CargoEntry entry;
        entry.name = name;
        entry.count = count;
        status.cargo.inventory.push_back(entry);

        if (name.equalsIgnoreCase("drones")) {
          cargoInfo.dronesCount = count;
        } else {
          cargoInfo.cargoCount += count;
        }
      }
    }

    updateHeader();
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
    uint32_t now = millis();

    // Trigger reconnect if allowed by backoff and WiFi is up
    if (!wsClient.available() && !wsConnecting && now >= wsNextReconnect && WiFi.status() == WL_CONNECTED) {
      connectWebSocket();
    }

    // Poll WebSocket for incoming messages
    if (wsClient.available()) {
      wsClient.poll();

      // Ping every 20s to keep connection alive; reset pong timer when sending
      if (now - wsLastPing > 20000) {
        wsClient.ping();
        wsLastPing = now;
        if (wsLastPong == 0) wsLastPong = now; // start grace window on first ping
      }

      // Require both a sent ping and a pong gap before timing out to avoid immediate disconnect after first pong
      if (wsLastPing > 0 && wsLastPong > 0 && (now - wsLastPong > 90000) && (now - wsLastPing > 90000)) {
        Serial.println("[WS] No pong in 90s, forcing reconnect");
        logWsDiagnostics("pong-timeout");
        wsClient.close();
        useWebSocket = false;
        wsConnecting = false;
        wsNextReconnect = now + wsReconnectDelayMs;
        wsReconnectDelayMs = std::min(WS_RECONNECT_DELAY_MAX, wsReconnectDelayMs * 2);
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

    // Bring up OTA once WiFi is ready
    startOtaIfNeeded();

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
  // Native USB-CDC: when the board hangs on a PC with no terminal reading,
  // the TX FIFO fills and every print BLOCKS up to the default 100 ms. With
  // logging inside lvglMutex holds that starved the render loop (display
  // froze until a monitor drained the buffer). Drop instead of block:
  Serial.setTxTimeoutMs(0);
  delay(100);
  Serial.println("\n\n[BOOT] Starting ESP32S3 BLE Joypad...");
  Serial.printf("[VERSION] Build: %s %s\n", __DATE__, __TIME__);

  // Keep the backlight firmly off during early boot (the panel GRAM still
  // holds the image from before the reset). The LEDC PWM attach happens only
  // after display init (see below).
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, LOW);

  // Check PSRAM availability
  Serial.printf("[PSRAM] Available: %s\n", psramFound() ? "YES" : "NO");
  if (psramFound()) {
    Serial.printf("[PSRAM] Total: %d bytes\n", ESP.getPsramSize());
    Serial.printf("[PSRAM] Free: %d bytes\n", ESP.getFreePsram());
  }
  
  // Allocate the log buffer from PSRAM (fallback to internal if unavailable).
  // The LVGL draw buffers are owned and allocated by Display::init() (see display.cpp).
  Serial.println("[MEM] Allocating buffers...");
  logText = (char*)heap_caps_malloc(LOG_TEXT_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!logText) {
    Serial.println("[MEM] PSRAM allocation failed for log buffer, trying internal RAM...");
    logText = (char*)heap_caps_malloc(LOG_TEXT_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }

  if (!logText) {
    Serial.println("ERROR: Failed to allocate log buffer!");
    while(1) delay(1000);
  }

  Serial.printf("[MEM] Log buffer allocated: %d bytes\n", LOG_TEXT_SIZE);
  Serial.printf("[HEAP] Free internal RAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  if (psramFound()) {
    Serial.printf("[PSRAM] Free PSRAM: %d bytes\n", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
  }
  delay(100);
  // pinMode(BUZZER_PIN, OUTPUT);  // Disabled - buzzer uses LEDC on ESP32S3
  
  // beepBootup();  // Disabled - tone() uses LEDC
  
  // Initialize display power management
  Serial.println("[INIT] Setting up display power management...");
  lastTouchTime = millis();
  lastBleActiveTime = millis();
  bleDisconnectedTime = millis();

  // Wire (GT911 touch) is initialized inside Display::init() via bb_captouch.
  // Wire1 carries the PCF8575 with the TTP223 pads.
  Serial.println("[I2C] Initializing Wire1 (PCF8575/TTP223)...");
  bool w1ok = Wire1.begin(PCF_SDA, PCF_SCL, I2C_SPEED);
  Serial.printf("[I2C] Wire1.begin(sda=%d, scl=%d, %lu Hz) -> %s\n",
                PCF_SDA, PCF_SCL, (unsigned long)I2C_SPEED, w1ok ? "OK" : "FAILED");
  scanI2CBus(Wire1, "Wire1");

  // Display first: clears the stale panel content and shows the boot splash
  // while the rest of setup() (WiFi, audio, BLE) runs.
  Serial.println("[DISPLAY] Initializing display and LVGL...");
  disp.init();
  disp.setTouchCallback(handleTouchActivity);
  // Attach the backlight PWM after panel init so nothing re-muxes GPIO1 later.
  ledcAttach(BL_PIN, 5000, 8);
  ledcWrite(BL_PIN, BL_DUTY_FULL);  // reveal the splash

  Serial.println("[WIFI] Connecting...");
  WifiConnect();

  // NS4168 mono I2S amp: no enable pin, no codec, no MCLK - just feed it I2S.
  Serial.println("[I2S] Initializing I2S audio (NS4168)...");

  // Initialize I2S (new IDF i2s_std driver): create the TX channel, configure
  // standard Philips slot format + clocking, then enable it.
  Serial.println("[I2S] Configuring I2S driver (std)...");
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  // Equivalent of the legacy tx_desc_auto_clear=true: send silence once the ring
  // is drained instead of endlessly replaying the last written samples.
  chan_cfg.auto_clear = true;
  esp_err_t i2s_err = i2s_new_channel(&chan_cfg, &i2s_tx_chan, NULL);
  if (i2s_err != ESP_OK) {
    Serial.printf("[I2S] ERROR: new_channel failed: %d\n", i2s_err);
  } else {
    i2s_std_config_t std_cfg = {
      .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,       // NS4168 needs no MCLK
        .bclk = (gpio_num_t)I2S_BCK,   // GPIO42
        .ws   = (gpio_num_t)I2S_WS,    // GPIO2 (LRCLK)
        .dout = (gpio_num_t)I2S_DOUT,  // GPIO41
        .din  = I2S_GPIO_UNUSED,
        .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
      },
    };

    if (i2s_channel_init_std_mode(i2s_tx_chan, &std_cfg) != ESP_OK) {
      Serial.println("[I2S] ERROR: init_std_mode failed");
    } else if (i2s_channel_enable(i2s_tx_chan) != ESP_OK) {
      Serial.println("[I2S] ERROR: channel enable failed");
    } else {
      soundSetInitialized(true);  // Mark I2S as ready for tones
      Serial.println("[I2S] Audio driver initialized successfully (std)");
    }
  }

  Serial.println("[PCF8575] Init TTP223 pads (Wire1 @0x20)...");
  pcf = new PCF8575(PCF8575_ADDR, &Wire1);
  pcfAvailable = pcf->begin();
  if (!pcfAvailable) Serial.println("[PCF8575] ERROR: not found on Wire1 (will retry)");

  Serial.println("[BLE] Creating and configuring gamepad...");
  bleGamepad = new BleGamepad("CoolJoyBLE", "leDev", 100);
  bleGamepadConfig.setButtonCount(20);
  bleGamepadConfig.setIncludeRxAxis(false);
  bleGamepadConfig.setWhichAxes(false, false, false, false, false, false, false, false);
  bleGamepadConfig.setHatSwitchCount(0);

  Serial.println("[BLE] Starting BLE gamepad...");
  bleGamepad->begin(&bleGamepadConfig);

  theme_init();
  Serial.println("[UI] Creating fighter UI...");
  create_fighter_ui();
  Serial.println("[UI] Creating logviewer UI...");
  create_logviewer_ui();
  Serial.println("[UI] Creating MFD shell...");
  create_shell_ui();
  shell_set_active_tab(currentPage);
  Serial.println("[UI] Loading logviewer screen...");
  lv_screen_load(logviewer_screen);

  // Replace the boot splash with the first full UI frame.
  lv_refr_now(NULL);
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
    1,                  // Priority (align with loop task to avoid starving idle)
    &msgTaskHandle,     // Task handle
    1                   // Core 1 (APP_CPU) to keep core0 idle/WiFi happy
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

  // Drain queued FSSBodySignals beeps, evenly spaced so they read as one
  // beep per signal instead of a continuous tone.
  static uint32_t lastSignalBeepTime = 0;
  if (pendingSignalBeeps > 0 && millis() - lastSignalBeepTime >= 150) {
    beepSignal();
    pendingSignalBeeps--;
    lastSignalBeepTime = millis();
  }

  // First discovery: three-tone chime (queued by the WS task's Scan hook).
  if (pendingFirstDiscBeep) {
    pendingFirstDiscBeep = false;
    beepFirstDiscovery();
  }

  checkBleConnection();
  checkDisplayTimeout();
  checkWifiConnection();
  // checkMessages();  // Now handled by loop2 on Core 0

  // Service deferred settings-UI requests here (outside the LVGL mutex)
  if (reqRestartWifi) {
    reqRestartWifi = false;
    Serial.println("[SETTINGS] Restarting WiFi...");
    WiFi.disconnect();
    wifiWasConnected = false;  // let checkWifiConnection re-establish WS after reconnect
    wifiMulti.run();
  }
  if (reqRestartWebSocket) {
    reqRestartWebSocket = false;
    Serial.println("[SETTINGS] Restarting WebSocket...");
    if (wsClient.available()) {
      wsClient.close();
    }
    connectWebSocket();
  }
  if (reqPageSwitch >= 0) {
    int p = reqPageSwitch;
    reqPageSwitch = -1;
    if (p != currentPage) switchToPage(p);
  }

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
  
  // Request initial state (and, once, the journal history) every 10 seconds
  // until received
  if ((!summaryReceived || !historyLoaded) &&
      (currentTime - lastSummaryRequest) > SUMMARY_REQUEST_INTERVAL) {
    if (wsClient.available()) {
      if (!summaryReceived) {
        requestCmdrStatusWs();
        requestShipStatusWs();
        requestCmdrProfileWs();
        requestNavRouteWs();
      }
      if (!historyLoaded && !historyRequested) {
        requestLoadingStatusWs();
      }
    } else {
      // Try to connect WS if not already
      connectWebSocket();
    }
    lastSummaryRequest = currentTime;
  }

  // Fallback for the current system name: after a long exploration session the
  // 50-entry replay window may hold no FSDJump/Location, leaving the footer on
  // "Waiting for events...". Once the replay is done, ask Icarus directly
  // (getSystem -> message.name) until we know where we are.
  static uint32_t lastSystemRequest = 0;
  if (historyLoaded && status.currentSystem.length() == 0 &&
      (currentTime - lastSystemRequest) > SUMMARY_REQUEST_INTERVAL) {
    if (wsClient.available()) requestSystemWs();
    lastSystemRequest = currentTime;
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

  // Handle OTA updates
  if (WiFi.status() == WL_CONNECTED) {
    if (!otaInitialized) {
      startOtaIfNeeded();
    }
    ArduinoOTA.handle();
  }
  
  // Handle screen blinking for motherlode detection
  if (blinkScreen && blinkCount > 0) {
    if (millis() - lastBlinkTime > 200) {  // Blink every 200ms
      if (displayOn) {
        ledcWrite(BL_PIN, 0);
        displayOn = false;
      } else {
        ledcWrite(BL_PIN, BL_DUTY_FULL);
        displayOn = true;
      }
      blinkCount--;
      lastBlinkTime = millis();

      if (blinkCount == 0) {
        blinkScreen = false;
        ledcWrite(BL_PIN, BL_DUTY_FULL);
        displayOn = true;
        displayDimmed = false;
      }
    }
  }

  // --- TTP223 page navigation (PCF8575 @0x20 on Wire1, active-high) ---
  // top = previous page, bottom = next page (wrap over the 3 pages),
  // middle = display off. While the display is dark, ANY pad only wakes it.
  // Without the expander (bad wiring / not fitted) the poll is skipped and a
  // reconnect is attempted every 5 s instead of spamming I2C errors at 5 ms.
  if (!pcfAvailable) {
    static uint32_t lastPcfRetry = 0;
    if (millis() - lastPcfRetry >= 5000) {
      lastPcfRetry = millis();
      pcfAvailable = pcf->begin();
      if (pcfAvailable) Serial.println("[PCF8575] reconnected");
    }
    delay(5);
    return;
  }
  static uint16_t lastTtpState = 0;
  uint16_t ttpState = pcf->read16();
  uint16_t rising = ttpState & ~lastTtpState;
  lastTtpState = ttpState;

  bool topEdge    = (rising >> TTP_TOP_BIT) & 1;
  bool midEdge    = (rising >> TTP_MID_BIT) & 1;
  bool bottomEdge = (rising >> TTP_BOTTOM_BIT) & 1;

  if (topEdge || midEdge || bottomEdge) {
    if (!displayOn) {
      wakeDisplay();               // dark display: first press only wakes
    } else if (topEdge) {
      switchToPage((currentPage + 2) % 3);   // previous
      Serial.printf("[TTP] prev -> page %d\n", currentPage);
    } else if (bottomEdge) {
      switchToPage((currentPage + 1) % 3);   // next
      Serial.printf("[TTP] next -> page %d\n", currentPage);
    } else if (midEdge) {
      Serial.println("[TTP] display off");
      setDisplayPower(false);
      displayManualOff = true;     // stays dark until touch/TTP/BLE wake
    }
  }

  delay(5);
}

static void jump_overlay_zoom_exec(void* obj, int32_t value) {
  // Avoid zero zoom which triggers divide-by-zero in LVGL transform math
  if (value < 1) value = 1;
  lv_obj_set_style_transform_scale(static_cast<lv_obj_t*>(obj), value, LV_PART_MAIN);
}

static void jump_overlay_opa_exec(void* obj, int32_t value) {
  lv_obj_set_style_opa(static_cast<lv_obj_t*>(obj), (lv_opa_t)value, LV_PART_MAIN);
}

static void jump_overlay_anim_ready(lv_anim_t* anim) {
  lv_obj_t* obj = static_cast<lv_obj_t*>(anim->var);
  if (obj) {
    lv_obj_delete_async(obj);
    if (obj == jump_overlay_label) {
      jump_overlay_label = nullptr;
    }
  }
}

void showJumpOverlay(int jumps) {
  if (!lvglMutex) return;
  if (lvglMutex && xSemaphoreTake(lvglMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
    if (jump_overlay_label) {
      lv_obj_delete(jump_overlay_label);
      jump_overlay_label = nullptr;
    }
    lv_obj_t* parent = lv_layer_top();  // persistent layer: survives page fades
    jump_overlay_label = lv_label_create(parent);
    lv_label_set_text_fmt(jump_overlay_label, "%d", jumps);
    lv_obj_set_style_text_color(jump_overlay_label, LV_COLOR_FG, 0);
    lv_obj_set_style_text_font(jump_overlay_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(jump_overlay_label, LV_ALIGN_TOP_MID, -SHELL_RAIL_W / 2,
                 CONTENT_Y + CONTENT_H / 2 - 20);
    lv_obj_set_style_opa(jump_overlay_label, LV_OPA_COVER, 0);
    lv_obj_set_style_transform_scale(jump_overlay_label, 768, LV_PART_MAIN);
    lv_obj_update_layout(jump_overlay_label);
    int32_t pivotX = lv_obj_get_width(jump_overlay_label) / 2;
    int32_t pivotY = lv_obj_get_height(jump_overlay_label) / 2;
    lv_obj_set_style_transform_pivot_x(jump_overlay_label, pivotX, LV_PART_MAIN);
    lv_obj_set_style_transform_pivot_y(jump_overlay_label, pivotY, LV_PART_MAIN);

    lv_anim_t zoom_anim;
    lv_anim_init(&zoom_anim);
    lv_anim_set_var(&zoom_anim, jump_overlay_label);
    lv_anim_set_values(&zoom_anim, 768, 0);
    lv_anim_set_duration(&zoom_anim, 1000);
    lv_anim_set_path_cb(&zoom_anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&zoom_anim, jump_overlay_zoom_exec);
    lv_anim_set_completed_cb(&zoom_anim, jump_overlay_anim_ready);
    lv_anim_start(&zoom_anim);

    lv_anim_t fade_anim;
    lv_anim_init(&fade_anim);
    lv_anim_set_var(&fade_anim, jump_overlay_label);
    lv_anim_set_values(&fade_anim, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&fade_anim, 1000);
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
  if (changed && !replayingHistory) {  // no overlay animation for old events
    pendingJumpValue = newValue;
    pendingJumpOverlay = true;
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

// Ask Icarus whether the journal import is complete (gates the history replay)
void requestLoadingStatusWs() {
  if (!wsClient.available()) {
    return;
  }

  String reqId = String("esp32-load-") + String(millis());
  String payload = String("{\"requestId\":\"") + reqId + "\",\"name\":\"getLoadingStatus\",\"message\":null}";
  wsClient.send(payload);
  Serial.println("[WS] Sent getLoadingStatus request");
}

// Ask Icarus for the current system (response: message.name). Boot fallback
// for when the replayed history contains no system-setting event. NOTE: the
// response can be large in busy systems (bodies/stations arrays), so this is
// only requested while no system name is known.
void requestSystemWs() {
  if (!wsClient.available()) {
    return;
  }

  String reqId = String("esp32-sys-") + String(millis());
  String payload = String("{\"requestId\":\"") + reqId + "\",\"name\":\"getSystem\",\"message\":{}}";
  wsClient.send(payload);
  Serial.println("[WS] Sent getSystem request");
}

// Fetch the most recent journal entries to rebuild log/pins/state after boot
void requestLogEntriesWs(int count) {
  if (!wsClient.available()) {
    return;
  }

  String reqId = String("esp32-hist-") + String(millis());
  String payload = String("{\"requestId\":\"") + reqId +
                   "\",\"name\":\"getLogEntries\",\"message\":{\"count\":" + String(count) + "}}";
  wsClient.send(payload);
  Serial.printf("[WS] Sent getLogEntries request (count=%d)\n", count);
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