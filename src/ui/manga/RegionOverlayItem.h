#pragma once

#include "ui/manga/RegionInteraction.h"
#include "ui/manga/SelectionHandleItem.h"

#include <QGraphicsRectItem>

namespace manga::ui::manga {

/// Visual overlay for a single text region on the manga canvas. Draws a cosmetic
/// rectangle outline at kZRegionNormal and manages eight SelectionHandleItem children
/// that are shown only when the region is selected. Holds a RegionId and transient
/// geometry; never queries Domain or ViewModel.
class RegionOverlayItem final : public QGraphicsRectItem {
public:
    explicit RegionOverlayItem(::manga::domain::RegionId id, QGraphicsItem* parent = nullptr);

    [[nodiscard]] ::manga::domain::RegionId regionId() const noexcept {
        return m_id;
    }

    /// Updates position, size, and handle visibility from a display projection.
    void updateFromDisplay(const canvas::RegionDisplay& display);

    /// Sets the geometry directly during an in-progress drag gesture.
    void setTransientGeometry(const ::manga::domain::RectGeometry& geometry);

    /// Returns the current scene-space geometry.
    [[nodiscard]] ::manga::domain::RectGeometry currentGeometry() const noexcept;

    /// Positions all eight handles at their anchor points for the current rect.
    void layoutHandles();

private:
    void setSelected(bool selected);

    ::manga::domain::RegionId m_id;
    std::array<SelectionHandleItem*, 8> m_handles{};
};

} // namespace manga::ui::manga
