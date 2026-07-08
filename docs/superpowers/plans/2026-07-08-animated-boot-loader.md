# Animated ED Boot Loader — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ersetze das statische ED-Wings-Boot-Splash durch einen prozedural gerenderten, pulsierenden ED-Loader-Ring (24 Dreiecke), der animiert, solange auf die WiFi-Verbindung gewartet wird.

**Architecture:** Ein in `src/display.cpp` gekapseltes Loader-Modul (Zugriff auf den dortigen `static panel`) rendert die 24 Dreiecke direkt via `panel->fillTriangle()` ins Panel-GRAM — kein LVGL, kein Extra-Task, kein Clear pro Frame (nur die Farbe ändert sich, Geometrie ist fix). In `setup()` wird die WiFi-Warteschleife zur Animationsschleife; danach ersetzt der erste LVGL-Frame den Ring.

**Tech Stack:** ESP32-S3, Arduino_GFX (`Arduino_NV3041A` Panel), PlatformIO (pioarduino fork), C++.

## Global Constraints

- Build/Flash: PlatformIO env `default`, `pio` ist **nicht** auf PATH → voller Pfad `"d:\pio\penv\Scripts\platformio.exe"`. Build-Check: `"d:\pio\penv\Scripts\platformio.exe" run -e default`.
- **Kein Test-Suite im Projekt** (per CLAUDE.md). „Test" pro Task = sauberer Compile; finale Verifikation = Flash + Sichtprüfung durch den User (User flasht selbst).
- Panel zeichnet direkt ins GRAM; RGB565-Farben via `RGB565(r,g,b)`-Makro (wie `RGB565_BLACK` in display.cpp:89). `fillTriangle(x0,y0,x1,y1,x2,y2,color)` ist auf `panel` verfügbar (Arduino_GFX-Basisklasse).
- Farbe #ff7100 → R=0xFF, G=0x71 (113), B=0x00.
- Geometrie-Quelle: `EDLoader1.svg`, viewBox 0..40 × 0..32, 24 Dreiecke, delay-Klasse N → Phasenversatz `N·1000/19` ms.
- `disp` ist global (`Display disp;` in main.cpp:87). `msgTask` startet erst main.cpp:2358 (nach UI-Aufbau) → kein Konflikt mit der Loader-Schleife.

---

### Task 1: Loader-Renderer-Modul + `Display`-API + Config + statisches Logo raus

**Files:**
- Modify: `src/config.h` (drei Konstanten ergänzen)
- Modify: `src/display.h` (zwei Methoden deklarieren)
- Modify: `src/display.cpp` (Loader-Modul + Methoden; `ed_logo`-Include und statischen Draw entfernen; in `init()` Ring-Frame 0 zeichnen)

**Interfaces:**
- Produces:
  - `void Display::bootLoaderInit();` — berechnet die 24 skalierten Dreieck-Vertices einmal (idempotent).
  - `void Display::drawBootLoaderFrame(uint32_t elapsedMs);` — zeichnet einen Frame des Rings.
  - `#define BOOT_LOADER_TIMEOUT_MS`, `BOOT_LOADER_FRAME_MS`, `BOOT_LOADER_SCALE` in config.h.

- [ ] **Step 1: Config-Konstanten ergänzen**

In `src/config.h` an passender Stelle (z. B. bei den Display-Geometrie-Defines) einfügen:

```cpp
// --- Boot loader ring animation (ED loader) ---
#define BOOT_LOADER_TIMEOUT_MS 8000   // max wait for WiFi before showing UI anyway
#define BOOT_LOADER_FRAME_MS   33     // frame interval (~30 fps)
#define BOOT_LOADER_SCALE      7.0f   // viewBox(40x32) -> px: 280x224, centered
```

- [ ] **Step 2: `Display`-Klasse erweitern**

In `src/display.h` die Klasse ergänzen:

```cpp
class Display
{
public:
    lv_display_t* init();
    void setTouchCallback(void (*cb)());
    void bootLoaderInit();
    void drawBootLoaderFrame(uint32_t elapsedMs);
};
```

- [ ] **Step 3: `ed_logo`-Include entfernen und math.h ergänzen**

In `src/display.cpp` die Include-Zeile `#include "ed_logo.h"` (Zeile 7) entfernen und `#include <math.h>` ergänzen. Ergebnis der Include-Sektion (Zeilen 1–7):

```cpp
#include "display.h"
#include "config.h"
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <bb_captouch.h>
#include <esp_heap_caps.h>
#include <math.h>
```

- [ ] **Step 4: Loader-Modul (Geometrie + Renderer) in display.cpp einfügen**

Direkt **vor** der Zeile `lv_display_t* Display::init(void)` (aktuell Zeile 84) einfügen:

