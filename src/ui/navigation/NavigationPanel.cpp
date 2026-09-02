#include "ui/navigation/NavigationPanel.h"

#include "ui/animation/AnimatedBackVisualSource.h"
#include "ui/animation/AnimatedGlobalNavVisualSource.h"
#include "ui/animation/AnimatedIcon.h"
#include "ui/navigation/NavigationFlyout.h"
#include "ui/navigation/NavigationIndicator.h"
#include "ui/navigation/NavigationMetrics.h"
#include "ui/navigation/NavigationPushButton.h"
#include "ui/navigation/NavigationToolButton.h"
#include "ui/navigation/NavigationTreeItem.h"
#include "ui/navigation/NavigationTreeWidget.h"
#include "ui/navigation/NavigationWidget.h"

#include <FluentQt/Design.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/Navigation.h>
#include <FluentQt/Windowing.h>
#include <QApplication>
#include <QDynamicPropertyChangeEvent>
#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QShowEvent>
#include <QTimer>

namespace manga::ui::navigation {
NavigationPanel::NavigationPanel(QWidget* parent) : QWidget(parent) {
    setupUi();
}

void NavigationPanel::setupUi() {
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAutoFillBackground(false);

    const auto s = themeSpacing();
    m_layout = new QBoxLayout(QBoxLayout::TopToBottom, this);
    m_layout->setContentsMargins(0, 0, 0, s.small);
    m_layout->setSpacing(0);

    m_headerLayout = new QBoxLayout(QBoxLayout::TopToBottom);
    m_headerLayout->setContentsMargins(0, s.xSmall, 0, 0);
    m_headerLayout->setSpacing(0);

    m_backButton = new NavigationToolButton(QString(), this);
    m_backButton->setAccessibleItemName(tr("返回"));
    m_backButton->setVisible(false);
    connect(m_backButton, &NavigationPushButton::clicked, this, &NavigationPanel::backRequested);
    m_headerLayout->addWidget(m_backButton, 0, Qt::AlignLeft);

    m_animatedBackIcon = new manga::ui::animation::AnimatedIcon(m_backButton);
    m_animatedBackIcon->setSource(std::make_shared<manga::ui::animation::AnimatedBackVisualSource>());

    m_paneToggleButton = new NavigationToolButton(QString(), this);
    m_paneToggleButton->setAccessibleItemName(tr("切换导航栏"));
    connect(m_paneToggleButton, &NavigationPushButton::clicked, this, &NavigationPanel::togglePane);
    m_headerLayout->addWidget(m_paneToggleButton, 0, Qt::AlignLeft);

    m_animatedPaneToggleIcon = new manga::ui::animation::AnimatedIcon(m_paneToggleButton);
    m_animatedPaneToggleIcon->setSource(std::make_shared<manga::ui::animation::AnimatedGlobalNavVisualSource>());
    m_layout->addLayout(m_headerLayout);

    m_tree = new NavigationTreeWidget(this);
    m_layout->addWidget(m_tree, 1);

    m_indicator = new NavigationIndicator(this);
    m_indicator->hide();

    connect(this, &NavigationPanel::indicatorOwnerChanged, m_tree, &NavigationTreeWidget::onIndicatorOwnerChanged);

    connect(
        m_tree, &NavigationTreeWidgetBase::expansionChanged, this, [this](const QString& routeKey, bool /*expanded*/) {
            if (m_indicatorOwner && m_tree->isAncestorOf(m_indicatorOwner->routeKey(), routeKey)) {
                refreshIndicatorVisuals(true);
            }
        });

    connect(m_tree, &NavigationTreeWidget::itemSelected, this, [this](const QString& routeKey) {
        Q_EMIT itemSelected(routeKey);
    });
    connect(m_tree,
            &NavigationTreeWidget::categoryActivated,
            this,
            [this](const QString& categoryKey, QWidget* anchorWidget) { showFlyoutMenu(categoryKey, anchorWidget); });
    connect(m_tree, &NavigationTreeWidget::categoryDeactivated, this, [this](const QString& /*categoryKey*/) {
        closeFlyoutMenu(true);
    });
    connect(m_tree,
            &NavigationTreeWidget::overflowMenuRequested,
            this,
            [this](QWidget* anchorWidget, const QVector<NavigationOverflowEntry>& entries) {
                showOverflowMenu(anchorWidget, entries);
            });

    m_paneFooterContainer = new QWidget(this);
    m_paneFooterLayout = new QBoxLayout(QBoxLayout::TopToBottom, m_paneFooterContainer);
    m_paneFooterLayout->setContentsMargins(0, 0, 0, 0);
    m_paneFooterLayout->setSpacing(0);
    m_paneFooterContainer->hide();
    m_layout->addWidget(m_paneFooterContainer);
}

void NavigationPanel::addItem(const QString& routeKey,
                              const QString& iconGlyph,
                              const QString& text,
                              const QString& parentKey,
                              NavigationItemPosition position,
                              bool selectable,
                              const QString& tooltip,
                              std::shared_ptr<manga::ui::animation::AnimatedVisualSource> visualSource) {
    if (!m_tree)
        return;
    m_tree->addItem(routeKey, iconGlyph, text, parentKey, position, selectable, tooltip, visualSource);
}

void NavigationPanel::addSectionHeader(const QString& text) {
    m_tree->addSectionHeader(text);
}

void NavigationPanel::addWidget(NavigationWidget* widget, NavigationItemPosition position) {
    m_tree->addWidget(widget, position);
}

void NavigationPanel::setCurrentItem(const QString& routeKey) {
    if (!m_tree || routeKey.isEmpty())
        return;
    NavigationTreeItem* prevOwner = m_indicatorOwner;
    {
        const QSignalBlocker blocker(this);
        const QSignalBlocker treeBlocker(m_tree);
        m_tree->setCurrentItem(routeKey, false);
    }
    NavigationTreeWidget* node = m_tree->nodeFor(routeKey);
    NavigationTreeItem* curOwner = nullptr;
    if (node && node->itemWidget()) {
        curOwner = qobject_cast<NavigationTreeItem*>(node->itemWidget());
    }
    m_indicatorOwner = curOwner;
    dispatchIndicatorAnimation(prevOwner, curOwner);
}

void NavigationPanel::setCategoryExpanded(const QString& routeKey, bool expanded, bool animated) {
    if (m_tree)
        m_tree->setCategoryExpanded(routeKey, expanded, animated);
}

QString NavigationPanel::currentRouteKey() const {
    return m_tree ? m_tree->currentRouteKey() : QString();
}

void NavigationPanel::setPaneFooter(NavigationWidget* footerWidget) {
    if (m_paneFooter == footerWidget)
        return;

    if (m_paneFooter) {
        m_paneFooterLayout->removeWidget(m_paneFooter);
        m_paneFooter->setParent(nullptr);
    }

    m_paneFooter = footerWidget;

    if (m_paneFooter) {
        m_paneFooter->setItemPosition(NavigationItemPosition::Bottom);
        m_paneFooter->setOrientation(m_orientation);
        m_paneFooter->setCompacted(m_isCompacted);
        m_paneFooterLayout->addWidget(m_paneFooter);
        m_paneFooterContainer->show();
    } else {
        m_paneFooterContainer->hide();
    }
    updateGeometry();
}

bool NavigationPanel::isBackButtonVisible() const {
    return m_backButton && m_backButton->isVisible();
}

void NavigationPanel::setBackButtonVisible(bool visible) {
    if (m_backButton) {
        bool changed = (m_backButton->isVisibleTo(this) != visible);
        m_backButton->setVisible(visible);
        if (changed) {
            updateGeometry();
        }
    }
}

bool NavigationPanel::isBackEnabled() const {
    return m_backButton && m_backButton->isEnabled();
}

void NavigationPanel::setBackEnabled(bool enabled) {
    if (m_backButton && m_backButton->isEnabled() != enabled) {
        m_backButton->setEnabled(enabled);
    }
}

bool NavigationPanel::isPaneToggleButtonVisible() const {
    return m_paneToggleButton && !m_paneToggleButton->isHidden();
}

void NavigationPanel::setPaneToggleButtonVisible(bool visible) {
    m_paneToggleExplicitlyHidden = !visible;
    if (m_paneToggleButton) {
        bool changed = (m_paneToggleButton->isVisibleTo(this) != visible);
        m_paneToggleButton->setVisible(visible);
        if (changed) {
            updateGeometry();
        }
    }
}

void NavigationPanel::setCompacted(bool compacted) {
    if (m_isCompacted == compacted)
        return;
    m_isCompacted = compacted;
    if (!compacted)
        closeFlyoutMenu(true);

    if (QWidget* focused = QApplication::focusWidget()) {
        if (isAncestorOf(focused)) {
            if (!NavigationWidget::isKeyboardMode()) {
                focused->clearFocus();
            }
        }
    }

    if (m_tree)
        m_tree->setCompacted(compacted);
    if (m_paneToggleButton)
        m_paneToggleButton->setCompacted(compacted);
    if (m_backButton)
        m_backButton->setCompacted(compacted);
    if (m_paneFooter)
        m_paneFooter->setCompacted(compacted);

    refreshIndicatorVisuals();

    Q_EMIT compactedChanged(m_isCompacted);
}

bool NavigationPanel::selectionFollowsFocus() const {
    return m_tree ? m_tree->selectionFollowsFocus() : false;
}

void NavigationPanel::setSelectionFollowsFocus(bool follows) {
    if (m_tree && m_tree->selectionFollowsFocus() != follows) {
        m_tree->setSelectionFollowsFocus(follows);
    }
}

void NavigationPanel::togglePane() {
    setCompacted(!m_isCompacted);
}

void NavigationPanel::refreshIndicatorVisuals(bool animated, NavigationTreeItem* prevLogicalOwner) {
    if (!m_indicator || !m_tree || !m_indicatorOwner)
        return;

    // Resolve visual proxy corresponding to logical owner.
    NavigationTreeItem* prevVisual = prevLogicalOwner ? m_tree->getVisualProxyFor(prevLogicalOwner) : nullptr;
    NavigationTreeItem* curVisual = m_indicatorOwner ? m_tree->getVisualProxyFor(m_indicatorOwner) : nullptr;

    NavigationWidget* prevProxy = prevVisual ? prevVisual : (prevLogicalOwner ? prevLogicalOwner : nullptr);
    NavigationWidget* curProxy = curVisual ? curVisual : (m_indicatorOwner ? m_indicatorOwner.data() : nullptr);

    if (!prevProxy || !curProxy) {
        animated = false;
    }

    // Fall back to immediate placement without animation if previous proxy is invisible.
    if (prevProxy && !prevProxy->isVisibleTo(this)) {
        animated = false;
    }

    const QRectF targetRect = m_tree->indicatorRectInHost(curProxy, this);

    if (!curProxy || !curProxy->isVisibleTo(this)) {
        m_indicator->activateAt(targetRect, false);
        return;
    }

    QRectF startRect;
    if (animated && m_indicator->currentRect().isValid() && m_indicator->currentRect().width() > 0) {
        startRect = m_indicator->currentRect();
    } else {
        startRect = m_tree->indicatorRectInHost(prevProxy, this);
    }

    // Transfer persistent indicator visibility.
    if (m_visualIndicatorOwner) {
        m_visualIndicatorOwner->setShowIndicator(false);
    }
    if (curVisual) {
        m_visualIndicatorOwner = curVisual;
    } else {
        m_visualIndicatorOwner = m_indicatorOwner;
    }

    // Reset and bind animation lifecycle connections to prevent interleaved signals.
    m_indicator->disconnect(SIGNAL(flightFinished()));
    m_indicator->disconnect(SIGNAL(flightStarted()));

    connect(
        m_indicator,
        &NavigationIndicator::flightStarted,
        this,
        [this, prevLogicalOwner]() {
            if (prevLogicalOwner)
                Q_EMIT indicatorOwnerChanged(prevLogicalOwner, false);
        },
        Qt::SingleShotConnection);

    connect(
        m_indicator,
        &NavigationIndicator::flightFinished,
        this,
        [this]() {
            if (m_visualIndicatorOwner)
                m_visualIndicatorOwner->setShowIndicator(true);
            if (m_indicatorOwner)
                Q_EMIT indicatorOwnerChanged(m_indicatorOwner, true);
        },
        Qt::SingleShotConnection);

    if (animated) {
        if (!m_indicator->isFlying() && startRect.isValid()) {
            m_indicator->setInitialPosition(startRect);
        }
        m_indicator->activateAt(targetRect, true);
    } else {
        m_indicator->activateAt(targetRect, false);
    }
}

void NavigationPanel::moveFocusBy(int delta) {
    if (m_tree)
        m_tree->moveFocusBy(delta);
}

QSize NavigationPanel::sizeHint() const {
    if (m_orientation == Qt::Horizontal) {
        return QSize(QWIDGETSIZE_MAX, kTopBarItemHeight);
    }
    const int w = m_isCompacted ? Breakpoints::NavigationPaneCompactWidth : Breakpoints::NavigationPaneExpandedWidth;
    return QSize(w, m_layout ? m_layout->sizeHint().height() : QWidget::sizeHint().height());
}

void NavigationPanel::paintEvent(QPaintEvent* event) {
    // Let underlying acrylic surface show through without background fill when floating.
    if (!m_surfaceVisible) {
        const QColor fill =
            fluent::windowing::windowChromeBackdropFill(*this, window(), window() && window()->isActiveWindow());
        if (fill.isValid()) {
            QPainter painter(this);
            painter.fillRect(rect(), fill);
        }
    }
    QWidget::paintEvent(event);
}

void NavigationPanel::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (m_indicator)
        m_indicator->raise();

