#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/OperationDescriptor.h"
#include "application/operation/OperationId.h"

#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>

namespace manga::application {

enum class OperationState {
    Queued,
    Running,
    Cancelling,
    Completed,
    Skipped,
    Failed,
    Cancelled,
    Stale,
};

struct OperationSnapshot {
    OperationId id;
    OperationDescriptor descriptor;
    OperationState state{OperationState::Queued};
    qint64 completed{0};
    qint64 total{0};
    QString statusText;
    std::optional<ApplicationError> error;
    QString reasonCode;
};

} // namespace manga::application

Q_DECLARE_METATYPE(manga::application::OperationSnapshot)
Q_DECLARE_METATYPE(QVector<manga::application::OperationSnapshot>)
