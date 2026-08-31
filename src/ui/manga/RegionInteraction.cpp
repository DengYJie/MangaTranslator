#include "ui/manga/RegionInteraction.h"

#include <algorithm>
#include <cmath>

namespace manga::ui::canvas {

QPointF sourceToView(QPointF source, double zoom) noexcept {
    return source * zoom;
}

QPointF viewToSource(QPointF view, double zoom) noexcept {
    if (qFuzzyIsNull(zoom)) {
        return {};
    }
    return view / zoom;
}

QPointF viewToDevice(QPointF view, double devicePixelRatio) noexcept {
    return view * devicePixelRatio;
}

QPointF deviceToView(QPointF device, double devicePixelRatio) noexcept {
    if (qFuzzyIsNull(devicePixelRatio)) {
        return {};
    }
    return device / devicePixelRatio;
}

static QPointF handleAnchorPoint(HandlePosition pos, const domain::RectGeometry& g) noexcept {
    const double left = g.x;
    const double top = g.y;
    const double right = g.x + g.width;
    const double bottom = g.y + g.height;
    const double cx = g.x + g.width * 0.5;
    const double cy = g.y + g.height * 0.5;

    switch (pos) {
        case HandlePosition::NorthWest:
            return {left, top};
        case HandlePosition::North:
            return {cx, top};
        case HandlePosition::NorthEast:
            return {right, top};
        case HandlePosition::East:
            return {right, cy};
        case HandlePosition::SouthEast:
            return {right, bottom};
        case HandlePosition::South:
            return {cx, bottom};
        case HandlePosition::SouthWest:
            return {left, bottom};
        case HandlePosition::West:
            return {left, cy};
    }
    return {cx, cy};
}

QRectF
handleHitRect(HandlePosition pos, const domain::RectGeometry& geometry, double zoom, double devicePixelRatio) noexcept {
    const QPointF anchor = handleAnchorPoint(pos, geometry);
    const QPointF viewPos = sourceToView(anchor, zoom);
    const QPointF devicePos = viewToDevice(viewPos, devicePixelRatio);

    const double halfHit = kHandleHitSizeDevicePx * 0.5;
    return QRectF(devicePos.x() - halfHit, devicePos.y() - halfHit, kHandleHitSizeDevicePx, kHandleHitSizeDevicePx);
}

std::optional<HandlePosition> hitTestHandle(const QPointF& devicePoint,
                                            const domain::RectGeometry& geometry,
                                            double zoom,
                                            double devicePixelRatio) noexcept {
    static constexpr HandlePosition allHandles[] = {HandlePosition::NorthWest,
                                                    HandlePosition::North,
                                                    HandlePosition::NorthEast,
                                                    HandlePosition::East,
                                                    HandlePosition::SouthEast,
                                                    HandlePosition::South,
                                                    HandlePosition::SouthWest,
                                                    HandlePosition::West};

    for (const auto pos : allHandles) {
        if (handleHitRect(pos, geometry, zoom, devicePixelRatio).contains(devicePoint)) {
            return pos;
        }
    }
    return std::nullopt;
}

domain::RectGeometry
moveRegion(const domain::RectGeometry& original, QPointF deltaSourcePx, const domain::ImageSize& pageBounds) noexcept {
    const double boundsW = static_cast<double>(pageBounds.width);
    const double boundsH = static_cast<double>(pageBounds.height);

    double newX = original.x + deltaSourcePx.x();
    double newY = original.y + deltaSourcePx.y();

    newX = std::clamp(newX, 0.0, boundsW - original.width);
    newY = std::clamp(newY, 0.0, boundsH - original.height);

    return {.x = newX, .y = newY, .width = original.width, .height = original.height};
}

std::optional<domain::RectGeometry> resizeRegion(const domain::RectGeometry& original,
                                                 HandlePosition handle,
                                                 QPointF deltaSourcePx,
                                                 const domain::ImageSize& pageBounds) noexcept {
    double left = original.x;
    double top = original.y;
    double right = original.x + original.width;
    double bottom = original.y + original.height;

    const double dx = deltaSourcePx.x();
    const double dy = deltaSourcePx.y();

    switch (handle) {
        case HandlePosition::NorthWest:
            left += dx;
            top += dy;
            break;
        case HandlePosition::North:
            top += dy;
            break;
        case HandlePosition::NorthEast:
            right += dx;
            top += dy;
            break;
        case HandlePosition::East:
            right += dx;
            break;
        case HandlePosition::SouthEast:
            right += dx;
            bottom += dy;
            break;
        case HandlePosition::South:
            bottom += dy;
            break;
        case HandlePosition::SouthWest:
            left += dx;
            bottom += dy;
            break;
        case HandlePosition::West:
            left += dx;
            break;
    }

    const double boundsW = static_cast<double>(pageBounds.width);
    const double boundsH = static_cast<double>(pageBounds.height);

    left = std::clamp(left, 0.0, boundsW);
    top = std::clamp(top, 0.0, boundsH);
    right = std::clamp(right, 0.0, boundsW);
    bottom = std::clamp(bottom, 0.0, boundsH);

    if (left > right) {
        std::swap(left, right);
    }
    if (top > bottom) {
        std::swap(top, bottom);
    }

    const double w = right - left;
    const double h = bottom - top;

    if (w < kMinRegionDimension || h < kMinRegionDimension) {
        return std::nullopt;
    }

    return domain::RectGeometry{.x = left, .y = top, .width = w, .height = h};
}

bool exceedsDragThreshold(QPointF deviceDelta) noexcept {
    const double distSq = deviceDelta.x() * deviceDelta.x() + deviceDelta.y() * deviceDelta.y();
    return distSq >= kDragThresholdDevicePx * kDragThresholdDevicePx;
}

} // namespace manga::ui::canvas
