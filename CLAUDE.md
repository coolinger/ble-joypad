# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for an ESP32-S3 touchscreen controller for *Elite Dangerous*. It receives live telemetry from [Icarus Terminal](https://github.com/iaincollins/icarus) over WebSocket, renders ship/commander state on a 4.3" LVGL UI, plays I2S audio cues, and exposes an on-screen fighter pad + 3 TTP223 pads (page nav/display off) as a BLE HID gamepad. Target board: Guition JC4827W543 CYD (4.3" 480×272 NV3041A QSPI, GT911 touch, ESP32-S3 N4R8).

## Build / flash / monitor

PlatformIO project, two environments: `default` (USB upload) and `ota` (extends `default`, only swaps the upload path to ArduinoOTA → `FighterController.fritz.box:3232`). **`[env:ota]` is currently INACTIVE**: the JC4827W543's 4 MB flash uses `huge_app.csv`, which has no `ota_0`/`ota_1` slots, so `espota` has nowhere to write. **The usual workflow is now USB**, via `-e default`. If OTA comes back, it needs a custom two-slot partition table and the firmware must fit in **~1.97 MB per slot** (see [CHANGELOG.md](CHANGELOG.md) for the current size vs. that budget). **`pio` is not on PATH** on this machine — use the full path:

```bash
# build + upload over USB (usual workflow)
"d:\pio\penv\Scripts\platformio.exe" run -e default -t upload
# build only
"d:\pio\penv\Scripts\platformio.exe" run -e default
# upload over the air (inactive until a two-slot partition table exists — see above)
"d:\pio\penv\Scripts\platformio.exe" run -e ota -t upload
# serial monitor (115200)
"d:\pio\penv\Scripts\platformio.exe" device monitor
# clean
"d:\pio\penv\Scripts\platformio.exe" run -t clean
```

There is no test suite.

## Toolchain / platform gotchas

- The `platform` in [platformio.ini](platformio.ini) is the **pioarduino fork** (pinned release 55.03.39 → Arduino-Core 3.3.9 / ESP-IDF 5.5.4), *not* stock `espressif32` (which is frozen at IDF 4.4). The pre-migration config lives in git history; what the upgrade broke and how it was re-fixed is summarized in [CHANGELOG.md](CHANGELOG.md).
- The display stack is **Arduino_GFX** (`Arduino_ESP32QSPI` bus + `Arduino_NV3041A` panel driver) and **LVGL 9** (`lvgl/lvgl@^9.5`), both pulled from the registry via `lib_deps` — **nothing display-related is vendored anymore** (the old `lvgl`/`TFT_eSPI`/`FT6336U_CTP_Controller` vendoring under `lib/` is gone along with the FT6336U-based panel). LVGL is configured by [lv_conf.h](lv_conf.h) at the repo root, found via `-DLV_CONF_INCLUDE_SIMPLE -I .` in `build_flags`. Changing display/UI behavior usually means editing that config, not the library source.
- `ESP32-BLE-Gamepad` is **still vendored in [lib/](lib/ESP32-BLE-Gamepad), and committed** — it's the one library that stays vendored, because it carries a local patch (next bullet). It is therefore *not* in `lib_deps`; only its NimBLE dependency is, pinned to `^2.5`.
- **Driver families must not be mixed (IDF 5.x aborts at runtime with "CONFLICT"):** Arduino-core 3.x `Wire` uses the new I2C `driver_ng`, so **everything** on I2C must go through `Wire`/`Wire1` — no legacy `driver/i2c.h` calls anywhere. Likewise audio uses the **new `i2s_std` driver** (`driver/i2s_std.h`) for the NS4168 mono amp: the TX channel `i2s_tx_chan` is created in main.cpp's setup() with `chan_cfg.auto_clear = true` (without it the DMA ring endlessly replays the last tone — still mandatory) and consumed by [src/sound.cpp](src/sound.cpp) via `i2s_channel_write`. Don't reintroduce `driver/i2s.h` legacy calls either.
- **GT911** touch is read via `bb_captouch` on `Wire` (SDA=8/SCL=4, INT=3, RST=38). The **PCF8575** driving the 3 TTP223 touch pads sits on a separate bus, **Wire1** (SDA=18/SCL=17), address `0x20` — don't confuse the two I2C buses when wiring up new peripherals.
- **GPIO1 is the backlight** (LEDC PWM, 5 kHz/8-bit) — never repurpose it. The old Freenove board used GPIO1 as the audio amp enable line; that usage must never come back on this board.
- **The `ESP32-BLE-Gamepad` patch:** its `BleNUS.h`/`BleNUS.cpp` rely on `Arduino.h` being included transitively (true under NimBLE 1.x, *not* under the NimBLE 2.x the new core pulls in), so they fail with `delay`/`Serial`/`ltoa`/`dtostrf` "not declared". The fix is one line — `#include <Arduino.h>` in [lib/ESP32-BLE-Gamepad/BleNUS.h](lib/ESP32-BLE-Gamepad/BleNUS.h). To keep that fix under version control (rather than in the throwaway `.pio/`), the whole library is vendored into `lib/` and committed. **If you ever re-add `lemmingDev/ESP32-BLE-Gamepad` to `lib_deps`, the registry copy will shadow the vendored one and the error returns** — keep it vendored, or re-apply the include.

