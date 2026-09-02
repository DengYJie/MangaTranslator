#pragma once

#include "application/ports/IThumbnailStore.h"

namespace manga::data {

class ProjectIoCoordinator;

/// Filesystem store for the one derived thumbnail a page owns. A thumbnail is addressed by an open
/// project session plus a page id and never by a caller-supplied path: the adapter builds the
/// single fixed name assets/derived/<page-id>/thumbnail/v1.png itself and confines every read and
/// write inside the canonical project root. Read and write share that one builder, so the version
/// segment is where a future stored form rolls forward instead of mixing two layouts in one
/// project. Both operations fail when the session is not open rather than touching disk.
class FileSystemThumbnailStore final : public application::IThumbnailStore {
public:
    explicit FileSystemThumbnailStore(ProjectIoCoordinator* coordinator);

    [[nodiscard]] QFuture<application::Result<QImage>> readThumbnail(application::ReadThumbnailRequest request,
                                                                     application::OperationContext context) override;

    [[nodiscard]] QFuture<application::Result<void>> writeThumbnail(application::WriteThumbnailRequest request,
                                                                    application::OperationContext context) override;

private:
    ProjectIoCoordinator* m_coordinator; // Non-owning; must outlive this store.
};

} // namespace manga::data
