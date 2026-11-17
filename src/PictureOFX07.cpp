#include "PictureOFX07.h"

#include <ofxInteract.h>
#include <windows.h>
#include <GL/gl.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

namespace diamond::fx {
namespace {

constexpr char locationName[] = "location";
constexpr char perspectiveName[] = "perspective";
constexpr char keyframeName[] = "keyframeHandles";
constexpr char overlayName[] = "showHandles";

struct Number {
    const char* name;
    const char* label;
    double PictureSettings::*member;
    double initial, low, high, increment;
};

constexpr Number numbers[] = {
    {"scale", "Scale", &PictureSettings::scale, 0.5, 0.005, 8, 0.005},
    {"rotation", "Rotation (degrees)", &PictureSettings::rotation, 0, -36000, 36000, 1},
    {"opacity", "Opacity", &PictureSettings::opacity, 1, 0, 1, 0.01},
    {"feather", "Edge feather (% of height)", &PictureSettings::feather, 0, 0, 20, 0.05}
};

constexpr const char* pictureCornerLabels[]{"Top left", "Top right", "Bottom right", "Bottom left"};

void require(OfxStatus status) {
    if (status != kOfxStatOK) {
        throw StatusError{status};
    }
}

OfxPropertySetHandle define(Parameters parameters, const char* type, const char* name, const char* label) {
    OfxPropertySetHandle property = nullptr;
    require(parameters.suite->paramDefine(parameters.set, type, name, &property));
    require(parameters.properties->propSetString(property, kOfxPropLabel, 0, label));
    return property;
}

void definePoint(Parameters parameters, const char* name, const char* label, PicturePoint value,
                 double limit, const char* hint) {
    const auto prop = define(parameters, kOfxParamTypeDouble2D, name, label);
    const auto* p = parameters.properties;
    const double initial[]{value.x, value.y};
    const double low[]{-limit, -limit};
    const double high[]{1 + limit, 1 + limit};
    const double displayLow[]{-1, -1};
    const double displayHigh[]{2, 2};
    require(p->propSetDoubleN(prop, kOfxParamPropDefault, 2, initial));
    require(p->propSetDoubleN(prop, kOfxParamPropMin, 2, low));
    require(p->propSetDoubleN(prop, kOfxParamPropMax, 2, high));
    p->propSetDoubleN(prop, kOfxParamPropDisplayMin, 2, displayLow);
    p->propSetDoubleN(prop, kOfxParamPropDisplayMax, 2, displayHigh);
    p->propSetInt(prop, kOfxParamPropDigits, 0, 3);
    p->propSetDouble(prop, kOfxParamPropIncrement, 0, 0.005);
    p->propSetInt(prop, kOfxParamPropAnimates, 0, 1);
    p->propSetInt(prop, kOfxParamPropPluginMayWrite, 0, 1);
    p->propSetString(prop, kOfxParamPropHint, 0, hint);
}

void defineToggle(Parameters parameters, const char* name, const char* label, bool initial, bool animate,
                  const char* hint) {
    const auto property = define(parameters, kOfxParamTypeBoolean, name, label);
    require(parameters.properties->propSetInt(property, kOfxParamPropDefault, 0, initial ? 1 : 0));
    parameters.properties->propSetInt(property, kOfxParamPropAnimates, 0, animate ? 1 : 0);
    parameters.properties->propSetInt(property, kOfxParamPropEvaluateOnChange, 0, animate ? 1 : 0);
    parameters.properties->propSetString(property, kOfxParamPropHint, 0, hint);
}

OfxParamHandle param(Parameters parameters, const char* name) {
    OfxParamHandle handle = nullptr;
    require(parameters.suite->paramGetHandle(parameters.set, name, &handle, nullptr));
    return handle;
}

double limit(double value, double low, double high, double fallback) {
    return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
}

PicturePoint readPoint(Parameters parameters, const char* name, OfxTime time, PicturePoint initial,
                       double bounds) {
    PicturePoint point{};
    require(parameters.suite->paramGetValueAtTime(param(parameters, name), time, &point.x, &point.y));
    return {limit(point.x, -bounds, 1 + bounds, initial.x), limit(point.y, -bounds, 1 + bounds, initial.y)};
}

int readInt(Parameters parameters, const char* name, OfxTime time) {
    int result = 0;
    require(parameters.suite->paramGetValueAtTime(param(parameters, name), time, &result));
    return result;
}

struct Services {
    const OfxImageEffectSuiteV1* effects;
    const OfxPropertySuiteV1* properties;
    const OfxParameterSuiteV1* params;
};

std::atomic<const OfxInteractSuiteV1*> interactSuite{nullptr};
std::once_flag overlayInit;
Services overlayServices{};

enum class DragMode { None, Move, Scale, Rotate, Corner };

struct Drag {
    DragMode mode = DragMode::None;
    int corner = -1;
    PicturePoint start{};
    PictureSettings settings{};
    PictureQuad quad{};
    bool edit = false;
};

struct Canvas {
    OfxRectD frame{};
    double width = 0, height = 0, aspect = 1, sourceAspect = 1;
    double pixelX = 1, pixelY = 1;

