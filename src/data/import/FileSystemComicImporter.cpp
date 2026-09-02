#include "data/import/FileSystemComicImporter.h"

#include "application/usecase/import/ImportUseCases.h"
#include "data/DataAsync.h"
#include "data/DataErrorMapping.h"
#include "data/image/ImageNormalization.h"
#include "data/project/PathSafety.h"
#include "data/project/ProjectLayout.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPromise>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QThreadPool>
#include <QUuid>

#include <optional>
#include <utility>

namespace manga::data {
namespace {

application::ApplicationError makeCancelledError(QStringView what) {
    return application::makeApplicationError(application::ErrorCategory::Cancelled,
                                             QStringLiteral("cancelled.import"),
                                             QStringLiteral("%1 was cancelled").arg(what));
}

application::ApplicationError makeProjectNotOpenError() {
    return application::makeApplicationError(application::ErrorCategory::Project,
                                             QStringLiteral("project.not_open"),
                                             QStringLiteral("The project session is not open"));
}

application::ApplicationError makeDuplicateImportError() {
    return application::makeApplicationError(application::ErrorCategory::Input,
                                             QStringLiteral("input.duplicate_import_path"),
                                             QStringLiteral("The same file was selected more than once"));
}

// A file selected directly or expanded from a top-level folder.
struct ImportCandidate {
    QString sourcePath;
    QString absolutePath;
};

// One top-level request entry: either a per-entry failure or the expanded candidate list.
struct ExpandedSource {
    std::optional<application::ImportFailure> failure;
    QVector<ImportCandidate> candidates;
};

QString duplicateKeyFor(const QString& absolutePath) {
    const QFileInfo info(absolutePath);
    QString key = info.canonicalFilePath();
    if (key.isEmpty()) {
        key = QDir::cleanPath(absolutePath);
    }
    return key.normalized(QString::NormalizationForm_KC).toCaseFolded();
}

QVector<ExpandedSource> expandSources(const QVector<QString>& filePaths) {
    QVector<ExpandedSource> expanded;
    expanded.reserve(filePaths.size());
    for (const QString& path : filePaths) {
        ExpandedSource source;
        const QFileInfo info(QDir::fromNativeSeparators(path));
        if (info.isDir()) {
            // Folder expansion is top-level only (no recursion) and uses the same natural
            // sort rule as the UseCase for the top-level request.
            QDir directory(info.absoluteFilePath());
            QStringList entries = directory.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Unsorted);
            std::stable_sort(entries.begin(), entries.end(), [](const QString& lhs, const QString& rhs) {
                const auto normalizedLhs = lhs.normalized(QString::NormalizationForm_KC);
                const auto normalizedRhs = rhs.normalized(QString::NormalizationForm_KC);
                const auto comparison = application::detail::compareNaturalFilePaths(normalizedLhs, normalizedRhs);
                return comparison != 0 ? comparison < 0 : normalizedLhs < normalizedRhs;
            });
            source.candidates.reserve(entries.size());
            for (const QString& entry : entries) {
                source.candidates.append({directory.absoluteFilePath(entry), directory.absoluteFilePath(entry)});
            }
        } else if (info.isFile()) {
            const QString absolute = QDir::cleanPath(info.absoluteFilePath());
            source.candidates.append({absolute, absolute});
        } else {
            application::ImportFailure failure;
            failure.path = path;
            failure.error = fileNotFoundError();
            source.failure = std::move(failure);
        }
        expanded.append(std::move(source));
    }
    return expanded;
}

struct SingleImportOutcome {
    std::optional<application::ImportedPage> importedPage;
    application::ImportFailure failure;
    QString committedAbsolutePath;
    bool success{false};
};

// Runs the per-file staging transaction: stream copy with SHA-256, content probe and header
// precheck, decode with normalization, then an atomic rename into assets/original. A failure
// never leaves a staged file behind.
SingleImportOutcome importOneFile(const ImportCandidate& candidate,
                                  const QString& canonicalRoot,
                                  const QString& stagingDir,
                                  int pageIndex,
                                  int& nextNumber,
                                  std::stop_token stopToken) {
    SingleImportOutcome outcome;
    outcome.failure.path = candidate.sourcePath;
    const QString stagedPath = stagingDir + QLatin1Char('/') + QUuid::createUuid().toString(QUuid::WithoutBraces);
    struct StagedFileCleanup {
        QString path;
        bool armed{true};
        ~StagedFileCleanup() {
            if (armed) {
                QFile::remove(path);
            }
        }
    } stagedCleanup{stagedPath};

    QFile source(candidate.absolutePath);
    if (!source.open(QIODevice::ReadOnly)) {
        outcome.failure.error = toPersistenceError(source.error());
        outcome.failure.error.diagnostic = QStringLiteral("stage:open-source");
        return outcome;
    }
    QByteArray sourceHash;
    {
        QFile stagedFile(stagedPath);
        if (!stagedFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            outcome.failure.error = toPersistenceError(stagedFile.error());
            outcome.failure.error.diagnostic = QStringLiteral("stage:open-staging");
            return outcome;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        while (!source.atEnd()) {
            const QByteArray chunk = source.read(1 << 20);
            if (chunk.isEmpty()) {
                outcome.failure.error = toPersistenceError(
                    source.error() == QFileDevice::NoError ? QFileDevice::ReadError : source.error());
                outcome.failure.error.diagnostic = QStringLiteral("stage:read-source");
                return outcome;
            }
            hash.addData(chunk);
            if (stagedFile.write(chunk) != chunk.size()) {
                const QFileDevice::FileError fileError = stagedFile.error();
                outcome.failure.error =
                    toPersistenceError(fileError == QFileDevice::NoError ? QFileDevice::WriteError : fileError);
                outcome.failure.error.diagnostic = QStringLiteral("stage:write-staging");
                return outcome;
            }
            if (stopToken.stop_requested()) {
                outcome.failure.error = makeCancelledError(u"Import");
                return outcome;
            }
        }
        sourceHash = hash.result();
    }
    source.close();

    QFile stagedVerification(stagedPath);
    if (!stagedVerification.open(QIODevice::ReadOnly)) {
        outcome.failure.error = toPersistenceError(stagedVerification.error());
        outcome.failure.error.diagnostic = QStringLiteral("stage:verify-open");
        return outcome;
    }
    QCryptographicHash stagedHash(QCryptographicHash::Sha256);
    while (!stagedVerification.atEnd()) {
        const QByteArray chunk = stagedVerification.read(1 << 20);
        if (chunk.isEmpty()) {
            outcome.failure.error =
                toPersistenceError(stagedVerification.error() == QFileDevice::NoError ? QFileDevice::ReadError
                                                                                      : stagedVerification.error());
            outcome.failure.error.diagnostic = QStringLiteral("stage:verify-read");
            return outcome;
        }
        stagedHash.addData(chunk);
    }
    if (stagedHash.result() != sourceHash) {
        outcome.failure.error =
            application::makeApplicationError(application::ErrorCategory::Persistence,
                                              QStringLiteral("persistence.atomic_commit_failed"),
                                              QStringLiteral("The imported image failed verification"));
        outcome.failure.error.diagnostic = QStringLiteral("stage:verify-hash");
        return outcome;
    }
    stagedVerification.close();

    QByteArray head;
    {
        // The handle must be closed before the final rename: an open QFile handle without
        // FILE_SHARE_DELETE blocks MoveFile on Windows.
        QFile headFile(stagedPath);
        if (headFile.open(QIODevice::ReadOnly)) {
            head = headFile.read(16);
        }
    }
    const ImageFileFormat detected = detectImageFileFormat(head);
    if (detected == ImageFileFormat::Unknown) {
        outcome.failure.error =
            application::makeApplicationError(application::ErrorCategory::Input,
                                              QStringLiteral("import.unsupported_format"),
                                              QStringLiteral("The file is not a supported image format"));
        outcome.failure.error.diagnostic = QStringLiteral("stage:sniff");
        return outcome;
    }

    auto normalized = loadNormalizedImage(stagedPath, stopToken);
    if (!normalized) {
        outcome.failure.error = normalized.error();
        return outcome;
    }

    application::ImportedPage imported;
    imported.sourcePath = candidate.sourcePath;
    const QString expectedSuffix = canonicalAssetSuffix(detected);
    if (!expectedSuffix.isEmpty()) {
        const QString sourceSuffix = QFileInfo(candidate.absolutePath).suffix().toLower();
        if (sourceSuffix != expectedSuffix.mid(1)) {
            application::ImportWarning warning;
            warning.path = candidate.sourcePath;
            warning.code = QStringLiteral("import.extension_mismatch");
            warning.safeMessage = QStringLiteral("The file content does not match its extension");
            imported.warnings.append(warning);
        }
    }
    for (const auto& [code, message] : normalized->warnings) {
        application::ImportWarning warning;
        warning.path = candidate.sourcePath;
        warning.code = code;
        warning.safeMessage = message;
        imported.warnings.append(warning);
    }

    // Assign a free page_NNNN name; an unexpected collision bumps the number and never
    // overwrites an existing asset.
    QString assetName;
    QString finalPath;
    const QString originalDir = canonicalRoot + QLatin1Char('/') + kAssetsOriginal;
    for (int attempt = 0; attempt < 1000; ++attempt) {
        assetName = QStringLiteral("page_%1").arg(nextNumber, 4, 10, QLatin1Char('0')) + expectedSuffix;
        finalPath = originalDir + QLatin1Char('/') + assetName;
        if (!QFileInfo::exists(finalPath)) {
            break;
        }
        ++nextNumber;
    }
    const domain::ImageSize normalizedSize{normalized->image.width(), normalized->image.height()};
    application::ImportedPage pageResult;
    pageResult.sourcePath = candidate.sourcePath;
    pageResult.warnings = imported.warnings;
    pageResult.page = domain::ComicPage(domain::PageId::generate(),
                                        (QString(kAssetsOriginal) + QLatin1Char('/') + assetName).toStdString(),
                                        normalizedSize,
                                        pageIndex);

    if (!QFile::rename(stagedPath, finalPath)) {
        outcome.failure.error = toPersistenceError(QFileDevice::RenameError);
        outcome.failure.error.diagnostic = QStringLiteral("stage:rename");
        return outcome;
    }
    stagedCleanup.armed = false;
    ++nextNumber;

    outcome.importedPage = std::move(pageResult);
    outcome.committedAbsolutePath = finalPath;
    outcome.success = true;
    return outcome;
}

QString pendingBatchPath(const QString& canonicalRoot) {
    return canonicalRoot + QLatin1Char('/') + kStagingDirName + QLatin1Char('/') + kPendingBatchFileName;
}

// Persists the unacknowledged import batch so openProject can remove orphaned originals after
// an interrupted session. Failure is best-effort and never changes the import result.
bool writePendingBatch(const QString& canonicalRoot,
                       const QString& batchId,
                       const QString& projectId,
                       const QSet<QString>& assetPaths) {
    const QString path = pendingBatchPath(canonicalRoot);
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return false;
    }
    QJsonObject batch;
    batch.insert(QStringLiteral("batchId"), batchId);
    batch.insert(QStringLiteral("projectId"), projectId);
    QJsonArray paths;
    for (const QString& assetPath : assetPaths) {
        paths.append(assetPath);
    }
    batch.insert(QStringLiteral("assetPaths"), paths);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(batch).toJson(QJsonDocument::Compact);
    if (file.write(bytes) != bytes.size() || file.error() != QFileDevice::NoError) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

std::optional<QJsonObject> readPendingBatch(const QString& canonicalRoot) {
    QFile file(pendingBatchPath(canonicalRoot));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!document.isObject()) {
        return std::nullopt;
    }
    return document.object();
}

} // namespace

