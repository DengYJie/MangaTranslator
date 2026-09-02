#pragma once

#include <QObject>

class RecognizeRegionUseCaseTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testSuccessfulRecognition();
    void testCancellation();
    void testEngineFailure();
    void testInvalidRequestDoesNotCallEngine();
};
