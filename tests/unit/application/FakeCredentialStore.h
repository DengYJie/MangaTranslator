#pragma once

#include "application/ports/ICredentialStore.h"

#include <QHash>

namespace manga::application::test {

class FakeCredentialStore : public ICredentialStore {
public:
    ApplicationError nextError;
    bool shouldFail{false};
    int readCount{0};
    int writeCount{0};
    int deleteCount{0};

    [[nodiscard]] Result<SecretBytes> readApiKey(const QString& providerId) override {
        ++readCount;
        if (shouldFail) {
            return std::unexpected(nextError);
        }
        const auto it = m_keys.constFind(providerId);
        if (it == m_keys.cend()) {
            return std::unexpected(makeApplicationError(
                ErrorCategory::Network, "network.credentials_missing", "No API key is stored for this provider"));
        }
        return SecretBytes(it.value());
    }

    [[nodiscard]] Result<void> writeApiKey(const QString& providerId, SecretBytes key) override {
        ++writeCount;
        if (shouldFail) {
            return std::unexpected(nextError);
        }
        m_keys.insert(providerId, key.bytes());
        return {};
    }

    [[nodiscard]] Result<void> deleteApiKey(const QString& providerId) override {
        ++deleteCount;
        if (shouldFail) {
            return std::unexpected(nextError);
        }
        m_keys.remove(providerId);
        return {};
    }

private:
    QHash<QString, QByteArray> m_keys;
};

} // namespace manga::application::test
