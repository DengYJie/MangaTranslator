#include "ui/manga/MangaTextItem.h"

#include "rendering/TextRasterizer.h"
#include "ui/manga/CanvasZOrder.h"

#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>

namespace manga::ui::manga {

MangaTextItem::MangaTextItem(domain::RegionId id, QGraphicsItem* parent) : QGraphicsItem(parent) {
    m_display.id = id;
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(canvas::kZTextLayer);
    // Rasterization rebuilds a QTextLayout on every draw, so without a cache each pan repaints every
    // visible region by re-running line breaking. The cache is regenerated on zoom and on display change.
    setCacheMode(QGraphicsItem::DeviceCoordinateCache);
}

void MangaTextItem::setDisplay(const TextLayerDisplay& display) {
    const bool geometryChanged = m_display.geometry.width != display.geometry.width ||
                                 m_display.geometry.height != display.geometry.height ||
                                 strokeMargin() != std::max<qreal>(2.0, display.layout.strokeWidthPx * 0.5);
    const bool layoutChanged = !m_hasCachedLayout || m_display.text != display.text ||
                               m_display.geometry.width != display.geometry.width ||
                               m_display.geometry.height != display.geometry.height ||
                               m_display.layout != display.layout || m_display.logicalDpi != display.logicalDpi;

    if (geometryChanged) {
        prepareGeometryChange();
    }

    m_display = display;
    setPos(QPointF(display.geometry.x, display.geometry.y));
    setVisible(display.visible);

    if (layoutChanged) {
        updateLayout();
    }
}

void MangaTextItem::setTransientGeometry(const domain::RectGeometry& geometry) {
    if (m_display.geometry.approxEquals(geometry)) {
        return;
    }

    const bool sizeChanged = m_display.geometry.width != geometry.width || m_display.geometry.height != geometry.height;
    if (sizeChanged) {
        prepareGeometryChange();
    }

    m_display.geometry = geometry;
    setPos(QPointF(geometry.x, geometry.y));
    if (sizeChanged) {
        updateLayout();
    }
    update();
}

void MangaTextItem::setLogicalDpi(double logicalDpi) {
    if (m_display.logicalDpi == logicalDpi) {
        return;
    }

    m_display.logicalDpi = logicalDpi;
    updateLayout();
}

QRectF MangaTextItem::boundingRect() const {
    const qreal margin = strokeMargin();
    return QRectF(0.0, 0.0, m_display.geometry.width, m_display.geometry.height)
        .adjusted(-margin, -margin, margin, margin);
}

void MangaTextItem::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    if (painter == nullptr || !m_display.enabled || !m_display.visible || m_display.text.isEmpty() ||
        !m_hasCachedLayout) {
        return;
    }

    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setRenderHint(QPainter::TextAntialiasing, true);
    rendering::TextRasterizer::draw(*painter, m_cachedLayout, m_display.logicalDpi);
}

void MangaTextItem::updateLayout() {
    const domain::RectGeometry localBounds{0.0, 0.0, m_display.geometry.width, m_display.geometry.height};
    m_cachedLayout =
        rendering::TextLayoutEngine::layoutText(m_display.text, localBounds, m_display.layout, m_display.logicalDpi);
    m_hasCachedLayout = true;
}

qreal MangaTextItem::strokeMargin() const noexcept {
    return std::max<qreal>(2.0, static_cast<qreal>(m_display.layout.strokeWidthPx * 0.5));
}

} // namespace manga::ui::manga
