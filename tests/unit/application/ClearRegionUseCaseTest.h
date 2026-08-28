#pragma once
#include <QObject>

class ClearRegionUseCaseTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulClear();
    void testClearFailure();
    void testInvalidRequestDoesNotCallInpainter();
    void testCancellation();
};
