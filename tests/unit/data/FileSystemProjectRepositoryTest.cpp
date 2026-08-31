#include "FileSystemProjectRepositoryTest.h"

#include "data/DataErrorMapping.h"
#include "data/project/FileSystemProjectRepository.h"
#include "data/project/PathSafety.h"
#include "data/project/ProjectIoCoordinator.h"
#include "data/project/ProjectSerializer.h"
#include "domain/model/ComicPage.h"
#include "domain/model/ComicProject.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFuture>
#include <QProcess>
#include <QRegularExpression>
#include <QtTest/QtTest>

#include <algorithm>
#include <memory>
#include <optional>
#include <stop_token>

using namespace manga;
using namespace manga::data;
using domain::ComicPage;
using domain::ComicProject;
using domain::ImageSize;
using domain::PageId;
using domain::RectGeometry;
using domain::RegionId;
using domain::TextContent;
using domain::TextRegion;

namespace {

template <typename T> application::Result<T> await(QFuture<application::Result<T>> future) {
    future.waitForFinished();
    return future.result();
}

void expectNoError(const application::ApplicationError& error) {
    if (!error.code.isEmpty()) {
        QFAIL(qPrintable(QStringLiteral("unexpected error: %1").arg(error.code)));
    }
}

void expectError(const application::ApplicationError& error, const QString& code) {
    QCOMPARE(error.code, code);
}

void expectStdString(const std::string& actual, const QString& expected) {
    QCOMPARE(QString::fromStdString(actual), expected);
}

// Adds one page whose original asset points at assets/original/page_0001.png and optionally
// creates the backing file inside the project root.
bool addAssetPage(ComicProject& project, const QString& root, bool createFile) {
    ComicPage page(PageId::generate(), std::string("assets/original/page_0001.png"), ImageSize{3, 2}, 0);
    if (createFile) {
        QFile asset(root + QStringLiteral("/assets/original/page_0001.png"));
        if (!asset.open(QIODevice::WriteOnly)) {
            return false;
        }
        asset.write("placeholder asset bytes");
        asset.close();
    }
    return project.addPage(page);
}

} // namespace

void FileSystemProjectRepositoryTest::init() {
    QVERIFY(m_tempDir.isValid());
    m_coordinator = std::make_unique<ProjectIoCoordinator>();
    m_repository = std::make_unique<FileSystemProjectRepository>(m_coordinator.get());
}

void FileSystemProjectRepositoryTest::cleanup() {
    m_repository.reset();
    m_coordinator.reset();
}

QString FileSystemProjectRepositoryTest::newRoot() {
    return m_tempDir.path() + QStringLiteral("/project-%1").arg(m_rootCounter++);
}

std::optional<application::ProjectDescriptor> FileSystemProjectRepositoryTest::createProject(const QString& path) {
    application::CreateProjectRequest request;
    request.name = QStringLiteral("Test Project");
    request.path = path;
    request.sourceLanguage = domain::languages::Japanese;
    request.targetLanguage = domain::languages::SimplifiedChinese;
    request.readingDirection = domain::ReadingDirection::RightToLeft;
    auto result = await(m_repository->createProject(std::move(request), {}));
    if (!result) {
        qWarning() << "createProject failed:" << result.error().code;
        return std::nullopt;
    }
    return *result;
}

std::optional<application::OpenProjectResult> FileSystemProjectRepositoryTest::openProject(const QString& path) {
    application::OpenProjectRequest request;
    request.path = path;
    auto result = await(m_repository->openProject(request, {}));
    if (!result) {
        qWarning() << "openProject failed:" << result.error().code;
        return std::nullopt;
    }
    return *result;
}

application::ApplicationError FileSystemProjectRepositoryTest::openProjectError(const QString& path) {
    application::OpenProjectRequest request;
    request.path = path;
    auto result = await(m_repository->openProject(request, {}));
    if (result) {
        return {};
    }
    return result.error();
}

application::ApplicationError FileSystemProjectRepositoryTest::saveProject(const ComicProject& project) {
    application::SaveProjectRequest request;
    request.project = project;
    auto result = await(m_repository->saveProject(std::move(request), {}));
    if (!result) {
        return result.error();
    }
    return {};
}

application::ApplicationError FileSystemProjectRepositoryTest::saveProjectAs(const ComicProject& project,
                                                                             const QString& newPath) {
    application::SaveAsProjectRequest request;
    request.project = project;
    request.newPath = newPath;
    auto result = await(m_repository->saveProjectAs(std::move(request), {}));
    if (!result) {
        return result.error();
    }
    m_saveAsDescriptor = result->descriptor;
    return {};
}

application::ApplicationError FileSystemProjectRepositoryTest::closeProject(const ComicProject& project) {
    application::CloseProjectRequest request;
    request.projectId = project.id();
    auto result = await(m_repository->closeProject(request, {}));
    if (!result) {
        return result.error();
    }
    return {};
}

