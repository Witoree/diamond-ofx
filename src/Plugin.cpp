#include "EffectsHost.h"

#include <ofxCore.h>
#include <ofxImageEffect.h>
#include <ofxParam.h>
#include <ofxProperty.h>

#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <new>

namespace {

using diamond::fx::Kind;
using diamond::fx::StatusError;

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
    {"D_FilmEffect", "Apply a film print look with tone shaping, grain, halation and vignette.", true}
};

const EffectDescription& description(Kind kind)
{
    return descriptions[static_cast<unsigned int>(kind)];
}

constexpr const char* sourceName = kOfxImageEffectSimpleSourceClipName;
constexpr const char* outputName = kOfxImageEffectOutputClipName;

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
}

void describe(OfxImageEffectHandle effect, Kind kind)
{
    const auto properties = effectProperties(effect);
    require(props->propSetString(properties, kOfxPropLabel, 0, description(kind).label));
    require(props->propSetString(properties, kOfxImageEffectPluginPropGrouping, 0, "Diamond"));
    props->propSetString(properties, kOfxPropPluginDescription, 0, description(kind).description);
    require(props->propSetString(properties, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedContexts, 1, kOfxImageEffectContextGeneral));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort));
    require(props->propSetString(properties, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat));
    require(props->propSetInt(properties, kOfxImageEffectPropSupportsTiles, 0, 0));
    require(props->propSetInt(properties, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0));
    require(props->propSetInt(properties, kOfxImageEffectPropSupportsMultipleClipPARs, 0, 0));
    require(props->propSetString(properties, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderFullySafe));
    props->propSetInt(properties, kOfxImageEffectPluginPropHostFrameThreading, 0, 0);
    props->propSetInt(properties, kOfxImageEffectPluginPropFieldRenderTwiceAlways, 0, 1);
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

