# V5 "MFD" UI Overhaul + Exploration Context Panel (Design)

Date: 2026-07-06
Status: approved by user (brainstorming session; visual direction approved via
mockup artifact "V5 MFD-Komplett-Overhaul")

## Goal

Rebuild the BLEJoy UI as a multi-function display (MFD): a persistent frame
(metrics strip on top, vertical page-tab rail on the right at the height of
the physical TTP223 pads, status footer) around three content pages. Add a
context-sensitive sidebar panel: BACKPACK while on foot, EXPLORATION while in
ship/SRV (honk state, body/scan/map counts, first-discovery/first-map counts,
stations by largest landing pad).

Baseline: `main` @ `212b09c` (JC4827W543 migration merged, hardware-verified).
Work happens on a new branch `v5-mfd`.

## Decisions (user-confirmed)

| Topic | Decision |
|---|---|
| Direction | V5 "MFD-Komplett-Overhaul" from the proposal artifact |
| Custom font | **Yes** (~+50 KB flash accepted) — **must include German umlauts äöüÄÖÜß** |
| Stations display | Count by largest landing pad: `1L 2M` (+ `nFC` for fleet carriers) |
| Context panel | Backpack **only on foot**; in ship/SRV an EXPLORATION panel instead |
| First-in-system | Show first-discovery and first-map counts (from `WasDiscovered`/`WasMapped`) |
| Page-title overlay | Removed — the tab rail shows the active page permanently |

## Shell architecture

**Persistent layer (`lv_layer_top()`)** — chosen over a single-screen
container swap (invasive) and per-screen frame duplication (3× handles or
stale data):

- Strip, tab rail and footer are created ONCE on LVGL's global top layer,
  which overlays every screen. One set of widget handles; update functions
  write once and the values stay correct on all pages.
- The three pages remain normal LVGL screens; `switchToPage`/`lv_screen_load`
  mechanics are unchanged. Pages simply keep the frame zones empty.
- The tab rail is **tappable** (direct page select via panel touch); the
  TTP223 pads keep cycling prev/next as before. Middle pad = display off,
  unchanged.

### Geometry (480×272)

| Zone | Rect | Content |
|---|---|---|
| Metrics strip | 0,0 480×44 | JUMPS (big number) · FUEL arc+% · HULL arc+% · CARGO n/N · BLE/WS/WiFi icons |
| Tab rail | 446,44 34×208 | FTR / LOG / SYS vertical, active = inverted orange, tappable |
| Footer | 0,252 446×20 | system name (left) · mode + conn state (right) |
| Content zone | 0,44 446×208 | per-page content |

Arcs use the stock `lv_arc` widget (read-only indicator mode).

### Fonts (all generated via `lv_font_conv`, OFL-licensed, WITH umlauts)

| Role | Face/size | Range | ~Flash |
|---|---|---|---|
| Display big | Michroma 24 | digits, A–Z, ÄÖÜ, %/.:- | ~25 KB |
| Display label | Michroma 12 | as above, uppercase labels | ~12 KB |
| Button text | Jura (or Michroma) 16 | full Latin incl. äöüÄÖÜß | ~15 KB |
| Body/log | Montserrat 14 (built-in) | ASCII (journal content is ASCII) | 0 |

Fighter labels switch to proper German: „Zurück“, „Befehle“ etc. (the
"Zurueck" spellings existed only because built-in Montserrat lacks umlauts).

### Color climate (from the approved mockup)

True black ground `#020101`; orange `#ff7100` leads (text, arcs, active tab);
cyan `#35c4f0` exclusively for measured values; red only for warnings; hairline
separators `#3a1c00`; dim labels `#8c4700`. Genus color code unchanged
(green `#00c060` = to scan, purple `#c85aff` = in sampler, blue `#4169e1` = done).

### Animation

Content fade-in (~120 ms) on page switch. No full-screen slides (blocking
QSPI flush would make them choppy). Jump overlay stays, centered in the
content zone.

## Pages (content zone 446×208)

- **LOG (default):** left EVENTS column (~225 px, Montserrat 14) with a
  right-aligned relative-age column (from existing `EventLogEntry.timestamp`);
  right column (~210 px): SIGNALS rail + pinned-body cards (genus chips as
  today; ~3 cards visible, overflow clipped) and the **context panel**
  anchored at the bottom (~70 px). System name lives in the footer only.
- **FTR:** 3×3 button grid (~145×64 targets), identical BLE IDs 13–20,
  pressed = inverted orange, button font with umlauts.
- **SYS:** diagnostics text left (FONT_SMALL), brightness/volume sliders and
  Restart WiFi / Restart WS / REBOOT buttons right — all inside the zone.

## Context panel

Header rail shows the mode; content switches on `status.onFoot`:

