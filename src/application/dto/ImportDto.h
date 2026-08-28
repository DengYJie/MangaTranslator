#pragma once

#include "application/error/ApplicationError.h"
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
    QVector<ImportedPage> importedPages;
    QVector<ImportFailure> failures;
};

struct DiscardImportedAssetsRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    QVector<QString> assetPaths;
};

struct LoadPageImageRequest {
    domain::ProjectId projectId;
    QString projectRoot;
    domain::PageId pageId;
    QString assetPath;
};

} // namespace manga::application