void FileSystemProjectRepositoryTest::createProjectProducesLayout() {
    const QString root = newRoot();
    const auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    QVERIFY(descriptor->id.isValid());
    QCOMPARE(descriptor->path, QFileInfo(root).canonicalFilePath());
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/project.json")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/assets/original")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/assets/derived")));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/.mangatranslator.lock")));

    application::CloseProjectRequest request;
    request.projectId = descriptor->id;
    QVERIFY(await(m_repository->closeProject(request, {})).has_value());
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/.mangatranslator.lock")));
}

void FileSystemProjectRepositoryTest::createProjectRejectsNonEmptyDirectory() {
    const QString root = newRoot();
    QVERIFY(QDir().mkpath(root));
    QFile marker(root + QStringLiteral("/occupied.txt"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("not empty");
    marker.close();

    const auto descriptor = createProject(root);
    QVERIFY(!descriptor.has_value());
}

void FileSystemProjectRepositoryTest::saveReopenRoundTripPreservesEdits() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    QVERIFY(!project.isDirty());
    QCOMPARE(opened->recoverySource, application::RecoverySource::Current);

    QVERIFY(addAssetPage(project, root, true));
    project.setName("Edited Name");
    QVERIFY(project.isDirty());

    const quint64 revision = project.revision();
    application::SaveProjectRequest saveRequest;
    saveRequest.project = project;
    const auto saveResult = await(m_repository->saveProject(saveRequest, {}));
    QVERIFY(saveResult.has_value());
    QCOMPARE(saveResult->savedRevision, revision);
    QVERIFY(project.markCleanIfRevision(saveResult->savedRevision));
    QVERIFY(!project.isDirty());
    expectNoError(closeProject(project));

    auto reopened = openProject(root);
    QVERIFY(reopened.has_value());
    expectStdString(reopened->project.name(), QStringLiteral("Edited Name"));
    QCOMPARE(reopened->recoverySource, application::RecoverySource::Current);
    QVERIFY(reopened->project.pages().size() == 1);
    expectStdString(reopened->project.pages().front().originalAssetPath(),
                    QStringLiteral("assets/original/page_0001.png"));
    QVERIFY((reopened->project.pages().front().imageSize() == ImageSize{3, 2}));
    QVERIFY(!reopened->project.isDirty());
}

void FileSystemProjectRepositoryTest::secondSessionLockRejectsOpen() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());

    // Same repository: reopening returns the existing session instead of a second lock.
    QVERIFY(openProject(root).has_value());

    // A second repository with its own coordinator exercises the inter-process QLockFile.
    ProjectIoCoordinator otherCoordinator;
    FileSystemProjectRepository otherRepository(&otherCoordinator);
    application::OpenProjectRequest request;
    request.path = root;
    auto second = await(otherRepository.openProject(request, {}));
    QVERIFY(!second.has_value());
    QCOMPARE(second.error().code, QStringLiteral("persistence.locked"));
    QVERIFY(second.error().safeMessage.contains(QStringLiteral("another instance")));
}

void FileSystemProjectRepositoryTest::staleLockIsCleared() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    application::CloseProjectRequest request;
    request.projectId = descriptor->id;
    QVERIFY(await(m_repository->closeProject(request, {})).has_value());

    // A child process acquires the lock and dies, leaving a lock file naming a dead process.
    QProcess crasher;
    crasher.start(QCoreApplication::applicationFilePath(), QStringList{QStringLiteral("--lock-and-crash"), root});
    QVERIFY(crasher.waitForStarted(10000));
    QVERIFY(crasher.waitForFinished(30000));
    QVERIFY(QFileInfo::exists(root + QStringLiteral("/.mangatranslator.lock")));

    auto opened = openProject(root);
    QVERIFY(opened.has_value());
}

void FileSystemProjectRepositoryTest::saveConflictDetectsExternalModification() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;

    project.setName("First Save");
    expectNoError(saveProject(project));

    QFile external(root + QStringLiteral("/project.json"));
    QVERIFY(external.open(QIODevice::Append));
    external.write(" external touch ");
    external.close();

    project.setName("Second Save");
    expectError(saveProject(project), QStringLiteral("persistence.save_conflict"));

    // The externally modified file is still intact and openable.
    auto reopened = openProject(root);
    QVERIFY(reopened.has_value());
}

void FileSystemProjectRepositoryTest::corruptCurrentRecoversFromBackup() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;

    project.setName("First Save");
    expectNoError(saveProject(project));
    project.setName("Second Save");
    expectNoError(saveProject(project));
    expectNoError(closeProject(project));

    QFile current(root + QStringLiteral("/project.json"));
    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Truncate));
    current.write("{ damaged");
    current.close();

    expectError(openProjectError(root), QStringLiteral("persistence.recovery_failed"));

    application::OpenProjectRequest recoveryRequest;
    recoveryRequest.path = root;
    auto recovered = await(m_repository->recoverProject(recoveryRequest, {}));
    QVERIFY(recovered.has_value());
    QCOMPARE(recovered->recoverySource, application::RecoverySource::Backup);
    // The backup is one generation behind: it holds the last valid save before the write
    // that was never observed as valid ("Second Save" was never rotated into the backup).
    expectStdString(recovered->project.name(), QStringLiteral("First Save"));
    const bool recoveryWarned = std::any_of(
        recovered->warnings.cbegin(), recovered->warnings.cend(), [](const application::ProjectWarning& warning) {
            return warning.code == QStringLiteral("project.recovered_from_backup");
        });
    QVERIFY(recoveryWarned);

    // The first save after a backup recovery skips rotation and succeeds.
    ComicProject& recoveredProject = recovered->project;
    const quint64 revision = recoveredProject.revision();
    expectNoError(saveProject(recoveredProject));
    QVERIFY(recoveredProject.markCleanIfRevision(revision));
    expectNoError(closeProject(recoveredProject));

    auto reopened = openProject(root);
    QVERIFY(reopened.has_value());
    QCOMPARE(reopened->recoverySource, application::RecoverySource::Current);
    expectStdString(reopened->project.name(), QStringLiteral("First Save"));
}

