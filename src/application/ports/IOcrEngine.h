#pragma once

#include "application/dto/OcrDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Reads the text of one region of an already normalized page image.
class IOcrEngine {
public:
    virtual ~IOcrEngine() = default;

    /// Returns the text found in request.rect of request.image, echoed with request.regionId. The
    /// implementation crops the region itself, so the caller passes the whole page and never a
    /// pre-crop. An implementation may route on request.language to a different model, which is why
    /// the language is part of the request rather than engine state. A region that yields no text is
    /// a failure, reported as inference.empty_result or inference.empty_output rather than as an
    /// empty response, and a decode that runs past its length limit reports inference.decode_limit.
    ///
    /// The work runs on a bounded CPU worker thread and fulfills the returned future there, so a
    /// continuation inherits that affinity and marshals to the GUI thread itself. The request and
    /// context are taken by value, while the engine and its executor must outlive the returned
    /// future. A stop requested before the task is dequeued or at a checkpoint inside it yields
    /// cancelled.user_requested with category Cancelled, and a stop observed after the inference
    /// finished discards the text the same way.
    ///
    /// A request the preprocessor cannot crop reports input.invalid_ocr_region, input.invalid_image,
    /// or input.crop_empty, an unusable bundle reports model.missing, model.incompatible,
    /// model.load_failed, or model.env_missing, a failed run reports inference.failed or
    /// inference.bad_shape, an exhausted task queue reports the retryable inference.queue_full, and a
    /// stopped executor reports internal.executor_shutdown. All of these carry their own category.
    virtual QFuture<Result<OcrResponse>> recognize(OcrRequest request, OperationContext context) = 0;

    /// Offers to release the loaded model sessions and weights. It may be called from any thread, and
    /// the application calls it from GUI thread idle timers while other operations may still be
    /// running, so the implementation decides its own quiescence and may return having released
    /// nothing. The call can block briefly on the adapter's inference lock and never cancels queued
    /// work, so a task that starts afterwards reloads the model lazily. A caller cannot observe
    /// whether anything was released and must not treat this call as a state change.
    virtual void release() {}
};

} // namespace manga::application
