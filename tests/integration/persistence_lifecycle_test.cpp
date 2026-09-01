#include "IntegrationHarness.h"
#include "application/dto/ProjectDto.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "domain/model/ComicProject.h"
#include "domain/model/LanguageCode.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <memory>

using namespace manga;
using namespace manga::test;
using namespace manga::application;
using namespace manga::ui::screen::editor;

namespace {

struct PersistenceFixture {
    RealDataStack stack;
    QString projectRoot;
    std::unique_ptr<EditorViewModel> viewModel;
    QVector<domain::PageId> pageIds;

    [[nodiscard]] bool init(const QString& scenarioName, int pageCount = 1) {
        projectRoot = stack.projectRoot(scenarioName);

        application::CreateProjectRequest createReq{
            .name = scenarioName,
            .path = projectRoot,
            .sourceLanguage = domain::languages::Japanese,
            .targetLanguage = domain::languages::English,
            .readingDirection = domain::ReadingDirection::RightToLeft,
        };
        auto createFuture = application::CreateProjectUseCase(stack.repository.get())
                                .execute(createReq, application::OperationContext{});
        if (!awaitFuture(createFuture) || !createFuture.result().has_value()) {
            return false;
        }

        application::OpenProjectRequest openReq{.path = projectRoot};
        auto openFuture =
            application::OpenProjectUseCase(stack.repository.get()).execute(openReq, application::OperationContext{});
        if (!awaitFuture(openFuture) || !openFuture.result().has_value()) {
            return false;
        }
        auto project = openFuture.result().value().project;

        QStringList paths;
        for (int i = 1; i <= pageCount; ++i) {
            const QString pagePath =
                QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_00%1.png").arg(i));
            if (!QFileInfo::exists(pagePath)) {
                return false;
            }
            paths.append(pagePath);
        }

        application::ImportImagesRequest importReq{
            .projectId = project.id(),
            .projectRoot = projectRoot,
            .filePaths = paths,
        };
        auto importFuture =
            application::ImportImagesUseCase(stack.importer.get()).execute(importReq, application::OperationContext{});
        if (!awaitFuture(importFuture) || !importFuture.result().has_value()) {
            return false;
        }

        application::ApplyImportResultRequest applyReq{
            .projectId = project.id(),
            .result = importFuture.result().value(),
            .sessionGeneration = 0,
            .currentSessionGeneration = 0,
        };
        auto applyRes = application::ApplyImportResultUseCase(&project).execute(applyReq);
        if (!applyRes.has_value() || applyRes.value().size() != pageCount) {
            return false;
        }

        application::AcknowledgeImportedAssetsRequest ackReq{
            .projectId = project.id(),
            .projectRoot = projectRoot,
            .batchId = importFuture.result().value().batchId,
        };
        auto ackFuture = application::AcknowledgeImportedAssetsUseCase(stack.importer.get())
                             .execute(ackReq, application::OperationContext{});
        if (!awaitFuture(ackFuture) || !ackFuture.result().has_value()) {
            return false;
        }

        application::SaveProjectRequest saveReq{.project = project};
        auto saveFuture =
            application::SaveProjectUseCase(stack.repository.get()).execute(saveReq, application::OperationContext{});
        if (!awaitFuture(saveFuture) || !saveFuture.result().has_value()) {
            return false;
        }
        if (!project.markCleanIfRevision(saveFuture.result().value().savedRevision)) {
            return false;
        }

        for (const auto& page : project.pages()) {
            pageIds.append(page.id());
        }
        if (pageIds.size() != pageCount) {
            return false;
        }

        viewModel = std::make_unique<EditorViewModel>(
            stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());

        application::OpenedProjectSession session{
            .project = std::move(project),
            .projectRootPath = projectRoot,
            .recoverySource = application::RecoverySource::Current,
            .warnings = {},
        };
        viewModel->openProjectSession(std::move(session));
        return viewModel->hasSession();
    }
};

[[nodiscard]] QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

} // namespace

class PersistenceLifecycleTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<ProjectCloseSummary>();
        qRegisterMetaType<application::ApplicationError>();
    }

    void saveRaceDuringTransactionKeepsDirty();
    void saveAsWritesCopyAndKeepsOriginalUntouched();
    void bakRecoveryRestoresLastValidProject();
    void corruptProjectJsonIsRejectedWithStableCode();
    void diskFullSaveFailureKeepsDirtyAndLastValidFile();
};

