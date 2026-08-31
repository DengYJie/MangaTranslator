#pragma once

#include <QObject>

class OperationRegistryTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void createsAndCompletesOperation();
    void cancelsOnlyMatchingProject();
    void retainsAtMostThirtyTwoSettledOperations();
    void skippedCarriesReasonCode();
    void staleStateIsDistinct();
    void progressIsMonotonic();
    void safeErrorHandling();
    void crossThreadPublication();
};
