#include "KoharuLayoutPostprocessor.h"

#include <algorithm>
#include <cmath>

namespace manga::services {

namespace {

struct InternalItem {
    domain::RectGeometry rect;
    std::optional<domain::RectGeometry> bubbleRect;
    application::DetectedRegionClass regionClass{application::DetectedRegionClass::Text};
    double confidence{0.0};
    int panelIndex{-1};
};

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

    if (raw.dets == nullptr || raw.labels == nullptr || prep.scale <= 0.0) {
        return response;
    }

    // Determine number of predictions
    size_t numDets = 0;
    size_t boxDim = 4;
    if (raw.detsShape.size() == 3) {
        numDets = static_cast<size_t>(raw.detsShape[1]);
        boxDim = static_cast<size_t>(raw.detsShape[2]);
    } else if (raw.detsShape.size() == 2) {
        numDets = static_cast<size_t>(raw.detsShape[0]);
        boxDim = static_cast<size_t>(raw.detsShape[1]);
    }

    QVector<domain::RectGeometry> rawPanels;
    QVector<InternalItem> candidateItems;

    for (size_t i = 0; i < numDets; ++i) {
        const float* boxPtr = raw.dets + i * boxDim;
        const int64_t label = raw.labels[i];

        float x1 = boxPtr[0];
        float y1 = boxPtr[1];
        float x2 = boxPtr[2];
        float y2 = boxPtr[3];
        float score = 1.0f;
        if (boxDim >= 5) {
            score = boxPtr[4];
        }

        if (score < static_cast<float>(minConfidence)) {
            continue;
        }

        // Check if coordinates are normalized [0, 1]
        if (x1 <= 1.0f && y1 <= 1.0f && x2 <= 1.0f && y2 <= 1.0f && (x2 > x1 || y2 > y1)) {
            x1 *= prep.targetWidth;
            y1 *= prep.targetHeight;
            x2 *= prep.targetWidth;
            y2 *= prep.targetHeight;
        }

        // If coordinates are in [cx, cy, w, h] format
        if (x2 < x1 || y2 < y1) {
            const float cx = x1;
            const float cy = y1;
            const float w = x2;
            const float h = y2;
            x1 = cx - w / 2.0f;
            y1 = cy - h / 2.0f;
            x2 = cx + w / 2.0f;
            y2 = cy + h / 2.0f;
        }

        // Unpad and rescale back to original image space
        const double origX1 = std::clamp((x1 - prep.padX) / prep.scale, 0.0, static_cast<double>(prep.originalWidth));
        const double origY1 = std::clamp((y1 - prep.padY) / prep.scale, 0.0, static_cast<double>(prep.originalHeight));
        const double origX2 = std::clamp((x2 - prep.padX) / prep.scale, 0.0, static_cast<double>(prep.originalWidth));
        const double origY2 = std::clamp((y2 - prep.padY) / prep.scale, 0.0, static_cast<double>(prep.originalHeight));

        const double width = origX2 - origX1;
        const double height = origY2 - origY1;

        if (width < 3.0 || height < 3.0) {
            continue;
        }

        domain::RectGeometry rect;
        rect.x = origX1;
        rect.y = origY1;
        rect.width = width;
        rect.height = height;

        if (label == 3) {
            // Panel frame
            rawPanels.append(rect);
        } else if (label == 2) {
            // Speech bubble
            InternalItem item;
            item.rect = rect;
            item.confidence = score;
            item.regionClass = application::DetectedRegionClass::Bubble;
            candidateItems.append(item);
        } else {
            InternalItem item;
            item.rect = rect;
            item.confidence = score;
            item.regionClass =
                (label == 1) ? application::DetectedRegionClass::Onomatopoeia : application::DetectedRegionClass::Text;
            candidateItems.append(item);
        }
    }

    // Separate text/sfx items and bubble items
    QVector<InternalItem> textItems;
    QVector<domain::RectGeometry> rawBubbles;
    for (const auto& item : candidateItems) {
        if (item.regionClass == application::DetectedRegionClass::Bubble) {
            rawBubbles.append(item.rect);
        } else {
            textItems.append(item);
        }
    }

    QVector<bool> bubbleHasText(rawBubbles.size(), false);

    // Associate each text item with its containing speech bubble
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
                if (overlapRatio > 0.35 && overlapRatio > bestOverlap) {
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

    // For any speech bubble that has no text inside it, create a default centered text box
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

    // Sort detected panels: Top-to-Bottom, Right-to-Left (for RTL)
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

    // Associate candidate text items with containing panel
    for (auto& item : textItems) {
        for (int p = 0; p < rawPanels.size(); ++p) {
            if (intersectsOrContains(rawPanels.at(p), item.rect)) {
                item.panelIndex = p;
                break;
            }
        }
    }

    // Sort candidate items by Panel order, then by Japanese / Manga reading order
    std::sort(textItems.begin(), textItems.end(), [readingDirection](const InternalItem& a, const InternalItem& b) {
        if (a.panelIndex != b.panelIndex) {
            if (a.panelIndex == -1)
                return false;
            if (b.panelIndex == -1)
                return true;
            return a.panelIndex < b.panelIndex;
        }

        // Within same panel / root:
        // RTL manga: columns from Right to Left, then Top to Bottom
        // LTR comic: lines from Top to Bottom, then Left to Right
        if (readingDirection == domain::ReadingDirection::RightToLeft) {
            const double centerAx = a.rect.x + a.rect.width / 2.0;
            const double centerAy = a.rect.y + a.rect.height / 2.0;
            const double centerBx = b.rect.x + b.rect.width / 2.0;
            const double centerBy = b.rect.y + b.rect.height / 2.0;

            const double scoreA = -centerAx + 1.6 * centerAy;
            const double scoreB = -centerBx + 1.6 * centerBy;
            return scoreA < scoreB;
        } else {
            const double centerAx = a.rect.x + a.rect.width / 2.0;
            const double centerAy = a.rect.y + a.rect.height / 2.0;
            const double centerBx = b.rect.x + b.rect.width / 2.0;
            const double centerBy = b.rect.y + b.rect.height / 2.0;

            const double scoreA = 1.6 * centerAy + centerAx;
            const double scoreB = 1.6 * centerBy + centerBx;
            return scoreA < scoreB;
        }
    });

    // Populate response detectedRegions with monotonic orderIndex
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
