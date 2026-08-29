#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "domain/model/Uuid.h"

#include <QFuture>
#include <QImage>
#include <QString>

namespace manga::application {

struct ReadThumbnailRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    domain::PageId pageId;
};

struct WriteThumbnailRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    domain::PageId pageId;
    QImage thumbnail;
};

/// Narrow port for reading and writing page thumbnails.
/// Adapters must bind the request to the canonical project root and PageId,
/// derive the output path themselves (fixed path semantics), and commit atomically.
class IThumbnailStore {
public:
    virtual ~IThumbnailStore() = default;

    /// Reads the thumbnail for a page. Missing thumbnail is returned as an error (e.g., not_found).
    [[nodiscard]] virtual QFuture<Result<QImage>> readThumbnail(ReadThumbnailRequest request,
                                                                OperationContext context) = 0;

    /// Atomically writes the thumbnail for a page to the fixed derived path.
    [[nodiscard]] virtual QFuture<Result<void>> writeThumbnail(WriteThumbnailRequest request,
                                                               OperationContext context) = 0;
};

} // namespace manga::application