void FileSystemProjectRepositoryTest::missingOriginalAssetRejectsOpen() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    QVERIFY(addAssetPage(project, root, true));
    expectNoError(saveProject(project));
    expectNoError(closeProject(project));
    QVERIFY(QFile::remove(root + QStringLiteral("/assets/original/page_0001.png")));

    expectError(openProjectError(root), QStringLiteral("project.missing_asset"));
}

void FileSystemProjectRepositoryTest::pathEscapeAssetRejectsOpen() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    ComicPage page(PageId::generate(), std::string("../evil.png"), ImageSize{3, 2}, 0);
    QVERIFY(project.addPage(page));
    expectNoError(closeProject(project));
    QFile projectJson(root + QStringLiteral("/project.json"));
    QVERIFY(projectJson.open(QIODevice::WriteOnly | QIODevice::Truncate));
    QCOMPARE(projectJson.write(ProjectSerializer::serialize(project)), ProjectSerializer::serialize(project).size());
    projectJson.close();

    expectError(openProjectError(root), QStringLiteral("project.invalid_semantic"));
}

void FileSystemProjectRepositoryTest::absolutePathAssetRejectsOpen() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    ComicPage page(PageId::generate(), std::string("C:/evil.png"), ImageSize{3, 2}, 0);
    QVERIFY(project.addPage(page));
    expectNoError(closeProject(project));
    QFile projectJson(root + QStringLiteral("/project.json"));
    QVERIFY(projectJson.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray bytes = ProjectSerializer::serialize(project);
    QCOMPARE(projectJson.write(bytes), bytes.size());
    projectJson.close();

    expectError(openProjectError(root), QStringLiteral("project.invalid_semantic"));
}

void FileSystemProjectRepositoryTest::derivedAssetMissingProducesWarning() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    ComicPage page(PageId::generate(), std::string("assets/original/page_0001.png"), ImageSize{3, 2}, 0);
    page.setClearLayerAsset(std::string("assets/derived/clear_v1.png"));
    QFile asset(root + QStringLiteral("/assets/original/page_0001.png"));
    QVERIFY(asset.open(QIODevice::WriteOnly));
    asset.write("placeholder asset bytes");
    asset.close();
    QVERIFY(project.addPage(page));
    expectNoError(saveProject(project));
    expectNoError(closeProject(project));

    auto reopened = openProject(root);
    QVERIFY(reopened.has_value());
    const bool warned = std::any_of(
        reopened->warnings.cbegin(), reopened->warnings.cend(), [](const application::ProjectWarning& warning) {
            return warning.code == QStringLiteral("project.derived_asset_missing") &&
                   warning.path == QStringLiteral("assets/derived/clear_v1.png");
        });
    QVERIFY(warned);
    QVERIFY(reopened->project.pages().size() == 1);
}

void FileSystemProjectRepositoryTest::derivedAssetCorruptionProducesWarning() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    QVERIFY(addAssetPage(project, root, true));
    const QString derivedPath = root + QStringLiteral("/assets/derived/clear_v1.png");
    QFile derived(derivedPath);
    QVERIFY(derived.open(QIODevice::WriteOnly));
    derived.write("not an image");
    derived.close();
    ComicPage page = project.pages().front();
    QVERIFY(project.removePage(page.id()));
    page.setClearLayerAsset(std::string("assets/derived/clear_v1.png"));
    QVERIFY(project.addPage(page));
    expectNoError(saveProject(project));
    expectNoError(closeProject(project));

    auto reopened = openProject(root);
    QVERIFY(reopened.has_value());
    const bool warned = std::any_of(
        reopened->warnings.cbegin(), reopened->warnings.cend(), [](const application::ProjectWarning& warning) {
            return warning.code == QStringLiteral("project.derived_asset_invalid") &&
                   warning.path == QStringLiteral("assets/derived/clear_v1.png");
        });
    QVERIFY(warned);
}

