#pragma once

#include "application/dto/ExportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>

namespace manga::application {

/// Writes finished pages outside the project from data the caller already resolved.
class IComicExporter {
public:
    virtual ~IComicExporter() = default;

    /// Composites each snapshot from source image, clear layer, translation layer, and manual
    /// override layer in that order and encodes the result, flattening a JPEG over an opaque white
    /// background. Output files never overwrite an existing file, so a conflict produces a numbered
    /// name and a run that cannot find a free name reports export.name_exhausted.
    ///
    /// One bad page does not fail the run and does not stop the later pages, so a successful result
    /// still has to be read together with its per page failures, and a page whose layers are
    /// mismatched or not sRGB reports export.invalid_layer while an unsafe stem reports
    /// export.invalid_output_name. A locked output directory reports export.output_locked, and an
    /// encode or rename that cannot complete reports export.encode_failed or export.commit_failed,
    /// all with category Persistence and a choose path recovery hint. An empty output directory or
    /// a directory that cannot be created fails the whole call with export.invalid_request, and a
    /// missing system encoder fails it with export.encoder_unavailable.
    ///
    /// The work runs on a storage worker thread and fulfills the returned future there, so a
    /// continuation inherits that affinity and marshals to the GUI thread itself. The request and
    /// context are taken by value and the operation reads nothing of the caller's afterwards. A stop
    /// requested before the work starts or between pages reports cancelled.user_requested for the
    /// whole call or for the affected page only, and a stop arriving while a page is being written
    /// discards that page's staged file.
    virtual QFuture<Result<ExportResponse>> exportPages(ExportRequest request, OperationContext context) = 0;
};

} // namespace manga::application
