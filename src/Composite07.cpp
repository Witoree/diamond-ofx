#include "Composite07.h"

#include <cstdint>
#include <limits>
#include <new>

namespace diamond::fx {
namespace {

double bounded(double value, double minimum, double maximum, double fallback = 0.0) {
    return std::clamp(std::isfinite(value) ? value : fallback, minimum, maximum);
}

float unit(double value) {
    return static_cast<float>(bounded(value, 0.0, 1.0));
}

double pixelAspect(const Context& context) {
    return bounded(context.pixelAspect, 0.01, 100.0, 1.0);
}

template <typename Evaluate>
void draw(const Context& context, const Evaluate& evaluate) {
    const Rect area = context.output.bounds;
    forEachRow(context, area.y1, area.y2, [&](int y) {
        for (int x = area.x1; x < area.x2; ++x) {
            context.output.pixel(x, y) = evaluate(x, y);
        }
    });
}

void copySource(const Context& context) {
    draw(context, [&](int x, int y) { return context.source.read(x, y); });
}

Pixel mixRgb(Pixel source, Pixel result, float mix) {
    if (mix < 1.0f) {
        result.r = source.r * (1.0f - mix) + result.r * mix;
        result.g = source.g * (1.0f - mix) + result.g * mix;
        result.b = source.b * (1.0f - mix) + result.b * mix;
    }
    result.a = source.a;
    return result;
}

struct Box {
    int radius, support;
    double wing, normalization;
};

Box gaussianBox(double sigma) {
    // Match the Gaussian's variance with three equal boxes and a fractional
    // outer tap on each end, so the radius can animate continuously.
    const double variance = sigma * sigma / 3.0;
    const double taps = (std::sqrt(1.0 + 12.0 * variance) - 1.0) * 0.5;
    if (taps >= std::numeric_limits<int>::max() / 6) {
        throw std::bad_alloc{};
    }
    const auto radius = static_cast<int>(taps);
    const double count = 2.0 * radius + 1.0;
    const double outer = radius + 1.0;
    const double wing = std::clamp(count * (variance - radius * outer / 3.0)
        / (2.0 * (outer * outer - variance)), 0.0, 1.0);
    return {radius, radius + (wing > 0.0 ? 1 : 0), wing, 1.0 / (count + 2.0 * wing)};
}

class BlurLine {
public:
    BlurLine(int count, Box box) : box_(box), count_(count), pad_(3 * box.support),
        values_(static_cast<std::size_t>(count) + 2ULL * pad_), scratch_(values_.size()) {}

