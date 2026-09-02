#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "domain/model/Uuid.h"

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace manga::application {

enum class ManualStrokeMode {
    Paint,
    Erase
};

struct ManualPagePaintRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    QString projectRoot;
    quint64 expectedSessionGeneration{0};
    quint64 currentSessionGeneration{0};
    QSize expectedPageSize;
    QString expectedManualLayerRef;
    ManualStrokeMode mode{ManualStrokeMode::Paint};
    QColor color{17, 17, 17, 255};
    double diameter{48.0};
    QVector<QPointF> points;
    QRect dirtyBounds;
    OperationContext context;
};

struct PreparedPageManualOverrideChange {
    domain::ProjectId projectId;
    domain::PageId pageId;
    QString beforeRef;
    QString afterRef;
    quint64 expectedSessionGeneration{0};
    QSize expectedPageSize;
};

} // namespace manga::application
