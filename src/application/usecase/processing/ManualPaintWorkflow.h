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

    /// Paints a stroke into an RGBA8888 layer. Each pixel center is measured against the stroke
    /// polyline, and coverage falls linearly from one at half a pixel inside the radius edge to
    /// zero at half a pixel outside it, so the feather is one pixel wide and the painted edge stays
    /// within half a pixel of the ideal disc. Paint composites the color over the destination with
    /// its alpha scaled by that coverage, and erase multiplies destination alpha by one minus it.
    /// An empty point list, a non-positive diameter, or a null image leaves the image unchanged.
    static void rasterizeStroke(
        QImage& image, ManualStrokeMode mode, const QColor& color, double diameter, const QVector<QPointF>& points);

private:
    IDerivedAssetStore* m_store{nullptr};
};

} // namespace manga::application
