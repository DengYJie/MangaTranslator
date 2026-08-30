#include "ui/navigation/NavigationTreeWidget.h"

#include "ui/navigation/NavigationFlyout.h"
#include "ui/navigation/NavigationIndicator.h"
#include "ui/navigation/NavigationMetrics.h"
#include "ui/navigation/NavigationSectionHeader.h"
#include "ui/navigation/NavigationSeparator.h"
#include "ui/navigation/NavigationToolButton.h"
#include "ui/navigation/NavigationTreeItem.h"
#include "ui/navigation/NavigationWidget.h"

#include <FluentQt/Design.h>
#include <FluentQt/DialogsFlyouts.h>
#include <FluentQt/Scrolling.h>
#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QEasingCurve>
#include <QEvent>
#include <QGuiApplication>
#include <QHash>
#include <QPaintEvent>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QTimer>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <functional>

namespace manga::ui::navigation {
namespace {
class NavigationScrollView final : public fluent::scrolling::ScrollView {
public:
    explicit NavigationScrollView(QWidget* parent = nullptr) : fluent::scrolling::ScrollView(parent) {
        applyTransparentSurface();
    }

protected:
    void onThemeUpdated() override {
        fluent::scrolling::ScrollView::onThemeUpdated();
        applyTransparentSurface();
    }

private:
    void applyTransparentSurface() {
        setAutoFillBackground(false);
        QWidget* area = viewport();
        if (!area)
            return;

        area->setAutoFillBackground(false);
        area->setAttribute(Qt::WA_TranslucentBackground, false);
        area->setAttribute(Qt::WA_OpaquePaintEvent, false);
        QPalette palette = area->palette();
        palette.setColor(QPalette::Window, Qt::transparent);
        palette.setColor(QPalette::Base, Qt::transparent);
        area->setPalette(palette);
        area->update();
    }
};
} // namespace

NavigationTreeWidget::NavigationTreeWidget(QWidget* parent) : NavigationTreeWidgetBase(this, parent) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoFillBackground(false);
    m_layout = new QBoxLayout(QBoxLayout::TopToBottom, this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    m_mainLayout = new QBoxLayout(QBoxLayout::TopToBottom);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(0);
    m_mainLayout->addStretch(1);

    m_footerLayout = new QBoxLayout(QBoxLayout::TopToBottom);
    m_footerLayout->setContentsMargins(0, 0, 0, 0);
    m_footerLayout->setSpacing(0);

    auto* scrollContainer = new QWidget(this);
    scrollContainer->setLayout(m_mainLayout);
    scrollContainer->setObjectName("NavigationScrollContainer");
    scrollContainer->setStyleSheet("background: transparent;");

    m_scrollView = new NavigationScrollView(this);
    m_scrollView->setWidgetResizable(true);
    m_scrollView->setContentWidget(scrollContainer);
    m_scrollView->setFrameShape(QFrame::NoFrame);
    m_scrollView->setVerticalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Visible);
    applyTransparentScrollView();

    m_layout->addWidget(m_scrollView, 1);
    m_layout->addLayout(m_footerLayout);

    m_overflowButton = new NavigationToolButton(Typography::Icons::More, this);
    m_overflowButton->setObjectName(QStringLiteral("NavigationOverflowButton"));
    m_overflowButton->setOrientation(Qt::Horizontal);
    m_overflowButton->setAnimatedMove(true);
    m_overflowButton->hide();
    connect(m_overflowButton, &NavigationToolButton::clicked, this, [this]() {
        Q_EMIT overflowMenuRequested(m_overflowButton, overflowEntries());
    });
    m_mainLayout->insertWidget(m_mainLayout->count() - 1, m_overflowButton);
}

NavigationTreeWidget::NavigationTreeWidget(NavigationTreeWidget* rootNode)
    : NavigationTreeWidgetBase(rootNode, rootNode) {
    // Logical node holds tree structure only and does not participate in layout, avoiding empty widgets intercepting
    // mouse events.
    hide();
}

NavigationTreeWidget* NavigationTreeWidget::nodeFor(const QString& routeKey) {
    return m_root->m_nodeIndex.value(routeKey, nullptr);
}

const NavigationTreeWidget* NavigationTreeWidget::nodeFor(const QString& routeKey) const {
    return m_root->m_nodeIndex.value(routeKey, nullptr);
}

