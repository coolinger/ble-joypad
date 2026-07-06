# V5 MFD Overhaul + Exploration Panel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rebuild the BLEJoy UI as a persistent MFD frame (metrics strip / tab rail / footer on `lv_layer_top`) around three content pages, with custom umlaut-capable fonts and a context-sensitive sidebar panel (Backpack on foot, Exploration stats in ship).

**Architecture:** Shell widgets live once on LVGL's global top layer; the three pages stay ordinary screens confined to a 446×208 content zone. A new `ExplorationInfo` block in `StatusModel` is fed by five journal events (FSSDiscoveryScan, FSSAllBodiesFound, Scan, SAAScanComplete, FSSSignalDiscovered) with BodyID-bitmap dedupe, reset wherever the pinned bodies clear.

**Tech Stack:** LVGL 9.5 (`lv_layer_top`, `lv_arc`), Arduino_GFX (unchanged), `lv_font_conv` via npx (Node v22 present), Michroma/Jura (OFL).

Spec: `docs/superpowers/specs/2026-07-06-v5-mfd-overhaul-design.md`

## Global Constraints

- `pio` NOT on PATH: `"d:\pio\penv\Scripts\platformio.exe"` (bash: `"d:/pio/penv/Scripts/platformio.exe"`). Build: `run -e default`.
- **NEVER run `-t upload` — the user flashes himself.** A task's acceptance gate is a green build (+ code checks); on-device checks are listed for the user.
- Branch `v5-mfd` (Task 1 creates it from main @ `28df52a`). Commits end with:
  `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`
- `lvglMutex` is NON-recursive. Update functions take it; `*_unlocked` variants don't. LVGL event callbacks run UNDER the mutex (inside `lv_timer_handler`) — they must never call a mutex-taking function; use the deferred-flag pattern (`reqRestartWifi` precedent in main.cpp).
- BLE HID unchanged: "CoolJoyBLE", 20 buttons, fighter IDs 13–20, `btn_to_cmd` map untouched.
- Palette verbatim (spec): ground `#020101`, orange `#ff7100`, cyan values `#35c4f0`, hairlines `#3a1c00`, dim labels `#8c4700`, warning red `#f30000`; genus colors unchanged (`#00c060`/`#c85aff`/`#4169e1`).
- All generated fonts MUST include `ÄÖÜäöüß`.
- UI tasks (3–6): the implementer invokes the Skill `frontend-design:frontend-design` before styling decisions and follows the spec's palette/geometry verbatim where given.
- Geometry: strip 0,0 480×44 · rail 446,44 34×208 · footer 0,252 446×20 · content 0,44 446×208.

---

### Task 1: Branch + custom fonts (umlauts!)

**Files:**
- Create: `src/fonts/font_michroma_24.c`, `src/fonts/font_michroma_12.c`, `src/fonts/font_jura_16.c` (generated), `tools/fonts/` (TTF sources, committed for reproducibility)
- Modify: `src/theme.h` (font declares), `platformio.ini` (one build flag)

**Interfaces:**
- Produces: LVGL fonts `font_michroma_24`, `font_michroma_12`, `font_jura_16`; theme macros `FONT_DISPLAY_BIG`, `FONT_DISPLAY_LABEL`, `FONT_BTN`. PIO compiles `src/fonts/*.c` automatically.

- [ ] **Step 1: Branch**

```bash
git checkout main && git pull && git checkout -b v5-mfd
```

- [ ] **Step 2: Fetch fonts (OFL)**

```bash
mkdir -p src/fonts tools/fonts
curl -L -o tools/fonts/Michroma-Regular.ttf "https://github.com/google/fonts/raw/main/ofl/michroma/Michroma-Regular.ttf"
curl -L -o "tools/fonts/Jura.ttf" "https://github.com/google/fonts/raw/main/ofl/jura/Jura%5Bwght%5D.ttf"
ls -la tools/fonts/
```
Expected: two TTFs, each > 40 KB.

- [ ] **Step 3: Generate LVGL fonts**

```bash
npx --yes lv_font_conv --font tools/fonts/Michroma-Regular.ttf --size 24 --bpp 2 --format lvgl --no-compress --lv-include lvgl.h -o src/fonts/font_michroma_24.c --range 0x20-0x7E --symbols "ÄÖÜäöüß"
npx --yes lv_font_conv --font tools/fonts/Michroma-Regular.ttf --size 12 --bpp 2 --format lvgl --no-compress --lv-include lvgl.h -o src/fonts/font_michroma_12.c --range 0x20-0x7E --symbols "ÄÖÜäöüß"
npx --yes lv_font_conv --font tools/fonts/Jura.ttf --size 16 --bpp 2 --format lvgl --no-compress --lv-include lvgl.h -o src/fonts/font_jura_16.c --range 0x20-0x7E --symbols "ÄÖÜäöüß"
```
Check each .c file: `grep -c "glyph_bitmap" src/fonts/*.c` (nonzero) and NO console warning about missing glyphs for the umlauts. If Jura (variable font) fails or warns → regenerate `font_jura_16.c` from Michroma at size 16 instead, keep the symbol name `font_jura_16`? NO — then name it `font_michroma_16.c`/`font_michroma_16` and use that symbol in Step 5; note the substitution in the report.

- [ ] **Step 4: Build flag** — in `platformio.ini` `build_flags`, add the line
```ini
  -DLV_LVGL_H_INCLUDE_SIMPLE
```
(directly after `-DLV_CONF_INCLUDE_SIMPLE`; the generated .c files use it to include lvgl.h).

- [ ] **Step 5: Declare in `src/theme.h`** — after the existing `FONT_*` block insert:

```cpp
// Custom MFD fonts (generated via lv_font_conv, see tools/fonts/; include äöüÄÖÜß)
LV_FONT_DECLARE(font_michroma_24);
LV_FONT_DECLARE(font_michroma_12);
LV_FONT_DECLARE(font_jura_16);
#define FONT_DISPLAY_BIG   (&font_michroma_24)
#define FONT_DISPLAY_LABEL (&font_michroma_12)
#define FONT_BTN           (&font_jura_16)
```

- [ ] **Step 6: Build**

```bash
"d:/pio/penv/Scripts/platformio.exe" run -e default
```
Expected: SUCCESS; note the flash delta (~+50 KB) in the report.

- [ ] **Step 7: Commit**

