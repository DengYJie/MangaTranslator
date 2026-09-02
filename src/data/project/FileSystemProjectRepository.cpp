#include "data/project/FileSystemProjectRepository.h"

#include "application/ports/IDerivedAssetStore.h"
#include "data/DataAsync.h"
#include "data/DataErrorMapping.h"
#include "data/project/PathSafety.h"
#include "data/project/ProjectLayout.h"
#include "data/project/ProjectSerializer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QThreadPool>

#include <limits>
#include <optional>
#include <utility>

namespace manga::data {
namespace {

constexpr qint64 kWriteMarginBytes = 64 * 1024;
constexpr int kProjectCloseTimeoutMs = 10'000;

// Collects every derived layer reference that project.json still points at; paths are
// project-root relative and form the keep-set when pruning unused derived assets.
QSet<QString> collectReferencedLayerPaths(const domain::ComicProject& project) {
    QSet<QString> paths;
    for (const auto& page : project.pages()) {
        const auto addLayer = [&paths](const std::optional<std::string>& layerPath) {
            if (layerPath.has_value() && !layerPath->empty()) {
                paths.insert(QString::fromStdString(*layerPath));
            }
        };
        addLayer(page.clearLayerAsset());
        addLayer(page.translationLayerAsset());
        addLayer(page.manualOverrideLayerAsset());
    }
    return paths;
}

application::ApplicationError makeDiskFullError() {
    auto error = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                   QStringLiteral("persistence.disk_full"),
                                                   QStringLiteral("Not enough disk space for this operation"));
    error.recovery = application::RecoveryAction::FreeDiskSpace;
    return error;
}

application::ApplicationError makeCancelledError(QStringView what) {
    return application::makeApplicationError(application::ErrorCategory::Cancelled,
                                             QStringLiteral("cancelled.project_operation"),
                                             QStringLiteral("%1 was cancelled").arg(what));
}

application::ApplicationError makeLockedError() {
    return toProjectLockError(QLockFile::LockFailedError);
}

application::ApplicationError makeTargetNotEmptyError() {
    return application::makeApplicationError(application::ErrorCategory::Persistence,
                                             QStringLiteral("persistence.target_not_empty"),
                                             QStringLiteral("The target directory must be empty"));
}

application::ApplicationError makeSaveConflictError() {
    return application::makeApplicationError(application::ErrorCategory::Persistence,
                                             QStringLiteral("persistence.save_conflict"),
                                             QStringLiteral("The project file changed outside this session"));
}

application::ApplicationError makeRecoveryFailedError(const QString& diagnostic) {
    auto error =
        application::makeApplicationError(application::ErrorCategory::Persistence,
                                          QStringLiteral("persistence.recovery_failed"),
                                          QStringLiteral("The project file is damaged and could not be opened"));
    error.diagnostic = diagnostic;
    return error;
}

std::expected<QByteArray, application::ApplicationError> readFileBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        const QFileDevice::FileError fileError = file.error();
        if (fileError == QFileDevice::PermissionsError || fileError == QFileDevice::OpenError) {
            return std::unexpected(toPersistenceError(QFileDevice::PermissionsError));
        }
        return std::unexpected(toPersistenceError(fileError));
    }
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        return std::unexpected(toPersistenceError(file.error()));
    }
    return bytes;
}

// Writes through a QSaveFile sibling temporary and commits atomically; directWriteFallback stays
// disabled so a failed commit never truncates an existing valid file.
std::optional<application::ApplicationError> writeFileSync(const QString& path, const QByteArray& bytes) {
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)) {
        return toPersistenceError(file.error());
    }
    const qint64 written = file.write(bytes);
    if (written != bytes.size() || file.error() != QFileDevice::NoError) {
        const QFileDevice::FileError fileError = file.error();
        file.cancelWriting();
        return toPersistenceError(fileError == QFileDevice::NoError ? QFileDevice::WriteError : fileError);
    }
    if (!file.commit()) {
        return toPersistenceError(file.error());
    }
    return std::nullopt;
}

struct FileFingerprint {
    qint64 size{0};
    qint64 modifiedMsecs{0};
    QByteArray sha256;
};

std::optional<FileFingerprint> fingerprintFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1 << 20);
        if (chunk.isEmpty()) {
            return std::nullopt;
        }
        hash.addData(chunk);
    }
    const QFileInfo info(path);
    FileFingerprint fingerprint;
    fingerprint.size = info.size();
    fingerprint.modifiedMsecs = info.lastModified().toMSecsSinceEpoch();
    fingerprint.sha256 = hash.result();
    return fingerprint;
}

