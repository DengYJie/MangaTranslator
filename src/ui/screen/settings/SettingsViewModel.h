#pragma once

#include "application/dto/AppSettingsDto.h"
#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IInpaintModelValidator.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "application/usecase/settings/AppSettingsUseCases.h"
#include "ui/base/BaseViewModel.h"
#include "ui/screen/settings/SettingsState.h"

namespace manga::ui::screen::settings {

using IAppSettingsStore = ::manga::application::IAppSettingsStore;
using IOcrModelValidator = ::manga::application::IOcrModelValidator;
using IDetectionModelValidator = ::manga::application::IDetectionModelValidator;
using IInpaintModelValidator = ::manga::application::IInpaintModelValidator;
using SecretBytes = ::manga::application::SecretBytes;

/// ViewModel managing persisted settings, validation, secure credential mutations, and instant theme preview.
class SettingsViewModel : public base::BaseViewModel<SettingsViewModel, SettingsState> {
    Q_OBJECT
public:
    explicit SettingsViewModel(IAppSettingsStore* settingsStore,
                               application::CredentialOperations* credentialOperations,
                               IOcrModelValidator* ocrModelValidator,
                               IInpaintModelValidator* inpaintModelValidator = nullptr,
                               IDetectionModelValidator* detectionModelValidator = nullptr,
                               QObject* parent = nullptr);
    ~SettingsViewModel() override = default;

    // --- Navigation & Lifetime ---
    void loadSettings();

    // --- OCR & Models Category Intents ---
    void setOcrModelDirectory(const QString& directory);
    void setDetectionModelDirectory(const QString& directory);
    void setInpaintModelDirectory(const QString& directory);

    // --- AI Translation Category Intents ---
    void setEndpoint(const QString& endpoint);
    void setModelName(const QString& modelName);
    void setTimeoutSeconds(int seconds);
    void setRetryCount(int count);
    void setCredential(SecretBytes key);
    void removeCredential();

    // --- Appearance Category Intents ---
    void setTheme(AppTheme theme);

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
    void validateDetectionModelDirectory(SettingsState& state);
    void validateInpaintModelDirectory(SettingsState& state);

    IAppSettingsStore* m_settingsStore;                        // non-owning
    application::CredentialOperations* m_credentialOperations; // non-owning
    IOcrModelValidator* m_ocrModelValidator;                   // non-owning
    IInpaintModelValidator* m_inpaintModelValidator;           // non-owning
    IDetectionModelValidator* m_detectionModelValidator;       // non-owning
    application::AppSettingsSnapshot m_lastPersistedSnapshot;
};

} // namespace manga::ui::screen::settings
