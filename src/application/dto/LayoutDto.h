#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/TextLayout.h"
#include "domain/model/Uuid.h"

#include <QString>

namespace manga::application {

struct LayoutRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    QString text;
    domain::RectGeometry bounds;
    domain::TextLayout constraints;
    double logicalDpi{96.0};
    quint64 contentRevision;
};

struct LayoutResponse {
    domain::RegionId regionId;
    domain::TextLayout resolvedLayout;
    bool overflow{false};
};

} // namespace manga::application
