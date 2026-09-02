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
/// Alternates accepted for one response item. The request never asks for a count, so this bounds
/// untrusted model output rather than describing the wire format.
constexpr int kMaxCandidatesPerRegion = 5;

/// Normalizes a provider endpoint URL to include the /v1/chat/completions path if omitted.
[[nodiscard]] QUrl normalizeChatCompletionsEndpoint(QUrl endpoint);

/// Builds the Chat Completions request body for one translation batch. The surrounding key
/// spellings are the provider wire vocabulary and are documented where they are written.
[[nodiscard]] QByteArray buildChatCompletionsRequest(const application::TranslationRequest& request,
                                                     const ProviderConfig& config);

/// Requires explicit regionId and sourceText fields and a non-empty translation or targetText.
/// Rejects ambiguous fields, empty batches for non-empty requests, and duplicate ids.
/// Preserves explicit ids and source text without inferring them from array positions.
/// Leaves unknown ids, missing regions and source mismatches to application partial adoption.
[[nodiscard]] application::Result<application::TranslationResponse> parseChatCompletionsResponse(
    const QByteArray& body, const application::TranslationRequest& request, const QString& providerId);

/// Retry-After in seconds, accepting integer seconds and an HTTP-date, clamped to the ceiling the
/// retry policy publishes as kMaxRetryAfterSeconds.
[[nodiscard]] std::optional<int> parseRetryAfterSeconds(const QByteArray& headerValue, const QDateTime& now);

struct HttpFailure {
    application::ApplicationError error;
    bool retryable{false};
};

[[nodiscard]] HttpFailure classifyHttpStatus(int status);
[[nodiscard]] HttpFailure classifyTransportFailure(int networkError, bool requestBodySent);

} // namespace manga::network
