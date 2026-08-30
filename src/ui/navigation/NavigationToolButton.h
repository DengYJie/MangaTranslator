#pragma once

#include "ui/navigation/NavigationPushButton.h"

#include <QPoint>
#include <QString>

class QVariantAnimation;
class QMoveEvent;
class QShowEvent;

namespace manga::ui::navigation {
/**
 * @brief Icon-only navigation tool button.
 *
 * Dedicated to action buttons such as the top hamburger and back buttons; fixed size
 * (48x48) with the icon always geometrically centered. Supports a smooth move
 * animation when the navigation bar layout changes (setAnimatedMove).
 */
class NavigationToolButton : public NavigationPushButton {
    Q_OBJECT

public:
    explicit NavigationToolButton(const QString& iconGlyph, QWidget* parent = nullptr);

    ~NavigationToolButton() override = default;

    bool isSelectable() const {
        return false;
    }

    void setCompacted(bool compacted) override;

    /// Keeps the fixed 48x48 size across orientation switches (the base class
    /// setFixedHeight by row height would break the locked size).
    void setOrientation(Qt::Orientation orientation) override;

    /// When enabled, displacement of this widget caused by layout reflow animates
    /// smoothly as an implicit animation (matches the WinUI overflow button 200ms spec).
    void setAnimatedMove(bool enabled);

    bool isAnimatedMove() const {
        return m_animatedMove;
    }

protected:
    int iconDrawX() const override;

    void showEvent(QShowEvent* event) override;

    void moveEvent(QMoveEvent* event) override;

private:
    /// Runs a single animation pass once the layout has settled, avoiding one
    /// animation per move event from a single reflow.
    void runPendingMove();

private:
    bool m_animatedMove = false;
    /// Whether the widget was just restored from the hidden state.
    bool m_justShown = false;
    /// Whether the current moveEvent was triggered by the animation's manual move
    /// (set and cleared synchronously, no residue).
    bool m_animating = false;
    /// Whether a deferred animation has been scheduled for the layout reflow.
    bool m_moveScheduled = false;
    /// Stable position before the layout reflow, used as the animation start point.
    QPoint m_pendingFrom;
    /// Final layout target position (the last of multiple move events wins).
    QPoint m_pendingTarget;
    QVariantAnimation* m_moveAnimation = nullptr;
};
} // namespace manga::ui::navigation
