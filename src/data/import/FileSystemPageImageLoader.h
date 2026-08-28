#pragma once

#include "application/ports/IPageImageLoader.h"
#include "data/project/ProjectIoCoordinator.h"

#include <QFuture>

namespace manga::data {

/// Filesystem-backed page image loader. Loading normalizes on the fly with the exact same
/// probe, EXIF, ICC, and RGBA8 functions used by the importer; original assets stay
/// byte-identical and no cache is part of the contract.
class FileSystemPageImageLoader final : public application::IPageImageLoader {
public:
    explicit FileSystemPageImageLoader(ProjectIoCoordinator* coordinator);

    [[nodiscard]] QFuture<application::Result<QImage>> loadPageImage(application::LoadPageImageRequest request,
                                                                     application::OperationContext context) override;

private:
    ProjectIoCoordinator* m_coordinator;
};

} // namespace manga::data
