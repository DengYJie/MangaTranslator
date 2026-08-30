#pragma once

#include "components/foundation/overlay/OverlayGeometry.h"
#include "components/foundation/overlay/OverlayShadow.h"
#include "design/Elevation.h"
#include "ui/navigation/NavigationFocusHost.h"
#include "ui/navigation/NavigationIndicator.h"
#include "ui/navigation/NavigationMetrics.h"
#include "ui/navigation/NavigationTreeItem.h"
#include "ui/navigation/NavigationTreeWidgetBase.h"
#include "ui/navigation/NavigationWidget.h"

#include <FluentQt/Foundation.h>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QVector>

class QBoxLayout;
class QPropertyAnimation;
class QParallelAnimationGroup;
class QHideEvent;
class QPaintEvent;

namespace manga::ui::navigation {
class NavigationTreeWidget;

/**
 * @brief Flyout dedicated to the navigation menu.
 *
 * Hosts the navigation flyout in an independent Qt::Tool top-level window so it
 * can render beyond the main window bounds. As a NavigationTreeWidgetBase
 * derivative it renders the activated category's children as a cloned subtree
 * (collapsible, with selection state), plus a Medium shadow, slide-in animation,
 * screen-edge clamping with vertical flip, anchor following, and light dismiss.
 */
class NavigationFlyout : public NavigationTreeWidgetBase, public INavigationFocusHost {
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen WRITE setIsOpen)

    Q_PROPERTY(Placement placement READ placement WRITE setPlacement)

    Q_PROPERTY(int anchorOffset READ anchorOffset WRITE setAnchorOffset)

public:
    enum class Placement {
        Right,       // Compact sidebar mode: opens to the right.
        Bottom,      // Top bar mode: opens downward (left-aligned or centered by default).
        BottomRight, // Top bar mode: opens downward, right-aligned (commonly used for the Overflow menu).
        Auto,        // Automatically detects screen edges and flips.
    };

    Q_ENUM(Placement)

    enum CloseFlag {
        NoAutoClose = 0,
        CloseOnPressOutside = 1 << 0,
        CloseOnEscape = 1 << 1,
    };

    Q_DECLARE_FLAGS(ClosePolicy, CloseFlag)

    /**
     * @brief Constructs an independent flyout window.
     * @param rootTree The original panel's navigation tree (the single
     * authoritative state source; page switching and selection state are driven
     * by it uniformly).
     * @param host Host (NavigationPanel), used for theme inheritance, anchor
     * following, and eventFilter attachment.
     */
    explicit NavigationFlyout(NavigationTreeWidget* rootTree, QWidget* host);

    ~NavigationFlyout() override;

    // Anchor and positioning.
    QWidget* anchor() const {
        return m_anchorWidget;
    }
    void setAnchor(QWidget* anchor) {
        m_anchorWidget = anchor;
    }

    Placement placement() const {
        return m_placement;
    }
    void setPlacement(Placement p) {
        m_placement = p;
    }

    int anchorOffset() const {
        return m_anchorOffset;
    }
    void setAnchorOffset(int px) {
        m_anchorOffset = px;
    }

    // State and policy.
    bool isOpen() const {
        return m_isOpen;
    }

    void setIsOpen(bool open);

    ClosePolicy closePolicy() const {
        return m_closePolicy;
    }
    void setClosePolicy(ClosePolicy policy) {
        m_closePolicy = policy;
    }

    bool isExitAnimationEnabled() const {
        return m_exitAnimationEnabled;
    }
    void setExitAnimationEnabled(bool enabled) {
        m_exitAnimationEnabled = enabled;
    }

    /**
     * @brief Sets whether clicking outside consumes the event (default true).
     */
    bool lightDismissConsumesPress() const {
        return m_lightDismissConsumesPress;
    }
    void setLightDismissConsumesPress(bool consume) {
        m_lightDismissConsumesPress = consume;
    }

    /**
     * @brief Passthrough allowlist.
     * Clicking these widgets closes the popup and lets the event pass through
     * directly without being consumed.
     */
    void addLightDismissPassthrough(QWidget* widget) {
        m_lightDismissPassthrough.append(widget);
    }
    void removeLightDismissPassthrough(QWidget* widget) {
        m_lightDismissPassthrough.removeOne(widget);
    }
    void clearLightDismissPassthrough() {
        m_lightDismissPassthrough.clear();
    }

