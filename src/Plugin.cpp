#include "EffectsHost.h"
#include "Gpu05.h"
#include "PictureOFX07.h"

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <ofxProperty.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <new>

namespace {

using diamond::fx::Kind;
using diamond::fx::PixelFormat;
using diamond::fx::StatusError;
using diamond::fx::isTransition;

struct EffectDescription {
    const char* label;
    const char* description;
    bool frameVarying;
};

constexpr EffectDescription descriptions[] = {
    {"D_Shake", "Add continuous handheld camera movement to photos and video.", true},
    {"D_LensFlare", "Create a lens flare with colored optical ghosts, halos and streaks.", false},
    {"D_Glow", "Bloom from highlights with a soft threshold and two scales of diffusion.", false},
    {"D_BlurMoCurves", "Apply shutter blur based on animated translation, zoom and rotation.", true},
    {"D_DigitalDamage", "Simulate signal breakup, color separation and codec artifacts.", true},
    {"D_Rays", "Project soft rays outward from a chosen point using image highlights.", false},
    {"D_UltraZap", "Draw animated branched lightning with a luminous core and layered glow.", true},
    {"D_PixelSort", "Sort selected runs of pixels along an adjustable angle.", false},
    {"D_RackDefocus", "Defocus through an adjustable iris with bright optical bokeh.", false},
    {"D_LightLeak", "Add a drifting film light leak with selectable color and composition.", true},
    {"D_FilmEffect", "Apply a film print look with tone shaping, grain, halation and vignette.", true},
    {"D_VHSDamage", "Simulate analog tape tracking, chroma bleed, scanlines and dropouts.", true},
    {"D_FilmBurn", "Create an animated film burn with a soft exposure front and hot spots.", true},
    {"D_DistortRGB", "Separate and warp color channels using detail in the source image.", false},
    {"D_WarpFishEye", "Bend the image around a movable center with fisheye distortion.", false},
    {"D_WarpWaves", "Displace the image with animated directional waves.", true},
    {"D_Glare", "Spread bright regions into broad, diffracted star patterns.", false},
    {"D_Glint", "Draw crisp star points around small bright details.", false},
    {"D_Streaks", "Stretch bright regions into directional light streaks.", false},
    {"D_HalfTone", "Turn footage into a screened print with mono, RGB or CMYK dots.", false},
    {"D_BleachBypass", "Apply a silver-retention look with controlled color and contrast.", false},
    {"D_Blur", "Soften the image with a controllable blur radius.", false},
    {"D_BlurDirectional", "Smear detail along an adjustable direction and bias.", false},
    {"D_Sharpen", "Enhance detail with a threshold to protect smooth areas.", false},
    {"D_Vignette", "Shape and tint the edges of the frame.", false},
    {"D_Grain", "Add seeded film grain with luminance response and color control.", true},
    {"D_Halation", "Spread a tinted optical halo around bright details.", false},
    {"D_Dissolve", "A smooth cross dissolve between two clips.", false},
    {"D_DissolveGlow", "Blend two clips through a glowing highlight bloom.", false},
    {"D_DissolveShake", "Blend two clips with a deterministic camera shake.", false},
    {"D_SwishPan", "Transition between two clips with a directional whip pan.", false},
    {"D_GlowAura", "Wrap highlights in animated colored contours.", true},
    {"D_GlowDarks", "Diffuse shadow tones into a tinted smoky halo.", false},
    {"D_GlowDist", "Build a soft glow at a measured distance from bright edges.", false},
    {"D_GlowEdges", "Outline image edges with colored bloom.", false},
    {"D_GlowNoise", "Add animated grain and texture to highlight bloom.", true},
    {"D_GlowOrthicon", "Darken the neighborhood of highlights with an orthicon halo.", false},
    {"D_GlowRainbow", "Spread moving spectral bands around highlights.", true},
    {"D_GlowRings", "Draw concentric glow bands around bright details.", false},
    {"D_UltraGlow", "Layer a tight bloom with a broad halo and separate RGB widths.", false},
    {"D_EdgeRays", "Project rays from contrast edges and silhouettes.", false},
    {"D_GlintRainbow", "Create spectral star glints around bright points.", false},
    {"D_BokehLights", "Add drifting, defocused lights with an adjustable iris.", true},
    {"D_DropShadow", "Composite a soft tinted shadow behind source transparency.", false},
    {"D_Flashbulbs", "Scatter animated camera flashes across the image.", true},
    {"D_Light3D", "Relight a diffuse pass with a point light and optional RGB normals.", false},
    {"D_SpotLight", "Aim a soft 3D light cone at the source image.", false},
    {"D_Hotspots", "Extract or enhance highlights while preserving their colors.", false},
    {"D_FilmDamage", "Simulate scratched film, dust, hairs, flicker and gate weave.", true},
    {"D_GrainStatic", "Add independent monochrome or color pixel noise.", true},
    {"D_UltraGrain", "Model digital sensor grain with shot, read and fixed-pattern noise.", true},
    {"D_ImageInImage", "Place, rotate and animate a perspective-pinned picture over the canvas.", false}
};

const EffectDescription& description(Kind kind)
{
    return descriptions[static_cast<unsigned int>(kind)];
}

constexpr const char* sourceName = kOfxImageEffectSimpleSourceClipName;
constexpr const char* sourceFromName = kOfxImageEffectTransitionSourceFromClipName;
constexpr const char* sourceToName = kOfxImageEffectTransitionSourceToClipName;
constexpr const char* outputName = kOfxImageEffectOutputClipName;
constexpr const char* normalsName = "Normals";
// VEGAS also requests the region through the legacy Catalyst action.
constexpr const char* vegasGetRegion = "OfxCatalystImageEffectActionGetRegionOfDefinition";

std::atomic<OfxHost*> host{nullptr};
std::mutex suiteLoad;
const OfxPropertySuiteV1* props = nullptr;
const OfxImageEffectSuiteV1* effects = nullptr;
const OfxParameterSuiteV1* params = nullptr;

void require(OfxStatus status)
{
    if (status != kOfxStatOK) {
        throw StatusError{status};
    }
}

struct DoubleControl {
    const char* name;
    const char* label;
    const char* hint;
    double diamond::ShakeSettings::* member;
    double defaultValue;
    double maximum;
    double displayMaximum;
    double increment;
};

constexpr DoubleControl doubles[] = {
    {"amplitude", "Amplitude", "Multiplier for the shake movement.", &diamond::ShakeSettings::amplitude, 1.0, 10.0, 3.0, 0.05},
    {"frequency", "Frequency (Hz)", "Speed of the shake. Zero pauses the movement.", &diamond::ShakeSettings::frequency, 1.5, 30.0, 8.0, 0.05},
    {"xPercent", "Horizontal (%)", "Horizontal movement as a percentage of frame width.", &diamond::ShakeSettings::xPercent, 0.60, 25.0, 5.0, 0.05},
    {"yPercent", "Vertical (%)", "Vertical movement as a percentage of frame height.", &diamond::ShakeSettings::yPercent, 0.40, 25.0, 5.0, 0.05},
    {"tiltDegrees", "Rotation (degrees)", "Amount of rotational shake.", &diamond::ShakeSettings::tiltDegrees, 0.35, 30.0, 5.0, 0.05},
    {"zoomPercent", "Zoom variation (%)", "Amount of zoom variation in the shake.", &diamond::ShakeSettings::zoomPercent, 0.20, 25.0, 5.0, 0.05},
    {"overscanPercent", "Overscan (%)", "Fixed zoom to keep the image edges out of view.", &diamond::ShakeSettings::overscanPercent, 3.0, 50.0, 15.0, 0.1},
    {"shutterDegrees", "Shutter angle", "Exposure angle for motion blur. Zero disables motion blur.", &diamond::ShakeSettings::shutterDegrees, 0.0, 360.0, 360.0, 5.0},
};

struct IntControl {
    const char* name;
    const char* label;
    const char* hint;
    int diamond::ShakeSettings::* member;
    int defaultValue;
    int minimum;
    int maximum;
    int displayMaximum;
};

constexpr IntControl integers[] = {
    {"samples", "Blur samples", "Number of samples used when the shutter angle is positive.", &diamond::ShakeSettings::samples, 5, 1, 16, 16},
    {"seed", "Seed", "Change the seed to get another shake pattern.", &diamond::ShakeSettings::seed, 0, 0, 2147483647, 100},
};

bool is(const char* value, const char* expected)
{
    return value && std::strcmp(value, expected) == 0;
}

void setHost(OfxHost* newHost)
{
    host.store(newHost);
}

OfxStatus load()
{
    const std::lock_guard<std::mutex> lock(suiteLoad);
    if (props && effects && params) {
        return kOfxStatOK;
    }
    const auto* current = host.load();
    if (!current || !current->fetchSuite) {
        return kOfxStatErrMissingHostFeature;
    }
    const auto* propertySuite = static_cast<const OfxPropertySuiteV1*>(current->fetchSuite(current->host, kOfxPropertySuite, 1));
    const auto* effectSuite = static_cast<const OfxImageEffectSuiteV1*>(current->fetchSuite(current->host, kOfxImageEffectSuite, 1));
    const auto* paramSuite = static_cast<const OfxParameterSuiteV1*>(current->fetchSuite(current->host, kOfxParameterSuite, 1));
    if (!propertySuite || !effectSuite || !paramSuite) {
        return kOfxStatErrMissingHostFeature;
    }
    props = propertySuite;
    effects = effectSuite;
    params = paramSuite;
    return kOfxStatOK;
}

OfxPropertySetHandle effectProperties(OfxImageEffectHandle effect)
{
    OfxPropertySetHandle properties = nullptr;
    require(effects->getPropertySet(effect, &properties));
    return properties;
}

OfxParamSetHandle parameters(OfxImageEffectHandle effect)
{
    OfxParamSetHandle set = nullptr;
    require(effects->getParamSet(effect, &set));
    return set;
}

OfxImageClipHandle clip(OfxImageEffectHandle effect, const char* name, OfxPropertySetHandle* properties = nullptr)
{
    OfxImageClipHandle result = nullptr;
    require(effects->clipGetHandle(effect, name, &result, properties));
    return result;
}

const char* stringProperty(OfxPropertySetHandle properties, const char* name)
{
    char* value = nullptr;
    if (properties && props->propGetString(properties, name, 0, &value) == kOfxStatOK) {
        return value;
    }
    return nullptr;
}

OfxImageClipHandle normalClip(OfxImageEffectHandle effect, OfxPropertySetHandle& properties)
{
    OfxImageClipHandle result = nullptr;
    if (effects->clipGetHandle(effect, normalsName, &result, &properties) != kOfxStatOK) {
        return nullptr;
    }
    int connected = 1;
    if (properties && props->propGetInt(properties, kOfxImageClipPropConnected, 0, &connected) == kOfxStatOK
        && !connected) {
        return nullptr;
    }
    return result;
}

void clipPreferences(OfxImageEffectHandle effect, OfxPropertySetHandle outArgs, Kind kind)
{
    require(props->propSetInt(outArgs, kOfxImageEffectFrameVarying, 0, description(kind).frameVarying ? 1 : 0));

    OfxPropertySetHandle sourceProperties = nullptr;
    clip(effect, sourceName, &sourceProperties);
    const bool straightAlpha = is(stringProperty(sourceProperties, kOfxImageEffectPropPreMultiplication),
        kOfxImageUnPreMultiplied);
    // Opaque input can produce transparency at uncovered frame edges.
    props->propSetString(outArgs, kOfxImageEffectPropPreMultiplication, 0,
        straightAlpha ? kOfxImageUnPreMultiplied : kOfxImagePreMultiplied);
    if (kind == Kind::ImageInImage) {
        double projectPar = 0;
        if (props->propGetDouble(effectProperties(effect), kOfxImageEffectPropProjectPixelAspectRatio, 0,
                                 &projectPar) == kOfxStatOK && std::isfinite(projectPar) && projectPar > 0) {
            props->propSetDouble(outArgs, "OfxImageClipPropPAR_Output", 0, projectPar);
        }
    }

    // In General, compositing hosts can supply an RGBA output for isolated
    // light and transparent warp edges, even when the source is RGB.
    const bool canCreateAlpha = kind == Kind::FilmBurn || kind == Kind::Glare
        || kind == Kind::Glint || kind == Kind::Streaks || kind == Kind::DistortRGB
        || kind == Kind::WarpFishEye || kind == Kind::WarpWaves || kind == Kind::Blur
        || kind == Kind::BlurDirectional || kind == Kind::Halation
        || (kind >= Kind::GlowAura && kind <= Kind::EdgeRays && kind != Kind::GlowOrthicon)
        || kind == Kind::GlintRainbow || kind == Kind::BokehLights || kind == Kind::Flashbulbs
        || kind == Kind::DropShadow || kind == Kind::ImageInImage;
    const bool separateOutputAlpha = kind == Kind::ImageInImage || kind == Kind::DropShadow;
    if (canCreateAlpha && (separateOutputAlpha || is(
            stringProperty(effectProperties(effect), kOfxImageEffectPropContext), kOfxImageEffectContextGeneral))) {
        const auto* current = host.load();
        int count = 0;
        if (current && props->propGetDimension(current->host,
                kOfxImageEffectPropSupportedComponents, &count) == kOfxStatOK) {
            for (int index = 0; index < count; ++index) {
                char* component = nullptr;
                if (props->propGetString(current->host, kOfxImageEffectPropSupportedComponents,
                        index, &component) == kOfxStatOK && is(component, kOfxImageComponentRGBA)) {
                    props->propSetString(outArgs, "OfxImageClipPropComponents_Output", 0,
                                         kOfxImageComponentRGBA);
                    break;
                }
            }
        }
    }
}

void describe(OfxImageEffectHandle effect, Kind kind)
{
    const auto properties = effectProperties(effect);
    require(props->propSetString(properties, kOfxPropLabel, 0, description(kind).label));
    require(props->propSetString(properties, kOfxImageEffectPluginPropGrouping, 0, "Diamond"));
    props->propSetString(properties, kOfxPropPluginDescription, 0, description(kind).description);
    if (isTransition(kind)) {
        require(props->propSetString(properties, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextTransition));
    } else {
        require(props->propSetString(properties, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter));
        require(props->propSetString(properties, kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral));
    }
    require(props->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat));
    require(props->propSetInt(properties, kOfxImageEffectPropSupportsTiles, 0, 0));
    require(props->propSetInt(properties, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0));
    require(props->propSetInt(properties, kOfxImageEffectPropSupportsMultipleClipPARs, 0,
                             kind == Kind::ImageInImage ? 1 : 0));
    require(props->propSetString(properties, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderFullySafe));
    props->propSetInt(properties, kOfxImageEffectPluginPropHostFrameThreading, 0, 0);
    props->propSetInt(properties, kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 1);
    if (kind == Kind::ImageInImage) {
        props->propSetInt(properties, kOfxImageEffectPropSupportsMultiResolution, 0, 1);
        props->propSetString(properties, kOfxPropVersionLabel, 0, "0.07.1");
        diamond::fx::registerPictureOverlay(host.load(), effects, {props, params, nullptr}, properties);
    }
}

