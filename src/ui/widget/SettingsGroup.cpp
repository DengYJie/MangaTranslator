#include "ui/widget/SettingsGroup.h"

#include "ui/widget/SettingsRow.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/Layout.h>
#include <FluentQt/TextFields.h>
#include <QVBoxLayout>

namespace manga::ui::widget {

SettingsGroup::SettingsGroup(QWidget* parent) : QWidget(parent) {
    setupUi();
}

SettingsGroup::SettingsGroup(const QString& title, const QString& description, QWidget* parent) : QWidget(parent) {
    setupUi();
    setTitle(title);
    setDescription(description);
}

void SettingsGroup::setupUi() {
    setAttribute(Qt::WA_StyledBackground, false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(6);

    // Group title outside card
    m_titleLabel = new fluent::textfields::Label(this);
    m_titleLabel->setFluentTypography(Typography::FontRole::Subtitle);
    m_titleLabel->setVisible(false);
    m_mainLayout->addWidget(m_titleLabel);

    // Group description outside card
    m_descLabel = new fluent::textfields::Label(this);
    m_descLabel->setFluentTypography(Typography::FontRole::Caption);
    m_descLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    m_descLabel->setWordWrap(true);
    m_descLabel->setVisible(false);
    m_mainLayout->addWidget(m_descLabel);

    // Card surface
    m_card = new fluent::layout::Card(this);
    m_card->setAppearance(fluent::layout::Card::Layer);
    m_card->setBorderVisible(true);

    m_cardLayout = new QVBoxLayout(m_card);
    m_cardLayout->setContentsMargins(0, 0, 0, 0);
    m_cardLayout->setSpacing(0);

    m_mainLayout->addWidget(m_card);
}

void SettingsGroup::setTitle(const QString& title) {
    m_title = title;
    m_titleLabel->setText(title);
    m_titleLabel->setVisible(!title.isEmpty());
}

QString SettingsGroup::title() const {
    return m_title;
}

void SettingsGroup::setDescription(const QString& description) {
    m_description = description;
    m_descLabel->setText(description);
    m_descLabel->setVisible(!description.isEmpty());
}

QString SettingsGroup::description() const {
    return m_description;
}

void SettingsGroup::addRow(SettingsRow* row) {
    if (!row) {
        return;
    }

    if (!m_rows.isEmpty()) {
        auto* divider = new fluent::layout::Divider(Qt::Horizontal, m_card);
        m_cardLayout->addWidget(divider);
    }

    m_rows.append(row);
    m_cardLayout->addWidget(row);
}

int SettingsGroup::rowCount() const {
    return m_rows.size();
}

SettingsRow* SettingsGroup::rowAt(int index) const {
    if (index < 0 || index >= m_rows.size()) {
        return nullptr;
    }
    return m_rows.at(index);
}

const QList<SettingsRow*>& SettingsGroup::rows() const {
    return m_rows;
}

void SettingsGroup::updateResponsiveLayout(int availableWidth) {
    for (auto* row : m_rows) {
        if (row) {
            row->updateResponsiveLayout(availableWidth);
        }
    }
}

void SettingsGroup::onThemeUpdated() {
    for (auto* row : m_rows) {
        if (row) {
            row->onThemeUpdated();
        }
    }
    update();
}

} // namespace manga::ui::widget
