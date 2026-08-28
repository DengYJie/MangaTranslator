#pragma once

#include "application/error/ApplicationError.h"
#include "domain/model/TextRegion.h"

#include <QImage>
#include <QString>
#include <QVector>

#include <vector>

namespace manga::application {

enum class ExportFormat {
    Png,
    Jpeg
};

struct ExportPageSnapshot {
    domain::PageId pageId;
    QImage sourceImage;
    std::vector<domain::TextRegion> regions;
};

struct ExportRequest {
    QVector<ExportPageSnapshot> pages;
    QString outputDir;
    ExportFormat format;
    int jpegQuality{95};
};

struct ExportedPage {
    domain::PageId pageId;
    QString path;
};

struct ExportFailure {
    domain::PageId pageId;
    ApplicationError error;
};

struct ExportResponse {
    QVector<ExportedPage> exported;
    QVector<ExportFailure> failures;
};

} // namespace manga::application