// Rejects original assets that are unsafe, missing, directories, or redirected outside the
// canonical project root (symlinks, junctions, reparse points). Missing or damaged derived
// assets only produce warnings; the stored project.json is never rewritten here.
std::expected<QVector<application::ProjectWarning>, application::ApplicationError>
validateProjectAssets(const QString& canonicalRoot, const domain::ComicProject& project) {
    QVector<application::ProjectWarning> warnings;
    for (const auto& page : project.pages()) {
        const QString originalPath = QString::fromStdString(page.originalAssetPath());
        const auto makeSemanticError = [&page](QStringView message) {
            auto error = application::makeApplicationError(application::ErrorCategory::Project,
                                                           QStringLiteral("project.invalid_semantic"),
                                                           QStringLiteral("A page asset reference is invalid"));
            error.diagnostic =
                QStringLiteral("page %1: %2").arg(QString::fromStdString(page.id().toString()), message.toString());
            return error;
        };
        if (!isSafeRelativeAssetPath(originalPath)) {
            return std::unexpected(makeSemanticError(u"originalAsset must be a relative path inside the project"));
        }
        const QFileInfo originalInfo(canonicalRoot + QLatin1Char('/') + originalPath);
        if (!originalInfo.exists()) {
            auto error = application::makeApplicationError(application::ErrorCategory::Project,
                                                           QStringLiteral("project.missing_asset"),
                                                           QStringLiteral("A page original image is missing"));
            error.diagnostic = QStringLiteral("page %1").arg(QString::fromStdString(page.id().toString()));
            return std::unexpected(error);
        }
        const QString resolvedOriginal = resolveInsideCanonicalRoot(canonicalRoot, originalPath);
        if (resolvedOriginal.isEmpty() || !QFileInfo(resolvedOriginal).isFile() ||
            !QFileInfo(resolvedOriginal).isReadable()) {
            return std::unexpected(
                makeSemanticError(u"originalAsset is not a readable regular file inside the project root"));
        }

        const std::optional<std::string> layerPaths[] = {
            page.clearLayerAsset(), page.translationLayerAsset(), page.manualOverrideLayerAsset()};
        for (const auto& layerPath : layerPaths) {
            if (!layerPath.has_value()) {
                continue;
            }
            const QString relative = QString::fromStdString(*layerPath);
            if (!isSafeRelativeAssetPath(relative)) {
                return std::unexpected(makeSemanticError(u"layer assets must be relative paths inside the project"));
            }
            const QFileInfo layerInfo(canonicalRoot + QLatin1Char('/') + relative);
            if (!layerInfo.isFile()) {
                application::ProjectWarning warning;
                warning.code = QStringLiteral("project.derived_asset_missing");
                warning.safeMessage = QStringLiteral("A derived image is missing and must be regenerated");
                warning.path = relative;
                warnings.append(warning);
                continue;
            }
            const QString resolvedLayer = canonicalPathResolved(canonicalRoot + QLatin1Char('/') + relative);
            if (!isInsideCanonicalRoot(canonicalRoot, resolvedLayer)) {
                return std::unexpected(makeSemanticError(u"layer assets must stay inside the project root"));
            }
            QImageReader reader(resolvedLayer);
            if (!layerInfo.isReadable() || !reader.canRead()) {
                application::ProjectWarning warning;
                warning.code = QStringLiteral("project.derived_asset_invalid");
                warning.safeMessage = QStringLiteral("A derived image is invalid and must be regenerated");
                warning.path = relative;
                warnings.append(warning);
            }
        }
    }
    return warnings;
}

// Copies one file while hashing the source stream and verifies the staged copy byte for byte.
std::optional<application::ApplicationError>
copyFileVerified(const QString& sourcePath, const QString& targetPath, std::stop_token stopToken) {
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        return toPersistenceError(source.error());
    }
    QSaveFile target(targetPath);
    target.setDirectWriteFallback(false);
    if (!target.open(QIODevice::WriteOnly)) {
        return toPersistenceError(target.error());
    }
    QCryptographicHash sourceHash(QCryptographicHash::Sha256);
    while (!source.atEnd()) {
        const QByteArray chunk = source.read(1 << 20);
        if (chunk.isEmpty()) {
            target.cancelWriting();
            return toPersistenceError(source.error() == QFileDevice::NoError ? QFileDevice::ReadError : source.error());
        }
        sourceHash.addData(chunk);
        if (target.write(chunk) != chunk.size()) {
            const QFileDevice::FileError fileError = target.error();
            target.cancelWriting();
            return toPersistenceError(fileError == QFileDevice::NoError ? QFileDevice::WriteError : fileError);
        }
        if (stopToken.stop_requested()) {
            target.cancelWriting();
            return makeCancelledError(u"Project copy");
        }
    }
    if (!target.commit()) {
        return toPersistenceError(target.error());
    }
    const auto staged = readFileBytes(targetPath);
    if (!staged || QCryptographicHash::hash(*staged, QCryptographicHash::Sha256) != sourceHash.result()) {
        return application::makeApplicationError(application::ErrorCategory::Persistence,
                                                 QStringLiteral("persistence.atomic_commit_failed"),
                                                 QStringLiteral("A copied file failed verification"));
    }
    return std::nullopt;
}

// Copies every regular file below sourceDir into stagingDir preserving relative paths.
std::optional<application::ApplicationError>
copyTreeVerified(const QString& sourceDir, const QString& stagingDir, std::stop_token stopToken) {
    const QString canonicalSourceDir = canonicalDirectoryPath(sourceDir);
    if (canonicalSourceDir.isEmpty()) {
        return fileNotFoundError();
    }
    QDirIterator iterator(sourceDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        if (stopToken.stop_requested()) {
            return makeCancelledError(u"Project copy");
        }
        const QString sourcePath = iterator.next();
        const QString resolvedSource = canonicalPathResolved(sourcePath);
        if (resolvedSource.isEmpty() || !isInsideCanonicalRoot(canonicalSourceDir, resolvedSource)) {
            return application::makeApplicationError(application::ErrorCategory::Project,
                                                     QStringLiteral("project.invalid_semantic"),
                                                     QStringLiteral("A project asset path is invalid"));
        }
        const QString relative = QStringView{sourcePath}.mid(sourceDir.size() + 1).toString();
        const QString targetPath = stagingDir + QLatin1Char('/') + relative;
        QFileInfo(targetPath).dir().mkpath(QStringLiteral("."));
        if (auto error = copyFileVerified(sourcePath, targetPath, stopToken)) {
            return error;
        }
    }
    return std::nullopt;
}