void describeClip(OfxImageEffectHandle effect, const char* name)
{
    OfxPropertySetHandle properties = nullptr;
    require(effects->clipDefine(effect, name, &properties));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedComponents, 1, kOfxImageComponentRGB));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedComponents, 2, kOfxImageComponentAlpha));
    require(props->propSetInt(properties, kOfxImageEffectPropSupportsTiles, 0, 0));
}

OfxPropertySetHandle defineControl(OfxParamSetHandle set, const char* type, const char* name,
                                 const char* label, const char* hint)
{
    OfxPropertySetHandle properties = nullptr;
    require(params->paramDefine(set, type, name, &properties));
    require(props->propSetString(properties, kOfxPropLabel, 0, label));
    props->propSetString(properties, kOfxParamPropHint, 0, hint);
    return properties;
}

void describeInContext(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, Kind kind)
{
    const auto set = parameters(effect);
    if (isTransition(kind)) {
        describeClip(effect, sourceFromName);
        describeClip(effect, sourceToName);
        // The host supplies this standard parameter. Its label and range belong to the host.
        OfxPropertySetHandle progress = nullptr;
        require(params->paramDefine(set, kOfxParamTypeDouble, kOfxImageEffectTransitionParamName, &progress));
    } else {
        describeClip(effect, sourceName);
        if (kind == Kind::Light3D && is(stringProperty(inArgs, kOfxImageEffectPropContext),
                                        kOfxImageEffectContextGeneral)) {
            OfxPropertySetHandle normals = nullptr;
            require(effects->clipDefine(effect, normalsName, &normals));
            require(props->propSetString(normals, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA));
            require(props->propSetString(normals, kOfxImageEffectPropSupportedComponents, 1, kOfxImageComponentRGB));
            require(props->propSetInt(normals, kOfxImageClipPropOptional, 0, 1));
            props->propSetInt(normals, kOfxImageEffectPropSupportsTiles, 0, 0);
        }
    }
    describeClip(effect, outputName);

    if (kind != Kind::Shake) {
        diamond::fx::describeControls(kind, {props, params, set});
        return;
    }
    for (const auto& control : doubles) {
        const auto properties = defineControl(set, kOfxParamTypeDouble, control.name, control.label, control.hint);
        require(props->propSetDouble(properties, kOfxParamPropDefault, 0, control.defaultValue));
        require(props->propSetDouble(properties, kOfxParamPropMin, 0, 0.0));
        require(props->propSetDouble(properties, kOfxParamPropMax, 0, control.maximum));
        props->propSetDouble(properties, kOfxParamPropDisplayMin, 0, 0.0);
        props->propSetDouble(properties, kOfxParamPropDisplayMax, 0, control.displayMaximum);
        props->propSetDouble(properties, kOfxParamPropIncrement, 0, control.increment);
        props->propSetInt(properties, kOfxParamPropDigits, 0, 2);
    }

    for (const auto& control : integers) {
        const auto properties = defineControl(set, kOfxParamTypeInteger, control.name, control.label, control.hint);
        require(props->propSetInt(properties, kOfxParamPropDefault, 0, control.defaultValue));
        require(props->propSetInt(properties, kOfxParamPropMin, 0, control.minimum));
        require(props->propSetInt(properties, kOfxParamPropMax, 0, control.maximum));
        props->propSetInt(properties, kOfxParamPropDisplayMin, 0, control.minimum);
        props->propSetInt(properties, kOfxParamPropDisplayMax, 0, control.displayMaximum);
    }

    const auto edge = defineControl(set, kOfxParamTypeChoice, "edge", "Edges", "Fill uncovered parts of the frame.");
    require(props->propSetString(edge, kOfxParamPropChoiceOption, 0, "Reflect"));
    require(props->propSetString(edge, kOfxParamPropChoiceOption, 1, "Clamp"));
    require(props->propSetString(edge, kOfxParamPropChoiceOption, 2, "Transparent"));
    require(props->propSetInt(edge, kOfxParamPropDefault, 0, 0));
}

