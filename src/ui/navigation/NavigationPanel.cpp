#include "ui/navigation/NavigationPanel.h"

#include <FluentQt/BasicInput.h>
#include <QHBoxLayout>
#include <QPalette>
#include <QVBoxLayout>

namespace manga::ui::navigation {

NavigationPanel::NavigationPanel(const QVector<NavigationItemDescriptor>& items, QWidget* parent)
    : QWidget(parent), m_itemsLayout(new QVBoxLayout(this)) {
    m_itemsLayout->setContentsMargins(0, 4, 0, 4);
    m_itemsLayout->setSpacing(2);
    for (const auto& item : items) {
        auto* rowWidget = new QWidget(this);
        auto* rowLayout = new QHBoxLayout(rowWidget);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(0);
        auto* indicator = new QWidget(rowWidget);
        indicator->setFixedWidth(3);
        indicator->setVisible(false);
        auto* button = new fluent::basicinput::Button(item.text, rowWidget);
        button->setIconGlyph(item.iconGlyph);
        rowLayout->addWidget(indicator);
        rowLayout->addWidget(button, 1);
        m_itemsLayout->addWidget(rowWidget);
        const QString routeKey = item.routeKey;
        connect(
            button, &fluent::basicinput::Button::clicked, this, [this, routeKey]() { Q_EMIT itemSelected(routeKey); });
        m_rows.append({routeKey, indicator, button});
    }
    m_itemsLayout->addStretch(1);
    applyIndicatorColors();
}

void NavigationPanel::setCurrentItem(const QString& routeKey) {
    bool found = false;
    for (const Row& row : m_rows) {
        const bool selected = row.routeKey == routeKey;
        row.indicator->setVisible(selected);
        found = found || selected;
    }
    if (found) {
        m_currentRouteKey = routeKey;
    }
}

void NavigationPanel::onThemeUpdated() {
    applyIndicatorColors();
    QWidget::update();
}

void NavigationPanel::applyIndicatorColors() {
    const QColor accent = themeColorsRef().accentDefault;
    for (const Row& row : m_rows) {
        QPalette palette = row.indicator->palette();
        palette.setColor(QPalette::Window, accent);
        row.indicator->setPalette(palette);
        row.indicator->setAutoFillBackground(true);
    }
}

} // namespace manga::ui::navigation
