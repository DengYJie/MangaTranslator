#include "ui/navigation/NavigationFlyout.h"

#include "components/foundation/overlay/OverlayShadow.h"
#include "ui/navigation/NavigationPushButton.h"
#include "ui/navigation/NavigationSectionHeader.h"
#include "ui/navigation/NavigationTreeWidget.h"
#include "ui/navigation/NavigationWidget.h"

#include <FluentQt/Design.h>
#include <QAccessible>
#include <QApplication>
#include <QBoxLayout>
#include <QEasingCurve>
#include <QEvent>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHash>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <functional>

namespace manga::ui::navigation {
namespace {
QPixmap generateGrainTile(qreal devicePixelRatio) {
    static QHash<int, QPixmap> s_grainCache;
    const int dprInt = qMax(1, qRound(devicePixelRatio * 100));

    if (s_grainCache.contains(dprInt)) {
        return s_grainCache.value(dprInt);
    }

    const int size = qMax(1, qRound(96 * devicePixelRatio));
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied);

    quint32 state = 0x8f3d7a21U ^ static_cast<quint32>(dprInt);
    for (int y = 0; y < size; ++y) {
        auto* scanLine = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < size; ++x) {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            const int value = static_cast<int>((state >> 24U) & 0xffU);
            scanLine[x] = qRgb(value, value, value);
        }
    }

    QPixmap texture = QPixmap::fromImage(image);
    texture.setDevicePixelRatio(devicePixelRatio);
    s_grainCache.insert(dprInt, texture);
    return texture;
}

void paintFlyoutGrain(QPainter& painter, const QRect& rect, qreal opacity = 0.030) {
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setOpacity(opacity);

    qreal dpr = 1.0;
    if (painter.device()) {
        dpr = painter.device()->devicePixelRatioF();
    }

    painter.drawTiledPixmap(rect, generateGrainTile(dpr), QPoint(0, 0));
    painter.restore();
}
} // namespace

NavigationFlyout::NavigationFlyout(NavigationTreeWidget* rootTree, QWidget* host)
    : NavigationTreeWidgetBase(rootTree, host), m_host(host) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAutoFillBackground(false);

    ::fluent::overlay::syncInheritedThemeOverride(this, host);

    const int cardPadding = themeSpacing().xSmall;
    m_contentLayout = new QBoxLayout(QBoxLayout::TopToBottom, this);
    m_contentLayout->setContentsMargins(kShadowMargin + cardPadding,
                                        kShadowMargin + cardPadding,
                                        kShadowMargin + cardPadding,
                                        kShadowMargin + cardPadding);
    m_contentLayout->setSpacing(2);

    if (m_host)
        m_host->installEventFilter(this);

    m_flyoutIndicator = new NavigationIndicator(this);
    m_flyoutIndicator->setOrientation(Qt::Vertical);

    if (rootTree) {
        connect(rootTree, &NavigationTreeWidget::itemSelected, this, [this](const QString& selectedKey) {
            for (auto it = m_itemIndex.begin(); it != m_itemIndex.end(); ++it) {
                NavigationTreeItem* item = it.value();
                if (!item)
                    continue;
                const bool selected = (item->routeKey() == selectedKey ||
                                       (root() && root()->isAncestorOf(selectedKey, item->routeKey())));
                item->setSelected(selected);
            }
        });
    }
}

NavigationFlyout::~NavigationFlyout() {
    if (m_host)
        m_host->removeEventFilter(this);
}

void NavigationFlyout::rebuildSubtree(const QString& categoryKey) {
    m_itemIndex.clear();
    NavigationTreeWidget* catNode = root()->nodeFor(categoryKey);
    if (!catNode)
        return;
    for (NavigationTreeWidget* child : catNode->children()) {
        if (child && child->itemWidget())
            cloneNode(child, nullptr, 0);
    }
    finalizeSize();
}

void NavigationFlyout::rebuildSubtreeFromEntries(const QVector<NavigationOverflowEntry>& entries) {
    m_itemIndex.clear();
    for (const auto& entry : entries) {
        if (entry.header) {
            auto* header = new NavigationSectionHeader(entry.header->text(), nullptr);
            header->setOrientation(Qt::Vertical);
            header->setCompacted(false);
            m_contentLayout->addWidget(header);
        } else if (entry.node && entry.node->itemWidget()) {
            cloneNode(entry.node, nullptr, 0);
        }
    }
    finalizeSize();
}

