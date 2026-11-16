#include "EffectsHost.h"
#include "Glow06.h"
#include "GlowSpectral06.h"
#include "NewOptics.h"
#include "NewStylize.h"
#include "NewWarp.h"
#include "UltraGlowEdgeRays.h"
#include "Video05.h"

#include <initializer_list>
#include <limits>

namespace diamond::fx {
namespace {

void require(OfxStatus status)
{
    if (status != kOfxStatOK) {
        throw StatusError{status};
    }
}

template<typename Settings>
struct Number {
    const char* name;
    const char* label;
    double Settings::* member;
    double initial;
    double minimum;
    double maximum;
    double step;
};

constexpr Number<LensFlareSettings> flareNumbers[] = {
    {"centerX", "Center X", &LensFlareSettings::centerX, 0.28, -1, 2, .01},
    {"centerY", "Center Y", &LensFlareSettings::centerY, 0.72, -1, 2, .01},
    {"brightness", "Brightness", &LensFlareSettings::brightness, 0.95, 0, 10, .05},
    {"size", "Size", &LensFlareSettings::size, 1, .05, 5, .05},
    {"streak", "Streak strength", &LensFlareSettings::streak, 1, 0, 3, .05},
    {"halo", "Halo strength", &LensFlareSettings::halo, .9, 0, 3, .05},
    {"ghosts", "Ghost strength", &LensFlareSettings::ghosts, .8, 0, 3, .05},
    {"rotation", "Streak rotation (degrees)", &LensFlareSettings::rotation, 0, -360, 360, 1}
};

constexpr Number<GlowSettings> glowNumbers[] = {
    {"threshold", "Threshold", &GlowSettings::threshold, .62, 0, 4, .01},
    {"knee", "Soft knee", &GlowSettings::knee, .22, 0, 2, .01},
    {"radius", "Radius (% of height)", &GlowSettings::radius, 2.8, 0, 30, .1},
    {"intensity", "Intensity", &GlowSettings::intensity, .8, 0, 10, .05},
    {"spread", "Wide glow mix", &GlowSettings::spread, .38, 0, 1, .01}
};

constexpr Number<BlurTransform> transformNumbers[] = {
    {"shiftX", "Shift X (% of width)", &BlurTransform::shiftX, 0, -200, 200, .1},
    {"shiftY", "Shift Y (% of height)", &BlurTransform::shiftY, 0, -200, 200, .1},
    {"zoom", "Zoom", &BlurTransform::zoom, 1, .05, 20, .01},
    {"rotation", "Rotation (degrees)", &BlurTransform::angle, 0, -36000, 36000, 1},
    {"centerX", "Pivot X", &BlurTransform::centerX, .5, 0, 1, .01},
    {"centerY", "Pivot Y", &BlurTransform::centerY, .5, 0, 1, .01}
};

constexpr Number<BlurSettings> blurNumbers[] = {
    {"shutterDuration", "Shutter duration (frames)", &BlurSettings::shutterDuration, .5, 0, 8, .05},
    {"shutterShift", "Shutter shift (frames)", &BlurSettings::shutterShift, 0, -8, 8, .05},
    {"exposureBias", "Exposure bias", &BlurSettings::exposureBias, .5, 0, 1, .01}
};

constexpr Number<DamageSettings> damageNumbers[] = {
    {"amount", "Amount", &DamageSettings::amount, .7, 0, 1, .01},
    {"frequency", "Update rate (Hz)", &DamageSettings::frequency, 8, 0, 30, .1},
    {"displacement", "Band displacement (% of width)", &DamageSettings::displacement, 4, 0, 25, .1},
    {"chroma", "RGB split (% of width)", &DamageSettings::chroma, .7, 0, 10, .1},
    {"blocks", "Macroblocks", &DamageSettings::blocks, .42, 0, 1, .01},
    {"scanlines", "Scanlines", &DamageSettings::scanlines, .18, 0, 1, .01},
    {"noise", "Noise", &DamageSettings::noise, .16, 0, 1, .01},
    {"dropout", "Signal dropout", &DamageSettings::dropout, .16, 0, 1, .01}
};

constexpr Number<RaysSettings> raysNumbers[] = {
    {"centerX", "Origin X", &RaysSettings::centerX, .5, -2, 3, .01},
    {"centerY", "Origin Y", &RaysSettings::centerY, .7, -2, 3, .01},
    {"length", "Ray length", &RaysSettings::length, .65, 0, 2, .01},
    {"brightness", "Brightness", &RaysSettings::brightness, 1.6, 0, 10, .05},
    {"threshold", "Highlight threshold", &RaysSettings::threshold, .54, 0, 1, .01},
    {"softness", "Highlight softness", &RaysSettings::softness, .18, 0, 1, .01},
    {"decay", "Ray decay", &RaysSettings::decay, 2.1, 0, 8, .05}
};

constexpr Number<ZapSettings> zapNumbers[] = {
    {"startX", "Start X", &ZapSettings::startX, .12, -1, 2, .01},
    {"startY", "Start Y", &ZapSettings::startY, .35, -1, 2, .01},
    {"endX", "End X", &ZapSettings::endX, .86, -1, 2, .01},
    {"endY", "End Y", &ZapSettings::endY, .65, -1, 2, .01},
    {"bend", "Bend (% of height)", &ZapSettings::bend, 5, -100, 100, .25},
    {"brightness", "Brightness", &ZapSettings::brightness, 1.4, 0, 10, .05},
    {"coreWidth", "Core width (% of height)", &ZapSettings::coreWidth, .15, .025, 4, .025},
    {"glowRadius", "Glow radius (% of height)", &ZapSettings::glowRadius, 1.3, 0, 10, .05},
    {"jaggedness", "Jaggedness", &ZapSettings::jaggedness, .55, 0, 1, .01},
    {"branching", "Branching", &ZapSettings::branching, .5, 0, 1, .01},
    {"speed", "Animation rate (Hz)", &ZapSettings::speed, 6, 0, 24, .1}
};

constexpr Number<PixelSortSettings> sortNumbers[] = {
    {"low", "Low luma", &PixelSortSettings::low, .26, 0, 1, .01},
    {"high", "High luma", &PixelSortSettings::high, .89, 0, 1, .01},
    {"angle", "Direction (degrees)", &PixelSortSettings::angle, 0, -180, 180, 1},
    {"mix", "Mix", &PixelSortSettings::mix, .9, 0, 1, .01},
    {"minLength", "Min. run length (%)", &PixelSortSettings::minLength, 1, 0, 100, .5},
    {"maxLength", "Max. run length (%)", &PixelSortSettings::maxLength, 45, 0, 100, .5},
    {"fragmentation", "Fragmentation", &PixelSortSettings::fragmentation, .08, 0, 1, .01}
};

constexpr Number<DefocusSettings> defocusNumbers[] = {
    {"radius", "Radius (% of height)", &DefocusSettings::radius, 1.6, 0, 20, .1},
    {"roundness", "Iris roundness", &DefocusSettings::roundness, .25, 0, 1, .01},
    {"rotation", "Iris rotation (degrees)", &DefocusSettings::rotation, 0, -360, 360, 1},
    {"aspect", "Iris aspect ratio", &DefocusSettings::aspect, 1, .25, 4, .025},
    {"highlightThreshold", "Highlight threshold", &DefocusSettings::highlightThreshold, .72, 0, 4, .01},
    {"highlightBoost", "Highlight boost", &DefocusSettings::highlightBoost, .8, 0, 8, .05},
    {"mix", "Mix", &DefocusSettings::mix, 1, 0, 1, .01}
};

constexpr Number<LightLeakSettings> leakNumbers[] = {
    {"centerX", "Center X", &LightLeakSettings::centerX, .1, -1, 2, .01},
    {"centerY", "Center Y", &LightLeakSettings::centerY, .45, -1, 2, .01},
    {"brightness", "Brightness", &LightLeakSettings::brightness, .6, 0, 10, .05},
    {"size", "Size", &LightLeakSettings::size, 1, .05, 4, .05},
    {"softness", "Softness", &LightLeakSettings::softness, .65, 0, 1, .01},
    {"angle", "Angle (degrees)", &LightLeakSettings::angle, 12, -360, 360, 1},
    {"speed", "Animation rate (Hz)", &LightLeakSettings::speed, .25, 0, 4, .01},
    {"drift", "Drift", &LightLeakSettings::drift, .25, 0, 1, .01},
    {"phase", "Phase (cycles)", &LightLeakSettings::phase, 0, -10, 10, .025}
};

constexpr Number<FilmSettings> filmNumbers[] = {
    {"exposure", "Exposure (stops)", &FilmSettings::exposure, 0, -5, 5, .05},
    {"contrast", "Contrast", &FilmSettings::contrast, 1.08, .1, 3, .025},
    {"fade", "Shadow fade", &FilmSettings::fade, .05, 0, 1, .01},
    {"saturation", "Saturation", &FilmSettings::saturation, .95, 0, 2, .025},
    {"warmth", "Warmth", &FilmSettings::warmth, .12, -1, 1, .025},
    {"grain", "Grain amount", &FilmSettings::grain, .22, 0, 1, .01},
    {"grainSize", "Grain size (px at 1080p)", &FilmSettings::grainSize, 1.25, .35, 6, .05},
    {"halation", "Halation", &FilmSettings::halation, .22, 0, 1, .01},
    {"halationRadius", "Halation radius (% of height)", &FilmSettings::halationRadius, 1.25, .1, 10, .05},
    {"vignette", "Vignette", &FilmSettings::vignette, .16, 0, 1, .01},
    {"mix", "Mix", &FilmSettings::mix, 1, 0, 1, .01}
};

constexpr Number<VHSSettings> vhsNumbers[] = {
    {"amount", "Amount", &VHSSettings::amount, 0.75, 0, 1, 0.01},
    {"frequency", "Update rate (Hz)", &VHSSettings::frequency, 24, 0, 60, 1},
    {"tracking", "Tracking distortion", &VHSSettings::tracking, 0.35, 0, 1, 0.01},
    {"jitter", "Horizontal jitter", &VHSSettings::jitter, 0.2, 0, 1, 0.01},
    {"chromaBleed", "Chroma bleed", &VHSSettings::chromaBleed, 0.45, 0, 1, 0.01},
    {"scanlines", "Scanlines", &VHSSettings::scanlines, 0.28, 0, 1, 0.01},
    {"noise", "Tape noise", &VHSSettings::noise, 0.22, 0, 1, 0.01},
    {"dropout", "Dropouts", &VHSSettings::dropout, 0.15, 0, 1, 0.01}
};

constexpr Number<FilmBurnSettings> burnNumbers[] = {
    {"centerX", "Center X", &FilmBurnSettings::centerX, 0.12, -1, 2, 0.01},
    {"centerY", "Center Y", &FilmBurnSettings::centerY, 0.42, -1, 2, 0.01},
    {"amount", "Exposure strength", &FilmBurnSettings::amount, 0.8, 0, 4, 0.05},
    {"size", "Burn size", &FilmBurnSettings::size, 1, 0.05, 4, 0.05},
    {"softness", "Edge softness", &FilmBurnSettings::softness, 0.55, 0, 1, 0.01},
    {"angle", "Angle (degrees)", &FilmBurnSettings::angle, 13, -360, 360, 1},
    {"speed", "Drift speed (Hz)", &FilmBurnSettings::speed, 0.7, 0, 8, 0.05}
};

constexpr Number<DistortRGBSettings> distortRgbNumbers[] = {
    {"amount", "Displacement (% of height)", &DistortRGBSettings::amount, 2.5, -25, 25, 0.05},
    {"detail", "Detail radius (% of height)", &DistortRGBSettings::detail, 0.65, 0.05, 5, 0.05},
    {"direction", "Direction (degrees)", &DistortRGBSettings::direction, 0, -360, 360, 1},
    {"red", "Red displacement", &DistortRGBSettings::red, 1, -3, 3, 0.05},
    {"green", "Green displacement", &DistortRGBSettings::green, 0.35, -3, 3, 0.05},
    {"blue", "Blue displacement", &DistortRGBSettings::blue, -0.8, -3, 3, 0.05},
    {"mix", "Mix", &DistortRGBSettings::mix, 1, 0, 1, 0.01}
};

constexpr Number<WarpFishEyeSettings> fishEyeNumbers[] = {
    {"amount", "Curvature", &WarpFishEyeSettings::amount, 0.6, -2, 2, 0.02},
    {"centerX", "Center X", &WarpFishEyeSettings::centerX, 0.5, -1, 2, 0.01},
    {"centerY", "Center Y", &WarpFishEyeSettings::centerY, 0.5, -1, 2, 0.01},
    {"zoom", "Zoom", &WarpFishEyeSettings::zoom, 1, 0.25, 4, 0.01},
    {"mix", "Mix", &WarpFishEyeSettings::mix, 1, 0, 1, 0.01}
};

constexpr Number<WarpWavesSettings> wavesNumbers[] = {
    {"amplitude", "Amplitude (% of height)", &WarpWavesSettings::amplitude, 2, 0, 25, 0.05},
    {"frequency", "Cycles per frame height", &WarpWavesSettings::frequency, 4, 0.1, 64, 0.1},
    {"direction", "Travel angle (degrees)", &WarpWavesSettings::direction, 90, -360, 360, 1},
    {"phase", "Phase (degrees)", &WarpWavesSettings::phase, 0, -360, 360, 1},
    {"speed", "Speed (cycles/sec)", &WarpWavesSettings::speed, 0.5, -12, 12, 0.05},
    {"crossWave", "Cross wave", &WarpWavesSettings::crossWave, 0.25, 0, 1, 0.01},
    {"mix", "Mix", &WarpWavesSettings::mix, 1, 0, 1, 0.01}
};

constexpr Number<GlareSettings> glareNumbers[] = {
    {"threshold", "Threshold", &GlareSettings::threshold, 0.7, 0, 4, 0.01},
    {"knee", "Soft knee", &GlareSettings::knee, 0.2, 0, 2, 0.01},
    {"radius", "Radius (% of height)", &GlareSettings::radius, 9, 0, 30, 0.1},
    {"intensity", "Intensity", &GlareSettings::intensity, 0.9, 0, 8, 0.05},
    {"dispersion", "Dispersion", &GlareSettings::dispersion, 0.3, 0, 1, 0.01},
    {"rotation", "Rotation (degrees)", &GlareSettings::rotation, 10, -360, 360, 1}
};

constexpr Number<GlintSettings> glintNumbers[] = {
    {"threshold", "Threshold", &GlintSettings::threshold, 0.78, 0, 4, 0.01},
    {"knee", "Soft knee", &GlintSettings::knee, 0.2, 0, 2, 0.01},
    {"length", "Length (% of height)", &GlintSettings::length, 4.5, 0, 25, 0.1},
    {"intensity", "Intensity", &GlintSettings::intensity, 0.9, 0, 8, 0.05},
    {"angle", "Angle (degrees)", &GlintSettings::angle, 24, -360, 360, 1},
    {"dispersion", "Dispersion", &GlintSettings::dispersion, 0.3, 0, 1, 0.01}
};

constexpr Number<StreaksSettings> streaksNumbers[] = {
    {"threshold", "Threshold", &StreaksSettings::threshold, 0.68, 0, 4, 0.01},
    {"knee", "Soft knee", &StreaksSettings::knee, 0.2, 0, 2, 0.01},
    {"length", "Length (% of height)", &StreaksSettings::length, 18, 0, 50, 0.1},
    {"intensity", "Intensity", &StreaksSettings::intensity, 0.9, 0, 8, 0.05},
    {"angle", "Angle (degrees)", &StreaksSettings::angle, 0, -360, 360, 1},
    {"spread", "Angular spread", &StreaksSettings::spread, 0.2, 0, 1, 0.01}
};

constexpr Number<HalfToneSettings> halfToneNumbers[] = {
    {"cellSize", "Cell size (px at 1080p)", &HalfToneSettings::cellSize, 9, 2, 120, 0.5},
    {"angle", "Screen angle (degrees)", &HalfToneSettings::angle, 22.5, -360, 360, 1},
    {"softness", "Dot softness", &HalfToneSettings::softness, 0.2, 0, 1, 0.01},
    {"threshold", "Midpoint", &HalfToneSettings::threshold, 0.5, 0, 1, 0.01},
    {"contrast", "Contrast", &HalfToneSettings::contrast, 1, 0.2, 4, 0.05},
    {"mix", "Mix", &HalfToneSettings::mix, 1, 0, 1, 0.01}
};

constexpr Number<BleachBypassSettings> bleachNumbers[] = {
    {"amount", "Amount", &BleachBypassSettings::amount, 0.8, 0, 1, 0.01},
    {"desaturation", "Desaturation", &BleachBypassSettings::desaturation, 0.65, 0, 1, 0.01},
    {"contrast", "Contrast", &BleachBypassSettings::contrast, 1.3, 0.25, 3, 0.05},
    {"exposure", "Exposure (stops)", &BleachBypassSettings::exposure, 0, -5, 5, 0.05},
    {"shadowLift", "Shadow lift", &BleachBypassSettings::shadowLift, 0, -0.25, 0.25, 0.01},
    {"highlightRolloff", "Highlight roll-off", &BleachBypassSettings::highlightRolloff, 0.25, 0, 1, 0.01},
    {"temperature", "Temperature", &BleachBypassSettings::temperature, -0.08, -1, 1, 0.01},
    {"mix", "Mix", &BleachBypassSettings::mix, 1, 0, 1, 0.01}
};

constexpr Number<BlurSettings05> blur05Numbers[] = {
    {"radius", "Radius (% of height)", &BlurSettings05::radius, 0.65, 0, 15, 0.05},
    {"mix", "Mix", &BlurSettings05::mix, 1, 0, 1, 0.01}
};

constexpr Number<DirectionalBlurSettings> directionalBlurNumbers[] = {
    {"length", "Length (% of height)", &DirectionalBlurSettings::length, 5, 0, 35, 0.1},
    {"angle", "Angle (degrees)", &DirectionalBlurSettings::angle, 0, -360, 360, 1},
    {"bias", "Bias", &DirectionalBlurSettings::bias, 0, -1, 1, 0.01},
    {"mix", "Mix", &DirectionalBlurSettings::mix, 1, 0, 1, 0.01}
};

constexpr Number<SharpenSettings> sharpenNumbers[] = {
    {"amount", "Amount", &SharpenSettings::amount, 0.65, 0, 4, 0.05},
    {"radius", "Radius (% of height)", &SharpenSettings::radius, 0.18, 0, 4, 0.02},
    {"threshold", "Threshold", &SharpenSettings::threshold, 0.02, 0, 1, 0.01},
    {"mix", "Mix", &SharpenSettings::mix, 1, 0, 1, 0.01}
};

constexpr Number<VignetteSettings> vignetteNumbers[] = {
    {"centerX", "Center X", &VignetteSettings::centerX, 0.5, -1, 2, 0.01},
    {"centerY", "Center Y", &VignetteSettings::centerY, 0.5, -1, 2, 0.01},
    {"radius", "Radius", &VignetteSettings::radius, 0.64, 0.02, 2, 0.02},
    {"softness", "Softness", &VignetteSettings::softness, 0.45, 0, 1, 0.01},
    {"roundness", "Roundness", &VignetteSettings::roundness, 0.45, 0, 1, 0.01},
    {"angle", "Angle (degrees)", &VignetteSettings::angle, 0, -360, 360, 1},
    {"amount", "Amount", &VignetteSettings::amount, 0.5, 0, 1, 0.01}
};

constexpr Number<GrainSettings> grainNumbers[] = {
    {"amount", "Amount", &GrainSettings::amount, 0.22, 0, 1, 0.01},
    {"size", "Size (px at 1080p)", &GrainSettings::size, 1.35, 0.25, 12, 0.05},
    {"roughness", "Roughness", &GrainSettings::roughness, 0.5, 0, 1, 0.01},
    {"color", "Color grain", &GrainSettings::color, 0.12, 0, 1, 0.01},
    {"response", "Luminance response", &GrainSettings::response, 0.7, 0, 1, 0.01},
    {"speed", "Update rate (Hz)", &GrainSettings::speed, 24, 0, 120, 1}
};

constexpr Number<HalationSettings> halationNumbers[] = {
    {"threshold", "Threshold", &HalationSettings::threshold, 0.62, 0, 4, 0.01},
    {"knee", "Soft knee", &HalationSettings::knee, 0.2, 0, 2, 0.01},
    {"radius", "Radius (% of height)", &HalationSettings::radius, 0.9, 0, 8, 0.05},
    {"amount", "Amount", &HalationSettings::amount, 0.72, 0, 4, 0.05}
};

constexpr Number<TransitionSettings> transitionCommon[] = {
    {"ease", "Ease", &TransitionSettings::ease, .5, 0, 1, .01}
};

constexpr Number<TransitionSettings> transitionGlow[] = {
    {"intensity", "Intensity", &TransitionSettings::intensity, 1, 0, 4, .05},
    {"threshold", "Highlight threshold", &TransitionSettings::threshold, .55, 0, 4, .01},
    {"radius", "Radius (% of height)", &TransitionSettings::radius, 3.5, 0, 15, .1}
};

constexpr Number<TransitionSettings> transitionShake[] = {
    {"shakeAmount", "Shake (% of width)", &TransitionSettings::shakeAmount, 1.75, 0, 15, .05},
    {"tilt", "Tilt (degrees)", &TransitionSettings::tilt, 2, 0, 15, .1},
    {"frequency", "Shake cycles", &TransitionSettings::frequency, 5, 0, 30, .1},
    {"shutter", "Shutter", &TransitionSettings::shutter, .45, 0, 1, .01}
};

constexpr Number<TransitionSettings> transitionSwish[] = {
    {"angle", "Direction (degrees)", &TransitionSettings::angle, 0, -360, 360, 1},
    {"shutter", "Shutter", &TransitionSettings::shutter, .45, 0, 1, .01}
};

constexpr Number<GlowAuraSettings> glowAuraNumbers[] = {
    {"threshold", "Threshold", &GlowAuraSettings::threshold, 0.38, 0, 4, 0.01},
    {"knee", "Soft knee", &GlowAuraSettings::knee, 0.15, 0, 2, 0.01},
    {"radius", "Radius (% of height)", &GlowAuraSettings::radius, 3.5, 0, 30, 0.1},
    {"intensity", "Intensity", &GlowAuraSettings::intensity, 0.95, 0, 10, 0.05},
    {"frequency", "Frequency (cycles)", &GlowAuraSettings::frequency, 4, 0, 16, 0.1},
    {"phase", "Phase (turns)", &GlowAuraSettings::phase, 0, -1, 1, 0.01},
    {"phaseSpeed", "Phase speed (turns/sec)", &GlowAuraSettings::phaseSpeed, 0.22, -5, 5, 0.025},
    {"twist", "Twist (turns)", &GlowAuraSettings::twist, 0.6, -4, 4, 0.05}
};

constexpr Number<GlowDarksSettings> glowDarksNumbers[] = {
    {"threshold", "Shadow threshold", &GlowDarksSettings::threshold, 0.42, 0, 2, 0.01},
    {"knee", "Soft knee", &GlowDarksSettings::knee, 0.14, 0, 1, 0.01},
    {"radius", "Radius (% of height)", &GlowDarksSettings::radius, 3.5, 0, 30, 0.1},
    {"intensity", "Intensity", &GlowDarksSettings::intensity, 0.85, 0, 4, 0.05},
    {"spread", "Wide glow mix", &GlowDarksSettings::spread, 0.4, 0, 1, 0.01}
};

constexpr Number<GlowDistSettings> glowDistNumbers[] = {
    {"threshold", "Threshold", &GlowDistSettings::threshold, 0.45, 0, 4, 0.01},
    {"radius", "Radius (% of height)", &GlowDistSettings::radius, 3.5, 0, 30, 0.1},
    {"intensity", "Intensity", &GlowDistSettings::intensity, 1.1, 0, 10, 0.05},
    {"falloff", "Falloff", &GlowDistSettings::falloff, 1.65, 0.25, 4, 0.05},
    {"redWidth", "Red width", &GlowDistSettings::redWidth, 0.75, 0, 3, 0.05},
    {"greenWidth", "Green width", &GlowDistSettings::greenWidth, 1, 0, 3, 0.05},
    {"blueWidth", "Blue width", &GlowDistSettings::blueWidth, 1.25, 0, 3, 0.05},
    {"alphaWeight", "Alpha influence", &GlowDistSettings::alphaWeight, 0, 0, 1, 0.01}
};

constexpr Number<GlowEdgesSettings> glowEdgesNumbers[] = {
    {"threshold", "Edge threshold", &GlowEdgesSettings::threshold, 0.075, 0, 4, 0.005},
    {"softness", "Edge softness", &GlowEdgesSettings::softness, 0.12, 0, 1, 0.01},
    {"edgeSize", "Edge size (px at 1080p)", &GlowEdgesSettings::edgeSize, 2, 0.5, 16, 0.25},
    {"radius", "Radius (% of height)", &GlowEdgesSettings::radius, 2, 0, 30, 0.1},
    {"intensity", "Intensity", &GlowEdgesSettings::intensity, 1.2, 0, 10, 0.05},
    {"spread", "Wide glow mix", &GlowEdgesSettings::spread, 0.35, 0, 1, 0.01},
    {"alphaWeight", "Alpha influence", &GlowEdgesSettings::alphaWeight, 0.35, 0, 1, 0.01}
};

constexpr Number<GlowNoiseSettings> glowNoiseNumbers[] = {
    {"threshold", "Highlight threshold", &GlowNoiseSettings::threshold, 0.55, 0, 4, 0.01},
    {"knee", "Soft knee", &GlowNoiseSettings::knee, 0.15, 0, 1, 0.01},
    {"radius", "Radius (% of height)", &GlowNoiseSettings::radius, 3, 0, 20, 0.1},
    {"brightness", "Brightness", &GlowNoiseSettings::brightness, 0.9, 0, 8, 0.05},
    {"noiseAmount", "Noise amount", &GlowNoiseSettings::noiseAmount, 0.65, 0, 1, 0.01},
    {"noiseScale", "Noise size (px at 1080p)", &GlowNoiseSettings::noiseScale, 10, 1, 150, 1},
    {"speed", "Noise speed (Hz)", &GlowNoiseSettings::speed, 0.5, 0, 12, 0.05}
};

constexpr Number<GlowOrthiconSettings> glowOrthiconNumbers[] = {
    {"threshold", "Highlight threshold", &GlowOrthiconSettings::threshold, 0.55, 0, 4, 0.01},
    {"knee", "Soft knee", &GlowOrthiconSettings::knee, 0.15, 0, 1, 0.01},
    {"radius", "Radius (% of height)", &GlowOrthiconSettings::radius, 1.6, 0, 20, 0.1},
    {"darkness", "Darkness", &GlowOrthiconSettings::darkness, 0.75, 0, 1, 0.01}
};

constexpr Number<GlowRainbowSettings> glowRainbowNumbers[] = {
    {"threshold", "Highlight threshold", &GlowRainbowSettings::threshold, 0.55, 0, 4, 0.01},
    {"knee", "Soft knee", &GlowRainbowSettings::knee, 0.15, 0, 1, 0.01},
    {"radius", "Radius (% of height)", &GlowRainbowSettings::radius, 3.5, 0, 20, 0.1},
    {"brightness", "Brightness", &GlowRainbowSettings::brightness, 0.9, 0, 8, 0.05},
    {"cycles", "Color cycles", &GlowRainbowSettings::cycles, 1.35, 0, 8, 0.05},
    {"phase", "Phase (turns)", &GlowRainbowSettings::phase, 0, -8, 8, 0.05},
    {"speed", "Cycle speed (Hz)", &GlowRainbowSettings::speed, 0.28, -8, 8, 0.025},
    {"saturation", "Saturation", &GlowRainbowSettings::saturation, 0.95, 0, 1, 0.01}
};

constexpr Number<GlowRingsSettings> glowRingsNumbers[] = {
    {"threshold", "Highlight threshold", &GlowRingsSettings::threshold, 0.55, 0, 4, 0.01},
    {"knee", "Soft knee", &GlowRingsSettings::knee, 0.15, 0, 1, 0.01},
    {"radius", "Radius (% of height)", &GlowRingsSettings::radius, 4.5, 0, 20, 0.1},
    {"brightness", "Brightness", &GlowRingsSettings::brightness, 0.9, 0, 8, 0.05},
    {"thickness", "Ring thickness", &GlowRingsSettings::thickness, 0.24, 0.025, 1, 0.025},
    {"dispersion", "Color dispersion", &GlowRingsSettings::dispersion, 0.45, 0, 1, 0.01}
};

constexpr Number<UltraGlowSettings> ultraGlowNumbers[] = {
    {"threshold", "Highlight threshold", &UltraGlowSettings::threshold, 0.58, 0, 4, 0.01},
    {"knee", "Soft knee", &UltraGlowSettings::knee, 0.18, 0, 2, 0.01},
    {"radius", "Radius (% of height)", &UltraGlowSettings::radius, 2.6, 0, 20, 0.1},
    {"brightness", "Brightness", &UltraGlowSettings::brightness, 1.2, 0, 8, 0.05},
    {"spread", "Wide glow mix", &UltraGlowSettings::spread, 0.35, 0, 1, 0.01},
    {"stretch", "Stretch (vertical to horizontal)", &UltraGlowSettings::stretch, 0, -1.5, 1.5, 0.025},
    {"redWidth", "Red width", &UltraGlowSettings::redWidth, 1, 0, 3, 0.025},
    {"greenWidth", "Green width", &UltraGlowSettings::greenWidth, 1, 0, 3, 0.025},
    {"blueWidth", "Blue width", &UltraGlowSettings::blueWidth, 1, 0, 3, 0.025},
    {"afterglow", "Afterglow", &UltraGlowSettings::afterglow, 0.55, 0, 3, 0.05}
};

constexpr Number<EdgeRaysSettings> edgeRaysNumbers[] = {
    {"centerX", "Origin X", &EdgeRaysSettings::centerX, 0.5, -2, 3, 0.01},
    {"centerY", "Origin Y", &EdgeRaysSettings::centerY, 0.55, -2, 3, 0.01},
    {"length", "Ray length", &EdgeRaysSettings::length, 0.6, 0, 2, 0.01},
    {"brightness", "Brightness", &EdgeRaysSettings::brightness, 2.2, 0, 10, 0.05},
    {"threshold", "Edge threshold", &EdgeRaysSettings::threshold, 0.055, 0, 1, 0.005},
    {"softness", "Edge softness", &EdgeRaysSettings::softness, 0.07, 0, 1, 0.005},
    {"edgeSize", "Edge size (px at 1080p)", &EdgeRaysSettings::edgeSize, 2, 0.5, 16, 0.25},
    {"decay", "Ray decay", &EdgeRaysSettings::decay, 1.4, 0, 8, 0.05}
};

class Controls {
public:
    explicit Controls(Parameters parameters) : params_(parameters) {}

