#pragma once

#include "ui/navigation/NavigationFocusHost.h"
#include "ui/navigation/NavigationMetrics.h"
#include "ui/navigation/NavigationTreeWidgetBase.h"

#include <FluentQt/Foundation.h>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QVector>
#include <QWidget>

#include <memory>

class QBoxLayout;
class QPaintEvent;
class QResizeEvent;

namespace fluent::scrolling {
class ScrollView;
}

namespace manga::ui::animation {
class AnimatedVisualSource;
}

namespace manga::ui::navigation {
class NavigationWidget;
class NavigationTreeItem;
class NavigationToolButton;
class NavigationIndicator;
class NavigationSectionHeader;

/**
 * @brief Navigation tree root container.
 *
 * Derives from NavigationTreeWidgetBase (node container) and additionally carries
 * the root resources: scroll view, indicator, overflow controller, and the global
 * route index. Every node (including the root itself) is an instance of this
 * class; nodes reach back to the root through m_root for global operations
 * (selection / indicator / flyout).
 */
class NavigationTreeWidget : public NavigationTreeWidgetBase, public INavigationFocusHost {
    Q_OBJECT
    Q_PROPERTY(bool selectionFollowsFocus READ selectionFollowsFocus WRITE setSelectionFollowsFocus)

    // The flyout rebuilds subtrees by cloning nodes and needs access to the
    // nodes' protected structural members.
    friend class NavigationFlyout;
    friend class NavigationPanel;
    friend class NavigationTreeWidgetBase;

public:
    explicit NavigationTreeWidget(QWidget* parent = nullptr);

    ~NavigationTreeWidget() override = default;

private:
    /// Node-mode constructor: attaches to rootNode without initializing root resources.
    explicit NavigationTreeWidget(NavigationTreeWidget* rootNode);

public:
    /**
     * @brief Registers a node.
     * @param routeKey Unique route key.
     * @param iconGlyph Fluent icon glyph.
     * @param text Display text.
     * @param parentKey Parent category key (empty for a top-level node).
     * @param position Region the item belongs to (Top main list / Bottom footer),
     * defaults to Top.
     * @param selectable Whether the item is selectable, defaults to true.
     * @param tooltip Tooltip text (defaults to text when empty).
     * @param visualSource Animated vector visual source (optional).
     */
    void addItem(const QString& routeKey,
                 const QString& iconGlyph,
                 const QString& text,
                 const QString& parentKey = QString(),
                 NavigationItemPosition position = NavigationItemPosition::Top,
                 bool selectable = true,
                 const QString& tooltip = QString(),
                 std::shared_ptr<animation::AnimatedVisualSource> visualSource = nullptr);

    /**
     * @brief Adds a section header.
     * @param text Section header text.
     */
    void addSectionHeader(const QString& text);

    /**
     * @brief Adds a custom navigation widget.
     *
     * The unified entry point for item placement: handles orientation, expansion
     * progress synchronization, and layout insertion, and registers section
     * headers. Custom widgets may connect their own clicked signal before being
     * passed in; addItem and addSectionHeader both funnel into this.
     * @param widget A constructed navigation widget (NavigationWidget subclass,
     * may be a NavigationSectionHeader).
     * @param position Region the item belongs to (Top main list / Bottom footer).
     */
    void addWidget(NavigationWidget* widget, NavigationItemPosition position = NavigationItemPosition::Top);

    /**
     * @brief Removes the given node (including all its descendants) from the tree.
     * @param routeKey Target route key.
     * @return true when the node existed and was removed.
     */
    bool removeItem(const QString& routeKey);

    NavigationTreeWidget* nodeFor(const QString& routeKey);

    const NavigationTreeWidget* nodeFor(const QString& routeKey) const;

    /**
     * @brief Activates the given route node and positions the indicator.
     * @param routeKey Target route key.
     * @param updateOverflow Whether to recompute the overflow layout (Top mode)
     * immediately; when false only the selection state updates and the caller
     * triggers the recompute later (for example when the flyout closes).
     */
    void setCurrentItem(const QString& routeKey, bool updateOverflow = true);

