#include "ui/base/BasePage.h"

#include <QResizeEvent>
#include <QShowEvent>

namespace manga::ui::base {

BasePage::BasePage(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);
}

void BasePage::onThemeUpdated() {
    QWidget::update();
}

void BasePage::setDefaultFocusWidget(QWidget* widget) {
    m_defaultFocusWidget = widget;
}

void BasePage::updateResponsiveLayout(int availableWidth) {
    Q_UNUSED(availableWidth);
}

void BasePage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResponsiveLayout(width());
}

void BasePage::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (m_defaultFocusWidget && m_defaultFocusWidget->isVisibleTo(this)) {
        m_defaultFocusWidget->setFocus();
    }
}

} // namespace manga::ui::base
