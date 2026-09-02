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

    /// Reads the committed thumbnail for a page. A missing asset is an error carrying
    /// thumbnail.not_found rather than a null image, while an invalid request, a closed project
    /// session, and an undecodable asset all report thumbnail.read_failed with category
    /// Persistence. The work runs on a storage worker thread and fulfills the returned future there,
    /// so a continuation inherits that affinity and marshals to the GUI thread itself. The request
    /// and context are taken by value, and the implementation must outlive the returned future. A
    /// stop requested before the read starts is reported as cancelled.user_requested.
    [[nodiscard]] virtual QFuture<Result<QImage>> readThumbnail(ReadThumbnailRequest request,
                                                                OperationContext context) = 0;

    /// Atomically writes the thumbnail for a page to the fixed derived path, replacing any stored
    /// asset. Rejections report thumbnail.commit_failed with category Persistence, including a null
    /// image, a closed project session, and a path that resolves outside the project. The work runs
    /// on a storage worker thread and fulfills the returned future there. A stop requested before
    /// the staged asset is committed is reported as cancelled.user_requested, and the commit does
    /// not recheck the token afterwards.
    [[nodiscard]] virtual QFuture<Result<void>> writeThumbnail(WriteThumbnailRequest request,
                                                               OperationContext context) = 0;
};

} // namespace manga::application
