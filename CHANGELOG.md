# BLE Joypad - Changelog

## V5 On-Device Hardening (July 2026, post-review)

Fixes and features from live exploration sessions on the finished V5 build.
Firmware after this round: 2,078,768 bytes (66.1 % of the 3 MB partition),
RAM 18.7 % static.

### SIGNALS sidebar redesign (user design, real-data driven)
- A 15-signal-body system blew past the old 4 card slots, and total-count
  sorting locked bio bodies out. New scheme: dynamic header tally
  ("SIGNALS 15  B8 G22", B = OPEN bio signals), ONE detail card only for the
  body whose gravity well the player is in (ApproachBody/LeaveBody ->
  `status.nearBodyId`), compact per-category body lists below
  ("BIO: 5b(2)" with open counts; bodies with several types appear in each
  line). Registry `MAX_PINNED_BODIES` 4 -> 16, sort/eviction bio-first.
- Pins store the FULL BodyName; labels shorten at render time against the
  current system name (self-healing after boot replay) and compact inner
  spaces ("5 b a" -> "5ba").
- ScanOrganic progress arriving before its body's pin exists (replay
  reordering: the game re-announces FSSBodySignals on approach) is buffered
  and applied when the pin appears.
- `Shutdown` no longer clears signals/exploration (the replay usually ends
  on one and wiped what it had just rebuilt); only jumps/CarrierJump clear.

### Boot replay: 100 entries, crash-free
- Replay window 50 -> 100 (the old lwIP ceiling was platform-specific).
- Vendored `ArduinoWebsockets` with a large-frame patch: the stock lib
  treats a momentarily empty TCP buffer as end-of-frame and truncated
  ~100 KB responses (they arrived as EMPTY messages). Bounded 3 s waits,
  zero-payload frames excepted, truncated frames dropped.
- Arduino `String` silently caps near 64 KB: `deserializeJson` now parses
  straight from the lib's internal `std::string` (`message.rawData()`).
- Boot `abort()` on core 0 decoded: mDNS (started by the dead ArduinoOTA
  listener) logging during the parse window while ArduinoJson's pools had
  drained internal RAM. ArduinoOTA fully removed; WS-path JsonDocuments use
  a PSRAM-pinned allocator; replay passes JsonVariant views (no per-entry
  copies) and yields to core 0 between entries.

### Quality of life
- `getSystem` boot fallback: adopts the current system name when the replay
  window held no FSDJump/Location ("Waiting for events..." fix).
- Periodic 5 s UI refresh (change-guarded) - healed data becomes visible
  without waiting for the next journal event.
- Non-blocking USB-CDC TX (`Serial.setTxTimeoutMs(0)`): with a PC attached
  but no terminal reading, blocked prints inside lvglMutex holds froze the
  display until a monitor drained the buffer.
- FSS signal pings pitched by category (bio 1800 Hz, geo 900 Hz, other
  1400 Hz, grouped); rising three-tone chime on first discoveries
  (`Scan.WasDiscovered == false`); shared audio gate so chime and pings
  never collide.
- JUMPS strip group follows the value/caption grid (Michroma 18, shared
  caption baseline); tab captions rotated 90 deg; static log time column
  removed, freed width widens the sidebar to 202 px.

## V5 MFD Overhaul (July 2026)

Reworked the UI around a persistent "shell" frame drawn on `lv_layer_top()`
instead of being rebuilt per page, added custom-generated fonts with full
umlaut support, and rebuilt all three pages into a shared content zone.
Exploration tracking gained a live context panel fed by a previously-dropped
event.

### Shell / navigation
- New `src/screens/shell.h`/`.cpp`: a persistent frame (metrics strip, tab
  rail, footer) built once in `setup()` on `lv_layer_top()`, overlaying every
  screen instead of being duplicated per page.
  - **Strip** (480×44): jump count (Michroma 24), fuel/hull `lv_arc` gauges
    with cyan % labels, cargo `n/N`, BLE/WS icons (orange when connected), WiFi
    icon colored by signal quality (green/yellow-green/orange/red by RSSI).
  - **Tab rail** (34px, right edge, `x`=446): `FTR`/`LOG`/`SYS` tabs, tappable
    (deferred via `reqPageSwitch`, actioned in `loop()`) alongside the
    existing TTP223 prev/next cycle; active tab inverts to orange via
    `shell_set_active_tab()`.
  - **Footer**: current system name (left) + on-foot/ship mode (right).
