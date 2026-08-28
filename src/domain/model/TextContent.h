#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace manga::domain {

/// Origin/provenance of the source text in a region.
enum class TextSourceType {
    OcrAuto,           // Automatically recognized via OCR
    OcrManualModified, // Initially recognized via OCR, then edited by user
    PureManual         // Manually typed or created from scratch by user
};

/// Source text content with provenance and confidence tracking.
struct TextContent {
    std::string text;
    TextSourceType sourceType{TextSourceType::PureManual};
    double confidence{1.0};
    std::string ocrEngine;
    uint64_t revision{0};

    [[nodiscard]] bool isValid() const noexcept {
        return !std::isnan(confidence) && confidence >= 0.0 && confidence <= 1.0;
    }

    /// Sets automated OCR result and marks source as OcrAuto.
    void setOcrResult(std::string recognizedText, double ocrConfidence, std::string engineName = "") {
        text = std::move(recognizedText);
        confidence = std::clamp(ocrConfidence, 0.0, 1.0);
        ocrEngine = std::move(engineName);
        sourceType = TextSourceType::OcrAuto;
        ++revision;
    }

    /// Records manual modification of OCR or existing text.
    void setManualText(std::string modifiedText) {
        text = std::move(modifiedText);
        if (sourceType == TextSourceType::OcrAuto) {
            sourceType = TextSourceType::OcrManualModified;
        }
        ++revision;
    }

    /// Sets pure manual input from scratch.
    void setPureManual(std::string manualText) {
        text = std::move(manualText);
        sourceType = TextSourceType::PureManual;
        confidence = 1.0;
        ocrEngine.clear();
        ++revision;
    }

    [[nodiscard]] bool operator==(const TextContent& other) const noexcept {
        return text == other.text && sourceType == other.sourceType &&
               std::abs(confidence - other.confidence) <= 1e-5 && ocrEngine == other.ocrEngine &&
               revision == other.revision;
    }
};

} // namespace manga::domain
