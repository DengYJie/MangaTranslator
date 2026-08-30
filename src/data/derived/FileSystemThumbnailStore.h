#pragma once

#include "application/ports/IThumbnailStore.h"

namespace manga::data {

class ProjectIoCoordinator;

class FileSystemThumbnailStore final : public application::IThumbnailStore {
public:
    explicit FileSystemThumbnailStore(ProjectIoCoordinator* coordinator);

    [[nodiscard]] QFuture<application::Result<QImage>> readThumbnail(application::ReadThumbnailRequest request,
                                                                     application::OperationContext context) override;

    [[nodiscard]] QFuture<application::Result<void>> writeThumbnail(application::WriteThumbnailRequest request,
                                                                    application::OperationContext context) override;

private:
    ProjectIoCoordinator* m_coordinator;
};

} // namespace manga::data
