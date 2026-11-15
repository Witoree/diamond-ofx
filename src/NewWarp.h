#pragma once

#include "Effects.h"

namespace diamond::fx {

struct DistortRGBSettings {
    double amount, detail, direction, red, green, blue, mix;
    Edge edge;
};

struct WarpFishEyeSettings {
    double amount, centerX, centerY, zoom, mix;
    Edge edge;
};

struct WarpWavesSettings {
    double amplitude, frequency, direction, phase, speed, crossWave, mix;
    Edge edge;
};

void renderDistortRGB(const Context&, const DistortRGBSettings&);
void renderWarpFishEye(const Context&, const WarpFishEyeSettings&);
void renderWarpWaves(const Context&, const WarpWavesSettings&);

} // namespace diamond::fx