    bool contains(const QString& routeKey) const;

    /**
     * @brief Determines whether a node is a root node (no parent category).
     * @param routeKey Target route key.
     */
    bool isRoot(const QString& routeKey) const;

    /**
     * @brief Determines whether a node is a leaf node (no children).
     * @param routeKey Target route key.
     */
    bool isLeaf(const QString& routeKey) const;

    QString currentRouteKey() const {
        return m_currentRouteKey;
    }

private:
    NavigationTreeItem* getVisualProxyFor(NavigationTreeItem* logicalOwner) const;

public:
    NavigationTreeItem* findFirstSelectableItem() const;

    /**
     * @brief Sets the compact icon bar mode.
     *
     * In compact mode, clicking a category no longer expands its children inline;
     * the host (NavigationPanel) opens a flyout to show the children instead.
     */
    void setCompacted(bool compacted);

    bool isCompacted() const {
        return m_isCompacted;
    }

    /**
     * @brief Sets the panel layout orientation (Horizontal / Vertical).
     */
    void setOrientation(Qt::Orientation orientation);

    Qt::Orientation orientation() const {
        return m_orientation;
    }

private:
    /// Marks the node as a clone inside a flyout: category clicks expand inline
    /// instead of opening another flyout.
    void setInlineExpansion(bool inlineMode) {
        m_inlineExpansion = inlineMode;
    }
    bool inlineExpansion() const {
        return m_inlineExpansion;
    }

private Q_SLOTS:

    void onIndicatorOwnerChanged(NavigationTreeItem* item, bool isOwner);

private:
    /// Computes the indicator target rectangle of the given widget in its host's
    /// coordinate system.
    QRectF indicatorRectInHost(NavigationWidget* item, QWidget* host = nullptr) const;

public:
    /**
     * @brief Determines whether a node is a descendant of the target node.
     */
    bool isAncestorOf(const QString& routeKey, const QString& ancestorKey) const;

    /**
     * @brief Programmatically expands/collapses the given category (ignored in
     * compact mode, where the flyout handles it).
     * @param routeKey Category node route key.
     * @param expanded Whether to expand.
     * @param animated Whether to enable the collapse height transition animation.
     */
    void setCategoryExpanded(const QString& routeKey, bool expanded, bool animated = true);

    /**
     * @brief Sets whether a category node remembers its expansion state.
     * @param routeKey Category node route key.
     * @param remember Whether to remember.
     */
    void setRememberExpandState(const QString& routeKey, bool remember);

private:
    /**
     * @brief Saves a category node's current expansion state (for later restore).
     */
    void saveExpandState(const QString& routeKey);

    /**
     * @brief Restores a category node's saved expansion state.
     */
    void restoreExpandState(const QString& routeKey, bool animated = true);

    void collapseCategoryChevron(const QString& categoryKey);

    /**
     * @brief Notifies the tree that a category's flyout was closed for external
     * reasons (click outside / Esc / child selected). The tree clears the active
     * state and retracts the chevron accordingly, keeping a single source of truth.
     */
    void dismissCategory();

    /**
     * @brief Recomputes overflow (Top mode).
     *
     * Clicking a selectable category item inside a flyout defers the overflow
     * recompute; this method performs the deferred overflow reflow and indicator
     * update after the flyout closes.
     */
    void updateOverflow();

    /**
     * @brief Activates a category (sets the active state + emits
     * categoryActivated), called by the node click decision. Ignored when the
     * category is already active (idempotent).
     */
    void activateCategory(const QString& categoryKey, QWidget* anchorWidget);

    /// Whether the given category is in the flyout-active state.
    bool isCategoryActive(const QString& categoryKey) const {
        return m_activeCategoryKey == categoryKey;
    }

public:
    void moveFocusBy(int delta) override;

    bool selectionFollowsFocus() const {
        return m_selectionFollowsFocus;
    }

    void setSelectionFollowsFocus(bool follows);

    /// Whether there are overflow items (when the bar content exceeds the
    /// available width in Top mode).
    bool hasOverflowItems() const {
        return !m_overflowNodes.isEmpty() || !m_overflowHeaders.isEmpty();
    }

Q_SIGNALS:
    /// The single driver of page switching: emitted after every interaction path
    /// funnels into setCurrentItem, avoiding multi-headed triggering.

