#pragma once

#include "application/dto/TextDetectionDto.h"
#include "services/detection/KoharuLayoutPreprocessor.h"

#include <vector>

namespace manga::services {

class KoharuLayoutPostprocessor {
public:
    struct RawOutputs {
        // RF-DETR native outputs: pred_logits [1, N, C] and pred_boxes [1, N, 4] (cx,cy,w,h normalized)
        const float* logits{nullptr}; // pred_logits, shape [1, num_queries, num_classes]
        std::vector<int64_t> logitsShape;
        const float* boxes{nullptr}; // pred_boxes, shape [1, num_queries, 4], cx/cy/w/h in [0,1]
        std::vector<int64_t> boxesShape;

        // Pre-processed fallback (legacy): dets [N, 5+] and labels [N]
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
            double minConfidence = 0.25,
            domain::ReadingDirection readingDirection = domain::ReadingDirection::RightToLeft);
};

} // namespace manga::services
