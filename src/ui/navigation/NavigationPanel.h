#pragma once

#include "ui/navigation/NavigationFlyout.h"
#include "ui/navigation/NavigationFocusHost.h"
#include "ui/navigation/NavigationMetrics.h"

#include <FluentQt/Foundation.h>
#include <QPointer>
#include <QRect>
#include <QVBoxLayout>
#include <QVariantAnimation>
#include <QWidget>

#include <memory>

class QPaintEvent;
class QShowEvent;

namespace manga::ui::animation {
class AnimatedIcon;
class AnimatedVisualSource;
} // namespace manga::ui::animation

namespace manga::ui::navigation {
class NavigationIndicator;
class NavigationTreeItem;
class NavigationTreeWidget;
class NavigationToolButton;
class NavigationWidget;

/**
 * @brief Integrated navigation panel.
 *
 * Serves as the main chrome widget of NavigationView, hosting the top header area (back and pane
 * toggle buttons), main navigation tree, pane footer, and footer widgets. It observes physical
 * width changes during resize events to calculate compaction and dispatches updates to the tree,
 * ensuring indicator geometry and layout remain synchronized across breakpoint transitions.
 */
class NavigationPanel : public QWidget, public fluent::FluentElement, public INavigationFocusHost {
    Q_OBJECT
    Q_PROPERTY(bool backButtonVisible READ isBackButtonVisible WRITE setBackButtonVisible)

    Q_PROPERTY(bool backEnabled READ isBackEnabled WRITE setBackEnabled)

    Q_PROPERTY(bool paneToggleButtonVisible READ isPaneToggleButtonVisible WRITE setPaneToggleButtonVisible)

    Q_PROPERTY(bool compacted READ isCompacted WRITE setCompacted NOTIFY compactedChanged)

    Q_PROPERTY(bool selectionFollowsFocus READ selectionFollowsFocus WRITE setSelectionFollowsFocus)

public:
    explicit NavigationPanel(QWidget* parent = nullptr);

    ~NavigationPanel() override = default;

    /**
     * @brief Adds a navigation route item.
     * @param routeKey Unique route identifier.
     * @param iconGlyph Fluent icon glyph.
     * @param text Display text.
     * @param parentKey Parent category key, or empty if this is a top-level node.
     * @param position Target container position (Top main list or Bottom pane footer).
     * @param selectable Whether the item can be selected.
     * @param tooltip Tooltip text (defaults to text if empty).
     * @param visualSource Dynamic vector animation source (optional).
     */
    void addItem(const QString& routeKey,
                 const QString& iconGlyph,
                 const QString& text,
                 const QString& parentKey = QString(),
                 NavigationItemPosition position = NavigationItemPosition::Top,
                 bool selectable = true,
                 const QString& tooltip = QString(),
                 std::shared_ptr<manga::ui::animation::AnimatedVisualSource> visualSource = nullptr);

    /**
     * @brief Adds a section header.
     * @param text Section header text.
     */
    void addSectionHeader(const QString& text);

    /**
     * @brief Adds a custom navigation widget.
     * @param widget Constructed navigation widget.
     * @param position Target container position (Top main list or Bottom pane footer).
     */
    void addWidget(NavigationWidget* widget, NavigationItemPosition position = NavigationItemPosition::Top);

    /**
     * @brief Sets the currently active route.
     * @param routeKey Target route identifier.
     */
    void setCurrentItem(const QString& routeKey);

    /**
     * @brief Enables or disables a navigation item.
     * @param routeKey Target route identifier.
     * @param enabled Whether the item accepts interaction. A disabled item is
     * painted with the disabled text token and cannot be clicked or focused.
     */
    void setItemEnabled(const QString& routeKey, bool enabled);

    /**
     * @brief Gets the currently active route key.
     * @return Current route key string.
     */
    QString currentRouteKey() const;