void NavigationFlyout::cloneNode(NavigationTreeWidget* srcNode, NavigationTreeWidget* parentClone, int depth) {
    auto* node = new NavigationTreeWidget(root());
    node->setParent(this);
    node->m_routeKey = srcNode->routeKey();
    node->m_parentNode = parentClone;
    node->setInlineExpansion(true);

    connect(node,
            &NavigationTreeWidgetBase::expansionChanged,
            this,
            [this, root = root()](const QString& key, bool expanded) {
                if (auto* target = root->nodeFor(key))
                    target->m_isExpanded = expanded;
                Q_EMIT expansionChanged(key, expanded);
            });

    const auto* srcItem = srcNode->item();
    auto* item = new NavigationTreeItem(srcNode->routeKey(),
                                        srcItem->iconGlyph(),
                                        srcItem->text(),
                                        srcItem->tooltipText(),
                                        srcNode->isCategory() ? NavigationTreeItem::Kind::Category
                                                              : NavigationTreeItem::Kind::Leaf,
                                        depth,
                                        srcItem->isSelectable(),
                                        nullptr);
    item->setOrientation(Qt::Vertical);
    item->setCompacted(false);
    item->setIconSize(srcItem->iconSize());
    item->setTreeParent(node);
    node->m_itemWidget = item;
    m_itemIndex.insert(item->routeKey(), item);

    connect(item, &NavigationTreeItem::itemClicked, node, [node, this](const QString& key, bool chevronClicked) {
        node->onItemClicked(key, chevronClicked);
        if (!node->isCategory()) {
            close();
        }
    });

    if (parentClone) {
        parentClone->m_children.append(node);
        if (!parentClone->m_childrenContainer) {
            auto* container = new QWidget(nullptr);
            container->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            container->setAutoFillBackground(false);
            auto* childLayout = new QVBoxLayout(container);
            childLayout->setContentsMargins(0, 0, 0, 0);
            childLayout->setSpacing(2);
            parentClone->m_childrenContainer = container;
            parentClone->m_childrenLayout = childLayout;

            QBoxLayout* hostLayout =
                parentClone->m_itemWidget && parentClone->m_itemWidget->parentWidget()
                    ? qobject_cast<QBoxLayout*>(parentClone->m_itemWidget->parentWidget()->layout())
                    : m_contentLayout;

            if (hostLayout) {
                const int idx = hostLayout->indexOf(parentClone->m_itemWidget) + 1;
                hostLayout->insertWidget(idx, container);
            } else {
                m_contentLayout->addWidget(container);
            }
            container->setVisible(parentClone->m_isExpanded);
        }
        parentClone->m_childrenLayout->addWidget(item);
    } else {
        m_contentLayout->addWidget(item);
        m_children.append(node);
    }

    const QString currentKey = root()->currentRouteKey();
    if (srcNode->routeKey() == currentKey) {
        item->setSelected(true);
    } else if (root()->isAncestorOf(currentKey, srcNode->routeKey())) {
        item->setSelected(true);
    }

    for (NavigationTreeWidget* child : srcNode->children())
        cloneNode(child, node, depth + 1);

    if (node->isCategory() && srcNode->m_isExpanded)
        node->setExpanded(true, false);
}

void NavigationFlyout::playSelectedItemCrossPortal(NavigationTreeItem* selectedItem,
                                                   const QRectF& mappedStartRect,
                                                   const QRectF& targetRect) {
    if (!m_flyoutIndicator || !selectedItem)
        return;

    if (NavigationWidget::isReducedMotion()) {
        return;
    }

    m_flyoutIndicator->show();
    m_flyoutIndicator->playCrossWindowPortal(mappedStartRect, targetRect);
}

NavigationTreeItem* NavigationFlyout::getVisualProxyFor(NavigationTreeItem* item) const {
    if (!item)
        return nullptr;
    NavigationTreeItem* clone = m_itemIndex.value(item->routeKey(), item);
    NavigationTreeWidget* node = clone ? clone->treeParent() : nullptr;
    if (!node)
        return clone;

    auto isReachable = [](const NavigationTreeWidget* n) -> bool {
        for (const NavigationTreeWidget* p = n->m_parentNode; p; p = p->m_parentNode) {
            if (p->isCategory() && !p->m_isExpanded)
                return false;
        }
        return true;
    };

    if (isReachable(node)) {
        return qobject_cast<NavigationTreeItem*>(node->itemWidget());
    }

    NavigationTreeWidget* proxy = node->m_parentNode;
    while (proxy && proxy->isCategory()) {
        if (isReachable(proxy)) {
            return qobject_cast<NavigationTreeItem*>(proxy->itemWidget());
        }
        proxy = proxy->m_parentNode;
    }

    if (proxy && proxy->itemWidget()) {
        return qobject_cast<NavigationTreeItem*>(proxy->itemWidget());
    }

    return clone;
}

