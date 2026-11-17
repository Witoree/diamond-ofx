#include "Film07.h"

#include "Effects.h"

#include <cstdint>
#include <vector>

namespace diamond::fx {
namespace {

constexpr double kTurn = 6.28318530717958647692;
constexpr double kTickPeriod = 4294967296.0;

double bounded(double value, double low, double high, double initial = 0.0) {
    return std::isfinite(value) ? std::clamp(value, low, high) : initial;
}

float unit(double value) {
    return static_cast<float>(bounded(value, 0, 1));
}

double aspect(const Context& context) {
    return bounded(context.pixelAspect, 0.01, 100, 1);
}

float smooth(double value) {
    const auto t = static_cast<float>(std::clamp(value, 0.0, 1.0));
    return t * t * (3.0f - 2.0f * t);
}

float luma(Pixel pixel) {
    return 0.2126f * pixel.r + 0.7152f * pixel.g + 0.0722f * pixel.b;
}

float interpolate(float first, float second, float weight) {
    return first + (second - first) * weight;
}

Pixel mix(Pixel dry, Pixel wet, float weight) {
    if (weight >= 1.0f) {
        return wet;
    }
    return {interpolate(dry.r, wet.r, weight), interpolate(dry.g, wet.g, weight),
            interpolate(dry.b, wet.b, weight), interpolate(dry.a, wet.a, weight)};
}

template <typename Render>
void draw(const Context& context, const Render& render) {
    const Rect area = context.output.bounds;
    forEachRow(context, area.y1, area.y2, [&](int y) {
        for (int x = area.x1; x < area.x2; ++x) {
            context.output.pixel(x, y) = render(x, y, context.source.read(x, y));
        }
    });
}

void copySource(const Context& context) {
    draw(context, [](int, int, Pixel source) { return source; });
}

std::uint32_t hash(std::uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    return value ^ (value >> 16);
}

float uniform(std::uint32_t key) {
    return static_cast<float>(hash(key) >> 8) * (1.0f / 16777216.0f);
}

float signedNoise(std::uint32_t key) {
    return 2.0f * uniform(key) - 1.0f;
}

float gaussianNoise(std::uint32_t key) {
    // Summing four byte-wide uniforms gives a centered, approximately
    // Gaussian draw with unit variance from a single integer hash.
    const std::uint32_t bits = hash(key);
    const auto sum = (bits & 255U) + ((bits >> 8) & 255U) + ((bits >> 16) & 255U) + (bits >> 24);
    return (static_cast<float>(sum) - 510.0f) * 0.00676587f;
}

std::uint32_t positionKey(int x, int y) {
    return static_cast<std::uint32_t>(x) * 0x9e3779b9U ^ static_cast<std::uint32_t>(y) * 0x85ebca6bU;
}

double phaseAt(const Context& context, double speed) {
    const double fps = bounded(context.fps, 0.001, 1000000, 30);
    const double time = context.timeFrames * bounded(speed, 0, 120) / fps;
    return std::isfinite(time) ? time : 0;
}

std::uint32_t tickAt(double phase) {
    const double wrapped = std::fmod(std::floor(phase), kTickPeriod);
    return static_cast<std::uint32_t>(wrapped >= 0 ? wrapped : wrapped + kTickPeriod);
}

float continuousNoise(double phase, std::uint32_t seed) {
    const std::uint32_t tick = tickAt(phase);
    const float first = signedNoise(seed ^ hash(tick));
    const float next = signedNoise(seed ^ hash(tick + 1));
    return interpolate(first, next, smooth(phase - std::floor(phase)));
}

struct Point {
    double x, y;

    Point operator+(Point other) const { return {x + other.x, y + other.y}; }
    Point operator-(Point other) const { return {x - other.x, y - other.y}; }
    Point operator*(double factor) const { return {x * factor, y * factor}; }
    double dot(Point other) const { return x * other.x + y * other.y; }
};

struct Mark {
    float dark = 0;
    float light = 0;
};

class DamageLayer {
public:
    DamageLayer(Rect bounds, bool enabled)
        : bounds_(bounds), stride_(width(bounds)),
          marks_(enabled ? static_cast<std::size_t>(stride_) * height(bounds) : 0) {}

    Mark read(int x, int y) const {
        return marks_.empty() ? Mark{} : marks_[index(x, y)];
    }

