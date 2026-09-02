#pragma once

#include "ui/manga/RegionInteraction.h"

#include <QGraphicsRectItem>

class QPainter;
class QStyleOptionGraphicsItem;

namespace manga::ui::manga {

/// Eight-directional resize handle for a region overlay. Renders as a cosmetic
/// square that ignores view transforms so its visual size stays constant across
/// zoom levels. Hit area covers at least 16 device pixels for touch and a11y.
class SelectionHandleItem final : public QGraphicsRectItem {
public:
    explicit SelectionHandleItem(canvas::HandlePosition position, QGraphicsItem* parent = nullptr);

    [[nodiscard]] canvas::HandlePosition handlePosition() const noexcept {
        return m_position;
    }

    [[nodiscard]] bool isEdgeHandle() const noexcept {
        return m_position == canvas::HandlePosition::North || m_position == canvas::HandlePosition::South ||
               m_position == canvas::HandlePosition::East || m_position == canvas::HandlePosition::West;
    }

    [[nodiscard]] bool isRotateHandle() const noexcept {
        return m_position == canvas::HandlePosition::Rotate;
    }

    void setEdgesOnly(bool edgesOnly);

    [[nodiscard]] QRectF boundingRect() const override;
    [[nodiscard]] QPainterPath shape() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget = nullptr) override;

    /// Updates the handle stroke/accent color from a resolved FluentQt semantic token.
    void setFillColor(const QColor& color);

private:
    canvas::HandlePosition m_position;
    bool m_edgesOnly{true};
    QColor m_accentColor{0, 120, 215};
};

} // namespace manga::ui::manga
