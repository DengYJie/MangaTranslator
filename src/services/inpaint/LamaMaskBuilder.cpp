#include "services/inpaint/LamaMaskBuilder.h"

#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QRectF>
#include <QSize>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

namespace manga::services {
namespace {

/// Below this background sample count the colour statistics are not trusted and the solid
/// fallback replaces derived ink.
constexpr int kMinBackgroundSamples = 16;

/// Stroke width removed from the bubble boundary so the outline is never part of the clip area.
constexpr double kBubbleOutlineKeepout = 4.0;

/// Square dilation radius that covers antialiased glyph edges and thin serifs.
constexpr int kInkDilateRadius = 3;

/// Rect dilation used as the clip area when no bubble polygon is available.
constexpr int kNoPolygonDilateRadius = 3;

/// Ring around the text rect sampled for background when no bubble polygon is available.
constexpr int kNoPolygonBackgroundRing = 6;

constexpr double kInkTauStdDevFactor = 2.5;
constexpr double kInkTauMin = 24.0;
constexpr double kInkTauMax = 90.0;

/// Derived ink outside this coverage band means the estimate missed everything or took the whole
/// bubble, so the solid fallback is safer.
constexpr double kMinInkCoverage = 0.03;
constexpr double kMaxInkCoverage = 0.90;

/// Grayscale level treated as masked, matching the binarisation the model side applies.
constexpr int kMaskBinaryLevel = 127;

/// Minimum squared distance from the bubble outline for a flat fill background sample.
constexpr double kUniformEdgeMargin = 3.0;

/// Channel spread boundary that selects the tighter acceptance threshold.
constexpr double kUniformSpreadSplit = 1.0;
constexpr double kUniformTightThreshold = 7.0;
constexpr double kUniformLooseThreshold = 10.0;

/// Rect as text proxy margin: a sample keeps this clearance from any masked pixel.
constexpr int kUniformMaskClearance = 3;

[[nodiscard]] constexpr std::size_t indexAt(std::size_t width, int x, int y) {
    return width * std::size_t(y) + std::size_t(x);
}

[[nodiscard]] constexpr double squaredOf(double value) {
    return value * value;
}

[[nodiscard]] LamaClearMask emptyResult() {
    LamaClearMask result;
    result.mask = QImage(1, 1, QImage::Format_Grayscale8);
    if (result.mask.isNull()) {
        return result;
    }
    std::fill_n(result.mask.scanLine(0), std::size_t(result.mask.bytesPerLine()), uchar{0});
    return result;
}

void clearToZero(QImage& image) {
    for (int y = 0; y < image.height(); ++y) {
        std::fill_n(image.scanLine(y), std::size_t(image.bytesPerLine()), uchar{0});
    }
}

[[nodiscard]] double medianOf(std::vector<double>& values) {
    std::sort(values.begin(), values.end());
    const std::size_t count = values.size();
    if (count == 0) {
        return 0.0;
    }
    const std::size_t middle = count / 2;
    if (count % 2 == 0) {
        return (values[middle - 1] + values[middle]) * 0.5;
    }
    return values[middle];
}

[[nodiscard]] double populationDeviation(const std::vector<double>& values, double center) {
    if (values.empty()) {
        return 0.0;
    }
    double sum = 0.0;
    for (const double value : values) {
        sum += (value - center) * (value - center);
    }
    return std::sqrt(sum / static_cast<double>(values.size()));
}

[[nodiscard]] bool pointInPolygon(const QPointF& point, const std::vector<domain::Point2D>& polygon) {
    bool inside = false;
    domain::Point2D previous = polygon.back();
    for (const domain::Point2D& current : polygon) {
        if ((current.y > point.y()) != (previous.y > point.y())) {
            const double intersectionX =
                (previous.x - current.x) * (point.y() - current.y) / (previous.y - current.y) + current.x;
            if (point.x() < intersectionX) {
                inside = !inside;
            }
        }
        previous = current;
    }
    return inside;
}

/// Squared distance to the implicitly closed polygon outline, so zero means exactly on an edge.
[[nodiscard]] double polygonEdgeDistanceSquared(const QPointF& point, const std::vector<domain::Point2D>& polygon) {
    double minimum = std::numeric_limits<double>::infinity();
    domain::Point2D start = polygon.back();
    for (const domain::Point2D& end : polygon) {
        const double segmentX = end.x - start.x;
        const double segmentY = end.y - start.y;
        const double lengthSquared = segmentX * segmentX + segmentY * segmentY;
        double projection = 0.0;
        if (lengthSquared > 0.0) {
            projection = ((point.x() - start.x) * segmentX + (point.y() - start.y) * segmentY) / lengthSquared;
        }
        projection = std::clamp(projection, 0.0, 1.0);
        const double deltaX = point.x() - (start.x + segmentX * projection);
        const double deltaY = point.y() - (start.y + segmentY * projection);
        minimum = std::min(minimum, deltaX * deltaX + deltaY * deltaY);
        start = end;
    }
    return minimum;
}

/// Pixel bounds that certainly contain the polygon, used to skip the expensive edge tests.
[[nodiscard]] QRect boundingBox(const std::vector<domain::Point2D>& polygon) {
    double left = polygon.front().x;
    double top = polygon.front().y;
    double right = left;
    double bottom = top;
    for (const domain::Point2D& point : polygon) {
        left = std::min(left, point.x);
        top = std::min(top, point.y);
        right = std::max(right, point.x);
        bottom = std::max(bottom, point.y);
    }
    return QRect(QPoint(int(std::floor(left)), int(std::floor(top))),
                 QPoint(int(std::ceil(right)), int(std::ceil(bottom))));
}

/// Rasterises a path without antialiasing so the result is a hard binary exclusion field.
[[nodiscard]] std::vector<quint8> rasterizeClip(const QPainterPath& path, int width, int height) {
    std::vector<quint8> clip(std::size_t(width) * std::size_t(height), 0);
    QImage surface(width, height, QImage::Format_ARGB32);
    if (surface.isNull()) {
        return clip;
    }
    surface.fill(Qt::transparent);
    QPainter painter(&surface);
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillPath(path, QColor(255, 255, 255, 255));
    painter.end();
    for (int y = 0; y < height; ++y) {
        const auto* line = reinterpret_cast<const QRgb*>(surface.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            clip[indexAt(std::size_t(width), x, y)] = qAlpha(line[x]) > 0 ? quint8(255) : quint8(0);
        }
    }
    return clip;
}

void dilateSquare(std::vector<quint8>& grid, int width, int height, int radius) {
    const std::size_t stride = std::size_t(width);
    std::vector<quint8> horizontal(grid.size(), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            quint8 value = 0;
            for (int offset = -radius; offset <= radius; ++offset) {
                const int sourceX = x + offset;
                if (sourceX >= 0 && sourceX < width) {
                    value = grid[indexAt(stride, sourceX, y)];
                    if (value != 0) {
                        break;
                    }
                }
            }
            horizontal[indexAt(stride, x, y)] = value;
        }
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            quint8 value = 0;
            for (int offset = -radius; offset <= radius; ++offset) {
                const int sourceY = y + offset;
                if (sourceY >= 0 && sourceY < height) {
                    value = horizontal[indexAt(stride, x, sourceY)];
                    if (value != 0) {
                        break;
                    }
                }
            }
            grid[indexAt(stride, x, y)] = value;
        }
    }
}

