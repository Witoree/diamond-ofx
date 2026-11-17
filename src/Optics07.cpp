#include "Optics07.h"

#include "Effects.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace diamond::fx {
namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double tau = 2.0 * pi;

struct Color {
    float r = 0, g = 0, b = 0;
};

struct Point {
    double x = 0, y = 0;
};

struct Space {
    Rect frame;
    double pixelAspect;

    explicit Space(const Context& context)
        : frame(context.frame), pixelAspect(std::isfinite(context.pixelAspect)
            ? std::clamp(context.pixelAspect, .01, 100.0) : 1.0) {}

    double x(int pixel) const { return (static_cast<double>(pixel) - frame.x1 + .5) * pixelAspect; }
    double y(int pixel) const { return static_cast<double>(pixel) - frame.y1 + .5; }
    double extentX() const { return width(frame) * pixelAspect; }
    double extentY() const { return height(frame); }
};

double bound(double value, double low, double high, double fallback = 0)
{
    return std::clamp(std::isfinite(value) ? value : fallback, low, high);
}

double smooth(double value)
{
    const double t = std::clamp(value, 0.0, 1.0);
    return t * t * (3 - 2 * t);
}

Color tint(double red, double green, double blue)
{
    return {static_cast<float>(bound(red, 0, 1)), static_cast<float>(bound(green, 0, 1)),
        static_cast<float>(bound(blue, 0, 1))};
}

Color mix(Color a, Color b, float amount)
{
    return {a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount, a.b + (b.b - a.b) * amount};
}

Color spectrum(double phase)
{
    const double hue = (phase - std::floor(phase)) * 6;
    const auto channel = [&](double offset) {
        const double channelHue = hue + offset;
        const double band = channelHue < 6 ? channelHue : channelHue - 6;
        return static_cast<float>(.12 + .88 * std::clamp(std::abs(band - 3) - 1, 0.0, 1.0));
    };
    return {channel(0), channel(4), channel(2)};
}