void FileSystemProjectRepositoryTest::saveAsRelocatesSessionAndKeepsOldDirectory() {
    const QString oldRoot = newRoot();
    auto descriptor = createProject(oldRoot);
    QVERIFY(descriptor.has_value());
    auto opened = openProject(oldRoot);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    QVERIFY(addAssetPage(project, oldRoot, true));
    expectNoError(saveProject(project));

    const QString movedRoot = newRoot();
    expectNoError(saveProjectAs(project, movedRoot));
    QVERIFY(m_saveAsDescriptor.has_value());
    QCOMPARE(m_saveAsDescriptor->id, descriptor->id);
    QCOMPARE(m_saveAsDescriptor->path, QFileInfo(movedRoot).canonicalFilePath());
    QVERIFY(QFileInfo::exists(movedRoot + QStringLiteral("/project.json")));
    QVERIFY(QFileInfo::exists(movedRoot + QStringLiteral("/assets/original/page_0001.png")));
    // The old directory is left as-is.
    QVERIFY(QFileInfo::exists(oldRoot + QStringLiteral("/project.json")));
    QVERIFY(QFileInfo::exists(oldRoot + QStringLiteral("/assets/original/page_0001.png")));

    // The session points at the new root: a later save only touches the new directory.
    project.setName("After Move");
    expectNoError(saveProject(project));
    expectNoError(closeProject(project));

    auto reopened = openProject(movedRoot);
    QVERIFY(reopened.has_value());
    expectStdString(reopened->project.name(), QStringLiteral("After Move"));
    expectNoError(closeProject(reopened->project));

    // The old root can be opened independently after the relocation.
    auto oldOpened = openProject(oldRoot);
    QVERIFY(oldOpened.has_value());
    expectStdString(oldOpened->project.name(), QStringLiteral("Test Project"));
}

void FileSystemProjectRepositoryTest::saveAsRejectsNonEmptyTarget() {
    const QString oldRoot = newRoot();
    QVERIFY(createProject(oldRoot).has_value());
    auto opened = openProject(oldRoot);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;

    const QString occupiedRoot = newRoot();
    QVERIFY(QDir().mkpath(occupiedRoot));
    QFile marker(occupiedRoot + QStringLiteral("/occupied.txt"));
    QVERIFY(marker.open(QIODevice::WriteOnly));
    marker.write("occupied");
    marker.close();

    expectError(saveProjectAs(project, occupiedRoot), QStringLiteral("persistence.target_not_empty"));

    // The session still points at the old root and saving keeps working there.
    project.setName("Still Old Root");
    expectNoError(saveProject(project));
    QVERIFY(QFileInfo::exists(oldRoot + QStringLiteral("/project.json.bak")));
}

void FileSystemProjectRepositoryTest::saveAsRejectsCurrentRootWithoutBlocking() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());

    application::SaveAsProjectRequest request;
    request.project = opened->project;
    request.newPath = root;
    auto future = m_repository->saveProjectAs(request, {});
    future.waitForFinished();
    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("input.invalid_save_as"));
}

void FileSystemProjectRepositoryTest::saveAsCommitFailureCleansExistingEmptyTarget() {
    const QString oldRoot = newRoot();
    QVERIFY(createProject(oldRoot).has_value());
    auto opened = openProject(oldRoot);
    QVERIFY(opened.has_value());
    QVERIFY(addAssetPage(opened->project, oldRoot, true));
    expectNoError(saveProject(opened->project));

    const QString targetRoot = newRoot();
    QVERIFY(QDir().mkpath(targetRoot));
    m_repository->setSaveAsCommitProbe([](const QString&, int commitIndex) { return commitIndex == 0; });
    expectError(saveProjectAs(opened->project, targetRoot), QStringLiteral("persistence.atomic_commit_failed"));
    m_repository->setSaveAsCommitProbe({});

    QCOMPARE(QDir(targetRoot).entryList(QDir::AllEntries | QDir::NoDotAndDotDot), QStringList{});
    opened->project.setName("Old Session Intact");
    expectNoError(saveProject(opened->project));
}

void FileSystemProjectRepositoryTest::repeatedOpenReturnsLatestSavedSnapshot() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    opened->project.setName("Latest Snapshot");
    expectNoError(saveProject(opened->project));

    auto repeated = openProject(root);
    QVERIFY(repeated.has_value());
    expectStdString(repeated->project.name(), QStringLiteral("Latest Snapshot"));
    QVERIFY(!repeated->project.isDirty());
}

void FileSystemProjectRepositoryTest::closeReleasesLockAndSession() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    application::CloseProjectRequest request;
    request.projectId = descriptor->id;
    QVERIFY(await(m_repository->closeProject(request, {})).has_value());

    application::SaveProjectRequest saveRequest;
    auto saveResult = await(m_repository->saveProject(std::move(saveRequest), {}));
    QVERIFY(!saveResult.has_value());
    QCOMPARE(saveResult.error().code, QStringLiteral("project.not_open"));

    auto secondClose = await(m_repository->closeProject(request, {}));
    QVERIFY(!secondClose.has_value());
    QCOMPARE(secondClose.error().code, QStringLiteral("project.not_open"));

    // The lock was released, so the project can be opened again.
    QVERIFY(openProject(root).has_value());
}

void FileSystemProjectRepositoryTest::closeWaitsForActivePageLease() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    auto lease = m_coordinator->acquireProjectLease(descriptor->id, descriptor->path);
    QVERIFY(lease.has_value());

    application::CloseProjectRequest request;
    request.projectId = descriptor->id;
    auto future = m_repository->closeProject(request, {});
    QTest::qWait(20);
    QVERIFY(!future.isFinished());

    lease->release();
    future.waitForFinished();
    QVERIFY(future.result().has_value());
}

