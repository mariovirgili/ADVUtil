#include "DisplayBrightness.h"

#include <M5Cardputer.h>

namespace {

uint8_t toPanelBrightness(uint8_t level) {
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(level) * 255U + (kDisplayBrightnessMaxLevel / 2U)) /
        kDisplayBrightnessMaxLevel);
}

}  // namespace

uint8_t clampDisplayBrightnessLevel(int level) {
    return static_cast<uint8_t>(constrain(
        level,
        static_cast<int>(kDisplayBrightnessMinLevel),
        static_cast<int>(kDisplayBrightnessMaxLevel)));
}

void applyDisplayBrightnessLevel(uint8_t level) {
    M5.Display.setBrightness(toPanelBrightness(clampDisplayBrightnessLevel(level)));
}
