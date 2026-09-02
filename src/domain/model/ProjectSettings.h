#pragma once

#include "domain/model/ReadingDirection.h"
#include "domain/model/TextLayout.h"

#include <cmath>
#include <string>

namespace manga::domain {

/// Project-wide default processing, layout, and engine settings.
struct ProjectSettings {
    std::string defaultFontFamily{"Sans-Serif"};
    double defaultFontSizePt{14.0};
    TextOrientation defaultOrientation{TextOrientation::Horizontal};
    ReadingDirection readingDirection{ReadingDirection::RightToLeft};

    std::string ocrEngine{"manga-ocr"};
    std::string translatorEngine{"openai-compatible"};

    /// Requires a positive default font size and leaves every other setting unconstrained.
    [[nodiscard]] bool isValid() const noexcept {
        return !std::isnan(defaultFontSizePt) && defaultFontSizePt > 0.0;
    }

    [[nodiscard]] bool operator==(const ProjectSettings& other) const noexcept = default;
};

} // namespace manga::domain