    template<typename Settings, std::size_t Count>
    void numbers(const Number<Settings> (&numbers)[Count]) const
    {
        for (const auto& number : numbers) {
            const auto prop = define(kOfxParamTypeDouble, number.name, number.label);
            require(params_.properties->propSetDouble(prop, kOfxParamPropDefault, 0, number.initial));
            require(params_.properties->propSetDouble(prop, kOfxParamPropMin, 0, number.minimum));
            require(params_.properties->propSetDouble(prop, kOfxParamPropMax, 0, number.maximum));
            const double displayMin = std::max(-360.0, number.minimum);
            const double displayMax = std::min(360.0, number.maximum);
            params_.properties->propSetDouble(prop, kOfxParamPropDisplayMin, 0, displayMin);
            params_.properties->propSetDouble(prop, kOfxParamPropDisplayMax, 0, displayMax);
            params_.properties->propSetDouble(prop, kOfxParamPropIncrement, 0, number.step);
            params_.properties->propSetInt(prop, kOfxParamPropDigits, 0, 2);
        }
    }

    void choice(const char* name, const char* label, int initial,
                std::initializer_list<const char*> options) const
    {
        const auto prop = define(kOfxParamTypeChoice, name, label);
        int index = 0;
        for (const auto* option : options) {
            require(params_.properties->propSetString(prop, kOfxParamPropChoiceOption, index++, option));
        }
        require(params_.properties->propSetInt(prop, kOfxParamPropDefault, 0, initial));
    }

