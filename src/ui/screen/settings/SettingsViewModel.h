#pragma once

#include "application/dto/AppSettingsDto.h"
#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "application/usecase/settings/AppSettingsUseCases.h"
#include "ui/base/BaseViewModel.h"
#include "ui/screen/settings/SettingsState.h"

namespace manga::ui::screen::settings {

using IAppSettingsStore = ::manga::application::IAppSettingsStore;
using IOcrModelValidator = ::manga::application::IOcrModelValidator;
using SecretBytes = ::manga::application::SecretBytes;

/// ViewModel managing persisted settings, validation, secure credential mutations,
/// instant theme preview, and category scroll positions.
class SettingsViewModel : public base::BaseViewModel<SettingsViewModel, SettingsState> {
    Q_OBJECT
public:
    explicit SettingsViewModel(IAppSettingsStore* settingsStore,
                               application::CredentialOperations* credentialOperations,
                               IOcrModelValidator* ocrModelValidator,
                               QObject* parent = nullptr);
    ~SettingsViewModel() override = default;

    // --- Navigation & Lifetime ---
    void loadSettings();
    void setCurrentCategory(SettingsCategory category);
    void setScrollPosition(SettingsCategory category, int scrollY);

    // --- General Category Intents ---
    void setRestoreLastProject(bool enabled);
    void setRecentProjectLimit(int limit);

    // --- OCR Category Intents ---
    void setOcrModelDirectory(const QString& directory);
    void setOcrLanguage(const QString& language);

    // --- AI Translation Category Intents ---
    void setProviderId(const QString& providerId);
    void setEndpoint(const QString& endpoint);
    void setModelName(const QString& modelName);
    void setTimeoutSeconds(int seconds);
    void setRetryCount(int count);
    void setCredential(SecretBytes key);
    void removeCredential();

    // --- Appearance Category Intents ---
    void setTheme(AppTheme theme);
    void setDensity(InterfaceDensity density);
    void setReduceMotion(bool reduce);
    void setCanvasBackground(const QString& color);

    // --- Actions ---
    void restoreDefaults();

Q_SIGNALS:
    void stateChanged(const SettingsState& state);
    void themePreviewRequested(AppTheme theme);
    void focusFieldRequested(const QString& fieldName);

protected:
    void emitStateChanged() override;

private:
    void persistSettings();
    void refreshCredentialStatus(SettingsState& state);
    void validateOcrModelDirectory(SettingsState& state);

    IAppSettingsStore* m_settingsStore;                        // non-owning
    application::CredentialOperations* m_credentialOperations; // non-owning
    IOcrModelValidator* m_ocrModelValidator;                   // non-owning
};

} // namespace manga::ui::screen::settings