void PersistenceLifecycleTest::saveRaceDuringTransactionKeepsDirty() {
    PersistenceFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("save_race_project"), 1));

    QVERIFY(!fixture.viewModel->state().saveState.isDirty);
    QVERIFY(fixture.viewModel->undoStackIsCleanForTest());
    QVERIFY(fixture.viewModel->projectAggregate() != nullptr);
    QVERIFY(!fixture.viewModel->projectAggregate()->isDirty());

    // 1. Initial edit on page 0
    const domain::RectGeometry geom1{50.0, 50.0, 100.0, 50.0};
    fixture.viewModel->createRegion(geom1);
    QVERIFY(fixture.viewModel->state().saveState.isDirty);
    QVERIFY(!fixture.viewModel->undoStackIsCleanForTest());
    const quint64 revAfterFirstEdit = fixture.viewModel->currentProjectRevision();

    // 2. Set save transaction hook that triggers another edit while save transaction is in flight
    bool hookRan = false;
    fixture.stack.repository->setSaveTransactionHook([&]() {
        hookRan = true;
        QMetaObject::invokeMethod(
            fixture.viewModel.get(),
            [&]() {
                const domain::RectGeometry geom2{200.0, 200.0, 120.0, 60.0};
                fixture.viewModel->createRegion(geom2);
            },
            Qt::BlockingQueuedConnection);
    });

    // 3. Initiate save
    QSignalSpy saveSpy(fixture.viewModel.get(), &EditorViewModel::saveCompleted);
    fixture.viewModel->saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(hookRan);
    fixture.stack.repository->setSaveTransactionHook({});

    // 4. Verify save completion received older revision and project remains dirty
    const QString savedRevStr = saveSpy.first().at(0).toString();
    QCOMPARE(savedRevStr.toULongLong(), revAfterFirstEdit);
    QVERIFY(fixture.viewModel->currentProjectRevision() > revAfterFirstEdit);
    QVERIFY(fixture.viewModel->projectAggregate()->isDirty());
    QVERIFY(fixture.viewModel->state().saveState.isDirty);
    QVERIFY(!fixture.viewModel->undoStackIsCleanForTest());

    // 5. Subsequent save clears dirty and moves undo clean point
    saveSpy.clear();
    fixture.viewModel->saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!fixture.viewModel->projectAggregate()->isDirty());
    QVERIFY(!fixture.viewModel->state().saveState.isDirty);
    QVERIFY(fixture.viewModel->undoStackIsCleanForTest());

    fixture.viewModel->closeProjectSession();
}