    PicturePoint absolute(PicturePoint p) const { return {frame.x1 + p.x*width, frame.y1 + p.y*height}; }
    PicturePoint relative(PicturePoint p) const { return {(p.x-frame.x1)/width, (p.y-frame.y1)/height}; }
    double distance(PicturePoint a, PicturePoint b) const {
        return std::hypot((a.x-b.x)*width/pixelX, (a.y-b.y)*height/pixelY);
    }
};

bool getSourceArea(Services services, OfxImageEffectHandle effect, OfxTime time, OfxRectD& bounds) {
    OfxImageClipHandle clip = nullptr;
    return services.effects->clipGetRegionOfDefinition
        && services.effects->clipGetHandle(effect, kOfxImageEffectSimpleSourceClipName, &clip, nullptr) == kOfxStatOK
        && services.effects->clipGetRegionOfDefinition(clip, time, &bounds) == kOfxStatOK
        && std::isfinite(bounds.x2-bounds.x1) && std::isfinite(bounds.y2-bounds.y1)
        && bounds.x2 > bounds.x1 && bounds.y2 > bounds.y1;
}

bool getCanvas(Services services, OfxImageEffectHandle effect, OfxPropertySetHandle action, OfxTime time,
               Canvas& canvas) {
    OfxRectD sourceBounds{};
    const bool hasSource = getSourceArea(services, effect, time, sourceBounds);
    if (!pictureProjectArea(services.effects, services.properties, effect, canvas.frame)) {
        if (!hasSource) {
            return false;
        }
        canvas.frame = sourceBounds;
    }
    canvas.width = canvas.frame.x2 - canvas.frame.x1;
    canvas.height = canvas.frame.y2 - canvas.frame.y1;
    canvas.aspect = canvas.width/canvas.height;
    canvas.sourceAspect = hasSource ? (sourceBounds.x2-sourceBounds.x1)/(sourceBounds.y2-sourceBounds.y1)
                                    : canvas.aspect;
    double pixelScale[]{canvas.width/1000, canvas.height/1000};
    services.properties->propGetDoubleN(action, kOfxInteractPropPixelScale, 2, pixelScale);
    canvas.pixelX = limit(pixelScale[0], 1.0e-6, 1.0e6, 1);
    canvas.pixelY = limit(pixelScale[1], 1.0e-6, 1.0e6, 1);
    return true;
}

PicturePoint rotationHandle(const Canvas& canvas, const PictureQuad& quad, PicturePoint center) {
    const PicturePoint top{0.5*(quad[0].x + quad[1].x), 0.5*(quad[0].y + quad[1].y)};
    const double span = canvas.distance(top, center);
    const double offset = 24.0 / std::max(span, 1.0);
    return {top.x + (top.x-center.x)*offset, top.y + (top.y-center.y)*offset};
}

void vertex(const Canvas& canvas, PicturePoint point) {
    const auto p = canvas.absolute(point);
    glVertex2d(p.x, p.y);
}

void ring(const Canvas& canvas, PicturePoint point, bool diamond, double radius) {
    constexpr double tau = 6.283185307179586;
    const int count = diamond ? 4 : 18;
    glBegin(GL_LINE_LOOP);
    for (int i = 0; i < count; ++i) {
        const double angle = tau*i/count;
        vertex(canvas, {point.x + std::cos(angle)*radius*canvas.pixelX/canvas.width,
                        point.y + std::sin(angle)*radius*canvas.pixelY/canvas.height});
    }
    glEnd();
}

void draw(const Canvas& canvas, const PictureSettings& settings, const PictureQuad& quad,
          const Drag& drag) {
    if (!wglGetCurrentContext()) {
        return;
    }
    glPushAttrib(GL_ENABLE_BIT | GL_LINE_BIT | GL_CURRENT_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_LIGHTING);
    glLineWidth(1.5f);
    if (validPictureQuad(quad)) {
        glColor3f(0.22f, 0.86f, 0.94f);
    } else {
        glColor3f(1.0f, 0.45f, 0.35f);
    }
    glBegin(GL_LINE_LOOP);
    for (auto point : quad) {
        vertex(canvas, point);
    }
    glEnd();
    for (int i = 0; i < 4; ++i) {
        if (drag.corner == i) {
            glColor3f(1, 1, 1);
        } else {
            glColor3f(0.22f, 0.86f, 0.94f);
        }
        ring(canvas, quad[i], settings.perspective, 6);
    }
    glColor3f(1.0f, 0.83f, 0.42f);
    ring(canvas, rotationHandle(canvas, quad, settings.center), false, 5);
    ring(canvas, settings.center, false, 4);
    glPopAttrib();
}

bool inside(const PictureQuad& quad, PicturePoint point) {
    double sign = 0;
    for (std::size_t i = 0; i < quad.size(); ++i) {
        const auto a = quad[i];
        const auto b = quad[(i+1)%quad.size()];
        const auto cross = (b.x-a.x)*(point.y-a.y) - (b.y-a.y)*(point.x-a.x);
        if (cross*sign < 0) {
            return false;
        }
        if (cross != 0) {
            sign = cross;
        }
    }
    return true;
}

template<typename... Values>
void write(Parameters parameters, OfxParamHandle handle, OfxTime time, bool keys, Values... values) {
    const auto suite = parameters.suite;
    unsigned int count = 0;
    if (suite->paramGetNumKeys) {
        suite->paramGetNumKeys(handle, &count);
    }
    if (keys || count > 0) {
        if (!suite->paramSetValueAtTime) {
            throw StatusError{kOfxStatErrUnsupported};
        }
        require(suite->paramSetValueAtTime(handle, time, values...));
    } else if (suite->paramSetValue) {
        require(suite->paramSetValue(handle, values...));
    }
}

void dragAt(const Canvas& canvas, const Drag& drag, Parameters parameters, OfxTime time,
            PicturePoint position) {
    const bool keys = readInt(parameters, keyframeName, time) != 0;
    const auto& initial = drag.settings;
    const PicturePoint delta{position.x-drag.start.x, position.y-drag.start.y};
    if (drag.mode == DragMode::Corner) {
        auto proposed = initial;
        const auto corner = drag.quad[drag.corner];
        const auto local = pictureLocalPoint(initial, canvas.aspect, canvas.sourceAspect,
                                             {corner.x + delta.x, corner.y + delta.y});
        proposed.corners[drag.corner] = {limit(local.x, -2, 3, initial.corners[drag.corner].x),
                                         limit(local.y, -2, 3, initial.corners[drag.corner].y)};
        if (validPictureQuad(pictureQuad(proposed, canvas.aspect, canvas.sourceAspect))) {
            const auto point = proposed.corners[drag.corner];
            write(parameters, param(parameters, pictureCornerNames[drag.corner]), time, keys, point.x, point.y);
        }
    } else if (drag.mode == DragMode::Move) {
        write(parameters, param(parameters, locationName), time, keys,
              std::clamp(initial.center.x + delta.x, -4.0, 5.0),
              std::clamp(initial.center.y + delta.y, -4.0, 5.0));
    } else if (drag.mode == DragMode::Rotate) {
        const auto angle = [&](PicturePoint p) {
            return std::atan2(p.y-initial.center.y, (p.x-initial.center.x)*canvas.aspect);
        };
        constexpr double degrees = 57.29577951308232;
        const double rotation = initial.rotation + std::remainder((angle(position)-angle(drag.start))*degrees, 360.0);
        write(parameters, param(parameters, "rotation"), time, keys, std::clamp(rotation, -36000.0, 36000.0));
    } else if (drag.mode == DragMode::Scale) {
        const auto anchor = drag.quad[(drag.corner+2)%4];
        const auto corner = drag.quad[drag.corner];
        const double dx = (corner.x-anchor.x)*canvas.aspect;
        const double dy = corner.y-anchor.y;
        const double factor = 1 + (delta.x*canvas.aspect*dx + delta.y*dy)/(dx*dx+dy*dy);
        const double scale = std::clamp(initial.scale*factor, 0.005, 8.0);
        const double ratio = scale/initial.scale;
        const auto scaleParam = param(parameters, "scale");
        const auto locationParam = param(parameters, locationName);
        write(parameters, scaleParam, time, keys, scale);
        write(parameters, locationParam, time, keys,
              std::clamp(anchor.x + (initial.center.x-anchor.x)*ratio, -4.0, 5.0),
              std::clamp(anchor.y + (initial.center.y-anchor.y)*ratio, -4.0, 5.0));
    }
}

void finishEdit(Drag& drag, Parameters parameters) {
    if (drag.edit && parameters.suite->paramEditEnd) {
        parameters.suite->paramEditEnd(parameters.set);
    }
    drag = {};
}

OfxStatus pictureInteract(const char* action, const void* handle, OfxPropertySetHandle inArgs,
                          OfxPropertySetHandle) {
    try {
        if (std::strcmp(action, kOfxActionDescribe) == 0) {
            return kOfxStatOK;
        }
        const auto* interact = interactSuite.load();
        if (!interact || !handle) {
            return kOfxStatReplyDefault;
        }
        const Services services = overlayServices;
        const auto overlay = static_cast<OfxInteractHandle>(const_cast<void*>(handle));
        OfxPropertySetHandle properties = nullptr;
        require(interact->interactGetPropertySet(overlay, &properties));
        void* data = nullptr;
        if (std::strcmp(action, kOfxActionCreateInstance) == 0) {
            auto state = std::make_unique<Drag>();
            require(services.properties->propSetPointer(properties, kOfxPropInstanceData, 0, state.get()));
            state.release();
            const char* slave[]{locationName, "scale", "rotation", perspectiveName, overlayName};
            int i = 0;
            for (auto* name : slave) {
                services.properties->propSetString(properties, kOfxInteractPropSlaveToParam, i++, name);
            }
            for (auto* name : pictureCornerNames) {
                services.properties->propSetString(properties, kOfxInteractPropSlaveToParam, i++, name);
            }
            return kOfxStatOK;
        }
        services.properties->propGetPointer(properties, kOfxPropInstanceData, 0, &data);
        auto* drag = static_cast<Drag*>(data);
        if (!drag) {
            return kOfxStatReplyDefault;
        }
        void* effectHandle = nullptr;
        if (!inArgs || services.properties->propGetPointer(inArgs, kOfxPropEffectInstance, 0, &effectHandle) != kOfxStatOK) {
            services.properties->propGetPointer(properties, kOfxPropEffectInstance, 0, &effectHandle);
        }
        const auto effect = static_cast<OfxImageEffectHandle>(effectHandle);
        Parameters parameters{services.properties, services.params, nullptr};
        if (effect) {
            services.effects->getParamSet(effect, &parameters.set);
        }
        if (std::strcmp(action, kOfxActionDestroyInstance) == 0) {
            std::unique_ptr<Drag> owner(drag);
            if (parameters.set) {
                finishEdit(*drag, parameters);
            }
            return kOfxStatOK;
        }
        if (!parameters.set || !inArgs) {
            return kOfxStatReplyDefault;
        }
        if (std::strcmp(action, kOfxInteractActionPenUp) == 0
            || std::strcmp(action, kOfxInteractActionLoseFocus) == 0) {
            const bool wasDragging = drag->mode != DragMode::None;
            finishEdit(*drag, parameters);
            if (wasDragging && interact->interactRedraw) {
                interact->interactRedraw(overlay);
            }
            return wasDragging ? kOfxStatOK : kOfxStatReplyDefault;
        }
        const bool drawing = std::strcmp(action, kOfxInteractActionDraw) == 0;
        const bool down = std::strcmp(action, kOfxInteractActionPenDown) == 0;
        const bool motion = std::strcmp(action, kOfxInteractActionPenMotion) == 0;
        if (!drawing && !down && (!motion || drag->mode == DragMode::None)) {
            return kOfxStatReplyDefault;
        }
        OfxTime time = 0;
        require(services.properties->propGetDouble(inArgs, kOfxPropTime, 0, &time));
        if (readInt(parameters, overlayName, time) == 0) {
            return kOfxStatReplyDefault;
        }
        Canvas canvas;
        if (!getCanvas(services, effect, inArgs, time, canvas)) {
            return kOfxStatReplyDefault;
        }
        const auto settings = pictureSettingsAt(parameters, time);
        const auto quad = pictureQuad(settings, canvas.aspect, canvas.sourceAspect);
        if (drawing) {
            draw(canvas, settings, quad, *drag);
            return kOfxStatOK;
        }
        double pen[2]{};
        require(services.properties->propGetDoubleN(inArgs, kOfxInteractPropPenPosition, 2, pen));
        const auto position = canvas.relative({pen[0], pen[1]});
        if (down) {
            finishEdit(*drag, parameters);
            double closest = 11;
            for (int i = 0; i < 4; ++i) {
                const auto distance = canvas.distance(position, quad[i]);
                if (distance < closest) {
                    closest = distance;
                    drag->corner = i;
                }
            }
            if (drag->corner >= 0) {
                drag->mode = settings.perspective ? DragMode::Corner : DragMode::Scale;
            } else if (canvas.distance(position, rotationHandle(canvas, quad, settings.center)) < 10) {
                drag->mode = DragMode::Rotate;
            } else if (inside(quad, position) || canvas.distance(position, settings.center) < 9) {
                drag->mode = DragMode::Move;
            } else {
                return kOfxStatReplyDefault;
            }
            drag->settings = settings;
            drag->quad = quad;
            drag->start = position;
            drag->edit = parameters.suite->paramEditBegin
                && parameters.suite->paramEditBegin(parameters.set, "Diamond Picture Transform") == kOfxStatOK;
        } else {
            dragAt(canvas, *drag, parameters, time, position);
        }
        if (interact->interactRedraw) {
            interact->interactRedraw(overlay);
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

} // namespace

void describePictureControls(Parameters parameters) {
    definePoint(parameters, locationName, "Location", {0.5, 0.5}, 4,
                "Position on the canvas. (0, 0) is the lower-left corner.");
    for (const auto& number : numbers) {
        const auto property = define(parameters, kOfxParamTypeDouble, number.name, number.label);
        const auto* p = parameters.properties;
        require(p->propSetDouble(property, kOfxParamPropDefault, 0, number.initial));
        require(p->propSetDouble(property, kOfxParamPropMin, 0, number.low));
        require(p->propSetDouble(property, kOfxParamPropMax, 0, number.high));
        p->propSetDouble(property, kOfxParamPropDisplayMin, 0, std::max(-360.0, number.low));
        p->propSetDouble(property, kOfxParamPropDisplayMax, 0, std::min(360.0, number.high));
        p->propSetDouble(property, kOfxParamPropIncrement, 0, number.increment);
        p->propSetInt(property, kOfxParamPropDigits, 0, 3);
        p->propSetInt(property, kOfxParamPropPluginMayWrite, 0, 1);
    }
    defineToggle(parameters, perspectiveName, "Perspective", false, true,
                 "Move each corner independently. The four corner points are animatable.");
    for (std::size_t i = 0; i < pictureCornerNames.size(); ++i) {
        definePoint(parameters, pictureCornerNames[i], pictureCornerLabels[i], pictureDefaultCorners[i], 2,
                    "Corner in picture-local coordinates. Animate this point on the effect timeline.");
    }
    defineToggle(parameters, keyframeName, "Keyframe handles", false, false,
                 "Dragging a preview handle writes a keyframe at the current time.");
    defineToggle(parameters, overlayName, "Show preview handles", true, false,
                 "Show transform handles in hosts that support OpenFX overlays.");
    const auto quality = define(parameters, kOfxParamTypeChoice, "quality", "Sampling");
    require(parameters.properties->propSetString(quality, kOfxParamPropChoiceOption, 0, "Bilinear"));
    require(parameters.properties->propSetString(quality, kOfxParamPropChoiceOption, 1, "Adaptive"));
    require(parameters.properties->propSetInt(quality, kOfxParamPropDefault, 0, 1));
}

PictureSettings pictureSettingsAt(Parameters parameters, OfxTime time) {
    PictureSettings settings;
    settings.center = readPoint(parameters, locationName, time, settings.center, 4);
    for (const auto& number : numbers) {
        double value = number.initial;
        require(parameters.suite->paramGetValueAtTime(param(parameters, number.name), time, &value));
        settings.*number.member = limit(value, number.low, number.high, number.initial);
    }
    settings.perspective = readInt(parameters, perspectiveName, time) != 0;
    if (settings.perspective) {
        for (std::size_t i = 0; i < pictureCornerNames.size(); ++i) {
            settings.corners[i] = readPoint(parameters, pictureCornerNames[i], time, pictureDefaultCorners[i], 2);
        }
    }
    settings.quality = readInt(parameters, "quality", time);
    return settings;
}

bool pictureProjectArea(const OfxImageEffectSuiteV1* effects, const OfxPropertySuiteV1* props,
                        OfxImageEffectHandle effect, OfxRectD& area) {
    OfxPropertySetHandle properties = nullptr;
    double extent[2]{};
    if (effects->getPropertySet(effect, &properties) == kOfxStatOK && properties
        && props->propGetDoubleN(properties, kOfxImageEffectPropProjectExtent, 2, extent) == kOfxStatOK
        && std::isfinite(extent[0]) && std::isfinite(extent[1]) && extent[0] > 0 && extent[1] > 0) {
        area = {0, 0, extent[0], extent[1]};
        return true;
    }
    return false;
}

void registerPictureOverlay(const OfxHost* host, const OfxImageEffectSuiteV1* effects, Parameters parameters,
                            OfxPropertySetHandle descriptor) {
    int supported = 0;
    if (!host || !host->fetchSuite || parameters.properties->propGetInt(
        host->host, kOfxImageEffectPropSupportsOverlays, 0, &supported) != kOfxStatOK || !supported) {
        return;
    }
    const auto* interact = static_cast<const OfxInteractSuiteV1*>(host->fetchSuite(host->host, kOfxInteractSuite, 1));
    if (interact && interact->interactGetPropertySet) {
        std::call_once(overlayInit, [&] {
            overlayServices = {effects, parameters.properties, parameters.suite};
            interactSuite.store(interact);
        });
        parameters.properties->propSetPointer(descriptor, kOfxImageEffectPluginPropOverlayInteractV1, 0,
                                              reinterpret_cast<void*>(&pictureInteract));
    }
}

} // namespace diamond::fx
