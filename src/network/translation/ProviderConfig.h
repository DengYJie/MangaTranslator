#pragma once

#include "application/error/ApplicationError.h"
#include "network/ProviderId.h"

#include <QRegularExpression>
#include <QString>
#include <QUrl>

#include <cmath>

namespace manga::network {

enum class StructuredOutputMode {
    /// Chat Completions response_format json_schema with strict:true.
    JsonSchemaStrict,
    /// Provider only supports JSON mode; the response is valid JSON but not schema checked.
    JsonObject,
};

/// Whether a plain HTTP endpoint is acceptable. Production configuration is HTTPS only;
/// tests point at a loopback server and opt in explicitly.
enum class EndpointPolicy {
    HttpsOnly,
    AllowLoopbackHttp,
};

struct ProviderConfig {
    QString providerId;
    QUrl endpoint;
    QString model;
    int timeoutMs{60000};
    StructuredOutputMode structuredOutputMode{StructuredOutputMode::JsonSchemaStrict};
    int maxCompletionTokens{2048};
    double temperature{0.2};

    [[nodiscard]] bool operator==(const ProviderConfig& other) const noexcept = default;
};

constexpr int kMinTimeoutMs = 5000;
constexpr int kMaxTimeoutMs = 300000;

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

    if (!isValidProviderId(config.providerId)) {
        return invalid(QStringLiteral("input.invalid_provider"),
                       QStringLiteral("Provider ID must match [a-z0-9][a-z0-9._-]{0,63}"));
    }
    if (!config.endpoint.isValid() || config.endpoint.isEmpty() || config.endpoint.isRelative()) {
        return invalid(QStringLiteral("input.invalid_endpoint"), QStringLiteral("A full endpoint URL is required"));
    }
    const QString scheme = config.endpoint.scheme().toLower();
    if (scheme == QStringLiteral("https")) {
        // Allowed.
    } else if (scheme == QStringLiteral("http") && policy == EndpointPolicy::AllowLoopbackHttp &&
               isLoopbackHost(config.endpoint.host())) {
        // Allowed for loopback tests only.
    } else {
        return invalid(QStringLiteral("input.invalid_endpoint"), QStringLiteral("The endpoint must use HTTPS"));
    }
    if (!config.endpoint.userInfo().isEmpty() || config.endpoint.hasFragment()) {
        return invalid(QStringLiteral("input.invalid_endpoint"),
                       QStringLiteral("The endpoint must not contain user info or a fragment"));
    }
    if (config.model.trimmed().isEmpty()) {
        return invalid(QStringLiteral("input.invalid_model"), QStringLiteral("A model name is required"));
    }
    if (config.timeoutMs < kMinTimeoutMs || config.timeoutMs > kMaxTimeoutMs) {
        return invalid(QStringLiteral("input.invalid_timeout"), QStringLiteral("The timeout is out of range"));
    }
    if (config.maxCompletionTokens <= 0) {
        return invalid(QStringLiteral("input.invalid_token_limit"),
                       QStringLiteral("The completion token limit must be positive"));
    }
    if (!std::isfinite(config.temperature) || config.temperature < 0.0 || config.temperature > 2.0) {
        return invalid(QStringLiteral("input.invalid_temperature"),
                       QStringLiteral("The temperature must be within [0, 2]"));
    }
    return {};
}

} // namespace manga::network
