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
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setContentsMargins(0, 0, 0, 0);
    m_mainLayout->setSpacing(4);

    // Group title outside card, aligned with card's outer left edge
    m_titleLabel = new fluent::textfields::Label(this);
    m_titleLabel->setFluentTypography(Typography::FontRole::BodyStrong);
    m_titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_titleLabel->setVisible(false);
    m_mainLayout->addWidget(m_titleLabel, 0, Qt::AlignLeft);

    // Group description outside card, aligned with card's outer left edge
    m_descLabel = new fluent::textfields::Label(this);
    m_descLabel->setFluentTypography(Typography::FontRole::Caption);
    m_descLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    m_descLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_descLabel->setVisible(false);
    m_mainLayout->addWidget(m_descLabel, 0, Qt::AlignLeft);
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

    m_rows.append(row);
    rebuildCards();
}

void SettingsGroup::rebuildCards() {
    // Remove existing cards
    for (auto* card : m_cards) {
        m_mainLayout->removeWidget(card);
        card->deleteLater();
    }
    m_cards.clear();

    // Create a card for each row (WinUI 3 style: one card per setting item)
    for (auto* row : m_rows) {
        auto* card = new fluent::layout::Card(this);
        card->setAppearance(fluent::layout::Card::Layer);
        card->setBorderVisible(true);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);

        row->setParent(card);
        cardLayout->addWidget(row);

        // Card spans full width, aligning with group title/description
        m_mainLayout->addWidget(card);
        m_cards.append(card);
    }
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