void FileSystemProjectRepositoryTest::projectWriteOperationsSerialize() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    opened->project.setName("Serialized Save");

    auto heldTicket = m_coordinator->beginProjectWrite(descriptor->id, descriptor->path);
    QVERIFY(heldTicket.has_value());
    application::SaveProjectRequest request;
    request.project = opened->project;
    auto future = m_repository->saveProject(request, {});
    QTest::qWait(20);
    QVERIFY(!future.isFinished());

    heldTicket->release();
    future.waitForFinished();
    QVERIFY(future.result().has_value());
}

void FileSystemProjectRepositoryTest::recoveryPendingSessionCanCloseByRoot() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    application::CloseProjectRequest closeHealthy;
    closeHealthy.projectId = descriptor->id;
    QVERIFY(await(m_repository->closeProject(closeHealthy, {})).has_value());

    QFile current(root + QStringLiteral("/project.json"));
    QVERIFY(current.open(QIODevice::WriteOnly | QIODevice::Truncate));
    current.write("{ damaged");
    current.close();
    expectError(openProjectError(root), QStringLiteral("persistence.recovery_failed"));

    application::CloseProjectRequest abandonRecovery;
    abandonRecovery.projectRoot = root;
    QVERIFY(await(m_repository->closeProject(abandonRecovery, {})).has_value());

    ProjectIoCoordinator otherCoordinator;
    FileSystemProjectRepository otherRepository(&otherCoordinator);
    application::OpenProjectRequest request;
    request.path = root;
    const auto result = await(otherRepository.openProject(request, {}));
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("persistence.recovery_failed"));
}

void FileSystemProjectRepositoryTest::saveFailsWhenDiskSpaceProbeReportsFull() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;

    m_repository->setDiskSpaceProbe([](const QString&) { return qint64(0); });
    project.setName("No Space");
    const auto error = saveProject(project);
    QCOMPARE(error.code, QStringLiteral("persistence.disk_full"));
    QVERIFY(error.category == application::ErrorCategory::Persistence);
    QVERIFY(error.recovery == application::RecoveryAction::FreeDiskSpace);

    // With the default probe the same save succeeds and the project stays usable.
    m_repository->setDiskSpaceProbe({});
    project.setName("Saved Again");
    expectNoError(saveProject(project));
}

void FileSystemProjectRepositoryTest::saveRejectsWriteProtectedDirectory() {
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    project.setName("Blocked Save");

#ifdef Q_OS_WIN
    // A read-only directory attribute does not block file creation on NTFS; a deny ACE for
    // "write data / create files" is what the OS actually enforces.
    const QStringList denyArgs{
        QDir::toNativeSeparators(root), QStringLiteral("/deny"), QStringLiteral("*S-1-1-0:(WD)")};
    if (QProcess::execute(QStringLiteral("icacls"), denyArgs) != 0) {
        QSKIP("modifying ACLs is not permitted in this environment");
    }
    const auto restore = [&root] {
        QProcess::execute(QStringLiteral("icacls"),
                          QStringList{QDir::toNativeSeparators(root), QStringLiteral("/reset")});
    };
#else
    const auto restore = [&root] {
        QFile::setPermissions(root,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
                                  QFileDevice::WriteUser | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                                  QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup);
    };
    if (!QFile::setPermissions(root,
                               QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ExeOwner |
                                   QFileDevice::ExeUser | QFileDevice::ReadGroup | QFileDevice::ExeGroup)) {
        QSKIP("write-protecting a directory is not supported on this platform");
    }
#endif

    const auto error = saveProject(project);
    restore();

    QVERIFY(error.category == application::ErrorCategory::Persistence);
    // With a deny ACE the save fails while rotating the backup, which the save transaction
    // maps to persistence.backup_failed; the direct write path surfaces permission_denied.
    QVERIFY2(error.code == QStringLiteral("persistence.permission_denied") ||
                 error.code == QStringLiteral("persistence.read_only") ||
                 error.code == QStringLiteral("persistence.backup_failed"),
             qPrintable(error.code));

    // The last valid project is intact and writable again after restoring access.
    expectNoError(saveProject(project));
}

