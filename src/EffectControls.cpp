#include "EffectsHost.h"

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
        const auto prop = define(kOfxParamTypeRGB, "tint", "Tint");
        const double color[] = {red, green, blue};
        require(params_.properties->propSetDoubleN(prop, kOfxParamPropDefault, 3, color));
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
        require(params_.suite->paramGetValueAtTime(get("tint"), time, &settings.red,
                                                   &settings.green, &settings.blue));
        const auto component = [](double value) {
            return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 1.0;
        };
        settings.red = component(settings.red);
        settings.green = component(settings.green);
        settings.blue = component(settings.blue);
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
    case Kind::Shake:
        break;
    }
}

bool controlsAreIdentity(Kind kind, Parameters parameters, OfxTime time)
{
    const Controls controls(parameters);
    switch (kind) {
    case Kind::UltraZap:
        return controls.readDouble("brightness", time) <= 0 && controls.readInt("boltOnly", time) == 0;
    case Kind::LightLeak:
        return controls.readDouble("brightness", time) <= 0 && controls.readInt("leakOnly", time) == 0;
    case Kind::RackDefocus:
        return controls.readDouble("radius", time) <= 0 || controls.readDouble("mix", time) <= 0;
    case Kind::PixelSort:
    case Kind::FilmEffect:
        return controls.readDouble("mix", time) <= 0;
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
    case Kind::Shake:
        break;
    }
}

} // namespace diamond::fx
