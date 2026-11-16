#pragma once

#include "Shake.h"

#include <vector>

namespace diamond::fx {

struct Context;

struct GlowDistanceSample {
    float distance;
    float strength;
};

class GlowDistanceField {
public:
    // Distances are measured in render pixels corrected for pixel aspect.
    // A positive limit crops the calculation to the output window plus padding.
    bool build(const Context&, double threshold, double knee, double alphaWeight = 0.0,
               double maximumDistance = 0.0);
    GlowDistanceSample sample(int x, int y) const;

private:
    Rect frame_{};
    Rect grid_{};
    int step_ = 1;
    std::vector<GlowDistanceSample> cells_;
};

struct GlowNoiseSettings {
    double threshold, knee, radius, brightness, noiseAmount, noiseScale, speed, red, green, blue;
    int seed, blend;
    bool glowOnly;
};

struct GlowOrthiconSettings {
    double threshold, knee, radius, darkness;
};

struct GlowRainbowSettings {
    double threshold, knee, radius, brightness, cycles, phase, speed, saturation, red, green, blue;
    int blend;
    bool glowOnly;
};

struct GlowRingsSettings {
    double threshold, knee, radius, brightness, thickness, dispersion, red, green, blue;
    int rings, blend;
    bool glowOnly;
};

void renderGlowNoise(const Context&, const GlowNoiseSettings&);
void renderGlowOrthicon(const Context&, const GlowOrthiconSettings&);
void renderGlowRainbow(const Context&, const GlowRainbowSettings&);
void renderGlowRings(const Context&, const GlowRingsSettings&);

} // namespace diamond::fx
