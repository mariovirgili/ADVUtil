#ifndef DISPLAY_BRIGHTNESS_H
#define DISPLAY_BRIGHTNESS_H

#include <Arduino.h>
#include <stdint.h>

constexpr uint8_t kDisplayBrightnessMinLevel = 0;
constexpr uint8_t kDisplayBrightnessMaxLevel = 10;
constexpr uint8_t kDisplayBrightnessDefaultLevel = 6;

uint8_t clampDisplayBrightnessLevel(int level);
void applyDisplayBrightnessLevel(uint8_t level);

#endif