qint64 treeSize(const QString& sourceDir) {
    qint64 total = 0;
    QDirIterator iterator(sourceDir, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const qint64 size = iterator.fileInfo().size();
        if (size > std::numeric_limits<qint64>::max() - total) {
            return std::numeric_limits<qint64>::max();
        }
        total += size;
    }
    return total;
}

bool sameRootPath(const QString& lhs, const QString& rhs) {
#ifdef Q_OS_WIN
    return QString::compare(QDir::cleanPath(lhs), QDir::cleanPath(rhs), Qt::CaseInsensitive) == 0;
#else
    return QDir::cleanPath(lhs) == QDir::cleanPath(rhs);
#endif
}

// Removes the staging tree and a target directory that this operation created; pre-existing
// directories are never deleted.
void removeCreatedTarget(const QString& stagingRoot, const QString& targetRoot, bool createdDirectory) {
    if (!stagingRoot.isEmpty()) {
        QDir(stagingRoot).removeRecursively();
    }
    if (createdDirectory) {
        QDir(targetRoot).removeRecursively();
    }
}

// Removes original images recorded by an interrupted import batch that the project does not
// reference, then drops the batch marker. Runs under the project write ticket during open.
void cleanupInterruptedImports(const QString& canonicalRoot, const domain::ComicProject& project) {
    const QString markerPath =
        canonicalRoot + QLatin1Char('/') + kStagingDirName + QLatin1Char('/') + kPendingBatchFileName;
    const QFileInfo markerInfo(markerPath);
    if (!markerInfo.exists()) {
        return;
    }
    QFile markerFile(markerPath);
    if (!markerFile.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(markerFile.readAll());
    markerFile.close();
    if (!document.isObject()) {
        QFile::remove(markerPath);
        return;
    }
    const QJsonArray paths = document.object().value(QStringLiteral("assetPaths")).toArray();
    QSet<QString> referenced;
    for (const auto& page : project.pages()) {
        referenced.insert(QString::fromStdString(page.originalAssetPath()));
    }
    for (const QJsonValue& value : paths) {
        const QString relativePath = value.toString();
        if (!isSafeRelativeAssetPath(relativePath) ||
            !relativePath.startsWith(QString(kAssetsOriginal) + QLatin1Char('/'))) {
            continue;
        }
        if (referenced.contains(relativePath)) {
            continue; // Referenced by the project; keep the asset.
        }
        const QString resolved = resolveInsideCanonicalRoot(canonicalRoot, relativePath);
        if (!resolved.isEmpty()) {
            QFile::remove(resolved);
        }
    }
    QFile::remove(markerPath);
}

} // namespace

struct RepositorySession {
    application::ProjectDescriptor descriptor;
    QString canonicalRoot;
    std::unique_ptr<QLockFile> lock;
    bool recoveryPending{false};
    bool recoveredFromBackup{false};
    FileFingerprint projectJsonFingerprint;
    bool hasFingerprint{false};
    // The domain aggregate this session was opened with; a repeated openProject on the same
    // canonical root returns the existing session instead of acquiring a second lock.
    std::optional<domain::ComicProject> project;
};

FileSystemProjectRepository::FileSystemProjectRepository(ProjectIoCoordinator* coordinator, DiskSpaceProbe probe)
    : m_coordinator(coordinator), m_probe(std::move(probe)) {}

FileSystemProjectRepository::~FileSystemProjectRepository() = default;

void FileSystemProjectRepository::setDiskSpaceProbe(DiskSpaceProbe probe) {
    m_probe = std::move(probe);
}

void FileSystemProjectRepository::setSaveAsCommitProbe(SaveAsCommitProbe probe) {
    m_saveAsCommitProbe = std::move(probe);
}

void FileSystemProjectRepository::setSaveTransactionHook(SaveTransactionHook hook) {
    m_saveTransactionHook = std::move(hook);
}

void FileSystemProjectRepository::setDerivedAssetStore(application::IDerivedAssetStore* store) {
    m_derivedStore = store;
}

bool FileSystemProjectRepository::hasDiskSpace(const QString& root, qint64 requiredBytes) const {
    const qint64 available = m_probe ? m_probe(root) : QStorageInfo(root).bytesAvailable();
    return available < 0 || available >= requiredBytes;
}

std::shared_ptr<RepositorySession> FileSystemProjectRepository::findSession(const domain::ProjectId& projectId) {
    QMutexLocker locker(&m_mutex);
    for (const auto& [root, session] : m_sessions) {
        if (session->descriptor.id == projectId) {
            return session;
        }
    }
    return nullptr;
}

std::shared_ptr<RepositorySession> FileSystemProjectRepository::findSessionByRoot(const QString& canonicalRoot) {
    QMutexLocker locker(&m_mutex);
    const auto it = m_sessions.find(canonicalRoot);
    return it != m_sessions.end() ? it->second : nullptr;
}

void FileSystemProjectRepository::registerSession(const std::shared_ptr<RepositorySession>& session) {
    QMutexLocker locker(&m_mutex);
    m_sessions.insert_or_assign(session->canonicalRoot, session);
}

void FileSystemProjectRepository::forgetSession(const QString& canonicalRoot) {
    QMutexLocker locker(&m_mutex);
    m_sessions.erase(canonicalRoot);
}

