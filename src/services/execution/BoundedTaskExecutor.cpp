#include "services/execution/BoundedTaskExecutor.h"

#include <QMutexLocker>
#include <QThread>

#include <algorithm>
#include <utility>

namespace manga::services {
namespace {

constexpr int kMaxPendingTasks = 32;
constexpr int kInteractiveRunBeforeBackground = 8;

application::ApplicationError makeQueueFullError() {
    auto error = application::makeApplicationError(application::ErrorCategory::Inference,
                                                   QStringLiteral("inference.queue_full"),
                                                   QStringLiteral("The task queue is full"),
                                                   application::RecoveryAction::Retry);
    error.retryable = true;
    return error;
}

application::ApplicationError makeShutdownError() {
    return application::makeApplicationError(application::ErrorCategory::Internal,
                                             QStringLiteral("internal.executor_shutdown"),
                                             QStringLiteral("The task executor is shutting down"));
}

} // namespace

int BoundedTaskExecutor::defaultWorkerCount() {
    int cores = QThread::idealThreadCount();
    if (cores <= 0) {
        cores = 1;
    }
    return std::clamp(cores >= 4 ? 2 : 1, 1, 2);
}

BoundedTaskExecutor::BoundedTaskExecutor(int workerCount) : m_workerCount(std::clamp(workerCount, 1, 2)) {
    m_state = std::make_shared<SharedState>();
    m_pool.setMaxThreadCount(m_workerCount);
}

BoundedTaskExecutor::~BoundedTaskExecutor() {
    shutdown();
    m_pool.waitForDone();
}

void BoundedTaskExecutor::shutdown() {
    std::deque<PendingTask> cancelled;
    {
        QMutexLocker locker(&m_state->mutex);
        m_state->shutdown = true;
        cancelled.swap(m_state->interactive);
        while (!m_state->background.empty()) {
            cancelled.push_back(std::move(m_state->background.front()));
            m_state->background.pop_front();
        }
    }
    for (auto& task : cancelled) {
        task.completeCancelled();
    }
    m_pool.waitForDone();
}

void BoundedTaskExecutor::pumpLocked(std::shared_ptr<SharedState> state,
                                     std::deque<PendingTask>& toStart,
                                     int workerCount) {
    while (state->active < workerCount && (!state->interactive.empty() || !state->background.empty())) {
        PendingTask next;
        const bool preferBackground =
            state->interactiveSinceBackground >= kInteractiveRunBeforeBackground && !state->background.empty();
        if (!state->background.empty() && (preferBackground || state->interactive.empty())) {
            next = std::move(state->background.front());
            state->background.pop_front();
            state->interactiveSinceBackground = 0;
        } else {
            next = std::move(state->interactive.front());
            state->interactive.pop_front();
            state->interactiveSinceBackground = state->background.empty() ? 0 : state->interactiveSinceBackground + 1;
        }
        ++state->active;
        toStart.push_back(std::move(next));
    }
}

void BoundedTaskExecutor::taskFinished(std::shared_ptr<SharedState> state, QThreadPool* pool) {
    std::deque<PendingTask> toStart;
    {
        QMutexLocker locker(&state->mutex);
        --state->active;
        pumpLocked(state, toStart, pool->maxThreadCount());
    }
    for (auto& next : toStart) {
        next.stopCallback.reset(); // Safely reset outside mutex
        auto* runnable = QRunnable::create([body = std::move(next.run)]() mutable { body(); });
        pool->start(runnable);
    }
}

} // namespace manga::services
