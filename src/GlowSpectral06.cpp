#include "GlowSpectral06.h"

#include "Effects.h"

#include <array>
#include <cstdint>
#include <limits>

namespace diamond::fx {
namespace {

constexpr float kInfinity = std::numeric_limits<float>::infinity();
constexpr double kTurn = 6.28318530717958647692;

double bounded(double value, double minimum, double maximum, double initial = 0) {
    return std::isfinite(value) ? std::clamp(value, minimum, maximum) : initial;
}

float smooth(double value) {
    const auto t = static_cast<float>(std::clamp(value, 0.0, 1.0));
    return t * t * (3.0f - 2.0f * t);
}

float luma(Pixel pixel) {
    return 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
}

bool contains(Rect rect, int x, int y) {
    return x >= rect.x1 && x < rect.x2 && y >= rect.y1 && y < rect.y2;
}

template <typename Render>
void draw(const Context& context, const Render& render) {
    const Rect window = context.output.bounds;
    forEachRow(context, window.y1, window.y2, [&](int y) {
        for (int x = window.x1; x < window.x2; ++x) {
            context.output.pixel(x, y) = render(x, y, context.source.read(x, y));
        }
    });
}

void bypass(const Context& context, bool isolated = false) {
    draw(context, [&](int, int, Pixel source) { return isolated ? Pixel{} : source; });
}

struct Color {
    float r, g, b;

