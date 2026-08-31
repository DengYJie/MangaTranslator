#pragma once

#include "application/error/ApplicationError.h"

#include <QElapsedTimer>
#include <QString>

#include <functional>
#include <mutex>
#include <optional>

namespace manga::application {

class OperationProgressReporter {
public:
    using ProgressPublisher =
        std::function<void(qint64 completed, qint64 total, const QString& statusText, bool force)>;
    using CompletionPublisher = std::function<void(std::optional<ApplicationError>, const QString& reasonCode)>;
    using RunningPublisher = std::function<void()>;

    OperationProgressReporter(ProgressPublisher progressPublisher,
                              RunningPublisher runningPublisher,
                              CompletionPublisher completionPublisher);

    void setRange(qint64 total);
    void setValue(qint64 completed);
    void setStatusText(QString statusText);
    void markRunning();
    void markFinished(std::optional<ApplicationError> error = std::nullopt, QString reasonCode = {});

private:
    void publishLocked(bool force);

    std::mutex m_mutex;
    ProgressPublisher m_progressPublisher;
    RunningPublisher m_runningPublisher;
    CompletionPublisher m_completionPublisher;
    qint64 m_completed{0};
    qint64 m_total{0};
    QString m_statusText;
    bool m_finished{false};
    std::optional<ApplicationError> m_finishError;
    QString m_reasonCode;
    bool m_runningPending{false};
    QElapsedTimer m_throttle;
};

} // namespace manga::application