    void dust(Point center, double radiusX, double radiusY, double rotation, double par,
              float strength, bool light, std::uint32_t seed) {
        const double peak = std::max(radiusX, radiusY);
        const double cosine = std::cos(rotation);
        const double sine = std::sin(rotation);
        const float ruffle = signedNoise(seed ^ 0x2af16e89U);
        const double support = 1.42 * peak;
        cover(center.x - support, center.x + support, center.y - support, center.y + support, par,
            light, strength, [&](Point pixel) {
                const Point delta = pixel - center;
                const double u = (cosine * delta.x + sine * delta.y) / radiusX;
                const double v = (-sine * delta.x + cosine * delta.y) / radiusY;
                const double distance = u * u + v * v;
                return 1.0f - smooth((distance + 0.21 * u * v * ruffle - 0.42) / 0.75);
            });
    }

    void line(Point start, Point end, double radius, double par, float strength, bool light) {
        const double feather = 0.42 * std::sqrt(1.0 + par * par);
        const double support = radius + feather;
        const Point vector = end - start;
        const double squared = vector.dot(vector);
        const double inverse = squared > 0.0 ? 1.0 / squared : 0.0;
        const float ink = strength * static_cast<float>(std::min(1.0, radius / feather));
        cover(std::min(start.x, end.x) - support, std::max(start.x, end.x) + support,
            std::min(start.y, end.y) - support, std::max(start.y, end.y) + support,
            par, light, ink, [&](Point pixel) {
                const Point fromStart = pixel - start;
                const double t = std::clamp(fromStart.dot(vector) * inverse, 0.0, 1.0);
                const Point radial = fromStart - vector * t;
                const double distance = std::sqrt(radial.dot(radial));
                return smooth((support - distance) / (2.0 * feather));
            });
    }

private:
    template <typename Coverage>
    void cover(double left, double right, double low, double high, double par,
               bool forLight, float strength, const Coverage& coverage) {
        const auto lower = [](double value, int from, int to) {
            return static_cast<int>(std::clamp(std::floor(value), static_cast<double>(from),
                                               static_cast<double>(to)));
        };
        const int x1 = lower(left / par, bounds_.x1, bounds_.x2);
        const int x2 = lower(right / par + 1.0, bounds_.x1, bounds_.x2);
        const int y1 = lower(low, bounds_.y1, bounds_.y2);
        const int y2 = lower(high + 1.0, bounds_.y1, bounds_.y2);
        for (int y = y1; y < y2; ++y) {
            for (int x = x1; x < x2; ++x) {
                Mark& mark = marks_[index(x, y)];
                float& channel = forLight ? mark.light : mark.dark;
                channel = std::max(channel, strength * coverage({x * par, static_cast<double>(y)}));
            }
        }
    }

    std::size_t index(int x, int y) const {
        return static_cast<std::size_t>(y - bounds_.y1) * stride_ + (x - bounds_.x1);
    }

    Rect bounds_;
    int stride_;
    std::vector<Mark> marks_;
};

struct FilmArea {
    Point corner;
    double width, height, par, damageScale;

