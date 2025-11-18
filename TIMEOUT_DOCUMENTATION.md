# Timeout Documentation for Screen Blanking

## Overview
This document describes the timeout mechanisms used to control the display and LED behavior based on user activity and BLE connection status.

## Timeout Constants

### Line 169-170: Timeout Definitions
```cpp
const uint32_t DISPLAY_TIMEOUT = 5 * 60 * 1000; // 5 minutes in milliseconds
const uint32_t LED_TIMEOUT = 5 * 60 * 1000; // 5 minutes in milliseconds
```
- **DISPLAY_TIMEOUT**: 5 minutes (300,000 ms) - Time after which the display turns off due to inactivity
- **LED_TIMEOUT**: 5 minutes (300,000 ms) - Time after which the LED turns off when BLE is disconnected

## State Variables

### Line 165-168: Timeout Tracking Variables
```cpp
bool displayOn = true;
uint32_t lastTouchTime = 0;
uint32_t lastBleActiveTime = 0;
uint32_t bleDisconnectedTime = 0;
```
- **displayOn**: Tracks whether the display is currently powered on
- **lastTouchTime**: Timestamp of the last touch event or activity
- **lastBleActiveTime**: Timestamp of the last BLE activity
- **bleDisconnectedTime**: Timestamp when BLE was disconnected

## Timeout Reset Points

### Line 475-479: Page Switch Timeout Reset
```cpp
// Reset timeout and turn on display on page switch
lastTouchTime = millis();
if (!displayOn) {
  setDisplayPower(true);
}
```
When the user switches pages, the timeout is reset and the display is turned on if it was off.

### Line 783-787: UDP Message Timeout Reset
```cpp
// Reset timeout and turn on display on message received
lastTouchTime = millis();
if (!displayOn) {
  setDisplayPower(true);
}
```
When a UDP message is received (e.g., Elite Dangerous game events), the timeout is reset and the display is turned on if it was off.

## Timeout Checking Functions

### Line 624-638: checkDisplayTimeout()
```cpp
void checkDisplayTimeout()
{
  uint32_t currentTime = millis();
  
  // Turn off display if no BLE connection
  if (!bleConnected && displayOn) {
    setDisplayPower(false);
    return;
  }
  
  // Turn off display after 30 minutes of no touch (only if BLE is connected)
  if (bleConnected && displayOn && (currentTime - lastTouchTime) > DISPLAY_TIMEOUT) {
    setDisplayPower(false);
  }
}
```
**Note**: The comment mentions "30 minutes" but the actual DISPLAY_TIMEOUT constant is set to 5 minutes. This is a documentation inconsistency in the code.

This function:
1. Immediately turns off the display if BLE is disconnected
2. Turns off the display after DISPLAY_TIMEOUT (5 minutes) of inactivity when BLE is connected

### Line 616-620: LED Timeout in checkBleConnection()
```cpp
// Turn off red LED after 5 minutes of no connection
if (ledOn && (millis() - bleDisconnectedTime) > LED_TIMEOUT) {
  setLedColor(0, 0, 0); // Turn off LED
  ledOn = false;
  Serial.println("[LED] Timeout - turning off");
}
```
This code turns off the red LED after LED_TIMEOUT (5 minutes) when BLE is disconnected.

## Main Loop Integration

### Line 914: Timeout Check in loop()
```cpp
checkDisplayTimeout();
```
The `checkDisplayTimeout()` function is called in every iteration of the main loop to continuously monitor and enforce timeout behavior.

## Timeout Behavior Summary

1. **Display Timeout (5 minutes)**:
   - Turns off the display after 5 minutes of no touch activity (when BLE is connected)
   - Immediately turns off the display when BLE disconnects
   - Resets on: page switches, UDP messages, touch events

2. **LED Timeout (5 minutes)**:
   - Turns off the red LED after 5 minutes of BLE disconnection
   - The LED is typically red when BLE is disconnected (set in checkBleConnection)

3. **Display Reactivation**:
   - Display can be turned back on by:
     - Touching the screen (page switch)
     - Receiving UDP messages from Elite Dangerous
     - BLE reconnection (handled in checkBleConnection)

## Code Inconsistency Note

There is a comment inconsistency at line 634 where the comment states "Turn off display after 30 minutes" but the actual timeout is 5 minutes (DISPLAY_TIMEOUT = 5 * 60 * 1000).