QFuture<application::Result<application::ProjectDescriptor>>
FileSystemProjectRepository::createProject(application::CreateProjectRequest request,
                                           application::OperationContext context) {
    return runDataOperation<application::ProjectDescriptor>(
        [this, request = std::move(request), context]() -> application::Result<application::ProjectDescriptor> {
            if (context.stopToken.stop_requested()) {
                return std::unexpected(makeCancelledError(u"Project creation"));
            }
            const QString cleaned = QDir::cleanPath(QDir::fromNativeSeparators(request.path.trimmed()));
            if (cleaned.isEmpty() || !QDir::isAbsolutePath(cleaned)) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Input,
                                                      QStringLiteral("input.invalid_project"),
                                                      QStringLiteral("The project path is invalid")));
            }

            bool createdDirectory = false;
            const QFileInfo rootInfo(cleaned);
            if (rootInfo.exists()) {
                if (!rootInfo.isDir() || !QDir(cleaned).isEmpty()) {
                    return std::unexpected(makeTargetNotEmptyError());
                }
            } else {
                if (context.stopToken.stop_requested()) {
                    return std::unexpected(makeCancelledError(u"Project creation"));
                }
                if (!QDir().mkpath(cleaned)) {
                    return std::unexpected(toPersistenceError(QFileDevice::PermissionsError));
                }
                createdDirectory = true;
            }
            const QString canonicalRoot = canonicalDirectoryPath(cleaned);
            if (canonicalRoot.isEmpty()) {
                return std::unexpected(toPersistenceError(QFileDevice::OpenError));
            }

            auto writeTicket = m_coordinator->beginWrite(canonicalRoot);
            if (!writeTicket) {
                return std::unexpected(makeLockedError());
            }
            if (findSessionByRoot(canonicalRoot) != nullptr) {
                return std::unexpected(makeLockedError());
            }

            auto lock = std::make_unique<QLockFile>(canonicalRoot + QLatin1Char('/') + kLockFileName);
            lock->setStaleLockTime(0);
            if (!lock->tryLock(0)) {
                return std::unexpected(toProjectLockError(lock->error()));
            }

            domain::ComicProject project(request.name.trimmed().toStdString(), domain::ProjectId::generate());
            project.setSourceLanguage(request.sourceLanguage);
            project.setTargetLanguage(request.targetLanguage);
            project.setReadingDirection(request.readingDirection);
            project.markClean();

            const QString projectJsonPath = canonicalRoot + QLatin1Char('/') + kProjectFileName;
            const QString originalPath = canonicalRoot + QLatin1Char('/') + kAssetsOriginal;
            const QString derivedPath = canonicalRoot + QLatin1Char('/') + kAssetsDerived;
            const bool originalExisted = QFileInfo::exists(originalPath);
            const bool derivedExisted = QFileInfo::exists(derivedPath);
            const auto cleanupLayout = [&] {
                if (createdDirectory) {
                    QDir(cleaned).removeRecursively();
                    return;
                }
                if (!derivedExisted) {
                    QDir(canonicalRoot).rmdir(kAssetsDerived.toString());
                }
                if (!originalExisted) {
                    QDir(canonicalRoot).rmdir(kAssetsOriginal.toString());
                }
                QDir(canonicalRoot).rmdir(QStringLiteral("assets"));
            };
            if (!QDir().mkpath(originalPath) || !QDir().mkpath(derivedPath)) {
                lock.reset();
                cleanupLayout();
                return std::unexpected(toPersistenceError(QFileDevice::PermissionsError));
            }
            const QByteArray initialBytes = ProjectSerializer::serialize(project);
            if (!hasDiskSpace(canonicalRoot, initialBytes.size() + kWriteMarginBytes)) {
                lock.reset();
                cleanupLayout();
                return std::unexpected(makeDiskFullError());
            }
            if (auto writeError = writeFileSync(projectJsonPath, initialBytes)) {
                lock.reset();
                cleanupLayout();
                return std::unexpected(*writeError);
            }

            auto session = std::make_shared<RepositorySession>();
            session->descriptor.id = project.id();
            session->descriptor.name = request.name.trimmed();
            session->descriptor.path = canonicalRoot;
            session->canonicalRoot = canonicalRoot;
            session->lock = std::move(lock);
            session->project = std::move(project);
            if (auto fingerprint = fingerprintFile(projectJsonPath)) {
                session->projectJsonFingerprint = *fingerprint;
                session->hasFingerprint = true;
            }
            registerSession(session);
            if (!m_coordinator->bindProject(session->descriptor.id, canonicalRoot)) {
                forgetSession(canonicalRoot);
                session->lock.reset();
                return std::unexpected(makeLockedError());
            }

            return session->descriptor;
        });
}