template<class Value>
void readValue(OfxParamSetHandle set, const char* name, OfxTime time, Value& value)
{
    OfxParamHandle parameter = nullptr;
    require(params->paramGetHandle(set, name, &parameter, nullptr));
    require(params->paramGetValueAtTime(parameter, time, &value));
}

diamond::ShakeSettings settingsAt(OfxImageEffectHandle effect, OfxTime time)
{
    const auto set = parameters(effect);
    diamond::ShakeSettings settings{};
    for (const auto& control : doubles) {
        readValue(set, control.name, time, settings.*control.member);
    }
    for (const auto& control : integers) {
        readValue(set, control.name, time, settings.*control.member);
    }

    int edge = 0;
    readValue(set, "edge", time, edge);
    switch (edge) {
    case 1: settings.edge = diamond::Edge::Clamp; break;
    case 2: settings.edge = diamond::Edge::Transparent; break;
    default: settings.edge = diamond::Edge::Reflect; break;
    }
    return settings;
}

OfxTime actionTime(OfxPropertySetHandle inArgs)
{
    OfxTime time = 0.0;
    require(props->propGetDouble(inArgs, kOfxPropTime, 0, &time));
    return time;
}

double transitionProgress(OfxParamSetHandle set, OfxTime time)
{
    double progress = 0.0;
    readValue(set, kOfxImageEffectTransitionParamName, time, progress);
    return std::isfinite(progress) ? std::clamp(progress, 0.0, 1.0) : 0.0;
}