void NavigationTreeWidget::addItem(const QString& routeKey,
                                   const QString& iconGlyph,
                                   const QString& text,
                                   const QString& parentKey,
                                   NavigationItemPosition position,
                                   bool selectable,
                                   const QString& tooltip,
                                   std::shared_ptr<manga::ui::animation::AnimatedVisualSource> visualSource) {
    auto* node = new NavigationTreeWidget(m_root);
    node->m_routeKey = routeKey;
    node->m_parentNode = parentKey.isEmpty() ? nullptr : nodeFor(parentKey);

    if (node->m_parentNode) {
        if (node->m_parentNode->m_itemWidget)
            node->m_parentNode->item()->setKind(NavigationTreeItem::Kind::Category);
        node->m_parentNode->m_children.append(node);
    }

    const int depth = node->m_parentNode ? node->m_parentNode->m_itemWidget->nodeDepth() + 1 : 0;
    const QString actualTooltip = tooltip.isEmpty() ? text : tooltip;
    auto* item = new NavigationTreeItem(routeKey,
                                        iconGlyph,
                                        text,
                                        actualTooltip,
                                        NavigationTreeItem::Kind::Leaf,
                                        depth,
                                        selectable,
                                        nullptr,
                                        visualSource);
    item->setOrientation(m_orientation);
    item->setCompacted(m_isCompacted);
    item->setItemPosition(position);
    item->setTreeParent(node);
    node->m_itemWidget = item;

    // Unified slot: itemWidget emits a single signal, consolidating expansion, selection, and flyout decisions.
    connect(item, &NavigationTreeItem::itemClicked, node, [node](const QString& key, bool chevronClicked) {
        node->onItemClicked(key, chevronClicked);
    });

    // Forward expansion events: root coordinates indicator ownership side effects.
    connect(node, &NavigationTreeWidgetBase::expansionChanged, this, [this](const QString& key, bool expanded) {
        onExpansionChanged(key, expanded);
    });

    m_root->m_nodeIndex.insert(routeKey, node);

    if (node->m_parentNode) {
        // Ensure children container for parent category exists and is inserted immediately after the parent item.
        NavigationTreeWidget* parentNode = node->m_parentNode;
        if (!parentNode->m_childrenContainer) {
            auto* container = new QWidget(nullptr);
            container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            container->setAutoFillBackground(false);
            auto* childLayout = new QVBoxLayout(container);
            childLayout->setContentsMargins(0, 0, 0, 0);
            childLayout->setSpacing(0);
            parentNode->m_childrenContainer = container;
            parentNode->m_childrenLayout = childLayout;
            QBoxLayout* hostLayout = qobject_cast<QBoxLayout*>(parentNode->m_itemWidget->parentWidget()->layout());
            if (hostLayout) {
                const int idx = hostLayout->indexOf(parentNode->m_itemWidget) + 1;
                hostLayout->insertWidget(idx, container);
            }
            container->setVisible(parentNode->m_isExpanded);
        }
        parentNode->m_childrenLayout->addWidget(item);
    } else {
        addWidget(item, position);
        m_children.append(node);
    }
}

bool NavigationTreeWidget::removeItem(const QString& routeKey) {
    NavigationTreeWidget* node = nodeFor(routeKey);
    if (!node)
        return false;

    std::function<void(NavigationTreeWidget*)> detach = [&](NavigationTreeWidget* n) {
        for (NavigationTreeWidget* child : n->m_children)
            detach(child);
        n->m_children.clear();
        if (n->m_childrenContainer) {
            n->m_childrenContainer->deleteLater();
            n->m_childrenContainer = nullptr;
            n->m_childrenLayout = nullptr;
        }
        if (n->m_itemWidget)
            n->m_itemWidget->deleteLater();
        m_root->m_nodeIndex.remove(n->m_routeKey);
        n->deleteLater();
    };

    if (node->m_parentNode)
        node->m_parentNode->m_children.removeOne(node);
    else
        m_children.removeOne(node);

    detach(node);

    if (m_currentRouteKey == routeKey) {
        m_currentRouteKey.clear();
    }

    return true;
}

void NavigationTreeWidget::addSectionHeader(const QString& text) {
    addWidget(new NavigationSectionHeader(text, this));
}

