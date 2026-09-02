#pragma once

#include "application/dto/ManualPaintDto.h"
#include "application/error/ApplicationError.h"
#include "application/ports/IDerivedAssetStore.h"

#include <QFuture>

namespace manga::application {

/// Performs one page-level brush paint or erase stroke on the manual override layer.
/// The resulting immutable asset is adopted by PageManualOverrideCommand only after stale guards pass.
class ManualPaintWorkflow {
public:
    explicit ManualPaintWorkflow(IDerivedAssetStore* store) : m_store(store) {}

    [[nodiscard]] QFuture<Result<PreparedPageManualOverrideChange>> execute(ManualPagePaintRequest request);

    /// Core rasterization algorithm matching Koharu's distance field coverage calculation.
    static void rasterizeStroke(
        QImage& image, ManualStrokeMode mode, const QColor& color, double diameter, const QVector<QPointF>& points);

private:
    IDerivedAssetStore* m_store{nullptr};
};

} // namespace manga::application
