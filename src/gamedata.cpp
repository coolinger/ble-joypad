#include "gamedata.h"

// Define instances
CargoInfo cargoInfo;
FuelInfo fuelInfo;
HullInfo hullInfo;
NavRouteInfo navRouteInfo;
BackpackInfo backpackInfo;
BioscanInfo bioscanInfo;

EventLogEntry eventLog[9] = {};
int eventLogIndex = 0;
int eventLogCount = 0;

char motherlodeMaterial[32] = "";
bool blinkScreen = false;
int blinkCount = 0;
uint32_t lastBlinkTime = 0;

// Minimal implementations forwarded from main.cpp - the main implementation
// remains in main.cpp for now to avoid merge churn. These are placeholders
// so other compilation units can link if they call these functions.
void updateJumpsRemaining(int newValue) {
  if (newValue < 0) newValue = 0;
  bool changed = navRouteInfo.jumpsRemaining != newValue;
  navRouteInfo.jumpsRemaining = newValue;
  (void)changed;
}

void addLogEntry(const char* text) {
  // lightweight fallback - real implementation lives in main.cpp
  if (!text) return;
  int idx = eventLogIndex % 9;
  strncpy(eventLog[idx].text, text, sizeof(eventLog[idx].text)-1);
  eventLog[idx].text[sizeof(eventLog[idx].text)-1] = '\0';
  eventLog[idx].timestamp = millis();
  eventLog[idx].count = 1;
  eventLogIndex = (eventLogIndex + 1) % 9;
  if (eventLogCount < 9) eventLogCount++;
}