QRectF NavigationFlyout::indicatorRectInHost(NavigationTreeItem* item) const {
    if (!item || !m_flyoutIndicator)
        return QRectF();

    NavigationTreeItem* visualItem = getVisualProxyFor(item);
    if (!visualItem)
        return QRectF();

    const QWidget* host = m_flyoutIndicator->parentWidget();
    const QPoint itemOriginInHost = visualItem->mapTo(host ? host : this, QPoint(0, 0));
    return QRectF(QPointF(itemOriginInHost) + visualItem->indicatorRect().topLeft(),
                  visualItem->indicatorRect().size());
}

void NavigationFlyout::playInternalFlight(const QRectF& targetRect) {
    if (!m_flyoutIndicator)
        return;
    m_flyoutIndicator->show();
    m_flyoutIndicator->activateAt(targetRect, true);
}

void NavigationFlyout::onIndicatorOwnerChanged(NavigationTreeItem* item, bool isOwner) {
    if (m_isClosing)
        return;
    if (!item)
        return;

    NavigationTreeItem* clone = m_itemIndex.value(item->routeKey());
    if (!clone)
        return;

    if (isOwner) {
        NavigationTreeItem* visualItem = getVisualProxyFor(clone);
        if (visualItem) {
            visualItem->setShowIndicator(true);
        }
    } else {
        clone->setShowIndicator(false);
        NavigationTreeWidget* node = clone->treeParent();
        while (node && node->m_parentNode) {
            node = node->m_parentNode;
            if (node->m_itemWidget) {
                if (auto* proxyItem = qobject_cast<NavigationTreeItem*>(node->itemWidget())) {
                    proxyItem->setShowIndicator(false);
                }
            }
        }
    }
}

void NavigationFlyout::finalizeSize() {
    const QFontMetrics fm(themeFont(Typography::FontRole::Body).toQFont());
    const int leftPadding = kTextLeftOffset;
    const int rightPadding = leftPadding + themeSpacing().medium;

    int maxContentW = 0;
    std::function<void(const NavigationTreeWidget*)> visit = [&](const NavigationTreeWidget* n) {
        if (!n || !n->itemWidget())
            return;
        const auto* btn = qobject_cast<const NavigationPushButton*>(n->itemWidget());
        const int indent = n->itemWidget()->nodeDepth() * themeSpacing().large;
        const int w = leftPadding + indent + (btn ? fm.horizontalAdvance(btn->text()) : 0) + rightPadding;
        maxContentW = qMax(maxContentW, w);
        for (auto* c : n->children())
            visit(c);
    };
    for (auto* top : m_children)
        visit(top);

    maxContentW = qMax(maxContentW, kCompactFlyoutRowWidth);

    const int cardPadding = themeSpacing().xSmall;
    const int outerW = maxContentW + 2 * (kShadowMargin + cardPadding);
    setMinimumWidth(outerW);
    setMaximumWidth(outerW);
    if (m_contentLayout)
        m_contentLayout->activate();
    adjustSize();
}

void NavigationFlyout::setIsOpen(bool open) {
    if (m_isOpen == open)
        return;
    m_isOpen = open;
    if (open) {
        this->open();
    } else {
        close();
    }
}

void NavigationFlyout::showAt(QWidget* anchor) {
    setAnchor(anchor);
    if (!anchor || !m_host)
        return;

    const QRect anchorRect(anchor->mapToGlobal(QPoint(0, 0)), anchor->size());
    const QSize cardSize = ::fluent::overlay::visibleCardRect(rect(), kShadowMargin).size();

    QPoint globalCardTopLeft;
    QPoint slideInOffset;

    if (m_placement == Placement::Right) {
        const int panelRightX = m_host->mapToGlobal(QPoint(m_host->width(), 0)).x();
        const int anchorCenterY = anchorRect.y() + anchorRect.height() / 2;
        const int yPos = anchorCenterY - cardSize.height() / 2;
        globalCardTopLeft = QPoint(panelRightX + m_anchorOffset, yPos);
        slideInOffset = QPoint(8, 0);
    } else {
        const int panelBottomY = m_host->mapToGlobal(QPoint(0, m_host->height())).y();
        int flyoutX = 0;
        if (m_placement == Placement::BottomRight) {
            const int anchorRightX = anchorRect.right();
            flyoutX = anchorRightX - cardSize.width() + 1;
        } else {
            const int anchorCenterX = anchorRect.x() + anchorRect.width() / 2;
            flyoutX = anchorCenterX - cardSize.width() / 2;
        }

        globalCardTopLeft = QPoint(flyoutX, panelBottomY + m_anchorOffset);
        slideInOffset = QPoint(0, 16);
    }

    openAt(globalCardTopLeft, slideInOffset);
}