OfxStatus identity(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs,
                   OfxPropertySetHandle outArgs, Kind kind)
{
    const auto time = actionTime(inArgs);
    const char* identitySource = sourceName;
    if (isTransition(kind)) {
        const double progress = transitionProgress(parameters(effect), time);
        if (progress > 0.0 && progress < 1.0) {
            return kOfxStatReplyDefault;
        }
        identitySource = progress <= 0.0 ? sourceFromName : sourceToName;
    } else if (kind == Kind::Shake) {
        const auto settings = settingsAt(effect, time);
        const bool axesDisabled = settings.xPercent == 0.0 && settings.yPercent == 0.0
            && settings.tiltDegrees == 0.0 && settings.zoomPercent == 0.0;
        if (settings.overscanPercent != 0.0 || (settings.amplitude != 0.0 && !axesDisabled)) {
            return kOfxStatReplyDefault;
        }
    } else if (!diamond::fx::controlsAreIdentity(kind, {props, params, parameters(effect)}, time)) {
        return kOfxStatReplyDefault;
    }
    require(props->propSetString(outArgs, kOfxPropName, 0, identitySource));
    require(props->propSetDouble(outArgs, kOfxPropTime, 0, time));
    return kOfxStatOK;
}

OfxStatus setSourceRegion(OfxImageEffectHandle effect, OfxTime time, OfxPropertySetHandle outArgs,
                          const char* clipName, const char* propertyName)
{
    OfxRectD sourceRegion{};
    const auto status = effects->clipGetRegionOfDefinition(clip(effect, clipName), time, &sourceRegion);
    if (status != kOfxStatOK) {
        return kOfxStatReplyDefault;
    }
    const double values[] = {sourceRegion.x1, sourceRegion.y1, sourceRegion.x2, sourceRegion.y2};
    return props->propSetDoubleN(outArgs, propertyName, 4, values);
}

