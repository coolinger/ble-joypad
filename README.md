# BLE Joypad

A compact ESP32-based Bluetooth gamepad with 12 physical buttons, designed for use as an auxiliary input device for simulators like Elite Dangerous.

## Motivation
I wanted a small box with extra buttons to enhance my simulator experience, especially for games like Elite Dangerous. This project provides a simple, wireless solution for additional controls.

## Features
- **Bluetooth Gamepad**: Uses ESP32-BLE-Gamepad library for seamless BLE HID connectivity.
- **12 Buttons**: All buttons are read via a PCF8575 I/O expander over I2C (SDA: 21, SCL: 22).
- **Button Layout**:
  - Top Row (Silver): SILVER_LEFT (13), SILVER_MID (14), SILVER_RIGHT (15)
  - Second Row (Small Colored): BLACK (1), WHITE (0), RED (5)
  - Third Row (Small Colored): YELLOW (2), BLUE (3), GREEN (4)
  - Bottom Row (Large): LARGE_YELLOW (12), LARGE_BLUE (11), LARGE_GREEN (10)
- **RGB LED Status**:
  - Red: No BLE connection
  - Blue: BLE connected
  - Green: Keypress detected
- **Serial Output**: Keypresses and BLE connection events are logged to serial for debugging.
- **Customizable BLE Name**: Device advertises as "CoolJoyBLE".

## Hardware
- CYD clone JC2432W328 (ESP32-based)
  - Onboard RGB LED (Red: GPIO 4, Green: GPIO 16, Blue: GPIO 17)
  - External connector for I2C pins (SDA: 21, SCL: 22)
  - 240x320 display (planned for future use)
- PCF8575 I/O expander
- 12 push buttons

## Wiring
- I2C: External connector (SDA to GPIO 21, SCL to GPIO 22)
- Buttons: Connect to PCF8575 pins as per layout above
- RGB LED: Onboard, connected to GPIOs 4, 16, 17 (active-low)
- Display: 240x320 (future use)

## Software
- PlatformIO project (Arduino framework)
- Libraries:
  - [ESP32-BLE-Gamepad](https://github.com/lemmingDev/ESP32-BLE-Gamepad)
  - [PCF8575](https://github.com/xreef/PCF8575)

## Usage
1. Build and upload the firmware using PlatformIO.
2. Power the device and pair it with your computer via Bluetooth (look for "CoolJoyBLE").
3. Use the extra buttons in your favorite simulator!

## Future Plans
- Integrate the onboard 240x320 display to show in-game data.
- Receive and display data over BLE serial console from the simulator/game.
