#pragma once

#include "application/error/ApplicationError.h"
#include "domain/model/Uuid.h"

#include <QImage>
#include <QString>
#include <QVector>

namespace manga::application {

struct ImportImagesRequest {
    domain::ProjectId projectId;
    QVector<QString> filePaths;
};

struct ImportedPage {
    domain::PageId pageId;
    QString assetPath;
    int orderIndex{0};
};

struct ImportFailure {
    QString path;
    ApplicationError error;
};

struct ImportImagesResult {
    QVector<ImportedPage> importedPages;
    QVector<ImportFailure> failures;
};

struct LoadPageImageRequest {
    domain::ProjectId projectId;
    domain::PageId pageId;
    QString assetPath;
};

} // namespace manga::application
