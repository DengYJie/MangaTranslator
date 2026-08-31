#pragma once

#include <QObject>

class BoundedTaskExecutorTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testWorkerCountBoundsConcurrency();
    void testQueueCapacityReturnsQueueFull();
    void testInteractiveRunsInSubmissionOrder();
    void testBackgroundRunsAfterEightInteractive();
    void testQueuedCancelCompletesAsCancelled();
    void testRunningStopAfterWorkDiscardsResult();
    void testExceptionMapping();
    void testShutdownCancelsQueuedAndWaitsRunning();
    void testProgressFinalValueEqualsTotal();
    void testConcurrentCallbackAndShutdown();
    void testPreCancelledTaskCompletesImmediatelyWithoutConsumingCapacity();
    void testConcurrentCancelDuringSubmissionDoesNotLeakQueueSlot();
    void testReporterReceivesRunningAndFinished();
};