void FileSystemProjectRepositoryTest::reparsePointEscapeRejectsOpen() {
#ifdef Q_OS_WIN
    const QString root = newRoot();
    QVERIFY(createProject(root).has_value());

    QTemporaryDir outsideDir;
    QVERIFY(outsideDir.isValid());
    QFile outsideAsset(outsideDir.path() + QStringLiteral("/outside.png"));
    QVERIFY(outsideAsset.open(QIODevice::WriteOnly));
    outsideAsset.write("outside bytes");
    outsideAsset.close();

    // A directory junction can be created by unprivileged users on NTFS and redirects to a
    // target outside the project root.
    const QString junctionPath = root + QStringLiteral("/assets/jail");
    if (QProcess::execute(QStringLiteral("cmd"),
                          QStringList{QStringLiteral("/c"),
                                      QStringLiteral("mklink"),
                                      QStringLiteral("/J"),
                                      QDir::toNativeSeparators(junctionPath),
                                      QDir::toNativeSeparators(outsideDir.path())}) != 0) {
        QSKIP("NTFS junction creation is not permitted in this environment");
    }

    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;
    ComicPage page(PageId::generate(), std::string("assets/jail/outside.png"), ImageSize{3, 2}, 0);
    QVERIFY(project.addPage(page));
    expectNoError(closeProject(project));
    QFile projectJson(root + QStringLiteral("/project.json"));
    QVERIFY(projectJson.open(QIODevice::WriteOnly | QIODevice::Truncate));
    const QByteArray bytes = ProjectSerializer::serialize(project);
    QCOMPARE(projectJson.write(bytes), bytes.size());
    projectJson.close();

    // The canonical containment check resolves the junction and rejects the escape.
    expectError(openProjectError(root), QStringLiteral("project.invalid_semantic"));
#else
    QSKIP("Windows-only junction test");
#endif
}

void FileSystemProjectRepositoryTest::saveIgnoresStopRequestAfterTransactionStart() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;

    std::stop_source source;
    application::OperationContext context;
    context.stopToken = source.get_token();
    m_repository->setSaveTransactionHook([&source] { source.request_stop(); });

    project.setName("Saved Despite Stop");
    application::SaveProjectRequest saveRequest;
    saveRequest.project = project;
    const auto saveResult = await(m_repository->saveProject(std::move(saveRequest), context));
    m_repository->setSaveTransactionHook({});

    QVERIFY(saveResult.has_value());
    QCOMPARE(saveResult->savedRevision, project.revision());
    expectStdString(project.name(), QStringLiteral("Saved Despite Stop"));
    // No half-written temporary file remains next to the committed project files.
    QStringList files = QDir(root).entryList(QDir::Files | QDir::NoDotAndDotDot);
    files.sort();
    QCOMPARE(files,
             QStringList({QStringLiteral(".mangatranslator.lock"),
                          QStringLiteral("project.json"),
                          QStringLiteral("project.json.bak")}));
}

void FileSystemProjectRepositoryTest::saveResultDoesNotClearEditsMadeDuringSave() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    auto opened = openProject(root);
    QVERIFY(opened.has_value());
    ComicProject& project = opened->project;

    const quint64 revisionAtSave = project.revision();
    application::SaveProjectRequest saveRequest;
    saveRequest.project = project;
    project.setName("Edited During Save");

    const auto saveResult = await(m_repository->saveProject(std::move(saveRequest), {}));
    QVERIFY(saveResult.has_value());
    QCOMPARE(saveResult->savedRevision, revisionAtSave);

    // The stale save completion must not clear the newer edit.
    QVERIFY(!project.markCleanIfRevision(saveResult->savedRevision));
    QVERIFY(project.isDirty());

    expectNoError(saveProject(project));
    expectNoError(closeProject(project));
    auto reopened = openProject(root);
    QVERIFY(reopened.has_value());
    expectStdString(reopened->project.name(), QStringLiteral("Edited During Save"));
}

