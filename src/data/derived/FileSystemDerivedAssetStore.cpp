#include "data/derived/FileSystemDerivedAssetStore.h"

#include "data/DataAsync.h"
#include "data/project/PathSafety.h"
#include "data/project/ProjectLayout.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QSaveFile>

#include <memory>
#include <utility>

namespace manga::data {
namespace {

// The stored name of a committed layer outlives the session: project.json records the relative
// path verbatim, a read rejects any stored prefix these helpers no longer produce, and the prune
// walk starts from this root. Renaming the root, a kind directory or the version segment strands
// the assets of every existing project rather than migrating them.
constexpr QLatin1String kVersionSegment("v1");

QString layerKindDirectory(application::DerivedLayerKind kind) {
    switch (kind) {
        case application::DerivedLayerKind::Clear:
            return QStringLiteral("cleaning");
        case application::DerivedLayerKind::Translation:
            return QStringLiteral("translation");
        case application::DerivedLayerKind::ManualOverride:
            return QStringLiteral("manual");
    }
    return QStringLiteral("cleaning");
}

application::ApplicationError makeStorageError(const QString& code, const QString& message) {
    return application::makeApplicationError(application::ErrorCategory::Persistence, code, message);
}

/// Content hash over the normalized RGBA bytes with the dimensions mixed in, so identical
/// pixels at different sizes never collide.
[[nodiscard]] QString layerContentHash(const QImage& layer) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArray::number(layer.width()));
    hash.addData(QByteArrayLiteral(":"));
    hash.addData(QByteArray::number(layer.height()));
    hash.addData(QByteArrayLiteral(":"));
    for (int y = 0; y < layer.height(); ++y) {
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(layer.constScanLine(y)), layer.width() * 4));
    }
    return QString::fromLatin1(hash.result().toHex());
}

/// Prefix that owns every layer version of one page and kind. Requests may only reach paths
/// under their own page and kind directory.
[[nodiscard]] QString layerDirectoryPrefix(const domain::PageId& pageId, application::DerivedLayerKind kind) {
    return QStringLiteral("%1/%2/%3/")
        .arg(kAssetsDerived.toString(), QString::fromStdString(pageId.toString()), layerKindDirectory(kind));
}

QString layerRelativePath(const domain::PageId& pageId, const QString& kindDir, const QString& hash) {
    return QStringLiteral("%1/%2/%3/%4/%5.png")
        .arg(kAssetsDerived.toString(),
             QString::fromStdString(pageId.toString()),
             kindDir,
             kVersionSegment.toString(),
             hash);
}

} // namespace

FileSystemDerivedAssetStore::FileSystemDerivedAssetStore(ProjectIoCoordinator* coordinator)
    : m_coordinator(coordinator) {}

FileSystemDerivedAssetStore::AssetLease FileSystemDerivedAssetStore::createAssetLease() {
    auto lease = std::make_shared<QSet<QString>>();
    std::weak_ptr<QSet<QString>> weak(lease);
    QMutexLocker locker(&m_leasesMutex);
    m_leases.append(weak);
    return lease;
}

QFuture<application::Result<QImage>>
FileSystemDerivedAssetStore::readDerivedLayer(application::ReadDerivedLayerRequest request,
                                              application::OperationContext context) {
    return runDataOperation<QImage>([this, request = std::move(request), context]() -> application::Result<QImage> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                                     QStringLiteral("cancelled.user_requested"),
                                                                     QStringLiteral("The layer read was cancelled")));
        }
        const QString canonicalRoot =
            canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
        if (canonicalRoot.isEmpty()) {
            return std::unexpected(makeStorageError(QStringLiteral("clear.layer_read_failed"),
                                                    QStringLiteral("The project root is not available")));
        }
        if (!request.projectId.isValid() || !request.pageId.isValid() ||
            !isSafeRelativeAssetPath(request.relativePath) ||
            !request.relativePath.startsWith(layerDirectoryPrefix(request.pageId, request.layerKind))) {
            return std::unexpected(makeStorageError(QStringLiteral("clear.layer_read_failed"),
                                                    QStringLiteral("The layer reference is invalid")));
        }
        auto lease = m_coordinator->acquireProjectLease(request.projectId, canonicalRoot);
        if (!lease) {
            return std::unexpected(makeStorageError(QStringLiteral("clear.layer_read_failed"),
                                                    QStringLiteral("The project session is not open")));
        }
        const QString resolved = resolveInsideCanonicalRoot(canonicalRoot, request.relativePath);
        if (resolved.isEmpty()) {
            return std::unexpected(
                makeStorageError(QStringLiteral("clear.layer_read_failed"),
                                 QStringLiteral("The layer file is missing or outside the project")));
        }
        QImageReader reader(resolved);
        reader.setAutoTransform(false);
        QImage image = reader.read();
        if (image.isNull()) {
            return std::unexpected(makeStorageError(QStringLiteral("clear.layer_read_failed"),
                                                    QStringLiteral("The layer file could not be decoded")));
        }
        return image;
    });
}