void PersistenceLifecycleTest::saveAsWritesCopyAndKeepsOriginalUntouched() {
    PersistenceFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("save_as_original"), 2));

    // Make an edit and save so original root is complete
    const domain::RectGeometry geom{40.0, 40.0, 120.0, 70.0};
    fixture.viewModel->createRegion(geom);
    QSignalSpy saveSpy(fixture.viewModel.get(), &EditorViewModel::saveCompleted);
    fixture.viewModel->saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!fixture.viewModel->state().saveState.isDirty);

    const QString originalRoot = fixture.projectRoot;
    auto originalTreeBefore = sha256Tree(originalRoot);
    originalTreeBefore.remove(QStringLiteral(".mangatranslator.lock"));
    QVERIFY(!originalTreeBefore.isEmpty());
    const QByteArray originalJsonBytes = readFile(originalRoot + QStringLiteral("/project.json"));
    QVERIFY(!originalJsonBytes.isEmpty());

    // Save As to a new directory
    const QString newRoot = fixture.stack.projectRoot(QStringLiteral("save_as_target"));
    application::SaveAsProjectRequest saveAsReq{
        .project = *fixture.viewModel->projectAggregate(),
        .newPath = newRoot,
    };
    auto saveAsFuture = application::SaveProjectAsUseCase(fixture.stack.repository.get())
                            .execute(saveAsReq, application::OperationContext{});
    QVERIFY(awaitFuture(saveAsFuture));
    auto saveAsRes = saveAsFuture.result();
    QVERIFY(saveAsRes.has_value());
    QCOMPARE(saveAsRes.value().descriptor.path, QFileInfo(newRoot).canonicalFilePath());

    // Original directory remains bit-identical (and its lock was released upon switching to new root)
    QVERIFY(!QFileInfo::exists(originalRoot + QStringLiteral("/.mangatranslator.lock")));
    const auto originalTreeAfter = sha256Tree(originalRoot);
    QCOMPARE(originalTreeAfter, originalTreeBefore);
    const QByteArray originalJsonBytesAfter = readFile(originalRoot + QStringLiteral("/project.json"));
    QCOMPARE(originalJsonBytesAfter, originalJsonBytes);

    // New directory is a complete project
    QVERIFY(QFileInfo::exists(newRoot + QStringLiteral("/project.json")));
    QVERIFY(QFileInfo::exists(newRoot + QStringLiteral("/assets/original")));

    // The session has switched to new root with lease:
    // 1. Original root can be opened by another repository instance (its lock was released)
    data::ProjectIoCoordinator otherCoordinator;
    data::FileSystemProjectRepository otherRepository(&otherCoordinator);
    application::OpenProjectRequest openOriginalReq{.path = originalRoot};
    auto openOriginalFuture =
        application::OpenProjectUseCase(&otherRepository).execute(openOriginalReq, application::OperationContext{});
    QVERIFY(awaitFuture(openOriginalFuture));
    QVERIFY(openOriginalFuture.result().has_value());

    // 2. New root is locked by the active session in fixture.stack.repository
    application::OpenProjectRequest openNewReq{.path = newRoot};
    auto openNewLockedFuture =
        application::OpenProjectUseCase(&otherRepository).execute(openNewReq, application::OperationContext{});
    QVERIFY(awaitFuture(openNewLockedFuture));
    QVERIFY(!openNewLockedFuture.result().has_value());
    QCOMPARE(openNewLockedFuture.result().error().code, QStringLiteral("persistence.locked"));

    // 3. Modifying project and saving via repository writes to newRoot and leaves originalRoot untouched
    domain::ComicProject modifiedProject = *fixture.viewModel->projectAggregate();
    modifiedProject.setName("After Save As Modification");
    application::SaveProjectRequest saveReq{.project = modifiedProject};
    auto saveFuture = application::SaveProjectUseCase(fixture.stack.repository.get())
                          .execute(saveReq, application::OperationContext{});
    QVERIFY(awaitFuture(saveFuture));
    QVERIFY(saveFuture.result().has_value());

    // Verify new root has the update
    const QByteArray newJsonBytes = readFile(newRoot + QStringLiteral("/project.json"));
    QVERIFY(newJsonBytes.contains("After Save As Modification"));

    // Verify original root is still untouched
    const QByteArray originalJsonBytesFinal = readFile(originalRoot + QStringLiteral("/project.json"));
    QCOMPARE(originalJsonBytesFinal, originalJsonBytes);

    // Close sessions
    application::CloseProjectRequest closeOtherReq{.projectId = openOriginalFuture.result().value().project.id()};
    auto closeOtherFuture =
        application::CloseProjectUseCase(&otherRepository).execute(closeOtherReq, application::OperationContext{});
    QVERIFY(awaitFuture(closeOtherFuture));

    application::CloseProjectRequest closeMainReq{.projectId = modifiedProject.id()};
    auto closeMainFuture = application::CloseProjectUseCase(fixture.stack.repository.get())
                               .execute(closeMainReq, application::OperationContext{});
    QVERIFY(awaitFuture(closeMainFuture));
}

