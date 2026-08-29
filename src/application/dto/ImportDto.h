#pragma once

#include "application/error/ApplicationError.h"
#include "application/operation/OperationId.h"
#include "domain/model/ComicPage.h"

#include <QImage>
#include <QString>
#include <QVector>

namespace manga::application {

struct ImportImagesRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    QVector<QString> filePaths;
};

struct ImportWarning {
    QString path;
    QString code;
    QString safeMessage;
};

struct ImportedPage {
    domain::ComicPage page;
    QString sourcePath;
    QVector<ImportWarning> warnings;
};

struct ImportFailure {
    QString path;
    ApplicationError error;
};

struct ImportImagesResult {
    // Capability for resolving assets that have not yet been adopted by the domain.
    OperationId batchId;
    QVector<ImportedPage> importedPages;
    QVector<ImportFailure> failures;
};

struct DiscardImportedAssetsRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    // The importer accepts only paths recorded under this pending batch.
    OperationId batchId;
    QVector<QString> assetPaths;
};

struct AcknowledgeImportedAssetsRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    // Consuming the batch prevents later cleanup from deleting adopted assets.
    OperationId batchId;
};

struct LoadPageImageRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    domain::PageId pageId;
    QString assetPath;
    // Guards decoded content against stale or mismatched project metadata.
    domain::ImageSize expectedSize;
};

} // namespace manga::application