void applyClip(std::vector<quint8>& values, const std::vector<quint8>& clip) {
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (clip[i] == 0) {
            values[i] = 0;
        }
    }
}

[[nodiscard]] std::size_t countSet(const std::vector<quint8>& grid) {
    std::size_t count = 0;
    for (const quint8 value : grid) {
        if (value != 0) {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] QImage normalizedGrayscale(const QImage& image) {
    if (image.isNull() || image.format() == QImage::Format_Grayscale8) {
        return image;
    }
    return image.convertToFormat(QImage::Format_Grayscale8);
}

/// True when the pixel and its full clearance neighbourhood exist and stay below the binary level.
[[nodiscard]] bool farFromMasked(const QImage& mask, int x, int y, int clearance) {
    for (int offsetY = -clearance; offsetY <= clearance; ++offsetY) {
        const int sampleY = y + offsetY;
        if (sampleY < 0 || sampleY >= mask.height()) {
            return false;
        }
        const uchar* line = mask.constScanLine(sampleY);
        for (int offsetX = -clearance; offsetX <= clearance; ++offsetX) {
            const int sampleX = x + offsetX;
            if (sampleX < 0 || sampleX >= mask.width()) {
                return false;
            }
            if (line[sampleX] >= kMaskBinaryLevel) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] int clampChannel(double value) {
    return int(std::clamp(std::lround(value), 0L, 255L));
}

} // namespace

LamaClearMask buildClearMask(const QImage& pageImage,
                             const QRect& regionRect,
                             const std::optional<domain::PolygonGeometry>& polygon,
                             QSize pageSize,
                             int contextMargin) {
    if (pageSize.isEmpty()) {
        pageSize = pageImage.size();
    }
    if (pageImage.isNull() || pageSize.isEmpty()) {
        return emptyResult();
    }

    const int margin = std::max(0, contextMargin);
    const QRect imageBounds(0, 0, pageImage.width(), pageImage.height());
    const QRect pageBounds = QRect(QPoint(0, 0), pageSize).intersected(imageBounds);
    const QRect region = regionRect.normalized();
    const QRect dirtyBounds = region.adjusted(-margin, -margin, margin, margin).intersected(pageBounds);
    if (dirtyBounds.width() <= 0 || dirtyBounds.height() <= 0) {
        return emptyResult();
    }

    LamaClearMask result;
    result.dirtyBounds = dirtyBounds;
    result.mask = QImage(dirtyBounds.size(), QImage::Format_Grayscale8);
    if (result.mask.isNull()) {
        return emptyResult();
    }
    clearToZero(result.mask);

    const QImage page =
        pageImage.format() == QImage::Format_RGBA8888 ? pageImage : pageImage.convertToFormat(QImage::Format_RGBA8888);
    if (page.isNull()) {
        return result;
    }

    const int width = dirtyBounds.width();
    const int height = dirtyBounds.height();
    const std::size_t stride = std::size_t(width);
    const QRect localRect = region.translated(-dirtyBounds.x(), -dirtyBounds.y());
    const bool hasPolygon = polygon.has_value() && polygon->isValid();

    std::vector<quint8> clip(stride * std::size_t(height), 0);
    if (hasPolygon) {
        const std::vector<domain::Point2D>& points = polygon->points;
        const double offsetX = double(-dirtyBounds.x());
        const double offsetY = double(-dirtyBounds.y());
        QPainterPath bubble;
        bubble.moveTo(points.front().x + offsetX, points.front().y + offsetY);
        for (std::size_t i = 1; i < points.size(); ++i) {
            bubble.lineTo(points[i].x + offsetX, points[i].y + offsetY);
        }
        bubble.closeSubpath();
        QPainterPathStroker stroker;
        stroker.setWidth(kBubbleOutlineKeepout);
        stroker.setJoinStyle(Qt::RoundJoin);
        const QPainterPath inner = bubble.subtracted(stroker.createStroke(bubble));
        clip = rasterizeClip(inner, width, height);
    } else {
        QPainterPath dilated;
        dilated.addRect(QRectF(localRect).adjusted(
            -kNoPolygonDilateRadius, -kNoPolygonDilateRadius, kNoPolygonDilateRadius, kNoPolygonDilateRadius));
        clip = rasterizeClip(dilated, width, height);
    }
    if (countSet(clip) == 0) {
        return result;
    }

    std::vector<quint8> candidate(stride * std::size_t(height), 0);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::size_t i = indexAt(stride, x, y);
            if (clip[i] != 0 && (!hasPolygon || localRect.contains(x, y))) {
                candidate[i] = 255;
            }
        }
    }
    const std::size_t candidateCount = countSet(candidate);
    if (candidateCount == 0) {
        return result;
    }

    // Background evidence must come from the same surface as the text: the bubble interior away
    // from the text rect when an outline exists, and the surrounding ring otherwise. Sampling the
    // whole context band lets page white bias the median and inflate every colour distance.
    const QRect ringBounds = QRect(0, 0, width, height)
                                 .intersected(localRect.adjusted(-kNoPolygonBackgroundRing,
                                                                 -kNoPolygonBackgroundRing,
                                                                 kNoPolygonBackgroundRing,
                                                                 kNoPolygonBackgroundRing));
    std::vector<double> reds;
    std::vector<double> greens;
    std::vector<double> blues;
    for (int y = 0; y < height; ++y) {
        const uchar* line = page.constScanLine(dirtyBounds.y() + y) + std::size_t(dirtyBounds.x()) * 4;
        for (int x = 0; x < width; ++x) {
            const std::size_t i = indexAt(stride, x, y);
            if (candidate[i] != 0 || (hasPolygon ? clip[i] == 0 : !ringBounds.contains(x, y))) {
                continue;
            }
            const uchar* pixel = line + std::size_t(x) * 4;
            reds.push_back(double(pixel[0]));
            greens.push_back(double(pixel[1]));
            blues.push_back(double(pixel[2]));
        }
    }

    std::vector<quint8> binary(stride * std::size_t(height), 0);
    bool useSolidFallback = reds.size() < std::size_t(kMinBackgroundSamples);
    if (!useSolidFallback) {
        const double medianRed = medianOf(reds);
        const double medianGreen = medianOf(greens);
        const double medianBlue = medianOf(blues);
        const double deviationRed = populationDeviation(reds, medianRed);
        const double deviationGreen = populationDeviation(greens, medianGreen);
        const double deviationBlue = populationDeviation(blues, medianBlue);
        const double meanDeviation = (deviationRed + deviationGreen + deviationBlue) / 3.0;
        const double tau = std::clamp(kInkTauStdDevFactor * meanDeviation, kInkTauMin, kInkTauMax);
        std::size_t inkCount = 0;
        for (int y = 0; y < height; ++y) {
            const uchar* line = page.constScanLine(dirtyBounds.y() + y) + std::size_t(dirtyBounds.x()) * 4;
            for (int x = 0; x < width; ++x) {
                const std::size_t i = indexAt(stride, x, y);
                if (candidate[i] == 0) {
                    continue;
                }
                const uchar* pixel = line + std::size_t(x) * 4;
                const double deltaRed = std::fabs(double(pixel[0]) - medianRed);
                const double deltaGreen = std::fabs(double(pixel[1]) - medianGreen);
                const double deltaBlue = std::fabs(double(pixel[2]) - medianBlue);
                const double distance = std::max(deltaRed, std::max(deltaGreen, deltaBlue));
                if (distance > tau) {
                    binary[i] = 255;
                    ++inkCount;
                }
            }
        }
        const double coverage = double(inkCount) / double(candidateCount);
        useSolidFallback = coverage < kMinInkCoverage || coverage > kMaxInkCoverage;
    }
    if (useSolidFallback) {
        binary = candidate;
    }

    dilateSquare(binary, width, height, kInkDilateRadius);
    applyClip(binary, clip);

    for (int y = 0; y < height; ++y) {
        const quint8* source = binary.data() + indexAt(stride, 0, y);
        std::copy(source, source + width, result.mask.scanLine(y));
    }
    return result;
}

std::optional<QColor> uniformBackgroundColor(const QImage& pageImage,
                                             const LamaClearMask& clearMask,
                                             const std::optional<domain::PolygonGeometry>& polygon) {
    const QImage grayMask = normalizedGrayscale(clearMask.mask);
    const QRect bounds = clearMask.dirtyBounds;
    if (pageImage.isNull() || grayMask.isNull() || grayMask.size() != bounds.size() || bounds.width() <= 0 ||
        bounds.height() <= 0) {
        return std::nullopt;
    }
    const QImage page =
        pageImage.format() == QImage::Format_RGBA8888 ? pageImage : pageImage.convertToFormat(QImage::Format_RGBA8888);
    if (page.isNull()) {
        return std::nullopt;
    }

    const bool hasPolygon = polygon.has_value() && polygon->isValid();
    std::vector<domain::Point2D> points;
    QRect polygonBounds;
    if (hasPolygon) {
        points = polygon->points;
        polygonBounds = boundingBox(points);
    }

    std::vector<double> reds;
    std::vector<double> greens;
    std::vector<double> blues;
    for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        if (y < 0 || y >= page.height()) {
            continue;
        }
        const uchar* line = page.constScanLine(y);
        const int localY = y - bounds.y();
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
            if (x < 0 || x >= page.width()) {
                continue;
            }
            const int localX = x - bounds.x();
            if (grayMask.constScanLine(localY)[localX] >= kMaskBinaryLevel) {
                continue;
            }
            if (hasPolygon) {
                if (!polygonBounds.contains(x, y)) {
                    continue;
                }
                const QPointF center(double(x) + 0.5, double(y) + 0.5);
                if (!pointInPolygon(center, points)) {
                    continue;
                }
                if (polygonEdgeDistanceSquared(center, points) < squaredOf(kUniformEdgeMargin)) {
                    continue;
                }
            } else if (!farFromMasked(grayMask, localX, localY, kUniformMaskClearance)) {
                continue;
            }
            const uchar* pixel = line + std::size_t(x) * 4;
            reds.push_back(double(pixel[0]));
            greens.push_back(double(pixel[1]));
            blues.push_back(double(pixel[2]));
        }
    }

    if (reds.size() < std::size_t(kMinBackgroundSamples)) {
        return std::nullopt;
    }

    const double medianRed = medianOf(reds);
    const double medianGreen = medianOf(greens);
    const double medianBlue = medianOf(blues);
    const double deviationRed = populationDeviation(reds, medianRed);
    const double deviationGreen = populationDeviation(greens, medianGreen);
    const double deviationBlue = populationDeviation(blues, medianBlue);
    const double meanDeviation = (deviationRed + deviationGreen + deviationBlue) / 3.0;
    const double channelSpread =
        std::sqrt((squaredOf(deviationRed - meanDeviation) + squaredOf(deviationGreen - meanDeviation) +
                   squaredOf(deviationBlue - meanDeviation)) /
                  3.0);
    const double threshold = channelSpread > kUniformSpreadSplit ? kUniformTightThreshold : kUniformLooseThreshold;
    if (std::max(deviationRed, std::max(deviationGreen, deviationBlue)) >= threshold) {
        return std::nullopt;
    }

    return QColor(clampChannel(medianRed), clampChannel(medianGreen), clampChannel(medianBlue));
}