```bash
git add -A && git commit -m "V5: custom MFD fonts (Michroma 24/12, Jura 16) with umlauts

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 2: ExplorationInfo data model + journal events

**Files:**
- Modify: `src/gamedata.h`, `src/gamedata.cpp`, `src/main.cpp`

**Interfaces:**
- Produces: `struct ExplorationInfo` as `status.exploration`; API in gamedata.h:
  `void explorationReset(); void explorationHonk(int bodyCount); void explorationAllFound(); void explorationScan(int bodyId, bool wasDiscovered, bool wasMapped); void explorationMapped(int bodyId); bool explorationStation(const char* signalName, const char* signalType);` (returns true if a new station was counted). Task 4's `updateContextPanel` reads `status.exploration`.

- [ ] **Step 1: gamedata.h — struct + API.** Inside `struct StatusModel`, after `BioscanInfo bioscan;` add:

```cpp
  ExplorationInfo exploration;
```
and ABOVE `struct StatusModel` add:

```cpp
// Per-system exploration progress (reset on hyperspace jump, like the pins).
// "first*" counters rely on Scan.WasDiscovered / Scan.WasMapped, which
// describe what Universal Cartographics knew BEFORE the player's scan.
struct ExplorationInfo {
  bool honked = false;        // FSSDiscoveryScan seen
  bool allFound = false;      // FSSAllBodiesFound
  int  bodyCount = 0;         // FSSDiscoveryScan.BodyCount
  int  scanned = 0;           // unique Scan BodyIDs
  int  mapped = 0;            // unique SAAScanComplete BodyIDs
  int  firstDiscovered = 0;   // scans with WasDiscovered == false
  int  firstMapped = 0;       // maps of bodies whose Scan had WasMapped == false
  uint8_t stationsL = 0;      // largest pad L (Coriolis/Orbis/Ocellus/Asteroid/MegaShip)
  uint8_t stationsM = 0;      // largest pad M (Outpost)
  uint8_t carriers = 0;       // FleetCarrier
};
```
After the pinned-bodies API declarations add:

```cpp
// Exploration progress API (implementation in gamedata.cpp; dedupe backing
// bitmaps are file-static there).
void explorationReset();
void explorationHonk(int bodyCount);
void explorationAllFound();
void explorationScan(int bodyId, bool wasDiscovered, bool wasMapped);
void explorationMapped(int bodyId);
bool explorationStation(const char* signalName, const char* signalType);
```

- [ ] **Step 2: gamedata.cpp — implementation.** Append:

```cpp
// ---------------- Exploration progress ----------------
// BodyID dedupe bitmaps (BodyIDs are small ints; clamp to 0..255).
static uint32_t scannedBits[8];
static uint32_t mappedBits[8];
static uint32_t unmappedBits[8];   // bodies whose Scan said WasMapped == false
#define EXPL_MAX_STATIONS 24
static uint32_t stationHashes[EXPL_MAX_STATIONS];
static int stationHashCount = 0;

static bool testAndSetBit(uint32_t* bits, int id) {
  if (id < 0 || id > 255) return false;
  uint32_t mask = 1UL << (id & 31);
  if (bits[id >> 5] & mask) return false;
  bits[id >> 5] |= mask;
  return true;
}

void explorationReset() {
  status.exploration = ExplorationInfo();
  memset(scannedBits, 0, sizeof(scannedBits));
  memset(mappedBits, 0, sizeof(mappedBits));
  memset(unmappedBits, 0, sizeof(unmappedBits));
  stationHashCount = 0;
}

void explorationHonk(int bodyCount) {
  status.exploration.honked = true;
  if (bodyCount > 0) status.exploration.bodyCount = bodyCount;
}

void explorationAllFound() {
  status.exploration.allFound = true;
}

void explorationScan(int bodyId, bool wasDiscovered, bool wasMapped) {
  if (!testAndSetBit(scannedBits, bodyId)) return;  // body already counted
  status.exploration.scanned++;
  if (!wasDiscovered) status.exploration.firstDiscovered++;
  if (!wasMapped) testAndSetBit(unmappedBits, bodyId);
}

void explorationMapped(int bodyId) {
  if (!testAndSetBit(mappedBits, bodyId)) return;
  status.exploration.mapped++;
  // First map iff nobody had mapped it before our Scan (Scan precedes mapping).
  if (bodyId >= 0 && bodyId <= 255 &&
      (unmappedBits[bodyId >> 5] & (1UL << (bodyId & 31)))) {
    status.exploration.firstMapped++;
  }
}

// FNV-1a over the signal name for station dedupe.
static uint32_t fnv1a(const char* s) {
  uint32_t h = 2166136261u;
  while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; }
  return h;
}

bool explorationStation(const char* signalName, const char* signalType) {
  if (!signalName || !signalName[0] || !signalType) return false;
  uint8_t* bucket = nullptr;
  // Largest-pad classification by journal SignalType.
  if (strcmp(signalType, "Outpost") == 0) {
    bucket = &status.exploration.stationsM;
  } else if (strcmp(signalType, "FleetCarrier") == 0) {
    bucket = &status.exploration.carriers;
  } else if (strcmp(signalType, "StationCoriolis") == 0 ||
             strcmp(signalType, "StationONeilOrbis") == 0 ||
             strcmp(signalType, "StationONeilCylinder") == 0 ||
             strcmp(signalType, "StationBernalSphere") == 0 ||
             strcmp(signalType, "StationAsteroid") == 0 ||
             strcmp(signalType, "StationMegaShip") == 0) {
    bucket = &status.exploration.stationsL;
  } else {
    return false;  // installations, USS, ... are not landable stations
  }
  uint32_t h = fnv1a(signalName);
  for (int i = 0; i < stationHashCount; i++) {
    if (stationHashes[i] == h) return false;  // already counted
  }
  if (stationHashCount < EXPL_MAX_STATIONS) stationHashes[stationHashCount++] = h;
  if (*bucket < 255) (*bucket)++;
  return true;
}
```
Add `#include <string.h>` at the top of gamedata.cpp if not present.

- [ ] **Step 3: main.cpp — un-ignore FSSSignalDiscovered.** Delete BOTH lines:
`const char ignoreEvent_FSSSignalDiscovered[] PROGMEM = "FSSSignalDiscovered";` (~line 104) and the `ignoreEvent_FSSSignalDiscovered` entry in the `ignoreJournalEvents[]` array (~line 116).

- [ ] **Step 4: main.cpp — event branches.** In `handleEliteEvent`, directly BEFORE the existing `else if (event == "FSSBodySignals" || event == "SAASignalsFound")` branch (~line 1663), insert:

```cpp
    } else if (event == "FSSSignalDiscovered") {
      // Silent, high-volume: count landable stations by largest pad, no log line.
      if (doc["IsStation"] | false) {
        explorationStation(doc["SignalName"] | "", doc["SignalType"] | "");
      }
      return;  // consume: never reaches the generic log
```
(Match the surrounding brace style: the chain is `} else if (...) {`.)

Then add DATA-ONLY hooks that do NOT consume (the events keep flowing to the
existing generic logging). Directly before the new FSSSignalDiscovered branch,
insert this block:

