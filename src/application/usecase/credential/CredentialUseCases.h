#pragma once

#include "application/error/ApplicationError.h"
#include "application/ports/ICredentialStore.h"

#include <QString>

#include <utility>

namespace manga::application {

struct CredentialStatusRequest {
    QString providerId;
};

enum class CredentialState {
    Configured,
    Missing,
    Unavailable
};

struct CredentialStatusResult {
    CredentialState state;
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

        auto result = m_store->hasApiKey(request.providerId.trimmed());
        if (!result) {
            return CredentialStatusResult{CredentialState::Unavailable};
        }

        return CredentialStatusResult{*result ? CredentialState::Configured : CredentialState::Missing};
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

/// Application-facing credential boundary for ViewModels; storage remains an infrastructure concern.
class CredentialOperations {
public:
    explicit CredentialOperations(ICredentialStore* store) : m_store(store) {}

    [[nodiscard]] Result<CredentialStatusResult> status(const QString& providerId) const {
        return CheckCredentialStatusUseCase(m_store).execute({providerId});
    }
    [[nodiscard]] Result<void> set(QString providerId, SecretBytes key) const {
        return SetCredentialUseCase(m_store).execute({std::move(providerId), std::move(key)});
    }
    [[nodiscard]] Result<void> clear(const QString& providerId) const {
        return ClearCredentialUseCase(m_store).execute({providerId});
    }

private:
    ICredentialStore* m_store;
};

} // namespace manga::application
