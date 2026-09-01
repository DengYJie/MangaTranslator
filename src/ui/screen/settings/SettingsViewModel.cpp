#include "ui/screen/settings/SettingsViewModel.h"

#include "ui/error/RecoveryPresenter.h"

#include <QColor>
#include <QUrl>

#include <utility>

namespace manga::ui::screen::settings {

namespace {

constexpr int kMinRecentProjectLimit = 1;
constexpr int kMaxRecentProjectLimit = 50;
constexpr int kMinTimeoutSeconds = 1;
constexpr int kMaxTimeoutSeconds = 300;
constexpr int kMinRetryCount = 0;
constexpr int kMaxRetryCount = 10;

bool isValidEndpointUrl(const QString& endpoint) {
    if (endpoint.trimmed().isEmpty()) {
        return true;
    }
    const QUrl url(endpoint.trimmed(), QUrl::TolerantMode);
    return url.isValid() && !url.host().isEmpty() &&
           (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"));
}

} // namespace

SettingsViewModel::SettingsViewModel(application::IAppSettingsStore* settingsStore,
                                     application::CredentialOperations* credentialOperations,
                                     application::IOcrModelValidator* ocrModelValidator,
                                     QObject* parent)
    : BaseViewModel(parent), m_settingsStore(settingsStore), m_credentialOperations(credentialOperations),
      m_ocrModelValidator(ocrModelValidator) {
    loadSettings();
}

void SettingsViewModel::emitStateChanged() {
    Q_EMIT stateChanged(m_state);
}

void SettingsViewModel::loadSettings() {
    updateState([this](SettingsState& state) {
        if (m_settingsStore != nullptr) {
            const application::LoadAppSettingsUseCase loadSettings(m_settingsStore);
            state.persisted = loadSettings.execute();
        } else {
            state.persisted = application::AppSettingsSnapshot{};
        }
        state.fieldErrors.clear();

        refreshCredentialStatus(state);
        validateOcrModelDirectory(state);
    });
    m_lastPersistedSnapshot = m_state.persisted;
}

void SettingsViewModel::setRestoreLastProject(bool enabled) {
    updateState([enabled](SettingsState& state) { state.persisted.restoreLastProject = enabled; });
    persistSettings();
}

void SettingsViewModel::setRecentProjectLimit(int limit) {
    updateState([this, limit](SettingsState& state) {
        state.persisted.recentProjectLimit = limit;
        if (limit < kMinRecentProjectLimit || limit > kMaxRecentProjectLimit) {
            state.fieldErrors.insert(
                QStringLiteral("recentProjectLimit"),
                tr("最近项目数量必须在 %1 到 %2 之间").arg(kMinRecentProjectLimit).arg(kMaxRecentProjectLimit));
        } else {
            state.fieldErrors.remove(QStringLiteral("recentProjectLimit"));
        }
    });
    persistSettings();
}

void SettingsViewModel::setOcrModelDirectory(const QString& directory) {
    updateState([this, directory](SettingsState& state) {
        state.persisted.ocrModelDirectory = directory;
        validateOcrModelDirectory(state);
    });
    persistSettings();
}

void SettingsViewModel::setOcrLanguage(const QString& language) {
    updateState([this, language](SettingsState& state) {
        state.persisted.ocrLanguage = language;
        if (language.trimmed().isEmpty()) {
            state.fieldErrors.insert(QStringLiteral("ocrLanguage"), tr("OCR 语言不能为空"));
        } else {
            state.fieldErrors.remove(QStringLiteral("ocrLanguage"));
        }
    });
    persistSettings();
}

void SettingsViewModel::setProviderId(const QString& providerId) {
    updateState([this, providerId](SettingsState& state) {
        state.persisted.providerId = providerId;
        if (providerId.trimmed().isEmpty()) {
            state.fieldErrors.insert(QStringLiteral("providerId"), tr("服务商不能为空"));
        } else {
            state.fieldErrors.remove(QStringLiteral("providerId"));
        }
        refreshCredentialStatus(state);
    });
    persistSettings();
}

void SettingsViewModel::setEndpoint(const QString& endpoint) {
    updateState([this, endpoint](SettingsState& state) {
        state.persisted.endpoint = endpoint;
        if (!isValidEndpointUrl(endpoint)) {
            state.fieldErrors.insert(QStringLiteral("endpoint"), tr("API 端点必须是有效的 HTTP 或 HTTPS URL 或留空"));
        } else {
            state.fieldErrors.remove(QStringLiteral("endpoint"));
        }
    });
    persistSettings();
}

void SettingsViewModel::setModelName(const QString& modelName) {
    updateState([modelName](SettingsState& state) { state.persisted.modelName = modelName; });
    persistSettings();
}

void SettingsViewModel::setTimeoutSeconds(int seconds) {
    updateState([this, seconds](SettingsState& state) {
        state.persisted.timeoutSeconds = seconds;
        if (seconds < kMinTimeoutSeconds || seconds > kMaxTimeoutSeconds) {
            state.fieldErrors.insert(
                QStringLiteral("timeoutSeconds"),
                tr("超时时间必须在 %1 到 %2 秒之间").arg(kMinTimeoutSeconds).arg(kMaxTimeoutSeconds));
        } else {
            state.fieldErrors.remove(QStringLiteral("timeoutSeconds"));
        }
    });
    persistSettings();
}

void SettingsViewModel::setRetryCount(int count) {
    updateState([this, count](SettingsState& state) {
        state.persisted.retryCount = count;
        if (count < kMinRetryCount || count > kMaxRetryCount) {
            state.fieldErrors.insert(QStringLiteral("retryCount"),
                                     tr("重试次数必须在 %1 到 %2 之间").arg(kMinRetryCount).arg(kMaxRetryCount));
        } else {
            state.fieldErrors.remove(QStringLiteral("retryCount"));
        }
    });
    persistSettings();
}

void SettingsViewModel::setCredential(application::SecretBytes key) {
    if (m_credentialOperations != nullptr) {
        const auto res = m_credentialOperations->set(m_state.persisted.providerId, std::move(key));
        if (!res) {
            updateState([&res](SettingsState& state) {
                state.fieldErrors.insert(QStringLiteral("credential"),
                                         ui::error::RecoveryPresenter::localizedSafeMessage(res.error()));
            });
        }
    }
    updateState([this](SettingsState& state) { refreshCredentialStatus(state); });
}

void SettingsViewModel::removeCredential() {
    if (m_credentialOperations != nullptr) {
        const auto res = m_credentialOperations->clear(m_state.persisted.providerId);
        if (!res) {
            updateState([&res](SettingsState& state) {
                state.fieldErrors.insert(QStringLiteral("credential"),
                                         ui::error::RecoveryPresenter::localizedSafeMessage(res.error()));
            });
        }
    }
    updateState([this](SettingsState& state) { refreshCredentialStatus(state); });
}

void SettingsViewModel::setTheme(application::AppTheme theme) {
    updateState([theme](SettingsState& state) { state.persisted.theme = theme; });
    persistSettings();
    Q_EMIT themePreviewRequested(theme);
}

void SettingsViewModel::setDensity(application::InterfaceDensity density) {
    updateState([density](SettingsState& state) { state.persisted.density = density; });
    persistSettings();
}

void SettingsViewModel::setReduceMotion(bool reduce) {
    updateState([reduce](SettingsState& state) { state.persisted.reduceMotion = reduce; });
    persistSettings();
}

void SettingsViewModel::setCanvasBackground(const QString& color) {
    updateState([this, color](SettingsState& state) {
        state.persisted.canvasBackground = color;
        if (!QColor::isValidColorName(color.trimmed())) {
            state.fieldErrors.insert(QStringLiteral("canvasBackground"), tr("画布背景必须是有效的颜色代码"));
        } else {
            state.fieldErrors.remove(QStringLiteral("canvasBackground"));
        }
    });
    persistSettings();
}

void SettingsViewModel::restoreDefaults() {
    const application::AppSettingsSnapshot defaults;
    updateState([this, &defaults](SettingsState& state) {
        state.persisted = defaults;
        state.fieldErrors.clear();
        validateOcrModelDirectory(state);
    });
    persistSettings();
    Q_EMIT themePreviewRequested(defaults.theme);
}

void SettingsViewModel::persistSettings() {
    if (m_settingsStore == nullptr) {
        updateState([this](SettingsState& state) {
            state.fieldErrors.insert(QStringLiteral("persist"), tr("设置存储不可用，无法保存更改"));
        });
        return;
    }

    const auto hasValidationError = [this](const QString& field) { return m_state.fieldErrors.contains(field); };
    auto snapshot = m_state.persisted;
    if (hasValidationError(QStringLiteral("recentProjectLimit"))) {
        snapshot.recentProjectLimit = m_lastPersistedSnapshot.recentProjectLimit;
    }
    if (hasValidationError(QStringLiteral("ocrLanguage"))) {
        snapshot.ocrLanguage = m_lastPersistedSnapshot.ocrLanguage;
    }
    if (hasValidationError(QStringLiteral("providerId"))) {
        snapshot.providerId = m_lastPersistedSnapshot.providerId;
    }
    if (hasValidationError(QStringLiteral("endpoint"))) {
        snapshot.endpoint = m_lastPersistedSnapshot.endpoint;
    }
    if (hasValidationError(QStringLiteral("timeoutSeconds"))) {
        snapshot.timeoutSeconds = m_lastPersistedSnapshot.timeoutSeconds;
    }
    if (hasValidationError(QStringLiteral("retryCount"))) {
        snapshot.retryCount = m_lastPersistedSnapshot.retryCount;
    }
    if (hasValidationError(QStringLiteral("canvasBackground"))) {
        snapshot.canvasBackground = m_lastPersistedSnapshot.canvasBackground;
    }

    application::SaveAppSettingsUseCase saveUseCase(m_settingsStore);
    const auto result = saveUseCase.execute(snapshot);

    if (!result) {
        if (!m_state.fieldErrors.contains(QStringLiteral("recentProjectLimit")) &&
            !m_state.fieldErrors.contains(QStringLiteral("ocrLanguage")) &&
            !m_state.fieldErrors.contains(QStringLiteral("providerId")) &&
            !m_state.fieldErrors.contains(QStringLiteral("endpoint")) &&
            !m_state.fieldErrors.contains(QStringLiteral("timeoutSeconds")) &&
            !m_state.fieldErrors.contains(QStringLiteral("retryCount")) &&
            !m_state.fieldErrors.contains(QStringLiteral("canvasBackground"))) {
            updateState([&result](SettingsState& state) {
                state.fieldErrors.insert(QStringLiteral("persist"),
                                         ui::error::RecoveryPresenter::localizedSafeMessage(result.error()));
            });
        }
    } else if (m_state.fieldErrors.contains(QStringLiteral("persist"))) {
        m_lastPersistedSnapshot = snapshot;
        updateState([](SettingsState& state) { state.fieldErrors.remove(QStringLiteral("persist")); });
    } else {
        m_lastPersistedSnapshot = snapshot;
    }
}

void SettingsViewModel::refreshCredentialStatus(SettingsState& state) {
    if (m_credentialOperations == nullptr) {
        state.credentialStatus = application::CredentialState::Unavailable;
        return;
    }
    const auto result = m_credentialOperations->status(state.persisted.providerId);
    state.credentialStatus = result ? result->state : application::CredentialState::Unavailable;
}

void SettingsViewModel::validateOcrModelDirectory(SettingsState& state) {
    const QString dir = state.persisted.ocrModelDirectory.trimmed();
    if (dir.isEmpty() || m_ocrModelValidator == nullptr) {
        state.modelStatus = application::OcrModelStatus{};
        state.fieldErrors.remove(QStringLiteral("ocrModelDirectory"));
        return;
    }

    const auto result = m_ocrModelValidator->validate(dir);
    if (result) {
        state.modelStatus = *result;
        state.fieldErrors.remove(QStringLiteral("ocrModelDirectory"));
    } else {
        state.modelStatus = application::OcrModelStatus{.isValid = false, .modelDirectory = dir};
        state.fieldErrors.insert(QStringLiteral("ocrModelDirectory"),
                                 ui::error::RecoveryPresenter::localizedSafeMessage(result.error()));
    }
}

} // namespace manga::ui::screen::settings