std::uint32_t hash(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

double random(std::uint32_t seed, std::uint32_t stream)
{
    return static_cast<double>(hash(seed ^ (stream * 0x9e3779b9u))) * (1.0 / 4294967296.0);
}

double fps(const Context& context)
{
    return bound(context.fps, 1, 1.0e6, 30);
}

double timeInSeconds(const Context& context)
{
    return std::isfinite(context.timeFrames) ? context.timeFrames / fps(context) : 0;
}

Pixel composite(Pixel source, Color light, int blend)
{
    const float peak = std::max({light.r, light.g, light.b});
    if (peak <= 0) {
        return source;
    }
    const auto channel = [blend](float base, float emission) {
        if (blend == 1) {
            // Screen display-range light while keeping any HDR excess additive.
            return base + emission - std::clamp(base, 0.0f, 1.0f) * std::min(emission, 1.0f);
        }
        return base + emission;
    };
    return {channel(source.r, light.r), channel(source.g, light.g), channel(source.b, light.b),
        source.a + (1.0f - std::clamp(source.a, 0.0f, 1.0f)) * std::min(peak, 1.0f)};
}

struct LightRow {
    const Space& space;
    Rect window;
    std::vector<Color>& pixels;

    template <typename Draw>
    void drawBetween(double left, double right, const Draw& draw)
    {
        const int begin = std::max(window.x1, space.frame.x1);
        const int end = std::min(window.x2, space.frame.x2);
        if (begin >= end || right < space.x(begin) || left > space.x(end - 1)) {
            return;
        }
        const double first = std::max(static_cast<double>(begin), left / space.pixelAspect + space.frame.x1 - .5);
        const double last = std::min(end - 1.0, right / space.pixelAspect + space.frame.x1 - .5);
        for (int x = static_cast<int>(std::ceil(first)); x <= static_cast<int>(std::floor(last)); ++x) {
            draw(pixels[static_cast<std::size_t>(x - window.x1)], space.x(x));
        }
    }
};

void accumulate(Color& light, Color color, double power)
{
    const auto scale = static_cast<float>(power);
    light.r += color.r * scale;
    light.g += color.g * scale;
    light.b += color.b * scale;
}

template <typename DrawRow>
void renderLights(const Context& context, const Space& space, Color color, int blend, bool lightsOnly,
                  const DrawRow& drawRow)
{
    const Rect window = context.output.bounds;
    if (width(window) == 0) {
        return;
    }
    parallelBands(window.y1, window.y2, [&](int first, int last) {
        std::vector<Color> emission(static_cast<std::size_t>(width(window)));
        LightRow row{space, window, emission};
        for (int y = first; y < last && !context.cancelled(); ++y) {
            std::fill(emission.begin(), emission.end(), Color{});
            if (y >= space.frame.y1 && y < space.frame.y2) {
                drawRow(row, space.y(y));
            }
            for (int x = window.x1; x < window.x2; ++x) {
                const Color light = emission[static_cast<std::size_t>(x - window.x1)];
                const Pixel base = lightsOnly ? Pixel{} : context.source.read(x, y);
                context.output.pixel(x, y) = composite(base, {color.r * light.r, color.g * light.g,
                    color.b * light.b}, blend);
            }
        }
    });
}

void passThrough(const Context& context, bool only)
{
    const Rect window = context.output.bounds;
    forEachRow(context, window.y1, window.y2, [&](int y) {
        for (int x = window.x1; x < window.x2; ++x) {
            context.output.pixel(x, y) = only ? Pixel{} : context.source.read(x, y);
        }
    });
}

double highlightEnergy(Pixel pixel, double threshold, double knee, double alphaWeight)
{
    const double luma = 0.2126 * pixel.r + 0.7152 * pixel.g + 0.0722 * pixel.b;
    const double signal = (1 - alphaWeight) * bound(luma, 0, 64) + alphaWeight * bound(pixel.a, 0, 1);
    const double excess = signal - threshold;
    const double kneeInput = std::clamp(excess + knee, 0.0, 2.0 * knee);
    return std::min(signal, std::max(excess, knee > 0 ? (kneeInput / (4 * knee)) * kneeInput : 0.0));
}

struct Highlight {
    Point center;
    double power = 0;
    Color color;
};

std::vector<Highlight> findHighlights(const Context& context, const Space& space,
                                      const GlintRainbowSettings& settings)
{
    const Rect frame = space.frame;
    const int cellY = std::max(1, height(frame) / 40);
    const int cellX = static_cast<int>(std::clamp(std::round(cellY / space.pixelAspect),
        1.0, static_cast<double>(width(frame))));
    const int columns = 1 + (width(frame) - 1) / cellX;
    const int rows = 1 + (height(frame) - 1) / cellY;
    const double threshold = bound(settings.threshold, 0, 4);
    const double knee = bound(settings.knee, 0, 2);
    const double alphaWeight = bound(settings.alphaWeight, 0, 1);
    std::vector<Highlight> candidates(static_cast<std::size_t>(columns) * rows);
    forEachRow(context, 0, rows, [&](int row) {
        const int y0 = frame.y1 + row * cellY;
        const int y1 = y0 + std::min(cellY, frame.y2 - y0);
        for (int column = 0; column < columns; ++column) {
            const int x0 = frame.x1 + column * cellX;
            const int x1 = x0 + std::min(cellX, frame.x2 - x0);
            double peak = 0, total = 0;
            int peakX = x0, peakY = y0;
            Pixel peakPixel{};
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    const Pixel pixel = context.source.read(x, y);
                    const double energy = highlightEnergy(pixel, threshold, knee, alphaWeight);
                    total += energy;
                    if (energy > peak) {
                        peak = energy;
                        peakX = x;
                        peakY = y;
                        peakPixel = pixel;
                    }
                }
            }
            if (peak <= 1.0e-5) {
                continue;
            }
            const double area = static_cast<double>(x1 - x0) * (y1 - y0);
            const double isolation = area > 1 ? smooth(2 * (1 - total / (area * peak))) : 1;
            const double largest = std::max({bound(peakPixel.r, 0, 64), bound(peakPixel.g, 0, 64),
                bound(peakPixel.b, 0, 64), 0.01});
            const Color color = mix({1, 1, 1}, tint(peakPixel.r / largest, peakPixel.g / largest,
                peakPixel.b / largest), static_cast<float>(.32 * (1 - alphaWeight)));
            candidates[static_cast<std::size_t>(row) * columns + column] = {
                {space.x(peakX), space.y(peakY)}, isolation * std::sqrt(peak), color};
        }
    });
    if (context.cancelled()) {
        return {};
    }
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), [](const Highlight& light) {
        return light.power <= 1.0e-5;
    }), candidates.end());
    std::stable_sort(candidates.begin(), candidates.end(), [](const Highlight& a, const Highlight& b) {
        return a.power > b.power;
    });

    std::vector<Highlight> selected;
    constexpr std::size_t limit = 96;
    selected.reserve(std::min(limit, candidates.size()));
    const double separation = std::max(1.0, space.extentY() * .0125);
    for (const auto& candidate : candidates) {
        const bool neighbor = std::any_of(selected.begin(), selected.end(), [&](const Highlight& other) {
            const double dx = candidate.center.x - other.center.x;
            const double dy = candidate.center.y - other.center.y;
            return dx * dx + dy * dy < separation * separation;
        });
        if (!neighbor) {
            selected.push_back(candidate);
            if (selected.size() == limit) {
                break;
            }
        }
    }
    return selected;
}

