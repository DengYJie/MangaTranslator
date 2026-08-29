#pragma once

#include "application/ports/ICredentialStore.h"

namespace manga::network {

/// Windows Credential Manager backed secret store. Keys live under
/// MangaTranslator/Credential/<providerId>/ApiKey as CRED_TYPE_GENERIC entries and never
/// reach QSettings, project files, logs or screenshots. Every successful read frees the
/// credential buffer exactly once.
class WindowsCredentialStore final : public application::ICredentialStore {
public:
    [[nodiscard]] application::Result<bool> hasApiKey(const QString& providerId) override;
    [[nodiscard]] application::Result<application::SecretBytes> readApiKey(const QString& providerId) override;
    [[nodiscard]] application::Result<void> writeApiKey(const QString& providerId,
                                                        application::SecretBytes key) override;
    [[nodiscard]] application::Result<void> deleteApiKey(const QString& providerId) override;
};

} // namespace manga::network
