#include "AppSettingsUseCases.h"

#include <QColor>
#include <QLatin1String>
#include <QMetaType>
#include <QUrl>
#include <QVariant>

#include <algorithm>

namespace manga::application {

namespace {

constexpr auto kKeyRestoreLastProject = "general/restoreLastProject";
constexpr auto kKeyRecentProjectLimit = "general/recentProjectLimit";
constexpr auto kKeyOcrModelDirectory = "ocr/modelDirectory";
constexpr auto kKeyOcrLanguage = "ocr/language";
constexpr auto kKeyProviderId = "translation/providerId";
constexpr auto kKeyEndpoint = "translation/endpoint";
constexpr auto kKeyModelName = "translation/modelName";
constexpr auto kKeyTimeoutSeconds = "translation/timeoutSeconds";
constexpr auto kKeyRetryCount = "translation/retryCount";
constexpr auto kKeyTheme = "appearance/theme";
constexpr auto kKeyDensity = "appearance/density";
constexpr auto kKeyReduceMotion = "appearance/reduceMotion";
constexpr auto kKeyCanvasBackground = "appearance/canvasBackground";

constexpr int kMinRecentProjectLimit = 1;
constexpr int kMaxRecentProjectLimit = 50;
constexpr int kMinTimeoutSeconds = 1;
constexpr int kMaxTimeoutSeconds = 300;
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

QString densityToString(InterfaceDensity density) {
    switch (density) {
        case InterfaceDensity::Comfortable:
            return QStringLiteral("comfortable");
        case InterfaceDensity::Compact:
            break;
    }
    return QStringLiteral("compact");
}

InterfaceDensity densityFromString(const QString& value) {
    const QString lower = value.trimmed().toLower();
    if (lower == QLatin1String("comfortable")) {
        return InterfaceDensity::Comfortable;
    }
    return InterfaceDensity::Compact;
}

bool parseBoolText(const QString& text, bool* parsed) {
    static const QStringList trueTexts = {QStringLiteral("true"), QStringLiteral("yes"), QStringLiteral("1")};
    static const QStringList falseTexts = {QStringLiteral("false"), QStringLiteral("no"), QStringLiteral("0")};
    const QString lower = text.trimmed().toLower();
    if (trueTexts.contains(lower)) {
        *parsed = true;
        return true;
    }
    if (falseTexts.contains(lower)) {
        *parsed = false;
        return true;
    }
    return false;
}

bool toBoolSetting(const QVariant& value, bool fallback) {
    if (value.typeId() == QMetaType::Bool) {
        return value.toBool();
    }
    if (value.typeId() == QMetaType::QString) {
        bool parsed = false;
        if (parseBoolText(value.toString(), &parsed)) {
            return parsed;
        }
    }
    return fallback;
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

    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyRestoreLastProject)); value.has_value()) {
        snapshot.restoreLastProject = toBoolSetting(*value, snapshot.restoreLastProject);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyRecentProjectLimit)); value.has_value()) {
        snapshot.recentProjectLimit =
            toIntSetting(*value, snapshot.recentProjectLimit, kMinRecentProjectLimit, kMaxRecentProjectLimit);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyOcrModelDirectory)); value.has_value()) {
        snapshot.ocrModelDirectory = toStringSetting(*value);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyOcrLanguage)); value.has_value()) {
        const QString language = toStringSetting(*value);
        if (!language.isEmpty()) {
            snapshot.ocrLanguage = language;
        }
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyProviderId)); value.has_value()) {
        const QString providerId = toStringSetting(*value);
        if (!providerId.isEmpty()) {
            snapshot.providerId = providerId;
        }
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
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyDensity)); value.has_value()) {
        snapshot.density = densityFromString(toStringSetting(*value));
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyReduceMotion)); value.has_value()) {
        snapshot.reduceMotion = toBoolSetting(*value, snapshot.reduceMotion);
    }
    if (const auto value = m_store->readSetting(QString::fromLatin1(kKeyCanvasBackground)); value.has_value()) {
        const QString color = toStringSetting(*value);
        if (QColor::isValidColorName(color)) {
            snapshot.canvasBackground = color;
        }
    }

    return snapshot;
}

Result<void> SaveAppSettingsUseCase::execute(const AppSettingsSnapshot& snapshot) {
    if (m_store == nullptr) {
        return std::unexpected(makeApplicationError(ErrorCategory::Internal,
                                                    QStringLiteral("internal.null_app_settings_store"),
                                                    QStringLiteral("Settings store is unavailable")));
    }

    // Every rule runs before the first write, so a rejected snapshot cannot change stored settings.
    if (snapshot.recentProjectLimit < kMinRecentProjectLimit || snapshot.recentProjectLimit > kMaxRecentProjectLimit) {
        return std::unexpected(invalidSetting(QStringLiteral("input.invalid_recent_project_limit"),
                                              QStringLiteral("Recent project limit must be between 1 and 50")));
    }
    if (snapshot.ocrLanguage.trimmed().isEmpty()) {
        return std::unexpected(
            invalidSetting(QStringLiteral("input.invalid_ocr_language"), QStringLiteral("OCR language is required")));
    }
    if (snapshot.providerId.trimmed().isEmpty()) {
        return std::unexpected(
            invalidSetting(QStringLiteral("input.invalid_provider_id"), QStringLiteral("Provider is required")));
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
    if (!QColor::isValidColorName(snapshot.canvasBackground.trimmed())) {
        return std::unexpected(invalidSetting(QStringLiteral("input.invalid_canvas_background"),
                                              QStringLiteral("Canvas background must be a valid color")));
    }

    m_store->writeSetting(QString::fromLatin1(kKeyRestoreLastProject), snapshot.restoreLastProject);
    m_store->writeSetting(QString::fromLatin1(kKeyRecentProjectLimit), snapshot.recentProjectLimit);
    m_store->writeSetting(QString::fromLatin1(kKeyOcrModelDirectory), snapshot.ocrModelDirectory.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyOcrLanguage), snapshot.ocrLanguage.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyProviderId), snapshot.providerId.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyEndpoint), snapshot.endpoint.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyModelName), snapshot.modelName.trimmed());
    m_store->writeSetting(QString::fromLatin1(kKeyTimeoutSeconds), snapshot.timeoutSeconds);
    m_store->writeSetting(QString::fromLatin1(kKeyRetryCount), snapshot.retryCount);
    m_store->writeSetting(QString::fromLatin1(kKeyTheme), themeToString(snapshot.theme));
    m_store->writeSetting(QString::fromLatin1(kKeyDensity), densityToString(snapshot.density));
    m_store->writeSetting(QString::fromLatin1(kKeyReduceMotion), snapshot.reduceMotion);
    m_store->writeSetting(QString::fromLatin1(kKeyCanvasBackground), snapshot.canvasBackground.trimmed());

    return {};
}

} // namespace manga::application