    // Initial show fallback: select first selectable item without animation if none is selected.
    if (!m_indicatorOwner && m_tree) {
        if (NavigationTreeItem* firstSelectable = m_tree->findFirstSelectableItem()) {
            setCurrentItem(firstSelectable->routeKey());
        }
    } else if (m_indicatorOwner) {
        refreshIndicatorVisuals(false);
    }
}

void NavigationPanel::setOrientation(Qt::Orientation orientation) {
    if (m_orientation == orientation)
        return;
    m_orientation = orientation;

    const auto direction = (orientation == Qt::Horizontal) ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;

    m_layout->setDirection(direction);
    m_headerLayout->setDirection(direction);
    m_paneFooterLayout->setDirection(direction);

    // Reset size constraints to prevent fixed sizes from constraining parent layout.
    setMinimumSize(0, 0);
    setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);

    const auto s = themeSpacing();
    if (orientation == Qt::Horizontal) {
        m_paneToggleButton->hide();
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(kTopBarItemHeight);
        // Clear outer margins in horizontal mode so items occupy full top bar height.
        m_layout->setContentsMargins(0, 0, 0, 0);
        m_headerLayout->setContentsMargins(s.small, 0, 0, 0);
        // Align pane footer to the right by stretching available space.
        m_layout->setStretch(2, 0);
    } else {
        if (!m_paneToggleExplicitlyHidden) {
            m_paneToggleButton->show();
        }
        setMaximumHeight(QWIDGETSIZE_MAX);
        setMinimumHeight(0);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_layout->setContentsMargins(0, 0, 0, s.small);
        m_headerLayout->setContentsMargins(0, s.xSmall, 0, 0);
    }

    if (m_tree) {
        m_tree->setOrientation(orientation);
    }
    if (m_paneFooter) {
        m_paneFooter->setOrientation(orientation);
    }
    if (m_indicator) {
        m_indicator->setOrientation(orientation);
    }

    refreshIndicatorVisuals(false);
    updateGeometry();
}

