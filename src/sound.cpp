#include "sound.h"

#include <cmath>
#include "driver/i2s.h"

// Tone feature toggles
#define BEEP_BOOT 0
#define BEEP_ERROR 1
#define BEEP_CONNECT 0
#define BEEP_DISCONNECT 0
#define BEEP_SHORT 1
#define BEEP_MOTHERLODE 1
#define BEEP_CLICK 1

int AUDIO_TONE_AMPL  = 6000; // reduce beep loudness

static bool soundReady = false;

void soundSetInitialized(bool ready) {
  soundReady = ready;
}

bool soundIsInitialized() {
  return soundReady;
}

// Generate a simple sine wave tone (stereo)
void playTone(uint16_t frequency, uint16_t duration_ms) {
  if (!soundReady) {
    Serial.println("I2S not initialized, cannot play tone");
    return;
  }
  size_t bytes_written = 0;

  const int sampleRate = AUDIO_SAMPLE_RATE;
  const int samples = (sampleRate * duration_ms) / 1000;
  int16_t* buffer = (int16_t*)malloc(samples * 2 * sizeof(int16_t)); // stereo buffer
  if (!buffer) {
    Serial.println("[I2S] tone buffer alloc failed");
    return;
  }

  for (int i = 0; i < samples; i++) {
    float t = (float)i / sampleRate;
    int16_t s = (int16_t)(sin(2.0f * PI * frequency * t) * AUDIO_TONE_AMPL);
    buffer[i * 2]     = s;
    buffer[i * 2 + 1] = s;
  }

  esp_err_t err = i2s_write(I2S_NUM_0, buffer, samples * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
  Serial.printf("[I2S] write err=%d bytes=%u\n", (int)err, (unsigned)bytes_written);

  free(buffer);
}

void beepShort() {
#if (BEEP_SHORT)
  playTone(1000, 50);
#endif
}

void beepClick() {
#if (BEEP_CLICK)
  playTone(800, 20);
#endif
}

void beepConnect() {
#if (BEEP_CONNECT)
  playTone(1200, 100);
#endif
}

void beepDisconnect() {
#if (BEEP_DISCONNECT)
  playTone(600, 100);
#endif
}

void beepMotherlode() {
#if (BEEP_MOTHERLODE)
  playTone(1500, 200);
#endif
}

void beepBootup() {
#if (BEEP_BOOT)
  playTone(1000, 150);
#endif
}
