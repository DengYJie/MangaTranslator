#pragma once

#include <FluentQt/Foundation.h>
#include <QString>
#include <QVector>
#include <QWidget>

class QVBoxLayout;

namespace fluent::basicinput {
class Button;
}

namespace manga::ui::navigation {

struct NavigationItemDescriptor {
    QString routeKey;
    QString text;
    QString iconGlyph; // Fluent icon font glyph, e.g. "\uE80F"
};

/// Flat navigation item list hosted as NavigationView chrome. Each row carries a 3px
/// accent indicator strip so selection never depends on button checked-state painting.
class NavigationPanel : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit NavigationPanel(const QVector<NavigationItemDescriptor>& items, QWidget* parent = nullptr);

    void setCurrentItem(const QString& routeKey);
    [[nodiscard]] QString currentItem() const {
        return m_currentRouteKey;
    }

    void onThemeUpdated() override;

signals:
    void itemSelected(const QString& routeKey);

private:
    struct Row {
        QString routeKey;
        QWidget* indicator{nullptr}; // 3px accent strip, non-owning observation
        fluent::basicinput::Button* button{nullptr};
    };

    void applyIndicatorColors();

    QVBoxLayout* m_itemsLayout{nullptr};
    QVector<Row> m_rows;
    QString m_currentRouteKey;
};

} // namespace manga::ui::navigation