    void checkbox(const char* name, const char* label) const
    {
        const auto prop = define(kOfxParamTypeBoolean, name, label);
        require(params_.properties->propSetInt(prop, kOfxParamPropDefault, 0, 0));
    }

    void integer(const char* name, const char* label, int initial,
                 int minimum, int maximum, int displayMaximum) const
    {
        const auto prop = define(kOfxParamTypeInteger, name, label);
        require(params_.properties->propSetInt(prop, kOfxParamPropDefault, 0, initial));
        require(params_.properties->propSetInt(prop, kOfxParamPropMin, 0, minimum));
        require(params_.properties->propSetInt(prop, kOfxParamPropMax, 0, maximum));
        params_.properties->propSetInt(prop, kOfxParamPropDisplayMin, 0, minimum);
        params_.properties->propSetInt(prop, kOfxParamPropDisplayMax, 0, displayMaximum);
    }

    void tint(double red, double green, double blue) const
    {
        color("tint", "Tint", red, green, blue);
    }

    void color(const char* name, const char* label, double red, double green, double blue) const
    {
        const auto prop = define(kOfxParamTypeRGB, name, label);
        const double components[] = {red, green, blue};
        require(params_.properties->propSetDoubleN(prop, kOfxParamPropDefault, 3, components));
    }

