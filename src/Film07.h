#pragma once

namespace diamond::fx {

struct Context;

struct FilmDamageSettings {
    double dust, hair, scratches, flicker, weave, size, speed, mix;
    int seed;
};

struct GrainStaticSettings {
    double amount, color, speed, mix;
    int seed;
};

struct UltraGrainSettings {
    double amount, photonNoise, readNoise, fixedPattern, color, size, speed, mix;
    int seed;
};

void renderFilmDamage(const Context&, const FilmDamageSettings&);
void renderGrainStatic(const Context&, const GrainStaticSettings&);
void renderUltraGrain(const Context&, const UltraGrainSettings&);

} // namespace diamond::fx
