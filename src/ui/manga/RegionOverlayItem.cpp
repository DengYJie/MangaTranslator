#include "ui/manga/RegionOverlayItem.h"

#include "ui/manga/CanvasZOrder.h"

#include <QBrush>
#include <QPainter>
#include <QPen>

namespace manga::ui::manga {

static constexpr canvas::HandlePosition kHandleOrder[] = {canvas::HandlePosition::NorthWest,
                                                          canvas::HandlePosition::North,
                                                          canvas::HandlePosition::NorthEast,
                                                          canvas::HandlePosition::East,
                                                          canvas::HandlePosition::SouthEast,
                                                          canvas::HandlePosition::South,
                                                          canvas::HandlePosition::SouthWest,
                                                          canvas::HandlePosition::West,
                                                          canvas::HandlePosition::Rotate};

RegionOverlayItem::RegionOverlayItem(::manga::domain::RegionId id, QGraphicsItem* parent)
    : QGraphicsRectItem(parent), m_id(id) {
    setZValue(canvas::kZRegionNormal);
    setPen(Qt::NoPen);
    setBrush(Qt::NoBrush);

    for (size_t i = 0; i < std::size(kHandleOrder); ++i) {
        m_handles[i] = new SelectionHandleItem(kHandleOrder[i], this);
        m_handles[i]->setVisible(false);
    }
}

void RegionOverlayItem::updateFromDisplay(const canvas::RegionDisplay& display, bool showHandles) {
    setRect(display.geometry.x, display.geometry.y, display.geometry.width, display.geometry.height);
    m_bubbleGeometry = display.bubbleGeometry;
    m_bubblePolygon = display.bubblePolygon;
    setSelected(display.selected, showHandles);
    layoutHandles();
}

void RegionOverlayItem::setTransientGeometry(const ::manga::domain::RectGeometry& geometry) {
    setRect(geometry.x, geometry.y, geometry.width, geometry.height);
    layoutHandles();
}

::manga::domain::RectGeometry RegionOverlayItem::currentGeometry() const noexcept {
    const QRectF r = rect();
    return {.x = r.x(), .y = r.y(), .width = r.width(), .height = r.height()};
}

QRectF RegionOverlayItem::boundingRect() const {
    QRectF br = QGraphicsRectItem::boundingRect().adjusted(-8.0, -24.0, 8.0, 8.0);
    if (m_bubbleGeometry.has_value()) {
        const auto& bg = *m_bubbleGeometry;
        br = br.united(QRectF(bg.x, bg.y, bg.width, bg.height).adjusted(-24.0, -24.0, 24.0, 24.0));
    }
    if (m_bubblePolygon.has_value() && m_bubblePolygon->isValid()) {
        for (const auto& pt : m_bubblePolygon->points) {
            br = br.united(QRectF(pt.x - 24.0, pt.y - 24.0, 48.0, 48.0));
        }
    }
    return br;
}

QPainterPath RegionOverlayItem::shape() const {
    QPainterPath path;
    path.addRect(rect());
    return path;
}

bool RegionOverlayItem::contains(const QPointF& point) const {
    return rect().contains(point);
}

void RegionOverlayItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) {
    // 1. Unselected & normal: completely invisible
    if (!m_selected && !m_hovered) {
        return;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    // 2. Automatic / bubble region: dual-layer composite dashed outline
    if (m_bubblePolygon.has_value() && m_bubblePolygon->isValid()) {
        QPainterPath bubblePath;
        const auto& pts = m_bubblePolygon->points;
        bubblePath.moveTo(pts[0].x, pts[0].y);
        for (size_t i = 1; i < pts.size(); ++i) {
            bubblePath.lineTo(pts[i].x, pts[i].y);
        }
        bubblePath.closeSubpath();

        // Layer 1: Dark high-contrast underlay dashed stroke (10px dash, 6px gap)
        QPen underlayPen(QColor(0, 0, 0, 150), 3.2, Qt::CustomDashLine);
        underlayPen.setCosmetic(true);
        underlayPen.setDashPattern({10.0, 6.0});
        underlayPen.setCapStyle(Qt::RoundCap);
        underlayPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(underlayPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(bubblePath);

        // Layer 2: Theme accent top layer dashed stroke (10px dash, 6px gap)
        const QColor strokeColor =
            m_selected ? m_selectedStroke
                       : QColor(m_selectedStroke.red(), m_selectedStroke.green(), m_selectedStroke.blue(), 180);
        QPen bubblePen(strokeColor, 1.4, Qt::CustomDashLine);
        bubblePen.setCosmetic(true);
        bubblePen.setDashPattern({10.0, 6.0});
        bubblePen.setCapStyle(Qt::RoundCap);
        bubblePen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(bubblePen);
        painter->drawPath(bubblePath);
    } else if (m_bubbleGeometry.has_value()) {
        const auto& bg = *m_bubbleGeometry;
        const QRectF bRect(bg.x, bg.y, bg.width, bg.height);

        // Layer 1: Dark high-contrast underlay dashed stroke (10px dash, 6px gap)
        QPen underlayPen(QColor(0, 0, 0, 150), 3.2, Qt::CustomDashLine);
        underlayPen.setCosmetic(true);
        underlayPen.setDashPattern({10.0, 6.0});
        underlayPen.setCapStyle(Qt::RoundCap);
        underlayPen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(underlayPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(bRect, 6.0, 6.0);

        // Layer 2: Theme accent top layer dashed stroke (10px dash, 6px gap)
        const QColor strokeColor =
            m_selected ? m_selectedStroke
                       : QColor(m_selectedStroke.red(), m_selectedStroke.green(), m_selectedStroke.blue(), 180);
        QPen bubblePen(strokeColor, 1.4, Qt::CustomDashLine);
        bubblePen.setCosmetic(true);
        bubblePen.setDashPattern({10.0, 6.0});
        bubblePen.setCapStyle(Qt::RoundCap);
        bubblePen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(bubblePen);
        painter->drawRoundedRect(bRect, 6.0, 6.0);
    }

    // 2 & 3. Hovered vs Selected state background
    if (m_selected) {
        // Selected: light transparent tint
        QColor fill = m_selectedStroke;
        fill.setAlpha(18);
        painter->setBrush(QBrush(fill));
    } else {
        // Hovered: completely transparent background
        painter->setBrush(Qt::NoBrush);
    }

    // 1px pure theme accent cosmetic border
    QPen outline(m_selectedStroke, 1.0);
    outline.setCosmetic(true);
    outline.setJoinStyle(Qt::MiterJoin);
    painter->setPen(outline);
    painter->drawRect(rect());

    painter->restore();
}

void RegionOverlayItem::setHovered(bool hovered) {
    if (m_hovered == hovered) {
        return;
    }
    m_hovered = hovered;
    update();
}

void RegionOverlayItem::layoutHandles() {
    const QRectF r = rect();
    const double left = r.left();
    const double top = r.top();
    const double right = r.right();
    const double bottom = r.bottom();
    const double cx = r.center().x();
    const double cy = r.center().y();

    const QPointF positions[] = {
        {left, top},     // NW
        {cx, top},       // N
        {right, top},    // NE
        {right, cy},     // E
        {right, bottom}, // SE
        {cx, bottom},    // S
        {left, bottom},  // SW
        {left, cy},      // W
        {cx, top - 16.0} // Rotate
    };

    for (size_t i = 0; i < std::size(kHandleOrder); ++i) {
        m_handles[i]->setPos(positions[i]);
    }
}

void RegionOverlayItem::setOrder(int order) {
    m_order = order;
    QString name = QStringLiteral("Region %1").arg(order);
    if (m_selected) {
        name += QStringLiteral(", selected");
    }
    setData(Qt::AccessibleDescriptionRole, name);
}

void RegionOverlayItem::setSelected(bool selected, bool showHandles) {
    m_selected = selected;
    for (auto* handle : m_handles) {
        handle->setVisible(selected && showHandles);
    }
    setZValue(selected ? canvas::kZRegionNormal + 5.0 : canvas::kZRegionNormal);
    update();

    if (m_order > 0) {
        QString name = QStringLiteral("Region %1").arg(m_order);
        if (selected) {
            name += QStringLiteral(", selected");
        }
        setData(Qt::AccessibleDescriptionRole, name);
    }
}

void RegionOverlayItem::setColors(const QColor& selectedStroke,
                                  const QColor& unselectedStroke,
                                  const QColor& handleFill) {
    m_selectedStroke = selectedStroke;
    m_unselectedStroke = unselectedStroke;
    m_handleFill = handleFill;
    setSelected(m_selected);
    for (auto* handle : m_handles) {
        handle->setFillColor(selectedStroke);
    }
}

} // namespace manga::ui::manga
