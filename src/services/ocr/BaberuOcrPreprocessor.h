#pragma once

#include "application/dto/OcrDto.h"
#include "application/error/ApplicationError.h"
#include "services/ocr/BaberuManifest.h"

#include <QImage>

#include <vector>

namespace manga::services {

/// Turns one OcrRequest region into the float32 NCHW [1, 3, H, W] tensor the vision graph expects: crop,
/// RGB conversion, bicubic resize to the manifest input size, and per-channel normalization.
class BaberuOcrPreprocessor final {
public:
    /// Reports input.invalid_ocr_region for a null image, a region with NaN geometry, a region that the
    /// floor and ceil expansion plus clamping leave under 2x2 pixels, or a manifest declaring a
    /// conversion, resize mode, or resample that this adapter does not implement.
    [[nodiscard]] static application::Result<std::vector<float>> preprocess(const application::OcrRequest& request,
                                                                            const OcrModelManifest& manifest);

private:
    /// Cubic weight of a sample at distance t, and zero once t reaches the two pixel support radius.
    static float bicubicWeight(float t) noexcept;
    static QImage bicubicResize(const QImage& src, int dstW, int dstH);
};

} // namespace manga::services
