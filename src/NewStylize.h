#pragma once

#include "Effects.h"

namespace diamond::fx {

struct VHSSettings {
    double amount, frequency, tracking, jitter, chromaBleed, scanlines, noise, dropout;
    int seed;
};

struct HalfToneSettings {
    double cellSize, angle, softness, threshold, contrast, mix;
    double inkRed, inkGreen, inkBlue, paperRed, paperGreen, paperBlue;
    int mode;
    bool invert;
};

struct BleachBypassSettings {
    double amount, desaturation, contrast, exposure, shadowLift, highlightRolloff, temperature, mix;
};

void renderVHSDamage(const Context&, const VHSSettings&);
void renderHalfTone(const Context&, const HalfToneSettings&);
void renderBleachBypass(const Context&, const BleachBypassSettings&);

} // namespace diamond::fx