void NavigationTreeWidget::addWidget(NavigationWidget* widget, NavigationItemPosition position) {
    if (!widget)
        return;

    widget->setOrientation(m_orientation);
    widget->setCompacted(m_isCompacted);
    widget->setItemPosition(position);

    if (auto* header = qobject_cast<NavigationSectionHeader*>(widget))
        m_headers.append(header);

    if (position == NavigationItemPosition::Bottom) {
        if (m_orientation == Qt::Vertical && !m_addingFooter) {
            m_addingFooter = true;
            auto* sep = new NavigationSeparator(this);
            sep->setOrientation(m_orientation);
            m_footerLayout->addWidget(sep);
        }
        m_footerLayout->addWidget(widget);
    } else {
        const int insertIdx = (m_overflowButton && m_mainLayout->indexOf(m_overflowButton) >= 0)
                                  ? m_mainLayout->indexOf(m_overflowButton)
                                  : m_mainLayout->count() - 1;

        // Constrain items in m_mainLayout from expanding horizontally.
        // Ensure excess space is absorbed by trailing stretch so the overflow button stays adjacent to the last item.
        QSizePolicy sp = widget->sizePolicy();
        sp.setHorizontalPolicy(m_orientation == Qt::Horizontal ? QSizePolicy::Fixed : QSizePolicy::Preferred);
        widget->setSizePolicy(sp);

        m_mainLayout->insertWidget(insertIdx, widget);
    }
}

bool NavigationTreeWidget::contains(const QString& routeKey) const {
    return m_root->m_nodeIndex.contains(routeKey);
}

bool NavigationTreeWidget::isRoot(const QString& routeKey) const {
    const NavigationTreeWidget* node = nodeFor(routeKey);
    return node && node->m_parentNode == nullptr;
}

bool NavigationTreeWidget::isLeaf(const QString& routeKey) const {
    const NavigationTreeWidget* node = nodeFor(routeKey);
    return node && node->m_children.isEmpty();
}

void NavigationTreeWidget::collapseCategoryChevron(const QString& categoryKey) {
    NavigationTreeWidget* node = nodeFor(categoryKey);
    if (!node || !node->m_itemWidget)
        return;
    node->item()->animateChevron(0.0f);
}

void NavigationTreeWidget::dismissCategory() {
    if (m_activeCategoryKey.isEmpty())
        return;
    const QString key = m_activeCategoryKey;
    m_activeCategoryKey.clear();
    collapseCategoryChevron(key);
    Q_EMIT categoryDeactivated(key);
}

void NavigationTreeWidget::updateOverflow() {
    computeOverflow(true);
}

void NavigationTreeWidget::activateCategory(const QString& categoryKey, QWidget* anchorWidget) {
    if (m_activeCategoryKey == categoryKey)
        return;
    // Deactivate previous category if one is active.
    if (!m_activeCategoryKey.isEmpty())
        dismissCategory();
    m_activeCategoryKey = categoryKey;
    Q_EMIT categoryActivated(categoryKey, anchorWidget);
}

bool NavigationTreeWidget::isAncestorOf(const QString& routeKey, const QString& ancestorKey) const {
    const NavigationTreeWidget* node = nodeFor(routeKey);
    const NavigationTreeWidget* ancestor = nodeFor(ancestorKey);
    if (!node || !ancestor || node == ancestor)
        return false;
    for (const NavigationTreeWidget* p = node->m_parentNode; p; p = p->m_parentNode) {
        if (p == ancestor)
            return true;
    }
    return false;
}

void NavigationTreeWidget::setCompacted(bool compacted) {
    if (m_isCompacted == compacted)
        return;
    m_isCompacted = compacted;

    std::function<void(NavigationTreeWidget*)> visit = [&](NavigationTreeWidget* node) {
        if (!node)
            return;
        if (node->m_itemWidget)
            node->m_itemWidget->setCompacted(compacted);
        for (NavigationTreeWidget* child : node->m_children)
            visit(child);
    };
    visit(m_root);

    for (NavigationSectionHeader* header : m_headers)
        if (header)
            header->setCompacted(compacted);

    if (compacted)
        collapseAllCategories();
    else
        restoreAllCategories();
}

void NavigationTreeWidget::setCurrentItem(const QString& routeKey, bool updateOverflow) {
    if (m_settingCurrentItem)
        return;
    m_settingCurrentItem = true;
    auto cleanup = qScopeGuard([this] { m_settingCurrentItem = false; });

    NavigationTreeWidget* node = nodeFor(routeKey);
    if (!node || !node->m_itemWidget)
        return;

    if (m_isCompacted || m_orientation == Qt::Horizontal) {
        NavigationTreeWidget* anchor = node;
        while (anchor->m_parentNode && anchor->m_parentNode != m_root) {
            anchor = anchor->m_parentNode;
        }
        if (!anchor->m_itemWidget)
            return;

        NavigationTreeWidget* oldAnchor = nullptr;
        if (NavigationTreeWidget* old = nodeFor(m_currentRouteKey); old && old->m_itemWidget) {
            old->m_itemWidget->setSelected(false);
            oldAnchor = old;
            while (oldAnchor->m_parentNode && oldAnchor->m_parentNode != m_root) {
                oldAnchor = oldAnchor->m_parentNode;
            }
        }

        // Acts as a depth-2 LRU cache (selected and pinned categories).
        if (oldAnchor && oldAnchor != anchor) {
            m_pinnedCategoryKey = oldAnchor->routeKey();
        }

        m_currentRouteKey = routeKey;
        node->m_itemWidget->setSelected(true);

        const bool wasInOverflow = m_overflowNodes.contains(anchor) || !anchor->m_itemWidget->isVisible();

        if (updateOverflow) {
            computeOverflow(false);
        }

        Q_EMIT itemSelected(routeKey);
        return;
    }

    if (m_orientation != Qt::Horizontal)
        expandAncestors(node);

    if (routeKey == m_currentRouteKey) {
        return;
    }

    if (NavigationTreeWidget* old = nodeFor(m_currentRouteKey); old && old->m_itemWidget)
        old->m_itemWidget->setSelected(false);
    m_currentRouteKey = routeKey;
    node->m_itemWidget->setSelected(true);

    Q_EMIT itemSelected(routeKey);
}

