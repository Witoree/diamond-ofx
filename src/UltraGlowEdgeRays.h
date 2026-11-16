#pragma once

#include "Effects.h"

namespace diamond::fx {

struct UltraGlowSettings {
    double threshold, knee, radius, brightness, spread, stretch;
    double redWidth, greenWidth, blueWidth, afterglow, red, green, blue;
    int blend;
    bool glowOnly;
};

struct EdgeRaysSettings {
    double centerX, centerY, length, brightness, threshold, softness, edgeSize, decay;
    double red, green, blue;
    int samples, blend;
    bool raysOnly;
};

void renderUltraGlow(const Context&, const UltraGlowSettings&);
void renderEdgeRays(const Context&, const EdgeRaysSettings&);

} // namespace diamond::fx
