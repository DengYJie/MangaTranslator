#pragma once

#include "ui/manga/RegionInteraction.h"
#include "ui/manga/SelectionHandleItem.h"

#include <QGraphicsRectItem>

class QPainter;
class QStyleOptionGraphicsItem;

namespace manga::ui::manga {

/// Visual overlay for a single text region on the manga canvas. Draws a cosmetic
/// rectangle outline at kZRegionNormal and manages eight SelectionHandleItem children
/// that are shown only when the region is selected. Holds a RegionId and transient
/// geometry; never queries Domain or ViewModel. The default rect shape covers the
/// full region extent and serves as the hit area without any override.
class RegionOverlayItem final : public QGraphicsRectItem {
public:
    explicit RegionOverlayItem(::manga::domain::RegionId id, QGraphicsItem* parent = nullptr);

    [[nodiscard]] ::manga::domain::RegionId regionId() const noexcept {
        return m_id;
    }

    [[nodiscard]] bool isSelected() const noexcept {
        return m_selected;
    }

    /// Updates position, size, and handle visibility from a display projection.
    void updateFromDisplay(const canvas::RegionDisplay& display);

    /// Sets the geometry directly during an in-progress drag gesture.
    void setTransientGeometry(const ::manga::domain::RectGeometry& geometry);

    /// Returns the current scene-space geometry.
    [[nodiscard]] ::manga::domain::RectGeometry currentGeometry() const noexcept;

    /// Positions all eight handles at their anchor points for the current rect.
    void layoutHandles();

    /// Sets the 1-based display order used for the accessible name.
    void setOrder(int order);

    /// Updates overlay and handle colors from resolved FluentQt semantic tokens.
    void setColors(const QColor& selectedStroke, const QColor& unselectedStroke, const QColor& handleFill);

    [[nodiscard]] QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

private:
    void setSelected(bool selected);

    ::manga::domain::RegionId m_id;
    bool m_selected{false};
    int m_order{0};
    QColor m_selectedStroke{0, 120, 215};
    QColor m_unselectedStroke{180, 180, 180, 160};
    QColor m_handleFill{0, 120, 215};
    std::array<SelectionHandleItem*, 8> m_handles{};
};

} // namespace manga::ui::manga
