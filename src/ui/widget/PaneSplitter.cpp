#include "ui/widget/PaneSplitter.h"

#include <QPaintEvent>
#include <QPainter>
#include <QSplitter>

namespace manga::ui::widget {

namespace {

/// Hairline divider between panes: the handle is exactly one pixel wide and
/// paints a single hairline, so the split reads as a plain 1px rule with no
/// visible gap. The native stretch cursor matches the modern Windows
/// left-right resize affordance.
class PaneSplitterHandle : public QSplitterHandle, public fluent::FluentElement {
public:
    explicit PaneSplitterHandle(Qt::Orientation orientation, QSplitter* parent) : QSplitterHandle(orientation, parent) {
        setCursor(orientation == Qt::Horizontal ? Qt::SizeHorCursor : Qt::SizeVerCursor);
    }

    void onThemeUpdated() override {
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(rect(), Qt::transparent);
        if (!shouldPaintDivider()) {
            return;
        }

        const auto& colors = themeColorsRef();
        painter.setPen(QPen(colors.strokeDivider, 1));
        if (orientation() == Qt::Horizontal) {
            const int x = width() / 2;
            painter.drawLine(x, 0, x, height());
        } else {
            const int y = height() / 2;
            painter.drawLine(0, y, width(), y);
        }
    }

private:
    bool shouldPaintDivider() const {
        const auto* splitter = qobject_cast<const QSplitter*>(parentWidget());
        if (!splitter || !isVisible()) {
            return false;
        }

        for (int handleIndex = 1; handleIndex < splitter->count(); ++handleIndex) {
            if (splitter->handle(handleIndex) != this) {
                continue;
            }

            const auto* leadingPane = splitter->widget(handleIndex - 1);
            const auto* trailingPane = splitter->widget(handleIndex);
            if (!leadingPane || !trailingPane) {
                return false;
            }

            const bool leadingVisible = leadingPane->isVisibleTo(splitter);
            const bool trailingVisible = trailingPane->isVisibleTo(splitter);
            if (orientation() == Qt::Horizontal) {
                return leadingVisible && trailingVisible && leadingPane->width() > 0 && trailingPane->width() > 0;
            }
            return leadingVisible && trailingVisible && leadingPane->height() > 0 && trailingPane->height() > 0;
        }

        return false;
    }
};

} // namespace

PaneSplitter::PaneSplitter(Qt::Orientation orientation, QWidget* parent) : QSplitter(orientation, parent) {
    setHandleWidth(1);
    connect(this, &QSplitter::splitterMoved, this, [this](int, int) { refreshHandlePainting(); });
}

void PaneSplitter::refreshHandlePainting() {
    for (int index = 1; index < count(); ++index) {
        if (auto* handle = this->handle(index)) {
            handle->update();
        }
    }
}

QSplitterHandle* PaneSplitter::createHandle() {
    return new PaneSplitterHandle(orientation(), this);
}

} // namespace manga::ui::widget
