#include "network/translation/RetryPolicy.h"

#include <algorithm>
#include <cmath>

namespace manga::network {
namespace {

constexpr int kBaseBackoffMs = 500;

} // namespace

RetryPolicy::RetryPolicy(Clock clock, Rng rng, int maxRetries)
    : m_clock(std::move(clock)), m_rng(std::move(rng)), m_maxRetries(std::clamp(maxRetries, 0, kMaxAttempts - 1)) {}

int RetryPolicy::jitterDelayMs(int attemptIndex) const {
    const double ceiling = static_cast<double>(kBaseBackoffMs) * std::pow(2.0, attemptIndex);
    const double bound = std::min(static_cast<double>(kMaxBackoffMs), ceiling);
    const double uniform = std::clamp(m_rng ? m_rng() : 0.0, 0.0, 1.0);
    return static_cast<int>(std::llround(uniform * bound));
}

RetryDecision RetryPolicy::decide(const HttpFailure& failure,
                                  int attemptIndex,
                                  const QByteArray& retryAfterHeader,
                                  int remainingDeadlineMs) const {
    if (!failure.retryable || attemptIndex >= m_maxRetries || remainingDeadlineMs <= 0) {
        return {};
    }
    int delayMs = jitterDelayMs(attemptIndex);
    if (!retryAfterHeader.isEmpty()) {
        const QDateTime now = m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
        if (const auto retryAfter = parseRetryAfterSeconds(retryAfterHeader, now)) {
            delayMs = *retryAfter * 1000;
        }
    }
    if (delayMs >= remainingDeadlineMs) {
        return {};
    }
    return RetryDecision{.shouldRetry = true, .delayMs = delayMs};
}

} // namespace manga::network
