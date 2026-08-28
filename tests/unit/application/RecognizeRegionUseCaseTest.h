#pragma once

#include <QObject>

class RecognizeRegionUseCaseTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulRecognition();
    void testCancellation();
    void testEngineFailure();
    void testInvalidRequestDoesNotCallEngine();
};
