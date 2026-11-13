#pragma once

#include <cstddef>

namespace diamond {

struct Rect {
    int x1, y1, x2, y2;
};

enum class Depth { Byte, Short, Float };
enum class Edge { Reflect, Clamp, Transparent };

// data points to the first component at (bounds.x1, bounds.y1).
// rowBytes may be negative. Bounds and windows are half-open rectangles.
struct ImageView {
    const void* data;
    std::ptrdiff_t rowBytes;
    Rect bounds;
};

struct MutableImageView {
    void* data;
    std::ptrdiff_t rowBytes;
    Rect bounds;
};

struct ShakeSettings {
    double amplitude;
    double frequency;
    double xPercent;
    double yPercent;
    double tiltDegrees;
    double zoomPercent;
    double overscanPercent;
    double shutterDegrees;
    int samples;
    int seed;
    Edge edge;
};

// frame is the full logical image in render-scale pixels, regardless of
// which part the host has asked us to render or supplied as source memory.
void renderShake(
    ImageView source, MutableImageView destination, Rect frame, Rect window,
    Depth depth, int components, double timeSeconds, double frameDurationSeconds,
    double pixelAspect, bool straightAlpha, const ShakeSettings& settings, bool (*abort)(void*),
    void* abortContext);

}  // namespace diamond
