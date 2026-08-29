#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/LanguageCode.h"
#include "domain/model/Uuid.h"

#include <QImage>
#include <QString>

namespace manga::application {

struct OcrRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    QImage image; // Full-page normalized working image; the adapter crops internally.
    domain::RectGeometry rect;
    domain::LanguageCode language;
};

struct OcrResponse {
    domain::RegionId regionId;
    QString text;
    double confidence{0.0};
};

} // namespace manga::application
