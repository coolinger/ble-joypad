# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for an ESP32-S3 touchscreen controller for *Elite Dangerous*. It receives live telemetry from [Icarus Terminal](https://github.com/iaincollins/icarus) over WebSocket, renders ship/commander state on a 2.8" LVGL UI, plays I2S audio cues, and exposes 12 physical buttons (via a PCF8575 expander) as a BLE HID gamepad. Target board: Freenove ESP32-S3 WROOM (FNK0104B), 8 MB PSRAM.

## Build / flash / monitor

PlatformIO project, single environment `default`. **`pio` is not on PATH** on this machine — use the full path:

```bash
# build
"d:\pio\penv\Scripts\platformio.exe" run
# upload over USB CDC
"d:\pio\penv\Scripts\platformio.exe" run -t upload
# serial monitor (115200)
"d:\pio\penv\Scripts\platformio.exe" device monitor
# clean
"d:\pio\penv\Scripts\platformio.exe" run -t clean
```

There is no test suite. OTA upload is also available at runtime via `ArduinoOTA` once the device is on WiFi.

## Toolchain / platform gotchas

- The `platform` in [platformio.ini](platformio.ini) is the **pioarduino fork** (pinned release 55.03.39 → Arduino-Core 3.3.9 / ESP-IDF 5.5.4), *not* stock `espressif32` (which is frozen at IDF 4.4). The pre-migration config lives in git history; what the upgrade broke and how it was re-fixed is summarized in [CHANGELOG.md](CHANGELOG.md).
- `lvgl`, `TFT_eSPI`, and `FT6336U_CTP_Controller` are **vendored in [lib/](lib/)** and take precedence over any `lib_deps` versions. Note these three are **gitignored** (not committed), so a fresh clone won't build until they're restored. LVGL is configured by [lv_conf.h](lv_conf.h) at the repo root; TFT_eSPI by [src/User_Setup.h](src/User_Setup.h) (and [lib/TFT_eSPI_Setups](lib/TFT_eSPI_Setups)). Changing display/UI behavior usually means editing these config files, not the library source.
- `ESP32-BLE-Gamepad` is **also vendored in [lib/](lib/ESP32-BLE-Gamepad), but committed** (unlike the three above) because it carries a local patch — see the next bullet. It is therefore *not* in `lib_deps`; only its NimBLE dependency is, pinned to `^2.0`.
- **I2S uses the legacy `driver/i2s.h` API deliberately** ([src/sound.cpp](src/sound.cpp), I2S init in [src/main.cpp](src/main.cpp)). On IDF 5.x this header still ships under `driver/deprecated/` and works (with deprecation warnings). The `ESP_I2S` / `I2SClass` path is *not* wired up — don't assume `USE_NEW_I2S_API` selects a working alternate path.
- **The `ESP32-BLE-Gamepad` patch:** its `BleNUS.h`/`BleNUS.cpp` rely on `Arduino.h` being included transitively (true under NimBLE 1.x, *not* under the NimBLE 2.x the new core pulls in), so they fail with `delay`/`Serial`/`ltoa`/`dtostrf` "not declared". The fix is one line — `#include <Arduino.h>` in [lib/ESP32-BLE-Gamepad/BleNUS.h](lib/ESP32-BLE-Gamepad/BleNUS.h). To keep that fix under version control (rather than in the throwaway `.pio/`), the whole library is vendored into `lib/` and committed. **If you ever re-add `lemmingDev/ESP32-BLE-Gamepad` to `lib_deps`, the registry copy will shadow the vendored one and the error returns** — keep it vendored, or re-apply the include.

## Architecture (the big picture)

Almost everything lives in [src/main.cpp](src/main.cpp) (~2300 lines), which orchestrates networking, UI updates, input, and audio. The other `src/` modules are extractions from it; the screen modules own UI *construction*, but main.cpp owns most of the *update* logic.

**Dual-core split (FreeRTOS):**
- `loop()` (Arduino task, core 1): drives `lv_timer_handler()` rendering, reads buttons, sends BLE reports, handles OTA / WiFi / battery / display-timeout.
- `loop2()` (pinned to core 0, created in `setup()` as `msgTaskHandle`): tight 10 ms poll of the WebSocket via `checkMessages()`.
- **All LVGL access is guarded by `lvglMutex`.** Any function that touches LVGL widgets (the many `update*` / `switchToPage` / overlay functions) takes this mutex first. New UI code must do the same, because rendering and data updates run on different cores.

**Data flow:** WebSocket message → `onWebSocketMessage` → `handleEliteEvent(eventType, doc)` parses Elite/Icarus JSON → mutates the single global **`StatusModel status`** (defined in [src/gamedata.h](src/gamedata.h)) → screen `update*` functions read `status` and push into LVGL widgets. `StatusModel` is the single source of truth (cargo, fuel, hull, shields, nav, location, credits, etc.); legacy `#define` aliases like `cargoInfo`/`fuelInfo` map onto `status.*` for migration convenience.

**Screens / pages:** three pages selected by `currentPage` and `switchToPage(int)`:
- 0 = Fighter command pad ([src/screens/fighter.cpp](src/screens/fighter.cpp))
- 1 = Log viewer + status bars ([src/screens/info.cpp](src/screens/info.cpp), the default start page)
- 2 = System/settings ([src/screens/system.cpp](src/screens/system.cpp))

Each screen module exposes `create_*_ui()` plus `extern lv_obj_t*` widget handles that main.cpp updates.

**Input:** the PCF8575 (`pcf->read16()`, active-low) is polled in `loop()`. The three SILVER buttons (indices 0–2) switch pages locally; buttons 3–11 are forwarded to `bleGamepad->press()/release()` only when BLE is connected. Button-to-pin mapping is `buttonPins[12]` near the top of main.cpp.

**Audio:** `playTone()` in [src/sound.cpp](src/sound.cpp) synthesizes sine beeps and writes them via legacy `i2s_write`. `soundSetInitialized(true)` is called after I2S init in main.cpp gates playback. [src/es8311.cpp](src/es8311.cpp) is an ES8311 codec driver for board variants that use it.

## Configuration

[src/config.h](src/config.h) holds **hardcoded** WiFi SSID/password, the default Icarus server IP/port (`DEFAULT_SERVER_IP`, `DEFAULT_WEBSOCKET_PORT`), hostname, and display geometry. Update these for a different network/host. USB mode is forced to native CDC via `build_flags` (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`) — don't also set the equivalent `board_build.*` options or you get "redefined" warnings.

Pin assignments live across [src/config.h](src/config.h), [src/display.h](src/display.h) (I2C/touch pins), and the button defines in main.cpp; [GPIOs.md](GPIOs.md) documents the physical wiring.
