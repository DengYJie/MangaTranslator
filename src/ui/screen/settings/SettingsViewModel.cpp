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
        state.draft = state.persisted;
        state.fieldErrors.clear();
        state.dirtyCategories.clear();
        state.saveError.reset();
        state.isSaving = false;

        refreshCredentialStatus(state);
        validateOcrModelDirectory(state);
    });
}

void SettingsViewModel::setCurrentCategory(SettingsCategory category) {
    updateState([category](SettingsState& state) { state.currentCategory = category; });
}

void SettingsViewModel::setScrollPosition(SettingsCategory category, int scrollY) {
    updateState([category, scrollY](SettingsState& state) { state.scrollPositions[category] = scrollY; });
}

void SettingsViewModel::setRestoreLastProject(bool enabled) {
    updateState([this, enabled](SettingsState& state) {
        state.draft.restoreLastProject = enabled;
        recomputeDirty(state);
    });
}

void SettingsViewModel::setRecentProjectLimit(int limit) {
    updateState([this, limit](SettingsState& state) {
        state.draft.recentProjectLimit = limit;
        if (limit < kMinRecentProjectLimit || limit > kMaxRecentProjectLimit) {
            state.fieldErrors.insert(
                QStringLiteral("recentProjectLimit"),
                tr("最近项目数量必须在 %1 到 %2 之间").arg(kMinRecentProjectLimit).arg(kMaxRecentProjectLimit));
        } else {
            state.fieldErrors.remove(QStringLiteral("recentProjectLimit"));
        }
        recomputeDirty(state);
    });
}

void SettingsViewModel::setOcrModelDirectory(const QString& directory) {
    updateState([this, directory](SettingsState& state) {
        state.draft.ocrModelDirectory = directory;
        validateOcrModelDirectory(state);
        recomputeDirty(state);
    });
}

void SettingsViewModel::setOcrLanguage(const QString& language) {
    updateState([this, language](SettingsState& state) {
        state.draft.ocrLanguage = language;
        if (language.trimmed().isEmpty()) {
            state.fieldErrors.insert(QStringLiteral("ocrLanguage"), tr("OCR 语言不能为空"));
        } else {
            state.fieldErrors.remove(QStringLiteral("ocrLanguage"));
        }
        recomputeDirty(state);
    });
}

void SettingsViewModel::setProviderId(const QString& providerId) {
    updateState([this, providerId](SettingsState& state) {
        state.draft.providerId = providerId;
        if (providerId.trimmed().isEmpty()) {
            state.fieldErrors.insert(QStringLiteral("providerId"), tr("服务商不能为空"));
        } else {
            state.fieldErrors.remove(QStringLiteral("providerId"));
        }
        refreshCredentialStatus(state);
        recomputeDirty(state);
    });
}

void SettingsViewModel::setEndpoint(const QString& endpoint) {
    updateState([this, endpoint](SettingsState& state) {
        state.draft.endpoint = endpoint;
        if (!isValidEndpointUrl(endpoint)) {
            state.fieldErrors.insert(QStringLiteral("endpoint"), tr("API 端点必须是有效的 HTTP 或 HTTPS URL 或留空"));
        } else {
            state.fieldErrors.remove(QStringLiteral("endpoint"));
        }
        recomputeDirty(state);
    });
}

void SettingsViewModel::setModelName(const QString& modelName) {
    updateState([this, modelName](SettingsState& state) {
        state.draft.modelName = modelName;
        recomputeDirty(state);
    });
}

void SettingsViewModel::setTimeoutSeconds(int seconds) {
    updateState([this, seconds](SettingsState& state) {
        state.draft.timeoutSeconds = seconds;
        if (seconds < kMinTimeoutSeconds || seconds > kMaxTimeoutSeconds) {
            state.fieldErrors.insert(
                QStringLiteral("timeoutSeconds"),
                tr("超时时间必须在 %1 到 %2 秒之间").arg(kMinTimeoutSeconds).arg(kMaxTimeoutSeconds));
        } else {
            state.fieldErrors.remove(QStringLiteral("timeoutSeconds"));
        }
        recomputeDirty(state);
    });
}

void SettingsViewModel::setRetryCount(int count) {
    updateState([this, count](SettingsState& state) {
        state.draft.retryCount = count;
        if (count < kMinRetryCount || count > kMaxRetryCount) {
            state.fieldErrors.insert(QStringLiteral("retryCount"),
                                     tr("重试次数必须在 %1 到 %2 之间").arg(kMinRetryCount).arg(kMaxRetryCount));
        } else {
            state.fieldErrors.remove(QStringLiteral("retryCount"));
        }
        recomputeDirty(state);
    });
}

