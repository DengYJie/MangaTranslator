#pragma once

#include "application/error/ApplicationError.h"
#include "network/ProviderId.h"

#include <QRegularExpression>
#include <QString>
#include <QUrl>

#include <cmath>

namespace manga::network {

enum class ProviderApiFlavor {
    /// Defaults to JSON Schema output when no output mode is selected explicitly.
    OpenAiNative,
    /// Defaults to JSON object output when no output mode is selected explicitly.
    OpenAiCompatible,
};

enum class StructuredOutputMode {
    ProviderDefault,
    JsonSchema,
    JsonObject,
};

enum class EndpointPolicy {
    HttpsOnly,
    AllowLoopbackHttp,
};

struct ProviderConfig {
    /// Identifies both the endpoint configuration and the stored credential, whose target name
    /// embeds it, so renaming an id detaches the key already saved under the old one.
    QString providerId;
    QUrl endpoint;
    QString model;
    /// Whole operation budget in milliseconds, retries included. Each attempt is given what is
    /// left of it, and a retry whose wait would overrun the remainder is not taken.
    int timeoutMs{300000};
    ProviderApiFlavor apiFlavor{ProviderApiFlavor::OpenAiCompatible};
    double temperature{0.2};
    /// Overrides the API flavor default to match the configured provider and model capabilities.
    StructuredOutputMode structuredOutputMode{StructuredOutputMode::ProviderDefault};

    [[nodiscard]] bool operator==(const ProviderConfig& other) const noexcept = default;
};

/// Admissible range, in milliseconds, for the operation budget held by ProviderConfig.
constexpr int kMinTimeoutMs = 5000;
constexpr int kMaxTimeoutMs = 600000;

[[nodiscard]] inline bool isLoopbackHost(const QString& host) {
    const QString lowered = host.toLower();
    return lowered == QStringLiteral("localhost") || lowered == QStringLiteral("127.0.0.1") ||
           lowered == QStringLiteral("::1") || lowered == QStringLiteral("[::1]");
}

[[nodiscard]] inline application::Result<void> validateProviderConfig(const ProviderConfig& config,
                                                                      EndpointPolicy policy) {
    using application::ErrorCategory;
    const auto invalid = [](QString code, QString message) {
        return std::unexpected(application::makeApplicationError(
            ErrorCategory::Input, std::move(code), std::move(message), application::RecoveryAction::OpenSettings));
    };

    if (config.providerId.trimmed().isEmpty()) {
        return invalid(QStringLiteral("input.invalid_provider"), QStringLiteral("The provider id must not be empty"));
    }
    if (!config.endpoint.isValid()) {
        return invalid(QStringLiteral("input.invalid_endpoint"), QStringLiteral("The endpoint is not a valid URL"));
    }

    const QString scheme = config.endpoint.scheme().toLower();
    if (scheme == QStringLiteral("https")) {
        // Allowed.
    } else if (scheme == QStringLiteral("http") && policy == EndpointPolicy::AllowLoopbackHttp &&
               isLoopbackHost(config.endpoint.host())) {
        // Allowed for loopback tests and local providers.
    } else {
        return invalid(QStringLiteral("input.invalid_endpoint"), QStringLiteral("The endpoint must use HTTPS"));
    }

    if (config.model.trimmed().isEmpty()) {
        return invalid(QStringLiteral("input.invalid_model"), QStringLiteral("The model name must not be empty"));
    }
    if (config.timeoutMs < kMinTimeoutMs || config.timeoutMs > kMaxTimeoutMs) {
        return invalid(QStringLiteral("input.invalid_timeout"), QStringLiteral("The timeout is out of range"));
    }
    if (!std::isfinite(config.temperature) || config.temperature < 0.0 || config.temperature > 2.0) {
        return invalid(QStringLiteral("input.invalid_temperature"),
                       QStringLiteral("The temperature must be within [0, 2]"));
    }
    return {};
}

} // namespace manga::network
