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

/// Logs model and export task lifecycle events without input content or filesystem paths.
void logTaskEvent(const char* event,
                  application::OperationId operationId,
                  application::OperationStage stage,
                  qint64 elapsedMs,
                  const QString& errorCode = {});

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
        case application::OperationStage::Detect:
            return QStringLiteral("detection.failed");
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

[[nodiscard]] inline application::ApplicationError cancelledUserError() {
    return application::makeApplicationError(application::ErrorCategory::Cancelled,
                                             QStringLiteral("cancelled.user_requested"),
                                             QStringLiteral("The task was cancelled"));
}

} // namespace detail

/// Queue selection for submitted tasks: Interactive serves the current page's user-triggered
/// work, Background serves precomputation. Every 8 dequeued Interactive tasks yield one
/// Background task so precomputation cannot starve.
enum class TaskQueue {
    Interactive,
    Background,
};

/// Bounded CPU task scheduler over a QThreadPool + QRunnable pair. Both TaskQueue lanes share the same
/// worker count of running slots, and a task's stop token is observed before its work starts and again
/// before its result is published.
/// Write operations per canonical project root are not the executor's concern, and the
/// executor never reads the Domain or interprets generation.
class BoundedTaskExecutor final {
public:
    /// Reports two workers once Qt's ideal thread count reaches four, and one below that, treating a
    /// non-positive count as one. The constructor clamps any explicit count into that same range.
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

    struct PendingTask {
        uint64_t id{0};
        std::function<void()> run;
        std::function<void()> completeCancelled;
        std::shared_ptr<std::stop_callback<std::function<void()>>> stopCallback;
    };

    struct SharedState {
        QMutex mutex;
        bool shutdown{false};
        std::deque<PendingTask> interactive;
        std::deque<PendingTask> background;
        int active{0};
        int interactiveSinceBackground{0};
        uint64_t nextTaskId{1};
    };

    static void taskFinished(std::shared_ptr<SharedState> state, QThreadPool* pool);
    static void pumpLocked(std::shared_ptr<SharedState> state, std::deque<PendingTask>& toStart, int workerCount);
    int m_workerCount;
    std::shared_ptr<SharedState> m_state;
    QThreadPool m_pool;
};

