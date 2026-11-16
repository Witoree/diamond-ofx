#include "Glow06.h"

#include "Effects.h"
#include "GlowSpectral06.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace diamond::fx {
namespace {

constexpr double tau = 6.2831853071795864769;

double limited(double value, double low, double high, double fallback = 0)
{
    return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}

double aspect(const Context& context)
{
    return std::isfinite(context.pixelAspect) && context.pixelAspect > 0
        ? context.pixelAspect : 1.0;
}

float coverage(Pixel pixel)
{
    return static_cast<float>(limited(pixel.a, 0, 1));
}

double luminance(Pixel pixel)
{
    return 0.2126 * pixel.r + 0.7152 * pixel.g + 0.0722 * pixel.b;
}

float smoothUnit(double x)
{
    const double t = std::clamp(x, 0.0, 1.0);
    return static_cast<float>(t * t * (3.0 - 2.0 * t));
}

Pixel tint(double red, double green, double blue)
{
    return {static_cast<float>(limited(red, 0, 1)), static_cast<float>(limited(green, 0, 1)),
        static_cast<float>(limited(blue, 0, 1)), 1.0f};
}

Pixel composite(Pixel source, Pixel light, bool screen, bool glowOnly)
{
    if (glowOnly) {
        return light;
    }
    if (light.a <= 0.0f) {
        return source;
    }
    if (screen) {
        light.r *= std::max(0.0f, 1.0f - source.r);
        light.g *= std::max(0.0f, 1.0f - source.g);
        light.b *= std::max(0.0f, 1.0f - source.b);
    }
    return {source.r + light.r, source.g + light.g, source.b + light.b,
        source.a + (1.0f - coverage(source)) * light.a};
}

template <typename MakePixel>
void draw(const Context& context, const MakePixel& makePixel)
{
    const Rect bounds = context.output.bounds;
    forEachRow(context, bounds.y1, bounds.y2, [&](int y) {
        for (int x = bounds.x1; x < bounds.x2; ++x) {
            context.output.pixel(x, y) = makePixel(context.source.read(x, y), x, y);
        }
    });
}

void passThrough(const Context& context, bool glowOnly)
{
    draw(context, [glowOnly](Pixel source, int, int) {
        return glowOnly ? Pixel{} : source;
    });
}

Rect paddedRegion(Rect bounds, Rect frame, double padX, double padY)
{
    const auto x = static_cast<std::int64_t>(std::ceil(std::min<double>(padX, width(frame))));
    const auto y = static_cast<std::int64_t>(std::ceil(std::min<double>(padY, height(frame))));
    return {static_cast<int>(std::max<std::int64_t>(frame.x1, bounds.x1 - x)),
        static_cast<int>(std::max<std::int64_t>(frame.y1, bounds.y1 - y)),
        static_cast<int>(std::min<std::int64_t>(frame.x2, bounds.x2 + x)),
        static_cast<int>(std::min<std::int64_t>(frame.y2, bounds.y2 + y))};
}

template <typename MakeMask>
void blurMask(const Context& context, Image& glow, double radius, double spread, const MakeMask& makeMask)
{
    const double radiusInPixels = radius * height(context.frame) / 100.0;
    const double support = radiusInPixels * (spread > 0 ? 3.0 : 1.0);
    const Rect bounds = paddedRegion(glow.bounds, context.frame, support / aspect(context) + 2, support + 2);
    Image mask(bounds);
    forEachRow(context, bounds.y1, bounds.y2, [&](int y) {
        for (int x = bounds.x1; x < bounds.x2; ++x) {
            const float value = makeMask(x, y);
            mask.pixel(x, y) = {value, value, value, value};
        }
    });
    if (!context.cancelled()) {
        const Context blur{mask, glow, context.frame, context.pixelAspect, context.timeFrames,
            context.fps, context.abort, context.abortContext};
        renderGlow(blur, {0, 0, radius, 1, spread, 1, 1, 1, 0, true});
    }
}

Pixel spectralBands(double phase)
{
    const double cosine = std::cos(tau * phase);
    const double sine = std::sin(tau * phase);
    const auto band = [](double wave) {
        const double unit = 0.5 + 0.5 * wave;
        return static_cast<float>(0.20 + 1.35 * unit * unit);
    };
    constexpr double sine120 = 0.86602540378443864676;
    return {band(cosine), band(-0.5 * cosine + sine120 * sine),
        band(-0.5 * cosine - sine120 * sine), 0};
}

double edgeSignal(Pixel pixel, double alphaWeight)
{
    const double luma = luminance(pixel);
    return (1.0 - alphaWeight) * (std::isfinite(luma) ? luma : 0.0)
        + alphaWeight * coverage(pixel);
}

} // namespace

