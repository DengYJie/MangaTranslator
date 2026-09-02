#include "KoharuLayoutPostprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace manga::services {

namespace {

// Class index constants matching Koharu RF-DETR label order:
// 0: text, 1: onomatopoeia, 2: bubble, 3: panel, 4: background (suppressed)
constexpr int kClassText = 0;
constexpr int kClassOnomatopoeia = 1;
constexpr int kClassBubble = 2;
constexpr int kClassPanel = 3;
constexpr int kClassBackground = 4;
constexpr int kNumClasses = 5;

// Low-resolution mask size as emitted by the model.
constexpr int kMaskEdge = 288;

struct RawDetection {
    float x1{0};
    float y1{0};
    float x2{0};
    float y2{0};
    float score{0};
    int label{0};
    int64_t queryIndex{-1}; // index into the masks tensor row
};

struct InternalItem {
    domain::RectGeometry rect;
    std::optional<domain::RectGeometry> bubbleRect;
    application::DetectedRegionClass regionClass{application::DetectedRegionClass::Text};
    double confidence{0.0};
    int panelIndex{-1};
};

// Sigmoid activation
inline float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

// Intersection over union on [x1,y1,x2,y2] absolute coords
float iou(const RawDetection& a, const RawDetection& b) {
    const float ix1 = std::max(a.x1, b.x1);
    const float iy1 = std::max(a.y1, b.y1);
    const float ix2 = std::min(a.x2, b.x2);
    const float iy2 = std::min(a.y2, b.y2);
    const float iw = std::max(0.0f, ix2 - ix1);
    const float ih = std::max(0.0f, iy2 - iy1);
    const float intersection = iw * ih;
    if (intersection <= 0.0f) {
        return 0.0f;
    }
    const float aArea = std::max(0.0f, a.x2 - a.x1) * std::max(0.0f, a.y2 - a.y1);
    const float bArea = std::max(0.0f, b.x2 - b.x1) * std::max(0.0f, b.y2 - b.y1);
    const float unionArea = aArea + bArea - intersection;
    return unionArea > 0.0f ? intersection / unionArea : 0.0f;
}

// Non-maximum suppression per class (IoU threshold 0.5, matching Koharu)
std::vector<RawDetection> nonMaximumSuppression(std::vector<RawDetection> dets, float iouThreshold) {
    std::stable_sort(
        dets.begin(), dets.end(), [](const RawDetection& a, const RawDetection& b) { return a.score > b.score; });

    std::vector<bool> suppressed(dets.size(), false);
    std::vector<RawDetection> kept;
    kept.reserve(dets.size());

    for (size_t i = 0; i < dets.size(); ++i) {
        if (suppressed[i]) {
            continue;
        }
        kept.push_back(dets[i]);
        for (size_t j = i + 1; j < dets.size(); ++j) {
            if (!suppressed[j] && dets[j].label == dets[i].label && iou(dets[i], dets[j]) >= iouThreshold) {
                suppressed[j] = true;
            }
        }
    }
    return kept;
}

// Compute the tight bounding rect of positive mask pixels in original-image coordinates.
// The raw mask is kMaskEdge x kMaskEdge floats (pre-sigmoid logits); pixel is "on" when > 0.
// Bilinear upsampling is approximated by mapping mask pixel centers to image space.
// Returns false (leaving rect unchanged) when the mask is entirely empty.
bool maskTightRect(const float* maskData, int origWidth, int origHeight, float& x1, float& y1, float& x2, float& y2) {
    // Scale factors from mask coords to original image coords (pixel center mapping)
    const float scaleX = static_cast<float>(origWidth) / static_cast<float>(kMaskEdge);
    const float scaleY = static_cast<float>(origHeight) / static_cast<float>(kMaskEdge);

    float minX = std::numeric_limits<float>::max();
    float minY = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float maxY = std::numeric_limits<float>::lowest();
    bool found = false;

    for (int my = 0; my < kMaskEdge; ++my) {
        for (int mx = 0; mx < kMaskEdge; ++mx) {
            // Threshold at 0 (matches Koharu: mask.gt(0.0), i.e. sigmoid > 0.5)
            if (maskData[my * kMaskEdge + mx] > 0.0f) {
                // Map mask pixel center to original image space
                const float imgX = (static_cast<float>(mx) + 0.5f) * scaleX;
                const float imgY = (static_cast<float>(my) + 0.5f) * scaleY;
                if (imgX < minX)
                    minX = imgX;
                if (imgY < minY)
                    minY = imgY;
                if (imgX > maxX)
                    maxX = imgX;
                if (imgY > maxY)
                    maxY = imgY;
                found = true;
            }
        }
    }

    if (!found) {
        return false;
    }

    x1 = std::clamp(minX, 0.0f, static_cast<float>(origWidth));
    y1 = std::clamp(minY, 0.0f, static_cast<float>(origHeight));
    x2 = std::clamp(maxX, 0.0f, static_cast<float>(origWidth));
    y2 = std::clamp(maxY, 0.0f, static_cast<float>(origHeight));
    return true;
}

// Decode RF-DETR native outputs: pred_logits [1,N,C] + pred_boxes [1,N,4]
// If masks is non-null, refine each detection's bounding rect from the mask.
std::vector<RawDetection> decodeNativeRfDetr(const float* logits,
                                             const std::vector<int64_t>& logitsShape,
                                             const float* boxes,
                                             const std::vector<int64_t>& boxesShape,
                                             const float* masks,
                                             const std::vector<int64_t>& masksShape,
                                             int origWidth,
                                             int origHeight,
                                             float minConfidence) {
    if (logitsShape.size() < 3 || boxesShape.size() < 3) {
        return {};
    }
    const int64_t numQueries = logitsShape[1];
    const int64_t numClasses = logitsShape[2];
    if (numQueries <= 0 || numClasses < kNumClasses) {
        return {};
    }

    // Check mask tensor: expected [1, N, kMaskEdge, kMaskEdge]
    const bool hasMasks = masks != nullptr && masksShape.size() == 4 && masksShape[1] == numQueries &&
                          masksShape[2] == kMaskEdge && masksShape[3] == kMaskEdge;

    const float W = static_cast<float>(origWidth);
    const float H = static_cast<float>(origHeight);
    const int64_t maskPixels = kMaskEdge * kMaskEdge;

    std::vector<RawDetection> result;
    result.reserve(static_cast<size_t>(numQueries));

    for (int64_t q = 0; q < numQueries; ++q) {
        const float* logitRow = logits + q * numClasses;
        const float* boxRow = boxes + q * 4;

        // Apply sigmoid; skip background (index 4)
        for (int c = 0; c < kClassBackground; ++c) {
            const float score = sigmoid(logitRow[c]);
            if (score < minConfidence) {
                continue;
            }

            // Fallback bounding box from pred_boxes (cx,cy,w,h normalized)
            const float cx = boxRow[0];
            const float cy = boxRow[1];
            const float bw = boxRow[2];
            const float bh = boxRow[3];
            float x1 = std::clamp((cx - bw / 2.0f) * W, 0.0f, W);
            float y1 = std::clamp((cy - bh / 2.0f) * H, 0.0f, H);
            float x2 = std::clamp((cx + bw / 2.0f) * W, 0.0f, W);
            float y2 = std::clamp((cy + bh / 2.0f) * H, 0.0f, H);

            // Refine with mask: bilinear upsample mask → threshold at 0 → tight bounding rect
            // This is the critical step that makes Koharu's bubbles conform to actual shape.
            if (hasMasks) {
                const float* maskData = masks + q * maskPixels;
                float mx1, my1, mx2, my2;
                if (maskTightRect(maskData, origWidth, origHeight, mx1, my1, mx2, my2)) {
                    x1 = mx1;
                    y1 = my1;
                    x2 = mx2;
                    y2 = my2;
                }
            }

            if (x2 - x1 < 3.0f || y2 - y1 < 3.0f) {
                continue;
            }

            result.push_back({x1, y1, x2, y2, score, c, q});
        }
    }
    return result;
}

// Decode pre-processed legacy format: dets [N, 5+] with score at index 4, labels int64
std::vector<RawDetection> decodeLegacyFormat(const float* dets,
                                             const std::vector<int64_t>& detsShape,
                                             const int64_t* labelData,
                                             int origWidth,
                                             int origHeight,
                                             float minConfidence) {
    size_t numDets = 0;
    size_t boxDim = 4;
    if (detsShape.size() == 3) {
        numDets = static_cast<size_t>(detsShape[1]);
        boxDim = static_cast<size_t>(detsShape[2]);
    } else if (detsShape.size() == 2) {
        numDets = static_cast<size_t>(detsShape[0]);
        boxDim = static_cast<size_t>(detsShape[1]);
    }

    const float W = static_cast<float>(origWidth);
    const float H = static_cast<float>(origHeight);

    std::vector<RawDetection> result;
    result.reserve(numDets);

    for (size_t i = 0; i < numDets; ++i) {
        const float* ptr = dets + i * boxDim;
        const int64_t label = labelData[i];

        float x1 = ptr[0];
        float y1 = ptr[1];
        float x2 = ptr[2];
        float y2 = ptr[3];
        float score = (boxDim >= 5) ? ptr[4] : 1.0f;

        if (score < minConfidence) {
            continue;
        }

        if (x1 <= 1.0f && y1 <= 1.0f && x2 <= 1.0f && y2 <= 1.0f) {
            x1 *= W;
            y1 *= H;
            x2 *= W;
            y2 *= H;
        }

        if (x2 < x1 || y2 < y1) {
            const float bw = x2, bh = y2;
            x1 = x1 - bw / 2.0f;
            y1 = y1 - bh / 2.0f;
            x2 = x1 + bw;
            y2 = y1 + bh;
        }

        x1 = std::clamp(x1, 0.0f, W);
        y1 = std::clamp(y1, 0.0f, H);
        x2 = std::clamp(x2, 0.0f, W);
        y2 = std::clamp(y2, 0.0f, H);

        if (x2 - x1 < 3.0f || y2 - y1 < 3.0f) {
            continue;
        }

        result.push_back({x1, y1, x2, y2, score, static_cast<int>(label), -1});
    }
    return result;
}

bool intersectsOrContains(const domain::RectGeometry& panel, const domain::RectGeometry& item) {
    const double centerX = item.x + item.width / 2.0;
    const double centerY = item.y + item.height / 2.0;
    return centerX >= panel.x && centerX <= (panel.x + panel.width) && centerY >= panel.y &&
           centerY <= (panel.y + panel.height);
}

} // namespace

