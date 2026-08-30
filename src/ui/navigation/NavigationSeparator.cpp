#include "ui/navigation/NavigationSeparator.h"

#include "ui/navigation/NavigationMetrics.h"

#include <QPainter>

namespace manga::ui::navigation {

NavigationSeparator::NavigationSeparator(QWidget* parent) : NavigationWidget(false, parent) {
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    // Overrides the base class setFixedHeight(kItemHeight): Left mode separator occupies
    // leading margin 3 + line 1 + trailing margin 4.
    setFixedHeight(kSeparatorLeadingMargin + kSeparatorLineThickness + kSeparatorTrailingMargin);
}

void NavigationSeparator::setOrientation(Qt::Orientation orientation) {
    NavigationWidget::setOrientation(orientation);
    if (orientation == Qt::Horizontal) {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        // Top mode: left margin 3 + line 1 + right margin 4, filling the bar height.
        setFixedSize(kSeparatorLeadingMargin + kSeparatorLineThickness + kSeparatorTrailingMargin, kTopBarItemHeight);
    } else {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setFixedHeight(kSeparatorLeadingMargin + kSeparatorLineThickness + kSeparatorTrailingMargin);
    }
    updateGeometry();
}

QSize NavigationSeparator::sizeHint() const {
    if (m_orientation == Qt::Horizontal) {
        return QSize(kSeparatorLeadingMargin + kSeparatorLineThickness + kSeparatorTrailingMargin, kTopBarItemHeight);
    }
    return QSize(0, kSeparatorLeadingMargin + kSeparatorLineThickness + kSeparatorTrailingMargin);
}

void NavigationSeparator::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    const auto& colors = colorsRef();

    // WinUI 3 NavigationViewItemSeparatorForeground = DividerStrokeColorDefaultBrush.
    QPen pen(colors.strokeDivider);
    pen.setWidth(kSeparatorLineThickness);
    pen.setCosmetic(true);
    painter.setPen(pen);

    // The line spans the full extent with no extra inset; only the leading margin positions it.
    if (m_orientation == Qt::Horizontal) {
        painter.drawLine(kSeparatorLeadingMargin, 0, kSeparatorLeadingMargin, height());
    } else {
        painter.drawLine(0, kSeparatorLeadingMargin, width(), kSeparatorLeadingMargin);
    }
}

} // namespace manga::ui::navigation
