#pragma once

#include "domain/model/ClearSettings.h"
#include "domain/model/Geometry.h"
#include "domain/model/Uuid.h"

#include <QImage>

namespace manga::application {

struct ClearRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    domain::RegionId regionId;
    QImage pageImage;
    domain::RectGeometry region;
    domain::ClearSettings settings;
    quint64 contentRevision;
};

struct ClearResponse {
    domain::RegionId regionId;
    domain::RectGeometry affectedBounds;
    QImage patch;
};

} // namespace manga::application