template <typename T, typename Fn>
application::Result<QFuture<application::Result<T>>> BoundedTaskExecutor::submit(
    application::OperationStage stage, TaskQueue queue, const application::OperationContext& context, Fn&& work) {
    auto promise = std::make_shared<QPromise<application::Result<T>>>();
    promise->start();
    auto future = promise->future();
    const auto reporter = context.progressReporter;
    const auto cancelled = detail::cancelledUserError();
    QElapsedTimer taskTimer;
    taskTimer.start();
    detail::logTaskEvent("submitted", context.operationId, stage, 0);

    auto cancelComplete = [promise, reporter, cancelled, operationId = context.operationId, stage, taskTimer]() {
        detail::logTaskEvent("cancelled", operationId, stage, taskTimer.elapsed(), cancelled.code);
        if (reporter) {
            reporter->markFinished(cancelled);
        }
        promise->addResult(std::unexpected(cancelled));
        promise->finish();
    };

    if (context.stopToken.stop_requested()) {
        cancelComplete();
        return future;
    }

    auto run = [state = m_state,
                pool = &m_pool,
                promise,
                context,
                stage,
                taskTimer,
                cancelComplete,
                reporter,
                work = std::forward<Fn>(work)]() mutable {
        // Pre-run cancellation checkpoint.
        if (context.stopToken.stop_requested()) {
            cancelComplete();
            BoundedTaskExecutor::taskFinished(state, pool);
            return;
        }

        detail::logTaskEvent("started", context.operationId, stage, taskTimer.elapsed());
        if (reporter) {
            reporter->markRunning();
        }

        application::TaskProgress progress(
            [promise, reporter](qint64 from, qint64 to) {
                promise->setProgressRange(from, to);
                if (reporter) {
                    reporter->setRange(to);
                }
            },
            [promise, reporter](qint64 value) {
                promise->setProgressValue(value);
                if (reporter) {
                    reporter->setValue(value);
                }
            },
            [reporter](QString text) {
                if (reporter) {
                    reporter->setStatusText(std::move(text));
                }
            },
            []() { return true; });

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
            BoundedTaskExecutor::taskFinished(state, pool);
            return;
        }

        detail::logTaskEvent(outcome->has_value() ? "work_completed" : "failed",
                             context.operationId,
                             stage,
                             taskTimer.elapsed(),
                             outcome->has_value() ? QString{} : outcome->error().code);
        if (outcome->has_value()) {
            // NOTE: Do not call reporter->markFinished() here. The caller (e.g. EditorProcessingController or Workflow)
            // owns the operation lifecycle and is responsible for finishing it when the composite operation completes.
        } else {
            if (reporter) {
                reporter->markFinished(outcome->error());
            }
        }
        promise->addResult(std::move(*outcome));
        promise->finish();
        BoundedTaskExecutor::taskFinished(state, pool);
    };
    uint64_t taskId = 0;
    {
        QMutexLocker locker(&m_state->mutex);
        taskId = m_state->nextTaskId++;
    }

    PendingTask pending;
    pending.id = taskId;
    pending.run = run;
    pending.completeCancelled = cancelComplete;
    if (context.stopToken.stop_possible()) {
        pending.stopCallback = std::make_shared<std::stop_callback<std::function<void()>>>(
            context.stopToken, [weakState = std::weak_ptr<SharedState>(m_state), taskId]() {
                if (auto state = weakState.lock()) {
                    std::optional<PendingTask> cancelledTask;
                    {
                        QMutexLocker locker(&state->mutex);
                        for (auto it = state->interactive.begin(); it != state->interactive.end(); ++it) {
                            if (it->id == taskId) {
                                cancelledTask = std::move(*it);
                                state->interactive.erase(it);
                                break;
                            }
                        }
                        if (!cancelledTask) {
                            for (auto it = state->background.begin(); it != state->background.end(); ++it) {
                                if (it->id == taskId) {
                                    cancelledTask = std::move(*it);
                                    state->background.erase(it);
                                    break;
                                }
                            }
                        }
                    }
                    // Completion and captured context destruction may reenter task infrastructure.
                    if (cancelledTask) {
                        cancelledTask->completeCancelled();
                    }
                }
            });
    }

    std::deque<PendingTask> toStart;
    application::ApplicationError rejection;
    {
        QMutexLocker locker(&m_state->mutex);
        if (context.stopToken.stop_requested()) {
            cancelComplete();
            return future;
        }
        // The queue and yield literals below mirror kMaxPendingTasks and kInteractiveRunBeforeBackground,
        // which are file-local to the implementation and unreachable from this header.
        if (m_state->shutdown) {
            rejection = application::makeApplicationError(application::ErrorCategory::Internal,
                                                          QStringLiteral("internal.executor_shutdown"),
                                                          QStringLiteral("The task executor is shutting down"));
        } else if (m_state->interactive.size() + m_state->background.size() >= 32) {
            rejection = application::makeApplicationError(application::ErrorCategory::Inference,
                                                          QStringLiteral("inference.queue_full"),
                                                          QStringLiteral("The task queue is full"),
                                                          application::RecoveryAction::Retry);
            rejection.retryable = true;
        } else {
            if (queue == TaskQueue::Interactive) {
                m_state->interactive.push_back(std::move(pending));
            } else {
                m_state->background.push_back(std::move(pending));
            }
            BoundedTaskExecutor::pumpLocked(m_state, toStart, m_workerCount);
        }
    }

    if (!rejection.code.isEmpty()) {
        detail::logTaskEvent("rejected", context.operationId, stage, taskTimer.elapsed(), rejection.code);
        if (reporter) {
            reporter->markFinished(rejection);
        }
        promise->addResult(std::unexpected(rejection));
        promise->finish();
        return std::unexpected(std::move(rejection));
    }

    for (auto& next : toStart) {
        next.stopCallback.reset(); // Safely outside the mutex
        auto* runnable = QRunnable::create([body = std::move(next.run)]() mutable { body(); });
        m_pool.start(runnable);
    }

    return future;
}

} // namespace manga::services