## Architecture (the big picture)

Almost everything lives in [src/main.cpp](src/main.cpp) (~2500 lines), which orchestrates networking, UI updates, input, and audio. The other `src/` modules are extractions from it; the screen modules own UI *construction*, but main.cpp owns most of the *update* logic (e.g. `updateContextPanel`, `shell_set_active_tab`).

**Dual-task split (FreeRTOS), not dual-core:** both tasks are pinned to core 1 at priority 1 — core 0 is deliberately left for WiFi/BT — so the split is task-level scheduling, not physical-core parallelism.
- `loop()` (Arduino task): drives `lv_timer_handler()` rendering, reads the TTP223 pads, sends BLE reports, handles OTA / WiFi / display-timeout.
- `loop2()` (created in `setup()` as `msgTaskHandle`, `xTaskCreatePinnedToCore(..., 1)`): tight 10 ms poll of the WebSocket via `checkMessages()`.
- **All LVGL access is guarded by `lvglMutex`.** Any function that touches LVGL widgets (the many `update*` / `switchToPage` / overlay functions) takes this mutex first. New UI code must do the same, because rendering and data updates run in different concurrently-scheduled tasks.

**Data flow:** WebSocket message → `onWebSocketMessage` → `handleEliteEvent(eventType, doc)` parses Elite/Icarus JSON → mutates the single global **`StatusModel status`** (defined in [src/gamedata.h](src/gamedata.h)) → screen `update*` functions read `status` and push into LVGL widgets. `StatusModel` is the single source of truth (cargo, fuel, hull, shields, nav, location, credits, etc.); legacy `#define` aliases like `cargoInfo`/`fuelInfo` map onto `status.*` for migration convenience.

**Shell (persistent frame):** [src/screens/shell.h](src/screens/shell.h)/[.cpp](src/screens/shell.cpp) build a chrome layer once in `setup()` on `lv_layer_top()`, so it overlays every screen instead of being rebuilt per page: a 480×44 metrics strip (jump count in `FONT_DISPLAY_BIG`, fuel/hull `lv_arc` gauges with cyan % labels, cargo `n/N`, BLE/WS icons that light up orange when connected, WiFi icon colored by signal quality: green/yellow-green/orange/red by RSSI), a tappable tab rail (`FTR`/`LOG`/`SYS`, 34px wide at the right edge, `x = SCREEN_WIDTH - SHELL_RAIL_W` = 446) whose taps set `reqPageSwitch` for `loop()` to action via `switchToPage()` (deferred because the tap callback itself runs under `lvglMutex`), and a footer (system name + on-foot/ship mode). `shell_set_active_tab(page)` inverts the active tab to orange. The content zone every page builds into is defined by `CONTENT_X/Y/W/H` in `shell.h` — 0,44 through 446,252 (446×208), i.e. full width minus the tab rail, full height minus strip and footer.

