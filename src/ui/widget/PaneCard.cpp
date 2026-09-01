#include "ui/widget/PaneCard.h"

#include <QPaintEvent>
#include <QPainter>

namespace manga::ui::widget {

PaneCard::PaneCard(QWidget* parent) : fluent::layout::Card(parent) {
    // Editor panes are borderless so neighbouring panes read as one
    // continuous work surface; only the splitter hairline separates them.
    setBorderVisible(false);
}

void PaneCard::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    const auto& colors = themeColorsRef();
    QColor fill = colors.bgLayer;
    switch (appearance()) {
        case Canvas:
            fill = colors.bgCanvas;
            break;
        case LayerAlt:
            fill = colors.bgLayerAlt;
            break;
        case Layer:
        default:
            fill = colors.bgLayer;
            break;
    }

    QPainter painter(this);
    painter.fillRect(rect(), fill);
    if (isBorderVisible()) {
        painter.setPen(QPen(colors.strokeCard, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5));
    }
}

} // namespace manga::ui::widget
