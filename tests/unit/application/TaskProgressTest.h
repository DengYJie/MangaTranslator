#pragma once

#include <QObject>

class TaskProgressTest : public QObject {
    Q_OBJECT

private slots:
    void testPublishesThroughFacade();
    void testThrottleSuppressesIntermediateValues();
    void testFinalValueBypassesThrottle();
    void testDecreasingValuesAreClamped();
    void testNoRangeIsNeverFinal();
};
