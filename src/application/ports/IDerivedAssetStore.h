#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "domain/model/Uuid.h"

#include <QFuture>
#include <QImage>
#include <QSet>
#include <QString>

namespace manga::application {

enum class DerivedLayerKind {
    Clear,
    Translation,
    ManualOverride,
};

struct ReadDerivedLayerRequest {
    domain::ProjectId projectId;
    QString projectRoot; // runtime absolute path, never persisted
    domain::PageId pageId;
    DerivedLayerKind layerKind{DerivedLayerKind::Clear};
    QString relativePath; // project-root relative layer reference
};

struct CommitDerivedLayerRequest {
    domain::ProjectId projectId;
    QString projectRoot; // runtime absolute path, never persisted
    domain::PageId pageId;
    DerivedLayerKind layerKind{DerivedLayerKind::Clear};
    QImage layer; // full-page RGBA8, straight alpha, sRGB
};

struct CommitDerivedLayerResponse {
    // Project-root relative, content-addressed path of the committed layer.
    QString relativePath;
};

struct PruneAssetsRequest {
    domain::ProjectId projectId;
    QString projectRoot;                   // runtime absolute path, never persisted
    QSet<QString> referencedRelativePaths; // project-root relative paths to keep
};

struct PruneAssetsResponse {
    int removedFiles{0};
    qint64 freedBytes{0};
};

/// Narrow port for reading and atomically committing page derived layers, plus pruning
/// unreferenced committed layers. Implementations must bind the request to the canonical
/// project root and PageId, derive the output path themselves (content-addressed, never
/// caller supplied), and reach a file only while that project session is open and only through
/// a reference that resolves inside the canonical root. ProjectIoCoordinator and the data layer
/// path safety helpers own those rules. Source images are never modified.
class IDerivedAssetStore {
public:
    virtual ~IDerivedAssetStore() = default;

    /// Reads a committed layer; a missing file is an error, not an empty image. The work runs on a
    /// storage worker thread and fulfills the returned future there, so a continuation inherits that
    /// affinity and marshals to the GUI thread itself. The request and context are taken by value,
    /// while the implementation must outlive the returned future. A stop requested before the read
    /// starts is reported as cancelled.user_requested; a stop requested during a decode is ignored.
    /// Invalid references, a closed session, a missing file, and an undecodable file all report
    /// clear.layer_read_failed with category Persistence.
    [[nodiscard]] virtual QFuture<Result<QImage>> readDerivedLayer(ReadDerivedLayerRequest request,
                                                                   OperationContext context) = 0;
    /// Stages and atomically commits the layer; committing identical content reuses the
    /// existing file and different content never overwrites an existing file. The commit runs on a
    /// storage worker thread and fulfills the returned future there. A stop requested before the
    /// staged file is committed is reported as cancelled.user_requested, and the commit does not
    /// recheck the token afterwards. Rejections report clear.layer_commit_failed with category
    /// Persistence, including a layer that is not full-page RGBA8888 and a path whose existing
    /// content does not match its hash.
    [[nodiscard]] virtual QFuture<Result<CommitDerivedLayerResponse>>
    commitDerivedLayer(CommitDerivedLayerRequest request, OperationContext context) = 0;
    /// Deletes every committed image under the project derived tree whose project-relative path is
    /// absent from the request, so the reference set must list each asset the caller wants kept.
    /// Removing one file never aborts the prune, and empty parent directories are removed best
    /// effort. A stop requested before the sweep starts is reported as cancelled.user_requested; a
    /// stop requested during the sweep ends it early and the result reports the partial counts as
    /// success. Rejections report clear.layer_prune_failed with category Persistence. The work runs
    /// on a storage worker thread and fulfills the returned future there.
    [[nodiscard]] virtual QFuture<Result<PruneAssetsResponse>> pruneUnusedAssets(PruneAssetsRequest request,
                                                                                 OperationContext context) = 0;
};

} // namespace manga::application