QFuture<application::Result<application::OpenProjectResult>>
FileSystemProjectRepository::openProject(application::OpenProjectRequest request,
                                         application::OperationContext context) {
    return runDataOperation<application::OpenProjectResult>(
        [this, request = std::move(request), context]() -> application::Result<application::OpenProjectResult> {
            if (context.stopToken.stop_requested()) {
                return std::unexpected(makeCancelledError(u"Project open"));
            }
            const QString canonicalRoot =
                canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.path.trimmed())));
            if (canonicalRoot.isEmpty()) {
                return std::unexpected(fileNotFoundError());
            }
            auto writeTicket = m_coordinator->beginWrite(canonicalRoot);
            if (!writeTicket) {
                return std::unexpected(makeLockedError());
            }
            if (auto existing = findSessionByRoot(canonicalRoot); existing != nullptr) {
                // The root is already open in this process; return the existing session.
                if (existing->recoveryPending || !existing->project.has_value()) {
                    return std::unexpected(
                        makeRecoveryFailedError(QStringLiteral("The existing project session is damaged")));
                }
                application::OpenProjectResult result;
                result.project = *existing->project;
                result.recoverySource = existing->recoveredFromBackup ? application::RecoverySource::Backup
                                                                      : application::RecoverySource::Current;
                if (existing->recoveredFromBackup) {
                    application::ProjectWarning warning;
                    warning.code = QStringLiteral("project.recovered_from_backup");
                    warning.safeMessage = QStringLiteral("Project data was restored from the backup file");
                    warning.path = QString(kProjectFileName) + kBackupSuffix;
                    result.warnings.append(warning);
                }
                return result;
            }
            auto lock = std::make_unique<QLockFile>(canonicalRoot + QLatin1Char('/') + kLockFileName);
            lock->setStaleLockTime(0);
            if (!lock->tryLock(0)) {
                return std::unexpected(toProjectLockError(lock->error()));
            }
            const QString projectJsonPath = canonicalRoot + QLatin1Char('/') + kProjectFileName;
            const QFileInfo projectJsonInfo(projectJsonPath);
            if (!projectJsonInfo.exists() || !projectJsonInfo.isFile()) {
                lock.reset();
                return std::unexpected(notAProjectError());
            }
            auto bytes = readFileBytes(projectJsonPath);
            if (!bytes) {
                lock.reset();
                return std::unexpected(bytes.error());
            }
            auto loaded = ProjectSerializer::deserialize(*bytes);
            if (!loaded) {
                // The current file is damaged: keep the lock and register a recovery-pending
                // session so recoverProject reuses the same session lock.
                auto session = std::make_shared<RepositorySession>();
                session->canonicalRoot = canonicalRoot;
                session->lock = std::move(lock);
                session->recoveryPending = true;
                registerSession(session);
                return std::unexpected(makeRecoveryFailedError(QString::fromStdString(loaded.error().toString())));
            }
            auto assets = validateProjectAssets(canonicalRoot, *loaded);
            if (!assets) {
                lock.reset();
                return std::unexpected(assets.error());
            }
            // A previous session may have exited before imported assets were acknowledged;
            // delete originals the project does not reference and clear the batch marker.
            cleanupInterruptedImports(canonicalRoot, *loaded);

            auto session = std::make_shared<RepositorySession>();
            session->descriptor.id = loaded->id();
            session->descriptor.name = QString::fromStdString(loaded->name());
            session->descriptor.path = canonicalRoot;
            session->canonicalRoot = canonicalRoot;
            session->lock = std::move(lock);
            session->project = *loaded;
            if (auto fingerprint = fingerprintFile(projectJsonPath)) {
                session->projectJsonFingerprint = *fingerprint;
                session->hasFingerprint = true;
            }
            registerSession(session);
            if (!m_coordinator->bindProject(session->descriptor.id, canonicalRoot)) {
                forgetSession(canonicalRoot);
                session->lock.reset();
                return std::unexpected(makeLockedError());
            }

            application::OpenProjectResult result;
            result.project = std::move(*loaded);
            result.recoverySource = application::RecoverySource::Current;
            result.warnings = std::move(*assets);
            return result;
        });
}

QFuture<application::Result<application::OpenProjectResult>>
FileSystemProjectRepository::recoverProject(application::OpenProjectRequest request,
                                            application::OperationContext context) {
    return runDataOperation<application::OpenProjectResult>(
        [this, request = std::move(request), context]() -> application::Result<application::OpenProjectResult> {
            if (context.stopToken.stop_requested()) {
                return std::unexpected(makeCancelledError(u"Project recovery"));
            }
            const QString canonicalRoot =
                canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.path.trimmed())));
            if (canonicalRoot.isEmpty()) {
                return std::unexpected(fileNotFoundError());
            }
            auto writeTicket = m_coordinator->beginWrite(canonicalRoot);
            if (!writeTicket) {
                return std::unexpected(makeLockedError());
            }
            auto session = findSessionByRoot(canonicalRoot);
            if (session == nullptr) {
                auto lock = std::make_unique<QLockFile>(canonicalRoot + QLatin1Char('/') + kLockFileName);
                lock->setStaleLockTime(0);
                if (!lock->tryLock(0)) {
                    return std::unexpected(toProjectLockError(lock->error()));
                }
                session = std::make_shared<RepositorySession>();
                session->canonicalRoot = canonicalRoot;
                session->lock = std::move(lock);
                session->recoveryPending = true;
                registerSession(session);
            }
            if (!session->recoveryPending) {
                return std::unexpected(
                    makeRecoveryFailedError(QStringLiteral("The project session is healthy, nothing to recover")));
            }

            auto bytes = readFileBytes(canonicalRoot + QLatin1Char('/') + kProjectFileName + kBackupSuffix);
            if (!bytes) {
                return std::unexpected(makeRecoveryFailedError(QStringLiteral("No readable backup file was found")));
            }
            auto loaded = ProjectSerializer::deserialize(*bytes);
            if (!loaded) {
                return std::unexpected(makeRecoveryFailedError(QString::fromStdString(loaded.error().toString())));
            }
            auto assets = validateProjectAssets(canonicalRoot, *loaded);
            if (!assets) {
                return std::unexpected(assets.error());
            }

            // The backup copy is only loaded into memory; project.json is not rewritten here.
            session->recoveryPending = false;
            session->recoveredFromBackup = true;
            session->hasFingerprint = false;
            session->descriptor.id = loaded->id();
            session->descriptor.name = QString::fromStdString(loaded->name());
            session->descriptor.path = canonicalRoot;
            session->project = *loaded;
            if (!m_coordinator->bindProject(session->descriptor.id, canonicalRoot)) {
                return std::unexpected(makeLockedError());
            }

            application::OpenProjectResult result;
            result.project = std::move(*loaded);
            result.recoverySource = application::RecoverySource::Backup;
            result.warnings = std::move(*assets);
            application::ProjectWarning recoveryWarning;
            recoveryWarning.code = QStringLiteral("project.recovered_from_backup");
            recoveryWarning.safeMessage = QStringLiteral("Project data was restored from the backup file");
            recoveryWarning.path = QString(kProjectFileName) + kBackupSuffix;
            result.warnings.append(recoveryWarning);
            return result;
        });
}

