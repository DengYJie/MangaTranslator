#pragma once
#include <QObject>

class ClearRegionUseCaseTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testSuccessfulClear();
    void testClearFailure();
    void testInvalidRequestDoesNotCallInpainter();
    void testDisabledClearingNeverReachesInpainter();
    void testCancellation();
};
