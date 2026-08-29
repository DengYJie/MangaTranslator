#pragma once

#include <QObject>

class RetryPolicyTest : public QObject {
    Q_OBJECT

private slots:
    void rateLimitRetriesUsingRetryAfterSeconds();
    void rateLimitClampsHttpDateRetryAfter();
    void serverErrorIsNotRetriedAutomatically();
    void connectionFailureRetriesOnlyBeforeTheBodyWasSent();
    void stopsAfterTwoRetries();
    void jitterUsesInjectedRngAndGrowsWithAttempt();
    void skipsRetryWhenTheDeadlineCannotCoverTheWait();
};