void NavigationFlyout::open() {
    if (m_anchorWidget) {
        showAt(m_anchorWidget);
    } else {
        openAt(m_globalCardPos, m_slideInOffset);
    }
}

void NavigationFlyout::openAt(const QPoint& globalCardTopLeft, const QPoint& slideInOffset) {
    if (!m_isOpen) {
        m_isOpen = true;
        Q_EMIT aboutToShow();
    }

    m_globalCardPos = globalCardTopLeft;
    m_slideInOffset = slideInOffset;

    const QSize cardSize = ::fluent::overlay::visibleCardRect(rect(), kShadowMargin).size();
    QPoint cardPos = m_globalCardPos;

    QScreen* screen = QGuiApplication::screenAt(m_globalCardPos);
    if (!screen)
        screen = QGuiApplication::primaryScreen();
    m_flippedUp = false;
    if (screen) {
        const QRect avail = screen->availableGeometry();
        if (cardPos.y() + cardSize.height() > avail.bottom()) {
            cardPos.setY(avail.bottom() - cardSize.height() + 1);
            if (cardPos.y() < avail.top())
                cardPos.setY(avail.top());
            m_flippedUp = true;
        }
        cardPos = ::fluent::overlay::clampCardTopLeft(cardPos, cardSize, avail, 0);
    }

    const QPoint outerTopLeft = cardPos - QPoint(kShadowMargin, kShadowMargin);
    m_hostAnchorOffset = cardPos - m_host->mapToGlobal(QPoint(0, 0));

    const QPoint effectiveOffset = m_flippedUp ? QPoint(m_slideInOffset.x(), -m_slideInOffset.y()) : m_slideInOffset;

    if (effectiveOffset.isNull() || NavigationWidget::isReducedMotion()) {
        move(outerTopLeft);
        setWindowOpacity(1.0);
        show();
        Q_EMIT opened();
    } else {
        const QPoint endPos = outerTopLeft;
        const QPoint startPos = endPos - effectiveOffset;
        if (!m_animGroup) {
            m_animGroup = new QParallelAnimationGroup(this);

            m_slideAnim = new QPropertyAnimation(this, "pos", this);
            m_slideAnim->setEasingCurve(themeAnimation().decelerate);
            m_slideAnim->setDuration(themeAnimation().normal);

            m_fadeAnim = new QPropertyAnimation(this, "windowOpacity", this);
            m_fadeAnim->setEasingCurve(QEasingCurve::Linear);
            m_fadeAnim->setDuration(themeAnimation().normal);

            m_animGroup->addAnimation(m_slideAnim);
            m_animGroup->addAnimation(m_fadeAnim);

            connect(m_animGroup, &QParallelAnimationGroup::finished, this, [this]() { Q_EMIT opened(); });
        }
        m_animGroup->stop();
        move(startPos);
        setWindowOpacity(0.0);
        show();

        m_slideAnim->setStartValue(startPos);
        m_slideAnim->setEndValue(endPos);

        m_fadeAnim->setStartValue(0.0);
        m_fadeAnim->setEndValue(1.0);

        m_animGroup->start();
    }

    qApp->installEventFilter(this);
    raise();
}

