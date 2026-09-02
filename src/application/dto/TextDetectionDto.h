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
    Text,         // Dialogue, captions, titles, and general text
    Onomatopoeia, // Comic sound effects and other stylized text
    Bubble,       // Speech bubble boundary, used while associating text with its bubble
    Panel         // Comic panel frame that orders the reading sequence
};

/// A single detected bounding region with classification and confidence. All geometry is in the
/// pixel coordinates of the image the detector received.
struct DetectedRegionItem {
    domain::RectGeometry rect;
    QPolygonF polygon;
    DetectedRegionClass regionClass{DetectedRegionClass::Text};
    std::optional<domain::RectGeometry> bubbleRect;
    std::optional<domain::PolygonGeometry> bubblePolygon;
    double confidence{0.0};
    int orderIndex{0};

    friend bool operator==(const DetectedRegionItem&, const DetectedRegionItem&) = default;
};

/// Detection request for one page. The image fixes the coordinate space the response reports in.
/// minConfidence gates text and onomatopoeia only, because bubble and panel acceptance uses a
/// threshold the detector owns, and a value that is not positive falls back to that default.
struct TextDetectionRequest {
    domain::PageId pageId;
    QImage image;
    domain::ReadingDirection readingDirection{domain::ReadingDirection::RightToLeft};
    double minConfidence{0.25};
};

/// Detection result for one page. detectedRegions carries text and onomatopoeia items in reading
/// order with orderIndex contiguous from zero, and a bubble holding no text contributes a synthetic
/// text item so its geometry still reaches the consumer. panels lists the detected frame rectangles
/// ordered top to bottom and then sideways according to the requested reading direction.
struct TextDetectionResponse {
    domain::PageId pageId;
    QVector<DetectedRegionItem> detectedRegions;
    QVector<domain::RectGeometry> panels;

    friend bool operator==(const TextDetectionResponse&, const TextDetectionResponse&) = default;
};

} // namespace manga::application