void SettingsViewModel::setCredential(application::SecretBytes key) {
    if (m_credentialOperations != nullptr) {
        const auto res = m_credentialOperations->set(m_state.draft.providerId, std::move(key));
        if (!res) {
            updateState([&res](SettingsState& state) {
                state.saveError = ui::error::RecoveryPresenter::localizedSafeMessage(res.error());
            });
        }
    }
    updateState([this](SettingsState& state) { refreshCredentialStatus(state); });
}

void SettingsViewModel::removeCredential() {
    if (m_credentialOperations != nullptr) {
        const auto res = m_credentialOperations->clear(m_state.draft.providerId);
        if (!res) {
            updateState([&res](SettingsState& state) {
                state.saveError = ui::error::RecoveryPresenter::localizedSafeMessage(res.error());
            });
        }
    }
    updateState([this](SettingsState& state) { refreshCredentialStatus(state); });
}

void SettingsViewModel::setTheme(application::AppTheme theme) {
    updateState([this, theme](SettingsState& state) {
        state.draft.theme = theme;
        recomputeDirty(state);
    });
    Q_EMIT themePreviewRequested(theme);
}

void SettingsViewModel::setDensity(application::InterfaceDensity density) {
    updateState([this, density](SettingsState& state) {
        state.draft.density = density;
        recomputeDirty(state);
    });
}

void SettingsViewModel::setReduceMotion(bool reduce) {
    updateState([this, reduce](SettingsState& state) {
        state.draft.reduceMotion = reduce;
        recomputeDirty(state);
    });
}

void SettingsViewModel::setCanvasBackground(const QString& color) {
    updateState([this, color](SettingsState& state) {
        state.draft.canvasBackground = color;
        if (!QColor::isValidColorName(color.trimmed())) {
            state.fieldErrors.insert(QStringLiteral("canvasBackground"), tr("画布背景必须是有效的颜色代码"));
        } else {
            state.fieldErrors.remove(QStringLiteral("canvasBackground"));
        }
        recomputeDirty(state);
    });
}

void SettingsViewModel::saveSettings() {
    QString firstErrorField;
    SettingsCategory firstErrorCategory = SettingsCategory::General;

    bool valid = false;
    updateState([this, &valid, &firstErrorField, &firstErrorCategory](SettingsState& state) {
        valid = validateDraft(state, &firstErrorField, &firstErrorCategory);
        if (!valid) {
            state.saveError = tr("设置包含无效输入，请修正后重试。");
            state.currentCategory = firstErrorCategory;
        } else {
            state.saveError.reset();
        }
    });

    if (!valid) {
        if (!firstErrorField.isEmpty()) {
            Q_EMIT focusFieldRequested(firstErrorField);
        }
        return;
    }

    if (m_settingsStore == nullptr) {
        updateState([](SettingsState& state) { state.saveError = tr("无法保存设置：存储服务不可用。"); });
        return;
    }

    application::SaveAppSettingsUseCase saveUseCase(m_settingsStore);
    const auto result = saveUseCase.execute(m_state.draft);

    if (result) {
        updateState([this](SettingsState& state) {
            state.persisted = state.draft;
            state.dirtyCategories.clear();
            state.fieldErrors.clear();
            state.saveError.reset();
            state.isSaving = false;
        });
        Q_EMIT settingsSaved();
    } else {
        updateState([&result](SettingsState& state) {
            state.saveError = ui::error::RecoveryPresenter::localizedSafeMessage(result.error());
            state.isSaving = false;
        });
    }
}

void SettingsViewModel::restoreDefaults() {
    const application::AppSettingsSnapshot defaults;
    updateState([this, &defaults](SettingsState& state) {
        state.draft = defaults;
        state.fieldErrors.clear();
        state.saveError.reset();
        recomputeDirty(state);
        validateOcrModelDirectory(state);
    });
    Q_EMIT themePreviewRequested(defaults.theme);
}

void SettingsViewModel::revertDraft() {
    updateState([this](SettingsState& state) {
        state.draft = state.persisted;
        state.dirtyCategories.clear();
        state.fieldErrors.clear();
        state.saveError.reset();
        refreshCredentialStatus(state);
        validateOcrModelDirectory(state);
    });
    Q_EMIT themePreviewRequested(m_state.persisted.theme);
}

void SettingsViewModel::requestLeave(const std::function<void(bool proceed)>& callback) {
    if (!m_state.isDirty()) {
        if (callback) {
            callback(true);
        }
        return;
    }
    Q_EMIT leaveDecisionRequested(callback);
}