NavigationTreeItem* NavigationTreeWidget::getVisualProxyFor(NavigationTreeItem* logicalOwner) const {
    if (!logicalOwner)
        return nullptr;
    NavigationTreeWidget* node = const_cast<NavigationTreeWidget*>(this)->nodeFor(logicalOwner->routeKey());
    if (!node || !node->m_itemWidget)
        return nullptr;

    if (m_isCompacted || m_orientation == Qt::Horizontal) {
        // In vertical compact or horizontal mode, descendant items do not appear directly in the main bar.
        // Trace back to the top-level root node in the main bar.
        NavigationTreeWidget* rootNode = node;
        while (rootNode->m_parentNode) {
            rootNode = rootNode->m_parentNode;
        }
        if (rootNode && rootNode->m_itemWidget) {
            if (m_orientation == Qt::Horizontal && isCategoryActive(rootNode->routeKey())) {
                // If selected item is a child and category is active, indicator belongs to flyout.
                // If selected item is the top-level category itself, preserve indicator on the top-level item.
                if (node != rootNode) {
                    return nullptr;
                }
            }
            if (m_orientation == Qt::Horizontal) {
                if (!m_overflowNodes.contains(rootNode) && !rootNode->m_itemWidget->isHidden()) {
                    return rootNode->item();
                }
            } else {
                if (!rootNode->m_itemWidget->isHidden()) {
                    return rootNode->item();
                }
            }
        }
        return nullptr;
    }

    auto isReachable = [](const NavigationTreeWidget* n) -> bool {
        for (const NavigationTreeWidget* p = n->m_parentNode; p; p = p->m_parentNode) {
            if (p->isCategory() && !p->m_isExpanded)
                return false;
        }
        return true;
    };

    if (isReachable(node)) {
        return logicalOwner;
    }

    NavigationTreeWidget* proxy = node->m_parentNode;
    while (proxy && proxy->isCategory()) {
        if (isCategoryActive(proxy->routeKey())) {
            return nullptr;
        }
        if (isReachable(proxy)) {
            return proxy->item();
        }
        proxy = proxy->m_parentNode;
    }

    if (proxy && proxy->m_itemWidget && !isCategoryActive(proxy->routeKey())) {
        return proxy->item();
    }

    return nullptr;
}

NavigationTreeItem* NavigationTreeWidget::findFirstSelectableItem() const {
    std::function<NavigationTreeItem*(const NavigationTreeWidget*)> visit =
        [&](const NavigationTreeWidget* n) -> NavigationTreeItem* {
        if (!n)
            return nullptr;
        if (n->itemWidget() && n->itemWidget()->isSelectable()) {
            return qobject_cast<NavigationTreeItem*>(n->itemWidget());
        }
        for (const NavigationTreeWidget* child : n->children()) {
            if (auto* found = visit(child))
                return found;
        }
        return nullptr;
    };

    for (const NavigationTreeWidget* topNode : m_children) {
        if (auto* found = visit(topNode))
            return found;
    }
    return nullptr;
}

QRectF NavigationTreeWidget::indicatorRectInHost(NavigationWidget* item, QWidget* host) const {
    if (!item)
        return QRectF();
    const QRectF local = item->indicatorRect();
    const QPoint itemOriginInHost = item->mapTo(host ? host : this, QPoint(0, 0));
    return QRectF(QPointF(itemOriginInHost) + local.topLeft(), local.size());
}

QVector<NavigationWidget*> NavigationTreeWidget::visibleItems() const {
    QVector<NavigationWidget*> result;
    collectVisible(m_root, result);
    return result;
}

