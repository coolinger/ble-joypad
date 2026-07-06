# GPIO Pinout — Guition JC4827W543 (ESP32-S3-WROOM-1 N4R8)

Source: References/JC4827W543_4.3inch_ESP32S3_board/Docs/"4.3 inches IO pin distribution.pdf"

## Onboard

| GPIO | Function | Notes |
|------|----------------------|-------|
| 0    | BOOT button / LCD_TE | |
| 1    | LCD backlight (PWM)  | LEDC 5 kHz/8 bit — NOT an amp enable! |
| 2    | I2S LRCLK (NS4168)   | |
| 3    | Touch INT (GT911)    | |
| 4    | Touch SCL (GT911)    | Wire |
| 8    | Touch SDA (GT911)    | Wire |
| 38   | Touch RST (GT911)    | |
| 41   | I2S DIN  (NS4168)    | |
| 42   | I2S BCLK (NS4168)    | |
| 45   | LCD QSPI CS          | |
| 47   | LCD QSPI CLK         | |
| 21/48/40/39 | LCD QSPI D0/D1/D2/D3 | NV3041A |
| 10/11/12/13 | SD slot (CS/MISO/CLK/MOSI) | unused |
| 19/20 | USB D-/D+           | native CDC |
| 43/44 | UART0 TX/RX         | |
| 35–37 | not available       | octal PSRAM |

## External (JST / headers)

| GPIO | Function | Notes |
|------|----------|-------|
| 18   | Wire1 SDA | PCF8575 @ 0x20 |
| 17   | Wire1 SCL | PCF8575 @ 0x20 |

## PCF8575 (0x20) — TTP223 touch pads, right of the display, active-high

| PCF bit | Pad | Position | Function |
|---------|--------|----------|-----------------|
| P5 | touch3 | top      | previous page |
| P6 | touch2 | middle   | display off |
| P7 | touch1 | bottom   | next page |

Free for future use: GPIO 5, 6, 7, 9, 14, 15, 16, 46.
