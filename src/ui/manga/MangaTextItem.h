#pragma once

#include "rendering/TextLayoutEngine.h"
#include "ui/manga/TextLayerDisplay.h"

#include <QGraphicsItem>
#include <QRectF>

class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

namespace manga::ui::manga {

class MangaTextItem final : public QGraphicsItem {
public:
    explicit MangaTextItem(domain::RegionId id, QGraphicsItem* parent = nullptr);

    void setDisplay(const TextLayerDisplay& display);
    void setLogicalDpi(double logicalDpi);

    [[nodiscard]] const TextLayerDisplay& display() const noexcept {
        return m_display;
    }

    [[nodiscard]] QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    void updateLayout();
    [[nodiscard]] qreal strokeMargin() const noexcept;

    TextLayerDisplay m_display;
    rendering::LayoutResult m_cachedLayout;
    bool m_hasCachedLayout{false};
};

} // namespace manga::ui::manga