void NavigationTreeWidget::collectVisible(NavigationTreeWidget* node, QVector<NavigationWidget*>& out) const {
    if (!node)
        return;
    if (node->m_itemWidget && !node->m_itemWidget->isFooterItem())
        out.append(node->m_itemWidget);

    // Direct children of root are always visible; child categories depend on m_isExpanded.
    bool shouldTraverse = (node == m_root) || (node->isCategory() && node->m_isExpanded);

    if (shouldTraverse) {
        for (NavigationTreeWidget* child : node->m_children)
            collectVisible(child, out);
    }
}

void NavigationTreeWidget::moveFocusBy(int delta) {
    if (m_root && m_root != this) {
        m_root->moveFocusBy(delta);
        return;
    }

    const QVector<NavigationWidget*> items = visibleItems();
    if (items.isEmpty()) {
        return;
    }

    QWidget* focused = qApp->focusWidget();
    int idx = -1;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i) == focused || items.at(i)->isAncestorOf(focused)) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        for (int i = 0; i < items.size(); ++i) {
            if (auto* item = qobject_cast<NavigationTreeItem*>(items.at(i))) {
                if (item->routeKey() == m_currentRouteKey) {
                    idx = i;
                    break;
                }
            }
        }
        if (idx < 0) {
            idx = (delta > 0) ? -1 : items.size();
        }
    }

    int next = idx;
    for (int k = 0; k < items.size(); ++k) {
        next = (next + delta + items.size()) % items.size();
        NavigationWidget* target = items.at(next);

        if (target->focusPolicy() == Qt::NoFocus || !target->isVisibleTo(this) || !target->isEnabled()) {
            continue;
        }

        target->setFocus(Qt::ShortcutFocusReason);

        if (m_selectionFollowsFocus) {
            if (auto* item = qobject_cast<NavigationTreeItem*>(target)) {
                if (item->isSelectable() && !item->isCategory()) {
                    setCurrentItem(item->routeKey());
                }
            } else if (auto* btn = qobject_cast<NavigationToolButton*>(target)) {
                if (btn->isSelectable()) {
                    btn->click();
                }
            }
        }
        break;
    }
}

void NavigationTreeWidget::setSelectionFollowsFocus(bool follows) {
    if (m_selectionFollowsFocus == follows)
        return;
    m_selectionFollowsFocus = follows;
}

void NavigationTreeWidget::onExpansionChanged(const QString& routeKey, bool expanded) {
    Q_EMIT expansionChanged(routeKey, expanded);
}

void NavigationTreeWidget::onIndicatorOwnerChanged(NavigationTreeItem* item, bool isOwner) {
    if (!item)
        return;

    if (isOwner) {
        if (NavigationTreeItem* proxy = getVisualProxyFor(item)) {
            proxy->setShowIndicator(true);
        }
    } else {
        item->setShowIndicator(false);
    }
}

void NavigationTreeWidget::setCategoryExpanded(const QString& routeKey, bool expanded, bool animated) {
    if (m_isCompacted)
        return;
    NavigationTreeWidget* node = nodeFor(routeKey);
    if (!node || !node->isCategory())
        return;
    node->setExpanded(expanded, animated);
}

void NavigationTreeWidget::setRememberExpandState(const QString& routeKey, bool remember) {
    if (NavigationTreeWidget* node = nodeFor(routeKey))
        node->m_rememberExpandState = remember;
}

void NavigationTreeWidget::saveExpandState(const QString& routeKey) {
    NavigationTreeWidget* node = nodeFor(routeKey);
    if (node && node->m_rememberExpandState)
        node->m_savedExpanded = node->m_isExpanded;
}

void NavigationTreeWidget::restoreExpandState(const QString& routeKey, bool animated) {
    NavigationTreeWidget* node = nodeFor(routeKey);
    if (!node || !node->m_rememberExpandState || !node->isCategory())
        return;
    node->setExpanded(node->m_savedExpanded, animated);
}

