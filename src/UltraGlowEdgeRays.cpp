#include "UltraGlowEdgeRays.h"

#include <array>

namespace diamond::fx {
namespace {

Rect intersect(Rect a, Rect b) {
    return {std::max(a.x1, b.x1), std::max(a.y1, b.y1),
            std::min(a.x2, b.x2), std::min(a.y2, b.y2)};
}

Context withImages(const Context& context, const Image& source, Image& output) {
    return {source, output, context.frame, context.pixelAspect, context.timeFrames,
            context.fps, context.abort, context.abortContext};
}

Pixel composite(Pixel base, Pixel light, bool screen) {
    const float coverage = std::clamp(std::max({light.r, light.g, light.b}), 0.0f, 1.0f);
    if (screen) {
        light.r *= std::max(0.0f, 1.0f - base.r);
        light.g *= std::max(0.0f, 1.0f - base.g);
        light.b *= std::max(0.0f, 1.0f - base.b);
    }
    return {base.r + light.r, base.g + light.g, base.b + light.b,
            base.a + (1.0f - base.a) * coverage};
}

void drawLight(const Context& context, const Image& light, const Image* secondary,
               float amount, float afterglow, bool only, bool screen) {
    const auto window = context.output.bounds;
    forEachRow(context, window.y1, window.y2, [&](int y) {
        for (int x = window.x1; x < window.x2; ++x) {
            const Pixel primary = light.read(x, y);
            const Pixel halo = secondary ? secondary->read(x, y) : Pixel{};
            const Pixel emission{amount * (primary.r + halo.r * afterglow),
                                 amount * (primary.g + halo.g * afterglow),
                                 amount * (primary.b + halo.b * afterglow), 0};
            context.output.pixel(x, y) = composite(only ? Pixel{} : context.source.read(x, y),
                                                   emission, screen);
        }
    });
}

void bloom(const Context& context, Image& output, double radius, const UltraGlowSettings& settings) {
    auto pass = withImages(context, context.source, output);
    const double stretch = std::exp2(settings.stretch);
    pass.pixelAspect /= stretch * stretch;
    renderGlow(pass, {settings.threshold, settings.knee, radius / stretch, 1.0,
        settings.spread, settings.red, settings.green, settings.blue, 0, true});
}

void primaryBloom(const Context& context, Image& result, const UltraGlowSettings& settings) {
    const std::array<double, 3> scale{settings.redWidth, settings.greenWidth, settings.blueWidth};
    if (scale[0] == scale[1] && scale[1] == scale[2]) {
        bloom(context, result, settings.radius * scale[0], settings);
        return;
    }

    const auto bounds = result.bounds;
    Image layer(bounds);
    const std::array<float Pixel::*, 3> channels{&Pixel::r, &Pixel::g, &Pixel::b};
    for (std::size_t i = 0; i < channels.size(); ++i) {
        if (context.cancelled()) {
            return;
        }
        bloom(context, layer, settings.radius * scale[i], settings);
        const auto channel = channels[i];
        forEachRow(context, bounds.y1, bounds.y2, [&](int y) {
            for (int x = bounds.x1; x < bounds.x2; ++x) {
                result.pixel(x, y).*channel = layer.pixel(x, y).*channel;
            }
        });
    }
    forEachRow(context, bounds.y1, bounds.y2, [&](int y) {
        for (int x = bounds.x1; x < bounds.x2; ++x) {
            auto& pixel = result.pixel(x, y);
            pixel.a = std::clamp(std::max({pixel.r, pixel.g, pixel.b}), 0.0f, 1.0f);
        }
    });
}

float luma(Pixel p) {
    return 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
}

float edgeSignal(const Context& context, int x, int y, double dx, double dy) {
    const auto sample = [&](double sx, double sy) {
        const auto frame = context.frame;
        return context.source.sample(std::clamp(sx, static_cast<double>(frame.x1), frame.x2 - 1.0),
                                     std::clamp(sy, static_cast<double>(frame.y1), frame.y2 - 1.0));
    };
    const float left = luma(sample(x - dx, y));
    const float right = luma(sample(x + dx, y));
    const float bottom = luma(sample(x, y - dy));
    const float top = luma(sample(x, y + dy));
    const float a = luma(sample(x - dx, y - dy));
    const float b = luma(sample(x + dx, y - dy));
    const float c = luma(sample(x - dx, y + dy));
    const float d = luma(sample(x + dx, y + dy));
    const float gx = 0.25f * (2.0f * (right - left) + b + d - a - c);
    const float gy = 0.25f * (2.0f * (top - bottom) + c + d - a - b);
    return std::hypot(gx, gy);
}

} // namespace

void renderUltraGlow(const Context& context, const UltraGlowSettings& settings) {
    const auto window = context.output.bounds;
    if (settings.brightness <= 0 || std::max({settings.red, settings.green, settings.blue}) <= 0) {
        drawLight(context, Image{}, nullptr, 0, 0, settings.glowOnly, false);
        return;
    }
    const double haloRadius = std::min(50.0, 2.75 * settings.radius);
    Rect expanded = window;
    const bool hasAfterglow = settings.afterglow > 0 && haloRadius > 0;
    if (hasAfterglow) {
        const double haloY = haloRadius * height(context.frame) / 100.0;
        const int padX = static_cast<int>(std::ceil(std::min(haloY / context.pixelAspect,
                                                            static_cast<double>(width(context.frame)))));
        const int padY = static_cast<int>(std::ceil(std::min(haloY, static_cast<double>(height(context.frame)))));
        expanded = intersect({window.x1 - padX, window.y1 - padY, window.x2 + padX, window.y2 + padY},
                             context.frame);
    }
    Image primary(expanded);
    primaryBloom(context, primary, settings);
    if (context.cancelled()) {
        return;
    }
    Image after;
    if (hasAfterglow) {
        after = Image(window);
        renderGlow(withImages(context, primary, after), {0, 0, haloRadius, 1, 0, 1, 1, 1, 0, true});
    }
    drawLight(context, primary, hasAfterglow ? &after : nullptr, static_cast<float>(settings.brightness),
              static_cast<float>(settings.afterglow), settings.glowOnly, settings.blend == 1);
}

void renderEdgeRays(const Context& context, const EdgeRaysSettings& settings) {
    if (settings.length <= 0 || settings.brightness <= 0) {
        drawLight(context, Image{}, nullptr, 0, 0, settings.raysOnly, false);
        return;
    }
    const auto bounds = intersect(context.frame, context.source.bounds);
    Image contours(bounds);
    const double dy = std::max(0.5, settings.edgeSize * height(context.frame) / 1080.0);
    const double dx = std::max(0.5, dy / context.pixelAspect);
    forEachRow(context, bounds.y1, bounds.y2, [&](int y) {
        for (int x = bounds.x1; x < bounds.x2; ++x) {
            const float edge = edgeSignal(context, x, y, dx, dy);
            contours.pixel(x, y) = {edge, edge, edge, 1.0f};
        }
    });
    if (context.cancelled()) {
        return;
    }
    Image rays(context.output.bounds);
    renderRays(withImages(context, contours, rays), {settings.centerX, settings.centerY, settings.length,
        settings.brightness, settings.threshold, settings.softness, settings.decay, settings.red,
        settings.green, settings.blue, settings.samples, true});
    drawLight(context, rays, nullptr, 1, 0, settings.raysOnly, settings.blend == 1);
}

} // namespace diamond::fx