```cpp
/* -------- ED loader ring (boot splash) ----------------------------------
   24 triangles from EDLoader1.svg (viewBox 0..40 x 0..32). Only opacity
   pulses (base -> 1 -> base, 1000ms), staggered per delay-class N so a bright
   wave travels around the ring. Geometry is fixed, so each frame just redraws
   all 24 triangles in their current orange shade over the black screen — no
   per-frame clear, no flicker. */
struct BootTri {
    uint8_t x0, y0, x1, y1, x2, y2;  // viewBox-unit vertices (0..40 / 0..32)
    float   base;                    // pulse floor: 0.3 outer ring, 0.4 inner
    uint8_t delayN;                  // delay class N (phase = N*1000/19 ms)
};

static const BootTri kBootTris[24] = {
    // outer ring (l1), base 0.3
    { 5, 8,10,16,15, 8, 0.3f,  1}, { 5, 8,10, 0,15, 8, 0.3f,  2},
    {10, 0,15, 8,20, 0, 0.3f,  3}, {15, 8,20, 0,25, 8, 0.3f,  4},
    {20, 0,25, 8,30, 0, 0.3f,  5}, {25, 8,30, 0,35, 8, 0.3f,  6},
    {25, 8,30,16,35, 8, 0.3f,  7}, {30,16,35, 8,40,16, 0.3f,  8},
    {30,16,35,24,40,16, 0.3f,  9}, {25,24,30,16,35,24, 0.3f, 10},
    {25,24,30,32,35,24, 0.3f, 11}, {20,32,25,24,30,32, 0.3f, 13},
    {15,24,20,32,25,24, 0.3f, 14}, {10,32,15,24,20,32, 0.3f, 15},
    { 5,24,10,32,15,24, 0.3f, 16}, { 5,24,10,16,15,24, 0.3f, 17},
    { 0,16, 5,24,10,16, 0.3f, 18}, { 0,16, 5, 8,10,16, 0.3f, 20},
    // inner ring (l2), base 0.4
    {10,16,15, 8,20,16, 0.4f,  0}, {15, 8,20,16,25, 8, 0.4f,  3},
    {20,16,25, 8,30,16, 0.4f,  6}, {20,16,25,24,30,16, 0.4f,  9},
    {15,24,20,16,25,24, 0.4f, 12}, {10,16,15,24,20,16, 0.4f, 15},
};

static int16_t bootTriPx[24][6];   // scaled display-px vertices
static bool    bootLoaderReady = false;

// phase in [0,1): rise base->1 over first 20%, linear fall 1->base over rest.
static inline float bootPulse(float phase, float base) {
    if (phase < 0.2f) return base + (1.0f - base) * (phase / 0.2f);
    return 1.0f - (1.0f - base) * ((phase - 0.2f) / 0.8f);
}

void Display::bootLoaderInit() {
    if (bootLoaderReady) return;
    const float   s  = BOOT_LOADER_SCALE;
    const int16_t ox = (int16_t)((panel->width()  - (int)(40 * s)) / 2);
    const int16_t oy = (int16_t)((panel->height() - (int)(32 * s)) / 2);
    for (int i = 0; i < 24; i++) {
        const BootTri& t = kBootTris[i];
        bootTriPx[i][0] = ox + (int16_t)(t.x0 * s);
        bootTriPx[i][1] = oy + (int16_t)(t.y0 * s);
        bootTriPx[i][2] = ox + (int16_t)(t.x1 * s);
        bootTriPx[i][3] = oy + (int16_t)(t.y1 * s);
        bootTriPx[i][4] = ox + (int16_t)(t.x2 * s);
        bootTriPx[i][5] = oy + (int16_t)(t.y2 * s);
    }
    bootLoaderReady = true;
}

void Display::drawBootLoaderFrame(uint32_t elapsedMs) {
    if (!bootLoaderReady) return;
    for (int i = 0; i < 24; i++) {
        const BootTri& t = kBootTris[i];
        float delayMs = t.delayN * (1000.0f / 19.0f);
        float phase = fmodf((float)elapsedMs - delayMs, 1000.0f);
        if (phase < 0) phase += 1000.0f;
        phase /= 1000.0f;
        float   b = bootPulse(phase, t.base);
        uint8_t r = (uint8_t)(0xFF * b + 0.5f);
        uint8_t g = (uint8_t)(0x71 * b + 0.5f);
        uint16_t color = RGB565(r, g, 0);
        panel->fillTriangle(bootTriPx[i][0], bootTriPx[i][1],
                            bootTriPx[i][2], bootTriPx[i][3],
                            bootTriPx[i][4], bootTriPx[i][5], color);
    }
}

```

- [ ] **Step 5: Statischen Logo-Draw in `init()` durch Ring-Frame 0 ersetzen**

In `src/display.cpp`, `Display::init()`, den bestehenden Boot-Splash-Block (aktuell Zeilen 91–95):

```cpp
    /* Boot splash (Elite Dangerous logo), centered. Stays visible while the
       rest of setup() runs, until the first LVGL frame replaces it. */
    panel->draw16bitRGBBitmap((panel->width()  - ED_LOGO_W) / 2,
                              (panel->height() - ED_LOGO_H) / 2,
                              (uint16_t *)ed_logo_map, ED_LOGO_W, ED_LOGO_H);
```