QFuture<application::Result<application::SaveProjectResult>>
FileSystemProjectRepository::saveProject(application::SaveProjectRequest request,
                                         application::OperationContext context) {
    return runDataOperation<application::SaveProjectResult>(
        [this, request = std::move(request), context]() -> application::Result<application::SaveProjectResult> {
            if (context.stopToken.stop_requested()) {
                return std::unexpected(makeCancelledError(u"Project save"));
            }
            auto session = findSession(request.project.id());
            if (session == nullptr || session->recoveryPending) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Project,
                                                      QStringLiteral("project.not_open"),
                                                      QStringLiteral("The project session is not open")));
            }
            auto writeTicket = m_coordinator->beginWrite(session->canonicalRoot);
            if (!writeTicket) {
                return std::unexpected(makeLockedError());
            }

            const QByteArray bytes = ProjectSerializer::serialize(request.project);
            auto validated = ProjectSerializer::deserialize(bytes);
            if (!validated) {
                return std::unexpected(toApplicationError(validated.error()));
            }
            auto assets = validateProjectAssets(session->canonicalRoot, request.project);
            if (!assets) {
                return std::unexpected(assets.error());
            }
            // Last cancellation checkpoint; the transaction below ignores stopToken so an
            // interrupted save never leaves a half-written project.
            if (context.stopToken.stop_requested()) {
                return std::unexpected(makeCancelledError(u"Project save"));
            }

            const QString canonicalRoot = session->canonicalRoot;
            const QString projectJsonPath = canonicalRoot + QLatin1Char('/') + kProjectFileName;
            if (!QFileInfo(canonicalRoot).isWritable()) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Persistence,
                                                      QStringLiteral("persistence.read_only"),
                                                      QStringLiteral("The project directory is read-only")));
            }
            if (!hasDiskSpace(canonicalRoot, bytes.size() + kWriteMarginBytes)) {
                return std::unexpected(makeDiskFullError());
            }

            // Test seam: fires once the transaction has begun; stop requests made from the
            // hook are deliberately ignored until the save completes.
            if (m_saveTransactionHook) {
                m_saveTransactionHook();
            }

            if (!session->recoveredFromBackup) {
                auto currentBytes = readFileBytes(projectJsonPath);
                if (!currentBytes) {
                    return std::unexpected(makeSaveConflictError());
                }
                if (session->hasFingerprint && QCryptographicHash::hash(*currentBytes, QCryptographicHash::Sha256) !=
                                                   session->projectJsonFingerprint.sha256) {
                    return std::unexpected(makeSaveConflictError());
                }
                // Rotate the single-generation backup only when the current file is complete and
                // valid; a damaged current file must never overwrite the only good copy. Because
                // rotation runs first, a write-protected directory surfaces here as
                // persistence.backup_failed and the old .bak is preserved.
                auto currentValid = ProjectSerializer::deserialize(*currentBytes);
                if (currentValid) {
                    if (auto backupError = writeFileSync(projectJsonPath + kBackupSuffix, *currentBytes)) {
                        auto error =
                            application::makeApplicationError(application::ErrorCategory::Persistence,
                                                              QStringLiteral("persistence.backup_failed"),
                                                              QStringLiteral("The backup copy could not be written"));
                        error.diagnostic = backupError->diagnostic;
                        return std::unexpected(error);
                    }
                }
            }

            if (auto writeError = writeFileSync(projectJsonPath, bytes)) {
                return std::unexpected(*writeError);
            }

            if (auto fingerprint = fingerprintFile(projectJsonPath)) {
                session->projectJsonFingerprint = *fingerprint;
                session->hasFingerprint = true;
            }
            session->recoveredFromBackup = false;
            session->descriptor.name = QString::fromStdString(request.project.name());
            session->project = request.project;
            session->project->markClean();

            // After a successful save, reclaim derived layer files no longer referenced by
            // project.json on a worker thread. Fire-and-forget: the future is intentionally
            // dropped and the save path never blocks on the prune, which needs the same
            // project write lock that this save transaction still holds.
            if (m_derivedStore != nullptr) {
                const domain::ProjectId savedProjectId = request.project.id();
                const QString savedRoot = session->canonicalRoot;
                QSet<QString> referenced = collectReferencedLayerPaths(request.project);
                QThreadPool::globalInstance()->start(
                    [store = m_derivedStore, savedProjectId, savedRoot, referenced = std::move(referenced)] {
                        application::PruneAssetsRequest request;
                        request.projectId = savedProjectId;
                        request.projectRoot = savedRoot;
                        request.referencedRelativePaths = referenced;
                        (void)store->pruneUnusedAssets(std::move(request), application::OperationContext{});
                    });
            }

            return application::SaveProjectResult{request.project.revision()};
        });
}

