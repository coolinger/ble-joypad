# Elite Dangerous Controller

An ESP32-S3 touchscreen controller for Elite Dangerous: receives live telemetry from Icarus Terminal, shows ship/commander state on a 2.8" capacitive display, plays alerts over I2S audio, and exposes 12 physical buttons over BLE HID.

## Motivation
Build a desk-friendly, wireless controller that combines tactile buttons with an always-on glanceable display for Elite Dangerous, fed by the community Icarus Terminal data source.

## Features
- **Icarus Terminal feed**: Live Elite Dangerous telemetry over WebSocket from [Icarus Terminal](https://github.com/iaincollins/icarus) for ship/commander state.
- **Multi-screen LVGL UI**: Log viewer, fighter command pad, and system/settings pages on the 2.8" capacitive display.
- **Central status model**: Fuel, cargo, hull, shields, jumps, location, backpack, bioscans, credits, legal state all flow into one model that drives every screen.
- **Audio cues**: I2S speaker beeps/alerts for connection and events.
- **BLE HID buttons**: ESP32-BLE-Gamepad with 12 buttons via PCF8575 expander.
- **Button layout**:
  - Top Row (Silver): SILVER_LEFT (13), SILVER_MID (14), SILVER_RIGHT (15)
  - Second Row (Small Colored): BLACK (1), WHITE (0), RED (5)
  - Third Row (Small Colored): YELLOW (2), BLUE (3), GREEN (4)
  - Bottom Row (Large): LARGE_YELLOW (12), LARGE_BLUE (11), LARGE_GREEN (10)
- **Status indicators**: Wi‑Fi, WebSocket, BLE icons; cargo/fuel/hull bars; backpack/bioscan counts.
- **Customizable BLE Name**: Advertises as "CoolJoyBLE".

## Hardware
- Freenove ESP32-S3 Display (FNK0104B)
  - 2.8" 240x320 IPS, capacitive touch
  - ESP32-S3 with 16 MB PSRAM
  - I2S speaker amp for audio output
  - I2C touch controller wired via onboard flex
- PCF8575 I/O expander for 12 push buttons
- 12 push buttons

## Wiring
- I2C: External connector (SDA to GPIO 21, SCL to GPIO 22)
- Buttons: Connect to PCF8575 pins as per layout above
- RGB LED: Onboard, connected to GPIOs 4, 16, 17 (active-low)
- Display: 240x320 (future use)

## Software
- PlatformIO project (Arduino framework)
- Data source: [Icarus Terminal](https://github.com/iaincollins/icarus) WebSocket feed
- Libraries:
  - [ESP32-BLE-Gamepad](https://github.com/lemmingDev/ESP32-BLE-Gamepad)
  - [PCF8575](https://github.com/xreef/PCF8575)
  - [ArduinoJson](https://arduinojson.org/)
  - [ArduinoWebsockets](https://github.com/gilmaimon/ArduinoWebsockets)
  - [lvgl](https://github.com/lvgl/lvgl)

## Usage
1. Build and upload the firmware using PlatformIO.
2. Power the device and pair it with your computer via Bluetooth (look for "CoolJoyBLE").
3. Use the extra buttons in your favorite simulator!

## Future Plans
- ~Integrate the onboard 240x320 display to show in-game data.~
- ~Receive and display data over BLE serial console from the simulator/game.~
- Add battery support for 18650 Li‑ion (charging, fuel gauge, and on-screen status).
