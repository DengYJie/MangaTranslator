#pragma once

#include "application/dto/OcrDto.h"
#include "application/error/ApplicationError.h"

#include <QImage>

#include <vector>

namespace manga::services {

struct PaddleVlPreprocessedData {
    std::vector<float> pixelValues; // shape [1, P, 3, 14, 14]
    std::vector<int64_t> gridThw;   // shape [1, 3] -> {1, H/14, W/14}
    int patchesH{0};
    int patchesW{0};
    int numPatches{0};   // P = patchesH * patchesW
    int mergedTokens{0}; // P / 4
};

/// Preprocessor for PaddleOCR-VL NaViT vision encoder.
class PaddleVlPreprocessor final {
public:
    static constexpr int kPatchSize = 14;
    static constexpr int kMergeSize = 2;
    static constexpr int kMinPixels = 112896;  // from preprocessor_config.json
    static constexpr int kMaxPixels = 1003520; // from preprocessor_config.json

    [[nodiscard]] static application::Result<PaddleVlPreprocessedData>
    preprocess(const application::OcrRequest& request);
};

} // namespace manga::services
