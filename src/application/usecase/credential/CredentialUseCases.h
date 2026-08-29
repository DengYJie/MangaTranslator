#pragma once

#include "application/error/ApplicationError.h"
#include "application/ports/ICredentialStore.h"

#include <QString>

#include <utility>

namespace manga::application {

struct CredentialStatusRequest {
    QString providerId;
};

struct CredentialStatusResult {
    bool hasCredential;
};

class CheckCredentialStatusUseCase {
public:
    explicit CheckCredentialStatusUseCase(ICredentialStore* store) : m_store(store) {}

    [[nodiscard]] Result<CredentialStatusResult> execute(const CredentialStatusRequest& request) {
        if (!m_store) {
            return std::unexpected(makeApplicationError(ErrorCategory::Internal,
                                                        QStringLiteral("internal.null_credential_store"),
                                                        QStringLiteral("Credential store is unavailable")));
        }
        if (request.providerId.trimmed().isEmpty()) {
            return std::unexpected(makeApplicationError(ErrorCategory::Input,
                                                        QStringLiteral("input.invalid_provider"),
                                                        QStringLiteral("Provider ID is required")));
        }

        // The status query never reads or copies the secret blob.
        auto result = m_store->hasApiKey(request.providerId.trimmed());
        if (!result) {
            // A status failure is reported as not configured; the UI offers setup.
            return CredentialStatusResult{false};
        }

        return CredentialStatusResult{*result};
    }

private:
    ICredentialStore* m_store;
};

struct SetCredentialRequest {
    QString providerId;
    SecretBytes key;
};

class SetCredentialUseCase {
public:
    explicit SetCredentialUseCase(ICredentialStore* store) : m_store(store) {}

    [[nodiscard]] Result<void> execute(SetCredentialRequest request) {
        if (!m_store) {
            return std::unexpected(makeApplicationError(ErrorCategory::Internal,
                                                        QStringLiteral("internal.null_credential_store"),
                                                        QStringLiteral("Credential store is unavailable")));
        }
        if (request.providerId.trimmed().isEmpty()) {
            return std::unexpected(makeApplicationError(ErrorCategory::Input,
                                                        QStringLiteral("input.invalid_provider"),
                                                        QStringLiteral("Provider ID is required")));
        }
        if (request.key.isEmpty()) {
            return std::unexpected(makeApplicationError(ErrorCategory::Input,
                                                        QStringLiteral("input.empty_credential"),
                                                        QStringLiteral("Credential key cannot be empty")));
        }

        return m_store->writeApiKey(request.providerId.trimmed(), std::move(request.key));
    }

private:
    ICredentialStore* m_store;
};

struct ClearCredentialRequest {
    QString providerId;
};

class ClearCredentialUseCase {
public:
    explicit ClearCredentialUseCase(ICredentialStore* store) : m_store(store) {}

    [[nodiscard]] Result<void> execute(const ClearCredentialRequest& request) {
        if (!m_store) {
            return std::unexpected(makeApplicationError(ErrorCategory::Internal,
                                                        QStringLiteral("internal.null_credential_store"),
                                                        QStringLiteral("Credential store is unavailable")));
        }
        if (request.providerId.trimmed().isEmpty()) {
            return std::unexpected(makeApplicationError(ErrorCategory::Input,
                                                        QStringLiteral("input.invalid_provider"),
                                                        QStringLiteral("Provider ID is required")));
        }

        return m_store->deleteApiKey(request.providerId.trimmed());
    }

private:
    ICredentialStore* m_store;
};

} // namespace manga::application
