#include "BoundedTaskExecutorTest.h"

#include "services/execution/BoundedTaskExecutor.h"

#include <QSemaphore>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <mutex>
#include <stop_token>
#include <string>
#include <vector>

using namespace manga;
using namespace manga::application;
using manga::services::BoundedTaskExecutor;
using manga::services::TaskQueue;

namespace {

application::OperationContext makeContext(std::stop_token token = {}) {
    application::OperationContext context;
    context.stopToken = token;
    return context;
}

// Blocks until released; records that it started and its completion order.
struct Blocker {
    QSemaphore started;
    QSemaphore release;
};

} // namespace

void BoundedTaskExecutorTest::init() {}

void BoundedTaskExecutorTest::cleanup() {}

void BoundedTaskExecutorTest::testWorkerCountBoundsConcurrency() {
    BoundedTaskExecutor executor(2);
    std::atomic<int> active{0};
    std::atomic<int> maxActive{0};
    std::atomic<int> completed{0};
    std::vector<application::OperationContext> contexts(6);
    std::vector<QFuture<application::Result<int>>> futures;

    for (int i = 0; i < 6; ++i) {
        contexts[static_cast<size_t>(i)] = makeContext();
        auto submitted = executor.submit<int>(
            application::OperationStage::Ocr,
            TaskQueue::Background,
            contexts[static_cast<size_t>(i)],
            [&active, &maxActive, &completed, i](const application::OperationContext&, TaskProgress&) {
                const int now = ++active;
                int expected = maxActive.load();
                while (now > expected && !maxActive.compare_exchange_weak(expected, now)) {
                }
                QThread::msleep(120);
                --active;
                ++completed;
                return application::Result<int>(i);
            });
        QVERIFY(submitted.has_value());
        futures.push_back(*submitted);
    }
    for (auto& future : futures) {
        future.waitForFinished();
    }
    QCOMPARE(completed.load(), 6);
    QVERIFY(maxActive.load() <= 2);
}

void BoundedTaskExecutorTest::testQueueCapacityReturnsQueueFull() {
    BoundedTaskExecutor executor(1);
    std::optional<Blocker> blocker;
    blocker.emplace();

    application::OperationContext blockerContext = makeContext();
    auto blockerSubmit = executor.submit<int>(application::OperationStage::Ocr,
                                              TaskQueue::Background,
                                              blockerContext,
                                              [&blocker](const application::OperationContext&, TaskProgress&) {
                                                  blocker->started.release();
                                                  blocker->release.acquire();
                                                  return application::Result<int>(0);
                                              });
    QVERIFY(blockerSubmit.has_value());
    blocker->started.acquire();

    // The only worker is busy: everything else pends. Capacity is 32 pending tasks.
    std::stop_source pendingStop;
    std::vector<QFuture<application::Result<int>>> pending;
    for (int i = 0; i < 32; ++i) {
        auto submitted = executor.submit<int>(
            application::OperationStage::Ocr,
            TaskQueue::Background,
            makeContext(pendingStop.get_token()),
            [](const application::OperationContext&, TaskProgress&) { return application::Result<int>(1); });
        QVERIFY(submitted.has_value());
        pending.push_back(*submitted);
    }
    auto rejected = executor.submit<int>(
        application::OperationStage::Ocr,
        TaskQueue::Background,
        makeContext(),
        [](const application::OperationContext&, TaskProgress&) { return application::Result<int>(2); });

    QVERIFY(!rejected.has_value());
    QCOMPARE(rejected.error().code, QStringLiteral("inference.queue_full"));
    QVERIFY(rejected.error().retryable);
    QCOMPARE(rejected.error().recovery, application::RecoveryAction::Retry);

    // Drain: stop the queued tasks, release the blocker, and wait for the pool.
    pendingStop.request_stop();
    blocker->release.release();
    for (auto& future : pending) {
        future.waitForFinished();
    }
    blockerSubmit->waitForFinished();
}

