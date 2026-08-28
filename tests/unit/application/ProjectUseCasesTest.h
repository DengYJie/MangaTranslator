#pragma once
#include <QObject>

class ProjectUseCasesTest : public QObject {
    Q_OBJECT
private slots:
    void testSuccessfulCreate();
    void testCreateFailure();
    void testRemainingProjectOperationsDispatch();
    void testInvalidProjectOperationsDoNotReachRepository();
    void testRepositoryFailurePropagates();
    void testPreCancelledOperationRejected();
    void testRecoverUsesRecoveryPort();
};