```cpp
    }
    // Exploration progress (data only; these events still get logged below)
    if (event == "FSSDiscoveryScan") {
      explorationHonk(doc["BodyCount"] | 0);
    } else if (event == "FSSAllBodiesFound") {
      explorationAllFound();
    } else if (event == "Scan") {
      explorationScan(doc["BodyID"] | -1,
                      doc["WasDiscovered"] | true,
                      doc["WasMapped"] | true);
    } else if (event == "SAAScanComplete") {
      explorationMapped(doc["BodyID"] | -1);
    }
    if (false) {
```
**IMPORTANT — read the surrounding code first.** The exact splice depends on
the local `if/else if` chain. The REQUIRED semantics (what the reviewer will
check): (1) FSSSignalDiscovered is consumed silently; (2) the four data hooks
run for their events WITHOUT consuming them (generic log/consolidation still
happens); (3) defaults are `WasDiscovered=true`, `WasMapped=true`, `BodyID=-1`
so absent fields never count as "first". If a cleaner splice than the
`if (false) {` bridge exists in the actual chain (e.g. separate statement
block before the chain), prefer it — semantics over literal text.

- [ ] **Step 5: main.cpp — reset alongside the pins.** At ALL THREE `clearPinnedBodies();` call sites (~lines 1562, 1569, 1775), add `explorationReset();` on the next line.

- [ ] **Step 6: main.cpp — diagnostics.** In `updateSystemInfo()`'s big snprintf, after the `"  Bioscans: %d\n\n"` line add:

```cpp
      "  Expl: %s B:%d S:%d M:%d 1st:%d/%d St:%dL %dM %dFC\n\n"
```
with arguments (place them in the matching position of the argument list):

```cpp
      status.exploration.honked ? (status.exploration.allFound ? "HONK+ALL" : "HONK") : "-",
      status.exploration.bodyCount, status.exploration.scanned, status.exploration.mapped,
      status.exploration.firstDiscovered, status.exploration.firstMapped,
      status.exploration.stationsL, status.exploration.stationsM, status.exploration.carriers,
```

- [ ] **Step 7: Build + commit**

```bash
"d:/pio/penv/Scripts/platformio.exe" run -e default
git add -A && git commit -m "V5: ExplorationInfo model + honk/scan/map/first/station tracking

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```
Expected: SUCCESS. User can later verify via SYS page: sample.json replay ⇒ `Expl: HONK+ALL B:8 S:9 M:1 1st:0/1 St:0L 0M 0FC`.

---

### Task 3: Shell module (strip / rail / footer) + palette

**Files:**
- Create: `src/screens/shell.h`, `src/screens/shell.cpp`
- Modify: `src/colors.h` (palette), `src/screens/info.h` + `src/screens/info.cpp` (remove moved widgets), `src/main.cpp` (shell init, retargeted `updateHeader`/`updateStatusLine`, deferred tab switch, `updateCargoBar` removal)

**Interfaces:**
- Consumes: theme fonts (Task 1).
- Produces (used by Tasks 4–7): externs `shell_jumps_label, shell_fuel_arc, shell_fuel_label, shell_hull_arc, shell_hull_label, shell_cargo_label, shell_mode_label` plus the RELOCATED `wifi_icon, websocket_icon, bluetooth_icon, status_label` (same names as before — main.cpp update functions keep compiling); `void create_shell_ui();` and `void shell_set_active_tab(int page);` (caller holds lvglMutex or runs pre-mutex in setup). Deferred flag `volatile int reqPageSwitch` in main.cpp.

UI task: invoke Skill `frontend-design:frontend-design` before styling; palette from Global Constraints verbatim.

- [ ] **Step 1: New `src/colors.h` palette** — replace the value definitions (keep every existing macro NAME so all screens restyle automatically; genus colors are inline hex elsewhere and unaffected):

```cpp
#pragma once
#ifndef SRC_COLORS_H
#define SRC_COLORS_H

#include <lvgl.h>

// V5 MFD palette: true black ground, one leading orange, cyan only for
// measured values, red only for warnings. (Spec 2026-07-06.)
#define LV_COLOR_BG           lv_color_hex(0x020101)  // ground
#define LV_COLOR_FG           lv_color_hex(0xff7100)  // leading orange
#define LV_COLOR_HIGHLIGHT_BG lv_color_hex(0xffa617)  // pressed/highlight fill
#define LV_COLOR_HIGHLIGHT_FG lv_color_hex(0x140801)  // text on highlight
#define LV_COLOR_GAUGE_BG     lv_color_hex(0x140801)  // panel/card fill
#define LV_COLOR_GAUGE_FG     lv_color_hex(0xff7100)  // indicator fill
#define LV_COLOR_HAIRLINE     lv_color_hex(0x3a1c00)  // separators/borders
#define LV_COLOR_DIM          lv_color_hex(0x8c4700)  // secondary labels
#define LV_COLOR_VALUE        lv_color_hex(0x35c4f0)  // measured values (cyan)

#define LV_COLOR_LED_ON     lv_color_hex(0xffa718)
#define LV_COLOR_LED_OFF    lv_color_hex(0x7b2c13)
#define LV_COLOR_LED_BORDER lv_color_hex(0xb23f0a)

#define LV_COLOR_WARNING_FG lv_color_hex(0xf30000)
#define LV_COLOR_WARNING_BG lv_color_hex(0x471711)

#define LV_COLOR_OK_FG lv_color_hex(0xc6e6dc)

#endif // SRC_COLORS_H
```
Also update `src/theme.cpp`: in `theme_init()` change the two border colors from `LV_COLOR_GAUGE_FG` to `LV_COLOR_HAIRLINE` (style_panel, style_btn) so panels stop glowing; everything else stays. (`LV_COLOR_WHITE` was unused — removed; verify with `grep -rn LV_COLOR_WHITE src/` = only colors.h history.)

- [ ] **Step 2: Create `src/screens/shell.h`**