    Point point(double x, double y) const {
        return {corner.x + x * width, corner.y + y * height};
    }
};

bool addDust(const Context& context, DamageLayer& layer, const FilmArea& area,
             std::uint32_t seed, float amount) {
    const double quota = amount * 76.0;
    const int count = static_cast<int>(std::ceil(quota));
    for (int i = 0; i < count; ++i) {
        if (context.cancelled()) {
            return false;
        }
        const auto key = hash(seed ^ (0x2ab057cbU * static_cast<std::uint32_t>(i + 1)));
        const Point point = area.point(uniform(key ^ 0x604c5985U), uniform(key ^ 0xfdd6f727U));
        const double r = uniform(key ^ 0x89645f81U);
        const double radius = std::max(0.32, (0.75 + 6.4 * r * r) * area.damageScale);
        const double irregular = 0.48 + 0.42 * uniform(key ^ 0x1c6916e5U);
        const float opacity = 0.18f + 0.78f * uniform(key ^ 0x62e30fcfU);
        const bool light = uniform(key ^ 0xe89b5751U) > 0.70f;
        layer.dust(point, radius, radius * irregular, kTurn * uniform(key ^ 0x189a0fd5U),
                   area.par, opacity * static_cast<float>(std::min(1.0, quota - i)), light, key);
    }
    return true;
}

bool addHairs(const Context& context, DamageLayer& layer, const FilmArea& area,
              std::uint32_t seed, float amount) {
    const double quota = amount * 6.0;
    const int count = static_cast<int>(std::ceil(quota));
    constexpr int segments = 12;
    for (int i = 0; i < count; ++i) {
        if (context.cancelled()) {
            return false;
        }
        const auto key = hash(seed ^ (0xc41a695dU * static_cast<std::uint32_t>(i + 1)));
        const double angle = kTurn * uniform(key ^ 0x195337cbU);
        const Point tangent{std::cos(angle), std::sin(angle)};
        const Point across{-tangent.y, tangent.x};
        const double length = std::min(area.height * 0.75,
            (23.0 + 86.0 * uniform(key ^ 0x67226a31U)) * area.damageScale);
        const double bend = 0.24 + 0.34 * signedNoise(key ^ 0x04e2c69dU);
        const Point start = area.point(uniform(key ^ 0x9cc6b52dU), uniform(key ^ 0x9aa23d87U));
        const Point first = start + tangent * (length * 0.15) + across * (length * bend);
        const Point end = start + tangent * length + across * (length * -0.12);
        const Point second = end - tangent * (0.63 * length) + across * (length * (bend + 0.43));
        const bool light = uniform(key ^ 0xea681b4dU) > 0.84f;
        const auto opacity = static_cast<float>(std::min(1.0, quota - i)) * (light ? 0.58f : 0.88f);
        const double radius = std::max(0.23, (0.48 + 0.49 * uniform(key ^ 0x830abab9U)) * area.damageScale);
        Point previous = start;
        for (int segment = 1; segment <= segments; ++segment) {
            const double t = static_cast<double>(segment) / segments;
            const double s = 1.0 - t;
            const Point next = start * (s * s * s) + first * (3 * t * s * s)
                + second * (3 * t * t * s) + end * (t * t * t);
            const double tip = 0.50 + 0.50 * std::sin((t - 0.5 / segments) * kTurn * 0.5);
            layer.line(previous, next, radius * tip, area.par, opacity, light);
            previous = next;
        }
    }
    return true;
}

bool addScratches(const Context& context, DamageLayer& layer, const FilmArea& area,
                  std::uint32_t seed, double phase, float amount) {
    const double quota = amount * 11.0;
    const int count = static_cast<int>(std::ceil(quota));
    const auto strip = tickAt(phase / 9.0);
    constexpr int segments = 20;
    for (int i = 0; i < count; ++i) {
        if (context.cancelled()) {
            return false;
        }
        const auto key = hash(seed ^ hash(strip) ^ (0x395c0effU * static_cast<std::uint32_t>(i + 1)));
        const double x = uniform(key ^ 0xc3843b67U);
        const double position = x + 0.007 * continuousNoise(phase * 0.21, key ^ 0xd1b13e8bU);
        const double startY = -0.14 + 0.44 * uniform(key ^ 0xc48e573fU);
        const double endY = 0.64 + 0.48 * uniform(key ^ 0x7bafdd31U);
        const double slope = 0.0102 * signedNoise(key ^ 0xfad38b79U);
        const double wobble = 0.0012 + 0.0036 * uniform(key ^ 0xba98fe47U);
        const double extent = 1.4 + 3.1 * uniform(key ^ 0x27b07917U);
        const double ripple = kTurn * uniform(key ^ 0x2593a171U);
        const bool light = uniform(key ^ 0xcf82581bU) > 0.32f;
        const float contrast = 0.18f + 0.50f * uniform(key ^ 0x6d25a7cbU);
        const auto opacity = contrast * static_cast<float>(std::min(1.0, quota - i));
        const double radius = std::max(0.22, (0.36 + 0.65 * uniform(key ^ 0x89db2475U)) * area.damageScale);
        const auto path = [&](double t) {
            return area.point(position + slope * t + wobble * std::sin(extent * t + ripple), startY + (endY - startY) * t);
        };
        Point previous = path(0);
        for (int segment = 1; segment <= segments; ++segment) {
            const double t = static_cast<double>(segment) / segments;
            const Point next = path(t);
            const float texture = uniform(key ^ (0x4aff3dc9U * static_cast<std::uint32_t>(segment)));
            if (texture > 0.12f) {
                layer.line(previous, next, radius * (0.78 + 0.40 * texture), area.par,
                           opacity * (0.55f + 0.62f * texture), light);
            }
            previous = next;
        }
    }
    return true;
}

struct GrainGrid {
    int x, y;
    float u, v;

    GrainGrid(double xPosition, double yPosition)
        : x(static_cast<int>(std::floor(xPosition))), y(static_cast<int>(std::floor(yPosition))),
          u(smooth(xPosition - x)), v(smooth(yPosition - y)) {}

