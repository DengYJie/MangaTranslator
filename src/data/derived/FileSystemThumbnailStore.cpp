#include "FileSystemThumbnailStore.h"

#include "data/DataAsync.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>

#include <utility>

namespace manga::data {

QFuture<application::Result<QImage>> FileSystemThumbnailStore::readThumbnail(application::ReadThumbnailRequest request,
                                                                             application::OperationContext context) {
    return runDataOperation<QImage>([request = std::move(request), context]() -> application::Result<QImage> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Cancelled, "cancelled.read_thumbnail", "Read thumbnail cancelled"));
        }

        QDir projectDir(request.projectRoot);
        QString thumbPath = projectDir.filePath(QStringLiteral(".mangatranslator/thumbnails/%1.jpg")
                                                    .arg(QString::fromStdString(request.pageId.toString())));

        if (!QFileInfo::exists(thumbPath)) {
            return std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Persistence, "not_found.thumbnail", "Thumbnail not found"));
        }

        QImageReader reader(thumbPath);
        QImage image = reader.read();
        if (image.isNull()) {
            return std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Persistence, "read_error.thumbnail", "Failed to read thumbnail image"));
        }

        return image;
    });
}

QFuture<application::Result<void>> FileSystemThumbnailStore::writeThumbnail(application::WriteThumbnailRequest request,
                                                                            application::OperationContext context) {
    return runDataOperation<void>([request = std::move(request), context]() -> application::Result<void> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Cancelled, "cancelled.write_thumbnail", "Write thumbnail cancelled"));
        }

        QDir projectDir(request.projectRoot);
        QString thumbDir = projectDir.filePath(QStringLiteral(".mangatranslator/thumbnails"));

        QDir dir(thumbDir);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Persistence,
                                                                     "write_error.thumbnail_dir",
                                                                     "Failed to create thumbnails directory"));
        }

        QString thumbPath =
            dir.filePath(QStringLiteral("%1.jpg").arg(QString::fromStdString(request.pageId.toString())));

        QImageWriter writer(thumbPath);
        writer.setQuality(85);
        if (!writer.write(request.thumbnail)) {
            return std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Persistence, "write_error.thumbnail", "Failed to write thumbnail image"));
        }

        return {};
    });
}

} // namespace manga::data
