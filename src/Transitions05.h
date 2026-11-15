#pragma once

#include "Effects.h"

namespace diamond::fx {

enum class TransitionKind { Dissolve, DissolveGlow, DissolveShake, SwishPan };

struct TransitionSettings {
    double ease = 0.5;
    double intensity = 1.0;
    double threshold = 0.55;
    double radius = 3.5;
    double red = 1.0;
    double green = 0.85;
    double blue = 0.65;
    double shakeAmount = 1.75;
    double tilt = 2.0;
    double frequency = 5.0;
    double angle = 0.0;
    double shutter = 0.45;
    int samples = 9;
    int seed = 0;
};

// The context's source is the outgoing clip. The host provides the incoming clip and normalized progress.
void renderTransition(TransitionKind, const Context& from, const Image& to, double progress,
                      const TransitionSettings&);

} // namespace diamond::fx
