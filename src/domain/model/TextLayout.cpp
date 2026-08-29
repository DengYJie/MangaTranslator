#include "domain/model/TextLayout.h"

#include <cmath>

namespace manga::domain {

namespace {
bool isValidHexColor(const std::string& hex) {
    if (hex.size() != 7 || hex[0] != '#')
        return false;
    for (size_t i = 1; i < 7; ++i) {
        char c = hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}
} // namespace

bool TextLayout::isValid() const noexcept {
    if (std::isnan(fontSizePt) || fontSizePt <= 0.0 || std::isinf(fontSizePt)) {
        return false;
    }
    if (fontWeight < 100 || fontWeight > 900 || fontWeight % 100 != 0) {
        return false;
    }
    if (std::isnan(letterSpacingEm) || letterSpacingEm < -0.5 || letterSpacingEm > 2.0 || std::isinf(letterSpacingEm)) {
        return false;
    }
    if (!isValidHexColor(textColorHex)) {
        return false;
    }
    if (!isValidHexColor(strokeColorHex)) {
        return false;
    }
    if (std::isnan(strokeWidthPx) || strokeWidthPx < 0.0 || strokeWidthPx > 32.0 || std::isinf(strokeWidthPx)) {
        return false;
    }
    if (std::isnan(lineSpacing) || lineSpacing <= 0.0 || std::isinf(lineSpacing)) {
        return false;
    }
    if (std::isnan(minFontSizePt) || minFontSizePt <= 0.0 || std::isinf(minFontSizePt)) {
        return false;
    }
    if (std::isnan(maxFontSizePt) || maxFontSizePt < minFontSizePt || std::isinf(maxFontSizePt)) {
        return false;
    }
    if (std::isnan(rotationDeg) || std::isinf(rotationDeg)) {
        return false;
    }
    return margins.isValid();
}

bool TextLayout::approxEquals(const TextLayout& other, double epsilon) const noexcept {
    return fontFamily == other.fontFamily && std::abs(fontSizePt - other.fontSizePt) <= epsilon &&
           fontWeight == other.fontWeight && std::abs(letterSpacingEm - other.letterSpacingEm) <= epsilon &&
           textColorHex == other.textColorHex && strokeColorHex == other.strokeColorHex &&
           std::abs(strokeWidthPx - other.strokeWidthPx) <= epsilon && orientation == other.orientation &&
           alignment == other.alignment && std::abs(lineSpacing - other.lineSpacing) <= epsilon &&
           margins.approxEquals(other.margins, epsilon) && wordWrap == other.wordWrap && autoFit == other.autoFit &&
           std::abs(minFontSizePt - other.minFontSizePt) <= epsilon &&
           std::abs(maxFontSizePt - other.maxFontSizePt) <= epsilon &&
           std::abs(rotationDeg - other.rotationDeg) <= epsilon;
}

bool TextLayout::operator==(const TextLayout& other) const noexcept {
    return approxEquals(other);
}

} // namespace manga::domain