QFuture<application::Result<application::SaveAsProjectResult>>
FileSystemProjectRepository::saveProjectAs(application::SaveAsProjectRequest request,
                                           application::OperationContext context) {
    return runDataOperation<application::SaveAsProjectResult>(
        [this, request = std::move(request), context]() -> application::Result<application::SaveAsProjectResult> {
            if (context.stopToken.stop_requested()) {
                return std::unexpected(makeCancelledError(u"Project save as"));
            }
            auto session = findSession(request.project.id());
            if (session == nullptr || session->recoveryPending) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Project,
                                                      QStringLiteral("project.not_open"),
                                                      QStringLiteral("The project session is not open")));
            }
            const QString oldRoot = session->canonicalRoot;
            const QString newCleaned = QDir::cleanPath(QDir::fromNativeSeparators(request.newPath.trimmed()));
            if (newCleaned.isEmpty() || !QDir::isAbsolutePath(newCleaned)) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Input,
                                                      QStringLiteral("input.invalid_save_as"),
                                                      QStringLiteral("The destination path is invalid")));
            }
            const QFileInfo newInfo(newCleaned);
            QString newCoordinatorRoot;
            if (newInfo.exists()) {
                newCoordinatorRoot = canonicalDirectoryPath(newCleaned);
                if (newCoordinatorRoot.isEmpty()) {
                    return std::unexpected(makeTargetNotEmptyError());
                }
            } else {
                newCoordinatorRoot = QDir::cleanPath(newInfo.absoluteFilePath());
            }
            if (sameRootPath(oldRoot, newCoordinatorRoot)) {
                return std::unexpected(
                    application::makeApplicationError(application::ErrorCategory::Input,
                                                      QStringLiteral("input.invalid_save_as"),
                                                      QStringLiteral("The destination must differ from the project")));
            }
            // Serialize and validate first; the copy transaction below is cancelable per file.
            const QByteArray bytes = ProjectSerializer::serialize(request.project);
            auto validated = ProjectSerializer::deserialize(bytes);
            if (!validated) {
                return std::unexpected(toApplicationError(validated.error()));
            }

            // Acquire write serialization for both roots in deterministic order so concurrent
            // saveAs operations cannot deadlock.
            const QString first = std::min(oldRoot, newCoordinatorRoot);
            const QString second = std::max(oldRoot, newCoordinatorRoot);
            auto firstTicket = m_coordinator->beginWrite(first);
            if (!firstTicket) {
                return std::unexpected(makeLockedError());
            }
            auto secondTicket = m_coordinator->beginWrite(second);
            if (!secondTicket) {
                return std::unexpected(makeLockedError());
            }

            bool createdDirectory = false;
            if (newInfo.exists()) {
                if (!newInfo.isDir() || !QDir(newCleaned).isEmpty()) {
                    return std::unexpected(makeTargetNotEmptyError());
                }
            } else {
                if (context.stopToken.stop_requested()) {
                    return std::unexpected(makeCancelledError(u"Project save as"));
                }
                if (!QDir().mkpath(newCleaned)) {
                    return std::unexpected(toPersistenceError(QFileDevice::PermissionsError));
                }
                createdDirectory = true;
            }
            const QString canonicalNew = canonicalDirectoryPath(newCleaned);
            if (canonicalNew.isEmpty()) {
                removeCreatedTarget({}, newCleaned, createdDirectory);
                return std::unexpected(toPersistenceError(QFileDevice::OpenError));
            }

            // Cleanup guard: declared before newLock so the lock is released before the tree
            // is removed when the operation fails.
            struct FailedTargetCleanup {
                QString stagingRoot;
                QString targetRoot;
                QVector<QString> committedFiles;
                bool createdDirectory = false;
                bool armed = true;
                ~FailedTargetCleanup() {
                    if (armed) {
                        for (auto it = committedFiles.crbegin(); it != committedFiles.crend(); ++it) {
                            QFile::remove(*it);
                        }
                        removeCreatedTarget(stagingRoot, targetRoot, createdDirectory);
                        if (!createdDirectory) {
                            QDir(targetRoot).rmdir(QStringLiteral("assets/original"));
                            QDir(targetRoot).rmdir(QStringLiteral("assets/derived"));
                            QDir(targetRoot).rmdir(QStringLiteral("assets"));
                        }
                    }
                }
            } cleanup{canonicalNew + QLatin1Char('/') + kStagingDirName, newCleaned, {}, createdDirectory, true};

            if (findSessionByRoot(canonicalNew) != nullptr) {
                return std::unexpected(makeLockedError());
            }
            auto newLock = std::make_unique<QLockFile>(canonicalNew + QLatin1Char('/') + kLockFileName);
            newLock->setStaleLockTime(0);
            if (!newLock->tryLock(0)) {
                return std::unexpected(toProjectLockError(newLock->error()));
            }
            if (context.stopToken.stop_requested()) {
                return std::unexpected(makeCancelledError(u"Project save as"));
            }

            const QString stagingRoot = canonicalNew + QLatin1Char('/') + kStagingDirName;
            qint64 requiredBytes = bytes.size() + kWriteMarginBytes;
            for (const QLatin1String assetDir : {kAssetsOriginal, kAssetsDerived}) {
                const qint64 assetBytes = treeSize(oldRoot + QLatin1Char('/') + assetDir);
                if (assetBytes > std::numeric_limits<qint64>::max() - requiredBytes) {
                    requiredBytes = std::numeric_limits<qint64>::max();
                    break;
                }
                requiredBytes += assetBytes;
            }
            if (!hasDiskSpace(canonicalNew, requiredBytes)) {
                return std::unexpected(makeDiskFullError());
            }
            if (!QDir().mkpath(stagingRoot)) {
                return std::unexpected(toPersistenceError(QFileDevice::PermissionsError));
            }
            if (auto writeError = writeFileSync(stagingRoot + QLatin1Char('/') + kProjectFileName, bytes)) {
                return std::unexpected(*writeError);
            }
            for (const QLatin1String assetDir : {kAssetsOriginal, kAssetsDerived}) {
                const QString sourceDir = oldRoot + QLatin1Char('/') + assetDir;
                if (!QFileInfo(sourceDir).isDir()) {
                    continue;
                }
                if (auto copyError =
                        copyTreeVerified(sourceDir, stagingRoot + QLatin1Char('/') + assetDir, context.stopToken)) {
                    return std::unexpected(*copyError);
                }
            }
            QDir(stagingRoot).mkpath(kAssetsOriginal.toString());
            QDir(stagingRoot).mkpath(kAssetsDerived.toString());
            const QString canonicalStaging = canonicalDirectoryPath(stagingRoot);
            auto stagedAssets = validateProjectAssets(canonicalStaging, request.project);
            if (!stagedAssets) {
                return std::unexpected(stagedAssets.error());
            }

            // Commit: move the verified staged files into the empty target root; this is the
            // only non-cancelable step besides the session switch.
            const QString newJsonPath = canonicalNew + QLatin1Char('/') + kProjectFileName;
            QVector<QString> stagedPaths;
            QDirIterator staged(stagingRoot, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (staged.hasNext()) {
                stagedPaths.append(staged.next());
            }
            std::stable_sort(
                stagedPaths.begin(), stagedPaths.end(), [stagingRoot](const QString& lhs, const QString& rhs) {
                    const bool lhsIsProject = lhs == stagingRoot + QLatin1Char('/') + kProjectFileName;
                    const bool rhsIsProject = rhs == stagingRoot + QLatin1Char('/') + kProjectFileName;
                    return lhsIsProject != rhsIsProject ? !lhsIsProject : lhs < rhs;
                });
            int commitIndex = 0;
            for (const QString& stagedPath : stagedPaths) {
                const QString relative = QStringView{stagedPath}.mid(stagingRoot.size() + 1).toString();
                if (m_saveAsCommitProbe && !m_saveAsCommitProbe(relative, commitIndex)) {
                    return std::unexpected(
                        application::makeApplicationError(application::ErrorCategory::Persistence,
                                                          QStringLiteral("persistence.atomic_commit_failed"),
                                                          QStringLiteral("The project copy could not be committed")));
                }
                const QString targetPath = canonicalNew + QLatin1Char('/') + relative;
                QFileInfo(targetPath).dir().mkpath(QStringLiteral("."));
                if (!QFile::rename(stagedPath, targetPath)) {
                    return std::unexpected(toPersistenceError(QFileDevice::RenameError));
                }
                cleanup.committedFiles.append(targetPath);
                ++commitIndex;
            }
            if (!QDir().mkpath(canonicalNew + QLatin1Char('/') + kAssetsOriginal) ||
                !QDir().mkpath(canonicalNew + QLatin1Char('/') + kAssetsDerived)) {
                return std::unexpected(toPersistenceError(QFileDevice::PermissionsError));
            }
            if (!m_coordinator->bindProject(session->descriptor.id, canonicalNew)) {
                return std::unexpected(makeLockedError());
            }
            QDir(stagingRoot).removeRecursively();

            {
                QMutexLocker locker(&m_mutex);
                m_sessions.erase(oldRoot);
                session->descriptor.path = canonicalNew;
                session->descriptor.name = QString::fromStdString(request.project.name());
                session->canonicalRoot = canonicalNew;
                // Destroying the old QLockFile releases the old directory lock only after the
                // new lock and session have been established.
                session->lock = std::move(newLock);
                m_sessions.emplace(canonicalNew, session);
            }
            if (auto fingerprint = fingerprintFile(newJsonPath)) {
                session->projectJsonFingerprint = *fingerprint;
                session->hasFingerprint = true;
            }
            session->recoveredFromBackup = false;
            session->project = request.project;
            session->project->markClean();
            m_coordinator->unbindProject(oldRoot);
            if (sameRootPath(first, oldRoot)) {
                firstTicket->release();
            } else {
                secondTicket->release();
            }
            m_coordinator->discardRoot(oldRoot);
            cleanup.armed = false;
            return application::SaveAsProjectResult{session->descriptor, request.project.revision()};
        });
}

