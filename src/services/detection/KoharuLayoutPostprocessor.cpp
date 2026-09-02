#include "KoharuLayoutPostprocessor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace manga::services {

namespace {

// Class indices in the order the detection checkpoint's trained head emits logits, so they follow the
// exported model rather than this file. Background is never decoded as a region.
// 0: text, 1: onomatopoeia, 2: bubble, 3: panel, 4: background
constexpr int kClassText = 0;
constexpr int kClassOnomatopoeia = 1;
constexpr int kClassBubble = 2;
constexpr int kClassPanel = 3;
constexpr int kClassBackground = 4;
constexpr int kNumClasses = 5;

// Confidence floor per class, indexed by the constants above and carried over from the Koharu
// reference detector. Text and onomatopoeia yield to the caller's minConfidence when it is set.
// 0: text = 0.25, 1: onomatopoeia = 0.20, 2: bubble = 0.50, 3: panel = 0.50, 4: background = 1.0
static constexpr float kRecommendedThresholds[kNumClasses] = {0.25f, 0.20f, 0.50f, 0.50f, 1.0f};

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
    std::optional<domain::PolygonGeometry> bubblePolygon;
    application::DetectedRegionClass regionClass{application::DetectedRegionClass::Text};
    double confidence{0.0};
    int panelIndex{-1};
};

struct BubbleItem {
    domain::RectGeometry rect;
    std::optional<domain::PolygonGeometry> polygon;
};