struct Ray {
    Point center, direction;
    Color color;
    double length, width, power;
};

void drawRay(LightRow& row, double y, const Ray& ray, double dispersion, double hue)
{
    const double dy = y - ray.center.y;
    const double endY = ray.length * ray.direction.y;
    if (dy < std::min(0.0, endY) - ray.width || dy > std::max(0.0, endY) + ray.width) {
        return;
    }
    const double endX = ray.length * ray.direction.x;
    double left = std::min(0.0, endX) - ray.width;
    double right = std::max(0.0, endX) + ray.width;
    if (std::abs(ray.direction.y) > 1.0e-6) {
        const double mid = ray.direction.x * dy / ray.direction.y;
        const double half = ray.width / std::abs(ray.direction.y);
        left = std::max(left, mid - half);
        right = std::min(right, mid + half);
    }
    if (left > right) {
        return;
    }
    row.drawBetween(ray.center.x + left, ray.center.x + right, [&](Color& light, double x) {
        const double dx = x - ray.center.x;
        const double along = dx * ray.direction.x + dy * ray.direction.y;
        if (along < -ray.width || along >= ray.length) {
            return;
        }
        const double distance = std::max(0.0, along / ray.length);
        const double offAxis = std::abs(dx * ray.direction.y - dy * ray.direction.x);
        const double profile = 1.0 - distance;
        const double across = smooth(1 - offAxis / (ray.width * (.45 + .55 * profile)));
        const double nearEnd = along >= 0 ? 1 : smooth(1 + along / ray.width);
        const auto colorMix = static_cast<float>(dispersion * smooth(distance * 8));
        const Color color = colorMix > 0 ? mix(ray.color, spectrum(hue + distance * (.35 + .65 * dispersion)), colorMix)
            : ray.color;
        accumulate(light, color, ray.power * profile * profile * across * nearEnd);
    });
}

struct Disc {
    Point center;
    double radius, power;
    Color color;
};

double wrapped(double value, double period)
{
    const double cycles = std::floor(value / period);
    return value - period * cycles;
}

double apertureDistance(double x, double y, const std::vector<Point>& normals)
{
    if (normals.empty()) {
        return std::sqrt(x * x + y * y);
    }
    double result = 0;
    for (const Point normal : normals) {
        result = std::max(result, x * normal.x + y * normal.y);
    }
    return result;
}

// Integral of a fast attack and an exponential decay over the frame shutter.
double flashIntegral(double normalizedAge, double attack, double decay)
{
    const double age = std::clamp(normalizedAge, 0.0, 1.0);
    if (age <= attack) {
        return age * age / (2.0 * attack);
    }
    const double tail = age - attack;
    const double floor = std::exp(-decay * (1 - attack));
    return .5 * attack + (-std::expm1(-decay * tail) / decay - floor * tail) / (1 - floor);
}

} // namespace

