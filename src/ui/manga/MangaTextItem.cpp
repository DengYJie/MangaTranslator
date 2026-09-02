#include "ui/manga/MangaTextItem.h"

#include "rendering/TextRasterizer.h"
#include "ui/manga/CanvasZOrder.h"

#include <QDebug>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>

namespace manga::ui::manga {

MangaTextItem::MangaTextItem(domain::RegionId id, QGraphicsItem* parent) : QGraphicsItem(parent) {
    m_display.id = id;
    setAcceptedMouseButtons(Qt::NoButton);
    setZValue(canvas::kZTextLayer);
}

void MangaTextItem::setDisplay(const TextLayerDisplay& display) {
    const bool geometryChanged = m_display.geometry.width != display.geometry.width ||
                                 m_display.geometry.height != display.geometry.height ||
                                 strokeMargin() != std::max<qreal>(2.0, display.layout.strokeWidthPx * 0.5);
    const bool layoutDiff = m_display.layout != display.layout;
    const bool layoutChanged = !m_hasCachedLayout || m_display.text != display.text ||
                               m_display.geometry.width != display.geometry.width ||
                               m_display.geometry.height != display.geometry.height || layoutDiff ||
                               m_display.logicalDpi != display.logicalDpi;

    if (layoutDiff || layoutChanged) {
        qDebug().noquote() << "[MangaTextItem] setDisplay: region=" << QString::fromStdString(m_display.id.toString())
                           << "layoutChanged=" << layoutChanged << "layoutDiff=" << layoutDiff
                           << "oldWeight=" << m_display.layout.fontWeight << "newWeight=" << display.layout.fontWeight
                           << "oldOrient=" << static_cast<int>(m_display.layout.orientation)
                           << "newOrient=" << static_cast<int>(display.layout.orientation)
                           << "oldFamily=" << QString::fromStdString(m_display.layout.fontFamily)
                           << "newFamily=" << QString::fromStdString(display.layout.fontFamily);
    }

    if (geometryChanged) {
        prepareGeometryChange();
    }

    m_display = display;
    setPos(QPointF(display.geometry.x, display.geometry.y));
    setVisible(display.visible);

    if (layoutChanged) {
        updateLayout();
        update();
    } else if (geometryChanged) {
        update();
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
    qDebug().noquote() << "[MangaTextItem] updateLayout: region=" << QString::fromStdString(m_display.id.toString())
                       << "fontFamily=" << QString::fromStdString(m_display.layout.fontFamily)
                       << "fontWeight=" << m_display.layout.fontWeight << "fontSizePt=" << m_display.layout.fontSizePt
                       << "orient=" << static_cast<int>(m_display.layout.orientation)
                       << "text=" << m_display.text.left(30);
    m_cachedLayout =
        rendering::TextLayoutEngine::layoutText(m_display.text, localBounds, m_display.layout, m_display.logicalDpi);
    m_hasCachedLayout = true;
}

qreal MangaTextItem::strokeMargin() const noexcept {
    return std::max<qreal>(2.0, static_cast<qreal>(m_display.layout.strokeWidthPx * 0.5));
}

} // namespace manga::ui::manga