bool NavigationPanel::event(QEvent* event) {
    if (event->type() == QEvent::WindowDeactivate) {
        if (m_activeFlyout &&
            (qApp->activeWindow() == m_activeFlyout.data() || m_activeFlyout->isAncestorOf(qApp->focusWidget()))) {
            // Do not dismiss when focus moves into the active flyout.
        } else {
            closeFlyoutMenu(true);
        }
    }
    if (event->type() == QEvent::DynamicPropertyChange) {
        if (auto* change = static_cast<QDynamicPropertyChangeEvent*>(event);
            change && change->propertyName() == "fluentNavPaneFloating") {
            setSurfaceVisible(property("fluentNavPaneFloating").toBool());
        }
    }
    return QWidget::event(event);
}

void NavigationPanel::setSurfaceVisible(bool visible) {
    if (m_surfaceVisible == visible)
        return;
    m_surfaceVisible = visible;
    setAttribute(Qt::WA_NoSystemBackground, visible);
    setAttribute(Qt::WA_TranslucentBackground, visible);
    update();
}

void NavigationPanel::triggerCrossWindowPortal(NavigationFlyout* flyout,
                                               QWidget* anchorWidget,
                                               NavigationTreeItem* prevOwner,
                                               NavigationTreeItem* curClone) {
    if (m_orientation != Qt::Horizontal || !m_indicator || !flyout || !curClone)
        return;

    m_flyoutCloseIntent = FlyoutCloseIntent::PortalReturn;

    NavigationTreeItem* owner = m_indicatorOwner;
    NavigationTreeItem* visualOwner = m_tree->getVisualProxyFor(prevOwner ? prevOwner : owner);

    // Use current indicator visual geometry as launch origin to preserve continuity.
    QRectF topHostStartRect;
    if (m_indicator && m_indicator->currentRect().isValid() && m_indicator->currentRect().width() > 0) {
        topHostStartRect = m_indicator->currentRect();
    } else {
        NavigationWidget* topItem = visualOwner ? visualOwner : qobject_cast<NavigationWidget*>(anchorWidget);
        if (!topItem && owner)
            topItem = owner;
        topHostStartRect = m_tree->indicatorRectInHost(topItem, this);
    }

    // Map visual target to global coordinates after flyout positioning is finalized.
    NavigationTreeItem* visualTarget = flyout->getVisualProxyFor(curClone);
    if (!visualTarget)
        visualTarget = curClone;
    const QRectF flyoutRect = visualTarget->indicatorRect();
    const QPointF globalTargetTopLeft = visualTarget->mapToGlobal(flyoutRect.topLeft().toPoint());
    const QRectF globalTargetRect(globalTargetTopLeft, flyoutRect.size());

    const QPointF globalStartTopLeft = this->mapToGlobal(topHostStartRect.topLeft().toPoint());
    const QRectF globalStartRect(globalStartTopLeft, topHostStartRect.size());

    // Top bar indicator: collapse from current host position.
    const QRectF topHostTargetRect(this->mapFromGlobal(globalTargetRect.topLeft().toPoint()), globalTargetRect.size());

    // Flyout indicator: expand downward at destination.
    const QRectF flyoutHostStartRect(flyout->mapFromGlobal(globalStartRect.topLeft().toPoint()),
                                     globalStartRect.size());
    const QRectF itemInFlyoutHost = flyout->indicatorRectInHost(curClone);

    // Extinguish static indicator on origin node when flight begins.
    connect(
        m_indicator,
        &NavigationIndicator::flightStarted,
        this,
        [this, prevOwner, owner]() {
            NavigationTreeItem* itemToExtinguish = prevOwner ? prevOwner : owner;
            if (itemToExtinguish)
                Q_EMIT indicatorOwnerChanged(itemToExtinguish, false);
            if (m_visualIndicatorOwner) {
                m_visualIndicatorOwner->setShowIndicator(false);
                m_visualIndicatorOwner = nullptr;
            }
        },
        Qt::SingleShotConnection);

    m_indicator->playCrossWindowPortal(topHostStartRect, topHostTargetRect);

    // Light up static indicator on destination node when flight completes.
    if (NavigationIndicator* flyoutIndicator = flyout->indicator()) {
        connect(
            flyoutIndicator,
            &NavigationIndicator::flightFinished,
            this,
            [this, flyout, curClone]() {
                if (m_indicatorOwner)
                    Q_EMIT indicatorOwnerChanged(m_indicatorOwner, true);
            },
            Qt::SingleShotConnection);
    }

    // Use indicatorRectInHost as destination target to support collapsed parent proxies.
    flyout->playSelectedItemCrossPortal(curClone, flyoutHostStartRect, itemInFlyoutHost);
}