OfxStatus regionsOfInterest(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs,
                            Kind kind)
{
    if (!effects->clipGetRegionOfDefinition) {
        return kOfxStatReplyDefault;
    }
    const auto time = actionTime(inArgs);
    if (!isTransition(kind)) {
        const auto result = setSourceRegion(effect, time, outArgs, sourceName, "OfxImageClipPropRoI_Source");
        if (kind == Kind::Light3D && (result == kOfxStatOK || result == kOfxStatReplyDefault)) {
            OfxPropertySetHandle normalsProperties = nullptr;
            if (normalClip(effect, normalsProperties)) {
                const auto normalResult = setSourceRegion(effect, time, outArgs, normalsName,
                                                          "OfxImageClipPropRoI_Normals");
                return normalResult == kOfxStatReplyDefault ? result : normalResult;
            }
        }
        return result;
    }

    const auto from = setSourceRegion(effect, time, outArgs, sourceFromName, "OfxImageClipPropRoI_SourceFrom");
    if (from != kOfxStatOK && from != kOfxStatReplyDefault) {
        return from;
    }
    const auto to = setSourceRegion(effect, time, outArgs, sourceToName, "OfxImageClipPropRoI_SourceTo");
    return to == kOfxStatReplyDefault ? from : to;
}

class Image {
public:
    Image(OfxImageClipHandle input, OfxTime time, bool required, const OfxRectD* requested = nullptr)
    {
        if (!input && !required) {
            return;
        }
        auto status = effects->clipGetImage(input, time, requested, &properties_);
        if (requested && (status == kOfxStatFailed || status == kOfxStatErrUnsupported
                          || status == kOfxStatErrValue)) {
            // Some hosts only accept their default output request.
            status = effects->clipGetImage(input, time, nullptr, &properties_);
        }
        if (status == kOfxStatFailed && !required) {
            properties_ = nullptr;
            return;
        }
        require(status);
        if (!properties_ && required) {
            throw StatusError{kOfxStatFailed};
        }
    }

    ~Image()
    {
        if (properties_) {
            effects->clipReleaseImage(properties_);
        }
    }

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    OfxPropertySetHandle properties() const { return properties_; }

private:
    OfxPropertySetHandle properties_ = nullptr;
};

diamond::Rect rectProperty(OfxPropertySetHandle properties, const char* key, const diamond::Rect* fallback = nullptr)
{
    int values[4]{};
    const auto status = props->propGetIntN(properties, key, 4, values);
    if (status != kOfxStatOK && fallback) {
        return *fallback;
    }
    require(status);
    return {values[0], values[1], values[2], values[3]};
}

PixelFormat pixelFormat(OfxPropertySetHandle image)
{
    char* depthName = nullptr;
    char* componentName = nullptr;
    require(props->propGetString(image, kOfxImageEffectPropPixelDepth, 0, &depthName));
    require(props->propGetString(image, kOfxImageEffectPropComponents, 0, &componentName));

    PixelFormat format{};
    if (is(depthName, kOfxBitDepthByte)) {
        format.depth = diamond::Depth::Byte;
    } else if (is(depthName, kOfxBitDepthShort)) {
        format.depth = diamond::Depth::Short;
    } else if (is(depthName, kOfxBitDepthFloat)) {
        format.depth = diamond::Depth::Float;
    } else {
        throw StatusError{kOfxStatErrFormat};
    }

    if (is(componentName, kOfxImageComponentRGBA)) {
        format.components = 4;
    } else if (is(componentName, kOfxImageComponentRGB)) {
        format.components = 3;
    } else if (is(componentName, kOfxImageComponentAlpha)) {
        format.components = 1;
    } else {
        throw StatusError{kOfxStatErrFormat};
    }
    return format;
}

diamond::MutableImageView imageView(OfxPropertySetHandle image)
{
    void* data = nullptr;
    int rowBytes = 0;
    require(props->propGetPointer(image, kOfxImagePropData, 0, &data));
    require(props->propGetInt(image, kOfxImagePropRowBytes, 0, &rowBytes));
    return {data, rowBytes, rectProperty(image, kOfxImagePropBounds)};
}

