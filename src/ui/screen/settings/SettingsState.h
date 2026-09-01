#pragma once

#include "application/dto/AppSettingsDto.h"
#include "application/ports/IInpaintModelValidator.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/usecase/credential/CredentialUseCases.h"

#include <QHash>
#include <QString>

#include <optional>

namespace manga::ui::screen::settings {

using AppSettingsSnapshot = ::manga::application::AppSettingsSnapshot;
using AppTheme = ::manga::application::AppTheme;
using InterfaceDensity = ::manga::application::InterfaceDensity;
using CredentialState = ::manga::application::CredentialState;
using OcrModelStatus = ::manga::application::OcrModelStatus;
using InpaintModelStatus = ::manga::application::InpaintModelStatus;

enum class SettingsCategory {
    General,
    Ocr,
    AiTranslation,
    Appearance
};

inline QString settingsCategoryToString(SettingsCategory category) {
    switch (category) {
        case SettingsCategory::General:
            return QStringLiteral("General");
        case SettingsCategory::Ocr:
            return QStringLiteral("Ocr");
        case SettingsCategory::AiTranslation:
            return QStringLiteral("AiTranslation");
        case SettingsCategory::Appearance:
            return QStringLiteral("Appearance");
    }
    return QStringLiteral("General");
}

inline QString settingsCategoryRouteKey(SettingsCategory category) {
    switch (category) {
        case SettingsCategory::General:
            return QStringLiteral("settings/general");
        case SettingsCategory::Ocr:
            return QStringLiteral("settings/ocr");
        case SettingsCategory::AiTranslation:
            return QStringLiteral("settings/ai-translation");
        case SettingsCategory::Appearance:
            return QStringLiteral("settings/appearance");
    }
    return QStringLiteral("settings/general");
}

inline std::optional<SettingsCategory> settingsCategoryFromRouteKey(const QString& routeKey) {
    if (routeKey == QLatin1String("settings") || routeKey == QLatin1String("settings/general") ||
        routeKey == QLatin1String("general")) {
        return SettingsCategory::General;
    }
    if (routeKey == QLatin1String("settings/ocr") || routeKey == QLatin1String("ocr")) {
        return SettingsCategory::Ocr;
    }
    if (routeKey == QLatin1String("settings/ai-translation") || routeKey == QLatin1String("ai-translation") ||
        routeKey == QLatin1String("translation")) {
        return SettingsCategory::AiTranslation;
    }
    if (routeKey == QLatin1String("settings/appearance") || routeKey == QLatin1String("appearance")) {
        return SettingsCategory::Appearance;
    }
    return std::nullopt;
}

struct SettingsState {
    AppSettingsSnapshot persisted;
    QHash<QString, QString> fieldErrors;
    CredentialState credentialStatus{CredentialState::Missing};
    OcrModelStatus modelStatus;
    InpaintModelStatus inpaintModelStatus;

    [[nodiscard]] bool hasFieldErrors() const {
        return !fieldErrors.isEmpty();
    }

    friend bool operator==(const SettingsState&, const SettingsState&) = default;
};

} // namespace manga::ui::screen::settings