void NavigationPanel::showFlyoutMenu(const QString& categoryKey, QWidget* anchorWidget) {
    if (!m_tree || !anchorWidget)
        return;

    const QRect anchorRect(anchorWidget->mapTo(this, QPoint(0, 0)), anchorWidget->size());

    closeFlyoutMenu(false);

    auto* flyout = new NavigationFlyout(m_tree, this);
    flyout->rebuildSubtree(categoryKey);

    connect(flyout, &NavigationFlyout::aboutToShow, this, [this, flyout]() { m_activeFlyout = flyout; });
    connect(flyout, &NavigationFlyout::aboutToHide, this, [this, flyout]() {
        if (m_activeFlyout == flyout)
            m_activeFlyout = nullptr;
    });

    connect(this, &NavigationPanel::indicatorOwnerChanged, flyout, &NavigationFlyout::onIndicatorOwnerChanged);

    // Re-dispatch slide animation when expanding or collapsing an ancestor of selected item.
    connect(flyout,
            &NavigationFlyout::expansionChanged,
            this,
            [this, flyoutPtr = QPointer<NavigationFlyout>(flyout)](const QString& routeKey, bool /*expanded*/) {
                if (flyoutPtr && m_indicatorOwner && m_tree->isAncestorOf(m_indicatorOwner->routeKey(), routeKey)) {
                    QTimer::singleShot(0, this, [this, flyoutPtr]() {
                        if (flyoutPtr) {
                            dispatchIndicatorAnimation(m_indicatorOwner, m_indicatorOwner);
                        }
                    });
                }
            });

    // Clean up on flyout close by updating layout and triggering pending close animations.
    // Use aboutToHide to synchronize panel indicator return with flyout fade-out.
    connect(flyout, &NavigationFlyout::aboutToHide, this, [this, categoryKey, anchorWidget]() {
        animateFlyoutClosed(categoryKey, anchorWidget);
    });

    // In horizontal mode, trigger cross-window portal after flyout positioning settles.
    // Wait for opened signal since item geometry is unreliable before layout completes.
    if (m_orientation == Qt::Horizontal && m_indicator &&
        m_tree->isAncestorOf(m_tree->currentRouteKey(), categoryKey)) {
        connect(
            flyout,
            &NavigationFlyout::opened,
            this,
            [this, flyout, anchorWidget]() {
                if (NavigationTreeItem* curClone = flyout->cloneItemFor(m_tree->currentRouteKey())) {
                    triggerCrossWindowPortal(flyout, anchorWidget, m_indicatorOwner, curClone);
                }
            },
            Qt::SingleShotConnection);
    }

    flyout->addLightDismissPassthrough(
        this); // Allow clicks in navigation panel to pass through for seamless switching.

    if (m_orientation == Qt::Horizontal) {
        // Horizontal mode: position flyout below panel bottom edge, horizontally aligned with anchor.
        flyout->setPlacement(NavigationFlyout::Placement::Bottom);
        flyout->showAt(anchorWidget);
    } else {
        // Vertical compact mode: position flyout to the right of panel, vertically centered on anchor.
        flyout->setPlacement(NavigationFlyout::Placement::Right);
        flyout->showAt(anchorWidget);
    }
}

