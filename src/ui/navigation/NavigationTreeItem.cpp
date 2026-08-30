#include "ui/navigation/NavigationTreeItem.h"

#include "ui/navigation/NavigationMetrics.h"

#include <FluentQt/Design.h>
#include <FluentQt/StatusInfo.h>
#include <QAccessible>
#include <QEasingCurve>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace manga::ui::navigation {
NavigationTreeItem::NavigationTreeItem(const QString& routeKey,
                                       const QString& iconGlyph,
                                       const QString& text,
                                       const QString& tooltipText,
                                       Kind kind,
                                       int depth,
                                       bool selectable,
                                       QWidget* parent,
                                       std::shared_ptr<animation::AnimatedVisualSource> visualSource)
    : NavigationPushButton(iconGlyph, text, /*isSelectable=*/selectable, parent), m_routeKey(routeKey), m_kind(kind),
      m_tooltipText(tooltipText) {
    if (visualSource) {
        setVisualSource(visualSource);
    }
    setNodeDepth(depth);
    setSelectable(selectable);

    if (!text.isEmpty())
        setAccessibleItemName(text);
    notifyAccessibleRoleChange();

    // The category arrow rotates with the expand/collapse state; it points down
    // when expanded.
    m_rotateAnimation = new QVariantAnimation(this);
    m_rotateAnimation->setDuration(themeAnimation().fast);
    m_rotateAnimation->setEasingCurve(themeAnimation().decelerate);
    connect(m_rotateAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        setArrowAngle(v.toFloat());
    });

    // All clicks go through the base class: a leaf reports the indicator and
    // requests a page switch; a category toggles expansion.
    connect(this, &NavigationWidget::clicked, this, &NavigationTreeItem::onClicked);
}

void NavigationTreeItem::setAccessibleItemName(const QString& name) {
    if (name.isEmpty())
        return;
    setAccessibleName(name);
    QAccessibleEvent nameEvent(this, QAccessible::NameChanged);
    QAccessible::updateAccessibility(&nameEvent);
}

QAccessible::Role NavigationTreeItem::accessibleRole() const {
    return isCategory() ? QAccessible::TreeItem : QAccessible::Button;
}

void NavigationTreeItem::notifyAccessibleRoleChange() {
    QAccessibleEvent roleEvent(this, QAccessible::RoleChanged);
    QAccessible::updateAccessibility(&roleEvent);
}

void NavigationTreeItem::notifyAccessibleState(const QAccessible::State& state) {
    // Synchronizes state bit changes (selected, expanded/collapsed) to
    // accessibility assistive tools.
    QAccessible::State st = state;
    if (isCategory()) {
        st.expandable = true;
    }
    QAccessibleStateChangeEvent change(this, st);
    QAccessible::updateAccessibility(&change);
}

void NavigationTreeItem::setSelected(bool selected) {
    if (isSelected() == selected)
        return;
    NavigationPushButton::setSelected(selected);
    QAccessible::State st;
    st.selected = selected;
    notifyAccessibleState(st);
}

void NavigationTreeItem::setExpanded(bool expanded, bool animated) {
    if (!isCategory())
        return;

    const float target = expanded ? 180.0f : 0.0f;
    if (!animated) {
        setArrowAngle(target);
        update();
    } else {
        animateChevron(target);
    }

    QAccessible::State st;
    st.expanded = expanded;
    st.collapsed = !expanded;
    notifyAccessibleState(st);
}

void NavigationTreeItem::animateChevron(float target) {
    if (NavigationWidget::isReducedMotion()) {
        setArrowAngle(target);
        update();
    } else {
        m_rotateAnimation->stop();
        m_rotateAnimation->setStartValue(m_arrowAngle);
        m_rotateAnimation->setEndValue(target);
        m_rotateAnimation->start();
    }
}

void NavigationTreeItem::setOrientation(Qt::Orientation orientation) {
    NavigationPushButton::setOrientation(orientation);
}

void NavigationTreeItem::setShowIndicator(bool show) {
    if (m_showIndicator == show)
        return;
    m_showIndicator = show;
    update();
}

void NavigationTreeItem::setArrowAngle(float angle) {
    if (qFuzzyCompare(angle, m_arrowAngle))
        return;
    m_arrowAngle = angle;
    update();
}

float NavigationTreeItem::currentTextAlpha() const {
    if (m_orientation == Qt::Horizontal) {
        return 1.0f;
    }
    return isCompacted() ? 0.0f : NavigationPushButton::currentTextAlpha();
}