void BoundedTaskExecutorTest::testInteractiveRunsInSubmissionOrder() {
    BoundedTaskExecutor executor(1);
    std::optional<Blocker> blocker;
    blocker.emplace();

    application::OperationContext blockerContext = makeContext();
    auto blockerSubmit = executor.submit<int>(application::OperationStage::Ocr,
                                              TaskQueue::Interactive,
                                              blockerContext,
                                              [&blocker](const application::OperationContext&, TaskProgress&) {
                                                  blocker->started.release();
                                                  blocker->release.acquire();
                                                  return application::Result<int>(0);
                                              });
    QVERIFY(blockerSubmit.has_value());
    blocker->started.acquire();

    std::mutex orderMutex;
    std::vector<int> order;
    std::vector<QFuture<application::Result<int>>> futures;
    for (int i = 1; i <= 3; ++i) {
        auto submitted =
            executor.submit<int>(application::OperationStage::Ocr,
                                 TaskQueue::Interactive,
                                 makeContext(),
                                 [&orderMutex, &order, i](const application::OperationContext&, TaskProgress&) {
                                     std::lock_guard lock(orderMutex);
                                     order.push_back(i);
                                     return application::Result<int>(i);
                                 });
        QVERIFY(submitted.has_value());
        futures.push_back(*submitted);
    }
    blocker->release.release();
    for (auto& future : futures) {
        future.waitForFinished();
    }
    blockerSubmit->waitForFinished();
    QVERIFY((order == std::vector<int>{1, 2, 3}));
}

void BoundedTaskExecutorTest::testBackgroundRunsAfterEightInteractive() {
    BoundedTaskExecutor executor(1);
    std::optional<Blocker> blocker;
    blocker.emplace();

    application::OperationContext blockerContext = makeContext();
    auto blockerSubmit = executor.submit<int>(application::OperationStage::Ocr,
                                              TaskQueue::Interactive,
                                              blockerContext,
                                              [&blocker](const application::OperationContext&, TaskProgress&) {
                                                  blocker->started.release();
                                                  blocker->release.acquire();
                                                  return application::Result<int>(0);
                                              });
    QVERIFY(blockerSubmit.has_value());
    blocker->started.acquire();

    std::mutex orderMutex;
    std::vector<char> order;
    std::vector<QFuture<application::Result<int>>> futures;
    const auto enqueue = [&](char tag, TaskQueue queue) {
        auto submitted =
            executor.submit<int>(application::OperationStage::Ocr,
                                 queue,
                                 makeContext(),
                                 [&orderMutex, &order, tag](const application::OperationContext&, TaskProgress&) {
                                     std::lock_guard lock(orderMutex);
                                     order.push_back(tag);
                                     return application::Result<int>(0);
                                 });
        QVERIFY(submitted.has_value());
        futures.push_back(*submitted);
    };
    for (int i = 0; i < 8; ++i) {
        enqueue('I', TaskQueue::Interactive);
    }
    enqueue('B', TaskQueue::Background);
    enqueue('i', TaskQueue::Interactive);

    blocker->release.release();
    for (auto& future : futures) {
        future.waitForFinished();
    }
    blockerSubmit->waitForFinished();
    QVERIFY((order == std::vector<char>{'I', 'I', 'I', 'I', 'I', 'I', 'I', 'I', 'B', 'i'}));
}

void BoundedTaskExecutorTest::testQueuedCancelCompletesAsCancelled() {
    BoundedTaskExecutor executor(1);
    std::optional<Blocker> blocker;
    blocker.emplace();

    application::OperationContext blockerContext = makeContext();
    auto blockerSubmit = executor.submit<int>(application::OperationStage::Ocr,
                                              TaskQueue::Interactive,
                                              blockerContext,
                                              [&blocker](const application::OperationContext&, TaskProgress&) {
                                                  blocker->started.release();
                                                  blocker->release.acquire();
                                                  return application::Result<int>(0);
                                              });
    QVERIFY(blockerSubmit.has_value());
    blocker->started.acquire();

    std::stop_source stopSource;
    auto queued = executor.submit<int>(
        application::OperationStage::Ocr,
        TaskQueue::Interactive,
        makeContext(stopSource.get_token()),
        [](const application::OperationContext&, TaskProgress&) { return application::Result<int>(7); });
    QVERIFY(queued.has_value());
    stopSource.request_stop();

    blocker->release.release();
    queued->waitForFinished();
    QVERIFY(!queued->result().has_value());
    QCOMPARE(queued->result().error().code, QStringLiteral("cancelled.user_requested"));
    blockerSubmit->waitForFinished();
}

void BoundedTaskExecutorTest::testRunningStopAfterWorkDiscardsResult() {
    BoundedTaskExecutor executor(1);
    std::stop_source stopSource;
    auto running = executor.submit<int>(application::OperationStage::Ocr,
                                        TaskQueue::Interactive,
                                        makeContext(stopSource.get_token()),
                                        [&stopSource](const application::OperationContext& context, TaskProgress&) {
                                            // Finish the work, then let the post-run checkpoint observe the stop.
                                            while (!stopSource.stop_requested()) {
                                                QThread::msleep(5);
                                            }
                                            context.stopToken.stop_requested();
                                            return application::Result<int>(42);
                                        });
    QVERIFY(running.has_value());
    stopSource.request_stop();
    running->waitForFinished();

    QVERIFY(!running->result().has_value());
    QCOMPARE(running->result().error().code, QStringLiteral("cancelled.user_requested"));
}