void NavigationPanel::closeFlyoutMenu(bool animated) {
    if (!m_activeFlyout)
        return;
    auto* flyout = m_activeFlyout.data();

    if (animated && flyout->isVisible()) {
        connect(flyout, &NavigationFlyout::closed, flyout, &QObject::deleteLater);
        flyout->close();
    } else {
        // Invoke close() even without animation to complete the aboutToHide and closed lifecycle.
        flyout->setExitAnimationEnabled(false);
        connect(flyout, &NavigationFlyout::closed, flyout, &QObject::deleteLater);
        flyout->close();
    }
}

void NavigationPanel::showOverflowMenu(QWidget* anchorWidget, const QVector<NavigationOverflowEntry>& entries) {
    if (!anchorWidget || entries.isEmpty())
        return;

    const QRect anchorRect(anchorWidget->mapTo(this, QPoint(0, 0)), anchorWidget->size());

    closeOverflowMenu(false);

    auto* flyout = new NavigationFlyout(m_tree, this);
    flyout->rebuildSubtreeFromEntries(entries);

    connect(flyout, &NavigationFlyout::aboutToShow, this, [this, flyout]() { m_activeFlyout = flyout; });
    connect(flyout, &NavigationFlyout::aboutToHide, this, [this, flyout]() {
        if (m_activeFlyout == flyout)
            m_activeFlyout = nullptr;
    });

    connect(this, &NavigationPanel::indicatorOwnerChanged, flyout, &NavigationFlyout::onIndicatorOwnerChanged);

    flyout->addLightDismissPassthrough(this);

    // Re-dispatch slide animation when expanding or collapsing an ancestor of selected item.
    connect(flyout,
            &NavigationFlyout::expansionChanged,
            this,
            [this, flyoutPtr = QPointer<NavigationFlyout>(flyout)](const QString& routeKey, bool /*expanded*/) {
                if (flyoutPtr && m_indicatorOwner && m_tree->isAncestorOf(m_indicatorOwner->routeKey(), routeKey)) {
                    QTimer::singleShot(0, this, [this, flyoutPtr]() {
                        if (flyoutPtr) {
                            dispatchIndicatorAnimation(m_indicatorOwner, m_indicatorOwner);
                        }
                    });
                }
            });

    // Clean up on overflow flyout close: update layout and trigger transition animation.
    connect(flyout, &NavigationFlyout::aboutToHide, this, [this, anchorWidget]() {
        animateFlyoutClosed(QString(), anchorWidget);
    });

    // In horizontal mode, trigger cross-window portal once overflow flyout finishes opening.
    if (m_orientation == Qt::Horizontal && m_indicator) {
        connect(
            flyout,
            &NavigationFlyout::opened,
            this,
            [this, flyout, anchorWidget]() {
                if (NavigationTreeItem* curClone = flyout->cloneItemFor(m_tree->currentRouteKey())) {
                    triggerCrossWindowPortal(flyout, anchorWidget, m_indicatorOwner, curClone);
                }
            },
            Qt::SingleShotConnection);
    }

    flyout->setPlacement(NavigationFlyout::Placement::BottomRight);
    flyout->showAt(anchorWidget);
}

