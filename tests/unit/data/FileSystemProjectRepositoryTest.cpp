#include "FileSystemProjectRepositoryTest.h"

#include "data/project/FileSystemProjectRepository.h"
#include "data/project/ProjectIoCoordinator.h"
#include "domain/model/ComicPage.h"
#include "domain/model/ComicProject.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFuture>
#include <QProcess>
#include <QtTest/QtTest>

#include <algorithm>
#include <memory>
#include <optional>

using namespace manga;
using namespace manga::data;
using domain::ComicPage;
using domain::ComicProject;
using domain::ImageSize;
using domain::PageId;

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
    request.savedRevision = project.revision();
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
    request.savedRevision = project.revision();
    auto result = await(m_repository->saveProjectAs(std::move(request), {}));
    if (!result) {
        return result.error();
    }
    m_saveAsDescriptor = *result;
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
    expectNoError(saveProject(project));
    QVERIFY(project.markCleanIfRevision(revision));
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
    QVERIFY(addAssetPage(project, root, false));
    expectNoError(saveProject(project));
    expectNoError(closeProject(project));

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
    expectNoError(saveProject(project));
    expectNoError(closeProject(project));

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
    expectNoError(saveProject(project));
    expectNoError(closeProject(project));

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

#include "moc_FileSystemProjectRepositoryTest.cpp"