    template<typename Settings, std::size_t Count>
    void read(Settings& settings, const Number<Settings> (&numbers)[Count], OfxTime time) const
    {
        for (const auto& number : numbers) {
            double value = number.initial;
            require(params_.suite->paramGetValueAtTime(get(number.name), time, &value));
            settings.*number.member = std::isfinite(value)
                ? std::clamp(value, number.minimum, number.maximum) : number.initial;
        }
    }

    int readInt(const char* name, OfxTime time) const
    {
        int value = 0;
        require(params_.suite->paramGetValueAtTime(get(name), time, &value));
        return value;
    }

    double readDouble(const char* name, OfxTime time) const
    {
        double value = 0;
        require(params_.suite->paramGetValueAtTime(get(name), time, &value));
        return value;
    }

    Edge readEdges(OfxTime time) const
    {
        switch (readInt("edges", time)) {
        case 1: return Edge::Clamp;
        case 2: return Edge::Transparent;
        default: return Edge::Reflect;
        }
    }

    template<typename Settings>
    void readTint(Settings& settings, OfxTime time) const
    {
        readColor("tint", time, settings.red, settings.green, settings.blue);
    }

    void readColor(const char* name, OfxTime time, double& red, double& green, double& blue) const
    {
        require(params_.suite->paramGetValueAtTime(get(name), time, &red, &green, &blue));
        const auto component = [](double value) {
            return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 1.0;
        };
        red = component(red);
        green = component(green);
        blue = component(blue);
    }

private:
    Parameters params_;