    template <typename Read, typename Write>
    void run(const Read& read, const Write& write) {
        for (std::size_t i = 0; i < values_.size(); ++i) {
            values_[i] = read(static_cast<std::int64_t>(i) - pad_);
        }
        std::size_t active = values_.size();
        for (int pass = 0; pass < 3 && box_.support > 0; ++pass) {
            active -= 2ULL * box_.support;
            const int first = box_.support - box_.radius;
            const int last = box_.support + box_.radius;
            double sum = 0.0;
            for (int i = first; i <= last; ++i) {
                sum += values_[i];
            }
            for (std::size_t i = 0; i < active; ++i) {
                const double wings = static_cast<double>(values_[i]) + values_[i + 2ULL * box_.support];
                scratch_[i] = static_cast<float>((sum + box_.wing * wings) * box_.normalization);
                if (i + 1 < active) {
                    sum += static_cast<double>(values_[i + last + 1]) - values_[i + first];
                }
            }
            values_.swap(scratch_);
        }
        for (int i = 0; i < count_; ++i) {
            write(i, values_[i]);
        }
    }

private:
    Box box_;
    int count_, pad_;
    std::vector<float> values_, scratch_;
};

template <typename Read>
std::vector<float> blurField(const Context& context, double radius, const Read& read) {
    const Rect area = context.output.bounds;
    const int columns = width(area);
    const int rows = height(area);
    if (columns == 0 || rows == 0) {
        return {};
    }
    const auto horizontal = gaussianBox(0.5 * radius / pixelAspect(context));
    const auto vertical = gaussianBox(0.5 * radius);
    const int pad = 3 * vertical.support;
    if (rows > std::numeric_limits<int>::max() - 2 * pad) {
        throw std::bad_alloc{};
    }
    const int bufferedRows = rows + 2 * pad;
    std::vector<float> intermediate(static_cast<std::size_t>(columns) * bufferedRows);
    parallelBands(0, bufferedRows, [&](int first, int last) {
        BlurLine blur(columns, horizontal);
        for (int row = first; row < last && !context.cancelled(); ++row) {
            const auto y = static_cast<std::int64_t>(area.y1) + row - pad;
            auto* dest = intermediate.data() + static_cast<std::size_t>(row) * columns;
            blur.run([&](std::int64_t x) { return read(area.x1 + x, y); },
                     [&](int x, float value) { dest[x] = value; });
        }
    });
    if (context.cancelled()) {
        return {};
    }

    std::vector<float> result(static_cast<std::size_t>(columns) * rows);
    parallelBands(0, columns, [&](int first, int last) {
        BlurLine blur(rows, vertical);
        for (int x = first; x < last && !context.cancelled(); ++x) {
            blur.run([&](std::int64_t y) {
                return intermediate[static_cast<std::size_t>(y + pad) * columns + x];
            }, [&](int y, float value) { result[static_cast<std::size_t>(y) * columns + x] = value; });
        }
    });
    return result;
}

std::size_t indexAt(Rect area, int x, int y) {
    return static_cast<std::size_t>(y - area.y1) * width(area) + (x - area.x1);
}

struct Direction {
    double x, y, z;
};

Direction normalize(Direction value) {
    const double size = value.x * value.x + value.y * value.y + value.z * value.z;
    if (!std::isfinite(size) || size < 1e-12) {
        return {0, 0, 1};
    }
    const double inverse = 1.0 / std::sqrt(size);
    return {value.x * inverse, value.y * inverse, value.z * inverse};
}

double dot(Direction a, Direction b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

struct NormalMap {
    const Image* image;
    Rect frame;
    double strength;
    bool flipGreen, signedNormals;

    Direction at(int x, int y) const {
        if (!image || strength == 0.0 || width(image->bounds) == 0 || height(image->bounds) == 0) {
            return {0, 0, 1};
        }
        const Rect bounds = image->bounds;
        const auto sourceX = bounds.x1 + (x - static_cast<double>(frame.x1) + 0.5)
            * width(bounds) / width(frame) - 0.5;
        const auto sourceY = bounds.y1 + (y - static_cast<double>(frame.y1) + 0.5)
            * height(bounds) / height(frame) - 0.5;
        const Pixel sample = image->sample(sourceX, sourceY, Edge::Clamp);
        const double alpha = unit(sample.a);
        if (alpha < 1e-6) {
            return {0, 0, 1};
        }

        // The working image is premultiplied, including data imported as
        // straight RGBA. Recover the vector before decoding its components.
        const double factor = (signedNormals ? 1.0 : 2.0) / sample.a;
        const double offset = signedNormals ? 0.0 : 1.0;
        const double lateral = std::max(1.0, strength);
        const Direction mapped = normalize({(sample.r * factor - offset) * lateral,
            (sample.g * factor - offset) * lateral * (flipGreen ? -1.0 : 1.0), sample.b * factor - offset});
        const double weight = std::min(1.0, strength) * alpha;
        return normalize({mapped.x * weight, mapped.y * weight, 1.0 + (mapped.z - 1.0) * weight});
    }
};

double luminance(Pixel value) {
    return 0.2126 * value.r + 0.7152 * value.g + 0.0722 * value.b;
}

float lumaAt(const Image& image, std::int64_t x, std::int64_t y) {
    const Rect bounds = image.bounds;
    if (width(bounds) == 0 || height(bounds) == 0) {
        return 0.0f;
    }
    const auto cx = static_cast<int>(std::clamp(x, std::int64_t{bounds.x1}, std::int64_t{bounds.x2} - 1));
    const auto cy = static_cast<int>(std::clamp(y, std::int64_t{bounds.y1}, std::int64_t{bounds.y2} - 1));
    const Pixel source = image.read(cx, cy);
    const double straightLuma = source.a > 1e-6f ? luminance(source) / source.a : 0.0;
    return static_cast<float>(bounded(straightLuma, 0.0, std::numeric_limits<float>::max()));
}

float highlightWeight(double value, double threshold, double knee) {
    if (knee == 0.0) {
        return value > threshold ? 1.0f : 0.0f;
    }
    const auto t = unit((value - threshold) / knee + 0.5);
    return t * t * (3.0f - 2.0f * t);
}

} // namespace

void renderDropShadow(const Context& context, const DropShadowSettings& settings) {
    const auto opacity = unit(settings.opacity);
    if (opacity == 0.0f || width(context.source.bounds) == 0 || height(context.source.bounds) == 0) {
        if (!settings.shadowOnly) {
            copySource(context);
        }
        return;
    }
    const Rect area = context.output.bounds;
    const double scale = 0.01 * height(context.frame);
    const double xOffset = scale * bounded(settings.offsetX, -100.0, 100.0) / pixelAspect(context);
    const double yOffset = scale * bounded(settings.offsetY, -100.0, 100.0);
    const double radius = scale * bounded(settings.radius, 0.0, 20.0);
    const auto spread = 0.9f * unit(settings.spread);
    const auto tintR = static_cast<float>(bounded(settings.red, 0.0, 8.0));
    const auto tintG = static_cast<float>(bounded(settings.green, 0.0, 8.0));
    const auto tintB = static_cast<float>(bounded(settings.blue, 0.0, 8.0));
    const auto shiftedAlpha = [&](std::int64_t x, std::int64_t y) {
        return unit(context.source.sample(x - xOffset, y - yOffset).a);
    };
    const auto coverage = radius > 0.0 ? blurField(context, radius, shiftedAlpha) : std::vector<float>{};
    if (context.cancelled()) {
        return;
    }
    draw(context, [&](int x, int y) {
        const float matte = coverage.empty() ? shiftedAlpha(x, y) : unit(coverage[indexAt(area, x, y)]);
        // Raise the penumbra while leaving fully covered and empty pixels fixed.
        const float shadow = opacity * matte / (1.0f - spread * (1.0f - matte));
        Pixel foreground = settings.shadowOnly ? Pixel{} : context.source.read(x, y);
        const float visible = shadow * (1.0f - unit(foreground.a));
        const float alpha = foreground.a + visible;
        return Pixel{foreground.r + tintR * visible, foreground.g + tintG * visible,
                     foreground.b + tintB * visible, alpha};
    });
}

void renderLight3D(const Context& context, const Light3DSettings& settings, const Image* normals) {
    const auto mix = unit(settings.mix);
    const auto frameWidth = width(context.frame);
    const auto frameHeight = height(context.frame);
    if (mix == 0.0f || frameWidth == 0 || frameHeight == 0) {
        copySource(context);
        return;
    }

    const auto& frame = context.frame;
    const double centerX = frame.x1 + bounded(settings.centerX, -1.0, 2.0, 0.5) * (frameWidth - 1);
    const double centerY = frame.y1 + bounded(settings.centerY, -1.0, 2.0, 0.5) * (frameHeight - 1);
    const double elevation = bounded(settings.elevation, 0.04, 8.0, 0.75) * frameHeight;
    const double aspect = pixelAspect(context);
    const double falloff = bounded(settings.falloff, 0.0, 4.0) / (elevation * elevation);
    const double roughness = bounded(settings.roughness, 0.05, 1.0, 0.36);
    const double shininess = 2.0 / (roughness * roughness);
    const double ambient = bounded(settings.ambient, 0.0, 2.0);
    const double diffuse = bounded(settings.diffuse, 0.0, 4.0);
    const double specular = bounded(settings.specular, 0.0, 4.0);
    const double red = bounded(settings.red, 0.0, 8.0);
    const double green = bounded(settings.green, 0.0, 8.0);
    const double blue = bounded(settings.blue, 0.0, 8.0);
    const NormalMap map{normals, frame, bounded(settings.normalStrength, 0.0, 4.0),
                        settings.flipGreen, settings.signedNormals};
    draw(context, [&](int x, int y) {
        const Pixel source = context.source.read(x, y);
        if (!(source.a > 0.0f)) {
            return Pixel{0, 0, 0, source.a};
        }
        const double dx = (centerX - x) * aspect;
        const double dy = centerY - y;
        const double attenuation = 1.0 / (1.0 + (dx * dx + dy * dy) * falloff);
        const Direction light = normalize({dx, dy, elevation});
        const Direction normal = map.at(x, y);
        const double facing = std::max(0.0, dot(normal, light));
        const double direct = diffuse * facing * attenuation;
        double shine = 0.0;
        if (facing > 0.0 && normal.z > 0.0 && specular > 0.0) {
            const Direction halfway = normalize({light.x, light.y, light.z + 1.0});
            const double highlight = std::clamp(dot(normal, halfway), 0.0, 1.0);
            shine = specular * std::pow(highlight, shininess) * facing * attenuation;
        }

        const auto channel = [&](double albedo, double lightColor) {
            return static_cast<float>(albedo * (ambient + lightColor * direct) + source.a * lightColor * shine);
        };
        const Pixel lit = settings.lightingOnly
            ? Pixel{channel(source.a, red), channel(source.a, green), channel(source.a, blue), source.a}
            : Pixel{channel(source.r, red), channel(source.g, green), channel(source.b, blue), source.a};
        return mixRgb(source, lit, mix);
    });
}

void renderHotspots(const Context& context, const HotspotsSettings& settings) {
    const auto mix = unit(settings.mix);
    if (mix == 0.0f) {
        copySource(context);
        return;
    }
    const Rect area = context.output.bounds;
    const double threshold = bounded(settings.threshold, 0.0, 8.0);
    const double knee = bounded(settings.knee, 0.0, 2.0);
    const auto brightness = static_cast<float>(bounded(settings.brightness, 0.0, 8.0));
    const double saturation = bounded(settings.saturation, 0.0, 3.0);
    const double radius = 0.01 * height(context.frame) * bounded(settings.smoothing, 0.0, 5.0);
    const auto luma = [&](std::int64_t x, std::int64_t y) { return lumaAt(context.source, x, y); };
    const auto blurred = radius > 0.0 ? blurField(context, radius, luma) : std::vector<float>{};
    if (context.cancelled()) {
        return;
    }
    draw(context, [&](int x, int y) {
        const Pixel source = context.source.read(x, y);
        if (!(source.a > 0.0f)) {
            return Pixel{0, 0, 0, source.a};
        }
        const double gray = luminance(source);
        const double value = blurred.empty() ? gray / source.a : blurred[indexAt(area, x, y)];
        const auto selection = brightness * highlightWeight(value, threshold, knee);
        if (settings.outputMode == 2) {
            const float matte = source.a * unit(selection);
            return mixRgb(source, {matte, matte, matte, source.a}, mix);
        }
        const auto channel = [&](float original) {
            const double color = saturation == 1.0 ? original : gray + (original - gray) * saturation;
            const auto selected = static_cast<float>(color * selection);
            return settings.outputMode == 1 ? original + selected : selected;
        };
        return mixRgb(source, {channel(source.r), channel(source.g), channel(source.b), source.a}, mix);
    });
}

} // namespace diamond::fx