void renderGlintRainbow(const Context& context, const GlintRainbowSettings& settings)
{
    const Space space(context);
    const double length = bound(settings.length, 0, 35) * space.extentY() / 100;
    const double intensity = bound(settings.intensity, 0, 10);
    if (length <= 0 || intensity <= 0 || space.extentX() <= 0) {
        passThrough(context, settings.lightsOnly);
        return;
    }
    const auto centers = findHighlights(context, space, settings);
    if (context.cancelled()) {
        return;
    }
    if (centers.empty()) {
        passThrough(context, settings.lightsOnly);
        return;
    }
    const int count = std::clamp(settings.rays, 2, 12);
    const double rotation = bound(settings.rotation, -180, 180) * pi / 180;
    const double width = std::max(.65, bound(settings.width, .25, 20, 2.5) * space.extentY() / 1080);
    std::vector<Ray> rays;
    rays.reserve(centers.size() * static_cast<std::size_t>(count));
    for (const auto& center : centers) {
        for (int arm = 0; arm < count; ++arm) {
            const double angle = rotation + tau * arm / count;
            const bool primary = count >= 8 && arm % 2 == 0;
            const double span = length * (count < 8 || primary ? 1.0 : .68);
            const double power = center.power * intensity * (primary ? 1.12 : .86)
                / std::sqrt(static_cast<double>(count));
            rays.push_back({center.center, {std::cos(angle), std::sin(angle)}, center.color, span, width, power});
        }
    }
    const double dispersion = bound(settings.dispersion, 0, 1);
    const double hue = bound(settings.hue, -1, 1);
    renderLights(context, space, tint(settings.red, settings.green, settings.blue), settings.blend,
        settings.lightsOnly, [&](LightRow& row, double y) {
            for (const Ray& ray : rays) {
                drawRay(row, y, ray, dispersion, hue);
            }
        });
}

void renderBokehLights(const Context& context, const BokehLightsSettings& settings)
{
    const Space space(context);
    const int count = std::clamp(settings.count, 0, 200);
    const double intensity = bound(settings.intensity, 0, 10);
    if (count == 0 || intensity == 0 || space.extentX() <= 0 || space.extentY() <= 0) {
        passThrough(context, settings.lightsOnly);
        return;
    }

    const double radius = bound(settings.size, .1, 35, 5) * space.extentY() / 100;
    const double sizeVariation = bound(settings.sizeVariation, 0, 1);
    const double softness = bound(settings.softness, 0, 1);
    const double aspect = bound(settings.aspect, .25, 4, 1);
    const double drift = bound(settings.drift, 0, 50) * space.extentY() / 100;
    const double frequency = bound(settings.speed, -5, 5);
    const double clock = timeInSeconds(context);
    const double flicker = bound(settings.flicker, 0, 1);
    const double hueVariation = bound(settings.hueVariation, 0, 1);
    const double rotation = bound(settings.rotation, -180, 180) * pi / 180;
    const int blades = std::clamp(settings.blades, 0, 12);
    std::vector<Point> normals;
    if (blades >= 3) {
        for (int blade = 0; blade < blades; ++blade) {
            const double angle = rotation + tau * blade / blades;
            const double inverseApothem = 1 / std::cos(pi / blades);
            normals.push_back({std::cos(angle) * inverseApothem, std::sin(angle) * inverseApothem});
        }
    }

    std::vector<Disc> lights;
    lights.reserve(static_cast<std::size_t>(count));
    const auto seed = static_cast<std::uint32_t>(settings.seed);
    for (int index = 0; index < count; ++index) {
        const auto key = hash(seed ^ hash(static_cast<std::uint32_t>(index) + 1));
        const double speed = frequency * (.64 + .73 * random(key, 9));
        const double cycles = speed == 0 ? 0 : std::remainder(clock, 1 / std::abs(speed)) * speed;
        const double angularPhase = tau * cycles;
        const double phaseX = tau * random(key, 1);
        const double phaseY = tau * random(key, 2);
        const double size = radius * (1 + sizeVariation * (1.75 * random(key, 3) - .875));
        const double margin = size * std::max(1.0, aspect);
        const double spanX = space.extentX() + 2 * margin;
        const double spanY = space.extentY() + 2 * margin;
        const double dx = drift * (.8 * std::sin(angularPhase + phaseX) + .2 * std::sin(2 * angularPhase + phaseY));
        const double dy = drift * (.72 * std::cos(angularPhase + phaseY) + .28 * std::sin(3 * angularPhase + phaseX));
        const Point center{wrapped(random(key, 4) * spanX + dx, spanX) - margin,
            wrapped(random(key, 5) * spanY + dy, spanY) - margin};
        const double shimmer = 1 - flicker * (.5 + .5 * std::sin(5 * angularPhase + tau * random(key, 6)));
        const double power = intensity * (.24 + .76 * random(key, 7)) * shimmer;
        const Color palette = mix({1, 1, 1}, spectrum(random(key, 8)), static_cast<float>(hueVariation));
        lights.push_back({center, size, power, palette});
    }

    renderLights(context, space, tint(settings.red, settings.green, settings.blue), settings.blend,
        settings.lightsOnly, [&](LightRow& row, double y) {
            for (const Disc& light : lights) {
                const double dy = (y - light.center.y) / (light.radius * aspect);
                if (std::abs(dy) > 1) {
                    continue;
                }
                const double transition = std::clamp(.02 + softness * .75 + .65 / light.radius, .03, 1.0);
                const double span = normals.empty() ? light.radius * std::sqrt(std::max(0.0, 1 - dy * dy)) : light.radius;
                row.drawBetween(light.center.x - span, light.center.x + span, [&](Color& pixel, double x) {
                    const double dx = (x - light.center.x) / light.radius;
                    const double distance = apertureDistance(dx, dy, normals);
                    const double coverage = smooth((1 - distance) / transition);
                    const double rim = smooth((distance - .58) * 3.5) * (1 - softness);
                    const double center = 1 - .22 * std::clamp(distance * distance, 0.0, 1.0);
                    accumulate(pixel, light.color, light.power * coverage * (center + .23 * rim));
                });
            }
        });
}