void NavigationPanel::closeOverflowMenu(bool animated) {
    if (!m_activeFlyout)
        return;
    auto* flyout = m_activeFlyout.data();

    if (animated && flyout->isVisible()) {
        connect(flyout, &NavigationFlyout::closed, flyout, &QObject::deleteLater);
        flyout->close();
    } else {
        flyout->setExitAnimationEnabled(false);
        connect(flyout, &NavigationFlyout::closed, flyout, &QObject::deleteLater);
        flyout->close();
    }
}

void NavigationPanel::dispatchIndicatorAnimation(NavigationTreeItem* prevOwner, NavigationTreeItem* curOwner) {
    if (!curOwner)
        return;

    // Retrieve currently active flyout.
    NavigationFlyout* activeFlyout = m_activeFlyout.data();

    // Check whether clone items exist in the active flyout.
    NavigationTreeItem* prevClone =
        activeFlyout && prevOwner ? activeFlyout->cloneItemFor(prevOwner->routeKey()) : nullptr;
    NavigationTreeItem* curClone = activeFlyout ? activeFlyout->cloneItemFor(curOwner->routeKey()) : nullptr;

    if (activeFlyout && curClone && !curOwner->isCategory() && prevOwner != curOwner) {
        // Decision 0 (highest priority): leaf item clicked and flyout is closing.
        // Check whether previous item also belongs to this flyout:
        // - If prevClone exists, both items share this flyout scope; host uses PortalReturn without cross-item slide.
        // - If prevClone is null, previous item was outside; host uses LeafSlide across items.
        if (prevClone) {
            m_flyoutCloseIntent = FlyoutCloseIntent::PortalReturn;
        } else {
            m_flyoutCloseIntent = FlyoutCloseIntent::LeafSlide;
        }
        m_flyoutPrevOwner = prevOwner;
        if (prevOwner)
            Q_EMIT indicatorOwnerChanged(prevOwner, false);
        return;
    }

    if (activeFlyout && prevClone && curClone) {
        // Decision 1: flight within same flyout (inline expansion for selectable categories).
        NavigationIndicator* flyoutInd = activeFlyout->indicator();
        if (!flyoutInd)
            return;

        // Synchronize origin: preserve current visual position when expanding or collapsing the same node.
        if (prevOwner && prevOwner != curOwner) {
            const QRectF startRect = activeFlyout->indicatorRectInHost(prevClone);
            flyoutInd->setInitialPosition(startRect);
        }

        connect(
            flyoutInd,
            &NavigationIndicator::flightStarted,
            this,
            [this, prevOwner]() {
                if (prevOwner)
                    Q_EMIT indicatorOwnerChanged(prevOwner, false);
            },
            Qt::SingleShotConnection);
        connect(
            flyoutInd,
            &NavigationIndicator::flightFinished,
            this,
            [this, curOwner]() {
                if (m_indicatorOwner == curOwner)
                    Q_EMIT indicatorOwnerChanged(curOwner, true);
            },
            Qt::SingleShotConnection);

        const QRectF targetRect = activeFlyout->indicatorRectInHost(curClone);
        activeFlyout->playInternalFlight(targetRect);
    } else if (activeFlyout && !prevClone && curClone) {
        // Decision 2: cross-window portal transition.
        QWidget* anchor = activeFlyout->anchor();
        triggerCrossWindowPortal(activeFlyout, anchor, prevOwner, curClone);
    } else {
        // Decision 3: standard flight within main host or visual fallback.
        if (!m_indicator)
            return;

        if (!prevOwner) {
            refreshIndicatorVisuals(false);
            return;
        }

        // Delegate origin synchronization and signal dispatching to refreshIndicatorVisuals.
        refreshIndicatorVisuals(true, prevOwner);
    }
}