- **On foot — `BACKPACK · ON FOOT`:** `Med n · Cell n · Bio n` (as today).
- **In ship/SRV/taxi — `EXPLORATION`:**
  ```
  HONK ✓ · BODIES 8/8 ✓      (— before honk; "n" without ALL; "n/n ✓" after FSSAllBodiesFound)
  SCAN 9 · MAP 1
  FIRST: DISC 0 · MAP 1      (orange highlight when either > 0)
  STATIONS 1L 2M 1FC         ("—" when none known)
  ```

`updateBackpackDisplay` becomes `updateContextPanel` (single entry point for
both modes, called on Embark/Disembark and on every exploration event).

## Exploration data model

New block in `StatusModel` (gamedata.h), reset where the pins are cleared
(hyperspace StartJump, FSDJump, Shutdown, CarrierJump):

```cpp
struct ExplorationInfo {
  bool honked = false;        // FSSDiscoveryScan seen
  bool allFound = false;      // FSSAllBodiesFound
  int  bodyCount = 0;         // FSSDiscoveryScan.BodyCount
  int  scanned = 0;           // unique Scan BodyIDs
  int  mapped = 0;            // unique SAAScanComplete BodyIDs
  int  firstDiscovered = 0;   // Scans with WasDiscovered == false (unique)
  int  firstMapped = 0;       // SAAScanComplete on bodies whose Scan had WasMapped == false
  uint8_t stationsL = 0, stationsM = 0, carriers = 0;
  // dedupe backing (private to gamedata.cpp): BodyID bitmaps (256 bit each)
  // for scanned/mapped/wasUnmapped, station-name hash list (24 × uint32).
};
```

Event handling (`handleEliteEvent`):

- `FSSDiscoveryScan` → `honked = true`, `bodyCount` (keeps existing log line
  if any; no new one).
- `FSSAllBodiesFound` → `allFound = true`.
- `Scan` → dedupe by `BodyID`; count; if `WasDiscovered == false` count
  first-discovery; remember `WasMapped` per BodyID for first-map detection
  (Scan always precedes mapping, so the flag is available in time).
- `SAAScanComplete` → dedupe by `BodyID`; count; first-map if the remembered
  `WasMapped` was false.
- `FSSSignalDiscovered` → **removed from `ignoreJournalEvents`**, processed
  SILENTLY (no log entry, high volume): if `IsStation`, dedupe by
  `SignalName` hash, classify `SignalType`: `Outpost` → M;
  `StationCoriolis`/`StationONeilOrbis`/`StationONeilCylinder`/
  `StationBernalSphere`/`StationAsteroid`/`StationMegaShip` → L;
  `FleetCarrier` → FC; anything else (Installation, USS, …) ignored.

`WasDiscovered`/`WasMapped`/`WasFootfalled` semantics (verified against
sample.json entry 78): they describe the state BEFORE the player's scan —
`false` means the player is first. First-footfall is deliberately out of
scope (needs Disembark correlation; panel line is full).

Boot replay: counters fill automatically through the replayed events; the
50-entry window means a mid-session reboot may undercount — same accepted
trade-off as the pinned bodies. Known limitation: planetary surface ports do
not emit FSS station signals and are not counted.

## What changes / what stays

Changes: `theme.*` (new fonts/colors), `display` untouched, new
`src/screens/shell.cpp/.h` (strip/rail/footer + update functions),
info/fighter/system screens rebuilt into the content zone,
`updateHeader`/`updateStatusLine` retarget shell widgets,
`updateBackpackDisplay` → `updateContextPanel`, `showPageOverlay` removed,
`gamedata.*` gains ExplorationInfo, `handleEliteEvent` gains/changes the five
event branches, fighter labels get umlauts.

Stays: TTP navigation + display-off policy, BLE identity/IDs, lvglMutex
discipline, WebSocket/replay flow, pins/genus logic, jump overlay, brightness/
volume settings, boot splash.

## Risks

1. Flash: fonts ~+50 KB → ~70–71 % of 3 MB; OTA return needs a bigger diet
   later (documented in CHANGELOG).
2. `lv_layer_top` interaction: overlay must not steal touches outside its
   widgets (rail is the only interactive element; strip/footer are
   click-through—set `LV_OBJ_FLAG_CLICKABLE` false).
3. Content clipping in the sidebar with 4 pins + genus lines — accepted,
   weakest pin is evicted by count anyway.
4. Michroma license = OFL (fine to embed); generated fonts get committed as
   .c files with the generation command documented.

## Verification

- Build size recorded; sample.json replay must show: HONK ✓, BODIES 8/8 ✓,
  SCAN 9, MAP 1, FIRST: DISC 0 · MAP 1, backpack panel while on foot at end
  of the replayed session.
- On-device: tab rail tap + TTP cycle both work; arcs match fuel/hull; umlauts
  render on FTR buttons; stations line in a populated system (e.g. 1L 2M).

## Out of scope

- First-footfall tracking, OTA partition switch, WS-reconnect freeze fix
  (tracked separately), planetary-port station counting.
