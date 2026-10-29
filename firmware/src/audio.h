#pragma once

#include <Arduino.h>

// Speaker output through the board's I2S amplifier.
void audio_begin();

// Plays the two-note meeting chime in the background. volume: 0-100.
void audio_chime(uint8_t volume);

// Short click used as touch feedback and for the volume preview.
void audio_tick(uint8_t volume);
