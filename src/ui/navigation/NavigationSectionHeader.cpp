#include "ui/navigation/NavigationSectionHeader.h"

#include "ui/navigation/NavigationMetrics.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>

namespace manga::ui::navigation {

NavigationSectionHeader::NavigationSectionHeader(const QString& text, QWidget* parent)
    : NavigationWidget(/*isSelectable=*/false, parent), m_text(text) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);
    updateCompactHeight();
}

void NavigationSectionHeader::setCompacted(bool compacted) {
    NavigationWidget::setCompacted(compacted);
    updateCompactHeight();
}

void NavigationSectionHeader::setOrientation(Qt::Orientation orientation) {
    NavigationWidget::setOrientation(orientation);
    // The base class applies the generic row height; the header recomputes by its own
    // rule (Vertical = 40 with collapse scaling / Top = 48).
    updateCompactHeight();
    updateGeometry();
}

QSize NavigationSectionHeader::sizeHint() const {
    if (m_orientation == Qt::Horizontal) {
        QFont f = themeFont(Typography::FontRole::Caption).toQFont();
        QFontMetrics fm(f);
        // Text width plus padding on both sides.
        return QSize(fm.horizontalAdvance(m_text) + themeSpacing().medium * 2, kTopBarItemHeight);
    }
    return QSize(0, isCompacted() ? 0 : kSectionHeight);
}

void NavigationSectionHeader::updateCompactHeight() {
    // Top (Horizontal) mode aligns the height with the bar.
    if (m_orientation == Qt::Horizontal) {
        setFixedHeight(kTopBarItemHeight);
        return;
    }
    setFixedHeight(isCompacted() ? 0 : kSectionHeight);
}

void NavigationSectionHeader::mousePressEvent(QMouseEvent* event) {
    // Section headers are not clickable: no pressed state and no clicked signal.
    event->ignore();
}

void NavigationSectionHeader::mouseReleaseEvent(QMouseEvent* event) {
    event->ignore();
}

void NavigationSectionHeader::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);

    if (isCompacted())
        return;

    painter.setFont(themeFont(Typography::FontRole::Caption).toQFont());
    painter.setPen(colorsRef().textSecondary);

    // Horizontal uses the regular margin; Vertical aligns with the left icon edge.
    const int x = (m_orientation == Qt::Horizontal) ? themeSpacing().medium : (kRowLeftInset + kContentStart);
    const QRect textRect(x, 0, qMax(0, width() - x - themeSpacing().small), height());
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_text);
}

} // namespace manga::ui::navigation
