#pragma once

#include <QObject>

class OpenAiCompatibleTranslatorTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void translatesSuccessfullyAndKeepsProviderIdentity();
    void sendsBearerAuthorizationButNeverTheKeyInTheBody();
    void missingCredentialIsReportedWithoutSending();
    void serverErrorIsReportedWithoutRetry();
    void rateLimitedRequestRetriesOnceThenSucceeds();
    void unauthorizedIsNotRetried();
    void cancellationBeforeStartSkipsTheRequest();
    void cancellationAbortsTheInFlightRequest();
    void oversizeResponseIsRejected();
    void loopbackServerRoundTripUsesTheQtTransport();
    void loopbackServerRejectsOversizeBody();
    void loopbackInFlightCancelUsesRealQtHttpTransport();
    void loopbackTranslatorDestructionWhileJobIsInFlightUsesRealQtHttpTransport();
    void cancellationDuringRetryBackoffAbortsImmediately();
    void translatorDestructionWhileJobIsInFlightIsSafe();
    void retryIsSkippedWhenRetryAfterExceedsDeadline();
};