    // Business data loading.
    /**
     * @brief Rebuilds the subtree of the given category by cloning and fills the
     * flyout with it.
     * @param categoryKey Route key of the activated category (its children,
     * recursively, are cloned).
     */
    void rebuildSubtree(const QString& categoryKey);

    /**
     * @brief Rebuilds by cloning the given overflow entry set (for the overflow
     * flyout).
     * @param entries Overflow entry list (nodes are cloned with their recursive
     * children; section headers render in order).
     */
    void rebuildSubtreeFromEntries(const QVector<NavigationOverflowEntry>& entries);

    NavigationIndicator* indicator() const {
        return m_flyoutIndicator;
    }
    QWidget* host() const {
        return m_host;
    }

    /**
     * @brief Triggers the selected item's cross-window leap-in (Cross Window
     * Portal In) animation inside the overlay.
     */
    void playSelectedItemCrossPortal(NavigationTreeItem* selectedItem,
                                     const QRectF& mappedStartRect,
                                     const QRectF& targetRect);

    /// Returns the internal clone stand-in.
    NavigationTreeItem* cloneItemFor(const QString& routeKey) const {
        return m_itemIndex.value(routeKey, nullptr);
    }

    NavigationTreeItem* getVisualProxyFor(NavigationTreeItem* item) const;

    QRectF indicatorRectInHost(NavigationTreeItem* item) const;

    void playInternalFlight(const QRectF& targetRect);

public Q_SLOTS:
    /// Opens at the given anchor (position computed from placement + bounds
    /// check + entry animation).

    void showAt(QWidget* anchor);

    /// Manual open with an externally computed offset.
    void openAt(const QPoint& globalCardTopLeft, const QPoint& slideInOffset = QPoint(0, 0));

    /// Used only for state binding.
    void open();

    /// Closes the overlay (120ms exit animation or instant, per
    /// exitAnimationEnabled).
    void close();

    void onIndicatorOwnerChanged(NavigationTreeItem* item, bool isOwner);

    void moveFocusBy(int delta) override;

Q_SIGNALS:
    /// About to be shown (before the entry animation starts).

    void aboutToShow();

    /// Fully opened (entry animation finished).
    void opened();

    /// About to close (the instant the exit animation starts, so the top bar
    /// indicator returns concurrently).
    void aboutToHide();

    /// Fully closed (exit animation finished, ready for destruction).
    void closed();

public:
    void onThemeUpdated() override {
        update();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

    bool event(QEvent* event) override;

    void showEvent(QShowEvent* event) override;

    void hideEvent(QHideEvent* event) override;

    void paintEvent(QPaintEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;

private:
    QVector<NavigationWidget*> visibleItems() const;

    void collectVisible(NavigationTreeWidget* node, QVector<NavigationWidget*>& out) const;

    void cloneNode(NavigationTreeWidget* srcNode, NavigationTreeWidget* parentClone, int depth);

    void finalizeSize();

    QWidget* m_host = nullptr;
    QBoxLayout* m_contentLayout = nullptr;

    QPoint m_globalCardPos;
    QPoint m_slideInOffset;
    QPoint m_hostAnchorOffset;
    bool m_flippedUp = false;

    QPointer<QWidget> m_anchorWidget;
    Placement m_placement = Placement::Bottom;
    int m_anchorOffset = 0;

    bool m_isOpen = false;
    ClosePolicy m_closePolicy = ClosePolicy(CloseOnPressOutside | CloseOnEscape);
    bool m_exitAnimationEnabled = true;

    bool m_lightDismissConsumesPress = true;
    QVector<QPointer<QWidget>> m_lightDismissPassthrough;

    QParallelAnimationGroup* m_animGroup = nullptr;
    QPropertyAnimation* m_slideAnim = nullptr;
    QPropertyAnimation* m_fadeAnim = nullptr;

    NavigationIndicator* m_flyoutIndicator = nullptr;
    QHash<QString, NavigationTreeItem*> m_itemIndex;
    bool m_isClosing = false;
};

Q_DECLARE_OPERATORS_FOR_FLAGS(NavigationFlyout::ClosePolicy)
} // namespace manga::ui::navigation