FileSystemComicImporter::FileSystemComicImporter(ProjectIoCoordinator* coordinator, CommitObserver commitObserver)
    : m_coordinator(coordinator), m_commitObserver(std::move(commitObserver)) {}

QFuture<application::Result<application::ImportImagesResult>>
FileSystemComicImporter::importImages(application::ImportImagesRequest request, application::OperationContext context) {
    auto promise = std::make_shared<QPromise<application::Result<application::ImportImagesResult>>>();
    promise->start();
    auto future = promise->future();
    QThreadPool::globalInstance()->start([promise, this, request = std::move(request), context]() mutable {
        try {
            promise->addResult(importImagesBlocking(request, context, *promise));
        } catch (...) {
            promise->addResult(std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Internal,
                                                  QStringLiteral("internal.unexpected_exception"),
                                                  QStringLiteral("An unexpected internal error occurred"))));
        }
        promise->finish();
    });
    return future;
}

application::Result<application::ImportImagesResult>
FileSystemComicImporter::importImagesBlocking(const application::ImportImagesRequest& request,
                                              application::OperationContext context,
                                              QPromise<application::Result<application::ImportImagesResult>>& promise) {
    if (context.stopToken.stop_requested()) {
        return std::unexpected(makeCancelledError(u"Import"));
    }
    const QString canonicalRoot =
        canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
    if (canonicalRoot.isEmpty()) {
        return std::unexpected(fileNotFoundError());
    }
    auto writeTicket = m_coordinator->beginProjectWrite(request.projectId, canonicalRoot);
    if (!writeTicket) {
        return std::unexpected(makeProjectNotOpenError());
    }

    const auto expanded = expandSources(request.filePaths);
    // A file hit both directly and through a folder expansion rejects the whole request
    // before any disk work.
    {
        QSet<QString> seen;
        for (const auto& source : expanded) {
            for (const auto& candidate : source.candidates) {
                const QString key = duplicateKeyFor(candidate.absolutePath);
                if (seen.contains(key)) {
                    return std::unexpected(makeDuplicateImportError());
                }
                seen.insert(key);
            }
        }
    }

    int totalFiles = 0;
    for (const auto& source : expanded) {
        totalFiles += source.candidates.size();
    }
    promise.setProgressRange(0, totalFiles);

    const QString originalDir = canonicalRoot + QLatin1Char('/') + kAssetsOriginal;
    const QString stagingDir = canonicalRoot + QLatin1Char('/') + kStagingDirName;
    if (!QDir().mkpath(originalDir) || !QDir().mkpath(stagingDir)) {
        return std::unexpected(toPersistenceError(QFileDevice::PermissionsError));
    }

    // The whole request runs inside one coordinator write section, so the asset number scan
    // and the final existence check share the same critical section.
    int nextNumber = 1;
    const QRegularExpression assetNamePattern(QStringLiteral("^page_(\\d+)\\."));
    for (const QString& entry : QDir(originalDir).entryList(QDir::Files | QDir::NoDotAndDotDot)) {
        const auto match = assetNamePattern.match(entry);
        if (match.hasMatch()) {
            bool ok = false;
            const int number = match.captured(1).toInt(&ok);
            if (ok && number >= nextNumber) {
                nextNumber = number + 1;
            }
        }
    }

    application::ImportImagesResult result;
    result.batchId = context.operationId.isValid() ? context.operationId : application::OperationId::generate();
    QVector<QString> committedAssets;
    int pageIndex = 0;
    int completedFiles = 0;
    for (const auto& source : expanded) {
        if (source.failure) {
            result.failures.append(std::move(*source.failure));
            continue;
        }
        for (const auto& candidate : source.candidates) {
            if (context.stopToken.stop_requested()) {
                // A cancelled request rolls back every asset it committed; nothing was applied
                // to the domain yet, so no partial success is reported.
                for (const QString& path : committedAssets) {
                    QFile::remove(path);
                }
                QDir(canonicalRoot).rmdir(kStagingDirName.toString());
                return std::unexpected(makeCancelledError(u"Import"));
            }
            auto outcome =
                importOneFile(candidate, canonicalRoot, stagingDir, pageIndex, nextNumber, context.stopToken);
            if (outcome.success) {
                result.importedPages.append(std::move(*outcome.importedPage));
                committedAssets.append(outcome.committedAbsolutePath);
                ++pageIndex;
                if (m_commitObserver) {
                    m_commitObserver(committedAssets.size());
                }
            } else {
                result.failures.append(std::move(outcome.failure));
            }
            ++completedFiles;
            promise.setProgressValue(completedFiles);
        }
    }
    if (context.stopToken.stop_requested()) {
        for (const QString& path : committedAssets) {
            QFile::remove(path);
        }
        QDir(canonicalRoot).rmdir(kStagingDirName.toString());
        return std::unexpected(makeCancelledError(u"Import"));
    }
    QDir(canonicalRoot).rmdir(kStagingDirName.toString());
    if (!result.importedPages.isEmpty()) {
        PendingBatch batch;
        batch.projectId = request.projectId;
        batch.canonicalRoot = canonicalRoot;
        for (const auto& importedPage : result.importedPages) {
            batch.assetPaths.insert(QString::fromStdString(importedPage.page.originalAssetPath()));
        }
        // The path set must be persisted before batch is moved into the pending map.
        (void)writePendingBatch(
            canonicalRoot, result.batchId.toString(), request.projectId.toString(), batch.assetPaths);
        QMutexLocker locker(&m_batchesMutex);
        m_pendingBatches.insert_or_assign(result.batchId.toString(), std::move(batch));
    }
    return result;
}

