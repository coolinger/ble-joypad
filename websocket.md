# WebSocket API

The service in `src/service/main.js` exposes a single WebSocket endpoint on the same host/port as the HTTP server (default `3300`). Every message is JSON encoded.

- **Client → server envelope**: `{ requestId, name, message? }`
- **Server reply envelope**: `{ requestId, name, message }` (same `requestId` is echoed so callers can correlate responses).
- **Server push envelope**: `{ name, message? }` (no `requestId`; used for broadcasts triggered by local data changes).

## Commands accepted from clients

| name | request message | response message (`message` property) | notes |
| --- | --- | --- | --- |
| `hostInfo` | none | `{ urls: ["http://<lan-ip>:<port>", ...] }` | Enumerates reachable service URLs. |
| `getLoadingStatus` | none | `{ loadingComplete, loadingInProgress, loadingCompleted, loadingTime, numberOfFiles, numberOfEventsImported, numberOfLogLines, logSizeInBytes, lastActivity }` | Mirrors `getLoadingStatus` in `src/service/lib/events.js`. |
| `syncMessage` | any JSON | `undefined` (the handler only rebroadcasts) | Immediately broadcasts `syncMessage` with the provided payload. |
| `getCmdr` | none | `{ commander, credits }` | Uses the most recent `LoadGame` journal event. |
| `getLogEntries` | `{ count? = 100, timestamp? }` | Array of journal events; either the newest `count` entries or all entries after `timestamp`. | Raw Elite journal objects. |
| `getSystem` | `{ name?, useCache? = true }` | Enriched system object including `name`, `address`, `position`, `mode` (`SHIP`/`SRV`/`FOOT`/`TAXI`/`MULTICREW`), docking/body info, `allegiance`, `government`, `economy`, scan progress fields, and cached EDSM data (`stars`, `bodies`, surface scan signal counts, `systemMap` fields). | Defaults to current location when `name` is omitted. |
| `getShipStatus` | none | Snapshot of the current (or last known) ship state: `{ timestamp, type, name, ident, pips {systems, engines, weapons}, maxJumpRange, fuelLevel, fuelReservoir, fuelCapacity, modulePowerDraw, mass, rebuy, armour, cargo {capacity, count, inventory[]}, onBoard, modules }` where `modules` is keyed by slot and includes power/health, ammo/heatsinks, engineering metadata, size/slot info, and module stats (class/rating/mass/power/cost etc). | Falls back to the last known ship state when not on board. |
| `getMaterials` | none | Array of materials `{ symbol, name, type, category, grade, rarity, count, maxCount, blueprints }` with counts replayed from `MaterialCollected`/`MaterialDiscarded`/`EngineerCraft`/`MaterialTrade` after startup. | |
| `getInventory` | none | `{ counts: { goods, components, data }, items: [{ name, type, mission, stolen, count }] }` | Derived from `ShipLocker.json`. |
| `getEngineers` | none | Array of engineers `{ id, name, description, system { address, name, position }, marketId, progress { status, rank, rankProgress } }`, combining local progress with static location data. | |
| `getCmdrStatus` | none | Commander/vehicle status derived from `Status.json` and recent journal events: lower-cased properties from `Status.json`, `flags` map (booleans for ship/foot states), and `_location` array (up to two most-specific location labels). | |
| `getBlueprints` | none | Array of blueprint definitions `{ symbol, name, originalName, grades: [{ grade, components, features }], modules, appliedToModules, engineers }` where `components` includes material counts and `engineers` is enriched with location/progress. | |
| `getNavRoute` | none | `{ currentSystem, destination, jumpsToDestination, route, inSystemOnRoute }` where each `route` entry has `system`, `address`, `position`, `starClass`, `distance`, `isCurrentSystem`, `numberOfStars`, `numberOfPlanets`, `isExplored`. | Based on `NavRoute.json` and cached EDSM lookups. |
| `getPreferences` | none | Persisted preferences JSON from `%LOCALAPPDATA%/ICARUS Terminal/Preferences.json` (empty object when missing). | |
| `setPreferences` | arbitrary preferences JSON | The same preferences object after being written to disk. | Also triggers a `syncMessage` broadcast `{ name: "preferences" }`. |
| `getVoices` | none | Array of installed text-to-speech voice names. | |
| `testMessage` | `{ name, message }` | `undefined` | For development; broadcasts `name`/`message` when `name !== "testMessage"`. |
| `testVoice` | `{ voice }` | `undefined` | Speaks a synthesized confirmation with the requested voice. |
| `toggleSwitch` | `{ switchName }` | Always `false` (stub). | Placeholder for keybind automation. |

## Server-initiated broadcasts on local data changes

The server pushes events without a `requestId` when local files change or during startup (see `src/service/lib/events.js`):

- `loadingProgress`: Fired repeatedly during initial load and once complete. Payload matches `getLoadingStatus` output.
- `newLogEntry`: Emitted for each new Elite Dangerous journal line while not in the loading phase. Payload is the raw log event object.
- `gameStateChange`: Sent when `Status.json` (or other watched JSON) changes. No payload; clients should refetch relevant state (e.g., `getCmdrStatus`, `getShipStatus`).
- `syncMessage`: Generic sync fan-out used by `setPreferences` and the `syncMessage` command. Payload is whatever the originator passed (e.g., `{ name: "preferences" }`).

Broadcasts share the same `{ name, message }` envelope as responses but omit `requestId`.
