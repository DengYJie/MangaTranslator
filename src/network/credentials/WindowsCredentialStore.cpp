#include "network/credentials/WindowsCredentialStore.h"

#include "network/ProviderId.h"

#include <utility>

// clang-format off
#include <windows.h>
#include <wincred.h>
// clang-format on

namespace manga::network {
namespace {

/// Frees a credential buffer exactly once, however the enclosing scope exits.
class CredentialHandle final {
public:
    CredentialHandle() = default;
    CredentialHandle(const CredentialHandle&) = delete;
    CredentialHandle& operator=(const CredentialHandle&) = delete;
    ~CredentialHandle() {
        reset();
    }

    [[nodiscard]] PCREDENTIALW* out() noexcept {
        return &m_credential;
    }
    [[nodiscard]] PCREDENTIALW get() const noexcept {
        return m_credential;
    }

    void reset() noexcept {
        if (m_credential != nullptr) {
            CredFree(m_credential);
            m_credential = nullptr;
        }
    }

private:
    PCREDENTIALW m_credential{nullptr};
};

application::ApplicationError
credentialFailure(const QString& code,
                  const QString& message,
                  unsigned long winError,
                  application::RecoveryAction recovery = application::RecoveryAction::None) {
    auto error = application::makeApplicationError(application::ErrorCategory::Network, code, message, recovery);
    // The diagnostic carries the numeric Win32 code only; no target names or key material.
    error.diagnostic = QStringLiteral("win32:%1").arg(winError);
    return error;
}

application::ApplicationError mapCredentialError(unsigned long winError) {
    switch (winError) {
        case ERROR_NOT_FOUND:
            return credentialFailure(QStringLiteral("network.credentials_missing"),
                                     QStringLiteral("No API key is stored for this provider"),
                                     winError,
                                     application::RecoveryAction::Reauthenticate);
        case ERROR_NO_SUCH_LOGON_SESSION:
            return credentialFailure(QStringLiteral("network.credentials_unavailable"),
                                     QStringLiteral("The credential store is unavailable for this logon session"),
                                     winError,
                                     application::RecoveryAction::OpenSettings);
        case ERROR_ACCESS_DENIED:
            return credentialFailure(QStringLiteral("network.credentials_access_denied"),
                                     QStringLiteral("Access to the stored API key was denied"),
                                     winError,
                                     application::RecoveryAction::OpenSettings);
        default:
            return credentialFailure(QStringLiteral("network.credentials_failed"),
                                     QStringLiteral("The credential store could not be used"),
                                     winError,
                                     application::RecoveryAction::OpenSettings);
    }
}

application::ApplicationError invalidProviderError() {
    return application::makeApplicationError(application::ErrorCategory::Input,
                                             QStringLiteral("input.invalid_provider"),
                                             QStringLiteral("Provider ID must match [a-z0-9][a-z0-9._-]{0,63}"));
}

} // namespace

application::Result<bool> WindowsCredentialStore::hasApiKey(const QString& providerId) {
    if (!isValidProviderId(providerId)) {
        return std::unexpected(invalidProviderError());
    }
    const QString target = credentialTargetName(providerId);
    const QString filter = target + "*";
    unsigned long count = 0;
    PCREDENTIALW* credentials = nullptr;
    // Enumerating names avoids touching CredentialBlob, so a status query never loads the
    // secret into this process.
    const BOOL enumerated = CredEnumerateW(reinterpret_cast<LPCWSTR>(filter.utf16()), 0, &count, &credentials);
    if (enumerated == FALSE) {
        const unsigned long winError = GetLastError();
        if (winError == ERROR_NOT_FOUND) {
            return false;
        }
        return std::unexpected(mapCredentialError(winError));
    }
    bool found = false;
    for (unsigned long index = 0; index < count; ++index) {
        if (credentials[index] != nullptr && credentials[index]->TargetName != nullptr &&
            QString::fromWCharArray(credentials[index]->TargetName) == target) {
            found = true;
            break;
        }
    }
    CredFree(credentials);
    return found;
}

application::Result<application::SecretBytes> WindowsCredentialStore::readApiKey(const QString& providerId) {
    if (!isValidProviderId(providerId)) {
        return std::unexpected(invalidProviderError());
    }
    const QString target = credentialTargetName(providerId);
    CredentialHandle handle;
    const BOOL read = CredReadW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0, handle.out());
    if (read == FALSE) {
        return std::unexpected(mapCredentialError(GetLastError()));
    }
    PCREDENTIALW credential = handle.get();
    if (credential == nullptr || credential->CredentialBlob == nullptr || credential->CredentialBlobSize == 0) {
        return std::unexpected(credentialFailure(QStringLiteral("network.credentials_missing"),
                                                 QStringLiteral("The stored API key is empty"),
                                                 0,
                                                 application::RecoveryAction::Reauthenticate));
    }
    QByteArray key(reinterpret_cast<const char*>(credential->CredentialBlob),
                   static_cast<qsizetype>(credential->CredentialBlobSize));
    return application::SecretBytes(std::move(key));
}

application::Result<void> WindowsCredentialStore::writeApiKey(const QString& providerId, application::SecretBytes key) {
    if (!isValidProviderId(providerId)) {
        return std::unexpected(invalidProviderError());
    }
    const QByteArray& secret = key.bytes();
    if (secret.isEmpty()) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Input,
                                                                 QStringLiteral("input.empty_credential"),
                                                                 QStringLiteral("Credential key cannot be empty")));
    }
    if (secret.contains('\0')) {
        return std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Input,
                                              QStringLiteral("input.invalid_credential"),
                                              QStringLiteral("Credential key must not contain null bytes")));
    }
    if (static_cast<size_t>(secret.size()) > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        return std::unexpected(application::makeApplicationError(application::ErrorCategory::Input,
                                                                 QStringLiteral("input.credential_too_large"),
                                                                 QStringLiteral("Credential key is too large")));
    }

    const QString target = credentialTargetName(providerId);
    const QString userName = QStringLiteral("ApiKey");
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(target.utf16()));
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(secret.constData()));
    // V0.1 keeps credentials available to this user on this machine only; no roaming.
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(reinterpret_cast<LPCWSTR>(userName.utf16()));
    if (CredWriteW(&credential, 0) == FALSE) {
        return std::unexpected(mapCredentialError(GetLastError()));
    }
    return {};
}

application::Result<void> WindowsCredentialStore::deleteApiKey(const QString& providerId) {
    if (!isValidProviderId(providerId)) {
        return std::unexpected(invalidProviderError());
    }
    const QString target = credentialTargetName(providerId);
    if (CredDeleteW(reinterpret_cast<LPCWSTR>(target.utf16()), CRED_TYPE_GENERIC, 0) == FALSE) {
        const unsigned long winError = GetLastError();
        // Deleting an absent credential is already the desired end state.
        if (winError == ERROR_NOT_FOUND) {
            return {};
        }
        return std::unexpected(mapCredentialError(winError));
    }
    return {};
}

} // namespace manga::network
