#include "ImageInImage07.h"

namespace diamond::fx {
namespace {

constexpr double radians = 0.017453292519943295;

PicturePoint operator-(PicturePoint a, PicturePoint b) { return {a.x - b.x, a.y - b.y}; }
double cross(PicturePoint a, PicturePoint b) { return a.x * b.y - a.y * b.x; }

PicturePoint sizeOf(const PictureSettings& settings, double canvasAspect, double sourceAspect) {
    return {settings.scale * std::min(canvasAspect, sourceAspect),
            settings.scale * std::min(1.0, canvasAspect / sourceAspect)};
}

struct Projection {
    std::array<double, 9> inverse{};

    bool fromQuad(const PictureQuad& quad) {
        if (!validPictureQuad(quad)) {
            return false;
        }
        const auto p0 = quad[3];
        const auto p1 = quad[2];
        const auto p2 = quad[1];
        const auto p3 = quad[0];
        const auto first = p1 - p2;
        const auto second = p3 - p2;
        const auto diagonal = (p0 - p1) - (p3 - p2);
        const double denominator = cross(first, second);
        if (std::abs(denominator) < 1.0e-12) {
            return false;
        }
        const double g = cross(diagonal, second) / denominator;
        const double h = cross(first, diagonal) / denominator;
        const double a = p1.x * (1 + g) - p0.x;
        const double b = p3.x * (1 + h) - p0.x;
        const double c = p0.x;
        const double d = p1.y * (1 + g) - p0.y;
        const double e = p3.y * (1 + h) - p0.y;
        const double f = p0.y;
        inverse = {e - f*h, c*h - b, b*f - c*e,
                   f*g - d, a - c*g, c*d - a*f,
                   d*h - e*g, b*g - a*h, a*e - b*d};
        return true;
    }

