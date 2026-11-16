#pragma once

namespace diamond::fx {

struct Context;

struct GlowAuraSettings {
    double threshold = .38, knee = .15, radius = 3.5, intensity = .95;
    double frequency = 4, phase = 0, phaseSpeed = .22, twist = .6;
    double red = 1, green = 1, blue = 1;
    int blend = 0;
    bool glowOnly = false;
};

struct GlowDarksSettings {
    double threshold = .42, knee = .14, radius = 3.5, intensity = .85, spread = .4;
    double red = .12, green = .16, blue = .22;
    bool glowOnly = false;
};

struct GlowDistSettings {
    double threshold = .45, radius = 3.5, intensity = 1.1, falloff = 1.65;
    double redWidth = .75, greenWidth = 1, blueWidth = 1.25, alphaWeight = 0;
    double red = .60, green = .83, blue = 1;
    int blend = 0;
    bool glowOnly = false;
};

struct GlowEdgesSettings {
    double threshold = .075, softness = .12, edgeSize = 2, radius = 2, intensity = 1.2;
    double spread = .35, alphaWeight = .35, red = .78, green = .88, blue = 1;
    int blend = 0;
    bool glowOnly = false;
};

void renderGlowAura(const Context&, const GlowAuraSettings&);
void renderGlowDarks(const Context&, const GlowDarksSettings&);
void renderGlowDist(const Context&, const GlowDistSettings&);
void renderGlowEdges(const Context&, const GlowEdgesSettings&);

} // namespace diamond::fx