```cpp
// Persistent MFD frame on lv_layer_top(): metrics strip (top), page-tab rail
// (right, at the physical TTP223 pads), status footer (bottom). Created once;
// overlays every screen. Content zone for pages: 0,44 .. 446,252.
#pragma once

#include <lvgl.h>
#include "../config.h"   // SCREEN_WIDTH/SCREEN_HEIGHT

#define SHELL_STRIP_H   44
#define SHELL_RAIL_W    34
#define SHELL_FOOTER_H  20
#define CONTENT_X 0
#define CONTENT_Y SHELL_STRIP_H
#define CONTENT_W (SCREEN_WIDTH - SHELL_RAIL_W)    // 446
#define CONTENT_H (SCREEN_HEIGHT - SHELL_STRIP_H - SHELL_FOOTER_H)  // 208

extern lv_obj_t *shell_jumps_label;   // big jump count
extern lv_obj_t *shell_fuel_arc;
extern lv_obj_t *shell_fuel_label;    // cyan % value
extern lv_obj_t *shell_hull_arc;
extern lv_obj_t *shell_hull_label;
extern lv_obj_t *shell_cargo_label;   // "12/128"
extern lv_obj_t *shell_mode_label;    // footer right: ON FOOT/SHIP/...
// Relocated from the old info-screen header/sidebar (names kept so the
// existing update functions in main.cpp stay valid):
extern lv_obj_t *wifi_icon;
extern lv_obj_t *websocket_icon;
extern lv_obj_t *bluetooth_icon;
extern lv_obj_t *status_label;        // footer left: current system name

void create_shell_ui();               // call once in setup() after lv_init
void shell_set_active_tab(int page);  // 0=FTR 1=LOG 2=SYS; hold lvglMutex
```

- [ ] **Step 3: Create `src/screens/shell.cpp`**

```cpp
#include "shell.h"
#include "../config.h"
#include "../colors.h"
#include "../theme.h"

lv_obj_t *shell_jumps_label = nullptr;
lv_obj_t *shell_fuel_arc = nullptr;
lv_obj_t *shell_fuel_label = nullptr;
lv_obj_t *shell_hull_arc = nullptr;
lv_obj_t *shell_hull_label = nullptr;
lv_obj_t *shell_cargo_label = nullptr;
lv_obj_t *shell_mode_label = nullptr;
lv_obj_t *wifi_icon = nullptr;
lv_obj_t *websocket_icon = nullptr;
lv_obj_t *bluetooth_icon = nullptr;
lv_obj_t *status_label = nullptr;

static lv_obj_t *tab_buttons[3] = {nullptr, nullptr, nullptr};

// Page switches must not run inside an LVGL event callback (the callback runs
// under lvglMutex; switchToPage takes it again -> timeout no-op). Defer to
// loop() via this flag, exactly like reqRestartWifi.
extern volatile int reqPageSwitch;

static void tab_event_cb(lv_event_t *e) {
  int page = (int)(intptr_t)lv_event_get_user_data(e);
  reqPageSwitch = page;
}

static lv_obj_t* make_zone(lv_obj_t *parent) {
  lv_obj_t *z = lv_obj_create(parent);
  lv_obj_set_style_bg_color(z, LV_COLOR_BG, 0);
  lv_obj_set_style_bg_opa(z, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(z, 0, 0);
  lv_obj_set_style_radius(z, 0, 0);
  lv_obj_set_style_pad_all(z, 0, 0);
  lv_obj_set_scrollbar_mode(z, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(z, LV_DIR_NONE);
  lv_obj_remove_flag(z, LV_OBJ_FLAG_CLICKABLE);  // click-through for touches
  return z;
}

static lv_obj_t* dim_label(lv_obj_t *parent, const char *txt, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, LV_COLOR_DIM, 0);
  lv_obj_set_style_text_font(l, FONT_DISPLAY_LABEL, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

static lv_obj_t* value_label(lv_obj_t *parent, const char *txt, int x, int y) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_color(l, LV_COLOR_VALUE, 0);
  lv_obj_set_style_text_font(l, FONT_SMALL, 0);
  lv_obj_set_pos(l, x, y);
  return l;
}

static lv_obj_t* make_arc(lv_obj_t *parent, int x) {
  lv_obj_t *a = lv_arc_create(parent);
  lv_obj_set_size(a, 34, 34);
  lv_obj_set_pos(a, x, 4);
  lv_arc_set_rotation(a, 270);
  lv_arc_set_bg_angles(a, 0, 360);
  lv_arc_set_range(a, 0, 100);
  lv_arc_set_value(a, 100);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);           // read-only look
  lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_arc_width(a, 4, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 4, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, LV_COLOR_GAUGE_BG, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, LV_COLOR_FG, LV_PART_INDICATOR);
  return a;
}

void create_shell_ui() {
  lv_obj_t *top = lv_layer_top();
  lv_obj_remove_flag(top, LV_OBJ_FLAG_CLICKABLE);

  // ---- metrics strip ----
  lv_obj_t *strip = make_zone(top);
  lv_obj_set_size(strip, SCREEN_WIDTH, SHELL_STRIP_H);
  lv_obj_set_pos(strip, 0, 0);
  lv_obj_set_style_border_side(strip, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(strip, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(strip, 1, 0);

  shell_jumps_label = lv_label_create(strip);
  lv_label_set_text(shell_jumps_label, "0");
  lv_obj_set_style_text_color(shell_jumps_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(shell_jumps_label, FONT_DISPLAY_BIG, 0);
  lv_obj_set_pos(shell_jumps_label, 10, 6);
  dim_label(strip, "JUMPS", 48, 22);

  shell_fuel_arc = make_arc(strip, 108);
  shell_fuel_label = value_label(strip, "--%", 148, 7);
  dim_label(strip, "FUEL", 148, 22);

  shell_hull_arc = make_arc(strip, 212);
  shell_hull_label = value_label(strip, "--%", 252, 7);
  dim_label(strip, "HULL", 252, 22);

  shell_cargo_label = value_label(strip, "0/0", 318, 7);
  dim_label(strip, "CARGO", 318, 22);

  bluetooth_icon = lv_label_create(strip);
  lv_label_set_text(bluetooth_icon, LV_SYMBOL_BLUETOOTH);
  websocket_icon = lv_label_create(strip);
  lv_label_set_text(websocket_icon, LV_SYMBOL_REFRESH);
  wifi_icon = lv_label_create(strip);
  lv_label_set_text(wifi_icon, LV_SYMBOL_WIFI);
  lv_obj_t *icons[3] = {bluetooth_icon, websocket_icon, wifi_icon};
  for (int i = 0; i < 3; i++) {
    lv_obj_set_style_text_color(icons[i], LV_COLOR_HAIRLINE, 0);  // off state
    lv_obj_set_style_text_font(icons[i], FONT_BODY, 0);
    lv_obj_set_pos(icons[i], 398 + i * 28, 13);
  }

  // ---- tab rail ----
  static const char *tab_names[3] = {"FTR", "LOG", "SYS"};
  lv_obj_t *rail = make_zone(top);
  lv_obj_set_size(rail, SHELL_RAIL_W, SCREEN_HEIGHT - SHELL_STRIP_H);
  lv_obj_set_pos(rail, SCREEN_WIDTH - SHELL_RAIL_W, SHELL_STRIP_H);
  lv_obj_set_style_border_side(rail, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_color(rail, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(rail, 1, 0);

  for (int i = 0; i < 3; i++) {
    lv_obj_t *b = lv_button_create(rail);
    tab_buttons[i] = b;
    lv_obj_set_size(b, SHELL_RAIL_W - 1, 76);
    lv_obj_set_pos(b, 1, i * 76);
    lv_obj_set_style_bg_color(b, LV_COLOR_BG, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(b, 0, 0);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(b, LV_COLOR_HAIRLINE, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_add_event_cb(b, tab_event_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, tab_names[i]);
    lv_obj_set_style_text_font(l, FONT_DISPLAY_LABEL, 0);
    lv_obj_set_style_text_color(l, LV_COLOR_DIM, 0);
    lv_obj_center(l);
  }

  // ---- footer ----
  lv_obj_t *footer = make_zone(top);
  lv_obj_set_size(footer, SCREEN_WIDTH - SHELL_RAIL_W, SHELL_FOOTER_H);
  lv_obj_set_pos(footer, 0, SCREEN_HEIGHT - SHELL_FOOTER_H);
  lv_obj_set_style_border_side(footer, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_style_border_color(footer, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(footer, 1, 0);

  status_label = lv_label_create(footer);
  lv_label_set_text(status_label, "Waiting for events...");
  lv_obj_set_style_text_color(status_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(status_label, FONT_SMALL, 0);
  lv_label_set_long_mode(status_label, LV_LABEL_LONG_MODE_DOT);
  lv_obj_set_width(status_label, 300);
  lv_obj_set_pos(status_label, 8, 3);

  shell_mode_label = lv_label_create(footer);
  lv_label_set_text(shell_mode_label, "");
  lv_obj_set_style_text_color(shell_mode_label, LV_COLOR_VALUE, 0);
  lv_obj_set_style_text_font(shell_mode_label, FONT_SMALL, 0);
  lv_obj_set_style_text_align(shell_mode_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_pos(shell_mode_label, 316, 3);
  lv_obj_set_width(shell_mode_label, 122);
}

void shell_set_active_tab(int page) {
  for (int i = 0; i < 3; i++) {
    if (!tab_buttons[i]) return;
    lv_obj_t *label = lv_obj_get_child(tab_buttons[i], 0);
    if (i == page) {
      lv_obj_set_style_bg_color(tab_buttons[i], LV_COLOR_FG, 0);
      lv_obj_set_style_text_color(label, LV_COLOR_HIGHLIGHT_FG, 0);
    } else {
      lv_obj_set_style_bg_color(tab_buttons[i], LV_COLOR_BG, 0);
      lv_obj_set_style_text_color(label, LV_COLOR_DIM, 0);
    }
  }
}
```

