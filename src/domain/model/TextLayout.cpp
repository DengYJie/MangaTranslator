#include "domain/model/TextLayout.h"

#include <cmath>

namespace manga::domain {

bool TextLayout::isValid() const noexcept {
    if (std::isnan(fontSizePt) || fontSizePt <= 0.0 || std::isinf(fontSizePt)) {
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
           orientation == other.orientation && alignment == other.alignment &&
           std::abs(lineSpacing - other.lineSpacing) <= epsilon && margins.approxEquals(other.margins, epsilon) &&
           wordWrap == other.wordWrap && autoFit == other.autoFit &&
           std::abs(minFontSizePt - other.minFontSizePt) <= epsilon &&
           std::abs(maxFontSizePt - other.maxFontSizePt) <= epsilon &&
           std::abs(rotationDeg - other.rotationDeg) <= epsilon;
}

bool TextLayout::operator==(const TextLayout& other) const noexcept {
    return approxEquals(other);
}

} // namespace manga::domain
