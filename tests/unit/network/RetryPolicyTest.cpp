#include "RetryPolicyTest.h"

#include "network/translation/RetryPolicy.h"

#include <QDateTime>
#include <QtTest/QtTest>

using namespace manga;
using manga::network::HttpFailure;
using manga::network::RetryPolicy;

namespace {

HttpFailure failureWith(bool retryable) {
    HttpFailure failure;
    failure.error = application::makeApplicationError(application::ErrorCategory::Network,
                                                      QStringLiteral("network.unavailable"),
                                                      QStringLiteral("failure"),
                                                      application::RecoveryAction::Retry);
    failure.retryable = retryable;
    return failure;
}

RetryPolicy policyWithRng(double uniform) {
    return RetryPolicy([]() { return QDateTime(QDate(2026, 1, 1), QTime(0, 0), Qt::UTC); },
                       [uniform]() { return uniform; });
}

} // namespace

void RetryPolicyTest::rateLimitRetriesUsingRetryAfterSeconds() {
    const auto policy = policyWithRng(0.0);
    const auto decision = policy.decide(failureWith(true), 0, QByteArrayLiteral("3"), 60000);
    QVERIFY(decision.shouldRetry);
    QCOMPARE(decision.delayMs, 3000);
}

void RetryPolicyTest::rateLimitClampsHttpDateRetryAfter() {
    const auto policy = policyWithRng(0.0);
    // Two hours in the future clamps to the 60 second ceiling.
    const auto decision =
        policy.decide(failureWith(true), 0, QByteArrayLiteral("Thu, 01 Jan 2026 02:00:00 GMT"), 600000);
    QVERIFY(decision.shouldRetry);
    QCOMPARE(decision.delayMs, 60000);
}

void RetryPolicyTest::serverErrorIsNotRetriedAutomatically() {
    const auto policy = policyWithRng(0.0);
    const auto decision = policy.decide(failureWith(false), 0, QByteArray(), 60000);
    QVERIFY(!decision.shouldRetry);
}

void RetryPolicyTest::connectionFailureRetriesOnlyBeforeTheBodyWasSent() {
    const auto policy = policyWithRng(0.5);
    const auto beforeSend =
        manga::network::classifyTransportFailure(static_cast<int>(QNetworkReply::ConnectionRefusedError), false);
    QVERIFY(beforeSend.retryable);
    const auto decision = policy.decide(beforeSend, 0, QByteArray(), 60000);
    QVERIFY(decision.shouldRetry);
    QCOMPARE(decision.delayMs, 250); // 0.5 * 500ms

    const auto afterSend =
        manga::network::classifyTransportFailure(static_cast<int>(QNetworkReply::ConnectionRefusedError), true);
    QVERIFY(!afterSend.retryable);
    QVERIFY(!policy.decide(afterSend, 0, QByteArray(), 60000).shouldRetry);
}

void RetryPolicyTest::stopsAfterTwoRetries() {
    const auto policy = policyWithRng(0.0);
    QVERIFY(policy.decide(failureWith(true), 0, QByteArray(), 60000).shouldRetry);
    QVERIFY(policy.decide(failureWith(true), 1, QByteArray(), 60000).shouldRetry);
    QVERIFY(!policy.decide(failureWith(true), 2, QByteArray(), 60000).shouldRetry);
}

void RetryPolicyTest::jitterUsesInjectedRngAndGrowsWithAttempt() {
    const auto policy = policyWithRng(1.0);
    QCOMPARE(policy.decide(failureWith(true), 0, QByteArray(), 60000).delayMs, 500);
    QCOMPARE(policy.decide(failureWith(true), 1, QByteArray(), 60000).delayMs, 1000);
    QCOMPARE(policy.decide(failureWith(true), 2, QByteArray(), 60000).delayMs, 0);
}

void RetryPolicyTest::skipsRetryWhenTheDeadlineCannotCoverTheWait() {
    const auto policy = policyWithRng(1.0);
    // A 400 ms deadline cannot cover a 500 ms wait plus another request.
    QVERIFY(!policy.decide(failureWith(true), 0, QByteArray(), 400).shouldRetry);
    QVERIFY(policy.decide(failureWith(true), 0, QByteArray(), 5000).shouldRetry);
}

#include "moc_RetryPolicyTest.cpp"
