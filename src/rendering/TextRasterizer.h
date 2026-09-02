#pragma once

#include "rendering/TextLayoutEngine.h"

#include <QImage>
#include <QPainter>

namespace manga::rendering {

class TextRasterizer final {
public:
    /// Draws the layout result onto an existing QPainter.
    static void draw(QPainter& painter, const LayoutResult& layout, double logicalDpi = 72.0);

    /// Renders the layout onto a new transparent QImage of the specified size.
    static QImage renderToImage(const LayoutResult& layout, int width, int height, double logicalDpi = 72.0);
};

} // namespace manga::rendering
