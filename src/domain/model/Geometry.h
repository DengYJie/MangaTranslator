#pragma once

#include <cmath>
#include <optional>

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

    /// Checks if dimensions meet the 2x2 minimum size and non-negative constraints.
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

} // namespace manga::domain
