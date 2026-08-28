#include "domain/model/Geometry.h"

#include <algorithm>

namespace manga::domain {

bool RectGeometry::isValid() const noexcept {
    if (std::isnan(x) || std::isnan(y) || std::isnan(width) || std::isnan(height)) {
        return false;
    }
    if (std::isinf(x) || std::isinf(y) || std::isinf(width) || std::isinf(height)) {
        return false;
    }
    return x >= 0.0 && y >= 0.0 && width >= kMinDimension && height >= kMinDimension;
}

bool RectGeometry::isWithinBounds(const ImageSize& bounds) const noexcept {
    if (!isValid() || !bounds.isValid()) {
        return false;
    }
    const double maxX = x + width;
    const double maxY = y + height;
    return maxX <= static_cast<double>(bounds.width) && maxY <= static_cast<double>(bounds.height);
}

std::optional<RectGeometry> RectGeometry::clampedToBounds(const ImageSize& bounds) const noexcept {
    if (std::isnan(x) || std::isnan(y) || std::isnan(width) || std::isnan(height) || !bounds.isValid()) {
        return std::nullopt;
    }

    const double boundsW = static_cast<double>(bounds.width);
    const double boundsH = static_cast<double>(bounds.height);

    const double origRight = x + width;
    const double origBottom = y + height;

    const double clampedX = std::clamp(x, 0.0, boundsW);
    const double clampedY = std::clamp(y, 0.0, boundsH);
    const double clampedRight = std::clamp(origRight, 0.0, boundsW);
    const double clampedBottom = std::clamp(origBottom, 0.0, boundsH);

    const double clampedW = clampedRight - clampedX;
    const double clampedH = clampedBottom - clampedY;

    if (clampedW < kMinDimension || clampedH < kMinDimension) {
        return std::nullopt;
    }

    return RectGeometry{.x = clampedX, .y = clampedY, .width = clampedW, .height = clampedH};
}

bool RectGeometry::approxEquals(const RectGeometry& other, double epsilon) const noexcept {
    return std::abs(x - other.x) <= epsilon && std::abs(y - other.y) <= epsilon &&
           std::abs(width - other.width) <= epsilon && std::abs(height - other.height) <= epsilon;
}

bool RectGeometry::operator==(const RectGeometry& other) const noexcept {
    return approxEquals(other);
}

bool Margins::approxEquals(const Margins& other, double epsilon) const noexcept {
    return std::abs(top - other.top) <= epsilon && std::abs(right - other.right) <= epsilon &&
           std::abs(bottom - other.bottom) <= epsilon && std::abs(left - other.left) <= epsilon;
}

bool Margins::operator==(const Margins& other) const noexcept {
    return approxEquals(other);
}

} // namespace manga::domain
