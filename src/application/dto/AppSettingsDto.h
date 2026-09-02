#pragma once

#include <QString>

namespace manga::application {

enum class AppTheme {
    System,
    Light,
    Dark
};

struct AppSettingsSnapshot {
    QString ocrModelDirectory;
    QString detectionModelDirectory;
    QString inpaintModelDirectory;
    QString endpoint;
    QString modelName;
    int timeoutSeconds{300};
    int retryCount{2};
    AppTheme theme{AppTheme::System};

    friend bool operator==(const AppSettingsSnapshot&, const AppSettingsSnapshot&) = default;
};

} // namespace manga::application
