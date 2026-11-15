#pragma once

#include "Effects.h"

namespace diamond::fx {

struct BlurSettings05 {
    double radius, mix;
    Edge edge;
};

struct DirectionalBlurSettings {
    double length, angle, bias, mix;
    int samples;
    Edge edge;
};

struct SharpenSettings {
    double amount, radius, threshold, mix;
};

struct VignetteSettings {
    double centerX, centerY, radius, softness, roundness, angle, amount, red, green, blue;
};

struct GrainSettings {
    double amount, size, roughness, color, response, speed;
    int seed;
};

struct HalationSettings {
    double threshold, knee, radius, amount, red, green, blue;
    bool halationOnly;
};

void renderBlur(const Context&, const BlurSettings05&);
void renderBlurDirectional(const Context&, const DirectionalBlurSettings&);
void renderSharpen(const Context&, const SharpenSettings&);
void renderVignette(const Context&, const VignetteSettings&);
void renderGrain(const Context&, const GrainSettings&);
void renderHalation(const Context&, const HalationSettings&);

} // namespace diamond::fx
