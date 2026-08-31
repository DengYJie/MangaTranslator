#pragma once

#include "ui/manga/RegionInteraction.h"

#include <QGraphicsRectItem>

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

private:
    canvas::HandlePosition m_position;
};

} // namespace manga::ui::manga