int NavigationTreeItem::iconDrawX() const {
    if (m_orientation == Qt::Horizontal) {
        return NavigationPushButton::iconDrawX();
    }
    if (isCompacted()) {
        return qMax(0, (Breakpoints::NavigationPaneCompactWidth - iconSize()) / 2);
    }
    const qreal expandedLeft = kRowLeftInset + kContentStart + m_nodeDepth * themeSpacing().large;
    return qRound(expandedLeft);
}

int NavigationTreeItem::textRightOffset() const {
    if (m_orientation == Qt::Horizontal) {
        return chevronVisible() ? kTopChevronAddedWidth : 0;
    }
    return chevronVisible() ? (kChevronAreaWidth + kChevronRightInset) : 0;
}

QRectF NavigationTreeItem::indicatorRect() const {
    return NavigationPushButton::indicatorRect();
}

QSize NavigationTreeItem::sizeHint() const {
    QSize baseSize = NavigationPushButton::sizeHint();
    if (m_orientation == Qt::Horizontal && isCategory()) {
        baseSize.rwidth() += kTopChevronAddedWidth;
    }
    return baseSize;
}

void NavigationTreeItem::onClicked() {
    Q_EMIT itemClicked(m_routeKey, /*chevronClicked=*/false);
}

void NavigationTreeItem::paintEvent(QPaintEvent* event) {
    NavigationPushButton::paintEvent(event);

    // Controlled by m_showIndicator rather than isSelected(): when collapsed, the
    // parent category paints the indicator on behalf of the selected child.
    if (m_showIndicator) {
        const auto& colors = colorsRef();
        const QRectF ir = indicatorRect();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.accentDefault);
        painter.drawRoundedRect(ir.adjusted(0, 0, -1, -1), ::CornerRadius::Indicator, ::CornerRadius::Indicator);
    }

    if (!chevronVisible())
        return;

    const auto& colors = colorsRef();
    const QRectF cRect = chevronRect();

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setFont(Typography::Icons::font(kChevronIconPixelSize));
    painter.setPen(colors.textSecondary);
    painter.save();
    painter.translate(cRect.center());
    painter.rotate(m_arrowAngle);
    painter.translate(-cRect.center());
    painter.drawText(cRect,
                     Qt::AlignCenter,
                     Typography::Icons::glyphForSize(Typography::Icons::ChevronDownMed, kChevronIconPixelSize));
    painter.restore();
}

QRectF NavigationTreeItem::chevronRect() const {
    return QRectF(width() - kChevronRightInset - kChevronAreaWidth,
                  (height() - kChevronAreaWidth) / 2.0,
                  kChevronAreaWidth,
                  kChevronAreaWidth);
}

bool NavigationTreeItem::chevronVisible() const {
    if (!isCategory())
        return false;
    if (m_orientation == Qt::Horizontal)
        return true;
    return !isCompacted();
}

void NavigationTreeItem::keyPressEvent(QKeyEvent* event) {
    if (isCategory() &&
        (event->key() == Qt::Key_Space || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        if (!isSelectable()) {
            Q_EMIT itemClicked(m_routeKey, /*chevronClicked=*/true);
            event->accept();
            return;
        }
    }
    NavigationPushButton::keyPressEvent(event);
}

void NavigationTreeItem::mousePressEvent(QMouseEvent* event) {
    m_chevronPressActive = false;
    if (event->button() == Qt::LeftButton && isCategory() && chevronVisible()) {
        m_chevronPressActive = chevronRect().contains(event->pos());
    }
    NavigationPushButton::mousePressEvent(event);
}

void NavigationTreeItem::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_chevronPressActive) {
        m_chevronPressActive = false;
        m_isPressed = false;
        update();

        if (chevronRect().contains(event->pos())) {
            Q_EMIT itemClicked(m_routeKey, /*chevronClicked=*/true);
        }
        event->accept();
        return;
    }
    NavigationPushButton::mouseReleaseEvent(event);
}

void NavigationTreeItem::setCompacted(bool compacted) {
    NavigationWidget::setCompacted(compacted);

    if (compacted && nodeDepth() == 0) {
        fluent::status_info::ToolTip::attach(this, m_tooltipText, fluent::status_info::ToolTip::Placement::Above);
    } else {
        fluent::status_info::ToolTip::attach(this, QString());
    }
}
} // namespace manga::ui::navigation
