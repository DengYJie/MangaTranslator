#include "ui/navigation/NavigationTreeWidgetBase.h"

#include "ui/navigation/NavigationTreeItem.h"
#include "ui/navigation/NavigationTreeWidget.h"
#include "ui/navigation/NavigationWidget.h"

#include <FluentQt/Design.h>
#include <QEasingCurve>
#include <QVBoxLayout>
#include <QVariantAnimation>

namespace manga::ui::navigation {
NavigationTreeWidgetBase::NavigationTreeWidgetBase(NavigationTreeWidget* root, QWidget* parent)
    : QWidget(parent), m_root(root) {}

NavigationTreeWidgetBase::~NavigationTreeWidgetBase() = default;

NavigationTreeWidget* NavigationTreeWidgetBase::parentNode() const {
    return m_parentNode.data();
}

NavigationTreeWidget* NavigationTreeWidgetBase::root() {
    return m_root.data();
}

const NavigationTreeWidget* NavigationTreeWidgetBase::root() const {
    return m_root.data();
}

NavigationTreeItem* NavigationTreeWidgetBase::item() {
    return static_cast<NavigationTreeItem*>(m_itemWidget);
}

const NavigationTreeItem* NavigationTreeWidgetBase::item() const {
    return static_cast<const NavigationTreeItem*>(m_itemWidget);
}

void NavigationTreeWidgetBase::animateChildrenContainer(bool expanded) {
    if (!m_childrenContainer)
        return;

    if (NavigationWidget::isReducedMotion()) {
        m_childrenContainer->setVisible(expanded);
        return;
    }

    QWidget* container = m_childrenContainer;
    if (!m_heightAnimation) {
        m_heightAnimation = new QVariantAnimation(this);
        connect(m_heightAnimation, &QVariantAnimation::valueChanged, this, [container](const QVariant& v) {
            container->setFixedHeight(v.toInt());
        });
    }

    auto* anim = m_heightAnimation;
    anim->stop();
    anim->disconnect(SIGNAL(finished()));

    anim->setDuration(themeAnimation().fast);
    anim->setEasingCurve(themeAnimation().decelerate);

    if (expanded) {
        const int target =
            container->layout() ? container->layout()->sizeHint().height() : container->sizeHint().height();
        const int current = container->isVisible() ? container->height() : 0;

        container->show();
        anim->setStartValue(current);
        anim->setEndValue(target);
        connect(
            anim,
            &QVariantAnimation::finished,
            this,
            [this, container]() {
                container->setMinimumHeight(0);
                container->setMaximumHeight(QWIDGETSIZE_MAX);
                Q_EMIT expansionChanged(m_routeKey, true);
            },
            Qt::SingleShotConnection);
        anim->start();
    } else {
        const int current = container->height();

        anim->setStartValue(current);
        anim->setEndValue(0);
        connect(
            anim,
            &QVariantAnimation::finished,
            this,
            [container]() {
                container->setFixedHeight(0);
                container->hide();
                container->setMinimumHeight(0);
                container->setMaximumHeight(QWIDGETSIZE_MAX);
            },
            Qt::SingleShotConnection);
        anim->start();

        // Emitted immediately on collapse: the parent category geometry takes
        // effect at once, and the indicator slides to the parent category
        // stand-in concurrently.
        Q_EMIT expansionChanged(m_routeKey, false);
    }
}

void NavigationTreeWidgetBase::setExpanded(bool expanded, bool animated) {
    if (!isCategory() || m_isExpanded == expanded)
        return;
    m_isExpanded = expanded;

    item()->setExpanded(expanded, animated);

    if (m_childrenContainer) {
        if (!animated || NavigationWidget::isReducedMotion()) {
            m_childrenContainer->setVisible(expanded);
            Q_EMIT expansionChanged(m_routeKey, expanded);
        } else {
            animateChildrenContainer(expanded);
        }
    } else {
        Q_EMIT expansionChanged(m_routeKey, expanded);
    }
}

void NavigationTreeWidgetBase::expandAncestors(NavigationTreeWidget* node) {
    QVector<NavigationTreeWidget*> chain;
    for (NavigationTreeWidget* p = node->m_parentNode; p; p = p->m_parentNode)
        chain.prepend(p);
    for (NavigationTreeWidget* a : chain)
        a->setExpanded(true, false);
}

void NavigationTreeWidgetBase::onItemClicked(const QString& routeKey, bool chevronClicked) {
    auto* rootNode = root();

    // Leaf item: both body and chevron clicks only select and switch the page.
    if (!isCategory()) {
        rootNode->setCurrentItem(routeKey);
        return;
    }

    // Cloned category node inside a flyout: always expands inline and never
    // participates in flyout mode (otherwise it would open a second flyout).
    const auto* selfTree = qobject_cast<const NavigationTreeWidget*>(this);
    const bool inlineExpansion = selfTree && selfTree->inlineExpansion();

    const bool flyoutMode = !inlineExpansion && (rootNode->isCompacted() || rootNode->orientation() == Qt::Horizontal);

    if (flyoutMode) {
        // Compact/Top: the category renders its children through a flyout; a
        // chevron click toggles between active and inactive.
        if (chevronClicked && rootNode->isCategoryActive(routeKey)) {
            // Already active -> deactivate (close the flyout).
            rootNode->dismissCategory();
        } else {
            // Activate: the arrow rotates with the state (active = 180 degrees)
            // and the flyout opens.
            item()->animateChevron(180.0f);
            rootNode->activateCategory(routeKey, m_itemWidget);

            if (!chevronClicked && m_itemWidget && m_itemWidget->isSelectable())
                rootNode->setCurrentItem(routeKey);
        }

        return;
    }

    setExpanded(!m_isExpanded, true);

    // Left non-compact: the chevron only expands/collapses; a body click expands
    // first and then switches the page depending on selectability.
    if (chevronClicked)
        return;

    if (m_itemWidget && m_itemWidget->isSelectable())
        // Clicking a selectable category item inside a flyout (inlineExpansion): only
        // switch the selected state without recomputing overflow immediately; the
        // reflow is triggered by dismissCategory after the flyout closes, avoiding
        // top bar layout jitter while the flyout is open.
        rootNode->setCurrentItem(routeKey, !inlineExpansion);
}
} // namespace manga::ui::navigation
