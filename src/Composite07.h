#pragma once

#include "Effects.h"

namespace diamond::fx {

struct DropShadowSettings {
    double offsetX, offsetY, radius, spread, opacity, red, green, blue;
    bool shadowOnly;
};

struct Light3DSettings {
    double centerX, centerY, elevation, ambient, diffuse, specular, roughness, falloff;
    double normalStrength, red, green, blue, mix;
    bool flipGreen, lightingOnly, signedNormals;
};

struct HotspotsSettings {
    double threshold, knee, brightness, saturation, smoothing, mix;
    int outputMode;
};

void renderDropShadow(const Context&, const DropShadowSettings&);
void renderLight3D(const Context&, const Light3DSettings&, const Image* normals);
void renderHotspots(const Context&, const HotspotsSettings&);

} // namespace diamond::fx
