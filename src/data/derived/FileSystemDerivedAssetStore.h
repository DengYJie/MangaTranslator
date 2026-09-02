#pragma once

#include "application/ports/IDerivedAssetStore.h"
#include "data/project/ProjectIoCoordinator.h"

#include <QFuture>
#include <QMutex>
#include <QSet>
#include <QVector>

#include <memory>

namespace manga::data {

/// Filesystem-backed derived asset store. Committed layers are content-addressed PNGs under
/// assets/derived/<page-id>/<kind>/v1/<sha256>.png, where the kind segment is cleaning,
/// translation or manual. Identical content reuses the existing decodable file, different content
/// never overwrites, and the source image is never modified. Every path is built from the page id
/// and kind carried by the request and then confined inside the canonical project root, so a
/// caller cannot address a file outside its own page and kind directory.
class FileSystemDerivedAssetStore final : public application::IDerivedAssetStore {
public:
    /// Lease over project-relative derived layer paths. A live lease keeps its paths out of
    /// the next prune; the store never mutates the set itself.
    using AssetLease = std::shared_ptr<QSet<QString>>;

    explicit FileSystemDerivedAssetStore(ProjectIoCoordinator* coordinator);

    [[nodiscard]] QFuture<application::Result<QImage>> readDerivedLayer(application::ReadDerivedLayerRequest request,
                                                                        application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<application::CommitDerivedLayerResponse>>
    commitDerivedLayer(application::CommitDerivedLayerRequest request, application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<application::PruneAssetsResponse>>
    pruneUnusedAssets(application::PruneAssetsRequest request, application::OperationContext context) override;

    /// Creates a lease that pins project-relative derived layer paths during a prune. Fill the
    /// returned set before handing the lease to background work and keep it stable while a
    /// prune may read it; releasing the last shared_ptr drops the protection.
    [[nodiscard]] AssetLease createAssetLease();

private:
    ProjectIoCoordinator* m_coordinator;
    QMutex m_leasesMutex;
    QVector<std::weak_ptr<QSet<QString>>> m_leases;
};

} // namespace manga::data