void renderFlashbulbs(const Context& context, const FlashbulbsSettings& settings)
{
    const Space space(context);
    const double rate = bound(settings.rate, 0, 80);
    const double intensity = bound(settings.intensity, 0, 10);
    if (rate < 1.0e-8 || intensity == 0 || space.extentX() <= 0 || space.extentY() <= 0) {
        passThrough(context, settings.lightsOnly);
        return;
    }

    const double duration = bound(settings.duration, .01, 1, .105);
    const double trail = bound(settings.trail, 0, 1);
    const double exposure = 1 / fps(context);
    const double slot = std::remainder(timeInSeconds(context), 4294967296.0 / rate) * rate;
    const double open = slot - .5 * exposure * rate;
    const double close = slot + .5 * exposure * rate;
    const auto first = static_cast<std::int64_t>(std::floor(open - duration * rate)) - 1;
    const auto last = static_cast<std::int64_t>(std::floor(close));
    const double size = bound(settings.size, .1, 30, 3) * space.extentY() / 100;
    const double variation = bound(settings.sizeVariation, 0, 1);
    const double centerX = bound(settings.centerX, -1, 2, .5) * space.extentX();
    const double centerY = bound(settings.centerY, -1, 2, .57) * space.extentY();
    const double spreadX = bound(settings.spreadX, 0, 2) * space.extentX();
    const double spreadY = bound(settings.spreadY, 0, 2) * space.extentY();
    const auto seed = static_cast<std::uint32_t>(settings.seed);
    std::vector<Disc> lights;
    for (std::int64_t event = first; event <= last; ++event) {
        const auto key = hash(seed ^ hash(static_cast<std::uint32_t>(event)));
        const double start = static_cast<double>(event) + .08 + .84 * random(key, 1);
        const double lifetime = duration * (.70 + .30 * random(key, 2));
        constexpr double attack = .065;
        const double decay = 8.6 - 6.0 * trail;
        const double lag = (open - start) / (rate * lifetime);
        const double end = (close - start) / (rate * lifetime);
        const double power = (flashIntegral(end, attack, decay) - flashIntegral(lag, attack, decay))
            * (lifetime / exposure) * intensity * (.42 + 1.12 * random(key, 3));
        if (power <= 1.0e-4) {
            continue;
        }
        const Point center{centerX + (random(key, 4) - .5) * spreadX,
            centerY + (random(key, 5) - .5) * spreadY};
        const double radius = size * (1 + variation * (1.8 * random(key, 6) - .9));
        const Color color = mix({.8f, .9f, 1}, {1, .97f, .88f}, static_cast<float>(random(key, 7)));
        lights.push_back({center, std::max(.75, radius), power, color});
    }
    if (lights.empty()) {
        passThrough(context, settings.lightsOnly);
        return;
    }

    const int style = std::clamp(settings.style, 0, 2);
    const double rotation = bound(settings.rotation, -180, 180) * pi / 180;
    const double sine = std::sin(rotation), cosine = std::cos(rotation);
    renderLights(context, space, tint(settings.red, settings.green, settings.blue), settings.blend,
        settings.lightsOnly, [&](LightRow& row, double y) {
            for (const Disc& light : lights) {
                const double dy = (y - light.center.y) / light.radius;
                if (std::abs(dy) >= 1) {
                    continue;
                }
                const double span = light.radius * std::sqrt(1 - dy * dy);
                row.drawBetween(light.center.x - span, light.center.x + span, [&](Color& pixel, double x) {
                    const double dx = (x - light.center.x) / light.radius;
                    const double distanceSquared = dx * dx + dy * dy;
                    const double extent = smooth(1 - distanceSquared);
                    const double blur = style == 2 ? .20 : .06;
                    const double core = blur / (blur + distanceSquared * 3);
                    double rays = 0;
                    if (style != 2) {
                        const double u = std::abs(cosine * dx + sine * dy);
                        const double v = std::abs(cosine * dy - sine * dx);
                        const double width = std::clamp(.035 + .55 / light.radius, .035, .42);
                        rays = .25 * smooth(1 - std::min(u, v) / width);
                        if (style == 1) {
                            rays += .20 * smooth(1 - std::abs(u - v) / (1.41421356237 * width));
                        }
                    }
                    const double halo = .085 / (1 + 8 * distanceSquared);
                    accumulate(pixel, light.color, light.power * extent * (core + halo + rays * (.6 + trail)));
                });
            }
        });
}

