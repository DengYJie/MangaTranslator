#include "AppSettingsUseCases.h"

#include <QLatin1String>
#include <QMetaType>
#include <QUrl>
#include <QVariant>

#include <algorithm>

namespace manga::application {

namespace {

constexpr auto kKeyOcrModelDirectory = "ocr/modelDirectory";
constexpr auto kKeyDetectionModelDirectory = "detection/modelDirectory";
constexpr auto kKeyInpaintModelDirectory = "inpaint/modelDirectory";
constexpr auto kKeyEndpoint = "translation/endpoint";
constexpr auto kKeyModelName = "translation/modelName";
constexpr auto kKeyTimeoutSeconds = "translation/timeoutSeconds";
constexpr auto kKeyRetryCount = "translation/retryCount";
constexpr auto kKeyTheme = "appearance/theme";

constexpr int kMinTimeoutSeconds = 1;
constexpr int kMaxTimeoutSeconds = 1800;
constexpr int kMinRetryCount = 0;
constexpr int kMaxRetryCount = 10;

QString themeToString(AppTheme theme) {
    switch (theme) {
        case AppTheme::Light:
            return QStringLiteral("light");
        case AppTheme::Dark:
            return QStringLiteral("dark");
        case AppTheme::System:
            break;
    }
    return QStringLiteral("system");
}

AppTheme themeFromString(const QString& value) {
    const QString lower = value.trimmed().toLower();
    if (lower == QLatin1String("light")) {
        return AppTheme::Light;
    }
    if (lower == QLatin1String("dark")) {
        return AppTheme::Dark;
    }
    return AppTheme::System;
}

int toIntSetting(const QVariant& value, int fallback, int minValue, int maxValue) {
    bool ok = false;
    const int parsed = value.toInt(&ok);
    if (!ok) {
        return fallback;
    }
    return std::clamp(parsed, minValue, maxValue);
}

QString toStringSetting(const QVariant& value) {
    return value.typeId() == QMetaType::QString ? value.toString().trimmed() : QString();
}

bool isValidEndpoint(const QString& endpoint) {
    if (endpoint.isEmpty()) {
        return true;
    }
    const QUrl url(endpoint, QUrl::TolerantMode);
    return url.isValid() && !url.host().isEmpty() &&
           (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
}

ApplicationError invalidSetting(const QString& code, const QString& message) {
    return makeApplicationError(ErrorCategory::Input, code, message);
}

} // namespace

AppSettingsSnapshot LoadAppSettingsUseCase::execute() const {
    AppSettingsSnapshot snapshot;
    if (m_store == nullptr) {
        return snapshot;
    }

    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyOcrModelDirectory)); value.has_value()) {
        snapshot.ocrModelDirectory = toStringSetting(*value);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyDetectionModelDirectory)); value.has_value()) {
        snapshot.detectionModelDirectory = toStringSetting(*value);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyInpaintModelDirectory)); value.has_value()) {
        snapshot.inpaintModelDirectory = toStringSetting(*value);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyEndpoint)); value.has_value()) {
        const QString endpoint = toStringSetting(*value);
        if (isValidEndpoint(endpoint)) {
            snapshot.endpoint = endpoint;
        }
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyModelName)); value.has_value()) {
        snapshot.modelName = toStringSetting(*value);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyTimeoutSeconds)); value.has_value()) {
        snapshot.timeoutSeconds = toIntSetting(*value, snapshot.timeoutSeconds, kMinTimeoutSeconds, kMaxTimeoutSeconds);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyRetryCount)); value.has_value()) {
        snapshot.retryCount = toIntSetting(*value, snapshot.retryCount, kMinRetryCount, kMaxRetryCount);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyTheme)); value.has_value()) {
        snapshot.theme = themeFromString(toStringSetting(*value));
    }

    return snapshot;
}

Result<void> SaveAppSettingsUseCase::execute(const AppSettingsSnapshot& snapshot) {
    if (m_store == nullptr) {
        return std::unexpected(makeApplicationError(ErrorCategory::Internal,
                                                    QStringLiteral("internal.null_app_settings_store"),
                                                    QStringLiteral("Settings store is unavailable")));
    }

    if (!isValidEndpoint(snapshot.endpoint.trimmed())) {
        return std::unexpected(invalidSetting(QStringLiteral("input.invalid_endpoint"),
                                              QStringLiteral("Endpoint must be an HTTP or HTTPS URL or stay empty")));
    }
    if (snapshot.timeoutSeconds < kMinTimeoutSeconds || snapshot.timeoutSeconds > kMaxTimeoutSeconds) {
        return std::unexpected(invalidSetting(QStringLiteral("input.invalid_timeout"),
                                              QStringLiteral("Timeout must be between 1 and 300 seconds")));
    }
    if (snapshot.retryCount < kMinRetryCount || snapshot.retryCount > kMaxRetryCount) {
        return std::unexpected(invalidSetting(QStringLiteral("input.invalid_retry_count"),
                                              QStringLiteral("Retry count must be between 0 and 10")));
    }

    m_store->writeSetting(QString::fromLatin1(kKeyOcrModelDirectory), snapshot.ocrModelDirectory.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyDetectionModelDirectory), snapshot.detectionModelDirectory.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyInpaintModelDirectory), snapshot.inpaintModelDirectory.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyEndpoint), snapshot.endpoint.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyModelName), snapshot.modelName.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyTimeoutSeconds), snapshot.timeoutSeconds);
    m_store->writeSetting(QString::fromLatin1(kKeyRetryCount), snapshot.retryCount);
    m_store->writeSetting(QString::fromLatin1(kKeyTheme), themeToString(snapshot.theme));

    return {};
}

} // namespace manga::application