QFuture<application::Result<void>> FileSystemProjectRepository::closeProject(application::CloseProjectRequest request,
                                                                             application::OperationContext context) {
    return runDataOperation<void>([this, request = std::move(request), context]() -> application::Result<void> {
        if (context.stopToken.stop_requested()) {
            return std::unexpected(makeCancelledError(u"Project close"));
        }
        std::shared_ptr<RepositorySession> session;
        if (request.projectId.isValid()) {
            session = findSession(request.projectId);
        } else {
            const QString canonicalRoot =
                canonicalDirectoryPath(QDir::cleanPath(QDir::fromNativeSeparators(request.projectRoot.trimmed())));
            if (!canonicalRoot.isEmpty()) {
                session = findSessionByRoot(canonicalRoot);
            }
        }
        if (session == nullptr) {
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Project,
                                                  QStringLiteral("project.not_open"),
                                                  QStringLiteral("The project session is not open")));
        }
        const QString canonicalRoot = session->canonicalRoot;
        auto closeTicket = m_coordinator->beginClose(canonicalRoot, kProjectCloseTimeoutMs);
        if (!closeTicket) {
            if (closeTicket.error() == ProjectIoCoordinator::CloseFailure::Timeout) {
                return std::unexpected(application::makeApplicationError(
                    application::ErrorCategory::Project,
                    QStringLiteral("project.close_timeout"),
                    QStringLiteral("The project is still busy and could not be closed yet"),
                    application::RecoveryAction::Retry,
                    true));
            }
            return std::unexpected(
                application::makeApplicationError(application::ErrorCategory::Project,
                                                  QStringLiteral("project.not_open"),
                                                  QStringLiteral("The project session is not open")));
        }
        // Releasing the lock file and dropping runtime state only; no file writes and no
        // domain mutation.
        session->lock.reset();
        forgetSession(canonicalRoot);
        closeTicket->release();
        m_coordinator->discardRoot(canonicalRoot);
        return {};
    });
}

} // namespace manga::data
