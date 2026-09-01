#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/ReadingDirection.h"
#include "domain/model/Uuid.h"

#include <QImage>
#include <QPolygonF>
#include <QString>
#include <QVector>

namespace manga::application {

/// Target classes predicted by the comic layout detection model.
enum class DetectedRegionClass {
    Text,         ///< 0: Dialogue, captions, titles, general text
    Onomatopoeia, ///< 1: Comic sound effects / SFX / stylized text
    Bubble,       ///< 2: Speech balloon / dialogue bubble boundary
    Panel         ///< 3: Comic panel frame / layout boundary
};

/// A single detected bounding region with classification and confidence.
struct DetectedRegionItem {
    domain::RectGeometry rect;
    QPolygonF polygon;
    DetectedRegionClass regionClass{DetectedRegionClass::Text};
    std::optional<domain::RectGeometry> bubbleRect;
    double confidence{0.0};
    int orderIndex{0};

    friend bool operator==(const DetectedRegionItem&, const DetectedRegionItem&) = default;
};

/// Input request payload for text and layout detection on a page.
struct TextDetectionRequest {
    domain::PageId pageId;
    QImage image;
    domain::ReadingDirection readingDirection{domain::ReadingDirection::RightToLeft};
    double minConfidence{0.35};
};

/// Output response containing recognized text regions and panels.
struct TextDetectionResponse {
    domain::PageId pageId;
    QVector<DetectedRegionItem> detectedRegions;
    QVector<domain::RectGeometry> panels;

    friend bool operator==(const TextDetectionResponse&, const TextDetectionResponse&) = default;
};

} // namespace manga::application
