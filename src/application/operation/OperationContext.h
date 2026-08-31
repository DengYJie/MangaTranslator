#pragma once

#include "application/operation/OperationId.h"
#include "application/operation/OperationProgressReporter.h"

#include <QtGlobal>

#include <memory>
#include <stop_token>

namespace manga::application {

struct OperationContext {
    OperationId operationId;
    quint64 sessionGeneration{0};
    quint64 viewGeneration{0};
    std::stop_token stopToken;
    // Progress reporter published to the owning OperationRegistry from worker threads.
    std::shared_ptr<OperationProgressReporter> progressReporter;
    // Adapters retain the context until their returned future finishes.
    std::shared_ptr<void> lifetime;
};

} // namespace manga::application
