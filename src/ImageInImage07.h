#pragma once

#include "Effects.h"

#include <array>

namespace diamond::fx {

struct PicturePoint {
    double x, y;
};

using PictureQuad = std::array<PicturePoint, 4>;

inline constexpr PictureQuad pictureDefaultCorners{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
inline constexpr std::array<const char*, 4> pictureCornerNames{
    "topLeft", "topRight", "bottomRight", "bottomLeft"};

struct PictureSettings {
    PicturePoint center{0.5, 0.5};
    double scale = 0.5;
    double rotation = 0;
    double opacity = 1;
    double feather = 0;
    PictureQuad corners = pictureDefaultCorners;
    bool perspective = false;
    int quality = 1;
};

struct PictureRenderSpace {
    Rect sourceRegion;
    double sourcePixelAspect;
};

PictureQuad pictureQuad(const PictureSettings&, double canvasAspect, double sourceAspect);
PicturePoint pictureLocalPoint(const PictureSettings&, double canvasAspect, double sourceAspect,
                               PicturePoint canvasPoint);
bool validPictureQuad(const PictureQuad&);
void renderImageInImage(const Context&, const PictureSettings&);
void renderImageInImage(const Context&, const PictureSettings&, PictureRenderSpace);

} // namespace diamond::fx
