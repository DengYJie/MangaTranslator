#pragma once

#include "application/dto/LayoutDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Resolves how translated text fits one region, shared by the canvas preview and the exporter.
class ITextLayouter {
public:
    virtual ~ITextLayouter() = default;

    /// Returns the layout that resolves request.text inside request.bounds, measured at
    /// request.logicalDpi in the source image pixel coordinates the bounds use. request.constraints
    /// supplies the requested typography and the result reports what was actually applied, so an
    /// unsatisfiable request succeeds with overflow set and non-blocking warnings rather than with an
    /// error. This port defines no revision enforcement, so request.contentRevision carries no meaning
    /// here and the adoption UseCase is what rejects a stale revision.
    ///
    /// The work runs on a bounded CPU worker thread and fulfills the returned future there, so a
    /// continuation inherits that affinity and marshals to the GUI thread itself. The request and
    /// context are taken by value, while the implementation and its executor must outlive the
    /// returned future. A stop requested before the task is dequeued, at either side of the layout
    /// computation, or after its work is done yields cancelled.user_requested with category
    /// Cancelled and discards the computed layout.
    ///
    /// An exhausted task queue reports the retryable inference.queue_full, a stopped executor reports
    /// internal.executor_shutdown, and an unexpected exception inside the work reports layout.failed
    /// or inference.oom.
    virtual QFuture<Result<LayoutResponse>> layout(LayoutRequest request, OperationContext context) = 0;
};

} // namespace manga::application
