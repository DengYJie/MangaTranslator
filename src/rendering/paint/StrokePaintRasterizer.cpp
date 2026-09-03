#include "rendering/paint/StrokePaintRasterizer.h"

#include <algorithm>
#include <cmath>

namespace manga::rendering {
namespace {

/// Rasterizes one capsule segment. Both endpoints keep their round cap because coverage is
/// measured against the segment itself, which is what makes incremental and whole-stroke
/// rasterization agree pixel for pixel.
void rasterizeSpan(QImage& image, const StrokePaintParams& params, const QPointF& from, const QPointF& to) {
    const double radius = params.diameter * 0.5;
    const int imgWidth = image.width();
    const int imgHeight = image.height();

    const uchar colorR = static_cast<uchar>(params.color.red());
    const uchar colorG = static_cast<uchar>(params.color.green());
    const uchar colorB = static_cast<uchar>(params.color.blue());
    const uchar colorA = static_cast<uchar>(params.color.alpha());

    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x(), to.x()) - radius - 0.5)));
    const int top = std::max(0, static_cast<int>(std::floor(std::min(from.y(), to.y()) - radius - 0.5)));
    const int right = std::min(imgWidth, static_cast<int>(std::ceil(std::max(from.x(), to.x()) + radius + 0.5)));
    const int bottom = std::min(imgHeight, static_cast<int>(std::ceil(std::max(from.y(), to.y()) + radius + 0.5)));

    const double dx = to.x() - from.x();
    const double dy = to.y() - from.y();
    const double lengthSquared = dx * dx + dy * dy;

    for (int y = top; y < bottom; ++y) {
        uchar* line = image.scanLine(y);
        const double py = static_cast<double>(y) + 0.5;
        for (int x = left; x < right; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            double t = 0.0;
            if (lengthSquared > 0.0) {
                t = std::clamp(((px - from.x()) * dx + (py - from.y()) * dy) / lengthSquared, 0.0, 1.0);
            }
            const double projX = from.x() + t * dx;
            const double projY = from.y() + t * dy;
            const double distance = std::hypot(px - projX, py - projY);

            const float coverage = static_cast<float>(std::clamp(radius + 0.5 - distance, 0.0, 1.0));
            if (coverage <= 0.0f) {
                continue;
            }

            uchar* pixel = line + (x * 4);
            if (params.mode == StrokePaintMode::Paint) {
                const float srcA = (static_cast<float>(colorA) / 255.0f) * coverage;
                const float dstA = static_cast<float>(pixel[3]) / 255.0f;
                const float outA = srcA + dstA * (1.0f - srcA);
                if (outA > 0.0f) {
                    const float srcR = static_cast<float>(colorR) / 255.0f;
                    const float srcG = static_cast<float>(colorG) / 255.0f;
                    const float srcB = static_cast<float>(colorB) / 255.0f;

                    const float dstR = static_cast<float>(pixel[0]) / 255.0f;
                    const float dstG = static_cast<float>(pixel[1]) / 255.0f;
                    const float dstB = static_cast<float>(pixel[2]) / 255.0f;

                    pixel[0] = static_cast<uchar>(std::clamp(
                        std::round(((srcR * srcA + dstR * dstA * (1.0f - srcA)) / outA) * 255.0f), 0.0f, 255.0f));
                    pixel[1] = static_cast<uchar>(std::clamp(
                        std::round(((srcG * srcA + dstG * dstA * (1.0f - srcA)) / outA) * 255.0f), 0.0f, 255.0f));
                    pixel[2] = static_cast<uchar>(std::clamp(
                        std::round(((srcB * srcA + dstB * dstA * (1.0f - srcA)) / outA) * 255.0f), 0.0f, 255.0f));
                }
                pixel[3] = static_cast<uchar>(std::clamp(std::round(outA * 255.0f), 0.0f, 255.0f));
            } else {
                const float dstA = static_cast<float>(pixel[3]);
                pixel[3] = static_cast<uchar>(std::clamp(std::round(dstA * (1.0f - coverage)), 0.0f, 255.0f));
            }
        }
    }
}

} // namespace

QRect StrokePaintRasterizer::segmentBounds(const QSize& imageSize,
                                           const QPointF& from,
                                           const QPointF& to,
                                           double diameter) {
    if (!imageSize.isValid() || diameter <= 0.0) {
        return QRect{};
    }
    const double radius = diameter * 0.5;
    const int left = std::max(0, static_cast<int>(std::floor(std::min(from.x(), to.x()) - radius - 0.5)));
    const int top = std::max(0, static_cast<int>(std::floor(std::min(from.y(), to.y()) - radius - 0.5)));
    const int right =
        std::min(imageSize.width(), static_cast<int>(std::ceil(std::max(from.x(), to.x()) + radius + 0.5)));
    const int bottom =
        std::min(imageSize.height(), static_cast<int>(std::ceil(std::max(from.y(), to.y()) + radius + 0.5)));
    if (right <= left || bottom <= top) {
        return QRect{};
    }
    return QRect(left, top, right - left, bottom - top);
}

void StrokePaintRasterizer::rasterizeSegment(QImage& image,
                                             const StrokePaintParams& params,
                                             const QPointF& from,
                                             const QPointF& to) {
    if (image.isNull() || params.diameter <= 0.0) {
        return;
    }
    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }
    rasterizeSpan(image, params, from, to);
}

void StrokePaintRasterizer::rasterizeCap(QImage& image, const StrokePaintParams& params, const QPointF& at) {
    rasterizeSegment(image, params, at, at);
}

QRect StrokePaintRasterizer::rasterizeStroke(QImage& image,
                                             const StrokePaintParams& params,
                                             const QVector<QPointF>& points) {
    if (image.isNull() || params.diameter <= 0.0 || points.isEmpty()) {
        return QRect{};
    }
    if (image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }

    QRect dirty;
    const auto accumulate = [&image, &params, &dirty](const QPointF& from, const QPointF& to) {
        const QRect bounds = segmentBounds(image.size(), from, to, params.diameter);
        if (bounds.isEmpty()) {
            return;
        }
        rasterizeSpan(image, params, from, to);
        dirty = dirty.isEmpty() ? bounds : dirty.united(bounds);
    };

    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        accumulate(points[i], points[i + 1]);
    }
    accumulate(points.last(), points.last());

    return dirty;
}

} // namespace manga::rendering
