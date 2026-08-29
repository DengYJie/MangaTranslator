#pragma once

#include "domain/model/Geometry.h"
#include "domain/model/TextLayout.h"

#include <QFont>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTransform>
#include <QVector>

namespace manga::rendering {

struct LayoutWarning {
    QString code;
    QString safeMessage;
};

struct GlyphCluster {
    QString text;
    QPointF position; // Top-left or baseline depending on usage, but typically top-left of the em-box for vertical, or
                      // baseline for horizontal
    QRectF inkBounds; // The ink bounding box of this cluster (including stroke width)
    QRectF emBox;     // The logical em-box rect (optional, useful for debugging/hit-testing)
    bool isVertical{false};
    bool isTateChuYoko{false};
    QTransform transform; // Any additional transform (e.g. 90-degree rotation)
};

struct LayoutResult {
    QString originalText;
    domain::TextLayout resolvedLayout;
    QVector<GlyphCluster> clusters;
    QRectF inkBounds;
    QRectF layoutBounds;
    bool overflow{false};
    QVector<LayoutWarning> warnings;
};

class TextLayoutEngine final {
public:
    TextLayoutEngine();
    ~TextLayoutEngine();

    // The main entry point for computing layout.
    // logicalDpi determines how fontSizePt translates to pixel sizes.
    [[nodiscard]] static LayoutResult layoutText(const QString& text,
                                                 const domain::RectGeometry& bounds,
                                                 const domain::TextLayout& constraints,
                                                 double logicalDpi = 96.0);

private:
    static LayoutResult layoutHorizontal(const QString& text,
                                         const domain::RectGeometry& bounds,
                                         const domain::TextLayout& constraints,
                                         double pixelSize);
    static LayoutResult layoutVertical(const QString& text,
                                       const domain::RectGeometry& bounds,
                                       const domain::TextLayout& constraints,
                                       double pixelSize);
    static void computeAutoFit(const QString& text,
                               const domain::RectGeometry& bounds,
                               const domain::TextLayout& constraints,
                               double logicalDpi,
                               LayoutResult& outResult);
};

} // namespace manga::rendering
