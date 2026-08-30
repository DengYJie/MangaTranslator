#include "FileSystemThumbnailStore.h"

#include "data/DataAsync.h"
#include "data/project/PathSafety.h"
#include "data/project/ProjectIoCoordinator.h"

#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>

#include <utility>

namespace manga::data {
namespace {

[[nodiscard]] QString thumbnailRelativePath(const domain::PageId& pageId) {
    return QStringLiteral("assets/derived/%1/thumbnail/v1.png").arg(QString::fromStdString(pageId.toString()));
}

[[nodiscard]] application::ApplicationError thumbnailError(const QString& code, const QString& message) {
    return application::makeApplicationError(application::ErrorCategory::Persistence, code, message);
}

[[nodiscard]] application::ApplicationError cancelledError() {
    return application::makeApplicationError(application::ErrorCategory::Cancelled,
                                             QStringLiteral("cancelled.user_requested"),
                                             QStringLiteral("The thumbnail operation was cancelled"));
}

} // namespace

FileSystemThumbnailStore::FileSystemThumbnailStore(ProjectIoCoordinator* coordinator) : m_coordinator(coordinator) {}

QFuture<application::Result<QImage>> FileSystemThumbnailStore::readThumbnail(application::ReadThumbnailRequest request,
                                                                             application::OperationContext context) {
    return runDataOperation<QImage>([this, request = std::move(request), context]() -> application::Result<QImage> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(cancelledError());
        }
        const QString canonicalRoot =
            canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
        if (m_coordinator == nullptr || canonicalRoot.isEmpty() || !request.projectId.isValid() ||
            !request.pageId.isValid()) {
            return std::unexpected(thumbnailError(QStringLiteral("thumbnail.read_failed"),
                                                  QStringLiteral("The thumbnail request is invalid")));
        }
        auto lease = m_coordinator->acquireProjectLease(request.projectId, canonicalRoot);
        if (!lease) {
            return std::unexpected(thumbnailError(QStringLiteral("thumbnail.read_failed"),
                                                  QStringLiteral("The project session is not open")));
        }
        const QString resolved = resolveInsideCanonicalRoot(canonicalRoot, thumbnailRelativePath(request.pageId));
        if (resolved.isEmpty()) {
            return std::unexpected(thumbnailError(QStringLiteral("thumbnail.not_found"),
                                                  QStringLiteral("The thumbnail is not available")));
        }
        QImageReader reader(resolved, "PNG");
        reader.setAutoTransform(false);
        QImage image = reader.read();
        if (image.isNull()) {
            return std::unexpected(thumbnailError(QStringLiteral("thumbnail.read_failed"),
                                                  QStringLiteral("The thumbnail could not be decoded")));
        }
        return image;
    });
}

QFuture<application::Result<void>> FileSystemThumbnailStore::writeThumbnail(application::WriteThumbnailRequest request,
                                                                            application::OperationContext context) {
    return runDataOperation<void>([this, request = std::move(request), context]() -> application::Result<void> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(cancelledError());
        }
        const QString canonicalRoot =
            canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
        const auto fail = [](const QString& message) {
            return std::unexpected(thumbnailError(QStringLiteral("thumbnail.commit_failed"), message));
        };
        if (m_coordinator == nullptr || canonicalRoot.isEmpty() || !request.projectId.isValid() ||
            !request.pageId.isValid() || request.thumbnail.isNull()) {
            return fail(QStringLiteral("The thumbnail request is invalid"));
        }
        auto writeTicket = m_coordinator->beginProjectWrite(request.projectId, canonicalRoot);
        if (!writeTicket) {
            return fail(QStringLiteral("The project session is not open"));
        }
        const QString relativePath = thumbnailRelativePath(request.pageId);
        const QString finalPath = canonicalRoot + QLatin1Char('/') + relativePath;
        const QString parentPath = QFileInfo(finalPath).absolutePath();
        if (!QDir().mkpath(parentPath)) {
            return fail(QStringLiteral("The thumbnail directory could not be created"));
        }
        const QString resolvedParent = canonicalPathResolved(parentPath);
        if (resolvedParent.isEmpty() || !isInsideCanonicalRoot(canonicalRoot, resolvedParent)) {
            return fail(QStringLiteral("The thumbnail path is outside the project"));
        }

        QSaveFile file(finalPath);
        file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) {
            return fail(QStringLiteral("The thumbnail staging file could not be created"));
        }
        if (!request.thumbnail.save(&file, "PNG")) {
            file.cancelWriting();
            return fail(QStringLiteral("The thumbnail could not be encoded"));
        }
        if (context.stopToken.stop_requested()) {
            file.cancelWriting();
            return std::unexpected(cancelledError());
        }
        if (!file.commit()) {
            return fail(QStringLiteral("The thumbnail could not be committed"));
        }
        return {};
    });
}

} // namespace manga::data
