#include "ui/navigation/NavigationPushButton.h"

#include "ui/animation/AnimatedIcon.h"
#include "ui/animation/AnimatedVisualSource.h"
#include "ui/navigation/NavigationMetrics.h"

#include <FluentQt/Design.h>
#include <QPainter>
#include <QResizeEvent>

namespace manga::ui::navigation {
NavigationPushButton::NavigationPushButton(const QString& iconGlyph,
                                           const QString& text,
                                           bool isSelectable,
                                           QWidget* parent)
    : NavigationWidget(isSelectable, parent), m_iconGlyph(iconGlyph), m_text(text) {
    setCursor(Qt::PointingHandCursor);
    if (!text.isEmpty()) {
        setAccessibleItemName(text);
    }
}

void NavigationPushButton::setText(const QString& text) {
    m_text = text;
    if (!text.isEmpty()) {
        setAccessibleItemName(text);
    }
    update();
}

void NavigationPushButton::setIconGlyph(const QString& glyph) {
    m_iconGlyph = glyph;
    update();
}

void NavigationPushButton::setVisualSource(std::shared_ptr<manga::ui::animation::AnimatedVisualSource> source) {
    m_visualSource = source;
    if (m_visualSource) {
        if (!m_animatedIcon) {
            m_animatedIcon = new manga::ui::animation::AnimatedIcon(this);
            m_animatedIcon->setAutoFillParent(false);
        }
        m_animatedIcon->setSource(m_visualSource);
        m_animatedIcon->show();
        updateIconGeometry();
    } else {
        if (m_animatedIcon) {
            m_animatedIcon->hide();
        }
    }
    update();
}

void NavigationPushButton::updateIconGeometry() {
    if (m_animatedIcon && m_visualSource) {
        const int s = m_iconSize;
        const int x = iconDrawX();
        const int y = (height() - s) / 2;
        m_animatedIcon->setGeometry(x, y, s, s);
    }
}

void NavigationPushButton::resizeEvent(QResizeEvent* event) {
    NavigationWidget::resizeEvent(event);
    updateIconGeometry();
}

void NavigationPushButton::setIconSize(int size) {
    if (m_iconSize == size)
        return;
    m_iconSize = size;
    updateIconGeometry();
    updateGeometry();
    update();
}

QSize NavigationPushButton::sizeHint() const {
    if (m_orientation == Qt::Horizontal) {
        if (isFooterItem() || m_text.isEmpty()) {
            return QSize(kTopBarItemHeight, kTopBarItemHeight);
        }
        int w = kTopBarItemHorizontalPadding * 2;
        if (!m_iconGlyph.isEmpty()) {
            w += m_iconSize;
        }
        if (!m_text.isEmpty()) {
            QFont f = themeFont(Typography::FontRole::Body).toQFont();
            QFontMetrics fm(f);
            if (!m_iconGlyph.isEmpty()) {
                w += kTopBarButtonSpacing; // spacing between icon and text
            }
            w += fm.horizontalAdvance(m_text);
        }
        return QSize(w, kTopBarItemHeight);
    }
    return QSize(0, kItemHeight);
}

int NavigationPushButton::iconDrawX() const {
    if (m_orientation == Qt::Horizontal) {
        if (isFooterItem() || m_text.isEmpty()) {
            return qMax(0, (width() - m_iconSize) / 2);
        }
        return kTopBarItemHorizontalPadding;
    }
    if (isCompacted()) {
        return qMax(0, (Breakpoints::NavigationPaneCompactWidth - m_iconSize) / 2);
    }
    const qreal expandedLeft = kRowLeftInset + kContentStart + m_nodeDepth * themeSpacing().large;
    return qRound(expandedLeft);
}

void NavigationPushButton::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const auto& colors = colorsRef();
    const auto radius = themeRadius().control;
    const auto spacing = themeSpacing();

    const int iconX = iconDrawX();

    QColor bg = currentBackgroundColor();
    if (bg.alpha() > 0) {
        QRectF itemRect;
        if (m_orientation == Qt::Horizontal) {
            if (m_text.isEmpty() || isFooterItem()) {
                // Top icon-only/Footer items: 1:1 square card
                const int cardSize = height() - kItemBgPaddingV * 2;
                const qreal cardX = (width() - cardSize) / 2.0;
                itemRect = QRectF(cardX, kItemBgPaddingV, cardSize, cardSize);
            } else {
                itemRect = QRectF(
                    spacing.xSmall, kItemBgPaddingV, width() - spacing.xSmall * 2, height() - kItemBgPaddingV * 2);
            }
        } else {
            if (isCompacted()) {
                const int compactWidth = Breakpoints::NavigationPaneCompactWidth;
                itemRect = QRectF(
                    spacing.xSmall, kItemBgPaddingV, compactWidth - spacing.xSmall * 2, height() - kItemBgPaddingV * 2);
            } else {
                itemRect = QRectF(
                    spacing.xSmall, kItemBgPaddingV, width() - spacing.xSmall * 2, height() - kItemBgPaddingV * 2);
            }
        }
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(itemRect, radius, radius);
    }

    updateIconGeometry();

    if (!m_visualSource && !m_iconGlyph.isEmpty()) {
        painter.setFont(Typography::Icons::font(m_iconSize));
        painter.setPen(m_isSelected ? colors.textPrimary : colors.textSecondary);
        const QString glyph = Typography::Icons::glyphForSize(m_iconGlyph, m_iconSize);
        painter.drawText(QRect(iconX, 0, m_iconSize, height()), Qt::AlignCenter, glyph);
    }

    const float alpha = currentTextAlpha();
    if (alpha > 0.0f && !m_text.isEmpty() && !(m_orientation == Qt::Horizontal && isFooterItem())) {
        painter.setFont(
            themeFont(m_isSelected ? Typography::FontRole::BodyStrong : Typography::FontRole::Body).toQFont());
        QColor textCol = colors.textPrimary;
        textCol.setAlphaF(alpha);
        painter.setPen(textCol);

        int textLeft = 0;
        int maxTextWidth = 0;
        if (m_orientation == Qt::Horizontal) {
            textLeft = kTopBarItemHorizontalPadding;
            if (!m_iconGlyph.isEmpty()) {
                textLeft += m_iconSize + kTopBarButtonSpacing;
            }
            maxTextWidth = qMax(0, width() - textLeft - textRightOffset() - kTopBarItemHorizontalPadding);
        } else {
            textLeft = kTextLeftOffset + m_nodeDepth * spacing.large;
            maxTextWidth = qMax(0, width() - textLeft - textRightOffset() - spacing.small);
        }
        painter.drawText(
            QRect(textLeft, 0, maxTextWidth, height()), Qt::AlignVCenter | Qt::AlignLeft | Qt::TextSingleLine, m_text);
    }

    drawFocusVisual(painter, QRectF(0, 0, width(), height()));
}
} // namespace manga::ui::navigation