ersetzen durch:

```cpp
    /* Boot splash: ED loader ring, centered. Frame 0 shows immediately; the
       WiFi-wait loop in setup() animates it. First LVGL frame later replaces it. */
    bootLoaderInit();
    drawBootLoaderFrame(0);
```

- [ ] **Step 6: Build-Check**

Run: `"d:\pio\penv\Scripts\platformio.exe" run -e default`
Expected: Compile **erfolgreich** (kein Fehler zu `ed_logo`, `bootLoaderInit`, `fillTriangle`, `RGB565`). Der Ring ist jetzt das statische Splash-Bild (Frame 0), noch nicht animiert.

- [ ] **Step 7: Commit**

```bash
git add src/config.h src/display.h src/display.cpp
git commit -m "Boot splash: procedural ED loader ring replaces static wings logo"
```

---

### Task 2: WiFi-Warteschleife als Animationsschleife in `setup()`

**Files:**
- Modify: `src/main.cpp` (Schleife vor `theme_init();` einfügen, aktuell Zeile 2330)

**Interfaces:**
- Consumes: `disp.drawBootLoaderFrame(uint32_t)` (Task 1), `BOOT_LOADER_TIMEOUT_MS`, `BOOT_LOADER_FRAME_MS` (config.h).

- [ ] **Step 1: Animationsschleife einfügen**

In `src/main.cpp`, `setup()`, unmittelbar **vor** der Zeile `theme_init();` (aktuell Zeile 2330) einfügen:

```cpp
  // Animated ED loader ring: hold the boot splash while WiFi comes up.
  // WifiConnect() started WiFi async above; animate until connected or timeout.
  Serial.println("[BOOT] Loader ring until WiFi (or timeout)...");
  uint32_t bootStart = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - bootStart < BOOT_LOADER_TIMEOUT_MS) {
    disp.drawBootLoaderFrame(millis() - bootStart);
    delay(BOOT_LOADER_FRAME_MS);
  }

```

- [ ] **Step 2: Build-Check**

Run: `"d:\pio\penv\Scripts\platformio.exe" run -e default`
Expected: Compile **erfolgreich**.

- [ ] **Step 3: Commit**

```bash
git add src/main.cpp
git commit -m "Boot: animate ED loader ring while waiting for WiFi (timeout-bounded)"
```

---

### Task 3: Ungenutztes `ed_logo`-Asset entfernen

**Files:**
- Delete: `src/ed_logo.h`
- Delete: `ed_logo.png`

**Interfaces:** keine (Aufräumen; nach Task 1 gibt es keine Referenzen mehr).

- [ ] **Step 1: Sicherstellen, dass keine Referenzen mehr existieren**

Run (Grep): Suche `ed_logo` im gesamten `src/`.
Expected: **kein Treffer** mehr (Include und Draw wurden in Task 1 entfernt).

- [ ] **Step 2: Dateien löschen**

```bash
git rm src/ed_logo.h ed_logo.png
```

- [ ] **Step 3: Build-Check**

Run: `"d:\pio\penv\Scripts\platformio.exe" run -e default`
Expected: Compile **erfolgreich** (nichts referenziert die gelöschten Dateien).

- [ ] **Step 4: Commit**

```bash
git commit -m "Remove unused ed_logo asset (replaced by procedural loader ring)"
```

---

## Finale Verifikation (nach allen Tasks)

Kein Test-Suite → Verifikation auf Hardware durch den User (flasht selbst):

- [ ] Flash `-e default`; beim Boot pulst der orange Ring **mittig** auf schwarzem Grund.
- [ ] Die helle Welle wandert einmal/Sekunde um den Ring (äußerer + innerer Ring).
- [ ] Übergang zur normalen UI, sobald WiFi steht — bzw. spätestens nach `BOOT_LOADER_TIMEOUT_MS`.
- [ ] Ohne WiFi: Ring läuft bis Timeout, dann UI (kein Hängenbleiben).
- [ ] Kein Flackern.

## Self-Review-Ergebnis (Autor)

- **Spec-Coverage:** Geometrie (Task 1 Tabelle), Pulskurve (`bootPulse`), Skalierung/Position (`bootLoaderInit`), Rendering ohne Clear (`drawBootLoaderFrame`), Timing/WiFi-Loop (Task 2), Config (Task 1 Step 1), Aufräumen (Task 3) — alle Spec-Abschnitte abgedeckt.
- **Placeholder-Scan:** keine TBD/TODO; jeder Code-Step enthält vollständigen Code.
- **Typ-Konsistenz:** `bootLoaderInit()` / `drawBootLoaderFrame(uint32_t)` identisch in display.h, display.cpp, init() und main.cpp-Loop. `kBootTris`/`bootTriPx`/`bootPulse`/`bootLoaderReady` konsistent benannt.
