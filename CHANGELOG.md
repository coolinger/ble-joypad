# BLE Joypad - Changelog

## Toolchain Migration - pioarduino / ESP-IDF 5.5 (June 2026)

Moved off the now-frozen stock `espressif32` platform (Arduino core 2.0.x / ESP-IDF 4.4)
onto the maintained **pioarduino** fork. This bumped the underlying SDK from ESP-IDF 4.4
to **5.5.4** (Arduino-Core 3.3.9), which required several source-level fixes.

### Platform / build
- `platform` in `platformio.ini` now points at the pioarduino release zip
  (`55.03.39` → Arduino-Core 3.3.9 / ESP-IDF 5.5.4) instead of `espressif32`.
- Added `src/idf_component.yml` declaring `idf: '>=5.1'`.
- Unpinned `earlephilhower/ESP8266Audio` (was `@2.2.0`, incompatible with the new core).
- USB CDC is forced via build flags (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`).

### Source fixes forced by the SDK bump
- **I2S**: ESP-IDF 5.x no longer auto-selects the legacy driver. Removed the half-finished
  `USE_NEW_I2S_API` switch in `main.cpp` and always include the legacy `driver/i2s.h`
  (still shipped under `driver/deprecated/`, still functional). Matches `sound.cpp`.
- **USB power detection**: the old `is_plugged_usb()` peeked the USB-Serial-JTAG frame
  register via `DR_REG_USB_SERIAL_JTAG_BASE`, which the new core no longer exposes. Replaced
  with TP4056 CHRG-pin sensing on GPIO2 (`BATPIN`) plus an ADC threshold fallback. Battery
  sampling interval shortened 30 s → 5 s.
- **ESP32-BLE-Gamepad / NimBLE 2.x**: NimBLE 1.x used to include `Arduino.h` transitively;
  2.x does not, so the library's `BleNUS` fails to compile (`delay`/`Serial`/`ltoa`/`dtostrf`
  undeclared). Fixed by adding `#include <Arduino.h>` to `BleNUS.h`. *(Initially patched in
  the throwaway `.pio/` dir; subsequently made durable by vendoring the library into `lib/` —
  see the following commit.)*

## Version 2.0.0 - Major Update (November 2025)

### New Features

#### Elite Dangerous Log Stream Integration
- **UDP Listener**: Receives Elite Dangerous game events on port 12345
- **Event Types Supported**:
  - `JOURNAL` - Game journal events
  - `STATUS` - Real-time ship status
  - `CARGO` - Cargo hold inventory
  - `NAVROUTE` - Navigation route information

#### Dual Page LVGL Interface
- **Page 1: Fighter Control** (Original functionality)
  - Button matrix for Elite Dangerous fighter commands
  - Swipe left to switch to log viewer
  
- **Page 2: Log Viewer** (New!)
  - Real-time event log display (last 5 events)
  - Swipe right to return to fighter control

#### Log Viewer Features

**Header Bar (Top)**:
- Jump count from navigation route
- Fuel bar (orange) showing current fuel percentage
- Hull bar (green) showing hull integrity

**Event Log Area (Center)**:
- Scrolling display of recent Elite Dangerous events
- Special handling for ProspectedAsteroid events
- **Motherlode Detection**: Screen blinks 3 times when a motherlode material is detected

**Cargo Bar (Bottom - 40px)**:
- Visual bar showing cargo capacity utilization
- Displays total capacity, used space, and drone count
- Distinguishes between regular cargo and drones

### Technical Changes

#### Memory Optimizations
- Reduced LVGL buffer from 1/20 to 1/50 screen size
- Switched from String to fixed-size char arrays
- Reduced event log from 10 to 5 entries
- Optimized UDP buffer size (800 bytes)
- Event log entries limited to 60 characters

#### Dependencies Added
- ArduinoJson 7.2.0 for JSON parsing

#### Event Processing
- **ProspectedAsteroid**: 
  - Detects motherlode materials
  - Triggers 3-blink screen flash
  - Logs material name (localized)
  
- **LoadGame/Loadout**: 
  - Captures fuel capacity
  - Captures cargo capacity
  
- **STATUS**: 
  - Updates fuel level
  - Updates cargo count
  
- **CARGO**: 
  - Parses inventory
  - Separates drones from regular cargo
  
- **NAVROUTE**: 
  - Tracks remaining jumps

### UI/UX Improvements
- Touch gesture support for page switching
- Swipe left/right to navigate between pages
- Display remains on during BLE connection
- Real-time status updates in log viewer

### Build Information
- RAM Usage: 37.9% (124,344 bytes)
- Flash Usage: 69.3% (1,362,997 bytes)
- Platform: ESP32 (Arduino framework 2.0.14)

### Notes
- WiFi must be configured for UDP reception
- Default UDP port: 12345
- Compatible with Elite Dangerous log streamer applications