- Page-title overlay (shown briefly on TTP page switches) removed — the tab
  rail always shows current-page state, making the overlay redundant. An
  optional page-fade (`PAGE_FADE_MS` in `config.h`, default 120 ms, 0
  disables) replaces it; the jump-detected overlay moved to `lv_layer_top()`
  so it survives page fades (`act_scr` flips ~33 ms late otherwise).
- All three pages rebuilt into the shell's 446×208 content zone
  (`CONTENT_X/Y/W/H` in `shell.h`) instead of each owning the full screen.

### Fonts
- Added custom MFD fonts generated via `npx lv_font_conv` from TTFs in
  `tools/fonts/` (committed): `font_michroma_24`/`font_michroma_12`
  (headline/label) and `font_jura_16` (buttons), all built **with** the
  German umlaut set (äöüÄÖÜß) so fighter labels read "Zurück"/"Befehle"
  natively. Adds roughly +50 KB to flash.
- Dropped the now-dead `FONT_HEAD` (`lv_font_montserrat_16`): its only caller
  was the old page header, replaced by the shell strip. Removed the
  `#define` from `theme.h` and disabled `LV_FONT_MONTSERRAT_16` in
  `lv_conf.h`.

### Log page / context panel
- LOG page (`src/screens/info.cpp`) rebuilt into the content zone: an
  events + relative-time column, a `SIGNALS` sidebar of pinned-body cards,
  and a new bottom context panel.
- Context panel toggles via `updateContextPanel()` between **BACKPACK** (on
  foot) and **EXPLORATION** (in ship): honk state, bodies scanned/mapped
  (`n[/n] OK` once `FSSAllBodiesFound` fires), first-discovery/first-map
  counts (highlighted when >0), and stations by landing-pad size
  (`nL`/`nM`/`nFC`).
- `FSSSignalDiscovered` is no longer dropped on the floor: it's now consumed
  silently (no log line — high-volume) to count stations by pad size
  (Outpost→M, big stations→L, FleetCarrier→FC), deduped by a capped
  name-hash table that can undercount but never inflates.

### Size / OTA budget
- Measured firmware size (`pio run -e default`):
  - `Flash: [=======   ]  68.0% (used 2139350 bytes from 3145728 bytes)`
  - `RAM:   [==        ]  19.4% (used 63480 bytes from 327680 bytes)`
  - `.pio/build/default/firmware.bin`: 2,139,760 bytes (~2.04 MB)
- OTA has been dropped by decision (2026-07-06): `[env:ota]` is removed from
  `platformio.ini` and USB flashing is the only workflow going forward. With
  no OTA slot to fit, there is no image-size constraint beyond the 3 MB
  `huge_app.csv` app partition.

## Hardware Migration - Guition JC4827W543 (July 2026)

Migrated off the Freenove ESP32-S3 WROOM (FNK0104B) onto a **Guition JC4827W543**
(ESP32-S3-WROOM-1 N4R8, 4.3" 480×272 NV3041A QSPI panel, GT911 capacitive touch,
4 MB flash / 8 MB octal PSRAM). This is a full hardware swap: new display driver
stack, new touch controller, no more physical buttons, a different audio amp, no
battery monitoring, and an LVGL 8→9 bump to go with the new display library. The
UI was largely rebuilt for the bigger panel along the way.

### Hardware
- **Display**: TFT_eSPI (parallel, 2.8") → **Arduino_GFX** (`Arduino_ESP32QSPI` +
  `Arduino_NV3041A`), 4.3" 480×272 over QSPI.
