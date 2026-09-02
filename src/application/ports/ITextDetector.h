#pragma once

#include "application/dto/TextDetectionDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Abstract port for asynchronous comic layout analysis and text detection.
class ITextDetector {
public:
    virtual ~ITextDetector() = default;

    /// Detects text regions, bubbles, and panels in one page and reports their geometry in the pixel
    /// coordinates of request.image. A model is loaded lazily inside the queued task, so the first
    /// call after startup or after a release carries that cost and waits behind a load already under
    /// way.
    ///
    /// The work runs on a bounded CPU worker thread and fulfills the returned future there, so a
    /// continuation inherits that affinity and marshals to the GUI thread itself. The request and
    /// context are taken by value, while the detector and its executor must outlive the returned
    /// future. A stop requested before the task is dequeued or at any checkpoint inside it yields
    /// cancelled.user_requested with category Cancelled, and a task that observes a stop after its
    /// inference finished discards the result the same way.
    ///
    /// An unusable model bundle reports model.missing, model.incompatible, or model.corrupt with a
    /// locate model recovery hint, an absent runtime or executor reports detection.null_env or
    /// detection.null_executor with category Internal, a failed run reports inference.failed, an
    /// exhausted task queue reports the retryable inference.queue_full, and a stopped executor
    /// reports internal.executor_shutdown.
    [[nodiscard]] virtual QFuture<Result<TextDetectionResponse>> detect(TextDetectionRequest request,
                                                                        OperationContext context) = 0;

    /// Offers to release the loaded model sessions and weights. It may be called from any thread, and
    /// the application calls it from GUI thread idle timers while other operations may still be
    /// running, so the implementation decides its own quiescence and may return having released
    /// nothing. The call can block briefly on the adapter's inference lock and never cancels queued
    /// work, so a task that starts afterwards reloads the model lazily. A caller cannot observe
    /// whether anything was released and must not treat this call as a state change.
    virtual void release() {}
};

} // namespace manga::application
