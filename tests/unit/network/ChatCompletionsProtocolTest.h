#pragma once

#include <QObject>

class ChatCompletionsProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void requestCarriesPromptV1AndDeterministicPayload();
    void requestUsesJsonObjectModeWhenConfigured();
    void requestSkipsDisabledOrInvalidGlossary();

    void parsesStrictResponse();
    void reportsRefusal();
    void reportsTruncatedAndFilteredResponses();
    void reportsMissingContent();
    void rejectsMalformedJson();
    void rejectsUnknownKeyAndWrongTypes();
    void rejectsDuplicateIds();
    void rejectsTooManyCandidatesAndOversizeFields();
    void rejectsOversizeBody();
    void passesUnknownIdsThroughForPartialAdoption();
};