void PersistenceLifecycleTest::bakRecoveryRestoresLastValidProject() {
    RealDataStack stack;
    const QString projectRoot = stack.projectRoot(QStringLiteral("bak_recovery_project"));

    // 1. Create project
    application::CreateProjectRequest createReq{
        .name = QStringLiteral("First Save"),
        .path = projectRoot,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createFuture =
        application::CreateProjectUseCase(stack.repository.get()).execute(createReq, application::OperationContext{});
    QVERIFY(awaitFuture(createFuture));
    QVERIFY(createFuture.result().has_value());

    application::OpenProjectRequest openReq{.path = projectRoot};
    auto openFuture =
        application::OpenProjectUseCase(stack.repository.get()).execute(openReq, application::OperationContext{});
    QVERIFY(awaitFuture(openFuture));
    QVERIFY(openFuture.result().has_value());
    auto project = openFuture.result().value().project;

    // Import 1 page
    const QString pagePath =
        QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_001.png"));
    QVERIFY(QFileInfo::exists(pagePath));

    application::ImportImagesRequest importReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .filePaths = {pagePath},
    };
    auto importFuture =
        application::ImportImagesUseCase(stack.importer.get()).execute(importReq, application::OperationContext{});
    QVERIFY(awaitFuture(importFuture));
    QVERIFY(importFuture.result().has_value());

    application::ApplyImportResultRequest applyReq{
        .projectId = project.id(),
        .result = importFuture.result().value(),
        .sessionGeneration = 0,
        .currentSessionGeneration = 0,
    };
    auto applyRes = application::ApplyImportResultUseCase(&project).execute(applyReq);
    QVERIFY(applyRes.has_value());

    application::AcknowledgeImportedAssetsRequest ackReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .batchId = importFuture.result().value().batchId,
    };
    auto ackFuture = application::AcknowledgeImportedAssetsUseCase(stack.importer.get())
                         .execute(ackReq, application::OperationContext{});
    QVERIFY(awaitFuture(ackFuture));
    QVERIFY(ackFuture.result().has_value());

    // First save: "First Save"
    application::SaveProjectRequest saveReq1{.project = project};
    auto saveFuture1 =
        application::SaveProjectUseCase(stack.repository.get()).execute(saveReq1, application::OperationContext{});
    QVERIFY(awaitFuture(saveFuture1));
    QVERIFY(saveFuture1.result().has_value());
    QVERIFY(project.markCleanIfRevision(saveFuture1.result().value().savedRevision));

    // Second save: "Second Save" (rotates "First Save" into project.json.bak)
    project.setName("Second Save");
    application::SaveProjectRequest saveReq2{.project = project};
    auto saveFuture2 =
        application::SaveProjectUseCase(stack.repository.get()).execute(saveReq2, application::OperationContext{});
    QVERIFY(awaitFuture(saveFuture2));
    QVERIFY(saveFuture2.result().has_value());
    QVERIFY(project.markCleanIfRevision(saveFuture2.result().value().savedRevision));

    // Close project cleanly
    application::CloseProjectRequest closeReq{.projectId = project.id()};
    auto closeFuture =
        application::CloseProjectUseCase(stack.repository.get()).execute(closeReq, application::OperationContext{});
    QVERIFY(awaitFuture(closeFuture));
    QVERIFY(closeFuture.result().has_value());

    QVERIFY(QFileInfo::exists(projectRoot + QStringLiteral("/project.json.bak")));

    // 2. Corrupt current project.json
    QFile currentFile(projectRoot + QStringLiteral("/project.json"));
    QVERIFY(currentFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    currentFile.write("{ corrupt json content invalid 12345");
    currentFile.close();

    // 3. OpenProjectUseCase fails with persistence.recovery_failed and holds recovery lock
    auto openCorruptFuture =
        application::OpenProjectUseCase(stack.repository.get()).execute(openReq, application::OperationContext{});
    QVERIFY(awaitFuture(openCorruptFuture));
    auto openCorruptRes = openCorruptFuture.result();
    QVERIFY(!openCorruptRes.has_value());
    QCOMPARE(openCorruptRes.error().code, QStringLiteral("persistence.recovery_failed"));

    // 4. RecoverProjectUseCase recovers from backup
    auto recoverFuture =
        application::RecoverProjectUseCase(stack.repository.get()).execute(openReq, application::OperationContext{});
    QVERIFY(awaitFuture(recoverFuture));
    auto recoverRes = recoverFuture.result();
    QVERIFY(recoverRes.has_value());

    QCOMPARE(recoverRes.value().recoverySource, application::RecoverySource::Backup);
    QCOMPARE(QString::fromStdString(recoverRes.value().project.name()), QStringLiteral("First Save"));

    const bool hasRecoveryWarning = std::any_of(
        recoverRes.value().warnings.cbegin(),
        recoverRes.value().warnings.cend(),
        [](const application::ProjectWarning& w) { return w.code == QStringLiteral("project.recovered_from_backup"); });
    QVERIFY(hasRecoveryWarning);

    // 5. Subsequent save rewrites healthy project.json
    auto recoveredProject = recoverRes.value().project;
    application::SaveProjectRequest saveRecoveredReq{.project = recoveredProject};
    auto saveRecoveredFuture = application::SaveProjectUseCase(stack.repository.get())
                                   .execute(saveRecoveredReq, application::OperationContext{});
    QVERIFY(awaitFuture(saveRecoveredFuture));
    QVERIFY(saveRecoveredFuture.result().has_value());
    QVERIFY(recoveredProject.markCleanIfRevision(saveRecoveredFuture.result().value().savedRevision));

    // Close and reopen cleanly
    application::CloseProjectRequest closeRecoveredReq{.projectId = recoveredProject.id()};
    auto closeRecoveredFuture = application::CloseProjectUseCase(stack.repository.get())
                                    .execute(closeRecoveredReq, application::OperationContext{});
    QVERIFY(awaitFuture(closeRecoveredFuture));

    auto reopenFuture =
        application::OpenProjectUseCase(stack.repository.get()).execute(openReq, application::OperationContext{});
    QVERIFY(awaitFuture(reopenFuture));
    QVERIFY(reopenFuture.result().has_value());
    QCOMPARE(reopenFuture.result().value().recoverySource, application::RecoverySource::Current);
    QCOMPARE(QString::fromStdString(reopenFuture.result().value().project.name()), QStringLiteral("First Save"));

    application::CloseProjectRequest closeFinalReq{.projectId = reopenFuture.result().value().project.id()};
    auto closeFinalFuture = application::CloseProjectUseCase(stack.repository.get())
                                .execute(closeFinalReq, application::OperationContext{});
    QVERIFY(awaitFuture(closeFinalFuture));
}