void NavigationTreeWidget::propagateOrientation(Qt::Orientation orientation) {
    // Recursively propagate orientation to item widgets and toggle children container visibility and constraints.
    std::function<void(NavigationTreeWidget*)> visit = [&](NavigationTreeWidget* node) {
        if (!node)
            return;
        if (node->m_itemWidget) {
            node->m_itemWidget->setOrientation(orientation);
            QSizePolicy sp = node->m_itemWidget->sizePolicy();
            if (orientation == Qt::Vertical) {
                node->m_itemWidget->setVisible(true);
                sp.setHorizontalPolicy(QSizePolicy::Preferred);
            } else {
                sp.setHorizontalPolicy(QSizePolicy::Fixed);
            }
            node->m_itemWidget->setSizePolicy(sp);
        }
        if (node->m_childrenContainer) {
            if (orientation == Qt::Horizontal) {
                node->m_childrenContainer->hide();
                node->m_childrenContainer->setMaximumSize(0, 0);
            } else {
                node->m_childrenContainer->show();
                node->m_childrenContainer->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            }
        }
        for (NavigationTreeWidget* child : node->m_children)
            visit(child);
    };
    visit(m_root);

    for (NavigationSectionHeader* header : m_headers) {
        header->setOrientation(orientation);
        header->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        header->setVisible(true);

        QSizePolicy sp = header->sizePolicy();
        sp.setHorizontalPolicy(orientation == Qt::Vertical ? QSizePolicy::Preferred : QSizePolicy::Fixed);
        header->setSizePolicy(sp);
    }
}

void NavigationTreeWidget::collapseAllCategories() {
    // Capture expanded state snapshot and collapse containers without mutating m_isExpanded.
    // m_isExpanded is preserved as data source for flyout cloning.
    m_savedExpandStates.clear();
    std::function<void(NavigationTreeWidget*)> visit = [&](NavigationTreeWidget* node) {
        if (!node)
            return;
        if (node->isCategory() && node->m_childrenContainer) {
            m_savedExpandStates.insert(node->m_routeKey, node->m_isExpanded);
            node->m_childrenContainer->setFixedHeight(0);
            node->m_childrenContainer->hide();
            node->item()->setArrowAngle(0.0f);
            node->item()->setExpanded(false, /*animated=*/false);
        }
        for (NavigationTreeWidget* child : node->m_children)
            visit(child);
    };
    visit(m_root);
}

void NavigationTreeWidget::restoreAllCategories() {
    // Restore expanded states from snapshot for all categories.
    std::function<void(NavigationTreeWidget*)> visit = [&](NavigationTreeWidget* node) {
        if (!node)
            return;
        if (node->isCategory() && node->m_childrenContainer) {
            const bool expanded = m_savedExpandStates.value(node->m_routeKey, node->m_isExpanded);
            node->m_isExpanded = expanded;
            if (expanded) {
                node->m_childrenContainer->setMinimumHeight(0);
                node->m_childrenContainer->setMaximumHeight(QWIDGETSIZE_MAX);
                node->m_childrenContainer->show();
                node->item()->setExpanded(true, /*animated=*/false);
                node->item()->setArrowAngle(180.0f);
            }
        }
        for (NavigationTreeWidget* child : node->m_children)
            visit(child);
    };
    visit(m_root);
}

void NavigationTreeWidget::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
}

void NavigationTreeWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    QTimer::singleShot(0, this, [this]() {
        if (m_orientation == Qt::Horizontal)
            computeOverflow();
    });
}

void NavigationTreeWidget::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_orientation == Qt::Horizontal && event->size().width() != event->oldSize().width()) {
        computeOverflow();
    }
}

void NavigationTreeWidget::setOrientation(Qt::Orientation orientation) {
    if (m_orientation == orientation)
        return;
    m_orientation = orientation;

    const auto direction = (orientation == Qt::Horizontal) ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom;

    m_layout->setDirection(direction);
    m_mainLayout->setDirection(direction);
    m_footerLayout->setDirection(direction);

    if (m_scrollView) {
        if (orientation == Qt::Horizontal) {
            m_scrollView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_scrollView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_scrollView->setHorizontalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Hidden);
            m_scrollView->setVerticalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Hidden);
            m_scrollView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        } else {
            m_scrollView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            m_scrollView->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            m_scrollView->setHorizontalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Hidden);
            m_scrollView->setVerticalScrollBarVisibility(fluent::scrolling::ScrollView::ScrollBarVisibility::Visible);
            m_scrollView->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
        }
    }

    propagateOrientation(orientation);

    if (orientation == Qt::Horizontal) {
        computeOverflow();
    } else {
        if (m_overflowButton) {
            m_overflowButton->hide();
            m_overflowButton->setSelected(false);
        }
        for (NavigationTreeWidget* node : m_children)
            if (node->m_itemWidget)
                node->m_itemWidget->show();
        for (auto* header : m_headers)
            header->show();
    }

    update();
}

