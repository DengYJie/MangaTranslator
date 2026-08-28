#pragma once

#include "application/ports/IComicImporter.h"
#include "data/project/ProjectIoCoordinator.h"

#include <QFuture>
#include <QPromise>

namespace manga::data {

/// Filesystem-backed comic importer for V0.1: top-level image files and image folders (JPG,
/// PNG, WEBP). Each file is staged inside the project, probed by content, normalized, and
/// atomically committed to assets/original with a page_NNNN name assigned by scanning the
/// highest existing number. All request files run inside one coordinator write section.
class FileSystemComicImporter final : public application::IComicImporter {
public:
    explicit FileSystemComicImporter(ProjectIoCoordinator* coordinator);

    [[nodiscard]] QFuture<application::Result<application::ImportImagesResult>>
    importImages(application::ImportImagesRequest request, application::OperationContext context) override;
    [[nodiscard]] QFuture<application::Result<void>>
    discardImportedAssets(application::DiscardImportedAssetsRequest request,
                          application::OperationContext context) override;

private:
    [[nodiscard]] application::Result<application::ImportImagesResult>
    importImagesBlocking(const application::ImportImagesRequest& request,
                         application::OperationContext context,
                         QPromise<application::Result<application::ImportImagesResult>>& promise);

    ProjectIoCoordinator* m_coordinator;
};

} // namespace manga::data