// 8-neighborhood offsets in clockwise order starting from Left (West)
// 0: (-1, 0) W, 1: (-1, -1) NW, 2: (0, -1) N, 3: (1, -1) NE
// 4: (1, 0) E, 5: (1, 1) SE, 6: (0, 1) S, 7: (-1, 1) SW
static constexpr int kDx[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
static constexpr int kDy[8] = {0, -1, -1, -1, 0, 1, 1, 1};

inline bool isMaskPixel(const float* maskData, int x, int y) {
    if (x < 0 || x >= kMaskEdge || y < 0 || y >= kMaskEdge) {
        return false;
    }
    return maskData[y * kMaskEdge + x] > 0.0f;
}

/// Squared distance from (px, py) to the segment running from (ax, ay) to (bx, by).
double pointToSegmentDistSq(double px, double py, double ax, double ay, double bx, double by) {
    const double l2 = (bx - ax) * (bx - ax) + (by - ay) * (by - ay);
    if (l2 < 1e-12) {
        return (px - ax) * (px - ax) + (py - ay) * (py - ay);
    }
    const double t = std::clamp(((px - ax) * (bx - ax) + (py - ay) * (by - ay)) / l2, 0.0, 1.0);
    const double projX = ax + t * (bx - ax);
    const double projY = ay + t * (by - ay);
    return (px - projX) * (px - projX) + (py - projY) * (py - projY);
}

/// Ramer-Douglas-Peucker simplification over the inclusive range [start, end], marking survivors in keep.
// An explicit stack bounds call depth for jagged contours.
void rdpSimplifyRange(const std::vector<std::pair<double, double>>& pts,
                      size_t start,
                      size_t end,
                      double epsSq,
                      std::vector<bool>& keep) {
    std::vector<std::pair<size_t, size_t>> stack;
    stack.push_back({start, end});
    while (!stack.empty()) {
        const auto [s, e] = stack.back();
        stack.pop_back();
        if (e <= s + 1) {
            continue;
        }
        double maxDistSq = 0.0;
        size_t maxIdx = s;
        const auto [ax, ay] = pts[s];
        const auto [bx, by] = pts[e];
        for (size_t i = s + 1; i < e; ++i) {
            const double dSq = pointToSegmentDistSq(pts[i].first, pts[i].second, ax, ay, bx, by);
            if (dSq > maxDistSq) {
                maxDistSq = dSq;
                maxIdx = i;
            }
        }
        if (maxDistSq > epsSq) {
            keep[maxIdx] = true;
            stack.push_back({s, maxIdx});
            stack.push_back({maxIdx, e});
        }
    }
}

/// Moore-Neighbor boundary trace of the 8-connected component containing (startX, startY), started at
/// its topmost-leftmost pixel. Returns the ordered boundary pixels without repeating the start pixel,
/// or nothing when the walk exceeds its step cap before closing.
/// An unclosed contour must not be emitted: its closing chord would cut across the region and corrupt
/// downstream clearing.
std::optional<std::vector<std::pair<int, int>>> traceBoundary(const float* maskData, int startX, int startY) {
    // Step cap that fails the trace, so a boundary that never returns to its start cannot spin forever.
    constexpr int maxSteps = 4096;

    std::vector<std::pair<int, int>> contour;
    contour.reserve(1024);
    contour.push_back({startX, startY});

    int curX = startX;
    int curY = startY;
    int backtrackDir = 0;

    for (int step = 0; step < maxSteps; ++step) {
        int nextDir = -1;
        for (int i = 0; i < 8; ++i) {
            const int checkDir = (backtrackDir + i) % 8;
            if (isMaskPixel(maskData, curX + kDx[checkDir], curY + kDy[checkDir])) {
                nextDir = checkDir;
                break;
            }
        }
        if (nextDir < 0) {
            return std::nullopt; // isolated pixel has no boundary loop
        }
        const int nx = curX + kDx[nextDir];
        const int ny = curY + kDy[nextDir];
        if (nx == startX && ny == startY) {
            return contour;
        }
        contour.push_back({nx, ny});
        backtrackDir = (nextDir + 5) % 8;
        curX = nx;
        curY = ny;
    }
    return std::nullopt;
}

/// Marks every pixel of the 8-connected component that contains (startX, startY) in visited.
void markComponentVisited(const float* maskData, std::vector<uint8_t>& visited, int startX, int startY) {
    std::vector<std::pair<int, int>> stack;
    stack.push_back({startX, startY});
    visited[startY * kMaskEdge + startX] = 1;
    while (!stack.empty()) {
        const auto [cx, cy] = stack.back();
        stack.pop_back();
        for (int d = 0; d < 8; ++d) {
            const int nx = cx + kDx[d];
            const int ny = cy + kDy[d];
            if (nx < 0 || nx >= kMaskEdge || ny < 0 || ny >= kMaskEdge) {
                continue;
            }
            const int idx = ny * kMaskEdge + nx;
            if (visited[idx] != 0 || maskData[idx] <= 0.0f) {
                continue;
            }
            visited[idx] = 1;
            stack.push_back({nx, ny});
        }
    }
}

/// Shoelace area of a closed pixel contour, in mask-grid units rather than source-image pixels.
double contourArea(const std::vector<std::pair<int, int>>& contour) {
    double twice = 0.0;
    for (size_t i = 0; i < contour.size(); ++i) {
        const auto [x1, y1] = contour[i];
        const auto [x2, y2] = contour[(i + 1) % contour.size()];
        twice += static_cast<double>(x1) * y2 - static_cast<double>(x2) * y1;
    }
    return std::abs(twice) * 0.5;
}

/// Largest traced contour of the mask, simplified and scaled into source-image pixels because the mask
/// grid is a fixed low resolution. Returns nothing when no contour is large enough to simplify.
std::optional<domain::PolygonGeometry> extractBubblePolygon(const float* maskData, int origWidth, int origHeight) {
    std::vector<uint8_t> visited(kMaskEdge * kMaskEdge, 0);
    std::vector<std::pair<int, int>> bestContour;
    double maxArea = -1.0;

    for (int y = 0; y < kMaskEdge; ++y) {
        for (int x = 0; x < kMaskEdge; ++x) {
            if (maskData[y * kMaskEdge + x] > 0.0f && visited[y * kMaskEdge + x] == 0) {
                if (auto contourOpt = traceBoundary(maskData, x, y)) {
                    double area = contourArea(*contourOpt);
                    if (area > maxArea) {
                        maxArea = area;
                        bestContour = std::move(*contourOpt);
                    }
                }
                markComponentVisited(maskData, visited, x, y);
            }
        }
    }

    if (bestContour.size() < 4) {
        return std::nullopt;
    }

    std::vector<std::pair<double, double>> rawContour;
    rawContour.reserve(bestContour.size());
    for (const auto& pt : bestContour) {
        rawContour.push_back({static_cast<double>(pt.first), static_cast<double>(pt.second)});
    }
    // ensure closed loop for RDP by appending the start point at the end
    rawContour.push_back(rawContour.front());

    const double scaleX = static_cast<double>(origWidth) / static_cast<double>(kMaskEdge);
    const double scaleY = static_cast<double>(origHeight) / static_cast<double>(kMaskEdge);

    // Epsilon proportional to the contour perimeter, Koharu's relative arc-length tolerance, so
    // simplification removes the same share of detail at any bubble size.
    double arcLength = 0.0;
    for (size_t i = 0; i + 1 < rawContour.size(); ++i) {
        arcLength +=
            std::hypot(rawContour[i + 1].first - rawContour[i].first, rawContour[i + 1].second - rawContour[i].second);
    }
    // Floored at one source pixel, which is one over scaleX in mask space, so an upscaled mask keeps
    // the detail the source image actually resolved.
    double epsilon = std::max(arcLength * 0.001, 1.0 / std::max(scaleX, 1.0));
    double epsilonSq = epsilon * epsilon;

    size_t furthestIdx = 0;
    double maxDistSq = 0.0;
    const auto [x0, y0] = rawContour[0];
    for (size_t i = 1; i + 1 < rawContour.size(); ++i) {
        const double dx = rawContour[i].first - x0;
        const double dy = rawContour[i].second - y0;
        const double dSq = dx * dx + dy * dy;
        if (dSq > maxDistSq) {
            maxDistSq = dSq;
            furthestIdx = i;
        }
    }

    if (furthestIdx == 0 || furthestIdx >= rawContour.size() - 1) {
        furthestIdx = rawContour.size() / 2;
    }

    std::vector<bool> keep(rawContour.size(), false);
    keep[0] = true;
    keep[furthestIdx] = true;

    rdpSimplifyRange(rawContour, 0, furthestIdx, epsilonSq, keep);
    rdpSimplifyRange(rawContour, furthestIdx, rawContour.size() - 1, epsilonSq, keep);
    const double maxW = static_cast<double>(origWidth);
    const double maxH = static_cast<double>(origHeight);

    domain::PolygonGeometry result;
    result.points.reserve(rawContour.size());

    const size_t numPoints = rawContour.size() - 1;
    for (size_t i = 0; i < numPoints; ++i) {
        if (keep[i]) {
            const double imgX = std::clamp((rawContour[i].first + 0.5) * scaleX, 0.0, maxW);
            const double imgY = std::clamp((rawContour[i].second + 0.5) * scaleY, 0.0, maxH);
            result.points.push_back({imgX, imgY});
        }
    }

    if (!result.isValid()) {
        return std::nullopt;
    }

    return result;
}

// Sigmoid activation
inline float sigmoid(float x) {
    return 1.0f / (1.0f + std::exp(-x));
}

/// Intersection over union of two boxes held as x1, y1, x2, y2 corners in source-image pixels.
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

/// Keeps the highest scoring detection of each label and drops the rest of that label's detections whose
/// box overlaps a kept one by at least iouThreshold.
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

/// Decodes the native graph outputs pred_logits [1, N, C] and pred_boxes [1, N, 4] into boxes already
/// restored to source-image pixels.
std::vector<RawDetection> decodeNativeRfDetr(const float* logits,
                                             const std::vector<int64_t>& logitsShape,
                                             const float* boxes,
                                             const std::vector<int64_t>& boxesShape,
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

    const float W = static_cast<float>(origWidth);
    const float H = static_cast<float>(origHeight);

    std::vector<RawDetection> result;
    result.reserve(static_cast<size_t>(numQueries));

    for (int64_t q = 0; q < numQueries; ++q) {
        const float* logitRow = logits + q * numClasses;
        const float* boxRow = boxes + q * 4;

        // Apply sigmoid; skip background (index 4)
        for (int c = 0; c < kClassBackground; ++c) {
            const float score = sigmoid(logitRow[c]);
            float threshold = kRecommendedThresholds[c];
            if (minConfidence > 0.0f) {
                if (c == kClassText) {
                    threshold = minConfidence;
                } else if (c == kClassOnomatopoeia) {
                    threshold = std::min(minConfidence, 0.20f);
                }
            }
            if (score < threshold) {
                continue;
            }

            // A pred_boxes row is a center plus a size normalized against the graph input, so restoring
            // coordinates scales by the source dimensions and clamps them to the page.
            const float cx = boxRow[0];
            const float cy = boxRow[1];
            const float bw = boxRow[2];
            const float bh = boxRow[3];
            const float x1 = std::clamp((cx - bw / 2.0f) * W, 0.0f, W);
            const float y1 = std::clamp((cy - bh / 2.0f) * H, 0.0f, H);
            const float x2 = std::clamp((cx + bw / 2.0f) * W, 0.0f, W);
            const float y2 = std::clamp((cy + bh / 2.0f) * H, 0.0f, H);

            // Side floor in source-image pixels. It also removes any box the clamp above collapsed.
            if (x2 - x1 < 3.0f || y2 - y1 < 3.0f) {
                continue;
            }

            result.push_back({x1, y1, x2, y2, score, c, q});
        }
    }
    return result;
}

/// Decodes the pre-processed fallback shape, a [N, 5] or wider row per detection holding x1, y1, x2, y2
/// and a score at index 4, with the class id carried separately as int64 labels.
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

        float threshold =
            (label == kClassBubble || label == kClassPanel) ? 0.50f : (minConfidence > 0.0f ? minConfidence : 0.25f);
        if (score < threshold) {
            continue;
        }

        // Exports disagree on the row's space, so a row that still fits the unit square is read as
        // normalized and scaled to the page, otherwise it is already in source-image pixels.
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

        // Same source-image side floor as the native path.
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
        rawDets = decodeNativeRfDetr(
            raw.logits, raw.logitsShape, raw.boxes, raw.boxesShape, prep.originalWidth, prep.originalHeight, confF);
    } else if (raw.dets != nullptr && raw.labels != nullptr && !raw.detsShape.empty()) {
        rawDets =
            decodeLegacyFormat(raw.dets, raw.detsShape, raw.labels, prep.originalWidth, prep.originalHeight, confF);
    } else {
        return response;
    }

    // Suppression overlap of 0.5 IoU, carried over from the Koharu reference detector.
    rawDets = nonMaximumSuppression(std::move(rawDets), 0.5f);

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
            if (raw.masks != nullptr && det.queryIndex >= 0) {
                const float* maskData = raw.masks + det.queryIndex * (kMaskEdge * kMaskEdge);
                item.bubblePolygon = extractBubblePolygon(maskData, prep.originalWidth, prep.originalHeight);
            }
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

    QVector<InternalItem> textItems;
    QVector<BubbleItem> rawBubbles;
    for (const auto& item : candidateItems) {
        if (item.regionClass == application::DetectedRegionClass::Bubble) {
            BubbleItem bItem;
            bItem.rect = item.rect;
            bItem.polygon = item.bubblePolygon;
            rawBubbles.append(bItem);
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
            const double ix1 = std::max(bubble.rect.x, text.rect.x);
            const double iy1 = std::max(bubble.rect.y, text.rect.y);
            const double ix2 = std::min(bubble.rect.x + bubble.rect.width, text.rect.x + text.rect.width);
            const double iy2 = std::min(bubble.rect.y + bubble.rect.height, text.rect.y + text.rect.height);
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
            text.bubbleRect = rawBubbles.at(bestBubbleIdx).rect;
            text.bubblePolygon = rawBubbles.at(bestBubbleIdx).polygon;
            bubbleHasText[bestBubbleIdx] = true;
        }
    }

    // A bubble that no text landed in still needs a region to typeset into, so one is synthesized: the
    // bubble rect inset by a fixed fraction of its own size and floored so a small bubble still yields a
    // usable box.
    for (int b = 0; b < rawBubbles.size(); ++b) {
        if (!bubbleHasText.at(b)) {
            const auto& bubble = rawBubbles.at(b);
            const double insetX = bubble.rect.width * 0.12;
            const double insetY = bubble.rect.height * 0.12;
            const double textW = std::max(bubble.rect.width - 2.0 * insetX, 16.0);
            const double textH = std::max(bubble.rect.height - 2.0 * insetY, 16.0);

            InternalItem defaultText;
            defaultText.rect = {.x = bubble.rect.x + (bubble.rect.width - textW) / 2.0,
                                .y = bubble.rect.y + (bubble.rect.height - textH) / 2.0,
                                .width = textW,
                                .height = textH};
            defaultText.bubbleRect = bubble.rect;
            defaultText.bubblePolygon = bubble.polygon;
            defaultText.regionClass = application::DetectedRegionClass::Text;
            defaultText.confidence = 0.8; // Fixed stand-in, not a measured model score.
            textItems.append(defaultText);
        }
    }

    // Panels are ordered by row, topmost first. Two tops inside the same row band fall back to the
    // reading direction, right to left for manga.
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

    // Reading order: the owning panel first, then the top edge ascending within the panel row band, then
    // horizontal position, right to left for manga. A region outside every panel sorts after the panelled
    // ones.
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
        regionItem.bubblePolygon = item.bubblePolygon;
        regionItem.regionClass = item.regionClass;
        regionItem.confidence = item.confidence;
        regionItem.orderIndex = order++;
        response.detectedRegions.append(regionItem);
    }

    return response;
}

} // namespace manga::services