void FileSystemProjectRepositoryTest::errorsAndWarningsDoNotLeakPathsOrUserContent() {
    const auto leak = [this](const QString& text, const char* context) {
        QVERIFY2(!text.contains(QRegularExpression(QStringLiteral("[A-Za-z]:[\\\\/]"))),
                 qPrintable(QStringLiteral("%1 leaks an absolute path: %2").arg(QLatin1String(context), text)));
        QVERIFY2(!text.contains(m_tempDir.path()),
                 qPrintable(QStringLiteral("%1 leaks the project root: %2").arg(QLatin1String(context), text)));
        QVERIFY2(!text.contains(QStringLiteral("LEAK-NAME")),
                 qPrintable(QStringLiteral("%1 leaks project content: %2").arg(QLatin1String(context), text)));
        QVERIFY2(!text.contains(QStringLiteral("LEAK-TEXT")),
                 qPrintable(QStringLiteral("%1 leaks project content: %2").arg(QLatin1String(context), text)));
    };

    // Project A: derived asset warning, save conflict.
    {
        const QString root = newRoot();
        auto descriptor = createProject(root);
        QVERIFY(descriptor.has_value());
        auto opened = openProject(root);
        QVERIFY(opened.has_value());
        ComicProject& project = opened->project;
        QVERIFY(addAssetPage(project, root, true));
        project.setName("LEAK-NAME-SENTINEL");
        expectNoError(saveProject(project));
        expectNoError(closeProject(project));

        QFile external(root + QStringLiteral("/project.json"));
        QVERIFY(external.open(QIODevice::Append));
        // Whitespace-only trailing bytes keep the JSON parseable while changing the
        // fingerprint, which is what triggers the save conflict below.
        external.write("\n\n\n");
        external.close();

        auto conflicted = openProject(root);
        QVERIFY(conflicted.has_value());
        ComicProject& conflictedProject = conflicted->project;
        conflictedProject.setName("Second");
        const auto conflictError = saveProject(conflictedProject);
        leak(conflictError.safeMessage, "save conflict safeMessage");
        leak(conflictError.diagnostic, "save conflict diagnostic");
        expectNoError(closeProject(conflictedProject));
    }

    // Project B: missing original asset and derived asset warnings on open.
    {
        const QString root = newRoot();
        QVERIFY(createProject(root).has_value());
        auto opened = openProject(root);
        QVERIFY(opened.has_value());
        ComicProject& project = opened->project;
        ComicPage page(PageId::generate(), std::string("assets/original/page_0001.png"), ImageSize{3, 2}, 0);
        TextRegion region(RegionId::generate(), RectGeometry(0, 0, 3, 2));
        domain::TextContent source;
        source.text = "LEAK-TEXT-SENTINEL";
        region.setSource(source);
        page.setClearLayerAsset(std::string("assets/derived/missing_layer.png"));
        QFile asset(root + QStringLiteral("/assets/original/page_0001.png"));
        QVERIFY(asset.open(QIODevice::WriteOnly));
        asset.write("asset bytes");
        asset.close();
        QVERIFY(project.addPage(page));
        expectNoError(saveProject(project));
        expectNoError(closeProject(project));

        auto reopened = openProject(root);
        QVERIFY(reopened.has_value());
        for (const auto& warning : reopened->warnings) {
            leak(warning.safeMessage, "derived asset warning safeMessage");
        }
        expectNoError(closeProject(reopened->project));
    }

    // Project C: missing asset and escape rejections on open. The invalid on-disk states are
    // written through the serializer directly, bypassing the save-time asset validation.
    {
        const QString root = newRoot();
        QVERIFY(createProject(root).has_value());
        auto opened = openProject(root);
        QVERIFY(opened.has_value());
        ComicProject& project = opened->project;
        QVERIFY(addAssetPage(project, root, true));
        expectNoError(saveProject(project));
        expectNoError(closeProject(project));
        QVERIFY(QFile::remove(root + QStringLiteral("/assets/original/page_0001.png")));
        const auto missingError = openProjectError(root);
        leak(missingError.safeMessage, "missing asset safeMessage");
        leak(missingError.diagnostic, "missing asset diagnostic");
        QCOMPARE(missingError.code, QStringLiteral("project.missing_asset"));

        QFile restored(root + QStringLiteral("/assets/original/page_0001.png"));
        QVERIFY(restored.open(QIODevice::WriteOnly));
        restored.write("asset bytes");
        restored.close();
        auto secondOpen = openProject(root);
        QVERIFY(secondOpen.has_value());
        ComicProject& escapeProject = secondOpen->project;
        ComicPage escape(PageId::generate(), std::string("../evil.png"), ImageSize{3, 2}, 0);
        QVERIFY(escapeProject.addPage(escape));
        expectNoError(closeProject(escapeProject));
        QFile projectJson(root + QStringLiteral("/project.json"));
        QVERIFY(projectJson.open(QIODevice::WriteOnly | QIODevice::Truncate));
        const QByteArray bytes = ProjectSerializer::serialize(escapeProject);
        QCOMPARE(projectJson.write(bytes), bytes.size());
        projectJson.close();
        const auto escapeError = openProjectError(root);
        leak(escapeError.safeMessage, "escape safeMessage");
        leak(escapeError.diagnostic, "escape diagnostic");
        QCOMPARE(escapeError.code, QStringLiteral("project.invalid_semantic"));
    }
}

void FileSystemProjectRepositoryTest::openNonExistentDirectoryReturnsNotFound() {
    const QString nonExistent = m_tempDir.path() + QStringLiteral("/non_existent_project_dir");
    const auto error = openProjectError(nonExistent);
    QCOMPARE(error.code, QStringLiteral("persistence.not_found"));
    QVERIFY(error.category == application::ErrorCategory::Persistence);
    QVERIFY(error.recovery == application::RecoveryAction::ChoosePath);
    QVERIFY(!error.safeMessage.isEmpty());
}

void FileSystemProjectRepositoryTest::openDirectoryWithoutProjectJsonReturnsNotAProject() {
    const QString root = newRoot();
    QVERIFY(QDir().mkpath(root));

    // Existing directory without project.json must return project.not_a_project
    const auto error = openProjectError(root);
    QCOMPARE(error.code, QStringLiteral("project.not_a_project"));
    QVERIFY(error.category == application::ErrorCategory::Project);
    QVERIFY(error.recovery == application::RecoveryAction::ChoosePath);
    QVERIFY(!error.safeMessage.isEmpty());
    // Acquired session lock must be released on failure
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/.mangatranslator.lock")));

    // When project.json exists as a directory rather than a regular file, still return project.not_a_project
    QVERIFY(QDir().mkpath(root + QStringLiteral("/project.json")));
    const auto dirError = openProjectError(root);
    QCOMPARE(dirError.code, QStringLiteral("project.not_a_project"));
    QVERIFY(dirError.category == application::ErrorCategory::Project);
    QVERIFY(dirError.recovery == application::RecoveryAction::ChoosePath);
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/.mangatranslator.lock")));
}

void FileSystemProjectRepositoryTest::openProjectJsonPermissionDeniedReturnsPermissionDenied() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    application::CloseProjectRequest request;
    request.projectId = descriptor->id;
    QVERIFY(await(m_repository->closeProject(request, {})).has_value());

    const QString projectJsonPath = root + QStringLiteral("/project.json");
