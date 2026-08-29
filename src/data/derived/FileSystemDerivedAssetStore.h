#pragma once

#include "application/ports/IDerivedAssetStore.h"
#include "project/ProjectIoCoordinator.h"

#include <QFuture>

namespace manga::data {

/// Filesystem-backed derived asset store. Committed layers are content-addressed PNGs under
/// assets/derived/<page-id>/<kind>/v1/<sha256>.png; identical content reuses the existing
/// decodable file, different content never overwrites. Path building and containment reuse
/// the P3 rules; the source image is never modified.
class FileSystemDerivedAssetStore final : public application::IDerivedAssetStore {
public:
    explicit FileSystemDerivedAssetStore(ProjectIoCoordinator* coordinator);

    [[nodiscard]] QFuture<application::Result<QImage>> readDerivedLayer(application::ReadDerivedLayerRequest request,
                                                                        application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<application::CommitDerivedLayerResponse>>
    commitDerivedLayer(application::CommitDerivedLayerRequest request, application::OperationContext context) override;

private:
    ProjectIoCoordinator* m_coordinator;
};

} // namespace manga::data