    float sample(std::uint32_t key) const {
        const float p = gaussianNoise(positionKey(x, y) ^ key);
        const float q = gaussianNoise(positionKey(x + 1, y) ^ key);
        const float r = gaussianNoise(positionKey(x, y + 1) ^ key);
        const float s = gaussianNoise(positionKey(x + 1, y + 1) ^ key);
        return 1.29f * interpolate(interpolate(p, q, u), interpolate(r, s, u), v);
    }
};

float addNoise(float premultiplied, float offset, float alpha) {
    // Protect a zero-valued image channel from newly introduced negative data.
    // Floating-point negatives already present in the image retain headroom.
    return premultiplied + std::max(offset * alpha, -std::max(0.0f, premultiplied));
}

} // namespace

void renderFilmDamage(const Context& context, const FilmDamageSettings& settings) {
    const float mixAmount = unit(settings.mix);
    const float dust = unit(settings.dust);
    const float hair = unit(settings.hair);
    const float scratches = unit(settings.scratches);
    const float flicker = unit(settings.flicker);
    const float weave = unit(settings.weave);
    const Rect frame = context.frame;
    const int frameWidth = width(frame);
    const int frameHeight = height(frame);
    if (mixAmount <= 0 || frameWidth <= 0 || frameHeight <= 0
        || std::max({dust, hair, scratches, flicker, weave}) <= 0) {
        copySource(context);
        return;
    }
    const Rect output = context.output.bounds;
    if (width(output) == 0 || height(output) == 0 || context.cancelled()) {
        return;
    }

    const double par = aspect(context);
    const FilmArea area{{frame.x1 * par, static_cast<double>(frame.y1)}, frameWidth * par,
        static_cast<double>(frameHeight), frameHeight / 1080.0 * bounded(settings.size, 0.15, 6, 1)};
    const double phase = phaseAt(context, settings.speed);
    const auto seed = hash(static_cast<std::uint32_t>(settings.seed));
    const auto tick = hash(tickAt(phase));
    const bool hasMarks = std::max({dust, hair, scratches}) > 0;
    DamageLayer layer(output, hasMarks);
    if ((dust > 0 && !addDust(context, layer, area, seed ^ tick ^ 0xe7340ef1U, dust))
        || (hair > 0 && !addHairs(context, layer, area, seed ^ hash(tickAt(phase / 3.0)) ^ 0x020474cbU, hair))
        || (scratches > 0 && !addScratches(context, layer, area, seed ^ 0x88baf763U, phase, scratches))) {
        return;
    }

    const float pulse = 0.62f * continuousNoise(phase * 0.31, seed ^ 0x117053b9U)
        + 0.29f * signedNoise(seed ^ tick ^ 0x6bddeac5U)
        + 0.09f * continuousNoise(phase * 0.073, seed ^ 0xbe33eda1U);
    const float gain = std::exp2(0.86f * pulse * flicker);
    const double driftX = area.height * 0.011 * weave
        * (0.76 * continuousNoise(phase * 0.41, seed ^ 0x2de594cdU)
           + 0.24 * signedNoise(seed ^ tick ^ 0xb849f3b1U));
    const double driftY = area.height * 0.007 * weave
        * (0.78 * continuousNoise(phase * 0.37, seed ^ 0xd951ebc7U)
           + 0.22 * signedNoise(seed ^ tick ^ 0x943caec1U));
    const double rotation = 0.0125 * weave * continuousNoise(phase * 0.28, seed ^ 0x14107e01U);
    const Point center = area.point(0.5, 0.5);
    const double cosine = std::cos(rotation);
    const double sine = std::sin(rotation);
    draw(context, [&](int x, int y, Pixel source) {
        Pixel wet = source;
        if (weave > 0) {
            const Point local{x * par - center.x, y - center.y};
            const double sampleX = (center.x + cosine * local.x + sine * local.y - driftX) / par;
            const double sampleY = center.y - sine * local.x + cosine * local.y - driftY;
            wet = context.source.sample(sampleX, sampleY, Edge::Reflect);
        }
        const auto mark = layer.read(x, y);
        const float exposure = gain * (1.0f - std::clamp(mark.dark, 0.0f, 1.0f));
        const float blemish = wet.a * mark.light;
        wet.r = wet.r * exposure + 0.96f * blemish;
        wet.g = wet.g * exposure + 0.92f * blemish;
        wet.b = wet.b * exposure + 0.84f * blemish;
        return mix(source, wet, mixAmount);
    });
}

void renderGrainStatic(const Context& context, const GrainStaticSettings& settings) {
    const float amount = unit(settings.amount) * unit(settings.mix);
    if (amount <= 0.0f) {
        copySource(context);
        return;
    }
    const float color = unit(settings.color);
    const auto seed = hash(static_cast<std::uint32_t>(settings.seed)
        ^ hash(tickAt(phaseAt(context, settings.speed))));
    draw(context, [&](int x, int y, Pixel source) {
        if (source.a <= 0.0f) {
            return source;
        }
        const auto key = seed ^ positionKey(x - context.frame.x1, y - context.frame.y1);
        const float monochrome = signedNoise(key ^ 0x3480a603U) * (1.0f - color);
        const auto change = [&](float channel, std::uint32_t salt) {
            const float noise = monochrome + color * signedNoise(key ^ salt);
            return addNoise(channel, amount * 0.26f * noise, source.a);
        };
        return Pixel{change(source.r, 0xa23ea05dU), change(source.g, 0x6c887b27U),
                     change(source.b, 0xe2c0aeddU), source.a};
    });
}

void renderUltraGrain(const Context& context, const UltraGrainSettings& settings) {
    const float amount = unit(settings.amount) * unit(settings.mix);
    const float shot = 0.17f * unit(settings.photonNoise);
    const float read = 0.105f * unit(settings.readNoise);
    const float fixed = 0.13f * unit(settings.fixedPattern);
    const float shotVariance = shot * shot;
    const float readVariance = read * read;
    const int frameHeight = height(context.frame);
    if (amount <= 0 || frameHeight <= 0 || std::max({shot, read, fixed}) <= 0) {
        copySource(context);
        return;
    }
    const auto fixedSeed = hash(static_cast<std::uint32_t>(settings.seed) ^ 0x2494763bU);
    const auto frameSeed = fixedSeed ^ hash(tickAt(phaseAt(context, settings.speed)) ^ 0xe970af41U);
    const double par = aspect(context);
    const double size = bounded(settings.size, 0.35, 12, 1.2) * frameHeight / 1080.0;
    const float antiAlias = smooth(size / std::max(1.0, par));
    const float color = unit(settings.color);
    draw(context, [&](int x, int y, Pixel source) {
        if (source.a <= 1.0e-6f) {
            return source;
        }
        const GrainGrid grid((static_cast<double>(x) - context.frame.x1 + 0.5) * par / size,
            (static_cast<double>(y) - context.frame.y1 + 0.5) / size);
        const float linearLuma = luma(source) / source.a;
        if (!std::isfinite(linearLuma)) {
            return source;
        }
        // Shot noise variance tracks the photon signal. Readout noise remains
        // present in shadows where there is no photon signal.
        const float shotSignal = shot * std::sqrt(std::max(0.0f, linearLuma));
        const float mono = (shotSignal > 0 ? shotSignal * grid.sample(frameSeed ^ 0xf6bade5fU) : 0.0f)
            + (read > 0 ? read * grid.sample(frameSeed ^ 0xe5095e1dU) : 0.0f);
        const float fixedDrift = fixed > 0 ? fixed * (0.42f * grid.sample(fixedSeed ^ 0xe2ad6febU)
            + 0.29f * signedNoise(fixedSeed ^ positionKey(grid.x, 0) ^ 0xd073c597U)
            + 0.18f * signedNoise(fixedSeed ^ positionKey(0, grid.y) ^ 0x61a82871U)) : 0.0f;
        const auto change = [&](float component, std::uint32_t salt) {
            const float signal = component / source.a;
            float sample = mono * (1.0f - color);
            if (color > 0.0f && (shot > 0.0f || read > 0.0f)) {
                // Independent shot and read noise variances add.
                const float response = std::sqrt(shotVariance * std::max(signal, 0.0f) + readVariance);
                sample += color * response * grid.sample(frameSeed ^ salt);
            }
            // Fixed-pattern noise combines a mild sensor response variation
            // with a smaller readout offset that remains locked to the frame.
            const float pattern = fixedDrift * (0.18f + std::max(signal, 0.0f));
            return addNoise(component, amount * antiAlias * (sample + pattern), source.a);
        };
        return Pixel{change(source.r, 0xbb32d2c9U), change(source.g, 0x56525345U),
                     change(source.b, 0x532bb383U), source.a};
    });
}

} // namespace diamond::fx
