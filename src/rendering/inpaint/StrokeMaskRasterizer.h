#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QVector>

namespace manga::rendering {

struct RasterizedStrokeMask {
    QImage mask;
    QRect dirtyBounds;
};

class StrokeMaskRasterizer final {
public:
    /// Rasterizes source-image coordinates into an antialiased 8-bit mask.
    /// The returned mask has exactly sourceSize and dirtyBounds is clipped to it.
    /// An invalid size, diameter, or empty point list returns an empty mask result.
    [[nodiscard]] static RasterizedStrokeMask
    rasterize(const QSize& sourceSize, const QVector<QPointF>& points, double diameter);
};

} // namespace manga::rendering