**Screens / pages:** three pages selected by `currentPage` and `switchToPage(int)`, each laid out inside the shell's content zone:
- 0 = Fighter command pad ([src/screens/fighter.cpp](src/screens/fighter.cpp))
- 1 = Log viewer ([src/screens/info.cpp](src/screens/info.cpp), the default start page): an events + relative-time column, a `SIGNALS` sidebar of pinned-body cards, and a bottom context panel toggled by `updateContextPanel()` between **BACKPACK** (on foot) and **EXPLORATION** (in ship) — honk state, bodies scanned/mapped (with a `n[/n] OK` count once `FSSAllBodiesFound` fires), first-discovery/first-map counts (highlighted when >0), and stations by landing-pad size (`nL`/`nM`/`nFC`).
- 2 = System/settings ([src/screens/system.cpp](src/screens/system.cpp))

Each screen module exposes `create_*_ui()` plus `extern lv_obj_t*` widget handles that main.cpp updates.

**Exploration tracking:** `ExplorationInfo` ([src/gamedata.h](src/gamedata.h)) backs the context panel above. `FSSDiscoveryScan`/`FSSAllBodiesFound`/`Scan`/`SAAScanComplete` update it and still generate their normal generic journal log line; `FSSSignalDiscovered` alone is consumed silently (no log line, high-volume) and classifies stations by pad size — Outpost → M, Coriolis/Orbis/other big stations → L, FleetCarrier → FC — deduped by a capped name-hash table that can undercount but never inflates. `WasDiscovered`/`WasMapped` in the `Scan` payload reflect state *before* the player's own scan, so first-discovery/first-map counts only credit genuinely new finds. Everything resets on `FSSDiscoveryScan` (i.e. on jump). Log replay (e.g. from `sample.json`) repopulates all counters subject to the log's own 50-entry window.

**Fonts:** [src/theme.h](src/theme.h) declares custom MFD fonts — `font_michroma_24`/`font_michroma_12` (headline/label) and `font_jura_16` (buttons) — generated via `npx lv_font_conv` from the TTF sources in [tools/fonts/](tools/fonts/) (committed) and built with the German umlaut set (äöüÄÖÜß) included, since fighter-pad labels use them directly ("Zurück", "Befehle", etc). The remaining Montserrat sizes (12/14/20/28) are plain LVGL built-ins enabled in `lv_conf.h`.

**Input:** the PCF8575 (`pcf->read16()`, active-high on this board) is polled in `loop()` and drives 3 TTP223 touch pads on `Wire1` — top pad = previous page, bottom pad = next page (wrapping over the 3 pages), middle pad = display off; while the display is dark, any pad press only wakes it rather than acting. The shell's tab rail (above) is also directly tappable for the same page selection — TTP cycling and tab taps both funnel into `switchToPage()`. There are no physical gamepad buttons anymore: the **on-screen fighter pad** (screen 0, [src/screens/fighter.cpp](src/screens/fighter.cpp)) is the sole source of BLE HID presses, calling `bleGamepad->press()/release()` only when BLE is connected.

**Audio:** `playTone()` in [src/sound.cpp](src/sound.cpp) synthesizes sine beeps and writes them via the new `i2s_std` driver (`i2s_channel_write`) to the NS4168 mono amp. `soundSetInitialized(true)` is called after I2S init in main.cpp gates playback. There is no codec chip and no battery monitoring on this board — the ES8311 driver and battery-sampling code were both deleted (the JC4827W543's IP5306 has no status output to read).

## Configuration

[src/config.h](src/config.h) holds **hardcoded** WiFi SSID/password, the default Icarus server IP/port (`DEFAULT_SERVER_IP`, `DEFAULT_WEBSOCKET_PORT`), hostname, and display geometry. Update these for a different network/host. USB mode is forced to native CDC via `build_flags` (`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`) — don't also set the equivalent `board_build.*` options or you get "redefined" warnings.

All pin assignments now live in [src/config.h](src/config.h) (display QSPI/backlight, GT911 touch, PCF8575/TTP223, NS4168 I2S); [GPIOs.md](GPIOs.md) documents the physical wiring against the board's pin distribution doc.