    void itemSelected(const QString& routeKey);

    /// Emitted when a category item is clicked in compact mode (carries the
    /// category's button widget for flyout anchoring and debounce).
    void categoryActivated(const QString& categoryKey, QWidget* anchorWidget);

    /// Emitted when a category flyout closes (deactivates); symmetric with
    /// categoryActivated.
    void categoryDeactivated(const QString& categoryKey);

    /// Emitted when the overflow menu is requested (user clicked the overflow
    /// button), carrying the anchor widget and a snapshot of overflow entries for
    /// the panel to render.
    void overflowMenuRequested(QWidget* anchorWidget, const QVector<NavigationOverflowEntry>& entries);

public:
    void onThemeUpdated() override;

    /**
     * @brief Ensures the built-in ScrollView and its viewport keep a transparent
     * background so they do not cover the panel material.
     */
    void applyTransparentScrollView();

protected:
    void paintEvent(QPaintEvent* event) override;

    void showEvent(QShowEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;

private:
    /// Uniformly adjusts indicator ownership after an expansion change (collapse
    /// falls back to the parent, expansion returns it to the child).
    void onExpansionChanged(const QString& routeKey, bool expanded);

    QVector<NavigationWidget*> visibleItems() const;

    void collectVisible(NavigationTreeWidget* node, QVector<NavigationWidget*>& out) const;

    /// Collapse end state: captures a snapshot of all category expansion states
    /// and collapses the child containers.
    void collapseAllCategories();

    /// Expand end state: restores all category expansion states from the
    /// pre-collapse snapshot.
    void restoreAllCategories();

    void propagateOrientation(Qt::Orientation orientation);

    bool isSelectedUnder(NavigationTreeWidget* node) const;

    // Overflow computation (Top mode).
    /// Recomputes the overflow arrangement; returns whether any visibility/layout
    /// change occurred (so the caller can decide whether the indicator animates).
    bool computeOverflow(bool animated = false);

    /// Collects overflow entries in layout order (snapshot carried by the
    /// overflow signal).
    QVector<NavigationOverflowEntry> overflowEntries() const;

    QHash<QString, NavigationTreeWidget*> m_nodeIndex;

    QBoxLayout* m_layout = nullptr;
    QBoxLayout* m_mainLayout = nullptr;
    QBoxLayout* m_footerLayout = nullptr;
    bool m_addingFooter = false;
    QVector<NavigationSectionHeader*> m_headers;
    QString m_currentRouteKey;
    /// Currently flyout-active category (empty = none); the single authority on
    /// the active state.
    QString m_activeCategoryKey;
    bool m_isCompacted = false;
    /// Snapshot of all category expansion states captured at the collapse end
    /// state (routeKey -> expanded), used to restore at the expand end state.
    QHash<QString, bool> m_savedExpandStates;
    /// Whether this is a clone node inside a flyout: category clicks expand
    /// inline instead of entering flyout mode.
    bool m_inlineExpansion = false;
    Qt::Orientation m_orientation = Qt::Vertical;
    fluent::scrolling::ScrollView* m_scrollView = nullptr;

    // Overflow state (Top mode).
    NavigationToolButton* m_overflowButton = nullptr;
    QVector<NavigationTreeWidget*> m_overflowNodes;
    QVector<NavigationSectionHeader*> m_overflowHeaders;
    /// Top-level category key that stays pinned in the bar (only released or
    /// replaced when the window shrinks or a new item is picked in the overflow).
    QString m_pinnedCategoryKey;
    /// Last available width, used to detect window shrinking (shrinking releases
    /// the sticky protection).
    int m_lastTopAvailableWidth = 0;
    /// Prevents re-entry during layout iteration.
    bool m_updatingOverflow = false;
    /// Prevents signal cascade re-entry from setCurrentItem.
    bool m_settingCurrentItem = false;
    bool m_selectionFollowsFocus = false;
};
} // namespace manga::ui::navigation
