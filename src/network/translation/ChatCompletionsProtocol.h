#pragma once

#include "application/dto/TranslationDto.h"
#include "application/error/ApplicationError.h"
#include "network/translation/ProviderConfig.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <optional>

namespace manga::network {

/// Largest response body the adapter will buffer and parse.
constexpr qsizetype kMaxResponseBodyBytes = 2 * 1024 * 1024;
/// Per-field limit for regionId, sourceText, translation and candidates.
constexpr qsizetype kMaxFieldBytes = 64 * 1024;
constexpr int kMaxCandidatesPerRegion = 5;

[[nodiscard]] QByteArray buildChatCompletionsRequest(const application::TranslationRequest& request,
                                                     const ProviderConfig& config);

/// Validates a Chat Completions response and projects it onto the request. Structure errors
/// and duplicate ids fail the whole batch; unknown, missing or mismatched ids pass through so
/// the application layer can apply partial adoption.
[[nodiscard]] application::Result<application::TranslationResponse> parseChatCompletionsResponse(
    const QByteArray& body, const application::TranslationRequest& request, const QString& providerId);

/// Retry-After in seconds, accepting integer seconds and HTTP-date, clamped to 0..60.
[[nodiscard]] std::optional<int> parseRetryAfterSeconds(const QByteArray& headerValue, const QDateTime& now);

struct HttpFailure {
    application::ApplicationError error;
    bool retryable{false};
};

[[nodiscard]] HttpFailure classifyHttpStatus(int status);
[[nodiscard]] HttpFailure classifyTransportFailure(int networkError, bool requestBodySent);

} // namespace manga::network