void PersistenceLifecycleTest::corruptProjectJsonIsRejectedWithStableCode() {
    RealDataStack stack;
    const QString projectRoot = stack.projectRoot(QStringLiteral("corrupt_both_project"));

    // 1. Create and save twice so both files exist
    application::CreateProjectRequest createReq{
        .name = QStringLiteral("Corrupt Both"),
        .path = projectRoot,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createFuture =
        application::CreateProjectUseCase(stack.repository.get()).execute(createReq, application::OperationContext{});
    QVERIFY(awaitFuture(createFuture));
    QVERIFY(createFuture.result().has_value());

    application::OpenProjectRequest openReq{.path = projectRoot};
    auto openFuture =
        application::OpenProjectUseCase(stack.repository.get()).execute(openReq, application::OperationContext{});
    QVERIFY(awaitFuture(openFuture));
    auto project = openFuture.result().value().project;

    // Save twice
    application::SaveProjectRequest saveReq1{.project = project};
    auto saveFuture1 =
        application::SaveProjectUseCase(stack.repository.get()).execute(saveReq1, application::OperationContext{});
    QVERIFY(awaitFuture(saveFuture1));

    project.setName("Corrupt Both V2");
    application::SaveProjectRequest saveReq2{.project = project};
    auto saveFuture2 =
        application::SaveProjectUseCase(stack.repository.get()).execute(saveReq2, application::OperationContext{});
    QVERIFY(awaitFuture(saveFuture2));

    application::CloseProjectRequest closeReq{.projectId = project.id()};
    auto closeFuture =
        application::CloseProjectUseCase(stack.repository.get()).execute(closeReq, application::OperationContext{});
    QVERIFY(awaitFuture(closeFuture));

    // 2. Corrupt both project.json and project.json.bak
    QFile currentFile(projectRoot + QStringLiteral("/project.json"));
    QVERIFY(currentFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    currentFile.write("{ corrupted current json !");
    currentFile.close();

    QFile bakFile(projectRoot + QStringLiteral("/project.json.bak"));
    QVERIFY(bakFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    bakFile.write("{ corrupted backup json !");
    bakFile.close();

    const auto treeBefore = sha256Tree(projectRoot);

    // 3. OpenProjectUseCase fails
    auto openCorruptFuture =
        application::OpenProjectUseCase(stack.repository.get()).execute(openReq, application::OperationContext{});
    QVERIFY(awaitFuture(openCorruptFuture));
    auto openCorruptRes = openCorruptFuture.result();
    QVERIFY(!openCorruptRes.has_value());
    QCOMPARE(openCorruptRes.error().code, QStringLiteral("persistence.recovery_failed"));

    // 4. RecoverProjectUseCase also fails
    auto recoverCorruptFuture =
        application::RecoverProjectUseCase(stack.repository.get()).execute(openReq, application::OperationContext{});
    QVERIFY(awaitFuture(recoverCorruptFuture));
    auto recoverCorruptRes = recoverCorruptFuture.result();
    QVERIFY(!recoverCorruptRes.has_value());
    QCOMPARE(recoverCorruptRes.error().code, QStringLiteral("persistence.recovery_failed"));

    // 5. Abandon recovery session by path
    application::CloseProjectRequest abandonReq{.projectRoot = projectRoot};
    auto abandonFuture =
        application::CloseProjectUseCase(stack.repository.get()).execute(abandonReq, application::OperationContext{});
    QVERIFY(awaitFuture(abandonFuture));
    QVERIFY(abandonFuture.result().has_value());

    // 6. Directory content was not rewritten
    const auto treeAfter = sha256Tree(projectRoot);
    QCOMPARE(treeAfter, treeBefore);
}

void PersistenceLifecycleTest::diskFullSaveFailureKeepsDirtyAndLastValidFile() {
    PersistenceFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("disk_full_project"), 1));

    // Verify initial clean state
    QVERIFY(!fixture.viewModel->state().saveState.isDirty);
    QVERIFY(fixture.viewModel->undoStackIsCleanForTest());

    const QString projectJsonPath = fixture.projectRoot + QStringLiteral("/project.json");
    const QByteArray jsonBytesBefore = readFile(projectJsonPath);
    QVERIFY(!jsonBytesBefore.isEmpty());

    // 1. Make an edit
    const domain::RectGeometry geom{60.0, 60.0, 150.0, 80.0};
    fixture.viewModel->createRegion(geom);
    QVERIFY(fixture.viewModel->state().saveState.isDirty);
    QVERIFY(!fixture.viewModel->undoStackIsCleanForTest());

    // 2. Inject disk full simulation
    fixture.stack.repository->setDiskSpaceProbe([](const QString&) { return qint64(0); });

    // 3. Save project -> fails with persistence.disk_full
    QSignalSpy failedSpy(fixture.viewModel.get(), &EditorViewModel::saveFailed);
    fixture.viewModel->saveProject();
    QVERIFY(failedSpy.wait(kAwaitTimeoutMs));
    QCOMPARE(failedSpy.count(), 1);

    const auto error = failedSpy.first().at(0).value<application::ApplicationError>();
    QCOMPARE(error.code, QStringLiteral("persistence.disk_full"));
    QCOMPARE(error.category, application::ErrorCategory::Persistence);
    QCOMPARE(error.recovery, application::RecoveryAction::FreeDiskSpace);

    // 4. Project remains dirty and Undo clean point unchanged
    QVERIFY(fixture.viewModel->state().saveState.isDirty);
    QVERIFY(fixture.viewModel->projectAggregate()->isDirty());
    QVERIFY(!fixture.viewModel->undoStackIsCleanForTest());

    // 5. Existing project.json bytes remain unchanged on disk
    const QByteArray jsonBytesAfterFail = readFile(projectJsonPath);
    QCOMPARE(jsonBytesAfterFail, jsonBytesBefore);

    // 6. Free space (restore default probe) and retry save
    fixture.stack.repository->setDiskSpaceProbe({});

    QSignalSpy saveSpy(fixture.viewModel.get(), &EditorViewModel::saveCompleted);
    fixture.viewModel->saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QCOMPARE(saveSpy.count(), 1);

    // 7. Project is now clean and project.json updated
    QVERIFY(!fixture.viewModel->state().saveState.isDirty);
    QVERIFY(!fixture.viewModel->projectAggregate()->isDirty());
    QVERIFY(fixture.viewModel->undoStackIsCleanForTest());

    const QByteArray jsonBytesAfterSuccess = readFile(projectJsonPath);
    QVERIFY(jsonBytesAfterSuccess != jsonBytesBefore);

    fixture.viewModel->closeProjectSession();
}

QTEST_MAIN(PersistenceLifecycleTest)
#include "persistence_lifecycle_test.moc"