    PicturePoint sourcePoint(double x, double y) const {
        const double w = inverse[6]*x + inverse[7]*y + inverse[8];
        if (std::abs(w) < 1.0e-12) {
            return {-2, -2};
        }
        return {(inverse[0]*x + inverse[1]*y + inverse[2]) / w,
                (inverse[3]*x + inverse[4]*y + inverse[5]) / w};
    }
};

struct Boundary {
    double dx, dy, constant, halfPixel;
    double distance(double x, double y) const { return dx*x + dy*y + constant; }
};

std::array<Boundary, 4> boundaries(const PictureQuad& quad, double columns, double rows, double par) {
    std::array<Boundary, 4> edges{};
    const double displayWidth = columns * par;
    const double inward = cross(quad[1] - quad[0], quad[2] - quad[1]) < 0 ? -1.0 : 1.0;
    for (std::size_t i = 0; i < quad.size(); ++i) {
        const auto point = quad[i];
        const auto vector = quad[(i + 1) % quad.size()] - point;
        const double length = std::hypot(vector.x * displayWidth, vector.y * rows);
        const double dx = -inward * vector.y * rows * displayWidth / length;
        const double dy = inward * vector.x * displayWidth * rows / length;
        const double halfPixel = 0.5 * (std::abs(dx)/columns + std::abs(dy)/rows);
        edges[i] = {dx, dy, -dx*point.x - dy*point.y, halfPixel};
    }
    return edges;
}

float coverage(const Boundary& edge, double x, double y, double feather) {
    const double distance = edge.distance(x, y);
    if (feather <= 0) {
        return static_cast<float>(std::clamp((edge.halfPixel + distance) / (2 * edge.halfPixel), 0.0, 1.0));
    }
    const auto t = static_cast<float>(std::clamp((edge.halfPixel + distance) / (edge.halfPixel + feather), 0.0, 1.0));
    return t*t*(3.0f - 2.0f*t);
}

void add(Pixel& result, Pixel sample, float weight) {
    result.r += sample.r * weight;
    result.g += sample.g * weight;
    result.b += sample.b * weight;
    result.a += sample.a * weight;
}

} // namespace

PictureQuad pictureQuad(const PictureSettings& settings, double canvasAspect, double sourceAspect) {
    const auto size = sizeOf(settings, canvasAspect, sourceAspect);
    const double cosine = std::cos(settings.rotation * radians);
    const double sine = std::sin(settings.rotation * radians);
    auto points = settings.perspective ? settings.corners : pictureDefaultCorners;
    for (auto& point : points) {
        const double x = (point.x - 0.5) * size.x;
        const double y = (point.y - 0.5) * size.y;
        point = {settings.center.x + (x*cosine - y*sine) / canvasAspect,
                 settings.center.y + x*sine + y*cosine};
    }
    return points;
}

PicturePoint pictureLocalPoint(const PictureSettings& settings, double canvasAspect, double sourceAspect,
                               PicturePoint canvasPoint) {
    const auto size = sizeOf(settings, canvasAspect, sourceAspect);
    const double cosine = std::cos(settings.rotation * radians);
    const double sine = std::sin(settings.rotation * radians);
    const double x = (canvasPoint.x - settings.center.x) * canvasAspect;
    const double y = canvasPoint.y - settings.center.y;
    return {(x*cosine + y*sine) / size.x + 0.5, (y*cosine - x*sine) / size.y + 0.5};
}

bool validPictureQuad(const PictureQuad& quad) {
    double last = 0;
    for (std::size_t i = 0; i < quad.size(); ++i) {
        const auto direction = quad[(i + 1) % 4] - quad[i];
        const auto next = quad[(i + 2) % 4] - quad[(i + 1) % 4];
        const double turn = cross(direction, next);
        if (!std::isfinite(turn) || std::abs(turn) < 1.0e-10 || turn*last < 0) {
            return false;
        }
        last = turn;
    }
    return true;
}

void renderImageInImage(const Context& context, const PictureSettings& settings) {
    renderImageInImage(context, settings, {context.source.bounds, context.pixelAspect});
}

void renderImageInImage(const Context& context, const PictureSettings& settings, PictureRenderSpace space) {
    const Rect input = space.sourceRegion;
    const Rect frame = context.frame;
    const Rect window = context.output.bounds;
    const double columns = width(frame);
    const double rows = height(frame);
    const int sourceWidth = width(input);
    const int sourceHeight = height(input);
    if (columns <= 0 || rows <= 0 || sourceWidth <= 0 || sourceHeight <= 0
        || width(context.source.bounds) == 0 || height(context.source.bounds) == 0 || settings.opacity <= 0) {
        return;
    }
    const double par = std::clamp(context.pixelAspect, 0.01, 100.0);
    const double sourcePar = std::clamp(space.sourcePixelAspect, 0.01, 100.0);
    const auto quad = pictureQuad(settings, columns*par/rows,
                                  static_cast<double>(sourceWidth)*sourcePar/sourceHeight);
    Projection projection;
    if (!projection.fromQuad(quad)) {
        return;
    }
    const auto edges = boundaries(quad, columns, rows, par);
    const double feather = rows * settings.feather * 0.01;
    const float opacity = static_cast<float>(settings.opacity);
    const auto sample = [&](PicturePoint point) {
        const double srcX = input.x1 + std::clamp(point.x*sourceWidth - 0.5, 0.0, sourceWidth - 1.0);
        const double srcY = input.y1 + std::clamp(point.y*sourceHeight - 0.5, 0.0, sourceHeight - 1.0);
        return context.source.sample(srcX, srcY, Edge::Transparent);
    };
    const auto density = [sourceWidth, sourceHeight](PicturePoint a, PicturePoint b) {
        return std::max(std::abs(a.x-b.x)*sourceWidth, std::abs(a.y-b.y)*sourceHeight);
    };
    forEachRow(context, window.y1, window.y2, [&](int y) {
        const double v = (static_cast<double>(y) - frame.y1 + 0.5) / rows;
        for (int x = window.x1; x < window.x2; ++x) {
            const double u = (static_cast<double>(x) - frame.x1 + 0.5) / columns;
            const float alpha = std::min({coverage(edges[0], u, v, feather), coverage(edges[1], u, v, feather),
                                          coverage(edges[2], u, v, feather), coverage(edges[3], u, v, feather)}) * opacity;
            if (alpha <= 0) {
                continue;
            }
            const auto center = projection.sourcePoint(u, v);
            int taps = 1;
            if (settings.quality != 0) {
                const double scale = std::max(density(center, projection.sourcePoint(u+1/columns, v)),
                                              density(center, projection.sourcePoint(u, v+1/rows)));
                taps = scale >= 4.0 ? 4 : scale >= 2.6 ? 3 : scale > 1.12 ? 2 : 1;
            }
            Pixel result{};
            const float weight = alpha / static_cast<float>(taps*taps);
            for (int ty = 0; ty < taps; ++ty) {
                const double sampleY = v + ((ty+0.5)/taps - 0.5) / rows;
                for (int tx = 0; tx < taps; ++tx) {
                    const double sampleX = u + ((tx+0.5)/taps - 0.5) / columns;
                    add(result, sample(taps == 1 ? center : projection.sourcePoint(sampleX, sampleY)), weight);
                }
            }
            context.output.pixel(x, y) = result;
        }
    });
}

} // namespace diamond::fx
