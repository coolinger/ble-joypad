#pragma once

#include <BleGamepad.h>

extern BleGamepad* bleGamepad;

// Initialize BLE gamepad - thin wrapper (implementation in main.cpp currently)
void initBleGamepad();
