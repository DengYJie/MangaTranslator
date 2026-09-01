#pragma once

#include "application/dto/TextDetectionDto.h"
#include "services/detection/KoharuLayoutPreprocessor.h"

#include <vector>

namespace manga::services {

class KoharuLayoutPostprocessor {
public:
    struct RawOutputs {
        const float* dets{nullptr};
        std::vector<int64_t> detsShape;
        const int64_t* labels{nullptr};
        std::vector<int64_t> labelsShape;
        const float* masks{nullptr};
        std::vector<int64_t> masksShape;
    };

    /// Decodes raw ONNX Runtime outputs into sorted detected regions and panel boundaries.
    [[nodiscard]] static application::TextDetectionResponse
    process(const domain::PageId& pageId,
            const RawOutputs& raw,
            const KoharuPreprocessedImage& prep,
            double minConfidence = 0.35,
            domain::ReadingDirection readingDirection = domain::ReadingDirection::RightToLeft);
};

} // namespace manga::services
