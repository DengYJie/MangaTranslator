#include "data/import/FileSystemPageImageLoader.h"

#include "data/DataAsync.h"
#include "data/DataErrorMapping.h"
#include "data/image/ImageNormalization.h"
#include "data/project/PathSafety.h"

#include <QDir>
#include <QFileInfo>

namespace manga::data {

FileSystemPageImageLoader::FileSystemPageImageLoader(ProjectIoCoordinator* coordinator) : m_coordinator(coordinator) {}

QFuture<application::Result<QImage>> FileSystemPageImageLoader::loadPageImage(application::LoadPageImageRequest request,
                                                                              application::OperationContext context) {
    return runDataOperation<QImage>([this, request = std::move(request), context]() -> application::Result<QImage> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                     QStringLiteral("cancelled.decode"),
                                                                     QStringLiteral("Image loading was cancelled")));
        }
        const QString canonicalRoot =
            canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
        if (canonicalRoot.isEmpty()) {
            return std::unexpected(fileNotFoundError());
        }
        auto lease = m_coordinator->acquireProjectLease(request.projectId, canonicalRoot);
        if (!lease) {
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Project,
                                                  QStringLiteral("project.not_open"),
                                                  QStringLiteral("The project session is not open")));
        }

        const QString assetPath = QDir::fromNativeSeparators(request.assetPath.trimmed());
        if (!isSafeRelativeAssetPath(assetPath)) {
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Input,
                                                  QStringLiteral("input.invalid_image_request"),
                                                  QStringLiteral("The page image request is invalid")));
        }
        const QFileInfo direct(canonicalRoot + QLatin1Char('/') + assetPath);
        const QString resolved = resolveInsideCanonicalRoot(canonicalRoot, assetPath);
        if (resolved.isEmpty()) {
            if (direct.exists()) {
                // The file exists but resolves outside the canonical root (symlink, junction,
                // or reparse point redirect).
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Input,
                                                      QStringLiteral("input.invalid_image_request"),
                                                      QStringLiteral("The page image request is invalid")));
            }
            return std::unexpected(fileNotFoundError());
        }
        if (!QFileInfo(resolved).isFile() || !QFileInfo(resolved).isReadable()) {
            return std::unexpected(fileNotFoundError());
        }

        if (context.stopToken.stop_requested()) {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                     QStringLiteral("cancelled.decode"),
                                                                     QStringLiteral("Image loading was cancelled")));
        }
        auto normalized = loadNormalizedImage(resolved, context.stopToken);
        if (!normalized) {
            return std::unexpected(normalized.error());
        }
        if (context.stopToken.stop_requested()) {
            // A Qt decode cannot be aborted mid-flight; the finished image is discarded instead.
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                     QStringLiteral("cancelled.decode"),
                                                                     QStringLiteral("Image loading was cancelled")));
        }
        if (normalized->image.width() != request.expectedSize.width ||
            normalized->image.height() != request.expectedSize.height) {
            return std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Project,
                QStringLiteral("project.image_dimension_mismatch"),
                QStringLiteral("The page image dimensions do not match the project")));
        }
        return normalized->image;
    });
}

} // namespace manga::data
