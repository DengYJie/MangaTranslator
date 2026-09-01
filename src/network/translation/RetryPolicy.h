#pragma once

#include "network/translation/ChatCompletionsProtocol.h"

#include <QByteArray>
#include <QDateTime>

#include <functional>

namespace manga::network {

/// First attempt plus a configurable number of retries.
constexpr int kMaxAttempts = 3;
constexpr int kMaxRetryAfterSeconds = 60;
constexpr int kMaxBackoffMs = 8000;

struct RetryDecision {
    bool shouldRetry{false};
    int delayMs{0};
};

/// Decides whether a failed attempt may be repeated and how long to wait. The clock and the
/// uniform [0, 1) RNG are injected so tests never depend on wall time.
class RetryPolicy {
public:
    using Clock = std::function<QDateTime()>;
    using Rng = std::function<double()>;

    explicit RetryPolicy(Clock clock = nullptr, Rng rng = nullptr, int maxRetries = kMaxAttempts - 1);

    /// attemptIndex is zero for the first attempt. remainingDeadlineMs is what is left of the
    /// operation deadline after this failure.
    [[nodiscard]] RetryDecision decide(const HttpFailure& failure,
                                       int attemptIndex,
                                       const QByteArray& retryAfterHeader,
                                       int remainingDeadlineMs) const;

private:
    [[nodiscard]] int jitterDelayMs(int attemptIndex) const;

    Clock m_clock;
    Rng m_rng;
    int m_maxRetries;
};

} // namespace manga::network
