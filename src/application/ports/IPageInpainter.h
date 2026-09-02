#pragma once

#include "application/dto/PageInpaintDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Removes masked content from a whole page and returns the repaired page.
class IPageInpainter {
public:
    virtual ~IPageInpainter() = default;

    /// Repairs the masked pixels of request.sourceImage and returns a full page that always keeps
    /// the request dimensions, so a caller can adopt the result as a page wide layer without
    /// resizing. Both inputs use source image pixel coordinates, the mask is read as non zero means
    /// remove, and request.dirtyBounds must already lie inside the source image because the
    /// implementation confines its work to that rectangle. The request image and mask are not
    /// modified.
    ///
    /// The work runs on a bounded CPU worker thread and fulfills the returned future there, so a
    /// continuation inherits that affinity and marshals to the GUI thread itself. The request and
    /// context are taken by value, while the engine and its executor must outlive the returned
    /// future. An implementation processes the page in tiles and checks the stop token between them,
    /// so a stop yields cancelled.user_requested with category Cancelled and discards a partially
    /// repaired page rather than returning it.
    ///
    /// A null, mismatched, or unconvertible source or mask reports inference.invalid_input and an
    /// empty or out of bounds rectangle reports inference.invalid_dirty_bounds, both with category
    /// Input. An unusable bundle reports model.load_failed, model.corrupt, or model.incompatible, a
    /// failed run reports inference.failed with a retry hint, an engine without its executor reports
    /// inference.unavailable, an exhausted task queue reports the retryable inference.queue_full, and
    /// a stopped executor reports internal.executor_shutdown.
    [[nodiscard]] virtual QFuture<Result<PageInpaintResponse>> inpaint(PageInpaintRequest request,
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
