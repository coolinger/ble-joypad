#pragma once

#include <Arduino.h>
#include <vector>

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