    /**
     * @brief Sets the bottom fixed slot widget (equivalent to WinUI 3 PaneFooter).
     * @param footerWidget Bottom slot widget pointer (inherits from NavigationWidget, responds to compact and
     * orientation).
     */
    void setPaneFooter(NavigationWidget* footerWidget);

    /**
     * @brief Gets the mounted bottom slot widget.
     * @return Bottom slot widget pointer.
     */
    NavigationWidget* paneFooter() const {
        return m_paneFooter;
    }

    /**
     * @brief Programmatically expands or collapses a category (ignored in compact mode).
     * @param routeKey Category node route key.
     * @param expanded Whether to expand the category.
     * @param animated Whether to animate height transitions.
     */
    void setCategoryExpanded(const QString& routeKey, bool expanded, bool animated = true);

    /**
     * @brief Gets the selection indicator instance.
     * @return Indicator pointer.
     */
    NavigationIndicator* indicator() const {
        return m_indicator;
    }

    /**
     * @brief Gets the navigation tree widget instance.
     * @return Navigation tree pointer.
     */
    NavigationTreeWidget* tree() const {
        return m_tree;
    }

    /**
     * @brief Gets the back button instance.
     * @return Back button pointer.
     */
    NavigationToolButton* backButton() const {
        return m_backButton;
    }

    /**
     * @brief Gets the pane toggle hamburger button instance.
     * @return Pane toggle button pointer.
     */
    NavigationToolButton* paneToggleButton() const {
        return m_paneToggleButton;
    }

    /**
     * @brief Checks if the back button is visible.
     * @return True if visible.
     */
    bool isBackButtonVisible() const;

    /**
     * @brief Sets back button visibility.
     * @param visible Whether the button is visible.
     */
    void setBackButtonVisible(bool visible);

    /**
     * @brief Checks if the back button is enabled.
     * @return True if enabled.
     */
    bool isBackEnabled() const;

    /**
     * @brief Sets back button enabled state.
     * @param enabled Whether the button is enabled.
     */
    void setBackEnabled(bool enabled);

    /**
     * @brief Checks if the hamburger button is visible.
     * @return True if visible.
     */
    bool isPaneToggleButtonVisible() const;

    /**
     * @brief Sets hamburger button visibility.
     * @param visible Whether the button is visible.
     */
    void setPaneToggleButtonVisible(bool visible);

    /**
     * @brief Checks if the hamburger button is explicitly hidden.
     * @return True if explicitly hidden.
     */
    bool isPaneToggleExplicitlyHidden() const {
        return m_paneToggleExplicitlyHidden;
    }

    /**
     * @brief Sets whether the hamburger button is explicitly hidden.
     * @param hidden Whether to hide the button.
     */
    void setPaneToggleExplicitlyHidden(bool hidden);

    /**
     * @brief Checks if the background surface is visible.
     * @return True if the surface is visible.
     */
    bool isSurfaceVisible() const {
        return m_surfaceVisible;
    }

    /**
     * @brief Checks if currently in compact icon column mode (48px).
     * @return True if in compact mode.
     */
    bool isCompacted() const {
        return m_isCompacted;
    }

    /**
     * @brief Sets compact mode state.
     * @param compacted Whether compact mode is active.
     */
    void setCompacted(bool compacted);

    /**
     * @brief Checks if selection follows focus mode is enabled.
     * @return True if selection follows focus.
     */
    bool selectionFollowsFocus() const;

    /**
     * @brief Sets whether selection follows focus mode is enabled.
     * @param follows Whether selection should follow focus.
     */
    void setSelectionFollowsFocus(bool follows);

    /**
     * @brief Refreshes indicator geometry.
     * @param animated Whether to animate geometry interpolation.
     */
    void updateIndicatorVisuals(bool animated = false) {
        refreshIndicatorVisuals(animated);
    }

