#pragma once

#include <QString>

namespace manga::application {

enum class AppTheme {
    System,
    Light,
    Dark
};

enum class InterfaceDensity {
    Comfortable,
    Compact
};

struct AppSettingsSnapshot {
    bool restoreLastProject{true};
    int recentProjectLimit{8};
    QString ocrModelDirectory;
    QString ocrLanguage{QStringLiteral("ja")};
    QString providerId{QStringLiteral("openai-compatible")};
    QString endpoint;
    QString modelName;
    int timeoutSeconds{60};
    int retryCount{2};
    AppTheme theme{AppTheme::System};
    InterfaceDensity density{InterfaceDensity::Compact};
    bool reduceMotion{false};
    QString canvasBackground{QStringLiteral("#202124")};

    friend bool operator==(const AppSettingsSnapshot&, const AppSettingsSnapshot&) = default;
};

} // namespace manga::application