    Color operator*(float value) const { return {r * value, g * value, b * value}; }
};

Color tintFor(double brightness, double red, double green, double blue) {
    const auto gain = static_cast<float>(bounded(brightness, 0, 8));
    return {gain * static_cast<float>(bounded(red, 0, 1, 1)),
            gain * static_cast<float>(bounded(green, 0, 1, 1)),
            gain * static_cast<float>(bounded(blue, 0, 1, 1))};
}

float peak(Color color) { return std::max({color.r, color.g, color.b}); }

Pixel composite(Pixel source, Color light, bool isolated, int blend) {
    const float alpha = std::clamp(peak(light), 0.0f, 1.0f);
    if (isolated) {
        return {light.r, light.g, light.b, alpha};
    }
    if (blend == 1) {
        light.r = std::min(light.r, 1.0f) * (1.0f - std::clamp(source.r, 0.0f, 1.0f));
        light.g = std::min(light.g, 1.0f) * (1.0f - std::clamp(source.g, 0.0f, 1.0f));
        light.b = std::min(light.b, 1.0f) * (1.0f - std::clamp(source.b, 0.0f, 1.0f));
    }
    return {source.r + light.r, source.g + light.g, source.b + light.b,
            source.a + (1.0f - source.a) * alpha};
}

float edgeStrength(float value, double threshold, double knee) {
    const float excess = value - static_cast<float>(threshold);
    if (!(excess > 0) || !std::isfinite(excess)) {
        return 0.0f;
    }
    const float kneeGain = knee > 0 ? smooth(excess / knee) : 1.0f;
    return kneeGain * (excess / (excess + 0.22f));
}

float darkSide(Pixel input, double threshold, double knee) {
    const float luminance = luma(input);
    return std::isfinite(luminance)
        ? 1.0f - smooth((luminance - threshold) / std::max(0.02, knee)) : 0.0f;
}

double frameRadius(const Context& context, double percent) {
    return static_cast<double>(height(context.frame)) * 0.01 * bounded(percent, 0, 20);
}

std::uint32_t hash(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

float noise(int x, int y, std::uint32_t seed) {
    const auto key = static_cast<std::uint32_t>(x) * 0x9e3779b9U
        ^ static_cast<std::uint32_t>(y) * 0x85ebca6bU ^ seed;
    return static_cast<float>(hash(key) >> 8) * (1.0f / 16777216.0f);
}

float coherentNoise(double x, double y, std::uint32_t seed) {
    const int left = static_cast<int>(std::floor(x));
    const int bottom = static_cast<int>(std::floor(y));
    const float u = smooth(x - left);
    const float v = smooth(y - bottom);
    const float a = noise(left, bottom, seed);
    const float b = noise(left + 1, bottom, seed);
    const float c = noise(left, bottom + 1, seed);
    const float d = noise(left + 1, bottom + 1, seed);
    const float low = a + u * (b - a);
    return low + v * (c + u * (d - c) - low);
}

double phaseAt(const Context& context, double speed) {
    const double time = context.timeFrames / bounded(context.fps, 0.001, 1000000, 30);
    const double phase = time * speed;
    return std::isfinite(phase) ? std::remainder(phase, 4096.0) : 0;
}

std::size_t offset(int x, int y, int stride) {
    return static_cast<std::size_t>(y) * stride + x;
}

struct RowDistance {
    double squared;
    float strength;
};

class DistanceLine {
public:
    explicit DistanceLine(int size) : locations_(size), starts_(size) {}

    template <typename Cost, typename Store>
    void transform(int size, double spacingSquared, const Cost& cost, const Store& store) {
        // The lower envelope of the seed parabolas gives the nearest seed to
        // every position in linear time. Add horizontal costs in the vertical pass.
        int used = 0;
        for (int position = 0; position < size; ++position) {
            const double current = cost(position);
            if (!std::isfinite(current)) {
                continue;
            }
            double crossing = -kInfinity;
            while (used > 0) {
                const int previous = locations_[used - 1];
                crossing = 0.5 * (static_cast<double>(position) + previous
                    + (current - cost(previous)) / (spacingSquared * (position - previous)));
                if (crossing > starts_[used - 1]) {
                    break;
                }
                --used;
            }
            locations_[used] = position;
            starts_[used] = used == 0 ? -kInfinity : crossing;
            ++used;
        }
        if (used == 0) {
            for (int position = 0; position < size; ++position) {
                store(position, kInfinity, -1);
            }
            return;
        }
        int nearest = 0;
        for (int position = 0; position < size; ++position) {
            while (nearest + 1 < used && starts_[nearest + 1] < position) {
                ++nearest;
            }
            const int seed = locations_[nearest];
            const auto dx = static_cast<double>(position - seed);
            store(position, cost(seed) + spacingSquared * dx * dx, seed);
        }
    }

private:
    std::vector<int> locations_;
    std::vector<double> starts_;
};

Color spectrum(double phase, double saturation) {
    const double angle = std::remainder(phase, 1.0) * kTurn;
    const auto channel = [angle, saturation](double shift) {
        const double wave = 0.5 + 0.5 * std::cos(angle + shift);
        return static_cast<float>(1.0 - saturation * (1.0 - wave));
    };
    return {channel(0), channel(-kTurn / 3), channel(kTurn / 3)};
}

float ringProfile(double distance, double center, double halfWidth) {
    const double softness = std::abs(distance - center) / halfWidth;
    return softness < 1.0 ? 1.0f - smooth(softness) : 0.0f;
}

} // namespace

bool GlowDistanceField::build(const Context& context, double threshold, double knee,
                              double alphaWeight, double maximumDistance) {
    frame_ = context.frame;
    cells_.clear();
    const int frameWidth = width(frame_);
    const int frameHeight = height(frame_);
    if (frameWidth <= 0 || frameHeight <= 0 || width(context.source.bounds) <= 0
        || height(context.source.bounds) <= 0) {
        return !context.cancelled();
    }
    const double aspect = bounded(context.pixelAspect, 0.01, 100.0, 1.0);
    step_ = std::max({1, 1 + (frameWidth - 1) / 2160, 1 + (frameHeight - 1) / 1080});
    grid_ = {0, 0, 1 + (frameWidth - 1) / step_, 1 + (frameHeight - 1) / step_};
    if (std::isfinite(maximumDistance) && maximumDistance > 0.0) {
        const Rect output = context.output.bounds;
        const auto lower = [&](double value, int limit) {
            return static_cast<int>(std::clamp(std::floor(value / step_) - 2.0, 0.0, static_cast<double>(limit)));
        };
        const auto upper = [&](double value, int limit) {
            return static_cast<int>(std::clamp(std::ceil(value / step_) + 2.0, 0.0, static_cast<double>(limit)));
        };
        grid_ = {lower(static_cast<double>(output.x1) - frame_.x1 - maximumDistance / aspect, grid_.x2),
                 lower(static_cast<double>(output.y1) - frame_.y1 - maximumDistance, grid_.y2),
                 upper(static_cast<double>(output.x2) - frame_.x1 + maximumDistance / aspect, grid_.x2),
                 upper(static_cast<double>(output.y2) - frame_.y1 + maximumDistance, grid_.y2)};
    }
    const int columns = width(grid_);
    const int rows = height(grid_);
    if (columns <= 0 || rows <= 0 || context.cancelled()) {
        return !context.cancelled();
    }

    threshold = bounded(threshold, 0, 4, 0.55);
    knee = bounded(knee, 0, 2, 0.15);
    const auto fromAlpha = static_cast<float>(bounded(alphaWeight, 0, 1));
    const auto size = static_cast<std::size_t>(columns) * rows;
    std::vector<RowDistance> horizontal(size, {kInfinity, 0});
    const double sx = step_ * aspect;
    parallelBands(0, rows, [&](int first, int last) {
        DistanceLine transform(columns);
        std::vector<float> seeds(columns);
        for (int row = first; row < last && !context.cancelled(); ++row) {
            const int beginY = frame_.y1 + (row + grid_.y1) * step_;
            const int endY = beginY + std::min(step_, frame_.y2 - beginY);
            for (int col = 0; col < columns; ++col) {
                const int beginX = frame_.x1 + (col + grid_.x1) * step_;
                const int endX = beginX + std::min(step_, frame_.x2 - beginX);
                float top = 0;
                for (int y = beginY; y < endY; ++y) {
                    for (int x = beginX; x < endX; ++x) {
                        const Pixel pixel = context.source.read(x, y);
                        const float brightness = luma(pixel) * (1.0f - fromAlpha)
                            + std::clamp(pixel.a, 0.0f, 1.0f) * fromAlpha;
                        if (std::isfinite(brightness)) {
                            top = std::max(top, brightness);
                        }
                    }
                }
                seeds[col] = edgeStrength(top, threshold, knee);
            }
            transform.transform(columns, sx * sx, [&](int col) {
                return seeds[col] > 0.0f ? 0.0 : static_cast<double>(kInfinity);
            }, [&](int col, double distance, int seed) {
                horizontal[offset(col, row, columns)] = {distance, seed >= 0 ? seeds[seed] : 0.0f};
            });
        }
    }, 16);
    if (context.cancelled()) {
        return false;
    }
    cells_.resize(size, {kInfinity, 0});
    const double sy = step_;
    parallelBands(0, columns, [&](int first, int last) {
        DistanceLine transform(rows);
        for (int col = first; col < last && !context.cancelled(); ++col) {
            transform.transform(rows, sy * sy, [&](int row) {
                return horizontal[offset(col, row, columns)].squared;
            }, [&](int row, double distance, int seed) {
                if (seed >= 0) {
                    cells_[offset(col, row, columns)] = {static_cast<float>(std::sqrt(distance)),
                        horizontal[offset(col, seed, columns)].strength};
                }
            });
        }
    }, 16);
    return !context.cancelled();
}

GlowDistanceSample GlowDistanceField::sample(int x, int y) const {
    if (cells_.empty() || !contains(frame_, x, y)) {
        return {kInfinity, 0};
    }
    const int columns = width(grid_);
    const double u = std::clamp((static_cast<double>(x) - frame_.x1 + 0.5) / step_ - 0.5,
        static_cast<double>(grid_.x1), grid_.x2 - 1.0);
    const double v = std::clamp((static_cast<double>(y) - frame_.y1 + 0.5) / step_ - 0.5,
        static_cast<double>(grid_.y1), grid_.y2 - 1.0);
    const int left = static_cast<int>(u);
    const int bottom = static_cast<int>(v);
    const int right = std::min(left + 1, grid_.x2 - 1);
    const int top = std::min(bottom + 1, grid_.y2 - 1);
    const auto p = cells_[offset(left - grid_.x1, bottom - grid_.y1, columns)];
    if (!std::isfinite(p.distance) || (u == left && v == bottom)) {
        return p;
    }
    const auto q = cells_[offset(right - grid_.x1, bottom - grid_.y1, columns)];
    const auto r = cells_[offset(left - grid_.x1, top - grid_.y1, columns)];
    const auto s = cells_[offset(right - grid_.x1, top - grid_.y1, columns)];
    const auto fx = static_cast<float>(u - left);
    const auto fy = static_cast<float>(v - bottom);
    const auto blend = [fx, fy](float a, float b, float c, float d) {
        const float lower = a + fx * (b - a);
        return lower + fy * (c + fx * (d - c) - lower);
    };
    return {blend(p.distance, q.distance, r.distance, s.distance),
        blend(p.strength, q.strength, r.strength, s.strength)};
}

void renderGlowNoise(const Context& context, const GlowNoiseSettings& settings) {
    const Color tint = tintFor(settings.brightness, settings.red, settings.green, settings.blue);
    if (peak(tint) <= 0 || frameRadius(context, settings.radius) <= 0) {
        bypass(context, settings.glowOnly);
        return;
    }
    const GlowSettings bloom{bounded(settings.threshold, 0, 4, 0.55), bounded(settings.knee, 0, 1, 0.15),
        bounded(settings.radius, 0, 20), 1.0, 0.36, 1.0, 1.0, 1.0, 0, true};
    renderGlow(context, bloom);
    if (context.cancelled()) {
        return;
    }
    const auto amount = static_cast<float>(bounded(settings.noiseAmount, 0, 1));
    const double cellSize = bounded(settings.noiseScale, 1, 150, 10) * height(context.frame) / 1080.0;
    const double aspect = bounded(context.pixelAspect, 0.01, 100.0, 1.0);
    const double phase = phaseAt(context, bounded(settings.speed, 0, 12));
    const auto seed = static_cast<std::uint32_t>(settings.seed);
    const float fineCoverage = smooth(cellSize / std::max(aspect, 1.0));
    draw(context, [&](int x, int y, Pixel source) {
        if (!contains(context.frame, x, y)) {
            return settings.glowOnly ? Pixel{} : source;
        }
        const Pixel glow = context.output.read(x, y);
        if (std::max({glow.r, glow.g, glow.b}) <= 0.0f) {
            return settings.glowOnly ? Pixel{} : source;
        }
        const double u = (static_cast<double>(x) - context.frame.x1 + 0.5) * aspect / cellSize;
        const double v = (static_cast<double>(y) - context.frame.y1 + 0.5) / cellSize;
        const float coarse = coherentNoise(u + phase * 1.36, v - phase * 0.52, seed);
        const float fine = coherentNoise(u * 2.3 - phase * 2.31, v * 2.3 + phase * 1.72, seed ^ 0xa61b6f23U);
        const float pattern = 0.67f * coarse + 0.33f * (0.5f + fineCoverage * (fine - 0.5f));
        const float attenuation = 1.0f - amount * (1.0f - (0.18f + 0.82f * pattern * pattern));
        return composite(source, Color{glow.r * tint.r, glow.g * tint.g, glow.b * tint.b} * attenuation,
            settings.glowOnly, settings.blend);
    });
}

void renderGlowOrthicon(const Context& context, const GlowOrthiconSettings& settings) {
    const double radius = frameRadius(context, settings.radius);
    const float darkness = static_cast<float>(bounded(settings.darkness, 0, 1));
    if (radius <= 0 || darkness <= 0) {
        bypass(context);
        return;
    }
    GlowDistanceField field;
    if (!field.build(context, settings.threshold, settings.knee, 0, radius)) {
        return;
    }
    draw(context, [&](int x, int y, Pixel source) {
        const auto around = field.sample(x, y);
        const double t = around.distance / radius;
        if (!(t > 0 && t < 1)) {
            return source;
        }
        const float mask = smooth(t / 0.17) * (1.0f - smooth((t - 0.13) / 0.87));
        const float remaining = 1.0f - darkness * mask * around.strength
            * darkSide(source, settings.threshold, settings.knee);
        return Pixel{source.r * remaining, source.g * remaining, source.b * remaining, source.a};
    });
}

void renderGlowRainbow(const Context& context, const GlowRainbowSettings& settings) {
    const Color tint = tintFor(settings.brightness, settings.red, settings.green, settings.blue);
    const double radius = frameRadius(context, settings.radius);
    if (radius <= 0 || peak(tint) <= 0) {
        bypass(context, settings.glowOnly);
        return;
    }
    GlowDistanceField field;
    if (!field.build(context, settings.threshold, settings.knee, 0, radius)) {
        return;
    }
    const double cycles = bounded(settings.cycles, 0, 8, 1.35);
    const double phase = bounded(settings.phase, -8, 8) - phaseAt(context, bounded(settings.speed, -8, 8));
    const double saturation = bounded(settings.saturation, 0, 1, 0.95);
    draw(context, [&](int x, int y, Pixel source) {
        const auto around = field.sample(x, y);
        const double t = around.distance / radius;
        if (!(t < 1) || around.strength <= 0.0f) {
            return settings.glowOnly ? Pixel{} : source;
        }
        const float energy = (1.0f - smooth(t)) * around.strength
            * darkSide(source, settings.threshold, settings.knee);
        const Color color = spectrum(phase + cycles * t, saturation);
        return composite(source, Color{color.r * tint.r, color.g * tint.g, color.b * tint.b} * energy,
            settings.glowOnly, settings.blend);
    });
}

void renderGlowRings(const Context& context, const GlowRingsSettings& settings) {
    const Color tint = tintFor(settings.brightness, settings.red, settings.green, settings.blue);
    const double radius = frameRadius(context, settings.radius);
    if (radius <= 0 || peak(tint) <= 0) {
        bypass(context, settings.glowOnly);
        return;
    }
    GlowDistanceField field;
    if (!field.build(context, settings.threshold, settings.knee, 0, radius)) {
        return;
    }
    const int rings = std::clamp(settings.rings, 1, 6);
    const double interval = radius / (rings + 0.35);
    const double dispersion = 0.28 * interval * bounded(settings.dispersion, 0, 1);
    const double width = std::max(0.55, interval * bounded(settings.thickness, 0.025, 1, 0.24));
    draw(context, [&](int x, int y, Pixel source) {
        const auto around = field.sample(x, y);
        const double t = around.distance / radius;
        if (!(t > 0 && t < 1) || around.strength <= 0.0f) {
            return settings.glowOnly ? Pixel{} : source;
        }
        Color ring{};
        for (int band = 0; band < rings; ++band) {
            const double center = (band + 0.73) * interval;
            const float gain = 1.0f - 0.56f * band / rings;
            ring.r += gain * ringProfile(around.distance, center + dispersion, width);
            ring.g += gain * ringProfile(around.distance, center, width);
            ring.b += gain * ringProfile(around.distance, center - dispersion, width);
        }
        const float edge = (1.0f - smooth((t - 0.84) / 0.16)) * smooth(t / 0.10);
        const float energy = edge * around.strength * darkSide(source, settings.threshold, settings.knee);
        return composite(source, Color{ring.r * tint.r, ring.g * tint.g, ring.b * tint.b} * energy,
            settings.glowOnly, settings.blend);
    });
}

} // namespace diamond::fx
