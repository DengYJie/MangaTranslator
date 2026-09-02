#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/TextLayout.h"
#include "domain/model/Uuid.h"

#include <QString>
#include <QVector>

namespace manga::application {

/// Non-blocking warning attached to a successful layout result.
struct LayoutWarning {
    QString code; // layout.font_missing or layout.missing_glyph
    QString safeMessage;
};

struct LayoutRequest {
    domain::PageId pageId;
    domain::RegionId regionId;
    QString text;
    domain::RectGeometry bounds;
    domain::TextLayout constraints;
    double logicalDpi{72.0};
    quint64 contentRevision;
};

struct LayoutResponse {
    domain::RegionId regionId;
    domain::TextLayout resolvedLayout;
    bool overflow{false};
    QVector<LayoutWarning> warnings;
};

} // namespace manga::application
