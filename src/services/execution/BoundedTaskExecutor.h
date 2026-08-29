#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "application/operation/OperationStage.h"
#include "application/operation/TaskProgress.h"

#include <QElapsedTimer>
#include <QFuture>
#include <QMutex>
#include <QPromise>
#include <QString>
#include <QThreadPool>

#include <deque>
#include <functional>
#include <optional>
#include <utility>

namespace manga::services {
namespace detail {

/// Fallback failure code for unexpected std::exceptions raised inside a task, keyed by the
/// task's processing stage.
[[nodiscard]] inline QString stageFailedCode(application::OperationStage stage) {
    switch (stage) {
        case application::OperationStage::Project:
            return QStringLiteral("project.failed");
        case application::OperationStage::Import:
            return QStringLiteral("import.failed");
        case application::OperationStage::LoadImage:
            return QStringLiteral("load_image.failed");
        case application::OperationStage::Ocr:
            return QStringLiteral("ocr.failed");
        case application::OperationStage::Translate:
            return QStringLiteral("translate.failed");
        case application::OperationStage::Clear:
            return QStringLiteral("clear.failed");
        case application::OperationStage::Layout:
            return QStringLiteral("layout.failed");
        case application::OperationStage::Export:
            return QStringLiteral("export.failed");
    }
    return QStringLiteral("internal.unexpected_exception");
}

} // namespace detail

/// Queue selection for submitted tasks: Interactive serves the current page's user-triggered
/// work, Background serves precomputation. Every 8 dequeued Interactive tasks yield one
/// Background task so precomputation cannot starve.
enum class TaskQueue {
    Interactive,
    Background,
};

/// Bounded CPU task scheduler over a QThreadPool + QRunnable pair, per the P4 contract.
/// Write operations per canonical project root are not the executor's concern, and the
/// executor never reads the Domain or interprets generation.
class BoundedTaskExecutor final {
public:
    /// clamp(hardware_concurrency - 1, 1, 4); hardware_concurrency()==0 counts as 1.
    [[nodiscard]] static int defaultWorkerCount();

    explicit BoundedTaskExecutor(int workerCount = defaultWorkerCount());
    ~BoundedTaskExecutor();

    BoundedTaskExecutor(const BoundedTaskExecutor&) = delete;
    BoundedTaskExecutor& operator=(const BoundedTaskExecutor&) = delete;

    /// Refuses new tasks, completes every queued task as cancelled, and waits for running
    /// tasks to release their OperationContext lifetime.
    void shutdown();

    /// Submits work onto a queue. The work callable receives the operation context and the
    /// progress facade and returns the task result; exceptions are mapped at the boundary
    /// (std::bad_alloc to inference.oom, std::exception to the stage's *.failed with a
    /// sanitized diagnostic, anything else to internal.unexpected_exception) and never
    /// escape QRunnable::run(). A queued task whose stopToken is already requested is
    /// completed as cancelled.user_requested when it reaches the front of the queue; a
    /// running task that observes stop after its work is done discards the result the same
    /// way. Returns inference.queue_full when the pending capacity is exhausted.
    template <typename T, typename Fn>
    [[nodiscard]] application::Result<QFuture<application::Result<T>>>
    submit(application::OperationStage stage, TaskQueue queue, const application::OperationContext& context, Fn&& work);

private:
    struct PendingTask {
        std::function<void()> run;
        std::function<void()> completeCancelled;
    };

    [[nodiscard]] application::ApplicationError enqueue(TaskQueue queue, PendingTask task);
    void taskFinished();
    /// Dequeues pending tasks into toStart while a worker slot is free, honoring the 8:1
    /// anti-starvation rule. Caller holds m_mutex.
    void pumpLocked(std::deque<PendingTask>& toStart);

    int m_workerCount;
    int m_active{0};
    int m_interactiveSinceBackground{0};
    bool m_shutdown{false};
    std::deque<PendingTask> m_interactive;
    std::deque<PendingTask> m_background;
    QThreadPool m_pool;
    QMutex m_mutex;
};

template <typename T, typename Fn>
application::Result<QFuture<application::Result<T>>> BoundedTaskExecutor::submit(
    application::OperationStage stage, TaskQueue queue, const application::OperationContext& context, Fn&& work) {
    auto promise = std::make_shared<QPromise<application::Result<T>>>();
    promise->start();
    auto future = promise->future();

    auto cancelComplete = [promise]() {
        promise->addResult(
            std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                              QStringLiteral("cancelled.user_requested"),
                                                              QStringLiteral("The task was cancelled"))));
        promise->finish();
    };

    auto run = [this, promise, context, stage, cancelComplete, work = std::forward<Fn>(work)]() mutable {
        // QPromise has no text-only progress API; stage text is carried by callers that use
        // setProgressValueAndText through their own promise when needed.
        application::TaskProgress progress([promise](qint64 from, qint64 to) { promise->setProgressRange(from, to); },
                                           [promise](qint64 value) { promise->setProgressValue(value); },
                                           {},
                                           [timer = std::make_shared<QElapsedTimer>()]() {
                                               // Throttle to one publication per 50 ms (P4 contract); final values
                                               // bypass this gate via TaskProgress.
                                               if (!timer->isValid()) {
                                                   timer->start();
                                                   return true;
                                               }
                                               if (timer->elapsed() < 50) {
                                                   return false;
                                               }
                                               timer->restart();
                                               return true;
                                           });

        // Pre-run cancellation checkpoint.
        if (context.stopToken.stop_requested()) {
            cancelComplete();
            taskFinished();
            return;
        }
        std::optional<application::Result<T>> outcome;
        try {
            outcome = work(context, progress);
        } catch (const std::bad_alloc&) {
            outcome = std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                        QStringLiteral("inference.oom"),
                                                                        QStringLiteral("The task ran out of memory"),
                                                                        application::RecoveryAction::Retry));
        } catch (const std::exception&) {
            // Sanitized: the exception message may carry paths or user content.
            outcome = std::unexpected(application::makeApplicationError(application::ErrorCategory::Inference,
                                                                        detail::stageFailedCode(stage),
                                                                        QStringLiteral("The task failed"),
                                                                        application::RecoveryAction::Retry));
        } catch (...) {
            outcome = std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Internal,
                                                  QStringLiteral("internal.unexpected_exception"),
                                                  QStringLiteral("An unexpected internal error occurred")));
        }
        // Post-run cancellation checkpoint: a stop observed after the work completed
        // discards the result, so cancelled tasks never publish partial output.
        if (context.stopToken.stop_requested()) {
            cancelComplete();
            taskFinished();
            return;
        }
        promise->addResult(std::move(*outcome));
        promise->finish();
        taskFinished();
    };

    PendingTask pending;
    pending.run = run;
    pending.completeCancelled = cancelComplete;
    auto error = enqueue(queue, std::move(pending));
    if (!error.code.isEmpty()) {
        // Complete the orphaned promise so the future is never left dangling.
        promise->addResult(std::unexpected(error));
        promise->finish();
        return std::unexpected(std::move(error));
    }
    return future;
}

} // namespace manga::services
