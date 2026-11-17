#pragma once

namespace diamond::fx {

struct Context;

struct GlintRainbowSettings {
    double threshold = .65, knee = .12, length = 6, width = 2.5, rotation = 0;
    double dispersion = .6, hue = 0, intensity = 1, alphaWeight = 0;
    double red = 1, green = 1, blue = 1;
    int rays = 8, blend = 0;
    bool lightsOnly = false;
};

struct BokehLightsSettings {
    double size = 5, sizeVariation = .65, softness = .23, aspect = 1, rotation = 0;
    double intensity = .52, hueVariation = .35, drift = 10, speed = .20, flicker = .20;
    double red = 1, green = .78, blue = .58;
    int count = 28, blades = 0, seed = 19, blend = 1;
    bool lightsOnly = false;
};

struct FlashbulbsSettings {
    double rate = 8, duration = .105, size = 3, sizeVariation = .62;
    double intensity = 1.65, trail = .42, rotation = 0;
    double centerX = .5, centerY = .57, spreadX = .9, spreadY = .48;
    double red = .86, green = .93, blue = 1;
    int seed = 217, style = 0, blend = 0;
    bool lightsOnly = false;
};

struct SpotLightSettings {
    double lightX = .35, lightY = .80, aimX = .53, aimY = .44;
    double height = 75, angle = 42, softness = .32, falloff = .40;
    double ambient = .25, inside = 1.35, outside = 0;
    double red = 1, green = .92, blue = .79;
    bool lightOnly = false;
};

void renderGlintRainbow(const Context&, const GlintRainbowSettings&);
void renderBokehLights(const Context&, const BokehLightsSettings&);
void renderFlashbulbs(const Context&, const FlashbulbsSettings&);
void renderSpotLight(const Context&, const SpotLightSettings&);

} // namespace diamond::fx