- [ ] **Step 4: info.h/info.cpp — release the moved widgets.** In `src/screens/info.h` DELETE the extern lines for `wifi_icon, websocket_icon, bluetooth_icon, status_label`. In `src/screens/info.cpp` DELETE their `lv_obj_t* ... = nullptr;` definitions, the `make_icon` helper + its three calls, and the sidebar `status_label` creation block. (The rest of info.cpp is rebuilt in Task 4 — keep changes here minimal so the build stays green: the header/gauges may remain visually beneath the strip for now.)

- [ ] **Step 5: main.cpp — wire the shell.**
1. Add `#include "screens/shell.h"` after the other screen includes.
2. Add global near `reqRestartWifi`: `volatile int reqPageSwitch = -1;  // set by the shell tab rail (LVGL cb), serviced in loop()`
3. In `setup()` directly AFTER `create_settings_ui()` would be created — concretely: after the `create_logviewer_ui();` call — insert:
```cpp
  Serial.println("[UI] Creating MFD shell...");
  create_shell_ui();
  shell_set_active_tab(currentPage);
```
4. In `loop()`, next to the `reqRestartWifi` service block, add:
```cpp
  if (reqPageSwitch >= 0) {
    int p = reqPageSwitch;
    reqPageSwitch = -1;
    if (p != currentPage) switchToPage(p);
  }
```
5. In `switchToPage`, inside the mutex-held section (after the page's screen is loaded, before `xSemaphoreGive`), add `shell_set_active_tab(page);`.

- [ ] **Step 6: main.cpp — retarget `updateHeader` (arcs + cargo) and `updateStatusLine` (footer + mode).** Replace the body of `updateHeader()` with:

```cpp
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
```
Also change `update_wifi_icon_unlocked` / `update_bluetooth_icon_unlocked`
"off/disconnected" colors from `lv_color_hex(0x000000)` to `LV_COLOR_HAIRLINE`
and the bluetooth "connected" color from white to `LV_COLOR_FG` (grep for
`0xFFFFFF` / `0x000000` inside those two functions).

Replace the body of `updateStatusLine()` with:

```cpp
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
```

- [ ] **Step 7: main.cpp — retire `updateCargoBar`.** Cargo lives in the strip now (updateHeader). Delete the whole `void updateCargoBar() {...}` function and replace every call site (`grep -n "updateCargoBar" src/main.cpp`) with `updateHeader();` — where a call site already calls `updateHeader()` adjacently, just delete the `updateCargoBar()` line. (`cargo_bar` widget itself dies in Task 4.)

- [ ] **Step 8: Build + commit**

```bash
"d:/pio/penv/Scripts/platformio.exe" run -e default
git add -A && git commit -m "V5: MFD shell on lv_layer_top (strip/tab-rail/footer) + palette swap

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```
Transitional state is expected: old page content still fills 480×272 beneath the shell zones until Tasks 4–6.

---

### Task 4: LOG page rebuild (content zone, time column, context panel)

**Files:**
- Modify: `src/screens/info.h`, `src/screens/info.cpp` (rewrite), `src/main.cpp` (`updateLogDisplay` time column, `updateBackpackDisplay` → `updateContextPanel`, jump overlay offset)

**Interfaces:**
- Consumes: shell geometry macros (`CONTENT_*`), `status.exploration` (Task 2), theme fonts.
- Produces: externs `log_label, log_time_label, pin_cards[], pin_title_labels[], pin_genus_labels[], ctx_panel, ctx_rail_label, ctx_lines[4]` and KEEPS `logviewer_screen, jump_overlay_label` plus backpack label names unused → REMOVED (`medpack_label` etc. die; `updateContextPanel` writes `ctx_lines`). `void updateContextPanel()` in main.cpp replaces `updateBackpackDisplay` (all call sites renamed).

UI task: invoke Skill `frontend-design:frontend-design`; palette verbatim.

- [ ] **Step 1: Rewrite `src/screens/info.h`**

```cpp
#pragma once

#include <lvgl.h>
#include "../gamedata.h"   // MAX_PINNED_BODIES

extern lv_obj_t *logviewer_screen;
extern lv_obj_t *log_label;        // event texts (left part of the column)
extern lv_obj_t *log_time_label;   // right-aligned relative ages, same line grid
extern lv_obj_t *jump_overlay_label;

// Pinned body signal cards (filled by updatePinnedSidebarUnlocked in main.cpp;
// caller must hold lvglMutex)
extern lv_obj_t *pin_cards[MAX_PINNED_BODIES];
extern lv_obj_t *pin_title_labels[MAX_PINNED_BODIES];
extern lv_obj_t *pin_genus_labels[MAX_PINNED_BODIES];

// Context panel (bottom of the sidebar): BACKPACK on foot, EXPLORATION else.
// updateContextPanel (main.cpp) fills the rail + 4 lines.
extern lv_obj_t *ctx_panel;
extern lv_obj_t *ctx_rail_label;
extern lv_obj_t *ctx_lines[4];

void create_logviewer_ui();
```

- [ ] **Step 2: Rewrite `src/screens/info.cpp`**

```cpp
#include "info.h"
#include "../config.h"
#include "../colors.h"
#include "../theme.h"
#include "shell.h"

lv_obj_t *logviewer_screen = nullptr;
lv_obj_t *log_label = nullptr;
lv_obj_t *log_time_label = nullptr;
lv_obj_t *jump_overlay_label = nullptr;
lv_obj_t *pin_cards[MAX_PINNED_BODIES] = {nullptr};
lv_obj_t *pin_title_labels[MAX_PINNED_BODIES] = {nullptr};
lv_obj_t *pin_genus_labels[MAX_PINNED_BODIES] = {nullptr};
lv_obj_t *ctx_panel = nullptr;
lv_obj_t *ctx_rail_label = nullptr;
lv_obj_t *ctx_lines[4] = {nullptr};

#define EVENTS_W 226
#define TIME_W   38
#define SIDE_X   (EVENTS_W + TIME_W + 8)          // 272
#define SIDE_W   (CONTENT_W - SIDE_X - 6)         // 168
#define CTX_H    72

static lv_obj_t* rail(lv_obj_t *parent, const char *txt, int x, int y, int w) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, FONT_DISPLAY_LABEL, 0);
  lv_obj_set_style_text_color(l, LV_COLOR_DIM, 0);
  lv_obj_set_style_border_side(l, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_color(l, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(l, 1, 0);
  lv_obj_set_style_pad_bottom(l, 2, 0);
  lv_obj_set_pos(l, x, y);
  lv_obj_set_width(l, w);
  return l;
}

void create_logviewer_ui() {
  logviewer_screen = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(logviewer_screen, LV_COLOR_BG, 0);

  // ---- events column (left) ----
  rail(logviewer_screen, "EVENTS", CONTENT_X + 8, CONTENT_Y + 4, EVENTS_W + TIME_W);

  log_label = lv_label_create(logviewer_screen);
  lv_obj_set_pos(log_label, CONTENT_X + 8, CONTENT_Y + 24);
  lv_obj_set_width(log_label, EVENTS_W);
  lv_label_set_text(log_label, " ");
  lv_label_set_long_mode(log_label, LV_LABEL_LONG_MODE_CLIP);  // 1 line per entry
  lv_obj_set_style_text_color(log_label, LV_COLOR_FG, 0);
  lv_obj_set_style_text_font(log_label, FONT_BODY, 0);

  log_time_label = lv_label_create(logviewer_screen);
  lv_obj_set_pos(log_time_label, CONTENT_X + 8 + EVENTS_W, CONTENT_Y + 24);
  lv_obj_set_width(log_time_label, TIME_W);
  lv_label_set_text(log_time_label, " ");
  lv_obj_set_style_text_align(log_time_label, LV_TEXT_ALIGN_RIGHT, 0);
  lv_obj_set_style_text_color(log_time_label, LV_COLOR_DIM, 0);
  lv_obj_set_style_text_font(log_time_label, FONT_BODY, 0);

  // ---- sidebar (right) ----
  lv_obj_t *side = lv_obj_create(logviewer_screen);
  lv_obj_set_size(side, SIDE_W + 6, CONTENT_H);
  lv_obj_set_pos(side, SIDE_X, CONTENT_Y);
  lv_obj_set_style_bg_color(side, LV_COLOR_BG, 0);
  lv_obj_set_style_border_side(side, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_color(side, LV_COLOR_HAIRLINE, 0);
  lv_obj_set_style_border_width(side, 1, 0);
  lv_obj_set_style_radius(side, 0, 0);
  lv_obj_set_style_pad_all(side, 4, 0);
  lv_obj_set_style_pad_row(side, 3, 0);
  lv_obj_set_scrollbar_mode(side, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(side, LV_DIR_NONE);

  lv_obj_t *sig_rail = rail(side, "SIGNALS", 0, 0, SIDE_W - 8);
  lv_obj_set_pos(sig_rail, 0, 0);

  for (int i = 0; i < MAX_PINNED_BODIES; i++) {
    pin_cards[i] = lv_obj_create(side);
    lv_obj_set_width(pin_cards[i], SIDE_W - 8);
    lv_obj_set_height(pin_cards[i], LV_SIZE_CONTENT);
    lv_obj_set_pos(pin_cards[i], 0, 20);   // stacked by flex below
    lv_obj_add_style(pin_cards[i], &style_card, 0);
    lv_obj_set_scrollbar_mode(pin_cards[i], LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_scroll_dir(pin_cards[i], LV_DIR_NONE);
    lv_obj_set_flex_flow(pin_cards[i], LV_FLEX_FLOW_COLUMN);
    lv_obj_add_flag(pin_cards[i], LV_OBJ_FLAG_HIDDEN);

    pin_title_labels[i] = lv_label_create(pin_cards[i]);
    lv_obj_set_width(pin_title_labels[i], SIDE_W - 18);
    lv_label_set_long_mode(pin_title_labels[i], LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_text(pin_title_labels[i], "");
    lv_obj_set_style_text_font(pin_title_labels[i], FONT_BODY, 0);
    lv_obj_set_style_text_color(pin_title_labels[i], lv_color_hex(0xffb000), 0);

    pin_genus_labels[i] = lv_label_create(pin_cards[i]);
    lv_obj_set_width(pin_genus_labels[i], SIDE_W - 18);
    lv_label_set_long_mode(pin_genus_labels[i], LV_LABEL_LONG_MODE_WRAP);
    lv_label_set_recolor(pin_genus_labels[i], true);
    lv_label_set_text(pin_genus_labels[i], "");
    lv_obj_set_style_text_font(pin_genus_labels[i], FONT_SMALL, 0);
    lv_obj_add_flag(pin_genus_labels[i], LV_OBJ_FLAG_HIDDEN);
  }
  // Stack rail + cards with flex; context panel is bottom-anchored separately.
  lv_obj_set_flex_flow(side, LV_FLEX_FLOW_COLUMN);

  // ---- context panel (bottom of sidebar, fixed) ----
  ctx_panel = lv_obj_create(logviewer_screen);
  lv_obj_set_size(ctx_panel, SIDE_W, CTX_H);
  lv_obj_set_pos(ctx_panel, SIDE_X + 5, CONTENT_Y + CONTENT_H - CTX_H - 2);
  lv_obj_add_style(ctx_panel, &style_card, 0);
  lv_obj_set_scrollbar_mode(ctx_panel, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_scroll_dir(ctx_panel, LV_DIR_NONE);

  ctx_rail_label = lv_label_create(ctx_panel);
  lv_label_set_text(ctx_rail_label, "EXPLORATION");
  lv_obj_set_style_text_font(ctx_rail_label, FONT_DISPLAY_LABEL, 0);
  lv_obj_set_style_text_color(ctx_rail_label, LV_COLOR_DIM, 0);
  lv_obj_set_pos(ctx_rail_label, 0, 0);

  for (int i = 0; i < 4; i++) {
    ctx_lines[i] = lv_label_create(ctx_panel);
    lv_label_set_text(ctx_lines[i], "");
    lv_obj_set_style_text_font(ctx_lines[i], FONT_SMALL, 0);
    lv_obj_set_style_text_color(ctx_lines[i], LV_COLOR_FG, 0);
    lv_obj_set_pos(ctx_lines[i], 0, 15 + i * 13);
  }
}
```
NOTE for the implementer: `side` uses flex for rail+cards while `ctx_panel` is
a sibling anchored over the sidebar's bottom — cards that would overlap it are
covered; acceptable per spec (~3 cards visible).

- [ ] **Step 3: main.cpp — time column in `updateLogDisplay`.** Add this static helper directly above `updateLogDisplay`:

```cpp
// Relative age like "9s" / "12m" / "2h" for the log time column.
static void formatAge(char* out, size_t n, uint32_t thenMs) {
  uint32_t d = (millis() - thenMs) / 1000;
  if (d < 60)        snprintf(out, n, "%lus", (unsigned long)d);
  else if (d < 3600) snprintf(out, n, "%lum", (unsigned long)(d / 60));
  else               snprintf(out, n, "%luh", (unsigned long)(d / 3600));
}
```
In `updateLogDisplay`, alongside the existing text assembly, build a parallel
ages string: declare `char ageText[160]; int ageLen = 0;` next to
`currentLen`; inside the entry loop, after an entry line is appended to
`logText`, append its age line:
```cpp
      char age[8];
      formatAge(age, sizeof(age), eventLog[idx].timestamp);
      ageLen += snprintf(ageText + ageLen, sizeof(ageText) - ageLen, "%s\n", age);
```
and after `lv_label_set_text(log_label, logText);` add:
```cpp
    if (log_time_label) lv_label_set_text(log_time_label, ageText);
```
Bounds: guard `ageLen < (int)sizeof(ageText) - 8` in the loop. (Replayed
entries carry `millis()`-at-replay timestamps → they all read "0s" right
after boot; accepted per spec.)

- [ ] **Step 4: main.cpp — `updateBackpackDisplay` → `updateContextPanel`.** Replace the entire function with:

```cpp
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
```
Then `grep -n "updateBackpackDisplay" src/main.cpp` and rename EVERY call site
to `updateContextPanel()` (declaration/forward decls too). Additionally call
`updateContextPanel();` after each of the three `explorationReset();` lines
from Task 2 and once after the `FSSSignalDiscovered` branch counts a new
station:
```cpp
      if ((doc["IsStation"] | false) &&
          explorationStation(doc["SignalName"] | "", doc["SignalType"] | "")) {
        updateContextPanel();
      }
```
Also add `updateContextPanel();` right after the exploration data hooks for
FSSDiscoveryScan/FSSAllBodiesFound/Scan/SAAScanComplete (one call covering
the block is fine — panel refresh is cheap and deduped by the mutex).

- [ ] **Step 5: main.cpp — jump overlay into the content zone.** In `showJumpOverlay`, replace `lv_obj_center(jump_overlay_label);` with:
```cpp
    lv_obj_align(jump_overlay_label, LV_ALIGN_TOP_MID, -SHELL_RAIL_W / 2,
                 CONTENT_Y + CONTENT_H / 2 - 20);
```
and ensure main.cpp includes `screens/shell.h` (done in Task 3).

- [ ] **Step 6: Build + commit**

```bash
"d:/pio/penv/Scripts/platformio.exe" run -e default
git add -A && git commit -m "V5: LOG page in content zone - time column, signals sidebar, context panel (backpack/exploration)

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 5: FTR page (grid in zone, umlauts)

**Files:**
- Modify: `src/screens/fighter.cpp`

**Interfaces:**
- Consumes: `CONTENT_*` macros (`#include "shell.h"`), `FONT_BTN`. BLE IDs/`btn_to_cmd`/`commands[]` UNTOUCHED (the PROGMEM command names stay ASCII — they only go to Serial).

UI task: invoke Skill `frontend-design:frontend-design`.

- [ ] **Step 1: Adapt `create_fighter_ui()`**
1. `#include "shell.h"` at the top.
2. Button map gets real German (display only):
```cpp
static const char * btnm_map[] = {"Zurück", "Verteid.", "Feuer", "\n",
                                  "Folgen", "Center", "Angriff", "\n",
                                  "Position", "Formation", "Befehle", ""};
```
3. Size/position into the content zone: replace the existing set_size/set_pos with
```cpp
  lv_obj_set_size(btnmatrix, CONTENT_W - 4, CONTENT_H - 4);
  lv_obj_set_pos(btnmatrix, CONTENT_X + 2, CONTENT_Y + 2);
```
4. Item font `FONT_BTN` (replaces FONT_HEAD), item radius 2, item border color `LV_COLOR_HAIRLINE`, pressed state = `LV_COLOR_FG` background with `LV_COLOR_HIGHLIGHT_FG` text:
```cpp
  lv_obj_set_style_text_font(btnmatrix, FONT_BTN, LV_PART_ITEMS);
  lv_obj_set_style_radius(btnmatrix, 2, LV_PART_ITEMS);
  lv_obj_set_style_border_color(btnmatrix, LV_COLOR_HAIRLINE, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(btnmatrix, LV_COLOR_FG, LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_text_color(btnmatrix, LV_COLOR_HIGHLIGHT_FG, LV_PART_ITEMS | LV_STATE_PRESSED);
```
(The old `LV_COLOR_HIGHLIGHT_BG` pressed line is replaced by these.) Keep the outer container styles; drop the outer `radius 10` to 0 and outer border to `LV_COLOR_HAIRLINE`.

- [ ] **Step 2: Build + commit**

```bash
"d:/pio/penv/Scripts/platformio.exe" run -e default
git add src/screens/fighter.cpp && git commit -m "V5: fighter pad in content zone, Jura font with umlauts, inverted press state

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 6: SYS page (fit into zone)

**Files:**
- Modify: `src/screens/system.cpp`

**Interfaces:**
- Consumes: `CONTENT_*` (`#include "shell.h"`), theme styles/fonts. Externs to main.cpp unchanged (`sys_info_label` etc. keep their names).

UI task: invoke Skill `frontend-design:frontend-design`.

- [ ] **Step 1: Re-layout `create_settings_ui()`** — content zone is 446×208; the rail shows "SYS" so drop the big title. Concrete changes:
1. `#include "shell.h"`.
2. Delete the `title` label block ("SYSTEM").
3. `info_area`: size `(250, CONTENT_H - 8)` at `(4, CONTENT_Y + 4)`; `sys_info_label` width 232.
4. Right column: `#define RIGHT_X 262` and `#define RIGHT_W (CONTENT_W - RIGHT_X - 6)` (=178); brightness label at `(RIGHT_X, CONTENT_Y + 4)`, slider at `(RIGHT_X, CONTENT_Y + 22)`; volume label `(RIGHT_X, CONTENT_Y + 44)`, slider `(RIGHT_X, CONTENT_Y + 62)`; buttons via the existing `make_button` at y `CONTENT_Y + 86`, `CONTENT_Y + 126`, `CONTENT_Y + 166`, height 32.
5. Slider theming so nothing default-blue remains:
```cpp
  lv_obj_set_style_bg_color(slider, LV_COLOR_GAUGE_BG, LV_PART_MAIN);
  lv_obj_set_style_bg_color(slider, LV_COLOR_FG, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(slider, LV_COLOR_FG, LV_PART_KNOB);
```
(apply to both sliders — factor a small `static void style_slider(lv_obj_t*)` helper).
6. Volume shows percent: in `updateAmplitudeDisplay` change the format to `"Volume: %d%%"` with `amplitude * 100 / 12000` (keep `AUDIO_TONE_AMPL` raw internally).

- [ ] **Step 2: Build + commit**

```bash
"d:/pio/penv/Scripts/platformio.exe" run -e default
git add src/screens/system.cpp && git commit -m "V5: system page fitted to content zone, themed sliders, volume in percent

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 7: Remove page overlay, add page fade

**Files:**
- Modify: `src/main.cpp`, `src/config.h`

**Interfaces:**
- Consumes: tab rail (Task 3) as the page indicator.
- Produces: `PAGE_FADE_MS` config define (0 disables).

- [ ] **Step 1: Remove the overlay.** Delete from main.cpp: `showPageOverlay` function, `page_overlay`/`page_overlay_opa_exec`/`page_overlay_anim_ready` statics, the forward declaration `void showPageOverlay(const char* name);`, the `pageNames[3]` array in the TTP block and BOTH `showPageOverlay(pageNames[currentPage]);` calls. `grep -n "showPageOverlay\|pageNames\|page_overlay" src/main.cpp` must return nothing.

- [ ] **Step 2: Fade.** In `src/config.h` add:

```cpp
// Page-switch fade duration in ms (0 = instant). Full-screen redraws are
// pushed over blocking QSPI - keep this short or zero if it stutters.
#define PAGE_FADE_MS 120
```
In `switchToPage`, replace each `lv_screen_load(...)` call with:

```cpp
#if PAGE_FADE_MS > 0
      lv_screen_load_anim(fighter_screen, LV_SCR_LOAD_ANIM_FADE_IN, PAGE_FADE_MS, 0, false);
#else
      lv_screen_load(fighter_screen);
#endif
```
(same pattern for `logviewer_screen` and `settings_screen`).

- [ ] **Step 3: Build + commit**

```bash
"d:/pio/penv/Scripts/platformio.exe" run -e default
git add -A && git commit -m "V5: drop page-title overlay (tab rail shows state), add optional page fade

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
```

---

### Task 8: Docs, size, final verification

**Files:**
- Modify: `CHANGELOG.md`, `CLAUDE.md`

- [ ] **Step 1: CHANGELOG entry** (top, match existing format): V5 MFD overhaul — persistent shell on `lv_layer_top` (strip/rail/footer, tappable tabs + TTP cycle), custom Michroma/Jura fonts WITH umlauts (~+50 KB), pages rebuilt into 446×208 content zone, context panel (Backpack on foot / Exploration in ship: honk, bodies, scan/map, first-disc/first-map, stations by pad size), FSSSignalDiscovered un-ignored (silent station counting), page-title overlay removed, measured firmware size + updated OTA-budget note.

- [ ] **Step 2: CLAUDE.md updates:** screens section (shell module, content-zone geometry, context panel, exploration events), fonts note (generated via lv_font_conv, umlauts, `tools/fonts/`), removal of the page-overlay mention if any, the "Update-Logik" sentence gains `updateContextPanel`/`shell_set_active_tab`. Keep everything else.

- [ ] **Step 3: Measure + record**

```bash
"d:/pio/penv/Scripts/platformio.exe" run -e default 2>&1 | grep -E "Flash|RAM"
ls -la .pio/build/default/firmware.bin
```
Write the numbers into the CHANGELOG entry.

- [ ] **Step 4: Commit + push**

```bash
git add CHANGELOG.md CLAUDE.md
git commit -m "Docs: V5 MFD overhaul (shell, fonts, exploration panel) + size budget

Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>"
git push -u origin v5-mfd
```

- [ ] **Step 5: USER VERIFY (on device, user flashes)**
1. Shell: strip values (jumps/fuel arc/hull arc/cargo) live on all three pages; icons orange when connected.
2. Tabs: tap FTR/LOG/SYS switches pages; TTP pads still cycle; active tab inverted orange; middle pad display-off unchanged.
3. LOG: time column ages count up; pins show; context panel shows EXPLORATION in ship and BACKPACK on foot.
4. sample.json replay ⇒ context panel `HONK OK · BODIES 8/8 OK`, `SCAN 9 · MAP 1`, `FIRST: DISC 0 · MAP 1`; SYS diagnostics line `Expl: HONK+ALL B:8 S:9 M:1 1st:0/1`.
5. FTR: umlauts render („Zurück“, „Befehle“); press = inverted orange; commands arrive via BLE.
6. Stations line in a populated system (e.g. `1L 2M`).
7. Fade acceptable? If stuttering: set `PAGE_FADE_MS 0`.

Merge to main only after user sign-off (superpowers:finishing-a-development-branch).
