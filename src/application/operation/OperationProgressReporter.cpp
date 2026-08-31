#include "application/operation/OperationProgressReporter.h"

#include <QElapsedTimer>

#include <utility>

namespace manga::application {

OperationProgressReporter::OperationProgressReporter(ProgressPublisher progressPublisher,
                                                     RunningPublisher runningPublisher,
                                                     CompletionPublisher completionPublisher)
    : m_progressPublisher(std::move(progressPublisher)), m_runningPublisher(std::move(runningPublisher)),
      m_completionPublisher(std::move(completionPublisher)) {}

void OperationProgressReporter::setRange(qint64 total) {
    std::lock_guard lock(m_mutex);
    if (m_finished) {
        return;
    }
    m_total = total;
    publishLocked(false);
}

void OperationProgressReporter::setValue(qint64 completed) {
    std::lock_guard lock(m_mutex);
    if (m_finished) {
        return;
    }
    if (completed < m_completed) {
        completed = m_completed;
    }
    m_completed = completed;
    publishLocked(false);
}

void OperationProgressReporter::setStatusText(QString statusText) {
    std::lock_guard lock(m_mutex);
    if (m_finished) {
        return;
    }
    m_statusText = std::move(statusText);
    publishLocked(false);
}

void OperationProgressReporter::markRunning() {
    std::lock_guard lock(m_mutex);
    if (m_finished || m_runningPending) {
        return;
    }
    m_runningPending = true;
    publishLocked(true);
}

void OperationProgressReporter::markFinished(std::optional<ApplicationError> error, QString reasonCode) {
    std::lock_guard lock(m_mutex);
    if (m_finished) {
        return;
    }
    m_finished = true;
    m_finishError = std::move(error);
    m_reasonCode = std::move(reasonCode);
    publishLocked(true);
}

void OperationProgressReporter::publishLocked(bool force) {
    if (m_runningPending) {
        m_runningPending = false;
        m_runningPublisher();
    }

    if (m_finished) {
        m_progressPublisher(m_completed, m_total, m_statusText, true);
        m_completionPublisher(std::move(m_finishError), std::move(m_reasonCode));
        return;
    }

    if (!force && m_throttle.isValid() && m_throttle.elapsed() < 50) {
        return;
    }

    m_progressPublisher(m_completed, m_total, m_statusText, force);

    if (!m_throttle.isValid()) {
        m_throttle.start();
    } else {
        m_throttle.restart();
    }
}

} // namespace manga::application
