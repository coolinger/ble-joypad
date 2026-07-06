#pragma once

#include <Arduino.h>
#include <vector>

// Game data structures used across the app. These used to be in main.cpp.
struct CargoEntry {
  String name;  // cargo symbol
  int count = 0;
};

struct CargoInfo {
  int totalCapacity = 256;
  int usedSpace = 0;
  int dronesCount = 0;
  int cargoCount = 0;  // non-drones cargo
  std::vector<CargoEntry> inventory;  // full inventory (symbol + count)
};

struct FuelInfo {
  float fuelMain = 0.0f;
  float fuelCapacity = 32.0f;
};

struct HullInfo {
  float hullHealth = 1.0f;
  String hullType;
};

struct NavRouteInfo {
  int jumpsRemaining = 0;
};

struct BackpackInfo {
  int healthpack = 0;
  int energycell = 0;
};

struct BioscanInfo {
  int totalScans = 0;
};

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

struct EventLogEntry {
  char text[55];
  uint32_t timestamp;
  int count;
};

struct CommunityGoal {
  String title;
  int percentile = -1;
  int tier = -1;
  int contributors = -1;
};

struct StatusModel {
  CargoInfo cargo;
  FuelInfo fuel;
  HullInfo hull;
  NavRouteInfo nav;
  BackpackInfo backpack;
  BioscanInfo bioscan;
  ExplorationInfo exploration;
  float shieldsPercent = 0.0f;

  String currentSystem;
  String currentStation;
  String destinationName;
  String legalState;
  bool onFoot = false;
  bool inShip = true;
  bool docked = false;
  bool inSpace = false;
  bool landed = false;
  bool inSrv = false;
  bool inTaxi = false;
  bool inMulticrew = false;
  long credits = 0;
  std::vector<CommunityGoal> communityGoals;
};

// Global status instance
extern StatusModel status;

// Event log
extern EventLogEntry eventLog[9];
extern int eventLogIndex;
extern int eventLogCount;

// Other global status flags
extern char motherlodeMaterial[32];
extern bool blinkScreen;
extern int blinkCount;
extern uint32_t lastBlinkTime;

// Bodies with FSS/DSS-detected signals, pinned at the top of the log view
// until the ship leaves the system (jump/shutdown). bioDone counts organisms
// fully analysed (3 samples -> ScanOrganic "Analyse") against the biological
// signal count. Per-genus scan progress is tracked for the detail line.
#define MAX_PINNED_BODIES 4
#define MAX_GENUSES 8

enum BioScanState : uint8_t {
  BIO_TODO = 0,      // known to exist, not scanned yet
  BIO_SCANNING = 1,  // 1st/2nd sample in the genetic sampler
  BIO_DONE = 2       // analysed (all 3 samples taken)
};

struct BioGenus {
  char name[14];  // localised genus name, e.g. "Bacterium"
  uint8_t state = BIO_TODO;
};

struct PinnedBody {
  int bodyId = -1;   // journal BodyID (matches ScanOrganic "Body")
  char label[20];    // body identifier within the system, e.g. "A 1"
  int bio = 0;
  int geo = 0;
  int other = 0;
  int bioDone = 0;   // number of genuses in BIO_DONE
  BioGenus genuses[MAX_GENUSES];
  int genusCount = 0;
};
extern PinnedBody pinnedBodies[MAX_PINNED_BODIES];
extern int pinnedBodyCount;

void pinBodySignals(int bodyId, const char* label, int bio, int geo, int other);
void addPinnedGenus(int bodyId, const char* name);  // from SAASignalsFound Genuses
// ScanOrganic progress: scanType is "Log" (1st sample), "Sample" (2nd/3rd)
// or "Analyse" (complete). Updates genus states and bioDone.
void organicScanProgress(int bodyId, const char* genusName, const char* scanType);
void clearPinnedBodies();

// Exploration progress API (implementation in gamedata.cpp; dedupe backing
// bitmaps are file-static there).
void explorationReset();
void explorationHonk(int bodyCount);
void explorationAllFound();
void explorationScan(int bodyId, bool wasDiscovered, bool wasMapped);
void explorationMapped(int bodyId);
bool explorationStation(const char* signalName, const char* signalType);

// Small API helpers
void updateJumpsRemaining(int newValue);
void addLogEntry(const char* text);

// Legacy compatibility aliases to ease migration while consolidating usage
#define cargoInfo (status.cargo)
#define fuelInfo (status.fuel)
#define hullInfo (status.hull)
#define navRouteInfo (status.nav)
#define backpackInfo (status.backpack)
#define bioscanInfo (status.bioscan)
