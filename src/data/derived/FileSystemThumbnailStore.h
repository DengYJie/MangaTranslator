#pragma once

#include "application/ports/IThumbnailStore.h"

namespace manga::data {

class FileSystemThumbnailStore final : public application::IThumbnailStore {
public:
    FileSystemThumbnailStore() = default;

    [[nodiscard]] QFuture<application::Result<QImage>> readThumbnail(application::ReadThumbnailRequest request,
                                                                     application::OperationContext context) override;

    [[nodiscard]] QFuture<application::Result<void>> writeThumbnail(application::WriteThumbnailRequest request,
                                                                    application::OperationContext context) override;
};

} // namespace manga::data