void describeInContext(OfxImageEffectHandle effect, Kind kind)
{
    describeClip(effect, sourceName);
    describeClip(effect, outputName);

    const auto set = parameters(effect);
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

OfxStatus identity(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs,
                   OfxPropertySetHandle outArgs, Kind kind)
{
    const auto time = actionTime(inArgs);
    if (kind == Kind::Shake) {
        const auto settings = settingsAt(effect, time);
        const bool axesDisabled = settings.xPercent == 0.0 && settings.yPercent == 0.0
            && settings.tiltDegrees == 0.0 && settings.zoomPercent == 0.0;
        if (settings.overscanPercent != 0.0 || (settings.amplitude != 0.0 && !axesDisabled)) {
            return kOfxStatReplyDefault;
        }
    } else if (!diamond::fx::controlsAreIdentity(kind, {props, params, parameters(effect)}, time)) {
        return kOfxStatReplyDefault;
    }
    require(props->propSetString(outArgs, kOfxPropName, 0, sourceName));
    require(props->propSetDouble(outArgs, kOfxPropTime, 0, time));
    return kOfxStatOK;
}

OfxStatus regionsOfInterest(OfxImageEffectHandle effect, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs)
{
    if (!effects->clipGetRegionOfDefinition) {
        return kOfxStatReplyDefault;
    }
    OfxRectD sourceRegion{};
    const auto status = effects->clipGetRegionOfDefinition(clip(effect, sourceName), actionTime(inArgs), &sourceRegion);
    if (status != kOfxStatOK) {
        return kOfxStatReplyDefault;
    }
    const double values[] = {sourceRegion.x1, sourceRegion.y1, sourceRegion.x2, sourceRegion.y2};
    return props->propSetDoubleN(outArgs, "OfxImageClipPropRoI_Source", 4, values);
}

class Image {
public:
    Image(OfxImageClipHandle input, OfxTime time, bool required)
    {
        const auto status = effects->clipGetImage(input, time, nullptr, &properties_);
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

struct PixelFormat {
    diamond::Depth depth;
    int components;
};

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

double positiveProperty(OfxPropertySetHandle properties, const char* name, int index, double fallback)
{
    double value = 0.0;
    if (properties && props->propGetDouble(properties, name, index, &value) == kOfxStatOK
        && std::isfinite(value) && value > 0.0) {
        return value;
    }
    return fallback;
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

    OfxPropertySetHandle outputClipProperties = nullptr;
    const Image output(clip(effect, outputName, &outputClipProperties), time, true);
    const auto outputProperties = output.properties();
    const auto destination = imageView(outputProperties);
    const auto format = pixelFormat(outputProperties);
    const auto frame = rectProperty(outputProperties, kOfxImagePropRegionOfDefinition, &destination.bounds);
    OfxPropertySetHandle sourceClipProperties = nullptr;
    const Image input(clip(effect, sourceName, &sourceClipProperties), time, false);
    diamond::ImageView source{};
    auto inputFormat = format;
    if (const auto inputProperties = input.properties()) {
        inputFormat = pixelFormat(inputProperties);
        if (kind == Kind::Shake
            && (inputFormat.depth != format.depth || inputFormat.components != format.components)) {
            return kOfxStatErrFormat;
        }
        const auto view = imageView(inputProperties);
        source = {view.data, view.rowBytes, view.bounds};
    }

    const double outputFps = positiveProperty(outputClipProperties, kOfxImageEffectPropFrameRate, 0, 30.0);
    const double fps = positiveProperty(effectProperties(effect), kOfxImageEffectPropFrameRate, 0, outputFps);

    const double scaleX = positiveProperty(inArgs, kOfxImageEffectPropRenderScale, 0, 1.0);
    const double scaleY = positiveProperty(inArgs, kOfxImageEffectPropRenderScale, 1, 1.0);
    const double par = positiveProperty(outputProperties, kOfxImagePropPixelAspectRatio, 0, 1.0);
    const auto field = stringProperty(outputProperties, kOfxImagePropField);
    const double fieldScale = is(field, kOfxImageFieldLower) || is(field, kOfxImageFieldUpper) ? 0.5 : 1.0;
    auto sourceAlpha = stringProperty(input.properties(), kOfxImageEffectPropPreMultiplication);
    if (!sourceAlpha) {
        sourceAlpha = stringProperty(sourceClipProperties, kOfxImageEffectPropPreMultiplication);
    }
    const bool straightAlpha = inputFormat.components == 4 && is(sourceAlpha, kOfxImageUnPreMultiplied);
    const double pixelAspect = par * scaleY * fieldScale / scaleX;

    if (kind == Kind::Shake) {
        diamond::renderShake(source, destination, frame, window, format.depth, format.components,
            time / fps, 1.0 / fps, pixelAspect, straightAlpha, settingsAt(effect, time), aborted, effect);
        return kOfxStatOK;
    }

    auto outputAlpha = stringProperty(outputProperties, kOfxImageEffectPropPreMultiplication);
    if (!outputAlpha) {
        outputAlpha = stringProperty(outputClipProperties, kOfxImageEffectPropPreMultiplication);
    }
    const bool outputStraight = format.components == 4
        && (outputAlpha ? is(outputAlpha, kOfxImageUnPreMultiplied) : straightAlpha);
    diamond::fx::renderEffect(kind, {props, params, parameters(effect)}, source,
        {inputFormat.depth, inputFormat.components, straightAlpha}, destination,
        {format.depth, format.components, outputStraight}, frame, window, pixelAspect,
        time, fps, aborted, effect);
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
            describeInContext(effect, kind);
        } else if (is(action, kOfxActionCreateInstance) || is(action, kOfxActionDestroyInstance)) {
            return kOfxStatOK;
        } else if (is(action, kOfxImageEffectActionGetClipPreferences)) {
            clipPreferences(effect, outArgs, kind);
        } else if (is(action, kOfxImageEffectActionIsIdentity)) {
            if constexpr (kind == Kind::Shake || kind >= Kind::UltraZap) {
                return identity(effect, inArgs, outArgs, kind);
            }
            return kOfxStatReplyDefault;
        } else if (is(action, kOfxImageEffectActionGetRegionsOfInterest)) {
            return regionsOfInterest(effect, inArgs, outArgs);
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
    {kOfxImageEffectPluginApi, 1, "com.diamondfx.dfilmeffect", 0, 3, setHost, mainEntry<Kind::FilmEffect>}
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
