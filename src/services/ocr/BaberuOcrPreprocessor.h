#pragma once

#include "application/dto/OcrDto.h"
#include "application/error/ApplicationError.h"
#include "services/ocr/BaberuManifest.h"

#include <QImage>

#include <vector>

namespace manga::services {

// Preprocesses an OcrRequest image region into a float32 NCHW tensor [1,3,H,W]
// following the onnx_infer.py pipeline: crop, RGB convert, bicubic resize,
// per-channel normalize. Bicubic uses a hand-written 4-tap kernel to match
// PIL Image.BICUBIC exactly, satisfying the 1e-6 golden tolerance requirement.
class BaberuOcrPreprocessor final {
public:
    // Returns input.invalid_ocr_region if the rect does not intersect the image
    // by at least 2x2 pixels after floor/ceil expansion and clamping.
    [[nodiscard]] static application::Result<std::vector<float>> preprocess(const application::OcrRequest& request,
                                                                            const OcrModelManifest& manifest);

private:
    // Bicubic kernel matching PIL BICUBIC (keys = -0.5).
    static float bicubicWeight(float t) noexcept;
    static QImage bicubicResize(const QImage& src, int dstW, int dstH);
};

} // namespace manga::services
