#include "ui/widget/SettingsRow.h"

#include <FluentQt/Design.h>
#include <FluentQt/Foundation.h>
#include <FluentQt/TextFields.h>
#include <QHBoxLayout>
#include <QPalette>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace manga::ui::widget {

namespace {
constexpr int kResponsiveBreakpoint = 680;
constexpr int kHorizontalPaddingH = 16;
constexpr int kHorizontalPaddingV = 12;
constexpr int kHorizontalSpacing = 16;
constexpr int kVerticalSpacing = 8;
constexpr int kEditorMaxWidth = 320;
constexpr int kMinimumHeight = 64;
} // namespace

SettingsRow::SettingsRow(QWidget* parent) : QWidget(parent) {
    setupUi();
}

SettingsRow::SettingsRow(const QString& title, const QString& description, QWidget* parent) : QWidget(parent) {
    setupUi();
    setTitle(title);
    setDescription(description);
}

void SettingsRow::setupUi() {
    setAttribute(Qt::WA_StyledBackground, false);
    setAutoFillBackground(false);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMinimumHeight(kMinimumHeight);

    m_mainLayout = new QBoxLayout(QBoxLayout::LeftToRight, this);
    m_mainLayout->setContentsMargins(
        kHorizontalPaddingH, kHorizontalPaddingV, kHorizontalPaddingH, kHorizontalPaddingV);
    m_mainLayout->setSpacing(kHorizontalSpacing);

    // Left text container
    m_textContainer = new QWidget(this);
    m_textContainer->setAutoFillBackground(false);
    m_textContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_textLayout = new QVBoxLayout(m_textContainer);
    m_textLayout->setContentsMargins(0, 0, 0, 0);
    m_textLayout->setSpacing(2);

    // Title row
    m_titleRow = new QWidget(m_textContainer);
    m_titleRowLayout = new QHBoxLayout(m_titleRow);
    m_titleRowLayout->setContentsMargins(0, 0, 0, 0);
    m_titleRowLayout->setSpacing(6);

    m_titleLabel = new fluent::textfields::Label(m_titleRow);
    m_titleLabel->setFluentTypography(Typography::FontRole::BodyStrong);
    m_titleLabel->setWordWrap(true);
    m_titleRowLayout->addWidget(m_titleLabel);

    m_titleRowLayout->addStretch(1);

    m_textLayout->addWidget(m_titleRow);

    m_descLabel = new fluent::textfields::Label(m_textContainer);
    m_descLabel->setFluentTypography(Typography::FontRole::Caption);
    m_descLabel->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    m_descLabel->setWordWrap(true);
    m_descLabel->setVisible(false);
    m_textLayout->addWidget(m_descLabel);

    m_errorLabel = new fluent::textfields::Label(m_textContainer);
    m_errorLabel->setObjectName(QStringLiteral("settingsRowErrorLabel"));
    m_errorLabel->setFluentTypography(Typography::FontRole::Caption);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    m_textLayout->addWidget(m_errorLabel);

    m_mainLayout->addWidget(m_textContainer, 1);

    // Right editor container
    m_editorContainer = new QWidget(this);
    m_editorContainer->setAutoFillBackground(false);
    m_editorContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    m_editorLayout = new QVBoxLayout(m_editorContainer);
    m_editorLayout->setContentsMargins(0, 0, 0, 0);
    m_editorLayout->setSpacing(0);
    m_editorLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_mainLayout->addWidget(m_editorContainer, 0);

    onThemeUpdated();
}

void SettingsRow::setTitle(const QString& title) {
    m_title = title;
    m_titleLabel->setText(title);
    if (m_editorWidget) {
        m_editorWidget->setAccessibleName(title);
    }
}

QString SettingsRow::title() const {
    return m_title;
}

void SettingsRow::setDescription(const QString& description) {
    m_description = description;
    m_descLabel->setText(description);
    m_descLabel->setVisible(!description.isEmpty());
    if (m_editorWidget) {
        m_editorWidget->setAccessibleDescription(description);
    }
}

QString SettingsRow::description() const {
    return m_description;
}

void SettingsRow::setError(const QString& error) {
    m_error = error;
    m_errorLabel->setText(error);
    m_errorLabel->setVisible(!error.isEmpty());
    updateGeometry();
}

void SettingsRow::clearError() {
    setError(QString());
}

QString SettingsRow::error() const {
    return m_error;
}

bool SettingsRow::hasError() const {
    return !m_error.isEmpty();
}

void SettingsRow::setEditorWidget(QWidget* widget, bool fillWidthInVertical) {
    if (m_editorWidget) {
        m_editorLayout->removeWidget(m_editorWidget);
        m_editorWidget->setParent(nullptr);
    }
    m_editorWidget = widget;
    m_fillWidthInVertical = fillWidthInVertical;
    if (m_editorWidget) {
        m_editorLayout->addWidget(m_editorWidget);
        if (!m_title.isEmpty()) {
            m_editorWidget->setAccessibleName(m_title);
        }
        if (!m_description.isEmpty()) {
            m_editorWidget->setAccessibleDescription(m_description);
        }
    }
    applyMode(m_isHorizontal);
}

QWidget* SettingsRow::editorWidget() const {
    return m_editorWidget.data();
}

void SettingsRow::updateResponsiveLayout(int availableWidth) {
    applyMode(availableWidth >= kResponsiveBreakpoint);
}

void SettingsRow::applyMode(bool horizontal) {
    m_isHorizontal = horizontal;
    if (horizontal) {
        m_mainLayout->setDirection(QBoxLayout::LeftToRight);
        m_mainLayout->setSpacing(kHorizontalSpacing);
        m_mainLayout->setAlignment(m_textContainer, Qt::AlignVCenter);
        m_mainLayout->setAlignment(m_editorContainer, Qt::AlignRight | Qt::AlignVCenter);
        m_editorLayout->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_editorContainer->setMaximumWidth(kEditorMaxWidth);
        m_editorContainer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        if (m_editorWidget) {
            m_editorWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        }
        setMinimumHeight(kMinimumHeight);
    } else {
        m_mainLayout->setDirection(QBoxLayout::TopToBottom);
        m_mainLayout->setSpacing(kVerticalSpacing);
        m_mainLayout->setAlignment(m_textContainer, Qt::AlignLeft | Qt::AlignTop);
        m_mainLayout->setAlignment(m_editorContainer, Qt::AlignLeft | Qt::AlignTop);
        m_editorLayout->setAlignment(m_fillWidthInVertical ? Qt::Alignment() : Qt::AlignLeft | Qt::AlignTop);
        m_editorContainer->setMaximumWidth(QWIDGETSIZE_MAX);
        m_editorContainer->setSizePolicy(m_fillWidthInVertical ? QSizePolicy::Expanding : QSizePolicy::Preferred,
                                         QSizePolicy::Preferred);
        if (m_editorWidget) {
            m_editorWidget->setSizePolicy(m_fillWidthInVertical ? QSizePolicy::Expanding : QSizePolicy::Preferred,
                                          QSizePolicy::Preferred);
        }
        setMinimumHeight(0);
    }
    updateGeometry();
}

void SettingsRow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResponsiveLayout(event->size().width());
}

void SettingsRow::onThemeUpdated() {
    const auto& colors = themeColorsRef();
    if (m_errorLabel) {
        QPalette pal = m_errorLabel->palette();
        pal.setColor(QPalette::WindowText, colors.systemCritical);
        m_errorLabel->setPalette(pal);
    }
    update();
}

} // namespace manga::ui::widget