    /**
     * @brief Gets the visual widget or flyout proxy widget for the specified route.
     * @param routeKey Route key.
     * @return Matching widget pointer.
     */
    QWidget* visualWidgetForRoute(const QString& routeKey) const;

    /**
     * @brief Toggles pane expanded and collapsed state.
     */
    void togglePane();

    /**
     * @brief Sets navigation layout orientation.
     * @param orientation Layout orientation.
     */
    void setOrientation(Qt::Orientation orientation);

    /**
     * @brief Gets current navigation layout orientation.
     * @return Layout orientation enum.
     */
    Qt::Orientation orientation() const {
        return m_orientation;
    }

    /**
     * @brief Moves focus up/left or down/right.
     * @param delta Step delta (-1 for up/left, +1 for down/right).
     */
    void moveFocusBy(int delta) override;

    /**
     * @brief Gets recommended widget size hint.
     * @return Recommended size hint.
     */
    QSize sizeHint() const override;

Q_SIGNALS:
    /// Primary page switching signal broadcast by the navigation tree.
    void itemSelected(const QString& routeKey);

    void backRequested();

    void compactedChanged(bool isCompacted);

    void indicatorOwnerChanged(NavigationTreeItem* item, bool isOwner);

public:
    void onThemeUpdated() override {
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override;

    void showEvent(QShowEvent* event) override;

    bool event(QEvent* event) override;

private:
    void setupUi();

    void setSurfaceVisible(bool visible);

    void showFlyoutMenu(const QString& categoryKey, QWidget* anchorWidget);

    void showOverflowMenu(QWidget* anchorWidget, const QVector<NavigationOverflowEntry>& entries);

    void closeFlyoutMenu(bool animated = true);

    void closeOverflowMenu(bool animated = true);

    void triggerCrossWindowPortal(NavigationFlyout* flyout,
                                  QWidget* anchorWidget,
                                  NavigationTreeItem* prevOwner,
                                  NavigationTreeItem* curClone = nullptr);

    void dispatchIndicatorAnimation(NavigationTreeItem* prevOwner, NavigationTreeItem* curOwner);

    void animateFlyoutClosed(const QString& categoryKey, QWidget* anchorWidget);

    void refreshIndicatorVisuals(bool animated = false, NavigationTreeItem* prevOwner = nullptr);

    /// Animation intent executed after flyout dismissal.
    enum class FlyoutCloseIntent {
        None,         // No special animation (light dismiss or owner outside flyout).
        LeafSlide,    // Leaf click: slide top bar indicator from previous owner to current visual proxy.
        PortalReturn, // Indicator inside flyout: play portal return animation.
    };

private:
    bool m_isCompacted = false;
    bool m_surfaceVisible = false;
    bool m_paneToggleExplicitlyHidden = false;
    Qt::Orientation m_orientation = Qt::Vertical;

    QBoxLayout* m_layout = nullptr;
    QBoxLayout* m_headerLayout = nullptr;
    NavigationToolButton* m_backButton = nullptr;
    manga::ui::animation::AnimatedIcon* m_animatedBackIcon = nullptr;
    NavigationToolButton* m_paneToggleButton = nullptr;
    manga::ui::animation::AnimatedIcon* m_animatedPaneToggleIcon = nullptr;
    NavigationTreeWidget* m_tree = nullptr;
    NavigationIndicator* m_indicator = nullptr;
    QWidget* m_paneFooterContainer = nullptr;
    QBoxLayout* m_paneFooterLayout = nullptr;
    QPointer<NavigationWidget> m_paneFooter;
    QPointer<NavigationTreeItem> m_indicatorOwner;
    QPointer<NavigationTreeItem> m_visualIndicatorOwner;

    QPointer<NavigationFlyout> m_activeFlyout;
    FlyoutCloseIntent m_flyoutCloseIntent = FlyoutCloseIntent::None;
    QPointer<NavigationTreeItem> m_flyoutPrevOwner;
};
} // namespace manga::ui::navigation
