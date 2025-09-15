#include <Arduino.h>

#include <PCF8575.h>
#include <BleGamepad.h>

// PCF8575 I2C address (A0-A2 = GND)
#define PCF8575_ADDR 0x20
#define I2C_SDA 21
#define I2C_SCL 22

// RGB LED pins
#define LED_R 4
#define LED_G 16
#define LED_B 17

// Button mapping (PCF8575 pins)
enum ButtonIndex {
  SILVER_LEFT = 13,
  SILVER_MID = 14,
  SILVER_RIGHT = 15,
  BLACK = 1,
  WHITE = 0,
  RED = 5,
  YELLOW = 2,
  BLUE = 3,
  GREEN = 4,
  LARGE_YELLOW = 12,
  LARGE_BLUE = 11,
  LARGE_GREEN = 10
};

const uint8_t buttonPins[12] = {
  SILVER_LEFT, SILVER_MID, SILVER_RIGHT,
  BLACK, WHITE, RED,
  YELLOW, BLUE, GREEN,
  LARGE_YELLOW, LARGE_BLUE, LARGE_GREEN
};


PCF8575 pcf(PCF8575_ADDR);
BleGamepad bleGamepad("CoolJoyBLE", "leDev", 100);
bool lastButtonState[12] = {0};
bool bleConnected = false;


void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
  // Invert logic: 0 = on, 255 = off
  analogWrite(LED_R, 255 - r);
  analogWrite(LED_G, 255 - g);
  analogWrite(LED_B, 255 - b);
}


void checkBleConnection() {
  if (bleGamepad.isConnected()) {
    if (!bleConnected) {
      Serial.println("[BLE] Connected");
      setLedColor(0, 0, 255); // Light blue
      bleConnected = true;
    }
  } else {
    if (bleConnected) {
      Serial.println("[BLE] Disconnected");
      setLedColor(255, 0, 0); // Light red
      bleConnected = false;
    }
  }
}


void setup() {
  Serial.begin(115200);
  delay(500);
  // RGB LED setup
  pinMode(LED_R, OUTPUT);
  pinMode(LED_G, OUTPUT);
  pinMode(LED_B, OUTPUT);
  setLedColor(255, 0, 0); // Start: red (no BLE)


  // I2C and PCF8575 setup
  Wire.begin(I2C_SDA, I2C_SCL);
  pcf.begin();
  // All pins are inputs by default for PCF8575

  // BLE Gamepad setup
  bleGamepad.begin();
}


void loop() {
  checkBleConnection();
  if (!bleConnected) {
    delay(100);
    return;
  }

  uint16_t pcfState = pcf.read16();
  bool anyPressed = false;
  for (uint8_t i = 0; i < 12; i++) {
    bool pressed = !(pcfState & (1 << buttonPins[i])); // Active low
    if (pressed != lastButtonState[i]) {
      if (pressed) {
        bleGamepad.press(i+1); // Gamepad buttons start at 1
        Serial.printf("Button %d pressed\n", i+1);
        anyPressed = true;
      } else {
        bleGamepad.release(i+1);
        Serial.printf("Button %d released\n", i+1);
      }
      lastButtonState[i] = pressed;
    }
  }
  bleGamepad.sendReport();
  if (anyPressed) setLedColor(0, 255, 0); // Green on keypress
  else setLedColor(0, 0, 255); // Blue when connected, no keypress
  delay(10);
}