application::TextDetectionResponse KoharuLayoutPostprocessor::process(const domain::PageId& pageId,
                                                                      const RawOutputs& raw,
                                                                      const KoharuPreprocessedImage& prep,
                                                                      double minConfidence,
                                                                      domain::ReadingDirection readingDirection) {
    application::TextDetectionResponse response;
    response.pageId = pageId;

    const float confF = static_cast<float>(minConfidence);

    std::vector<RawDetection> rawDets;
    if (raw.logits != nullptr && raw.boxes != nullptr && !raw.logitsShape.empty() && !raw.boxesShape.empty()) {
        rawDets = decodeNativeRfDetr(raw.logits,
                                     raw.logitsShape,
                                     raw.boxes,
                                     raw.boxesShape,
                                     raw.masks,
                                     raw.masksShape,
                                     prep.originalWidth,
                                     prep.originalHeight,
                                     confF);
    } else if (raw.dets != nullptr && raw.labels != nullptr && !raw.detsShape.empty()) {
        rawDets =
            decodeLegacyFormat(raw.dets, raw.detsShape, raw.labels, prep.originalWidth, prep.originalHeight, confF);
    } else {
        return response;
    }

    // NMS with IoU threshold 0.5 (matching Koharu non_maximum_suppression)
    rawDets = nonMaximumSuppression(std::move(rawDets), 0.5f);

    // Split by class
    QVector<domain::RectGeometry> rawPanels;
    QVector<InternalItem> candidateItems;

    for (const auto& det : rawDets) {
        domain::RectGeometry rect;
        rect.x = static_cast<double>(det.x1);
        rect.y = static_cast<double>(det.y1);
        rect.width = static_cast<double>(det.x2 - det.x1);
        rect.height = static_cast<double>(det.y2 - det.y1);

        if (det.label == kClassPanel) {
            rawPanels.append(rect);
        } else if (det.label == kClassBubble) {
            InternalItem item;
            item.rect = rect;
            item.confidence = det.score;
            item.regionClass = application::DetectedRegionClass::Bubble;
            candidateItems.append(item);
        } else if (det.label == kClassText || det.label == kClassOnomatopoeia) {
            InternalItem item;
            item.rect = rect;
            item.confidence = det.score;
            item.regionClass = (det.label == kClassOnomatopoeia) ? application::DetectedRegionClass::Onomatopoeia
                                                                 : application::DetectedRegionClass::Text;
            candidateItems.append(item);
        }
    }

    // Separate bubbles from text/sfx
    QVector<InternalItem> textItems;
    QVector<domain::RectGeometry> rawBubbles;
    for (const auto& item : candidateItems) {
        if (item.regionClass == application::DetectedRegionClass::Bubble) {
            rawBubbles.append(item.rect);
        } else {
            textItems.append(item);
        }
    }

    // Associate text regions with their containing bubble (containment >= 50% of text area)
    QVector<bool> bubbleHasText(rawBubbles.size(), false);
    for (auto& text : textItems) {
        double bestOverlap = 0.0;
        int bestBubbleIdx = -1;
        for (int b = 0; b < rawBubbles.size(); ++b) {
            const auto& bubble = rawBubbles.at(b);
            const double ix1 = std::max(bubble.x, text.rect.x);
            const double iy1 = std::max(bubble.y, text.rect.y);
            const double ix2 = std::min(bubble.x + bubble.width, text.rect.x + text.rect.width);
            const double iy2 = std::min(bubble.y + bubble.height, text.rect.y + text.rect.height);
            if (ix2 > ix1 && iy2 > iy1) {
                const double intersection = (ix2 - ix1) * (iy2 - iy1);
                const double textArea = text.rect.width * text.rect.height;
                const double overlapRatio = textArea > 0.0 ? (intersection / textArea) : 0.0;
                if (overlapRatio >= 0.5 && overlapRatio > bestOverlap) {
                    bestOverlap = overlapRatio;
                    bestBubbleIdx = b;
                }
            }
        }
        if (bestBubbleIdx >= 0) {
            text.bubbleRect = rawBubbles.at(bestBubbleIdx);
            bubbleHasText[bestBubbleIdx] = true;
        }
    }

    // For bubbles with no associated text, synthesize a default centered text box
    for (int b = 0; b < rawBubbles.size(); ++b) {
        if (!bubbleHasText.at(b)) {
            const auto& bubble = rawBubbles.at(b);
            const double insetX = bubble.width * 0.12;
            const double insetY = bubble.height * 0.12;
            const double textW = std::max(bubble.width - 2.0 * insetX, 16.0);
            const double textH = std::max(bubble.height - 2.0 * insetY, 16.0);

            InternalItem defaultText;
            defaultText.rect = {.x = bubble.x + (bubble.width - textW) / 2.0,
                                .y = bubble.y + (bubble.height - textH) / 2.0,
                                .width = textW,
                                .height = textH};
            defaultText.bubbleRect = bubble;
            defaultText.regionClass = application::DetectedRegionClass::Text;
            defaultText.confidence = 0.8;
            textItems.append(defaultText);
        }
    }

    // Sort panels: top-to-bottom, then RTL or LTR
    std::sort(rawPanels.begin(),
              rawPanels.end(),
              [readingDirection](const domain::RectGeometry& a, const domain::RectGeometry& b) {
                  const double yDiff = a.y - b.y;
                  if (std::abs(yDiff) > 20.0) {
                      return a.y < b.y;
                  }
                  return readingDirection == domain::ReadingDirection::RightToLeft ? a.x > b.x : a.x < b.x;
              });

    response.panels = rawPanels;

    // Associate text items with panels by center-point containment
    for (auto& item : textItems) {
        for (int p = 0; p < rawPanels.size(); ++p) {
            if (intersectsOrContains(rawPanels.at(p), item.rect)) {
                item.panelIndex = p;
                break;
            }
        }
    }

    // Sort: panel order first, then Koharu detection_order (top asc, x desc for RTL)
    std::stable_sort(
        textItems.begin(), textItems.end(), [readingDirection](const InternalItem& a, const InternalItem& b) {
            if (a.panelIndex != b.panelIndex) {
                if (a.panelIndex == -1)
                    return false;
                if (b.panelIndex == -1)
                    return true;
                return a.panelIndex < b.panelIndex;
            }
            const double topA = a.rect.y;
            const double topB = b.rect.y;
            const double yDiff = topA - topB;
            if (std::abs(yDiff) > 20.0) {
                return topA < topB;
            }
            if (readingDirection == domain::ReadingDirection::RightToLeft) {
                return (a.rect.x + a.rect.width / 2.0) > (b.rect.x + b.rect.width / 2.0);
            }
            return (a.rect.x + a.rect.width / 2.0) < (b.rect.x + b.rect.width / 2.0);
        });

    int order = 0;
    for (const auto& item : textItems) {
        application::DetectedRegionItem regionItem;
        regionItem.rect = item.rect;
        regionItem.bubbleRect = item.bubbleRect;
        regionItem.regionClass = item.regionClass;
        regionItem.confidence = item.confidence;
        regionItem.orderIndex = order++;
        response.detectedRegions.append(regionItem);
    }

    return response;
}

} // namespace manga::services