void NavigationPanel::animateFlyoutClosed(const QString& categoryKey, QWidget* anchorWidget) {
    if (!m_tree) {
        m_flyoutCloseIntent = FlyoutCloseIntent::None;
        m_flyoutPrevOwner = nullptr;
        return;
    }

    // 1. Update layout so coordinate calculations use latest geometry.
    if (!categoryKey.isEmpty())
        m_tree->dismissCategory();
    m_tree->updateOverflow();

    // 2. Re-read current owner in case selection changed during animation.
    NavigationTreeItem* logicOwner = m_indicatorOwner;
    if (!logicOwner) {
        m_flyoutCloseIntent = FlyoutCloseIntent::None;
        m_flyoutPrevOwner = nullptr;
        return;
    }

    // 3. Take close intent and previous owner, then reset state.
    const FlyoutCloseIntent intent = m_flyoutCloseIntent;
    NavigationTreeItem* prevOwner = m_flyoutPrevOwner;
    m_flyoutCloseIntent = FlyoutCloseIntent::None;
    m_flyoutPrevOwner = nullptr;

    // 4. Defer calculation to next frame until overflow and category layout settle.
    QTimer::singleShot(0, this, [this, categoryKey, anchorWidget, logicOwner, intent, prevOwner]() {
        // Verify owner has not changed in deferred callback.
        if (m_indicatorOwner != logicOwner) {
            return;
        }

        const bool belongsToFlyout =
            categoryKey.isEmpty()
                ? m_tree->hasOverflowItems()
                : (logicOwner->routeKey() == categoryKey || m_tree->isAncestorOf(logicOwner->routeKey(), categoryKey));

        if (!belongsToFlyout) {
            Q_EMIT indicatorOwnerChanged(logicOwner, true);
            refreshIndicatorVisuals(false);
            return;
        }

        // Target geometry: visual proxy of the owner on the top bar.
        NavigationTreeItem* visualOwner = m_tree->getVisualProxyFor(logicOwner);
        NavigationWidget* returnItem = visualOwner ? visualOwner : qobject_cast<NavigationWidget*>(anchorWidget);
        if (!returnItem)
            returnItem = logicOwner;
        const QRectF targetRect = m_tree->indicatorRectInHost(returnItem, this);

        // Branch animation based on close intent.
        switch (intent) {
            case FlyoutCloseIntent::LeafSlide: {
                // Delegate synchronization, animation, and signal dispatching to refreshIndicatorVisuals.
                if (m_indicator) {
                    if (prevOwner) {
                        NavigationTreeItem* prevVisual = m_tree->getVisualProxyFor(prevOwner);
                        if (prevVisual) {
                            m_indicator->setInitialPosition(m_tree->indicatorRectInHost(prevVisual, this));
                        }
                    }
                    refreshIndicatorVisuals(true, prevOwner);
                }
                break;
            }
            case FlyoutCloseIntent::PortalReturn: {
                // Register completion callback specifically for PortalReturn.
                connect(
                    m_indicator,
                    &NavigationIndicator::flightFinished,
                    this,
                    [this, logicOwner]() {
                        if (m_indicatorOwner == logicOwner) {
                            Q_EMIT indicatorOwnerChanged(logicOwner, true);
                            refreshIndicatorVisuals(false);
                        }
                    },
                    Qt::SingleShotConnection);
                m_indicator->playPortalReturn(targetRect);
                break;
            }
            case FlyoutCloseIntent::None: {
                refreshIndicatorVisuals(false, prevOwner);
                break;
            }
        }
    });
}
} // namespace manga::ui::navigation