#ifdef Q_OS_WIN
    const QStringList denyArgs{
        QDir::toNativeSeparators(projectJsonPath), QStringLiteral("/deny"), QStringLiteral("*S-1-1-0:(RD)")};
    if (QProcess::execute(QStringLiteral("icacls"), denyArgs) != 0) {
        QSKIP("modifying ACLs is not permitted in this environment");
    }
    const auto restore = [&projectJsonPath] {
        QProcess::execute(QStringLiteral("icacls"),
                          QStringList{QDir::toNativeSeparators(projectJsonPath), QStringLiteral("/reset")});
    };
#else
    const auto restore = [&projectJsonPath] {
        QFile::setPermissions(projectJsonPath,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
                                  QFileDevice::WriteUser);
    };
    if (!QFile::setPermissions(projectJsonPath, QFileDevice::Permissions{})) {
        QSKIP("permission manipulation is not supported on this platform");
    }
#endif

    const auto error = openProjectError(root);
    restore();

    QCOMPARE(error.code, QStringLiteral("persistence.permission_denied"));
    QVERIFY(error.category == application::ErrorCategory::Persistence);
    QVERIFY(error.recovery == application::RecoveryAction::ChoosePath);
    // Lock must be released on failure
    QVERIFY(!QFileInfo::exists(root + QStringLiteral("/.mangatranslator.lock")));

    // Project is readable and openable again after restoring access
    auto reopened = openProject(root);
    QVERIFY(reopened.has_value());
}

void FileSystemProjectRepositoryTest::openWriteProtectedProjectReturnsReadOnly() {
    const QString root = newRoot();
    auto descriptor = createProject(root);
    QVERIFY(descriptor.has_value());
    application::CloseProjectRequest request;
    request.projectId = descriptor->id;
    QVERIFY(await(m_repository->closeProject(request, {})).has_value());

#ifdef Q_OS_WIN
    // Deny write on directory so creating .mangatranslator.lock fails with PermissionError
    const QStringList denyArgs{
        QDir::toNativeSeparators(root), QStringLiteral("/deny"), QStringLiteral("*S-1-1-0:(WD)")};
    if (QProcess::execute(QStringLiteral("icacls"), denyArgs) != 0) {
        QSKIP("modifying ACLs is not permitted in this environment");
    }
    const auto restore = [&root] {
        QProcess::execute(QStringLiteral("icacls"),
                          QStringList{QDir::toNativeSeparators(root), QStringLiteral("/reset")});
    };
#else
    const auto restore = [&root] {
        QFile::setPermissions(root,
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
                                  QFileDevice::WriteUser | QFileDevice::ExeOwner | QFileDevice::ExeUser |
                                  QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup);
    };
    if (!QFile::setPermissions(root,
                               QFileDevice::ReadOwner | QFileDevice::ReadUser | QFileDevice::ExeOwner |
                                   QFileDevice::ExeUser | QFileDevice::ReadGroup | QFileDevice::ExeGroup)) {
        QSKIP("write-protecting a directory is not supported on this platform");
    }
#endif

    const auto error = openProjectError(root);
    restore();

    QCOMPARE(error.code, QStringLiteral("persistence.read_only"));
    QVERIFY(error.category == application::ErrorCategory::Persistence);
    QVERIFY(error.recovery == application::RecoveryAction::ChoosePath);

    // After restoring permissions, opening succeeds
    auto reopened = openProject(root);
    QVERIFY(reopened.has_value());
}

void FileSystemProjectRepositoryTest::projectLockErrorMapping() {
    const auto locked = toProjectLockError(QLockFile::LockFailedError);
    QCOMPARE(locked.code, QStringLiteral("persistence.locked"));
    QVERIFY(locked.category == application::ErrorCategory::Persistence);
    QVERIFY(locked.recovery == application::RecoveryAction::Retry);
    QVERIFY(locked.retryable);
    QVERIFY(locked.safeMessage.contains(QStringLiteral("another instance")));

    const auto readOnly = toProjectLockError(QLockFile::PermissionError);
    QCOMPARE(readOnly.code, QStringLiteral("persistence.read_only"));
    QVERIFY(readOnly.category == application::ErrorCategory::Persistence);
    QVERIFY(readOnly.recovery == application::RecoveryAction::ChoosePath);
    QVERIFY(!readOnly.retryable);
    QVERIFY(!readOnly.safeMessage.isEmpty());

    const auto unknown = toProjectLockError(QLockFile::UnknownError);
    QCOMPARE(unknown.code, QStringLiteral("persistence.io"));
    QVERIFY(unknown.category == application::ErrorCategory::Persistence);
    QVERIFY(unknown.recovery == application::RecoveryAction::Retry);
    QVERIFY(unknown.retryable);
    QVERIFY(!unknown.safeMessage.isEmpty());
    QVERIFY(unknown.diagnostic.contains(QStringLiteral("QLockFile::LockError")));
}

#include "moc_FileSystemProjectRepositoryTest.cpp"
