#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QVector>

namespace manga::rendering {

enum class StrokePaintMode {
    Paint,
    Erase
};

/// Stroke appearance shared by the incremental and whole-stroke entry points.
struct StrokePaintParams {
    StrokePaintMode mode{StrokePaintMode::Paint};
    QColor color{17, 17, 17, 255};
    double diameter{32.0};
};

/// Rasterizes brush strokes into a straight-alpha RGBA8888 layer.
/// Whole-stroke and per-segment entry points produce identical pixels so a live
/// preview can reuse the exact math of the committed stroke.
class StrokePaintRasterizer final {
public:
    /// Pixels one segment can touch, clipped to the image. Empty when nothing can change.
    [[nodiscard]] static QRect
    segmentBounds(const QSize& imageSize, const QPointF& from, const QPointF& to, double diameter);

    /// Rasterizes one segment as a capsule, so both endpoints keep their round cap.
    static void
    rasterizeSegment(QImage& image, const StrokePaintParams& params, const QPointF& from, const QPointF& to);

    /// Rasterizes the round cap at one point; used for the stroke end and single-point strokes.
    static void rasterizeCap(QImage& image, const StrokePaintParams& params, const QPointF& at);

    /// Rasterizes a whole polyline and returns the union of touched pixels, clipped to the image.
    /// An empty point list, a non-positive diameter, or a null image changes nothing and returns
    /// an empty rect. A non-RGBA8888 image is converted in place first.
    [[nodiscard]] static QRect
    rasterizeStroke(QImage& image, const StrokePaintParams& params, const QVector<QPointF>& points);
};

} // namespace manga::rendering
