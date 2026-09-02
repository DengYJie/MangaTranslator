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
/// caller supplied), and reuse the P3 session and path containment rules. Source images
/// are never modified.
class IDerivedAssetStore {
public:
    virtual ~IDerivedAssetStore() = default;

    /// Reads a committed layer; a missing file is an error, not an empty image.
    [[nodiscard]] virtual QFuture<Result<QImage>> readDerivedLayer(ReadDerivedLayerRequest request,
                                                                   OperationContext context) = 0;
    /// Stages and atomically commits the layer; committing identical content reuses the
    /// existing file and different content never overwrites an existing file.
    [[nodiscard]] virtual QFuture<Result<CommitDerivedLayerResponse>>
    commitDerivedLayer(CommitDerivedLayerRequest request, OperationContext context) = 0;
    /// Removes committed layers whose project-relative paths are neither referenced by the
    /// request nor protected by an active asset lease. Removing a file never aborts the
    /// prune; empty parent directories are removed best effort.
    [[nodiscard]] virtual QFuture<Result<PruneAssetsResponse>> pruneUnusedAssets(PruneAssetsRequest request,
                                                                                 OperationContext context) = 0;
};

} // namespace manga::application