QFuture<application::Result<void>>
FileSystemComicImporter::discardImportedAssets(application::DiscardImportedAssetsRequest request,
                                               application::OperationContext context) {
    return runDataOperation<void>([this, request = std::move(request), context]() -> application::Result<void> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(makeCancelledError(u"Import cleanup"));
        }
        const QString canonicalRoot =
            canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
        if (canonicalRoot.isEmpty()) {
            return std::unexpected(fileNotFoundError());
        }
        auto writeTicket = m_coordinator->beginProjectWrite(request.projectId, canonicalRoot);
        if (!writeTicket) {
            return std::unexpected(makeProjectNotOpenError());
        }
        QSet<QString> allowedPaths;
        {
            QMutexLocker locker(&m_batchesMutex);
            const auto it = m_pendingBatches.find(request.batchId.toString());
            if (it == m_pendingBatches.end() || it->second.projectId != request.projectId ||
                it->second.canonicalRoot != canonicalRoot) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Input,
                                                      QStringLiteral("input.invalid_import_cleanup"),
                                                      QStringLiteral("The cleanup request is invalid")));
            }
            allowedPaths = it->second.assetPaths;
        }
        for (const QString& assetPath : request.assetPaths) {
            if (!isSafeRelativeAssetPath(assetPath) ||
                !assetPath.startsWith(QString(kAssetsOriginal) + QLatin1Char('/')) ||
                !allowedPaths.contains(assetPath)) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Input,
                                                      QStringLiteral("input.invalid_import_cleanup"),
                                                      QStringLiteral("The cleanup request is invalid")));
            }
            const QString resolved = resolveInsideCanonicalRoot(canonicalRoot, assetPath);
            if (resolved.isEmpty()) {
                continue; // already gone; discarding is idempotent
            }
            if (!QFile::remove(resolved)) {
                return std::unexpected(toPersistenceError(QFileDevice::PermissionsError));
            }
        }
        bool batchConsumed = false;
        QSet<QString> remainingPaths;
        {
            QMutexLocker locker(&m_batchesMutex);
            auto it = m_pendingBatches.find(request.batchId.toString());
            if (it != m_pendingBatches.end()) {
                for (const QString& assetPath : request.assetPaths) {
                    it->second.assetPaths.remove(assetPath);
                }
                remainingPaths = it->second.assetPaths;
                if (remainingPaths.isEmpty()) {
                    m_pendingBatches.erase(it);
                    batchConsumed = true;
                }
            }
        }
        if (batchConsumed) {
            QFile::remove(pendingBatchPath(canonicalRoot));
        } else if (!remainingPaths.isEmpty()) {
            (void)writePendingBatch(
                canonicalRoot, request.batchId.toString(), request.projectId.toString(), remainingPaths);
        }
        return {};
    });
}

