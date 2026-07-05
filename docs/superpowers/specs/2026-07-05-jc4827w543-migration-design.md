# Hardware Migration: Freenove FNK0104B → Guition JC4827W543 (Design)

Date: 2026-07-05
Status: approved by user (brainstorming session)

## Goal

Port the BLEJoy Elite Dangerous controller firmware from the Freenove ESP32-S3
FNK0104B (2.8" 320×240 SPI TFT, 12 hardware buttons on PCF8575) to the Guition
JC4827W543 "CYD" (4.3" 480×272 NV3041A QSPI, GT911 capacitive touch,
ESP32-S3-WROOM-1 N4R8). Rebuild the UI for the larger display, replace the
three silver page-switch buttons with three TTP223 touch sensors on the
PCF8575, drop all other hardware buttons, and bring every dependency
(platform, LVGL, libraries) to its latest version.

Reference material: `References/jc4827w543.yaml` (user's working ESPHome test)
and `References/JC4827W543_4.3inch_ESP32S3_board/` (vendor SDK: pin
distribution, datasheets, Arduino/PlatformIO LVGL demos).

## Decisions (user-confirmed)

| Topic | Decision |
|---|---|
| Panel touch variant | **C — capacitive GT911** |
| BLE gamepad | **Stays**, input exclusively via on-screen touch UI (fighter pad); 12 hardware buttons removed without replacement |
| TTP223 role | **Up/down page navigation**: top (touch3/P5) = previous page, bottom (touch1/P7) = next page (wrap across 3 pages), middle (touch2/P6) = display off |
| Flash / OTA | **huge_app.csv first (3 MB app, USB upload only)**; measure real firmware size after migration, then revisit custom OTA partition pair (~1.97 MB per slot, no SPIFFS) |
| Info screen layout | **Two-column dashboard** (log left, sidebar right) |
| Graphics stack | **A — Arduino_GFX + LVGL 9** (vendor-blessed panel path, latest LVGL, UI rebuild pays the v9 API jump exactly once) |

## Hardware target

Board def `esp32-s3-devkitc-1`, `board_build.arduino.memory_type = qio_opi`
(4 MB QIO flash, 8 MB octal PSRAM), CPU 240 MHz. USB-CDC build flags stay
(`ARDUINO_USB_MODE=1`, `ARDUINO_USB_CDC_ON_BOOT=1`).

### Pin map (replaces GPIOs.md content)

| Function | Old (Freenove) | New (JC4827W543) |
|---|---|---|
| Display | TFT_eSPI SPI: CS10, MOSI11, SCK12, DC46, BL45 | **QSPI NV3041A**: CS=45, CLK=47, D0=21, D1=48, D2=40, D3=39 |
| Backlight | GPIO45 LEDC | **GPIO1** LEDC (5 kHz / 8-bit; full/dim duty logic unchanged) |
| Panel touch | FT6336U | **GT911** on `Wire`: SDA=8, SCL=4, INT=3, RST=38 |
| PCF8575 | `Wire`, 12 buttons | **`Wire1`**: SDA=18, SCL=17, addr **0x22** — only TTP223 on P7 (bottom), P6 (middle), P5 (top); active-high |
| Audio | ES8311 codec, I2S MCLK=4/BCLK=5/WS=7/DOUT=8 | **NS4168 mono amp**: BCLK=42, LRCLK(WS)=2, DIN=41, **no MCLK, no codec**; `i2s_std` driver with `chan_cfg.auto_clear = true` stays |
| Battery | TP4056 CHRG (GPIO2) + ADC divider (GPIO9) | **removed** — IP5306 power-bank IC has no status output |
| Free IOs (future) | — | 5, 6, 7, 9, 14, 15, 16, 46 (SD slot on 10–13 unused) |

Note: GPIO0 = BOOT button (also LCD_TE); IO35–37 unavailable (octal PSRAM).

### Battery removal consequences

Delete battery icon, `is_plugged_usb()`, CHRG/ADC/hysteresis code. Display
timeout simplifies to: BLE connected → normal timeout with 30 % dim stage,
else 5 s. Wake exclusively via panel touch. Middle TTP223 = manual display
off (successor of the BLACK button); while the display is off, any TTP223
press only wakes (no navigation side effect), consistent with panel touch.

## Library changes

**Added (latest versions):**
- `moononournation/Arduino_GFX` — `Arduino_ESP32QSPI` bus + `Arduino_NV3041A`
  panel (official vendor path for this board)
- `lvgl/lvgl@^9` — **from the PlatformIO registry, no longer vendored**
- `bitbank2/bb_captouch` (actively maintained GT911 driver; handles the
  0x5D/0x14 address strapping variants)

**Removed:** TFT_eSPI (vendored), FT6336U_CTP_Controller (vendored),
`src/es8311.cpp/.h`, `src/User_Setup.h`, `lib/TFT_eSPI_Setups/`,
`earlephilhower/ESP8266Audio` (in lib_deps but sound.cpp synthesizes its own
sine — verify unused, then drop).

**Updated to latest:** pioarduino platform release, NimBLE-Arduino (2.x),
ArduinoJson, ArduinoWebsockets, robtillaart/PCF8575. ESP32-BLE-Gamepad: check
upstream for a newer release; stays vendored in `lib/` (re-apply the
`#include <Arduino.h>` BleNUS patch if upstream still lacks it).

## Code changes

- **`src/display.cpp` rewritten:** Arduino_GFX init (QSPI bus + NV3041A,
  rotation as config define, default 180° matching the ESPHome test — verify
  on hardware), LVGL 9 API (`lv_display_create`, flush cb →
  `panel->writePixels`), double partial draw buffers in PSRAM. Boot splash
  regenerated from ed_logo.png sized for 480×272.
- **Touch:** GT911 via bb_captouch as LVGL 9 pointer indev; every touch wakes
  the display.
- **`src/main.cpp`:** button matrix (`buttonPins[12]`, PCF poll → BLE
  press/release) removed. New TTP223 poll on Wire1 PCF8575 with edge
  detection/debounce: top = previous page, bottom = next page (wrap),
  middle = display off. ES8311 init removed; I2S pins swapped, everything
  else about the `i2s_std` setup kept.
- **Unchanged architecture:** dual-core split (loop / loop2 WS task),
  `lvglMutex` guarding all LVGL access, WebSocket/Icarus protocol,
  `StatusModel`, `handleEliteEvent`, history replay, pinned-bodies logic in
  gamedata.
- **LVGL 9 breaking change handled by design:** recolor tags
  (`#RRGGBB text#`) no longer exist. The log becomes monochrome amber; all
  color-coded info (pins, genus states green/purple/dark-blue) moves into
  dedicated sidebar widgets.
- **`lv_conf.h` rewritten for v9:** 16-bit color, Montserrat sizes for
  480×272, allocations from PSRAM.
- **Docs:** GPIOs.md rewritten, CLAUDE.md updated (stack, build, gotchas —
  TFT_eSPI/DMA notes removed and replaced by Arduino_GFX notes), CHANGELOG.md
  entry.

## UI design (480×272, LVGL 9)

Guiding style: Elite HUD look — orange (#FF7100 family) on dark, one central
theme/style module instead of scattered per-widget styling. Fine-tuning during
implementation with the frontend-design / ui-design skills.

- **Info screen (default) — two-column dashboard:**
  - Header (full width, ~36 px): jumps, fuel bar, hull bar, right-aligned
    WiFi/WS/BLE icons (battery icon gone)
  - Left (~300 px): event log, Montserrat 14, ~10 lines, monochrome amber;
    jump overlay stays
  - Right (~180 px): system name header; pinned bodies as cards (body label,
    `Bio 1/2 Geo 3`, genus chips colored green = to scan, purple = in
    sampler, dark blue = done); backpack panel at the bottom, visible only
    on foot
- **Fighter screen:** command pad with much larger touch targets filling
  480×236 under the header; **identical BLE button IDs** (no HID change).
- **System screen:** network diagnostics (IP, server, WS state), BLE state,
  brightness slider, firmware version/build, heap/PSRAM stats.
- **Page-switch feedback:** brief page-title overlay on TTP navigation.
- **Splash:** ed_logo re-rendered for 480×272.

## Process, git, risks

- **Git:** tag `freenove-fnk0104b` on current main (old hardware stays
  buildable forever), migration on branch `jc4827w543`, merge after hardware
  verification.
- **Build:** `-e default` (USB) until the OTA partition question is resolved
  in a later phase. `[env:ota]` stays defined with an "inactive until OTA
  partitions" comment; ArduinoOTA code stays compiled (harmless, instantly
  usable in phase 2).
- **Bring-up order** (each stage verifiable on hardware):
  ① display test pattern → ② panel-touch coordinates → ③ TTP223 navigation →
  ④ sound beep → ⑤ BLE pairing → ⑥ full WebSocket operation with replay.
- **Risks:**
  1. The old QSPI-DMA vs I2S-GDMA stall ghost could resurface with
     Arduino_GFX — bring-up tests display + sound together early.
  2. Rotation/touch mapping needs on-device calibration.
  3. LVGL 9 unknowns during the UI rebuild.
  4. Flash budget must be watched for the later OTA return (~1.97 MB/slot).

## Out of scope

- OTA partition switch (phase 2, after measuring the real firmware size)
- EDMC/Telemetry MQTT data source (deferred separately)
- SD card, IP5306-I2C probing, additional TTP223 gestures
