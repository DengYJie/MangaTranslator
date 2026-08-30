#pragma once

#include "NavigationMetrics.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <QRectF>
#include <QWidget>

class QVariantAnimation;

namespace manga::ui::navigation {
/**
 * @brief Navigation selection indicator capsule (used only for the switch animation).
 *
 * An independent floating widget that flies from the old position to the new one at
 * the moment the selected item changes, then hides when the animation ends; the
 * persistent indicator is painted by NavigationTreeItem itself.
 */
class NavigationIndicator : public QWidget, public fluent::FluentElement {
    Q_OBJECT

public:
    explicit NavigationIndicator(QWidget* parent = nullptr);

    ~NavigationIndicator() override = default;

    Qt::Orientation orientation() const {
        return m_orientation;
    }

    void setOrientation(Qt::Orientation orientation);

    void setInitialPosition(const QRectF& rect);

    /**
     * @brief Flies from the current position to the target geometry and hides
     * automatically when the animation ends.
     * @param targetRect Target rectangle in the host coordinate system.
     */
    void activateAt(const QRectF& targetRect, bool animated = true);

    /// Hides immediately (stops the animation).
    void hideIndicator();

    bool isFlying() const;

    /**
     * @brief Portal Return: after the flyout closes, the horizontal indicator
     * expands back from left to right.
     * @param targetRect Target rectangle in the host coordinate system (final
     * restored position).
     */
    void playPortalReturn(const QRectF& targetRect);

    /**
     * @brief Cross Window Portal: synchronized composite motion across windows
     * (Horizontal bar to Flyout overlay); the shape is dispatched by host orientation.
     * @param startRect Start rectangle in the host coordinate system (where the
     * horizontal bar begins shrinking).
     * @param targetRect Target rectangle in the host coordinate system (where the
     * flyout vertical bar finishes sliding in).
     *
     * Horizontal host (top bar): the horizontal bar shrinks to the left and
     * disappears (left edge fixed, right edge moves left, width decays to 0);
     * Vertical host (flyout): the vertical bar spawns at the top and grows downward
     * (top edge fixed, height grows from 0).
     */
    void playCrossWindowPortal(const QRectF& startRect, const QRectF& targetRect);

    QRectF currentRect() const {
        return m_currentRect;
    }

Q_SIGNALS:
    /// Emitted when the flight animation starts and the floating indicator takes
    /// over presentation: the host clears persistent indicator ownership on receipt.

    void flightStarted();

    /// Emitted when the flight animation ends and the indicator has arrived at
    /// the target position: the host lets the corresponding item paint the
    /// persistent indicator on receipt.
    void flightFinished();

public:
    void onThemeUpdated() override;

protected:
    void paintEvent(QPaintEvent* event) override;

    void showEvent(QShowEvent* event) override;

private:
    /// Short-circuits when target and current geometry nearly coincide, avoiding
    /// pointless animation.
    bool isSamePosition(const QRectF& targetRect) const;

    /// Ensures the Z-order stays above the scroll area.
    void raiseToTop();

    /// Configures the shared flight animation properties (easing + 0->1 +
    /// duration); each entry point passes its own easing. When durationMs is
    /// omitted the theme standard duration is used.
    void beginFlight(const QEasingCurve& easing, int durationMs = -1);

    /// Resolves once along the parent chain on first use or after invalidation,
    /// then caches the result.
    fluent::FluentElement::Theme cachedEffectiveTheme() const;

    /// Avoids the per-call parent-chain traversal of effectiveTheme() in
    /// themeColorsRef().
    const fluent::FluentElement::Colors& colorsRef() const;

private:
    QRectF m_startRect;
    QRectF m_targetRect;
    QRectF m_currentRect;

    mutable fluent::FluentElement::Theme m_effectiveTheme = fluent::FluentElement::Theme::Light;
    mutable bool m_themeValid = false;

    QVariantAnimation* m_flightAnimation = nullptr;
    Qt::Orientation m_orientation = Qt::Vertical;

    /// Current animation mode: Normal standard flight, PortalReturn expand-back,
    /// CrossWindowPortal fully mapped interpolated leap.
    enum class AnimationMode {
        Normal,
        PortalReturn,
        CrossWindowPortal
    };

    AnimationMode m_animMode = AnimationMode::Normal;
    /// This flight is a same-axis translation (same x or same y), using the WinUI
    /// segmented easing instead of a single decelerate curve.
    bool m_sameAxisFlight = false;
};
} // namespace manga::ui::navigation