bool NavigationTreeWidget::computeOverflow(bool animated) {
    if (m_updatingOverflow || m_orientation != Qt::Horizontal)
        return false;
    m_updatingOverflow = true;

    const int footerW = m_footerLayout ? m_footerLayout->sizeHint().width() : 0;
    const int availableWidth = qMax(0, width() - footerW);

    // Overflow button has fixed size; width() reflects its uncompressible footprint.
    const int overflowBtnWidth = m_overflowButton ? m_overflowButton->width() : kTopBarItemHeight;

    // Revoke pinned category protection immediately when available width shrinks to favor natural item order.
    // Exception: do not revoke pinned state if it contains the active selection.
    if (m_lastTopAvailableWidth > 0 && availableWidth < m_lastTopAvailableWidth) {
        if (!m_pinnedCategoryKey.isEmpty()) {
            bool shouldClear = true;
            if (NavigationTreeWidget* pinnedNode = nodeFor(m_pinnedCategoryKey)) {
                if (isSelectedUnder(pinnedNode)) {
                    shouldClear = false;
                }
            }
            if (shouldClear) {
                m_pinnedCategoryKey.clear();
            }
        }
    }

    // Collect top-level items with hash lookups.
    struct Entry {
        QWidget* widget = nullptr;
        NavigationTreeWidget* node = nullptr;
        NavigationSectionHeader* header = nullptr;
        int width = 0;
    };

    QHash<QWidget*, NavigationTreeWidget*> nodeLookup;
    nodeLookup.reserve(m_children.size());
    for (NavigationTreeWidget* child : m_children)
        if (child->itemWidget())
            nodeLookup.insert(child->itemWidget(), child);

    QHash<QWidget*, NavigationSectionHeader*> headerLookup;
    headerLookup.reserve(m_headers.size());
    for (auto* h : m_headers)
        headerLookup.insert(h, h);

    QVector<Entry> entries;
    entries.reserve(m_mainLayout->count());
    for (int i = 0; i < m_mainLayout->count(); ++i) {
        QLayoutItem* li = m_mainLayout->itemAt(i);
        QWidget* w = li ? li->widget() : nullptr;
        if (!w || w == m_overflowButton)
            continue;
        NavigationTreeWidget* foundNode = nodeLookup.value(w, nullptr);
        NavigationSectionHeader* foundHeader = headerLookup.value(w, nullptr);
        if (foundNode || foundHeader)
            entries.append({w, foundNode, foundHeader, w->sizeHint().width()});
    }

    // Find index of top-level category containing the active selection.
    int selectedIdx = -1;
    for (int i = 0; i < entries.size(); ++i) {
        if (entries[i].node && isSelectedUnder(entries[i].node)) {
            selectedIdx = i;
            break;
        }
    }

    // Keep overflow button visible even when available width is zero or negative.
    if (availableWidth <= 0) {
        for (auto& e : entries)
            e.widget->setVisible(false);
        if (m_overflowButton) {
            m_overflowButton->show();
            m_overflowButton->setSelected(selectedIdx >= 0);
        }
        m_lastTopAvailableWidth = availableWidth;
        m_updatingOverflow = false;
        return true; // All items hidden; treat as layout change.
    }

    int totalItemsWidth = 0;
    for (const auto& e : entries)
        totalItemsWidth += e.width;

    m_overflowNodes.clear();
    m_overflowHeaders.clear();

    bool layoutChanged = false;

    if (totalItemsWidth <= availableWidth) {
        // All items fit: make everything visible and hide overflow button.
        for (const auto& e : entries) {
            if (!e.widget->isVisible()) {
                layoutChanged = true;
            }
            e.widget->setVisible(true);
        }
        if (m_overflowButton) {
            if (m_overflowButton->isVisible()) {
                layoutChanged = true;
            }
            m_overflowButton->hide();
            m_overflowButton->setSelected(false);
        }
    } else {
        // Insufficient space: truncate from right to left.
        // Overflow button has highest priority; items occupy remaining width.
        int remainingWidth = availableWidth - overflowBtnWidth;

        QVector<bool> isVisible(entries.size(), true);
        QVector<int> parentHeaderIdx(entries.size(), -1);
        QHash<int, QList<int>> headerChildren;

        int currentHeaderIdx = -1;
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].header) {
                currentHeaderIdx = i;
            } else if (entries[i].node) {
                parentHeaderIdx[i] = currentHeaderIdx;
                if (currentHeaderIdx != -1) {
                    headerChildren[currentHeaderIdx].append(i);
                }
            }
        }

        int pinIdx = -1;
        if (!m_pinnedCategoryKey.isEmpty()) {
            for (int i = 0; i < entries.size(); ++i) {
                if (entries[i].node && entries[i].node->routeKey() == m_pinnedCategoryKey) {
                    pinIdx = i;
                    break;
                }
            }
        }

        // Truncate from right to left.
        for (int i = entries.size() - 1; i >= 0 && totalItemsWidth > remainingWidth; --i) {
            if (!isVisible[i])
                continue; // Already hidden.

            if (i == selectedIdx || i == pinIdx) {
                continue; // Protect selected item and pinned category.
            }

            if (entries[i].node) {
                isVisible[i] = false;
                totalItemsWidth -= entries[i].width;

                int pHeader = parentHeaderIdx[i];
                if (pHeader != -1 && isVisible[pHeader]) {
                    bool anyChildVisible = false;
                    for (int child : headerChildren[pHeader]) {
                        if (isVisible[child]) {
                            anyChildVisible = true;
                            break;
                        }
                    }
                    // Hide header if all its children are hidden and header is neither selected nor pinned.
                    if (!anyChildVisible && pHeader != selectedIdx && pHeader != pinIdx) {
                        isVisible[pHeader] = false;
                        totalItemsWidth -= entries[pHeader].width;
                    }
                }
            } else if (entries[i].header) {
                // Check standalone header with no visible children.
                bool anyChildVisible = false;
                for (int child : headerChildren[i]) {
                    if (isVisible[child]) {
                        anyChildVisible = true;
                        break;
                    }
                }
                if (!anyChildVisible) {
                    isVisible[i] = false;
                    totalItemsWidth -= entries[i].width;
                }
            }
        }

        // Apply visibility to widgets and populate overflow list.
        for (int i = 0; i < entries.size(); ++i) {
            if (entries[i].widget->isVisible() != isVisible[i]) {
                layoutChanged = true;
            }
            entries[i].widget->setVisible(isVisible[i]);

            if (entries[i].node && !isVisible[i]) {
                m_overflowNodes.append(entries[i].node);
            }

            if (entries[i].header) {
                // Add header to overflow if header itself is hidden or any of its children overflowed.
                bool anyChildHidden = false;
                for (int child : headerChildren[i]) {
                    if (!isVisible[child]) {
                        anyChildHidden = true;
                        break;
                    }
                }
                if (!isVisible[i] || anyChildHidden) {
                    m_overflowHeaders.append(entries[i].header);
                }
            }
        }

        if (m_overflowButton) {
            if (!m_overflowButton->isVisible()) {
                layoutChanged = true;
            }
            m_overflowButton->show();
            // Highlight overflow button if selected item is inside overflow menu.
            const bool overflowSelected = (selectedIdx >= 0 && !isVisible[selectedIdx]);
            m_overflowButton->setSelected(overflowSelected);
        }
    }

    if (layoutChanged) {
        animated = false;
    }

    m_lastTopAvailableWidth = availableWidth;
    m_updatingOverflow = false;
    return layoutChanged;
}

