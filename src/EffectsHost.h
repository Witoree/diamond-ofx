#pragma once

#include "Effects.h"
#include "Transitions05.h"

#include <ofxParam.h>
#include <ofxProperty.h>

namespace diamond::fx {

enum class Kind {
    Shake, LensFlare, Glow, BlurMoCurves, DigitalDamage, Rays,
    UltraZap, PixelSort, RackDefocus, LightLeak, FilmEffect,
    VHSDamage, FilmBurn, DistortRGB, WarpFishEye, WarpWaves,
    Glare, Glint, Streaks, HalfTone, BleachBypass,
    Blur, BlurDirectional, Sharpen, Vignette, Grain, Halation,
    Dissolve, DissolveGlow, DissolveShake, SwishPan,
    GlowAura, GlowDarks, GlowDist, GlowEdges, GlowNoise, GlowOrthicon,
    GlowRainbow, GlowRings, UltraGlow, EdgeRays,
    GlintRainbow, BokehLights, DropShadow, Flashbulbs, Light3D, SpotLight, Hotspots,
    FilmDamage, GrainStatic, UltraGrain, ImageInImage
};

struct StatusError {
    OfxStatus status;
};

struct Parameters {
    const OfxPropertySuiteV1* properties;
    const OfxParameterSuiteV1* suite;
    OfxParamSetHandle set;
};

struct PixelFormat {
    Depth depth;
    int components;
    bool straightAlpha;
};

constexpr bool isTransition(Kind kind) {
    return kind >= Kind::Dissolve && kind <= Kind::SwishPan;
}

constexpr TransitionKind transitionKind(Kind kind) {
    return static_cast<TransitionKind>(static_cast<int>(kind) - static_cast<int>(Kind::Dissolve));
}

void describeControls(Kind kind, Parameters parameters);
bool controlsAreIdentity(Kind kind, Parameters parameters, OfxTime time);
TransitionSettings transitionSettingsAt(Kind kind, Parameters parameters, OfxTime time);
void renderEffect(Kind kind, Parameters parameters, ImageView source, PixelFormat inputFormat,
                  MutableImageView destination, PixelFormat outputFormat, Rect frame, Rect window,
                  double pixelAspect, double timeFrames, double fps, bool (*abort)(void*),
                  void* abortContext, const ShakeSettings* shake = nullptr, ImageView normals = {},
                  PixelFormat normalsFormat = {Depth::Float, 4, false});
void renderTransitionEffect(TransitionKind kind, const TransitionSettings& settings, double progress,
                            ImageView from, PixelFormat fromFormat, ImageView to, PixelFormat toFormat,
                            MutableImageView destination, PixelFormat outputFormat, Rect frame, Rect window,
                            double pixelAspect, double timeFrames, double fps, bool (*abort)(void*),
                            void* abortContext);

} // namespace diamond::fx
