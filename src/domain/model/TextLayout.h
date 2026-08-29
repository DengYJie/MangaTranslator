#pragma once

#include "domain/model/Geometry.h"

#include <cmath>
#include <compare>
#include <string>

namespace manga::domain {

enum class TextOrientation {
    Horizontal,
    Vertical
};

enum class HorizontalAlignment {
    Left,
    Center,
    Right,
    Justify
};

enum class VerticalAlignment {
    Top,
    Middle,
    Bottom
};

struct TextAlignment {
    HorizontalAlignment horizontal{HorizontalAlignment::Center};
    VerticalAlignment vertical{VerticalAlignment::Middle};

    [[nodiscard]] constexpr auto operator<=>(const TextAlignment&) const noexcept = default;
    [[nodiscard]] constexpr bool operator==(const TextAlignment&) const noexcept = default;
};

/// Text layout configuration for rendering translated text within a region.
struct TextLayout {
    std::string fontFamily{"Sans-Serif"};
    double fontSizePt{14.0};
    int fontWeight{400};
    double letterSpacingEm{0.0};

    std::string textColorHex{"#000000"};
    std::string strokeColorHex{"#FFFFFF"};
    double strokeWidthPx{0.0};

    TextOrientation orientation{TextOrientation::Horizontal};
    TextAlignment alignment{HorizontalAlignment::Center, VerticalAlignment::Middle};

    double lineSpacing{1.2}; // Multiplier (e.g. 1.0, 1.2, 1.5)
    Margins margins{2.0, 2.0, 2.0, 2.0};

    bool wordWrap{true};
    bool autoFit{false};

    double minFontSizePt{6.0};
    double maxFontSizePt{72.0};

    double rotationDeg{0.0}; // Always 0.0 in V0.1

    /// Validates all layout constraints.
    [[nodiscard]] bool isValid() const noexcept;

    [[nodiscard]] bool approxEquals(const TextLayout& other, double epsilon = 1e-5) const noexcept;
    [[nodiscard]] bool operator==(const TextLayout& other) const noexcept;
};

} // namespace manga::domain