void renderGlowAura(const Context& context, const GlowAuraSettings& settings)
{
    const auto intensity = static_cast<float>(limited(settings.intensity, 0, 10));
    if (intensity == 0 || width(context.frame) == 0 || height(context.frame) == 0) {
        passThrough(context, settings.glowOnly);
        return;
    }

    const double radius = limited(settings.radius, 0, 30);
    const double pixelAspect = aspect(context);
    const int strideY = std::max(1, static_cast<int>(std::round(height(context.frame) / 540.0)));
    const int strideX = static_cast<int>(std::ceil(std::clamp(strideY / pixelAspect, 1.0,
        static_cast<double>(width(context.frame)))));
    const Rect bounds = paddedRegion(context.output.bounds, context.frame, strideX, strideY);
    Image field(bounds);
    const Context blur{context.source, field, context.frame, context.pixelAspect, context.timeFrames,
        context.fps, context.abort, context.abortContext};
    renderGlow(blur, {limited(settings.threshold, 0, 4), limited(settings.knee, 0, 2),
        radius, 1, .45, 1, 1, 1, 0, true});

    const auto color = tint(settings.red, settings.green, settings.blue);
    const float maximumTint = std::max({color.r, color.g, color.b});
    const double frequency = limited(settings.frequency, 0, 16);
    const double turns = std::floor(frequency);
    const auto frequencyBlend = static_cast<float>(frequency - turns);
    const double clock = context.timeFrames / limited(context.fps, 1.0e-3, 1.0e6, 30);
    const double speed = limited(settings.phaseSpeed, -5, 5);
    const double phase = limited(settings.phase, -1, 1)
        + (std::isfinite(clock * speed) ? std::remainder(clock * speed, 1.0) : 0.0);
    const double twist = limited(settings.twist, -4, 4);
    const double extent = std::max(1.0, radius * height(context.frame) / 100.0);
    const Rect frame = context.frame;
    draw(context, [&](Pixel source, int x, int y) {
        const Pixel glow = field.read(x, y);
        if (glow.a <= 0.0f) {
            return settings.glowOnly ? Pixel{} : source;
        }

        const auto left = field.read(std::max(frame.x1, x - strideX), y).a;
        const auto right = field.read(std::min(frame.x2 - 1, x + strideX), y).a;
        const auto bottom = field.read(x, std::max(frame.y1, y - strideY)).a;
        const auto top = field.read(x, std::min(frame.y2 - 1, y + strideY)).a;
        const double dx = (right - left) / (2.0 * strideX * pixelAspect);
        const double dy = (top - bottom) / (2.0 * strideY);
        const double gradient = std::sqrt(dx * dx + dy * dy);
        const float spectralMix = 0.84f * smoothUnit(gradient * extent * 1.8);
        Pixel light = glow;
        if (spectralMix > 0) {
            const double angle = std::atan2(dy, dx) / tau;
            const double spiral = phase + twist * (1.0 - coverage(glow));
            Pixel bands = spectralBands(angle * turns + spiral);
            // Interpolating adjacent integer harmonics keeps the angle wrap seamless.
            if (frequencyBlend > 0) {
                const Pixel next = spectralBands(angle * (turns + 1) + spiral);
                bands.r += (next.r - bands.r) * frequencyBlend;
                bands.g += (next.g - bands.g) * frequencyBlend;
                bands.b += (next.b - bands.b) * frequencyBlend;
            }
            const auto energy = static_cast<float>(std::max(0.0, luminance(glow)) * spectralMix);
            light.r = glow.r * (1.0f - spectralMix) + energy * bands.r;
            light.g = glow.g * (1.0f - spectralMix) + energy * bands.g;
            light.b = glow.b * (1.0f - spectralMix) + energy * bands.b;
        }
        light.r *= intensity * color.r;
        light.g *= intensity * color.g;
        light.b *= intensity * color.b;
        light.a = std::min(1.0f, glow.a * intensity * maximumTint);
        return composite(source, light, settings.blend == 1, settings.glowOnly);
    });
}

void renderGlowDarks(const Context& context, const GlowDarksSettings& settings)
{
    const double intensity = limited(settings.intensity, 0, 4);
    if (intensity == 0 || width(context.frame) == 0 || height(context.frame) == 0) {
        passThrough(context, settings.glowOnly);
        return;
    }

    const double threshold = limited(settings.threshold, 0, 2);
    const double knee = limited(settings.knee, 0, 1);
    Image glow(context.output.bounds);
    blurMask(context, glow, limited(settings.radius, 0, 30), limited(settings.spread, 0, 1), [&](int x, int y) {
        const Pixel source = context.source.read(x, y);
        const float alpha = coverage(source);
        if (alpha == 0) {
            return 0.0f;
        }
        const double luma = luminance(source) / alpha;
        if (!std::isfinite(luma)) {
            return 0.0f;
        }
        const float shadow = knee > 0 ? smoothUnit((threshold + knee - luma) / (2.0 * knee))
            : static_cast<float>(luma <= threshold);
        return alpha * shadow;
    });

    const Pixel color = tint(settings.red, settings.green, settings.blue);
    draw(context, [&](Pixel source, int x, int y) {
        const auto shadow = static_cast<float>(-std::expm1(-intensity * coverage(glow.read(x, y))));
        if (settings.glowOnly) {
            return Pixel{color.r * shadow, color.g * shadow, color.b * shadow, shadow};
        }
        if (shadow == 0.0f) {
            return source;
        }
        const float veil = (1.0f - coverage(source)) * shadow;
        return Pixel{source.r * (1.0f - shadow * (1.0f - color.r)) + color.r * veil,
            source.g * (1.0f - shadow * (1.0f - color.g)) + color.g * veil,
            source.b * (1.0f - shadow * (1.0f - color.b)) + color.b * veil,
            source.a + veil};
    });
}

