# Animiertes ED-Boot-Logo (Loader-Ring) — Design

**Datum:** 2026-07-08
**Status:** Genehmigt (Komposition B — nur Ring)

## Ziel

Das statische Boot-Splash (ED-Wings-Emblem, `ed_logo.h`) wird durch eine
prozedural gerenderte Nachbildung des Elite-Dangerous-Loader-Rings ersetzt
([EDLoader1.svg](https://edassets.org/static/img/svg/EDLoader1.svg)): 24 orange
Dreiecke in Ringanordnung, deren Deckkraft als wandernde Welle pulsiert. Der
Loader läuft während des Boots, **solange auf die WiFi-Verbindung gewartet wird**
(bis `WL_CONNECTED` oder Timeout), und wird danach vom ersten echten UI-Frame
abgelöst.

Das Wings-Emblem entfällt vollständig. Prozedurales Rendern statt Frame-Sequenz
→ nahezu 0 zusätzlicher Flash.

## Nicht-Ziele (YAGNI)

- Keine Loader-Anzeige über der laufenden UI bis WebSocket/Icarus-Daten kommen
  (die Shell zeigt WS/BLE-Status ohnehin per Icons).
- Keine LVGL-Animation während des Boots (kein `lv_timer_handler`-Pumpen, kein
  Extra-Task, keine Mutex-Interaktion).
- Keine Rotation/Formänderung — die Original-Animation ist reine Deckkraft.

## Quelle: SVG-Analyse

`EDLoader1.svg`: viewBox `0 0 40 40`, tatsächlicher Inhalt spannt x∈[0,40],
y∈[0,32]. Fill `#ff7100`. 24 Dreieck-Pfade (18 äußere Klasse `l1`, 6 innere
`l2`), abwechselnd auf-/abwärts, je 10×8 Einheiten groß.

Animation (CSS `@keyframes`, 1000 ms linear infinite):
- `outer` (l1): opacity 0.3 → 1.0 (bei 20 %) → 0.3
- `inner` (l2): opacity 0.4 → 1.0 (bei 20 %) → 0.4
- Pro Dreieck ein `animation-delay` aus der `dN`-Klasse: `delay = N · (1000/19) ms`.
  Der Delay ist der Phasenversatz → heller Puls wandert einmal/Sekunde um den Ring.

## Geometrie (Ground Truth)

Vertices in viewBox-Einheiten, aus den SVG-Pfaden `m X,Y l dx1,dy1 l dx2,dy2 z`
ausgerechnet: `V0=(X,Y)`, `V1=V0+(dx1,dy1)`, `V2=V1+(dx2,dy2)`.

Äußerer Ring (`l1`, Basis-Deckkraft 0.3):

| # | Vertices | delay-Klasse N |
|---|----------|----------------|
| 1 | (5,8)(10,16)(15,8) | 1 |
| 2 | (5,8)(10,0)(15,8) | 2 |
| 3 | (10,0)(15,8)(20,0) | 3 |
| 4 | (15,8)(20,0)(25,8) | 4 |
| 5 | (20,0)(25,8)(30,0) | 5 |
| 6 | (25,8)(30,0)(35,8) | 6 |
| 7 | (25,8)(30,16)(35,8) | 7 |
| 8 | (30,16)(35,8)(40,16) | 8 |
| 9 | (30,16)(35,24)(40,16) | 9 |
| 10 | (25,24)(30,16)(35,24) | 10 |
| 11 | (25,24)(30,32)(35,24) | 11 |
| 12 | (20,32)(25,24)(30,32) | 13 |
| 13 | (15,24)(20,32)(25,24) | 14 |
| 14 | (10,32)(15,24)(20,32) | 15 |
| 15 | (5,24)(10,32)(15,24) | 16 |
| 16 | (5,24)(10,16)(15,24) | 17 |
| 17 | (0,16)(5,24)(10,16) | 18 |
| 18 | (0,16)(5,8)(10,16) | 20 |

Innerer Ring (`l2`, Basis-Deckkraft 0.4):

| # | Vertices | delay-Klasse N |
|---|----------|----------------|
| 19 | (10,16)(15,8)(20,16) | 0 |
| 20 | (15,8)(20,16)(25,8) | 3 |
| 21 | (20,16)(25,8)(30,16) | 6 |
| 22 | (20,16)(25,24)(30,16) | 9 |
| 23 | (15,24)(20,16)(25,24) | 12 |
| 24 | (10,16)(15,24)(20,16) | 15 |

`delay_ms = (N · 1000 / 19)`, danach `mod 1000` (N=20 → ≈52 ms ≈ N=1; N=0 → 0).

## Pulskurve

`phase p = ((elapsedMs − delay_ms) mod 1000) / 1000`, in [0,1).

Basis-Deckkraft `base` = 0.3 (äußerer Ring) bzw. 0.4 (innerer Ring):
- `p ∈ [0, 0.2]`:  `b = base + (1 − base) · (p / 0.2)`
- `p ∈ [0.2, 1]`:  `b = 1 − (1 − base) · ((p − 0.2) / 0.8)`

Farbe pro Frame: `RGB565( round(0xFF·b), round(0x71·b), 0 )`
(0x71 = 113 = Grün-Anteil von #ff7100; Blau-Anteil ist 0).

## Skalierung & Position

Da der Ring das einzige Element ist, prominent und mittig:
- Skalierungsfaktor `s` (viewBox → Pixel), Richtwert `s = 6.0` → Ring ≈ 240×192 px.
- Zentriert: `ox = (SCREEN_WIDTH − 40·s)/2`, `oy = (SCREEN_HEIGHT − 32·s)/2`.
- Displaykoordinate eines Vertex: `(ox + x·s, oy + y·s)`.
- Geometrie **einmal** beim Boot in eine 24×3-Punkte-Tabelle (int16) vorberechnet.

Exakter Faktor wird im Implementierungsplan festgelegt (muss in 480×272 passen,
mit etwas Rand).

## Rendering

Neues, in [src/display.cpp](src/display.cpp) gekapseltes Loader-Modul (dort lebt
der `static panel`), exponiert über die `Display`-Klasse in
[src/display.h](src/display.h):

- `void bootLoaderInit()` — Vertices einmal auf Displaykoordinaten skalieren.
- `void drawBootLoaderFrame(uint32_t elapsedMs)` — für jedes der 24 Dreiecke
  `phase`/`b`/Farbe berechnen und `panel->fillTriangle(x0,y0,x1,y1,x2,y2, color)`.

Kein Clear pro Frame: Geometrie ist fix, es ändert sich nur die Farbe, also wird
jedes Dreieck direkt in seiner neuen Stufe überzeichnet. Der schwarze Zwischenraum
bleibt schwarz (initiales `panel->fillScreen(BLACK)` bleibt). → flackerfrei,
~24 `fillTriangle` pro Frame, für den S3/QSPI vernachlässigbar.

`panel` ist `Arduino_NV3041A` (erbt `Arduino_GFX`) und zeichnet direkt ins
Panel-GRAM — `fillTriangle`/`fillRect`/`fillScreen` sind verfügbar.

## Timing / Mechanik

`WifiConnect()` blockiert nicht (async über WiFi-Events). Die WiFi-Warteschleife
*wird* die Animationsschleife. Ablauf in `setup()` ([src/main.cpp](src/main.cpp)):

1. `disp.init()`: `panel->begin()`, `fillScreen(BLACK)`. **Der statische
   `ed_logo`-Draw entfällt.** (LVGL-Init/Buffers/Display-Create bleiben.)
2. `bleGamepad->begin()`, I2S-, PCF-Init laufen wie bisher (schnell, WiFi-unabhängig).
3. `WifiConnect()` (startet WiFi async).
4. **Neue Loader-Schleife**, direkt vor dem UI-Aufbau:
   ```cpp
   disp.bootLoaderInit();
   uint32_t start = millis();
   while (WiFi.status() != WL_CONNECTED &&
          millis() - start < BOOT_LOADER_TIMEOUT_MS) {
       disp.drawBootLoaderFrame(millis() - start);
       delay(BOOT_LOADER_FRAME_MS);   // ~33 ms → ~30 fps
   }
   ```
5. UI bauen (`theme_init`, `create_*_ui`, `lv_screen_load`) → `lv_refr_now()`
   ersetzt den Ring in einem Rutsch durch die echte UI.

Kein Hängenbleiben: ohne WiFi läuft der Ring bis Timeout und geht dann trotzdem
in die UI. Der `msgTask` (loop2) wird — wie bisher — erst so gestartet, dass er
den Loader nicht stört (im Plan verifizieren, dass sein Start nach der Schleife
liegt bzw. er weder Panel noch LVGL vor UI-Existenz anfasst).

## Konfiguration

Neu in [src/config.h](src/config.h):
- `BOOT_LOADER_TIMEOUT_MS` (Richtwert 8000) — max. Wartezeit auf WiFi.
- `BOOT_LOADER_FRAME_MS` (Richtwert 33) — Frame-Intervall.

## Aufräumen

- Statischen `ed_logo`-Draw und `#include "ed_logo.h"` aus
  [src/display.cpp](src/display.cpp) entfernen.
- `src/ed_logo.h` und `ed_logo.png` werden dadurch unbenutzt → im Plan entscheiden,
  ob löschen (empfohlen, hält Repo sauber) oder behalten.

## Betroffene Dateien

- [src/display.cpp](src/display.cpp) — Loader-Modul + `ed_logo`-Draw raus.
- [src/display.h](src/display.h) — `bootLoaderInit` / `drawBootLoaderFrame` deklarieren.
- [src/main.cpp](src/main.cpp) — Loader-/WiFi-Warteschleife in `setup()`.
- [src/config.h](src/config.h) — Timeout/Frame-Konstanten.
- `src/ed_logo.h`, `ed_logo.png` — entfernen (optional).

## Test / Verifikation

Kein Test-Suite im Projekt. Verifikation = Build (`-e default`) und Flash durch
den User: Ring pulst mittig beim Boot, Welle wandert um den Ring, Übergang zur
UI sobald WiFi steht bzw. nach Timeout; kein Flackern, kein Hängenbleiben ohne WiFi.
