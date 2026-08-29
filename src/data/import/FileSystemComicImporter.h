#pragma once

#include "application/ports/IComicImporter.h"
#include "data/project/ProjectIoCoordinator.h"

#include <QFuture>
#include <QMutex>
#include <QPromise>
#include <QSet>

#include <functional>
#include <map>
#include <string>

namespace manga::data {

/// Filesystem-backed comic importer for V0.1: top-level image files and image folders (JPG,
/// PNG, WEBP). Each file is staged inside the project, probed by content, normalized, and
/// atomically committed to assets/original with a page_NNNN name assigned by scanning the
/// highest existing number. All request files run inside one coordinator write section.
class FileSystemComicImporter final : public application::IComicImporter {
public:
    /// Observes committed assets while the project write ticket is held.
    using CommitObserver = std::function<void(int committedAssetCount)>;

    explicit FileSystemComicImporter(ProjectIoCoordinator* coordinator, CommitObserver commitObserver = {});

    [[nodiscard]] QFuture<application::Result<application::ImportImagesResult>>
    importImages(application::ImportImagesRequest request, application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<void>>
    discardImportedAssets(application::DiscardImportedAssetsRequest request,
                          application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<void>>
    acknowledgeImportedAssets(application::AcknowledgeImportedAssetsRequest request,
                              application::OperationContext context) override;

private:
    [[nodiscard]] application::Result<application::ImportImagesResult>
    importImagesBlocking(const application::ImportImagesRequest& request,
                         application::OperationContext context,
                         QPromise<application::Result<application::ImportImagesResult>>& promise);

    ProjectIoCoordinator* m_coordinator;
    CommitObserver m_commitObserver;
    // Acknowledgement or complete discard consumes the pending batch capability.
    struct PendingBatch {
        domain::ProjectId projectId;
        QString canonicalRoot;
        QSet<QString> assetPaths;
    };
    QMutex m_batchesMutex;
    std::map<std::string, PendingBatch> m_pendingBatches;
};

} // namespace manga::data
