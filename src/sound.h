#pragma once

#include <Arduino.h>

// Audio constants used by both sound and main
static const int AUDIO_SAMPLE_RATE = 44100;
static const int AUDIO_MCLK_HZ    = 11289600;

extern int AUDIO_TONE_AMPL;

void soundSetInitialized(bool ready);
bool soundIsInitialized();

void playTone(uint16_t frequency, uint16_t duration_ms);
void beepShort();
void beepClick();
void beepConnect();
void beepDisconnect();
void beepMotherlode();
void beepBootup();