QImage makeColorPatch(const QRect& bounds, const QColor& color, const QImage& mask) {
    if (bounds.width() <= 0 || bounds.height() <= 0) {
        return QImage();
    }
    QImage patch(bounds.size(), QImage::Format_RGBA8888);
    if (patch.isNull()) {
        return QImage();
    }
    patch.setColorSpace(QColorSpace(QColorSpace::SRgb));

    const QImage grayMask = normalizedGrayscale(mask);
    const bool hasMask =
        !grayMask.isNull() && grayMask.size() == bounds.size() && grayMask.format() == QImage::Format_Grayscale8;
    const uchar red = uchar(color.red());
    const uchar green = uchar(color.green());
    const uchar blue = uchar(color.blue());
    for (int y = 0; y < patch.height(); ++y) {
        uchar* target = patch.scanLine(y);
        const uchar* maskLine = hasMask ? grayMask.constScanLine(y) : nullptr;
        for (int x = 0; x < patch.width(); ++x) {
            const std::size_t offset = std::size_t(x) * 4;
            target[offset + 0] = red;
            target[offset + 1] = green;
            target[offset + 2] = blue;
            target[offset + 3] = (maskLine != nullptr && maskLine[x] >= kMaskBinaryLevel) ? uchar(255) : uchar(0);
        }
    }
    return patch;
}

