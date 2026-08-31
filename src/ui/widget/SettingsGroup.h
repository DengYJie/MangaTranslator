#pragma once

#include <FluentQt/Foundation.h>
#include <QList>
#include <QPointer>
#include <QWidget>

class QVBoxLayout;
class QLabel;

namespace fluent::layout {
class Card;
}
namespace fluent::textfields {
class Label;
}

namespace manga::ui::widget {

class SettingsRow;

/// Container surface for grouping related SettingsRows.
/// Features an optional section title and description outside the surface,
/// and a single rounded card surface with interior dividers between rows.
class SettingsGroup : public QWidget, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    explicit SettingsGroup(QWidget* parent = nullptr);
    explicit SettingsGroup(const QString& title, const QString& description = QString(), QWidget* parent = nullptr);
    ~SettingsGroup() override = default;

    void setTitle(const QString& title);
    [[nodiscard]] QString title() const;

    void setDescription(const QString& description);
    [[nodiscard]] QString description() const;

    void addRow(SettingsRow* row);
    [[nodiscard]] int rowCount() const;
    [[nodiscard]] SettingsRow* rowAt(int index) const;
    [[nodiscard]] const QList<SettingsRow*>& rows() const;

    void updateResponsiveLayout(int availableWidth);

    void onThemeUpdated() override;

private:
    void setupUi();

    QString m_title;
    QString m_description;

    QVBoxLayout* m_mainLayout{nullptr};
    fluent::textfields::Label* m_titleLabel{nullptr};
    fluent::textfields::Label* m_descLabel{nullptr};
    fluent::layout::Card* m_card{nullptr};
    QVBoxLayout* m_cardLayout{nullptr};

    QList<SettingsRow*> m_rows;
};

} // namespace manga::ui::widget
