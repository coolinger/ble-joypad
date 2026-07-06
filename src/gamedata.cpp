#include "gamedata.h"
#include <string.h>

// Unified status instance
StatusModel status;

// Event log buffer
EventLogEntry eventLog[9] = {};
int eventLogIndex = 0;
int eventLogCount = 0;

char motherlodeMaterial[32] = "";
bool blinkScreen = false;
int blinkCount = 0;
uint32_t lastBlinkTime = 0;

// Pinned body signals
PinnedBody pinnedBodies[MAX_PINNED_BODIES];
int pinnedBodyCount = 0;

static int pinTotal(const PinnedBody &pb) {
  return pb.bio + pb.geo + pb.other;
}

// Keep the list sorted descending by total signal count (stable insertion
// sort; the list is tiny). Invariant: the last entry is always the weakest.
static void sortPinnedBodies() {
  for (int i = 1; i < pinnedBodyCount; i++) {
    PinnedBody key = pinnedBodies[i];
    int j = i - 1;
    while (j >= 0 && pinTotal(pinnedBodies[j]) < pinTotal(key)) {
      pinnedBodies[j + 1] = pinnedBodies[j];
      j--;
    }
    pinnedBodies[j + 1] = key;
  }
}

void pinBodySignals(int bodyId, const char* label, int bio, int geo, int other) {
  // Update an existing pin for this body (e.g. FSS first, DSS mapping later),
  // keeping the organic-scan progress.
  for (int i = 0; i < pinnedBodyCount; i++) {
    if (pinnedBodies[i].bodyId == bodyId) {
      pinnedBodies[i].bio = bio;
      pinnedBodies[i].geo = geo;
      pinnedBodies[i].other = other;
      snprintf(pinnedBodies[i].label, sizeof(pinnedBodies[i].label), "%s", label);
      sortPinnedBodies();
      return;
    }
  }
  if (pinnedBodyCount >= MAX_PINNED_BODIES) {
    // Full: the new body must beat the weakest pin (fewest total signals),
    // otherwise it is not pinned at all.
    if (bio + geo + other <= pinTotal(pinnedBodies[pinnedBodyCount - 1])) return;
    pinnedBodyCount--;  // evict the weakest (list is sorted, last = weakest)
  }
  PinnedBody &pb = pinnedBodies[pinnedBodyCount++];
  pb.bodyId = bodyId;
  snprintf(pb.label, sizeof(pb.label), "%s", label);
  pb.bio = bio;
  pb.geo = geo;
  pb.other = other;
  pb.bioDone = 0;
  pb.genusCount = 0;  // slot may be recycled after unpin/clear: drop stale genuses
  sortPinnedBodies();
}

static PinnedBody* findPinnedBody(int bodyId) {
  for (int i = 0; i < pinnedBodyCount; i++) {
    if (pinnedBodies[i].bodyId == bodyId) return &pinnedBodies[i];
  }
  return nullptr;
}

// Find a genus on the pin by (localised) name; add it as BIO_TODO if unknown.
static BioGenus* findOrAddGenus(PinnedBody* pb, const char* name) {
  if (!name || !name[0]) return nullptr;
  for (int i = 0; i < pb->genusCount; i++) {
    if (strncasecmp(pb->genuses[i].name, name, sizeof(pb->genuses[i].name)) == 0)
      return &pb->genuses[i];
  }
  if (pb->genusCount >= MAX_GENUSES) return nullptr;
  BioGenus &g = pb->genuses[pb->genusCount++];
  snprintf(g.name, sizeof(g.name), "%s", name);
  g.state = BIO_TODO;
  return &g;
}

void addPinnedGenus(int bodyId, const char* name) {
  PinnedBody* pb = findPinnedBody(bodyId);
  if (pb) findOrAddGenus(pb, name);  // keeps the state if already known
}

void organicScanProgress(int bodyId, const char* genusName, const char* scanType) {
  PinnedBody* pb = findPinnedBody(bodyId);
  if (!pb) return;  // body without known signals: nothing to track
  BioGenus* g = findOrAddGenus(pb, genusName);
  if (!g) return;

  if (strcmp(scanType, "Analyse") == 0) {
    g->state = BIO_DONE;  // 3rd sample analysed -> checked off
  } else if (g->state != BIO_DONE) {
    // "Log" / "Sample": this organism is now loaded in the genetic sampler.
    // Only one organism fits the sampler, so any other in-progress genus
    // (on any pinned body) falls back to "still to scan".
    for (int i = 0; i < pinnedBodyCount; i++) {
      for (int j = 0; j < pinnedBodies[i].genusCount; j++) {
        if (pinnedBodies[i].genuses[j].state == BIO_SCANNING)
          pinnedBodies[i].genuses[j].state = BIO_TODO;
      }
    }
    g->state = BIO_SCANNING;
  }

  // bioDone = analysed genuses on this body
  int done = 0;
  for (int i = 0; i < pb->genusCount; i++) {
    if (pb->genuses[i].state == BIO_DONE) done++;
  }
  pb->bioDone = done;

  // A body whose only signals were biological and are all analysed is done —
  // treat it as signal-less and unpin it to free the slot.
  if (pb->bio > 0 && pb->bioDone >= pb->bio && pb->geo == 0 && pb->other == 0) {
    int idx = (int)(pb - pinnedBodies);
    for (int i = idx + 1; i < pinnedBodyCount; i++) pinnedBodies[i - 1] = pinnedBodies[i];
    pinnedBodyCount--;
  }
}

void clearPinnedBodies() {
  pinnedBodyCount = 0;
}

// ---------------- Exploration progress ----------------
// BodyID dedupe bitmaps (BodyIDs are small ints; clamp to 0..255).
static uint32_t scannedBits[8];
static uint32_t mappedBits[8];
static uint32_t unmappedBits[8];   // bodies whose Scan said WasMapped == false
#define EXPL_MAX_STATIONS 40
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
  if (stationHashCount >= EXPL_MAX_STATIONS) return false;  // full: undercount, never inflate
  stationHashes[stationHashCount++] = h;
  if (*bucket < 255) (*bucket)++;
  return true;
}

// Implementations for updateJumpsRemaining/addLogEntry live in main.cpp.