    OfxPropertySetHandle define(const char* type, const char* name, const char* label) const
    {
        OfxPropertySetHandle prop = nullptr;
        require(params_.suite->paramDefine(params_.set, type, name, &prop));
        require(params_.properties->propSetString(prop, kOfxPropLabel, 0, label));
        return prop;
    }

    OfxParamHandle get(const char* name) const
    {
        OfxParamHandle param = nullptr;
        require(params_.suite->paramGetHandle(params_.set, name, &param, nullptr));
        return param;
    }
};

BlurTransform evaluateTransform(void* controls, double time)
{
    BlurTransform result{};
    static_cast<const Controls*>(controls)->read(result, transformNumbers, time);
    return result;
}

} // namespace

void describeControls(Kind kind, Parameters parameters)
{
    const Controls controls(parameters);
    switch (kind) {
    case Kind::LensFlare:
        controls.choice("style", "Optical profile", 0, {"Anamorphic", "Spherical", "Soft Bloom"});
        controls.numbers(flareNumbers);
        controls.tint(1, .94, .85);
        controls.checkbox("flareOnly", "Flare only");
        break;
    case Kind::Glow:
        controls.numbers(glowNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::BlurMoCurves:
        controls.choice("mode", "Mode", 0, {"Transform and Blur", "Blur Only"});
        controls.numbers(transformNumbers);
        controls.numbers(blurNumbers);
        controls.integer("samples", "Exposure samples", 16, 2, 64, 64);
        controls.choice("edges", "Edges", 0, {"Reflect", "Clamp", "Transparent"});
        break;
    case Kind::DigitalDamage:
        controls.numbers(damageNumbers);
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        break;
    case Kind::Rays:
        controls.numbers(raysNumbers);
        controls.tint(1, .96, .86);
        controls.integer("samples", "Ray samples", 36, 8, 96, 96);
        controls.checkbox("raysOnly", "Rays only");
        break;
    case Kind::UltraZap:
        controls.numbers(zapNumbers);
        controls.tint(.52, .76, 1);
        controls.integer("bolts", "Bolts", 1, 1, 5, 5);
        controls.integer("detail", "Detail", 6, 3, 8, 8);
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        controls.checkbox("boltOnly", "Bolt only");
        break;
    case Kind::PixelSort:
        controls.numbers(sortNumbers);
        controls.choice("key", "Sort by", 0, {"Luma", "Red", "Green", "Blue", "Saturation"});
        controls.checkbox("descending", "Reverse order");
        controls.checkbox("outside", "Sort outside luma range");
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        break;
    case Kind::RackDefocus:
        controls.numbers(defocusNumbers);
        controls.integer("blades", "Iris blades", 7, 3, 12, 12);
        controls.integer("samples", "Aperture quality", 48, 12, 192, 192);
        controls.choice("edges", "Edges", 0, {"Reflect", "Clamp", "Transparent"});
        break;
    case Kind::LightLeak:
        controls.choice("style", "Profile", 0, {"Warm Film", "Cool Prism", "Golden Sweep"});
        controls.numbers(leakNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 1, {"Add", "Screen"});
        controls.checkbox("leakOnly", "Leak only");
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        break;
    case Kind::FilmEffect:
        controls.choice("profile", "Print profile", 1, {"Neutral Print", "Warm Print", "Cool Print", "Black and White"});
        controls.numbers(filmNumbers);
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        break;
    case Kind::VHSDamage:
        controls.numbers(vhsNumbers);
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        break;
    case Kind::FilmBurn:
        controls.numbers(burnNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        controls.choice("style", "Film stock", 0, {"Amber", "Crimson", "Bleach"});
        controls.checkbox("burnOnly", "Burn only");
        break;
    case Kind::DistortRGB:
        controls.numbers(distortRgbNumbers);
        controls.choice("edges", "Edges", 0, {"Reflect", "Clamp", "Transparent"});
        break;
    case Kind::WarpFishEye:
        controls.numbers(fishEyeNumbers);
        controls.choice("edges", "Edges", 0, {"Reflect", "Clamp", "Transparent"});
        break;
    case Kind::WarpWaves:
        controls.numbers(wavesNumbers);
        controls.choice("edges", "Edges", 0, {"Reflect", "Clamp", "Transparent"});
        break;
    case Kind::Glare:
        controls.numbers(glareNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.integer("blades", "Blades", 6, 2, 12, 12);
        controls.checkbox("glareOnly", "Glare only");
        break;
    case Kind::Glint:
        controls.numbers(glintNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.integer("rays", "Rays", 4, 2, 12, 12);
        controls.checkbox("glintOnly", "Glint only");
        break;
    case Kind::Streaks:
        controls.numbers(streaksNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.integer("count", "Streak count", 2, 1, 8, 8);
        controls.checkbox("streaksOnly", "Streaks only");
        break;
    case Kind::HalfTone:
        controls.numbers(halfToneNumbers);
        controls.choice("mode", "Color mode", 0, {"Mono ink", "RGB", "CMYK"});
        controls.color("ink", "Ink color", 0, 0, 0);
        controls.color("paper", "Paper color", 1, 1, 1);
        controls.checkbox("invert", "Invert");
        break;
    case Kind::BleachBypass:
        controls.numbers(bleachNumbers);
        break;
    case Kind::Blur:
        controls.numbers(blur05Numbers);
        controls.choice("edges", "Edges", 0, {"Reflect", "Clamp", "Transparent"});
        break;
    case Kind::BlurDirectional:
        controls.numbers(directionalBlurNumbers);
        controls.integer("samples", "Exposure samples", 24, 4, 96, 96);
        controls.choice("edges", "Edges", 0, {"Reflect", "Clamp", "Transparent"});
        break;
    case Kind::Sharpen:
        controls.numbers(sharpenNumbers);
        break;
    case Kind::Vignette:
        controls.numbers(vignetteNumbers);
        controls.tint(0, 0, 0);
        break;
    case Kind::Grain:
        controls.numbers(grainNumbers);
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        break;
    case Kind::Halation:
        controls.numbers(halationNumbers);
        controls.tint(1, .26, .09);
        controls.checkbox("halationOnly", "Halation only");
        break;
    case Kind::Dissolve:
        controls.numbers(transitionCommon);
        break;
    case Kind::DissolveGlow:
        controls.numbers(transitionCommon);
        controls.numbers(transitionGlow);
        controls.tint(1, .85, .65);
        break;
    case Kind::DissolveShake:
        controls.numbers(transitionCommon);
        controls.numbers(transitionShake);
        controls.integer("samples", "Exposure samples", 9, 1, 32, 32);
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        break;
    case Kind::SwishPan:
        controls.numbers(transitionCommon);
        controls.numbers(transitionSwish);
        controls.integer("samples", "Exposure samples", 9, 1, 32, 32);
        break;
    case Kind::GlowAura:
        controls.numbers(glowAuraNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::GlowDarks:
        controls.numbers(glowDarksNumbers);
        controls.tint(0.12, 0.16, 0.22);
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::GlowDist:
        controls.numbers(glowDistNumbers);
        controls.tint(0.6, 0.83, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::GlowEdges:
        controls.numbers(glowEdgesNumbers);
        controls.tint(0.78, 0.88, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::GlowNoise:
        controls.numbers(glowNoiseNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.integer("seed", "Seed", 0, 0, std::numeric_limits<int>::max(), 100);
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::GlowOrthicon:
        controls.numbers(glowOrthiconNumbers);
        break;
    case Kind::GlowRainbow:
        controls.numbers(glowRainbowNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::GlowRings:
        controls.numbers(glowRingsNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.integer("rings", "Ring count", 3, 1, 6, 6);
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::UltraGlow:
        controls.numbers(ultraGlowNumbers);
        controls.tint(1, 1, 1);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.checkbox("glowOnly", "Glow only");
        break;
    case Kind::EdgeRays:
        controls.numbers(edgeRaysNumbers);
        controls.tint(1, 0.91, 0.75);
        controls.choice("blend", "Blend", 0, {"Add", "Screen"});
        controls.integer("samples", "Ray samples", 36, 8, 96, 96);
        controls.checkbox("raysOnly", "Rays only");
        break;
    case Kind::Shake:
        break;
    }
}

TransitionSettings transitionSettingsAt(Kind kind, Parameters parameters, OfxTime time)
{
    const Controls controls(parameters);
    TransitionSettings settings{};
    controls.read(settings, transitionCommon, time);
    switch (kind) {
    case Kind::DissolveGlow:
        controls.read(settings, transitionGlow, time);
        controls.readTint(settings, time);
        break;
    case Kind::DissolveShake:
        controls.read(settings, transitionShake, time);
        settings.samples = std::clamp(controls.readInt("samples", time), 1, 32);
        settings.seed = controls.readInt("seed", time);
        break;
    case Kind::SwishPan:
        controls.read(settings, transitionSwish, time);
        settings.samples = std::clamp(controls.readInt("samples", time), 1, 32);
        break;
    default:
        break;
    }
    return settings;
}

bool controlsAreIdentity(Kind kind, Parameters parameters, OfxTime time)
{
    const Controls controls(parameters);
    switch (kind) {
    case Kind::Blur:
        return controls.readDouble("radius", time) <= 0 || controls.readDouble("mix", time) <= 0;
    case Kind::BlurDirectional:
        return controls.readDouble("length", time) <= 0 || controls.readDouble("mix", time) <= 0;
    case Kind::Sharpen:
        return controls.readDouble("amount", time) <= 0 || controls.readDouble("radius", time) <= 0
            || controls.readDouble("mix", time) <= 0;
    case Kind::Vignette:
    case Kind::Grain:
        return controls.readDouble("amount", time) <= 0;
    case Kind::Halation:
        return controls.readDouble("amount", time) <= 0 && controls.readInt("halationOnly", time) == 0;
    case Kind::UltraZap:
        return controls.readDouble("brightness", time) <= 0 && controls.readInt("boltOnly", time) == 0;
    case Kind::LightLeak:
        return controls.readDouble("brightness", time) <= 0 && controls.readInt("leakOnly", time) == 0;
    case Kind::RackDefocus:
        return controls.readDouble("radius", time) <= 0 || controls.readDouble("mix", time) <= 0;
    case Kind::PixelSort:
    case Kind::FilmEffect:
    case Kind::DistortRGB:
    case Kind::WarpFishEye:
    case Kind::WarpWaves:
    case Kind::HalfTone:
    case Kind::BleachBypass:
        return controls.readDouble("mix", time) <= 0;
    case Kind::VHSDamage:
        return controls.readDouble("amount", time) <= 0;
    case Kind::FilmBurn:
        return controls.readDouble("amount", time) <= 0 && controls.readInt("burnOnly", time) == 0;
    case Kind::Glare:
        return controls.readDouble("intensity", time) <= 0 && controls.readInt("glareOnly", time) == 0;
    case Kind::Glint:
        return controls.readDouble("intensity", time) <= 0 && controls.readInt("glintOnly", time) == 0;
    case Kind::Streaks:
        return controls.readDouble("intensity", time) <= 0 && controls.readInt("streaksOnly", time) == 0;
    case Kind::GlowAura:
    case Kind::GlowDarks:
    case Kind::GlowDist:
    case Kind::GlowEdges:
        return controls.readDouble("intensity", time) <= 0 && controls.readInt("glowOnly", time) == 0;
    case Kind::GlowNoise:
    case Kind::GlowRainbow:
    case Kind::GlowRings:
    case Kind::UltraGlow:
        return controls.readDouble("brightness", time) <= 0 && controls.readInt("glowOnly", time) == 0;
    case Kind::GlowOrthicon:
        return controls.readDouble("darkness", time) <= 0 || controls.readDouble("radius", time) <= 0;
    case Kind::EdgeRays:
        return (controls.readDouble("brightness", time) <= 0 || controls.readDouble("length", time) <= 0)
            && controls.readInt("raysOnly", time) == 0;
    default:
        return false;
    }
}

void renderConfiguredEffect(Kind kind, Parameters parameters, const Context& context)
{
    Controls controls(parameters);
    const auto time = context.timeFrames;
    switch (kind) {
    case Kind::LensFlare: {
        LensFlareSettings settings{};
        controls.read(settings, flareNumbers, time);
        controls.readTint(settings, time);
        settings.style = std::clamp(controls.readInt("style", time), 0, 2);
        settings.flareOnly = controls.readInt("flareOnly", time) != 0;
        renderLensFlare(context, settings);
        break;
    }
    case Kind::Glow: {
        GlowSettings settings{};
        controls.read(settings, glowNumbers, time);
        controls.readTint(settings, time);
        settings.blend = controls.readInt("blend", time);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderGlow(context, settings);
        break;
    }
    case Kind::BlurMoCurves: {
        BlurSettings settings{};
        controls.read(settings, blurNumbers, time);
        settings.samples = std::clamp(controls.readInt("samples", time), 2, 64);
        settings.edge = controls.readEdges(time);
        settings.blurOnly = controls.readInt("mode", time) == 1;
        settings.evaluate = evaluateTransform;
        settings.evalContext = &controls;
        renderBlurMoCurves(context, settings);
        break;
    }
    case Kind::DigitalDamage: {
        DamageSettings settings{};
        controls.read(settings, damageNumbers, time);
        settings.seed = controls.readInt("seed", time);
        renderDigitalDamage(context, settings);
        break;
    }
    case Kind::Rays: {
        RaysSettings settings{};
        controls.read(settings, raysNumbers, time);
        controls.readTint(settings, time);
        settings.samples = std::clamp(controls.readInt("samples", time), 8, 96);
        settings.raysOnly = controls.readInt("raysOnly", time) != 0;
        renderRays(context, settings);
        break;
    }
    case Kind::UltraZap: {
        ZapSettings settings{};
        controls.read(settings, zapNumbers, time);
        controls.readTint(settings, time);
        settings.bolts = std::clamp(controls.readInt("bolts", time), 1, 5);
        settings.detail = std::clamp(controls.readInt("detail", time), 3, 8);
        settings.seed = controls.readInt("seed", time);
        settings.boltOnly = controls.readInt("boltOnly", time) != 0;
        renderUltraZap(context, settings);
        break;
    }
    case Kind::PixelSort: {
        PixelSortSettings settings{};
        controls.read(settings, sortNumbers, time);
        settings.key = std::clamp(controls.readInt("key", time), 0, 4);
        settings.seed = controls.readInt("seed", time);
        settings.descending = controls.readInt("descending", time) != 0;
        settings.outside = controls.readInt("outside", time) != 0;
        renderPixelSort(context, settings);
        break;
    }
    case Kind::RackDefocus: {
        DefocusSettings settings{};
        controls.read(settings, defocusNumbers, time);
        settings.blades = std::clamp(controls.readInt("blades", time), 3, 12);
        settings.samples = std::clamp(controls.readInt("samples", time), 12, 192);
        settings.edge = controls.readEdges(time);
        renderRackDefocus(context, settings);
        break;
    }
    case Kind::LightLeak: {
        LightLeakSettings settings{};
        controls.read(settings, leakNumbers, time);
        controls.readTint(settings, time);
        settings.style = std::clamp(controls.readInt("style", time), 0, 2);
        settings.blend = controls.readInt("blend", time);
        settings.seed = controls.readInt("seed", time);
        settings.leakOnly = controls.readInt("leakOnly", time) != 0;
        renderLightLeak(context, settings);
        break;
    }
    case Kind::FilmEffect: {
        FilmSettings settings{};
        controls.read(settings, filmNumbers, time);
        settings.profile = std::clamp(controls.readInt("profile", time), 0, 3);
        settings.seed = controls.readInt("seed", time);
        renderFilmEffect(context, settings);
        break;
    }
    case Kind::VHSDamage: {
        VHSSettings settings{};
        controls.read(settings, vhsNumbers, time);
        settings.seed = controls.readInt("seed", time);
        renderVHSDamage(context, settings);
        break;
    }
    case Kind::FilmBurn: {
        FilmBurnSettings settings{};
        controls.read(settings, burnNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.seed = controls.readInt("seed", time);
        settings.style = std::clamp(controls.readInt("style", time), 0, 2);
        settings.burnOnly = controls.readInt("burnOnly", time) != 0;
        renderFilmBurn(context, settings);
        break;
    }
    case Kind::DistortRGB: {
        DistortRGBSettings settings{};
        controls.read(settings, distortRgbNumbers, time);
        settings.edge = controls.readEdges(time);
        renderDistortRGB(context, settings);
        break;
    }
    case Kind::WarpFishEye: {
        WarpFishEyeSettings settings{};
        controls.read(settings, fishEyeNumbers, time);
        settings.edge = controls.readEdges(time);
        renderWarpFishEye(context, settings);
        break;
    }
    case Kind::WarpWaves: {
        WarpWavesSettings settings{};
        controls.read(settings, wavesNumbers, time);
        settings.edge = controls.readEdges(time);
        renderWarpWaves(context, settings);
        break;
    }
    case Kind::Glare: {
        GlareSettings settings{};
        controls.read(settings, glareNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.blades = std::clamp(controls.readInt("blades", time), 2, 12);
        settings.glareOnly = controls.readInt("glareOnly", time) != 0;
        renderGlare(context, settings);
        break;
    }
    case Kind::Glint: {
        GlintSettings settings{};
        controls.read(settings, glintNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.rays = std::clamp(controls.readInt("rays", time), 2, 12);
        settings.glintOnly = controls.readInt("glintOnly", time) != 0;
        renderGlint(context, settings);
        break;
    }
    case Kind::Streaks: {
        StreaksSettings settings{};
        controls.read(settings, streaksNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.count = std::clamp(controls.readInt("count", time), 1, 8);
        settings.streaksOnly = controls.readInt("streaksOnly", time) != 0;
        renderStreaks(context, settings);
        break;
    }
    case Kind::HalfTone: {
        HalfToneSettings settings{};
        controls.read(settings, halfToneNumbers, time);
        controls.readColor("ink", time, settings.inkRed, settings.inkGreen, settings.inkBlue);
        controls.readColor("paper", time, settings.paperRed, settings.paperGreen, settings.paperBlue);
        settings.mode = std::clamp(controls.readInt("mode", time), 0, 2);
        settings.invert = controls.readInt("invert", time) != 0;
        renderHalfTone(context, settings);
        break;
    }
    case Kind::BleachBypass: {
        BleachBypassSettings settings{};
        controls.read(settings, bleachNumbers, time);
        renderBleachBypass(context, settings);
        break;
    }
    case Kind::Blur: {
        BlurSettings05 settings{};
        controls.read(settings, blur05Numbers, time);
        settings.edge = controls.readEdges(time);
        renderBlur(context, settings);
        break;
    }
    case Kind::BlurDirectional: {
        DirectionalBlurSettings settings{};
        controls.read(settings, directionalBlurNumbers, time);
        settings.samples = std::clamp(controls.readInt("samples", time), 4, 96);
        settings.edge = controls.readEdges(time);
        renderBlurDirectional(context, settings);
        break;
    }
    case Kind::Sharpen: {
        SharpenSettings settings{};
        controls.read(settings, sharpenNumbers, time);
        renderSharpen(context, settings);
        break;
    }
    case Kind::Vignette: {
        VignetteSettings settings{};
        controls.read(settings, vignetteNumbers, time);
        controls.readTint(settings, time);
        renderVignette(context, settings);
        break;
    }
    case Kind::Grain: {
        GrainSettings settings{};
        controls.read(settings, grainNumbers, time);
        settings.seed = controls.readInt("seed", time);
        renderGrain(context, settings);
        break;
    }
    case Kind::Halation: {
        HalationSettings settings{};
        controls.read(settings, halationNumbers, time);
        controls.readTint(settings, time);
        settings.halationOnly = controls.readInt("halationOnly", time) != 0;
        renderHalation(context, settings);
        break;
    }
    case Kind::GlowAura: {
        GlowAuraSettings settings{};
        controls.read(settings, glowAuraNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderGlowAura(context, settings);
        break;
    }
    case Kind::GlowDarks: {
        GlowDarksSettings settings{};
        controls.read(settings, glowDarksNumbers, time);
        controls.readTint(settings, time);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderGlowDarks(context, settings);
        break;
    }
    case Kind::GlowDist: {
        GlowDistSettings settings{};
        controls.read(settings, glowDistNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderGlowDist(context, settings);
        break;
    }
    case Kind::GlowEdges: {
        GlowEdgesSettings settings{};
        controls.read(settings, glowEdgesNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderGlowEdges(context, settings);
        break;
    }
    case Kind::GlowNoise: {
        GlowNoiseSettings settings{};
        controls.read(settings, glowNoiseNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.seed = controls.readInt("seed", time);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderGlowNoise(context, settings);
        break;
    }
    case Kind::GlowOrthicon: {
        GlowOrthiconSettings settings{};
        controls.read(settings, glowOrthiconNumbers, time);
        renderGlowOrthicon(context, settings);
        break;
    }
    case Kind::GlowRainbow: {
        GlowRainbowSettings settings{};
        controls.read(settings, glowRainbowNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderGlowRainbow(context, settings);
        break;
    }
    case Kind::GlowRings: {
        GlowRingsSettings settings{};
        controls.read(settings, glowRingsNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.rings = std::clamp(controls.readInt("rings", time), 1, 6);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderGlowRings(context, settings);
        break;
    }
    case Kind::UltraGlow: {
        UltraGlowSettings settings{};
        controls.read(settings, ultraGlowNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.glowOnly = controls.readInt("glowOnly", time) != 0;
        renderUltraGlow(context, settings);
        break;
    }
    case Kind::EdgeRays: {
        EdgeRaysSettings settings{};
        controls.read(settings, edgeRaysNumbers, time);
        controls.readTint(settings, time);
        settings.blend = std::clamp(controls.readInt("blend", time), 0, 1);
        settings.samples = std::clamp(controls.readInt("samples", time), 8, 96);
        settings.raysOnly = controls.readInt("raysOnly", time) != 0;
        renderEdgeRays(context, settings);
        break;
    }
    case Kind::Dissolve:
    case Kind::DissolveGlow:
    case Kind::DissolveShake:
    case Kind::SwishPan:
    case Kind::Shake:
        break;
    }
}

} // namespace diamond::fx
