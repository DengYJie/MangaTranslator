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
    return std::clamp(cores - 1, 1, 4);
}

BoundedTaskExecutor::BoundedTaskExecutor(int workerCount) : m_workerCount(std::clamp(workerCount, 1, 4)) {
    m_pool.setMaxThreadCount(m_workerCount);
}

BoundedTaskExecutor::~BoundedTaskExecutor() {
    shutdown();
    m_pool.waitForDone();
}

void BoundedTaskExecutor::shutdown() {
    std::deque<PendingTask> cancelled;
    {
        QMutexLocker locker(&m_mutex);
        m_shutdown = true;
        cancelled.swap(m_interactive);
        while (!m_background.empty()) {
            cancelled.push_back(std::move(m_background.front()));
            m_background.pop_front();
        }
    }
    for (auto& task : cancelled) {
        task.completeCancelled();
    }
    m_pool.waitForDone();
}

void BoundedTaskExecutor::pumpLocked(std::deque<PendingTask>& toStart) {
    while (m_active < m_workerCount && (!m_interactive.empty() || !m_background.empty())) {
        PendingTask next;
        const bool preferBackground =
            m_interactiveSinceBackground >= kInteractiveRunBeforeBackground && !m_background.empty();
        if (!m_background.empty() && (preferBackground || m_interactive.empty())) {
            next = std::move(m_background.front());
            m_background.pop_front();
            m_interactiveSinceBackground = 0;
        } else {
            next = std::move(m_interactive.front());
            m_interactive.pop_front();
            // The starvation counter only accumulates while a Background task is waiting.
            m_interactiveSinceBackground = m_background.empty() ? 0 : m_interactiveSinceBackground + 1;
        }
        ++m_active;
        toStart.push_back(std::move(next));
    }
}

application::ApplicationError BoundedTaskExecutor::enqueue(TaskQueue queue, PendingTask task) {
    std::deque<PendingTask> toStart;
    application::ApplicationError rejection;
    {
        QMutexLocker locker(&m_mutex);
        if (m_shutdown) {
            return makeShutdownError();
        }
        if (m_interactive.size() + m_background.size() >= kMaxPendingTasks) {
            return makeQueueFullError();
        }
        if (queue == TaskQueue::Interactive) {
            m_interactive.push_back(std::move(task));
        } else {
            m_background.push_back(std::move(task));
        }
        pumpLocked(toStart);
    }
    for (auto& next : toStart) {
        auto* runnable = QRunnable::create([body = std::move(next.run)]() mutable { body(); });
        m_pool.start(runnable);
    }
    return rejection;
}

void BoundedTaskExecutor::taskFinished() {
    std::deque<PendingTask> toStart;
    {
        QMutexLocker locker(&m_mutex);
        --m_active;
        pumpLocked(toStart);
    }
    for (auto& next : toStart) {
        auto* runnable = QRunnable::create([body = std::move(next.run)]() mutable { body(); });
        m_pool.start(runnable);
    }
}

} // namespace manga::services