const char* premultiplication(OfxPropertySetHandle image, OfxPropertySetHandle clipProperties)
{
    const auto value = stringProperty(image, kOfxImageEffectPropPreMultiplication);
    return value ? value : stringProperty(clipProperties, kOfxImageEffectPropPreMultiplication);
}

struct InputFrame {
    diamond::ImageView view;
    PixelFormat format;
};

InputFrame inputFrame(const Image& image, OfxPropertySetHandle clipProperties, PixelFormat outputFormat)
{
    InputFrame frame{{}, outputFormat};
    if (const auto imageProperties = image.properties()) {
        frame.format = pixelFormat(imageProperties);
        const auto view = imageView(imageProperties);
        frame.view = {view.data, view.rowBytes, view.bounds};
    }
    frame.format.straightAlpha = frame.format.components == 4
        && is(premultiplication(image.properties(), clipProperties), kOfxImageUnPreMultiplied);
    return frame;
}

double positiveProperty(OfxPropertySetHandle properties, const char* name, int index, double fallback)
{
    double value = 0.0;
    if (properties && props->propGetDouble(properties, name, index, &value) == kOfxStatOK
        && std::isfinite(value) && value > 0.0) {
        return value;
    }
    return fallback;
}

double imageFieldScale(OfxPropertySetHandle properties, double fallback = 1.0)
{
    const auto field = stringProperty(properties, kOfxImagePropField);
    if (!field) {
        return fallback;
    }
    return is(field, kOfxImageFieldLower) || is(field, kOfxImageFieldUpper) ? 0.5 : 1.0;
}

diamond::Rect pixelArea(OfxRectD area, double xFactor, double yFactor, diamond::Rect fallback)
{
    const double left = std::floor(area.x1 * xFactor + 1.0e-8);
    const double bottom = std::floor(area.y1 * yFactor + 1.0e-8);
    const double right = std::ceil(area.x2 * xFactor - 1.0e-8);
    const double top = std::ceil(area.y2 * yFactor - 1.0e-8);
    constexpr auto low = std::numeric_limits<int>::min();
    constexpr auto high = std::numeric_limits<int>::max();
    if (!(left >= low && bottom >= low && right <= high && top <= high
          && right > left && top > bottom && right - left <= high && top - bottom <= high)) {
        return fallback;
    }
    return {static_cast<int>(left), static_cast<int>(bottom),
            static_cast<int>(right), static_cast<int>(top)};
}

bool aborted(void* context)
{
    return effects->abort && effects->abort(static_cast<OfxImageEffectHandle>(context)) != 0;
}