bool NavigationTreeWidget::isSelectedUnder(NavigationTreeWidget* node) const {
    if (!node)
        return false;
    return node->routeKey() == m_currentRouteKey || isAncestorOf(m_currentRouteKey, node->routeKey());
}

QVector<NavigationOverflowEntry> NavigationTreeWidget::overflowEntries() const {
    // Build widget-to-entry lookup and scan layout in single pass.
    QHash<QWidget*, NavigationOverflowEntry> lookup;
    lookup.reserve(m_overflowNodes.size() + m_overflowHeaders.size());
    for (auto* node : m_overflowNodes)
        if (node->itemWidget())
            lookup.insert(node->itemWidget(), {node, nullptr});
    for (auto* h : m_overflowHeaders)
        lookup.insert(h, {nullptr, h});

    QVector<NavigationOverflowEntry> result;
    result.reserve(lookup.size());
    for (int i = 0; i < m_mainLayout->count(); ++i) {
        QWidget* w = m_mainLayout->itemAt(i) ? m_mainLayout->itemAt(i)->widget() : nullptr;
        if (!w || w == m_overflowButton)
            continue;
        auto it = lookup.find(w);
        if (it != lookup.end())
            result.append(*it);
    }
    return result;
}

void NavigationTreeWidget::onThemeUpdated() {
    applyTransparentScrollView();
    update();
}

void NavigationTreeWidget::applyTransparentScrollView() {
    if (!m_scrollView)
        return;
    m_scrollView->setAutoFillBackground(false);
    QWidget* area = m_scrollView->viewport();
    if (!area)
        return;

    area->setAutoFillBackground(false);
    area->setAttribute(Qt::WA_TranslucentBackground, false);
    area->setAttribute(Qt::WA_OpaquePaintEvent, false);
    QPalette palette = area->palette();
    palette.setColor(QPalette::Window, Qt::transparent);
    palette.setColor(QPalette::Base, Qt::transparent);
    area->setPalette(palette);
    area->update();
}
} // namespace manga::ui::navigation
