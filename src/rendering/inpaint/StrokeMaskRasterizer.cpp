#include "rendering/inpaint/StrokeMaskRasterizer.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>

namespace manga::rendering {

RasterizedStrokeMask
StrokeMaskRasterizer::rasterize(const QSize& sourceSize, const QVector<QPointF>& points, double diameter) {
    RasterizedStrokeMask result;
    if (!sourceSize.isValid() || points.isEmpty() || diameter < 1.0 || diameter > 128.0) {
        return result;
    }

    result.mask = QImage(sourceSize, QImage::Format_Grayscale8);
    result.mask.fill(0);

    QPainter painter(&result.mask);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::white, diameter, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);

    if (points.size() == 1) {
        const QPointF center = points.front();
        const double radius = diameter * 0.5;
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(center, radius, radius);
    } else {
        QPainterPath stroke;
        stroke.moveTo(points.front());
        for (int i = 1; i < points.size(); ++i) {
            stroke.lineTo(points.at(i));
        }
        painter.drawPath(stroke);
    }
    painter.end();

    int left = sourceSize.width();
    int top = sourceSize.height();
    int right = -1;
    int bottom = -1;
    for (int y = 0; y < sourceSize.height(); ++y) {
        const auto* scanline = result.mask.constScanLine(y);
        for (int x = 0; x < sourceSize.width(); ++x) {
            if (scanline[x] == 0) {
                continue;
            }
            left = qMin(left, x);
            right = qMax(right, x);
            top = qMin(top, y);
            bottom = qMax(bottom, y);
        }
    }
    if (right >= left && bottom >= top) {
        result.dirtyBounds = QRect(QPoint(left, top), QPoint(right, bottom));
    }
    return result;
}

} // namespace manga::rendering