void NavigationFlyout::close() {
    if (m_isClosing)
        return;
    m_isClosing = true;

    Q_EMIT aboutToHide();

    if (m_isOpen) {
        m_isOpen = false;
    }

    if (m_anchorWidget) {
        if (NavigationWidget::isKeyboardMode()) {
            m_anchorWidget->setFocus(Qt::ShortcutFocusReason);
        }
    }

    if (!m_exitAnimationEnabled || NavigationWidget::isReducedMotion()) {
        hide();
        Q_EMIT closed();
        this->deleteLater();
        return;
    }

    if (m_animGroup) {
        m_animGroup->stop();
    }

    QParallelAnimationGroup* closeGroup = new QParallelAnimationGroup(this);
    const auto& animTokens = themeAnimation();

    QPropertyAnimation* fadeOut = new QPropertyAnimation(this, "windowOpacity", closeGroup);
    fadeOut->setStartValue(windowOpacity());
    fadeOut->setEndValue(0.0);
    fadeOut->setDuration(animTokens.fast);
    fadeOut->setEasingCurve(animTokens.exit);

    QPropertyAnimation* slideOut = new QPropertyAnimation(this, "pos", closeGroup);
    slideOut->setStartValue(pos());

    QPoint effectiveOffset = m_flippedUp ? QPoint(m_slideInOffset.x(), -m_slideInOffset.y()) : m_slideInOffset;

    QPoint exitOffset(0, -8);
    if (effectiveOffset.y() > 0)
        exitOffset = QPoint(0, -8);
    else if (effectiveOffset.y() < 0)
        exitOffset = QPoint(0, 8);
    else if (effectiveOffset.x() > 0)
        exitOffset = QPoint(-8, 0);
    else if (effectiveOffset.x() < 0)
        exitOffset = QPoint(8, 0);

    slideOut->setEndValue(pos() + exitOffset);
    slideOut->setDuration(animTokens.fast);
    slideOut->setEasingCurve(animTokens.exit);

    closeGroup->addAnimation(fadeOut);
    closeGroup->addAnimation(slideOut);

    connect(closeGroup, &QParallelAnimationGroup::finished, this, [this, closeGroup]() {
        hide();
        Q_EMIT closed();
        closeGroup->deleteLater();
        this->deleteLater();
    });

    closeGroup->start();
}

