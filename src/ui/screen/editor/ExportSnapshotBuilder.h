#pragma once

#include "application/dto/ExportDto.h"
#include "application/error/ApplicationError.h"
#include "application/operation/OperationContext.h"
#include "domain/model/ComicProject.h"

#include <QFuture>
#include <QImage>
#include <QString>
#include <QVector>

namespace manga::application {
class IPageImageLoader;
class IDerivedAssetStore;
} // namespace manga::application

namespace manga::ui::screen::editor {

/// Builds immutable ExportPageSnapshots from the open session: source images via
/// LoadPageImageUseCase, committed derived layers via IDerivedAssetStore, and translation
/// layers rasterized with rendering::TextLayoutEngine + TextRasterizer. Canvas preview uses the
/// same rendering entry points, so preview and export share one result.
class ExportSnapshotBuilder {
public:
    ExportSnapshotBuilder(domain::ComicProject* project,
                          application::IPageImageLoader* loader,
                          application::IDerivedAssetStore* derivedStore);

    struct Request {
        QString projectRoot;
        QVector<domain::PageId> pageIds; // empty means all pages in project order
        double logicalDpi{96.0};
    };

    struct PreviewResult {
        QImage compositeImage;
        QImage cleaningImage;
    };

    [[nodiscard]] QFuture<application::Result<QVector<application::ExportPageSnapshot>>>
    execute(const Request& request, const application::OperationContext& context);

    [[nodiscard]] QFuture<application::Result<PreviewResult>>
    executePreview(const Request& request, const application::OperationContext& context);

private:
    domain::ComicProject* m_project;
    application::IPageImageLoader* m_loader;
    application::IDerivedAssetStore* m_derivedStore;
};

} // namespace manga::ui::screen::editor
