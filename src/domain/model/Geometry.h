#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace manga::domain {

/// Image dimensions in source pixels.
struct ImageSize {
    int width{0};
    int height{0};

    [[nodiscard]] constexpr bool isValid() const noexcept {
        return width > 0 && height > 0;
    }

    [[nodiscard]] constexpr bool operator==(const ImageSize& other) const noexcept = default;
};

/// Axis-aligned rectangle geometry in source image pixel coordinates.
struct RectGeometry {
    static constexpr double kMinDimension = 2.0;

    double x{0.0};
    double y{0.0};
    double width{0.0};
    double height{0.0};

    /// Checks that the coordinates are non-negative and finite and that both sides reach kMinDimension.
    [[nodiscard]] bool isValid() const noexcept;

    /// Checks if this rectangle is fully contained within the given image bounds.
    [[nodiscard]] bool isWithinBounds(const ImageSize& bounds) const noexcept;

    /// Returns a rectangle clamped to image bounds.
    /// If the resulting clamped width or height is less than kMinDimension, returns std::nullopt.
    [[nodiscard]] std::optional<RectGeometry> clampedToBounds(const ImageSize& bounds) const noexcept;

    /// Compares for equality with a small epsilon tolerance.
    [[nodiscard]] bool approxEquals(const RectGeometry& other, double epsilon = 1e-5) const noexcept;

    [[nodiscard]] bool operator==(const RectGeometry& other) const noexcept;
};

/// Margins (top, right, bottom, left) in points or pixels.
struct Margins {
    double top{0.0};
    double right{0.0};
    double bottom{0.0};
    double left{0.0};

    [[nodiscard]] bool isValid() const noexcept {
        return top >= 0.0 && right >= 0.0 && bottom >= 0.0 && left >= 0.0 && !std::isnan(top) && !std::isnan(right) &&
               !std::isnan(bottom) && !std::isnan(left);
    }

    [[nodiscard]] bool approxEquals(const Margins& other, double epsilon = 1e-5) const noexcept;
    [[nodiscard]] bool operator==(const Margins& other) const noexcept;
};

/// 2D point in source image pixel coordinates.
struct Point2D {
    double x{0.0};
    double y{0.0};

    [[nodiscard]] constexpr bool operator==(const Point2D& other) const noexcept = default;
};

/// Polygonal boundary in source image pixel coordinates.
struct PolygonGeometry {
    std::vector<Point2D> points;

    /// A polygon is usable when it has at least three vertices, all finite.
    [[nodiscard]] bool isValid() const noexcept {
        return points.size() >= 3 && std::all_of(points.begin(), points.end(), [](const Point2D& pt) {
                   return std::isfinite(pt.x) && std::isfinite(pt.y);
               });
    }

    [[nodiscard]] bool operator==(const PolygonGeometry& other) const noexcept = default;
};

} // namespace manga::domain