void BoundedTaskExecutorTest::testExceptionMapping() {
    BoundedTaskExecutor executor(1);

    auto oom = executor.submit<int>(application::OperationStage::Ocr,
                                    TaskQueue::Interactive,
                                    makeContext(),
                                    [](const application::OperationContext&,
                                       TaskProgress&) -> application::Result<int> { throw std::bad_alloc(); });
    QVERIFY(oom.has_value());
    oom->waitForFinished();
    QVERIFY(!oom->result().has_value());
    QCOMPARE(oom->result().error().code, QStringLiteral("inference.oom"));

    auto stageFailed =
        executor.submit<int>(application::OperationStage::Layout,
                             TaskQueue::Interactive,
                             makeContext(),
                             [](const application::OperationContext&, TaskProgress&) -> application::Result<int> {
                                 throw std::runtime_error("secret path C:/leak");
                             });
    QVERIFY(stageFailed.has_value());
    stageFailed->waitForFinished();
    QVERIFY(!stageFailed->result().has_value());
    QCOMPARE(stageFailed->result().error().code, QStringLiteral("layout.failed"));
    QVERIFY(!stageFailed->result().error().diagnostic.contains(QStringLiteral("C:")));

    auto unknown = executor.submit<int>(
        application::OperationStage::Ocr,
        TaskQueue::Interactive,
        makeContext(),
        [](const application::OperationContext&, TaskProgress&) -> application::Result<int> { throw 42; });
    QVERIFY(unknown.has_value());
    unknown->waitForFinished();
    QVERIFY(!unknown->result().has_value());
    QCOMPARE(unknown->result().error().code, QStringLiteral("internal.unexpected_exception"));
}

void BoundedTaskExecutorTest::testShutdownCancelsQueuedAndWaitsRunning() {
    std::optional<QFuture<application::Result<int>>> pendingFuture;
    QSemaphore blockerDone;
    {
        BoundedTaskExecutor executor(1);
        std::optional<Blocker> blocker;
        blocker.emplace();
        application::OperationContext blockerContext = makeContext();
        auto blockerSubmit =
            executor.submit<int>(application::OperationStage::Ocr,
                                 TaskQueue::Interactive,
                                 blockerContext,
                                 [&blocker, &blockerDone](const application::OperationContext&, TaskProgress&) {
                                     blocker->started.release();
                                     // Bounded wait so shutdown is never blocked forever if the release never
                                     // arrives (release arrives after shutdown returns in this test).
                                     blocker->release.tryAcquire(1, 2000);
                                     blockerDone.release();
                                     return application::Result<int>(0);
                                 });
        QVERIFY(blockerSubmit.has_value());
        blocker->started.acquire();

        auto pending = executor.submit<int>(
            application::OperationStage::Ocr,
            TaskQueue::Interactive,
            makeContext(),
            [](const application::OperationContext&, TaskProgress&) { return application::Result<int>(1); });
        QVERIFY(pending.has_value());
        pendingFuture = *pending;

        executor.shutdown();
        // Queued tasks were completed as cancelled by shutdown.
        QVERIFY(pendingFuture.has_value() && !pendingFuture->result().has_value());
        QCOMPARE(pendingFuture->result().error().code, QStringLiteral("cancelled.user_requested"));

        // New submissions are refused.
        auto rejected = executor.submit<int>(
            application::OperationStage::Ocr,
            TaskQueue::Interactive,
            makeContext(),
            [](const application::OperationContext&, TaskProgress&) { return application::Result<int>(2); });
        QVERIFY(!rejected.has_value());
        blocker->release.release();
    }
    // The running task was allowed to finish without a deadlock.
    QVERIFY(blockerDone.tryAcquire(1));
}

void BoundedTaskExecutorTest::testProgressFinalValueEqualsTotal() {
    BoundedTaskExecutor executor(1);

    auto task =
        executor.submit<int>(application::OperationStage::Ocr,
                             TaskQueue::Interactive,
                             makeContext(),
                             [](const application::OperationContext&, TaskProgress& progress) {
                                 progress.setRange(0, 100);
                                 for (qint64 value = 1; value <= 100; ++value) {
                                     progress.setValue(value); // 100 values well within the 50 ms throttle window.
                                 }
                                 return application::Result<int>(100);
                             });
    QVERIFY(task.has_value());
    task->waitForFinished();

    QVERIFY(task->isFinished());
    QVERIFY(task->result().has_value());
    // The facade force-publishes the final value, so the last published value equals total.
    QCOMPARE(task->progressValue(), 100);
}

#include "moc_BoundedTaskExecutorTest.cpp"