void renderSpotLight(const Context& context, const SpotLightSettings& settings)
{
    const Space space(context);
    if (space.extentX() <= 0 || space.extentY() <= 0) {
        passThrough(context, settings.lightOnly);
        return;
    }

    const Point origin{bound(settings.lightX, -2, 3, .35) * space.extentX(),
        bound(settings.lightY, -2, 3, .80) * space.extentY()};
    const double z = bound(settings.height, 1, 400, 75) * space.extentY() / 100;
    const Point aim{bound(settings.aimX, -2, 3, .53) * space.extentX() - origin.x,
        bound(settings.aimY, -2, 3, .44) * space.extentY() - origin.y};
    const double axisSquared = aim.x * aim.x + aim.y * aim.y + z * z;
    const double inverseAxis = 1 / std::sqrt(axisSquared);
    const double opening = bound(settings.angle, 1, 160, 42) * pi / 360;
    const double softness = bound(settings.softness, 0, 1);
    const double outer = std::cos(opening);
    const double inner = std::cos(opening * (1 - .82 * softness));
    const double edgeRange = inner - outer;
    const double falloff = bound(settings.falloff, 0, 2);
    const double ambient = bound(settings.ambient, 0, 4);
    const double inside = bound(settings.inside, 0, 8);
    const double outside = bound(settings.outside, 0, 8);
    const auto color = tint(settings.red, settings.green, settings.blue);
    const Rect window = context.output.bounds;
    forEachRow(context, window.y1, window.y2, [&](int y) {
        const double dy = space.y(y) - origin.y;
        const double yDistance = dy * dy + z * z;
        const double yDot = dy * aim.y + z * z;
        for (int x = window.x1; x < window.x2; ++x) {
            const double dx = space.x(x) - origin.x;
            const double distanceSquared = dx * dx + yDistance;
            const double cosine = (dx * aim.x + yDot) * inverseAxis / std::sqrt(distanceSquared);
            const double beam = edgeRange > 0 ? smooth((cosine - outer) / edgeRange) : (cosine >= outer ? 1.0 : 0.0);
            const double attenuation = 1 / (1 + falloff * std::max(0.0, distanceSquared / axisSquared - 1));
            const auto baseline = static_cast<float>(ambient + outside * (1 - beam));
            const auto light = static_cast<float>(inside * attenuation * beam);
            const Color channels{baseline + color.r * light, baseline + color.g * light, baseline + color.b * light};
            if (settings.lightOnly) {
                context.output.pixel(x, y) = {channels.r, channels.g, channels.b,
                    std::min(1.0f, std::max({channels.r, channels.g, channels.b}))};
            } else {
                const Pixel source = context.source.read(x, y);
                context.output.pixel(x, y) = {source.r * channels.r, source.g * channels.g, source.b * channels.b, source.a};
            }
        }
    });
}

} // namespace diamond::fx