void SettingsViewModel::confirmLeaveWithSave(const std::function<void(bool proceed)>& callback) {
    saveSettings();
    const bool success = !m_state.isDirty();
    if (callback) {
        callback(success);
    }
}

void SettingsViewModel::confirmLeaveWithoutSave(const std::function<void(bool proceed)>& callback) {
    revertDraft();
    if (callback) {
        callback(true);
    }
}

void SettingsViewModel::recomputeDirty(SettingsState& state) {
    state.dirtyCategories.clear();

    const auto& d = state.draft;
    const auto& p = state.persisted;

    if (d.restoreLastProject != p.restoreLastProject || d.recentProjectLimit != p.recentProjectLimit) {
        state.dirtyCategories.insert(SettingsCategory::General);
    }
    if (d.ocrModelDirectory != p.ocrModelDirectory || d.ocrLanguage != p.ocrLanguage) {
        state.dirtyCategories.insert(SettingsCategory::Ocr);
    }
    if (d.providerId != p.providerId || d.endpoint != p.endpoint || d.modelName != p.modelName ||
        d.timeoutSeconds != p.timeoutSeconds || d.retryCount != p.retryCount) {
        state.dirtyCategories.insert(SettingsCategory::AiTranslation);
    }
    if (d.theme != p.theme || d.density != p.density || d.reduceMotion != p.reduceMotion ||
        d.canvasBackground != p.canvasBackground) {
        state.dirtyCategories.insert(SettingsCategory::Appearance);
    }
}

bool SettingsViewModel::validateDraft(SettingsState& state,
                                      QString* outFirstErrorField,
                                      SettingsCategory* outFirstErrorCategory) {
    state.fieldErrors.clear();

    const auto& d = state.draft;

    auto recordError = [&state, outFirstErrorField, outFirstErrorCategory](
                           const QString& field, const QString& message, SettingsCategory category) {
        state.fieldErrors.insert(field, message);
        if (outFirstErrorField && outFirstErrorField->isEmpty()) {
            *outFirstErrorField = field;
            if (outFirstErrorCategory) {
                *outFirstErrorCategory = category;
            }
        }
    };

    if (d.recentProjectLimit < kMinRecentProjectLimit || d.recentProjectLimit > kMaxRecentProjectLimit) {
        recordError(QStringLiteral("recentProjectLimit"),
                    tr("最近项目数量必须在 %1 到 %2 之间").arg(kMinRecentProjectLimit).arg(kMaxRecentProjectLimit),
                    SettingsCategory::General);
    }

    if (d.ocrLanguage.trimmed().isEmpty()) {
        recordError(QStringLiteral("ocrLanguage"), tr("OCR 语言不能为空"), SettingsCategory::Ocr);
    }

    if (d.providerId.trimmed().isEmpty()) {
        recordError(QStringLiteral("providerId"), tr("服务商不能为空"), SettingsCategory::AiTranslation);
    }

    if (!isValidEndpointUrl(d.endpoint)) {
        recordError(QStringLiteral("endpoint"),
                    tr("API 端点必须是有效的 HTTP 或 HTTPS URL 或留空"),
                    SettingsCategory::AiTranslation);
    }

    if (d.timeoutSeconds < kMinTimeoutSeconds || d.timeoutSeconds > kMaxTimeoutSeconds) {
        recordError(QStringLiteral("timeoutSeconds"),
                    tr("超时时间必须在 %1 到 %2 秒之间").arg(kMinTimeoutSeconds).arg(kMaxTimeoutSeconds),
                    SettingsCategory::AiTranslation);
    }

    if (d.retryCount < kMinRetryCount || d.retryCount > kMaxRetryCount) {
        recordError(QStringLiteral("retryCount"),
                    tr("重试次数必须在 %1 到 %2 之间").arg(kMinRetryCount).arg(kMaxRetryCount),
                    SettingsCategory::AiTranslation);
    }

    if (!QColor::isValidColorName(d.canvasBackground.trimmed())) {
        recordError(
            QStringLiteral("canvasBackground"), tr("画布背景必须是有效的颜色代码"), SettingsCategory::Appearance);
    }

    return state.fieldErrors.isEmpty();
}

void SettingsViewModel::refreshCredentialStatus(SettingsState& state) {
    if (m_credentialOperations == nullptr) {
        state.credentialStatus = application::CredentialState::Unavailable;
        return;
    }
    const auto result = m_credentialOperations->status(state.draft.providerId);
    state.credentialStatus = result ? result->state : application::CredentialState::Unavailable;
}

void SettingsViewModel::validateOcrModelDirectory(SettingsState& state) {
    const QString dir = state.draft.ocrModelDirectory.trimmed();
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
