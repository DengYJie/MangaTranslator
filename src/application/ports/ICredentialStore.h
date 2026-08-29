#pragma once

#include "application/error/ApplicationError.h"

#include <QByteArray>
#include <QString>

#include <utility>

namespace manga::application {

/// Move-only byte container for secrets. The buffer is overwritten before destruction and
/// before a move transfers ownership so plaintext key material keeps a minimal lifetime with
/// no accidental copies. Qt or OS internals may still create copies that cannot be cleared,
/// so this only reduces exposure; it makes no whole-process zero-residue guarantee.
class SecretBytes final {
public:
    SecretBytes() = default;
    explicit SecretBytes(QByteArray bytes) : m_bytes(std::move(bytes)) {}

    SecretBytes(SecretBytes&& other) noexcept : m_bytes(std::move(other.m_bytes)) {
        other.zeroize();
    }
    SecretBytes& operator=(SecretBytes&& other) noexcept {
        if (this != &other) {
            zeroize();
            m_bytes = std::move(other.m_bytes);
            other.zeroize();
        }
        return *this;
    }
    SecretBytes(const SecretBytes&) = delete;
    SecretBytes& operator=(const SecretBytes&) = delete;
    ~SecretBytes() {
        zeroize();
    }

    /// Exposes the plaintext bytes for immediate consumption (e.g. building an
    /// Authorization header). The reference must not be retained, copied, or logged; the
    /// buffer is scrubbed when this container is destroyed or moved from.
    [[nodiscard]] const QByteArray& bytes() const noexcept {
        return m_bytes;
    }
    [[nodiscard]] bool isEmpty() const noexcept {
        return m_bytes.isEmpty();
    }

    void clear() {
        zeroize();
        m_bytes.clear();
    }

private:
    void zeroize() noexcept {
        // Volatile writes prevent the optimizer from removing the scrub.
        volatile char* cursor = m_bytes.data();
        const volatile char* end = m_bytes.data() + m_bytes.size();
        while (cursor < end) {
            *cursor++ = '\0';
        }
    }

    QByteArray m_bytes;
};

/// Synchronous by design: credential operations are short local calls made from the network
/// thread while a request is being prepared; GUI code and ViewModels never read secrets.
class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;

    /// Reports whether a credential exists without reading or copying the secret blob. The
    /// settings surface shows this status; only the network adapter reads the key itself.
    [[nodiscard]] virtual Result<bool> hasApiKey(const QString& providerId) = 0;
    [[nodiscard]] virtual Result<SecretBytes> readApiKey(const QString& providerId) = 0;
    [[nodiscard]] virtual Result<void> writeApiKey(const QString& providerId, SecretBytes key) = 0;
    [[nodiscard]] virtual Result<void> deleteApiKey(const QString& providerId) = 0;
};

} // namespace manga::application