OfxStatus render(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, Kind kind)
{
    if (aborted(effect)) {
        return kOfxStatOK;
    }
    const auto time = actionTime(inArgs);
    const auto window = rectProperty(inArgs, kOfxImageEffectPropRenderWindow);
    if (window.x1 >= window.x2 || window.y1 >= window.y2) {
        return kOfxStatOK;
    }

    OfxRectD projectArea{};
    const bool projectCanvas = kind == Kind::ImageInImage
        && diamond::fx::pictureProjectArea(effects, props, effect, projectArea);
    OfxPropertySetHandle outputClipProperties = nullptr;
    const Image output(clip(effect, outputName, &outputClipProperties), time, true,
                       projectCanvas ? &projectArea : nullptr);
    const auto outputProperties = output.properties();
    const auto destination = imageView(outputProperties);
    auto format = pixelFormat(outputProperties);
    const auto frame = rectProperty(outputProperties, kOfxImagePropRegionOfDefinition, &destination.bounds);
    const double outputFps = positiveProperty(outputClipProperties, kOfxImageEffectPropFrameRate, 0, 30.0);
    const double fps = positiveProperty(effectProperties(effect), kOfxImageEffectPropFrameRate, 0, outputFps);

    const double scaleX = positiveProperty(inArgs, kOfxImageEffectPropRenderScale, 0, 1.0);
    const double scaleY = positiveProperty(inArgs, kOfxImageEffectPropRenderScale, 1, 1.0);
    const double par = positiveProperty(outputProperties, kOfxImagePropPixelAspectRatio, 0, 1.0);
    const double fieldScale = imageFieldScale(outputProperties);
    const double pixelAspect = par * scaleY * fieldScale / scaleX;
    const auto outputAlpha = premultiplication(outputProperties, outputClipProperties);

    if (isTransition(kind)) {
        const auto set = parameters(effect);
        const double progress = transitionProgress(set, time);
        OfxPropertySetHandle fromProperties = nullptr;
        OfxPropertySetHandle toProperties = nullptr;
        const Image from(progress < 1.0 ? clip(effect, sourceFromName, &fromProperties) : nullptr, time, false);
        const Image to(progress > 0.0 ? clip(effect, sourceToName, &toProperties) : nullptr, time, false);
        const auto outgoing = inputFrame(from, fromProperties, format);
        const auto incoming = inputFrame(to, toProperties, format);
        const bool matchingStraight = (progress >= 1.0 || outgoing.format.straightAlpha)
            && (progress <= 0.0 || incoming.format.straightAlpha);
        format.straightAlpha = format.components == 4
            && (outputAlpha ? is(outputAlpha, kOfxImageUnPreMultiplied) : matchingStraight);
        const auto settings = diamond::fx::transitionSettingsAt(kind, {props, params, set}, time);
        diamond::fx::renderTransitionEffect(diamond::fx::transitionKind(kind), settings, progress, outgoing.view, outgoing.format,
            incoming.view, incoming.format, destination, format, frame, window, pixelAspect, time, fps, aborted, effect);
        return kOfxStatOK;
    }

    OfxPropertySetHandle sourceClipProperties = nullptr;
    const Image input(clip(effect, sourceName, &sourceClipProperties), time, false);
    const auto source = inputFrame(input, sourceClipProperties, format);
    format.straightAlpha = format.components == 4
        && (outputAlpha ? is(outputAlpha, kOfxImageUnPreMultiplied) : source.format.straightAlpha);
    if (kind == Kind::ImageInImage) {
        const auto inputProperties = input.properties();
        auto logicalSource = inputProperties
            ? rectProperty(inputProperties, kOfxImagePropRegionOfDefinition, &source.view.bounds)
            : source.view.bounds;
        if (logicalSource.x2 <= logicalSource.x1 || logicalSource.y2 <= logicalSource.y1) {
            logicalSource = source.view.bounds;
        }
        const double inputPar = positiveProperty(inputProperties, kOfxImagePropPixelAspectRatio, 0,
            positiveProperty(sourceClipProperties, kOfxImagePropPixelAspectRatio, 0, par));
        const double sourceField = imageFieldScale(inputProperties, fieldScale);
        const diamond::fx::PictureRenderSpace picture{logicalSource, inputPar * scaleY * sourceField / scaleX};
        const auto canvas = projectCanvas ? pixelArea(projectArea, scaleX / par, scaleY * fieldScale, frame) : frame;
        diamond::fx::renderEffect(kind, {props, params, parameters(effect)}, source.view, source.format,
            destination, format, canvas, window, pixelAspect, time, fps, aborted, effect, nullptr, {},
            {diamond::Depth::Float, 4, false}, &picture);
        return kOfxStatOK;
    }
    if (kind == Kind::Shake) {
        const auto shake = settingsAt(effect, time);
        if (source.format.depth == format.depth && source.format.components == format.components
            && source.format.straightAlpha == format.straightAlpha) {
            diamond::renderShake(source.view, destination, frame, window, format.depth, format.components,
                time / fps, 1.0 / fps, pixelAspect, format.straightAlpha, shake, aborted, effect);
        } else {
            // Some hosts supply RGB input to an RGBA output, or use a different
            // alpha convention on either side. Filter in premultiplied float.
            diamond::fx::renderEffect(kind, {}, source.view, source.format, destination, format,
                frame, window, pixelAspect, time, fps, aborted, effect, &shake);
        }
        return kOfxStatOK;
    }

    OfxPropertySetHandle normalsProperties = nullptr;
    const Image normals(kind == Kind::Light3D ? normalClip(effect, normalsProperties) : nullptr, time, false);
    const auto normalFrame = inputFrame(normals, normalsProperties, format);
    diamond::fx::renderEffect(kind, {props, params, parameters(effect)}, source.view, source.format,
        destination, format, frame, window, pixelAspect, time, fps, aborted, effect, nullptr,
        normalFrame.view, normalFrame.format);
    return kOfxStatOK;
}

template<Kind kind>
OfxStatus mainEntry(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                    OfxPropertySetHandle outArgs)
{
    try {
        if (is(action, kOfxActionLoad)) {
            return load();
        }
        if (is(action, kOfxActionUnload)) {
            if constexpr (kind == Kind::Rays || kind == Kind::BlurMoCurves || kind == Kind::EdgeRays) {
                diamond::fx::releaseGpu05();
            }
            // The same binary and suites may still be used by another Diamond effect.
            return kOfxStatOK;
        }
        if (!props || !effects || !params) {
            return kOfxStatErrMissingHostFeature;
        }

        const auto effect = static_cast<OfxImageEffectHandle>(const_cast<void*>(handle));
        if (is(action, kOfxActionDescribe)) {
            describe(effect, kind);
        } else if (is(action, kOfxImageEffectActionDescribeInContext)) {
            describeInContext(effect, inArgs, kind);
        } else if (is(action, kOfxActionCreateInstance) || is(action, kOfxActionDestroyInstance)) {
            return kOfxStatOK;
        } else if (is(action, kOfxImageEffectActionGetClipPreferences)) {
            if constexpr (isTransition(kind)) {
                return kOfxStatReplyDefault;
            }
            clipPreferences(effect, outArgs, kind);
        } else if (is(action, kOfxImageEffectActionIsIdentity)) {
            if constexpr (kind == Kind::Shake || kind >= Kind::UltraZap) {
                return identity(effect, inArgs, outArgs, kind);
            }
            return kOfxStatReplyDefault;
        } else if (is(action, kOfxImageEffectActionGetRegionsOfInterest)) {
            return regionsOfInterest(effect, inArgs, outArgs, kind);
        } else if (is(action, kOfxImageEffectActionGetRegionOfDefinition) || is(action, vegasGetRegion)) {
            if constexpr (kind == Kind::ImageInImage) {
                OfxRectD area{};
                if (diamond::fx::pictureProjectArea(effects, props, effect, area)) {
                    const double values[]{area.x1, area.y1, area.x2, area.y2};
                    return props->propSetDoubleN(outArgs, kOfxImageEffectPropRegionOfDefinition, 4, values);
                }
            }
            return kOfxStatReplyDefault;
        } else if (is(action, kOfxImageEffectActionRender)) {
            return render(effect, inArgs, kind);
        } else {
            return kOfxStatReplyDefault;
        }
        return kOfxStatOK;
    } catch (const StatusError& error) {
        return error.status;
    } catch (const std::bad_alloc&) {
        return kOfxStatErrMemory;
    } catch (...) {
        return kOfxStatFailed;
    }
}