- **Touch**: FT6336U → **GT911**, read via `bb_captouch` on `Wire` (SDA=8/SCL=4).
- **Buttons**: the 12-button PCF8575 gamepad is gone entirely. Replaced by an
  on-screen fighter pad (BLE IDs 13–20 unchanged) plus 3 TTP223 capacitive pads
  on a PCF8575 — now on a *second* I2C bus, `Wire1` (SDA=18/SCL=17), address
  `0x22` — used only for page navigation and display-off, never for BLE.
- **Audio**: ES8311 codec driver deleted. A bare **NS4168 mono I2S amp** now
  drives sound directly (BCLK=42, WS=2, DOUT=41, no MCLK, no codec setup),
  still via `i2s_std` with `chan_cfg.auto_clear = true`.
- **Battery monitoring deleted** — the JC4827W543's IP5306 exposes no status
  line to sample.

### LVGL 8.4 → 9.5
- `lvgl` moved from a vendored/gitignored copy to `lvgl/lvgl@^9.5` pulled
  straight from the registry (`lib_deps`), alongside `moononournation/GFX
  Library for Arduino@^1.6.6` for Arduino_GFX.
- `lv_conf.h` at the repo root trimmed down to a minimal 9.x config, picked up
  via `-DLV_CONF_INCLUDE_SIMPLE -I .` in `platformio.ini`'s `build_flags`.
- v8→v9 rename sweep across the UI code: `lv_scr_load` → `lv_screen_load`,
  `lv_btnmatrix` → `lv_buttonmatrix`, `lv_obj_clear_flag` →
  `lv_obj_remove_flag`, zoom → `lv_obj_set_style_transform_scale`,
  `LV_LABEL_LONG_WRAP` → `LV_LABEL_LONG_MODE_WRAP`, the timer `user_data`
  getter, and an `lv_event_get_target` cast fixup. Recolor markup (`#color
  ...#`) now survives only in the sidebar's genus labels — it was stripped
  from the `ProspectedAsteroid` log line since the log itself is monochrome.

### Input model change
- All 12 physical PCF8575 gamepad buttons and the FNK0104B expander wiring
  are gone.
- 3 TTP223 pads sit right of the display on the `Wire1` PCF8575 (0x22),
  active-high, edge-triggered: top = previous page, middle = display off,
  bottom = next page. While the display is off, any pad press only wakes it
  rather than acting.
- BLE gamepad presses now come exclusively from the on-screen fighter pad
  (IDs 13–20 unchanged); the TTP223 pads never reach BLE.

### UI rebuild for 480×272
- New `theme` module (`src/theme.h`/`.cpp`) centralizes colors/styles.
- Info screen rebuilt as a two-column dashboard: log (left, 300px) + a
  pinned-bodies sidebar (right, 180px) with genus color states; a backpack
  card shows only when on-foot; cargo bar bottom-left; 32px header.
- Fighter pad scaled up with bigger touch targets/fonts for the 4.3" panel
  (BLE IDs 13–20 unchanged).
- New system screen: diagnostics plus brightness/volume sliders.
- Page-title overlay shown on TTP223 page switches.
- Boot splash resized to 460×265.

### 4 MB flash / `huge_app.csv` (OTA parked)
- The board only has 4 MB flash, so `platformio.ini` now uses
  `board_build.partitions = huge_app.csv` (no OTA slots). `[env:ota]` is kept
  in the config but is **INACTIVE** until a custom two-slot partition table
  exists.
- Measured firmware size (`pio run -e default`):
  - `Flash: [=======   ]  68.6% (used 2156838 bytes from 3145728 bytes)`
  - `RAM:   [==        ]  19.3% (used 63144 bytes from 327680 bytes)`
  - `.pio/build/default/firmware.bin`: 2,157,248 bytes (~2.06 MB)
- OTA return needs ≤ ~1.97 MB (custom two-slot table w/o SPIFFS) — the
  current build is already **~89 KB over** that per-slot budget. Trim
  candidates before OTA can return: the Montserrat font selection in
  `lv_conf.h` (12/14/16/20/28 all enabled — audit which sizes the UI
  actually uses), and the 238 KB boot splash asset (`src/ed_logo.h`).

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
