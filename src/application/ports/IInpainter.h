#pragma once

#include "application/dto/ClearDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Produces a replacement patch for one text region, leaving the page image itself untouched.
class IInpainter {
public:
    virtual ~IInpainter() = default;

    /// Returns a patch covering the requested region in the source image pixel coordinates of
    /// request.pageImage, together with the affected bounds actually touched. The page image is not
    /// modified, and this port defines no revision enforcement, so request.contentRevision carries no
    /// meaning here and the adoption UseCase is what rejects a stale revision.
    ///
    /// The work runs on a bounded CPU worker thread and fulfills the returned future there, so a
    /// continuation inherits that affinity and marshals to the GUI thread itself. The request and
    /// context are taken by value, while the implementation and its executor must outlive the
    /// returned future. A stop requested before the task is dequeued, inside its checkpoints, or
    /// after its work is done yields cancelled.user_requested with category Cancelled and discards
    /// the produced patch.
    ///
    /// A rejected region or setting reports clear.invalid_region, clear.invalid_color, or
    /// clear.invalid_opacity with category Input. An exhausted task queue reports the retryable
    /// inference.queue_full, a stopped executor reports internal.executor_shutdown, and an
    /// unexpected exception inside the work reports clear.failed or inference.oom.
    virtual QFuture<Result<ClearResponse>> clear(ClearRequest request, OperationContext context) = 0;
};

} // namespace manga::application