bool NavigationFlyout::eventFilter(QObject* watched, QEvent* event) {
    QWidget* topLevel = m_host ? m_host->window() : nullptr;

    if (watched == m_host || watched == m_anchorWidget || (topLevel && watched == topLevel)) {
        switch (event->type()) {
            case QEvent::Move:
                if (isVisible()) {
                    const QPoint hostTopLeft = m_host->mapToGlobal(QPoint(0, 0));
                    move(hostTopLeft + m_hostAnchorOffset - QPoint(kShadowMargin, kShadowMargin));
                }
                break;
            case QEvent::Resize:
                if (isVisible()) {
                    close();
                }
                break;
            case QEvent::WindowDeactivate:
                if (watched == topLevel) {
                    if (qApp->activeWindow() != this && !this->isAncestorOf(qApp->focusWidget())) {
                        close();
                    }
                }
                break;
            case QEvent::Hide:
            case QEvent::Destroy:
                close();
                break;
            default:
                break;
        }
    }

    if (isVisible()) {
        if (event->type() == QEvent::ApplicationDeactivate) {
            close();
        } else if (event->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape && (m_closePolicy & CloseOnEscape)) {
                close();
                return true;
            }
        }
    }

    if (isVisible() && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::Wheel)) {
        QPoint globalPos;
        if (event->type() == QEvent::MouseButtonPress) {
            globalPos = static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        } else {
            globalPos = static_cast<QWheelEvent*>(event)->globalPosition().toPoint();
        }

        const QRect visibleCard = ::fluent::overlay::visibleCardRect(rect(), kShadowMargin);
        const QPoint localPos = mapFromGlobal(globalPos);
        const bool insideCard = visibleCard.contains(localPos);

        if (!insideCard) {
            if (event->type() == QEvent::Wheel) {
                close();
                return false;
            }

            QWidget* hitWidget = QApplication::widgetAt(globalPos);
            auto isHit = [&](QWidget* target) {
                if (!target)
                    return false;
                if (hitWidget && (hitWidget == target || target->isAncestorOf(hitWidget)))
                    return true;
                return target->rect().contains(target->mapFromGlobal(globalPos));
            };

            if (isHit(m_anchorWidget)) {
                close();
                return true;
            }

            for (const auto& ptWidget : std::as_const(m_lightDismissPassthrough)) {
                if (isHit(ptWidget)) {
                    close();
                    return false;
                }
            }

            if (m_closePolicy & CloseOnPressOutside) {
                close();
                return m_lightDismissConsumesPress;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

bool NavigationFlyout::event(QEvent* e) {
    if (e->type() == QEvent::LayoutRequest)
        adjustSize();
    return NavigationTreeWidgetBase::event(e);
}

void NavigationFlyout::showEvent(QShowEvent* event) {
    NavigationTreeWidgetBase::showEvent(event);

    QAccessibleEvent popupStartEvent(this, QAccessible::PopupMenuStart);
    QAccessible::updateAccessibility(&popupStartEvent);

    QTimer::singleShot(0, this, [this]() {
        if (!m_children.isEmpty()) {
            if (NavigationWidget::isKeyboardMode()) {
                if (auto* firstChild = m_children.first()->m_itemWidget) {
                    firstChild->setFocus(Qt::ShortcutFocusReason);
                }
            }
        }
    });
}

void NavigationFlyout::hideEvent(QHideEvent* event) {
    qApp->removeEventFilter(this);
    QWidget::hideEvent(event);

    QAccessibleEvent popupEndEvent(this, QAccessible::PopupMenuEnd);
    QAccessible::updateAccessibility(&popupEndEvent);

    if (m_isOpen) {
        Q_EMIT aboutToHide();
        m_isOpen = false;
    }
    if (m_anchorWidget) {
        if (NavigationWidget::isKeyboardMode()) {
            m_anchorWidget->setFocus(Qt::ShortcutFocusReason);
        }
    }
    Q_EMIT closed();
}

void NavigationFlyout::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && (m_closePolicy & CloseOnEscape)) {
        close();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        moveFocusBy(event->key() == Qt::Key_Up ? -1 : 1);
        event->accept();
        return;
    }
    NavigationTreeWidgetBase::keyPressEvent(event);
}

void NavigationFlyout::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QRect cardRect = ::fluent::overlay::visibleCardRect(rect(), kShadowMargin);
    const int r = themeRadius().overlay;

    ::fluent::overlay::paintLayeredShadow(painter, cardRect, r, themeShadow(Elevation::Medium));

    const auto& colors = themeColorsRef();

    QPainterPath clipPath;
    clipPath.addRoundedRect(QRectF(cardRect).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
    painter.save();
    painter.setClipPath(clipPath);

    const bool isDark = effectiveTheme() == Dark;
    const auto acrylicToken = ::Material::Acrylic::get(isDark);

    // Acrylic Layer 1: Base Background
    painter.fillRect(cardRect, colors.bgLayer);

    // Acrylic Layer 2: Exclusion Blend / Luminosity
    painter.setCompositionMode(QPainter::CompositionMode_Exclusion);
    QColor luminosityColor = Qt::white;
    luminosityColor.setAlphaF(acrylicToken.luminosityOpacity * 0.25);
    painter.fillRect(cardRect, luminosityColor);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Acrylic Layer 3: Tint Overlay
    QColor tintColor = acrylicToken.tintColor;
    tintColor.setAlphaF(0.80);
    painter.fillRect(cardRect, tintColor);

    // Acrylic Layer 4: Noise Grain
    paintFlyoutGrain(painter, cardRect, isDark ? 0.04 : 0.03);

    painter.restore();

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(colors.strokeSurface, 1.0));
    painter.drawRoundedRect(QRectF(cardRect).adjusted(0.5, 0.5, -0.5, -0.5), r, r);
}

QVector<NavigationWidget*> NavigationFlyout::visibleItems() const {
    QVector<NavigationWidget*> result;
    for (NavigationTreeWidget* child : m_children)
        collectVisible(child, result);
    return result;
}

void NavigationFlyout::collectVisible(NavigationTreeWidget* node, QVector<NavigationWidget*>& out) const {
    if (!node)
        return;
    if (node->itemWidget())
        out.append(node->itemWidget());
    if (node->isCategory() && node->m_isExpanded) {
        for (NavigationTreeWidget* child : node->children())
            collectVisible(child, out);
    }
}

void NavigationFlyout::moveFocusBy(int delta) {
    const QVector<NavigationWidget*> items = visibleItems();
    if (items.isEmpty())
        return;

    QWidget* focused = qApp->focusWidget();
    int idx = -1;
    for (int i = 0; i < items.size(); ++i) {
        if (items.at(i) == focused || items.at(i)->isAncestorOf(focused)) {
            idx = i;
            break;
        }
    }

    if (idx < 0) {
        idx = (delta > 0) ? -1 : items.size();
    }

    int next = idx;
    for (int k = 0; k < items.size(); ++k) {
        next = (next + delta + items.size()) % items.size();
        NavigationWidget* target = items.at(next);

        if (target->focusPolicy() == Qt::NoFocus || !target->isVisibleTo(this) || !target->isEnabled()) {
            continue;
        }

        target->setFocus(Qt::ShortcutFocusReason);
        break;
    }
}
} // namespace manga::ui::navigation