QFuture<application::Result<application::CommitDerivedLayerResponse>>
FileSystemDerivedAssetStore::commitDerivedLayer(application::CommitDerivedLayerRequest request,
                                                application::OperationContext context) {
    return runDataOperation<application::CommitDerivedLayerResponse>(
        [this,
         request = std::move(request),
         context]() -> application::Result<application::CommitDerivedLayerResponse> {
            if (context.stopToken.stop_requested()) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                      QStringLiteral("cancelled.user_requested"),
                                                      QStringLiteral("The layer commit was cancelled")));
            }
            const QString canonicalRoot =
                canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
            const auto fail = [](const QString& message) {
                return std::unexpected(makeStorageError(QStringLiteral("clear.layer_commit_failed"), message));
            };
            if (canonicalRoot.isEmpty() || !request.pageId.isValid() || !request.projectId.isValid()) {
                return fail(QStringLiteral("The commit request is invalid"));
            }
            if (request.layer.isNull() || request.layer.width() <= 0 || request.layer.height() <= 0 ||
                request.layer.format() != QImage::Format_RGBA8888) {
                return fail(QStringLiteral("The layer must be a non-empty RGBA8 image"));
            }
            auto writeTicket = m_coordinator->beginProjectWrite(request.projectId, canonicalRoot);
            if (!writeTicket) {
                return fail(QStringLiteral("The project session is not open"));
            }

            const QString hash = layerContentHash(request.layer);
            const QString relativePath = layerRelativePath(request.pageId, layerKindDirectory(request.layerKind), hash);
            const QString finalPath = canonicalRoot + QLatin1Char('/') + relativePath;

            // Identical content reuses the existing decodable file; different content lands
            // under a different hash and never overwrites anything.
            if (QFileInfo::exists(finalPath)) {
                QImageReader reader(finalPath);
                QImage existing = reader.read();
                if (!existing.isNull()) {
                    if (existing.format() != QImage::Format_RGBA8888) {
                        existing = existing.convertToFormat(QImage::Format_RGBA8888);
                    }
                    if (existing.size() == request.layer.size() && layerContentHash(existing) == hash) {
                        application::CommitDerivedLayerResponse response;
                        response.relativePath = relativePath;
                        return response;
                    }
                }
                return fail(
                    QStringLiteral("The derived asset exists but its content does not match the expected hash"));
            }

            if (!QDir().mkpath(QFileInfo(finalPath).absolutePath())) {
                return fail(QStringLiteral("The layer directory could not be created"));
            }
            QSaveFile file(finalPath);
            file.setDirectWriteFallback(false);
            if (!file.open(QIODevice::WriteOnly)) {
                return fail(QStringLiteral("The layer file could not be created"));
            }
            if (!request.layer.save(&file, "PNG")) {
                file.cancelWriting();
                return fail(QStringLiteral("The layer file could not be encoded"));
            }
            if (context.stopToken.stop_requested()) {
                file.cancelWriting();
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                      QStringLiteral("cancelled.user_requested"),
                                                      QStringLiteral("The layer commit was cancelled")));
            }
            if (!file.commit()) {
                return fail(QStringLiteral("The layer file could not be committed"));
            }
            application::CommitDerivedLayerResponse response;
            response.relativePath = relativePath;
            return response;
        });
}

QFuture<application::Result<application::PruneAssetsResponse>>
FileSystemDerivedAssetStore::pruneUnusedAssets(application::PruneAssetsRequest request,
                                               application::OperationContext context) {
    return runDataOperation<application::PruneAssetsResponse>(
        [this, request = std::move(request), context]() -> application::Result<application::PruneAssetsResponse> {
            if (context.stopToken.stop_requested()) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Cancelled,
                                                      QStringLiteral("cancelled.user_requested"),
                                                      QStringLiteral("The asset prune was cancelled")));
            }
            const QString canonicalRoot =
                canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
            const auto fail = [](const QString& message) {
                return std::unexpected(makeStorageError(QStringLiteral("clear.layer_prune_failed"), message));
            };
            if (canonicalRoot.isEmpty()) {
                return fail(QStringLiteral("The project root is not available"));
            }
            if (!request.projectId.isValid()) {
                return fail(QStringLiteral("The prune request is invalid"));
            }
            auto writeTicket = m_coordinator->beginProjectWrite(request.projectId, canonicalRoot);
            if (!writeTicket) {
                return fail(QStringLiteral("The project session is not open"));
            }

            QSet<QString> marked = request.referencedRelativePaths;
            {
                // Live leases pin their paths; expired lease handles are dropped.
                QMutexLocker locker(&m_leasesMutex);
                QVector<std::weak_ptr<QSet<QString>>> retained;
                retained.reserve(m_leases.size());
                for (std::weak_ptr<QSet<QString>>& lease : m_leases) {
                    if (std::shared_ptr<QSet<QString>> live = lease.lock()) {
                        marked.unite(*live);
                        retained.append(lease);
                    }
                }
                m_leases = std::move(retained);
            }

            const QString derivedRoot = canonicalRoot + QLatin1Char('/') + kAssetsDerived.toString();
            if (!QDir(derivedRoot).exists()) {
                application::PruneAssetsResponse response;
                return response;
            }

            application::PruneAssetsResponse response;
            QSet<QString> touchedLeaves;
            QDirIterator iterator(derivedRoot, {QStringLiteral("*.png")}, QDir::Files, QDirIterator::Subdirectories);
            while (iterator.hasNext()) {
                const QString absolutePath = iterator.next();
                const QString relativePath =
                    QDir::cleanPath(QDir::fromNativeSeparators(absolutePath.mid(canonicalRoot.length() + 1)));
                if (marked.contains(relativePath)) {
                    continue;
                }
                QFileInfo fileInfo(absolutePath);
                if (QFile::remove(absolutePath)) {
                    ++response.removedFiles;
                    response.freedBytes += fileInfo.size();
                    touchedLeaves.insert(fileInfo.absolutePath());
                }
                if (context.stopToken.stop_requested()) {
                    break;
                }
            }

            // Empty directory cleanup is best effort; rmdir only succeeds on empty directories.
            for (const QString& leaf : touchedLeaves) {
                QString directory = leaf;
                while (directory.startsWith(derivedRoot) && directory != derivedRoot) {
                    QDir().rmdir(directory);
                    directory = QFileInfo(directory).absolutePath();
                }
            }
            return response;
        });
}

} // namespace manga::data