QImage makeInpaintedPatch(const QRect& bounds, const QImage& source, const QImage& mask) {
    if (bounds.width() <= 0 || bounds.height() <= 0) {
        return QImage();
    }
    QImage patch(bounds.size(), QImage::Format_RGBA8888);
    if (patch.isNull()) {
        return QImage();
    }
    patch.setColorSpace(QColorSpace(QColorSpace::SRgb));

    const QImage grayMask = normalizedGrayscale(mask);
    const bool hasMask =
        !grayMask.isNull() && grayMask.size() == bounds.size() && grayMask.format() == QImage::Format_Grayscale8;

    QImage sourceImage;
    if (!source.isNull() && source.size() == bounds.size()) {
        sourceImage =
            source.format() == QImage::Format_RGBA8888 ? source : source.convertToFormat(QImage::Format_RGBA8888);
    }
    if (sourceImage.isNull() || sourceImage.size() != bounds.size()) {
        clearToZero(patch);
        return patch;
    }

    for (int y = 0; y < patch.height(); ++y) {
        uchar* target = patch.scanLine(y);
        const uchar* sourceLine = sourceImage.constScanLine(y);
        const uchar* maskLine = hasMask ? grayMask.constScanLine(y) : nullptr;
        for (int x = 0; x < patch.width(); ++x) {
            const std::size_t offset = std::size_t(x) * 4;
            target[offset + 0] = sourceLine[offset + 0];
            target[offset + 1] = sourceLine[offset + 1];
            target[offset + 2] = sourceLine[offset + 2];
            target[offset + 3] = (maskLine != nullptr && maskLine[x] >= kMaskBinaryLevel) ? uchar(255) : uchar(0);
        }
    }
    return patch;
}

} // namespace manga::services
