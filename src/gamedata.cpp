#include "gamedata.h"

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

// Implementations for updateJumpsRemaining/addLogEntry live in main.cpp.
