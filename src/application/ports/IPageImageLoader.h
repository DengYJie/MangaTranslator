#pragma once

#include "application/dto/ImportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"

#include <QFuture>
#include <QImage>

namespace manga::application {

/// Decodes one committed project page asset into the working image every stage measures against.
class IPageImageLoader {
public:
    virtual ~IPageImageLoader() = default;

    /// Returns the decoded page as straight alpha RGBA8 in sRGB, normalized so that detection, OCR,
    /// inpainting, layout, and export all measure the same pixels. request.assetPath is a project
    /// relative reference resolved inside the canonical root, and an absolute reference, a reference
    /// that escapes the root, or one redirected there by a link reports input.invalid_image_request.
    /// A mismatch between the decoded size and request.expectedSize reports
    /// project.image_dimension_mismatch, which is how stale project metadata is caught. A missing or
    /// unreadable file reports persistence.not_found, an oversized one input.image_too_large, and
    /// undecodable or whitelisted-but-unknown content import.decode_failed or
    /// import.unsupported_format.
    ///
    /// The work runs on a storage worker thread and fulfills the returned future there, so a
    /// continuation inherits that affinity and marshals to the GUI thread itself. The request and
    /// context are taken by value, while the loader must outlive the returned future. The load holds
    /// a project read lease for its whole duration, so closing the project waits for it.
    /// A stop requested before the work starts or after the decode returns reports cancelled.decode,
    /// and because the decode itself cannot be interrupted its image is discarded instead.
    virtual QFuture<Result<QImage>> loadPageImage(LoadPageImageRequest request, OperationContext context) = 0;
};

} // namespace manga::application