void renderGlowDist(const Context& context, const GlowDistSettings& settings)
{
    const double intensity = limited(settings.intensity, 0, 10);
    const double radius = limited(settings.radius, 0, 30) * height(context.frame) / 100.0;
    const double redWidth = radius * limited(settings.redWidth, 0, 3);
    const double greenWidth = radius * limited(settings.greenWidth, 0, 3);
    const double blueWidth = radius * limited(settings.blueWidth, 0, 3);
    const double reach = std::max({redWidth, greenWidth, blueWidth});
    if (intensity == 0 || reach == 0 || width(context.frame) == 0) {
        passThrough(context, settings.glowOnly);
        return;
    }

    const double threshold = limited(settings.threshold, 0, 4);
    const double alphaWeight = limited(settings.alphaWeight, 0, 1);
    GlowDistanceField field;
    if (!field.build(context, threshold, 0, alphaWeight, reach + 1)) {
        return;
    }

    const Pixel color = tint(settings.red, settings.green, settings.blue);
    const double power = limited(settings.falloff, .25, 4, 1.65);
    const double halfPixel = 0.5 * std::min(1.0, aspect(context));
    draw(context, [&](Pixel source, int x, int y) {
        if (edgeSignal(source, alphaWeight) > threshold) {
            return settings.glowOnly ? Pixel{} : source;
        }
        const auto sample = field.sample(x, y);
        if (sample.strength <= 0 || !std::isfinite(sample.distance) || sample.distance > reach + halfPixel) {
            return settings.glowOnly ? Pixel{} : source;
        }
        const double distance = std::max(0.0, sample.distance - halfPixel);
        const auto falloff = [&](double channelWidth, float gain) {
            return channelWidth > distance && gain > 0
                ? static_cast<float>(intensity * gain * std::pow(1.0 - distance / channelWidth, power))
                : 0.0f;
        };
        Pixel light{falloff(redWidth, color.r), falloff(greenWidth, color.g), falloff(blueWidth, color.b), 0};
        light.a = std::min(1.0f, std::max({light.r, light.g, light.b}));
        return composite(source, light, settings.blend == 1, settings.glowOnly);
    });
}

void renderGlowEdges(const Context& context, const GlowEdgesSettings& settings)
{
    const double intensity = limited(settings.intensity, 0, 10);
    const Rect frame = context.frame;
    if (intensity == 0 || width(frame) == 0 || height(frame) == 0) {
        passThrough(context, settings.glowOnly);
        return;
    }

    const double pixelAspect = aspect(context);
    const double radius = limited(settings.radius, 0, 30);
    const double stepY = std::max(0.5, limited(settings.edgeSize, .5, 16, 2) * height(frame) / 1080.0);
    const double stepX = stepY / pixelAspect;
    const double threshold = limited(settings.threshold, 0, 4);
    const double softness = limited(settings.softness, 0, 1);
    const double alphaWeight = limited(settings.alphaWeight, 0, 1);
    const auto signal = [&](double x, double y) {
        return edgeSignal(context.source.sample(std::clamp(x, static_cast<double>(frame.x1), frame.x2 - 1.0),
            std::clamp(y, static_cast<double>(frame.y1), frame.y2 - 1.0)), alphaWeight);
    };
    Image glow(context.output.bounds);
    blurMask(context, glow, radius, limited(settings.spread, 0, 1), [&](int x, int y) {
        const double central = edgeSignal(context.source.read(x, y), alphaWeight);
        const double dx = std::max(std::abs(signal(x - stepX, y) - central),
            std::abs(signal(x + stepX, y) - central));
        const double dy = std::max(std::abs(signal(x, y - stepY) - central),
            std::abs(signal(x, y + stepY) - central));
        const double excess = std::sqrt(dx * dx + dy * dy) - threshold;
        return softness > 0 ? smoothUnit(excess / softness) : static_cast<float>(excess > 0);
    });

    const Pixel color = tint(settings.red, settings.green, settings.blue);
    // Offset the dilution of a thin contour as the blur radius grows.
    const double gain = intensity * std::clamp(1.0 + radius * height(frame) / (300.0 * stepY), 1.0, 10.0);
    draw(context, [&](Pixel source, int x, int y) {
        const auto energy = static_cast<float>(gain * coverage(glow.read(x, y)));
        const Pixel light{energy * color.r, energy * color.g, energy * color.b,
            std::min(1.0f, energy * std::max({color.r, color.g, color.b}))};
        return composite(source, light, settings.blend == 1, settings.glowOnly);
    });
}

} // namespace diamond::fx
