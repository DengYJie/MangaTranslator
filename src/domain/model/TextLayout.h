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
    std::string fontFamily{"Noto Sans CJK SC"};
    double fontSizePt{14.0};
    int fontWeight{400};
    double letterSpacingEm{0.0};

    std::string textColorHex{"#000000"};
    std::string strokeColorHex{"#FFFFFF"};
    double strokeWidthPx{0.0};

    TextOrientation orientation{TextOrientation::Horizontal};
    TextAlignment alignment{HorizontalAlignment::Center, VerticalAlignment::Middle};

    double lineSpacing{1.2}; // Line advance as a multiple of the font pixel size.
    Margins margins{2.0, 2.0, 2.0, 2.0};

    bool wordWrap{true};
    bool autoFit{false};

    double minFontSizePt{6.0};
    double maxFontSizePt{72.0};

    double rotationDeg{0.0};

    /// Requires positive finite font, minimum font, and line spacing values, a maximum font size at least
    /// the minimum, a font weight that is a multiple of one hundred within the supported range, bounded and
    /// finite letter spacing and stroke width, text and stroke colors written as a hash and six hexadecimal
    /// digits, a finite rotation, and non-negative margins.
    [[nodiscard]] bool isValid() const noexcept;

    [[nodiscard]] bool approxEquals(const TextLayout& other, double epsilon = 1e-5) const noexcept;
    [[nodiscard]] bool operator==(const TextLayout& other) const noexcept;
};

} // namespace manga::domain
