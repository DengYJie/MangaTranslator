#pragma once

#include "application/dto/AppSettingsDto.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/usecase/credential/CredentialUseCases.h"

#include <QHash>
#include <QSet>
#include <QString>

#include <optional>
#include <type_traits>

namespace manga::ui::screen::settings {

using AppSettingsSnapshot = ::manga::application::AppSettingsSnapshot;
using AppTheme = ::manga::application::AppTheme;
using InterfaceDensity = ::manga::application::InterfaceDensity;
using CredentialState = ::manga::application::CredentialState;
using OcrModelStatus = ::manga::application::OcrModelStatus;

enum class SettingsCategory {
    General,
    Ocr,
    AiTranslation,
    Appearance
};

inline size_t qHash(SettingsCategory key, size_t seed = 0) noexcept {
    return ::qHash(static_cast<std::underlying_type_t<SettingsCategory>>(key), seed);
}

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
    AppSettingsSnapshot draft;
    QHash<QString, QString> fieldErrors;
    QSet<SettingsCategory> dirtyCategories;
    CredentialState credentialStatus{CredentialState::Missing};
    OcrModelStatus modelStatus;
    SettingsCategory currentCategory{SettingsCategory::General};
    QHash<SettingsCategory, int> scrollPositions;
    bool isSaving{false};
    std::optional<QString> saveError;

    [[nodiscard]] bool isDirty() const {
        return !dirtyCategories.isEmpty();
    }

    [[nodiscard]] bool isCategoryDirty(SettingsCategory category) const {
        return dirtyCategories.contains(category);
    }

    [[nodiscard]] bool hasFieldErrors() const {
        return !fieldErrors.isEmpty();
    }

    friend bool operator==(const SettingsState&, const SettingsState&) = default;
};

} // namespace manga::ui::screen::settings
