#pragma once

namespace diamond::fx {

struct Context;

struct GlareSettings {
    double threshold, knee, radius, intensity, dispersion, rotation, red, green, blue;
    int blades, blend;
    bool glareOnly;
};

struct GlintSettings {
    double threshold, knee, length, intensity, angle, dispersion, red, green, blue;
    int rays, blend;
    bool glintOnly;
};

struct StreaksSettings {
    double threshold, knee, length, intensity, angle, spread, red, green, blue;
    int count, blend;
    bool streaksOnly;
};

struct FilmBurnSettings {
    double centerX, centerY, amount, size, softness, angle, speed, red, green, blue;
    int style, blend, seed;
    bool burnOnly;
};

void renderGlare(const Context&, const GlareSettings&);
void renderGlint(const Context&, const GlintSettings&);
void renderStreaks(const Context&, const StreaksSettings&);
void renderFilmBurn(const Context&, const FilmBurnSettings&);

} // namespace diamond::fx
