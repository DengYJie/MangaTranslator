#include "ui/widget/TransparentScrollView.h"

#include <QFrame>
#include <QPalette>

namespace manga::ui::widget {

TransparentScrollView::TransparentScrollView(QWidget* parent) : fluent::scrolling::ScrollView(parent) {
    setFrameShape(QFrame::NoFrame);
    keepViewportTransparent();
}

void TransparentScrollView::onThemeUpdated() {
    // The base implementation overwrites the viewport palette, so transparency is restored after it.
    fluent::scrolling::ScrollView::onThemeUpdated();
    keepViewportTransparent();
}

void TransparentScrollView::keepViewportTransparent() {
    setAutoFillBackground(false);
    if (QWidget* vp = viewport()) {
        QPalette pal = vp->palette();
        pal.setColor(QPalette::Window, Qt::transparent);
        pal.setColor(QPalette::Base, Qt::transparent);
        vp->setPalette(pal);
        vp->setAutoFillBackground(false);
    }
}

} // namespace manga::ui::widget
