#pragma once

#include <Arduino.h>

// Game data structures used across the app. These used to be in main.cpp.
struct CargoInfo {
  int totalCapacity = 256;
  int usedSpace = 0;
  int dronesCount = 0;
  int cargoCount = 0;  // non-drones cargo
};

struct FuelInfo {
  float fuelMain = 0.0f;
  float fuelCapacity = 32.0f;
};

struct HullInfo {
  float hullHealth = 1.0f;
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

struct EventLogEntry {
  char text[55];
  uint32_t timestamp;
  int count;
};

// Extern instances accessible from other translation units
extern CargoInfo cargoInfo;
extern FuelInfo fuelInfo;
extern HullInfo hullInfo;
extern NavRouteInfo navRouteInfo;
extern BackpackInfo backpackInfo;
extern BioscanInfo bioscanInfo;

// Event log
extern EventLogEntry eventLog[9];
extern int eventLogIndex;
extern int eventLogCount;

// Other global status
extern char motherlodeMaterial[32];
extern bool blinkScreen;
extern int blinkCount;
extern uint32_t lastBlinkTime;

// Small API helpers
void updateJumpsRemaining(int newValue);
void addLogEntry(const char* text);