OfxPlugin plugins[] = {
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dshake", 0, 1, setHost, mainEntry<Kind::Shake>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dlensflare", 0, 2, setHost, mainEntry<Kind::LensFlare>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglow", 0, 2, setHost, mainEntry<Kind::Glow>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dblurmocurves", 0, 2, setHost, mainEntry<Kind::BlurMoCurves>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.ddigitaldamage", 0, 2, setHost, mainEntry<Kind::DigitalDamage>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.drays", 0, 2, setHost, mainEntry<Kind::Rays>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dultrazap", 0, 3, setHost, mainEntry<Kind::UltraZap>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dpixelsort", 0, 3, setHost, mainEntry<Kind::PixelSort>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.drackdefocus", 0, 3, setHost, mainEntry<Kind::RackDefocus>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dlightleak", 0, 3, setHost, mainEntry<Kind::LightLeak>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dfilmeffect", 0, 3, setHost, mainEntry<Kind::FilmEffect>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dvhsdamage", 0, 4, setHost, mainEntry<Kind::VHSDamage>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dfilmburn", 0, 4, setHost, mainEntry<Kind::FilmBurn>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.ddistortrgb", 0, 4, setHost, mainEntry<Kind::DistortRGB>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dwarpfisheye", 0, 4, setHost, mainEntry<Kind::WarpFishEye>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dwarpwaves", 0, 4, setHost, mainEntry<Kind::WarpWaves>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglare", 0, 4, setHost, mainEntry<Kind::Glare>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglint", 0, 4, setHost, mainEntry<Kind::Glint>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dstreaks", 0, 4, setHost, mainEntry<Kind::Streaks>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dhalftone", 0, 4, setHost, mainEntry<Kind::HalfTone>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dbleachbypass", 0, 4, setHost, mainEntry<Kind::BleachBypass>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dblur", 0, 5, setHost, mainEntry<Kind::Blur>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dblurdirectional", 0, 5, setHost, mainEntry<Kind::BlurDirectional>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dsharpen", 0, 5, setHost, mainEntry<Kind::Sharpen>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dvignette", 0, 5, setHost, mainEntry<Kind::Vignette>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dgrain", 0, 5, setHost, mainEntry<Kind::Grain>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dhalation", 0, 5, setHost, mainEntry<Kind::Halation>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.ddissolve", 0, 5, setHost, mainEntry<Kind::Dissolve>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.ddissolveglow", 0, 5, setHost, mainEntry<Kind::DissolveGlow>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.ddissolveshake", 0, 5, setHost, mainEntry<Kind::DissolveShake>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dswishpan", 0, 5, setHost, mainEntry<Kind::SwishPan>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglowaura", 0, 6, setHost, mainEntry<Kind::GlowAura>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglowdarks", 0, 6, setHost, mainEntry<Kind::GlowDarks>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglowdist", 0, 6, setHost, mainEntry<Kind::GlowDist>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglowedges", 0, 6, setHost, mainEntry<Kind::GlowEdges>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglownoise", 0, 6, setHost, mainEntry<Kind::GlowNoise>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dgloworthicon", 0, 6, setHost, mainEntry<Kind::GlowOrthicon>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglowrainbow", 0, 6, setHost, mainEntry<Kind::GlowRainbow>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglowrings", 0, 6, setHost, mainEntry<Kind::GlowRings>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dultraglow", 0, 6, setHost, mainEntry<Kind::UltraGlow>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dedgerays", 0, 6, setHost, mainEntry<Kind::EdgeRays>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dglintrainbow", 0, 7, setHost, mainEntry<Kind::GlintRainbow>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dbokehlights", 0, 7, setHost, mainEntry<Kind::BokehLights>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.ddropshadow", 0, 7, setHost, mainEntry<Kind::DropShadow>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dflashbulbs", 0, 7, setHost, mainEntry<Kind::Flashbulbs>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dlight3d", 0, 7, setHost, mainEntry<Kind::Light3D>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dspotlight", 0, 7, setHost, mainEntry<Kind::SpotLight>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dhotspots", 0, 7, setHost, mainEntry<Kind::Hotspots>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dfilmdamage", 0, 7, setHost, mainEntry<Kind::FilmDamage>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dgrainstatic", 0, 7, setHost, mainEntry<Kind::GrainStatic>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dultragrain", 0, 7, setHost, mainEntry<Kind::UltraGrain>},
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dimageinimage", 0, 7, setHost, mainEntry<Kind::ImageInImage>}
};

constexpr int pluginCount = static_cast<int>(sizeof(plugins) / sizeof(plugins[0]));
static_assert(sizeof(descriptions) / sizeof(descriptions[0]) == pluginCount);

}  // namespace

OfxExport int OfxGetNumberOfPlugins(void)
{
    return pluginCount;
}

OfxExport OfxPlugin* OfxGetPlugin(int index)
{
    return index >= 0 && index < pluginCount ? &plugins[index] : nullptr;
}