QFuture<application::Result<void>>
FileSystemComicImporter::acknowledgeImportedAssets(application::AcknowledgeImportedAssetsRequest request,
                                                   application::OperationContext context) {
    return runDataOperation<void>([this, request = std::move(request), context]() -> application::Result<void> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(makeCancelledError(u"Import acknowledgement"));
        }
        const QString canonicalRoot =
            canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
        if (canonicalRoot.isEmpty()) {
            return std::unexpected(fileNotFoundError());
        }
        auto writeTicket = m_coordinator->beginProjectWrite(request.projectId, canonicalRoot);
        if (!writeTicket) {
            return std::unexpected(makeProjectNotOpenError());
        }
        QMutexLocker locker(&m_batchesMutex);
        const auto it = m_pendingBatches.find(request.batchId.toString());
        if (it == m_pendingBatches.end() || it->second.projectId != request.projectId ||
            it->second.canonicalRoot != canonicalRoot) {
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Input,
                                                  QStringLiteral("input.invalid_import_cleanup"),
                                                  QStringLiteral("The import acknowledgement is invalid")));
        }
        m_pendingBatches.erase(it);
        // Adopted assets must not be treated as orphans by the next openProject.
        QFile::remove(pendingBatchPath(canonicalRoot));
        return {};
    });
}

} // namespace manga::data
