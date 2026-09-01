#include "IntegrationHarness.h"
#include "application/dto/ExportDto.h"
#include "application/dto/ProjectDto.h"
#include "application/usecase/apply/ApplyUseCases.h"
#include "application/usecase/export/ExportUseCases.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "domain/model/ComicProject.h"
#include "rendering/TextLayoutEngine.h"
#include "rendering/TextRasterizer.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/editor/ExportSnapshotBuilder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>

using namespace manga;
using namespace manga::test;
using namespace manga::application;
using namespace manga::ui::screen::editor;

class ManualOfflineScenarioTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void manualOfflineMainline();
    void reopenYieldsIdenticalDomainWithEmptyUndoHistory();
    void sourceImagesUnchangedAcrossMainline();
};

void ManualOfflineScenarioTest::manualOfflineMainline() {
    RealDataStack stack;
    const QString projectRoot = stack.projectRoot(QStringLiteral("e01_manual_offline_mainline"));

    // 1. Create project
    CreateProjectRequest createReq{
        .name = QStringLiteral("ManualMainline"),
        .path = projectRoot,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createFuture = CreateProjectUseCase(stack.repository.get()).execute(createReq, OperationContext{});
    QVERIFY(awaitFuture(createFuture));
    auto createRes = createFuture.result();
    QVERIFY(createRes.has_value());

    // Open project to get domain aggregate
    OpenProjectRequest openInitialReq{.path = projectRoot};
    auto openInitialFuture = OpenProjectUseCase(stack.repository.get()).execute(openInitialReq, OperationContext{});
    QVERIFY(awaitFuture(openInitialFuture));
    auto openInitialRes = openInitialFuture.result();
    QVERIFY(openInitialRes.has_value());
    domain::ComicProject project = openInitialRes.value().project;

    // Import page_001.png and folder containing page_002.png/page_003.png
    const QString fixturesDir = QStringLiteral(MANGA_TEST_FIXTURES_DIR);
    const QString page1Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_001.png"));
    const QString page2Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_002.png"));
    const QString page3Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_003.png"));
    QVERIFY(QFileInfo::exists(page1Path));
    QVERIFY(QFileInfo::exists(page2Path));
    QVERIFY(QFileInfo::exists(page3Path));

    QTemporaryDir importFolder;
    QVERIFY(QFile::copy(page2Path, QDir(importFolder.path()).filePath(QStringLiteral("page_002.png"))));
    QVERIFY(QFile::copy(page3Path, QDir(importFolder.path()).filePath(QStringLiteral("page_003.png"))));

    ImportImagesRequest importReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .filePaths = {page1Path, importFolder.path()},
    };
    auto importFuture = ImportImagesUseCase(stack.importer.get()).execute(importReq, OperationContext{});
    QVERIFY(awaitFuture(importFuture));
    auto importRes = importFuture.result();
    QVERIFY(importRes.has_value());
    QCOMPARE(importRes.value().importedPages.size(), 3);

    ApplyImportResultRequest applyReq{
        .projectId = project.id(),
        .result = importRes.value(),
        .sessionGeneration = 0,
        .currentSessionGeneration = 0,
    };
    auto applyRes = ApplyImportResultUseCase(&project).execute(applyReq);
    QVERIFY(applyRes.has_value());
    QCOMPARE(applyRes.value().size(), 3);

    AcknowledgeImportedAssetsRequest ackReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .batchId = importRes.value().batchId,
    };
    auto ackFuture = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackReq, OperationContext{});
    QVERIFY(awaitFuture(ackFuture));
    QVERIFY(ackFuture.result().has_value());

    SaveProjectRequest initialSaveReq{.project = project};
    auto initialSaveFuture = SaveProjectUseCase(stack.repository.get()).execute(initialSaveReq, OperationContext{});
    QVERIFY(awaitFuture(initialSaveFuture));
    auto initialSaveRes = initialSaveFuture.result();
    QVERIFY(initialSaveRes.has_value());
    QVERIFY(project.markCleanIfRevision(initialSaveRes.value().savedRevision));

    // 2. Open EditorViewModel session
    EditorViewModel viewModel(
        stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
    OpenedProjectSession session{
        .project = project,
        .projectRootPath = projectRoot,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    viewModel.openProjectSession(session);
    QVERIFY(viewModel.hasSession());
    QCOMPARE(viewModel.state().pages.size(), 3);

    const auto page0Id = viewModel.state().pages[0].id;
    const auto page1Id = viewModel.state().pages[1].id;
    const auto page2Id = viewModel.state().pages[2].id;

    // Fast page switching & verify generation
    QSignalSpy imageSpy(&viewModel, &EditorViewModel::pageImageReady);
    viewModel.selectPage(page1Id);
    viewModel.selectPage(page2Id);
    QVERIFY(imageSpy.wait(kAwaitTimeoutMs));
    const auto lastDelivery = imageSpy.takeLast().at(0).value<PageImageDelivery>();
    QCOMPARE(lastDelivery.pageId, page2Id);
    QVERIFY(!lastDelivery.image.isNull());

    // 3. Edit regions on page 0
    QSignalSpy page0ImageSpy(&viewModel, &EditorViewModel::pageImageReady);
    viewModel.selectPage(page0Id);
    if (viewModel.currentPageImage().isNull()) {
        QVERIFY(page0ImageSpy.wait(kAwaitTimeoutMs));
    }
    QCOMPARE(viewModel.state().currentPageId, page0Id);
    QVERIFY(!viewModel.currentPageImage().isNull());

    domain::RectGeometry initGeom{50.0, 50.0, 160.0, 100.0};
    viewModel.createRegion(initGeom);
    QCOMPARE(viewModel.state().regions.size(), 1);
    const auto regionId = viewModel.state().regions[0].id;

    viewModel.selectRegion(regionId, RegionSelectionMode::Replace);
    QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);

    domain::RectGeometry movedGeom{60.0, 70.0, 180.0, 120.0};
    viewModel.commitRegionGeometry(regionId, movedGeom, ui::canvas::EditKind::Move);
    QCOMPARE(viewModel.state().regions[0].geometry.x, 60.0);
    QCOMPARE(viewModel.state().regions[0].geometry.y, 70.0);

    viewModel.commitInspectorFieldEdit(page0Id, {regionId}, InspectorField::SourceText, QStringLiteral("テスト原文"));
    viewModel.commitInspectorFieldEdit(
        page0Id, {regionId}, InspectorField::ManualTranslation, QStringLiteral("Test Translation"));
    viewModel.sealTextEditSession();

    viewModel.commitInspectorFieldEdit(page0Id, {regionId}, InspectorField::FontSizePt, 18.0);
    viewModel.commitInspectorFieldEdit(page0Id, {regionId}, InspectorField::FontFamily, QStringLiteral("Arial"));

    // Two-stage clearing
    ClearRegionUseCase clearUseCase(stack.inpainter.get(), &stack.operationRegistry);
    ClearRequest clearReq{
        .projectId = project.id(),
        .pageId = page0Id,
        .regionId = regionId,
        .pageImage = viewModel.currentPageImage(),
        .region = movedGeom,
        .settings = domain::ClearSettings{.enabled = true},
    };
    OperationDescriptor clearDesc{
        .stage = OperationStage::Clear,
        .projectId = project.id(),
        .pageId = page0Id,
        .regionId = regionId,
        .displayTarget = QStringLiteral("Clear"),
        .cancellable = true,
    };
    OperationId clearOpId;
    auto clearFuture = clearUseCase.execute(clearReq, clearDesc, viewModel.projectSessionGeneration(), clearOpId);
    QVERIFY(awaitFuture(clearFuture));
    auto clearRes = clearFuture.result();
    QVERIFY(clearRes.has_value());

    PrepareClearResultUseCase prepareClearUseCase(viewModel.projectAggregate(), stack.derivedAssetStore.get());
    PrepareClearResultRequest prepReq{
        .projectId = project.id(),
        .pageId = page0Id,
        .expectedRegionId = regionId,
        .pageSize = QSize(viewModel.currentPageImage().width(), viewModel.currentPageImage().height()),
        .projectRoot = projectRoot,
        .result = clearRes.value(),
        .sessionGeneration = viewModel.projectSessionGeneration(),
        .currentSessionGeneration = viewModel.projectSessionGeneration(),
        .expectedRevision = viewModel.projectAggregate()->findPage(page0Id)->findRegion(regionId)->contentRevision(),
        .context = OperationContext{},
    };
    auto prepFuture = prepareClearUseCase.execute(prepReq);
    QVERIFY(awaitFuture(prepFuture));
    auto prepRes = prepFuture.result();
    QVERIFY(prepRes.has_value());

    ApplyPreparedClearChangeRequest applyClearReq{
        .projectId = project.id(),
        .pageId = page0Id,
        .expectedRegionId = regionId,
        .targetRef = prepRes.value().afterRef,
        .targetState = true,
        .sessionGeneration = viewModel.projectSessionGeneration(),
        .currentSessionGeneration = viewModel.projectSessionGeneration(),
        .expectedRevision = viewModel.projectAggregate()->findPage(page0Id)->findRegion(regionId)->contentRevision(),
    };
    auto applyClearRes = ApplyPreparedClearChangeUseCase(viewModel.projectAggregate()).execute(applyClearReq);
    QVERIFY(applyClearRes.has_value());

    // 4. Undo and Redo
    QVERIFY(viewModel.state().canUndo);
    viewModel.undo();
    QVERIFY(viewModel.state().canRedo);
    viewModel.redo();
    QVERIFY(viewModel.state().canUndo);
    QVERIFY(viewModel.state().saveState.isDirty);

    // 5. Save project
    QSignalSpy saveSpy(&viewModel, &EditorViewModel::saveCompleted);
    viewModel.saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!viewModel.state().saveState.isDirty);
    QVERIFY(!viewModel.projectAggregate()->isDirty());
    QVERIFY(viewModel.state().canUndo);
    QVERIFY(viewModel.undoStackCountForTest() > 0);
    const auto savedProjectSnapshot = *viewModel.projectAggregate();

    // 6. Close project & Reopen
    QSignalSpy closeSpy(&viewModel, &EditorViewModel::projectClosed);
    viewModel.requestCloseProject();
    QVERIFY(closeSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!viewModel.hasSession());
    QThreadPool::globalInstance()->waitForDone();

    OpenProjectRequest openReq{.path = projectRoot};
    auto openFuture = OpenProjectUseCase(stack.repository.get()).execute(openReq, OperationContext{});
    QVERIFY(awaitFuture(openFuture));
    auto openRes = openFuture.result();
    QVERIFY(openRes.has_value());
    const domain::ComicProject reopenedProject = openRes.value().project;
    QVERIFY(reopenedProject == savedProjectSnapshot);

    EditorViewModel reopenedViewModel(
        stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
    OpenedProjectSession reopenedSession{
        .project = reopenedProject,
        .projectRootPath = projectRoot,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    reopenedViewModel.openProjectSession(reopenedSession);
    QCOMPARE(reopenedViewModel.undoStackCountForTest(), 0);
    QVERIFY(!reopenedViewModel.state().canUndo);
    QVERIFY(!reopenedViewModel.state().canRedo);
    QVERIFY(!reopenedViewModel.state().saveState.isDirty);

    // 7. Export PNG and JPEG
    ExportSnapshotBuilder snapshotBuilder(
        reopenedViewModel.projectAggregate(), stack.pageImageLoader.get(), stack.derivedAssetStore.get());
    ExportSnapshotBuilder::Request snapReq{
        .projectRoot = projectRoot,
        .pageIds = {},
        .logicalDpi = 96.0,
    };
    auto snapFuture = snapshotBuilder.execute(snapReq, OperationContext{});
    QVERIFY(awaitFuture(snapFuture));
    auto snapRes = snapFuture.result();
    QVERIFY(snapRes.has_value());
    QCOMPARE(snapRes.value().size(), 3);

    QTemporaryDir exportOutDir;
    ExportPagesUseCase exportUseCase(stack.exporter.get(), reopenedViewModel.projectAggregate());

    // PNG Export
    ExportPagesRequest pngReq{
        .projectId = reopenedProject.id(),
        .scope = ExportScope::AllPages,
        .outputDir = exportOutDir.path(),
        .format = ExportFormat::Png,
        .pageSnapshots = snapRes.value(),
    };
    auto pngFuture = exportUseCase.execute(pngReq, OperationContext{});
    QVERIFY(awaitFuture(pngFuture));
    auto pngRes = pngFuture.result();
    QVERIFY(pngRes.has_value());
    QCOMPARE(pngRes.value().exported.size(), 3);
    QCOMPARE(pngRes.value().failures.size(), 0);
    for (const auto& exp : pngRes.value().exported) {
        QVERIFY(QFile::exists(exp.path));
        QVERIFY(QFileInfo(exp.path).size() > 0);
        QVERIFY(exp.path.endsWith(QStringLiteral(".png")));
    }

    // JPEG Export
    ExportPagesRequest jpegReq{
        .projectId = reopenedProject.id(),
        .scope = ExportScope::AllPages,
        .outputDir = exportOutDir.path(),
        .format = ExportFormat::Jpeg,
        .jpegQuality = 90,
        .pageSnapshots = snapRes.value(),
    };
    auto jpegFuture = exportUseCase.execute(jpegReq, OperationContext{});
    QVERIFY(awaitFuture(jpegFuture));
    auto jpegRes = jpegFuture.result();
    QVERIFY(jpegRes.has_value());
    QCOMPARE(jpegRes.value().exported.size(), 3);
    QCOMPARE(jpegRes.value().failures.size(), 0);
    for (const auto& exp : jpegRes.value().exported) {
        QVERIFY(QFile::exists(exp.path));
        QVERIFY(QFileInfo(exp.path).size() > 0);
        QVERIFY(exp.path.endsWith(QStringLiteral(".jpg")));
    }
}

void ManualOfflineScenarioTest::reopenYieldsIdenticalDomainWithEmptyUndoHistory() {
    RealDataStack stack;
    const QString projectRoot = stack.projectRoot(QStringLiteral("e01_reopen_identity"));

    CreateProjectRequest createReq{
        .name = QStringLiteral("ReopenIdentity"),
        .path = projectRoot,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createFuture = CreateProjectUseCase(stack.repository.get()).execute(createReq, OperationContext{});
    QVERIFY(awaitFuture(createFuture));
    auto createRes = createFuture.result();
    QVERIFY(createRes.has_value());

    OpenProjectRequest openInitialReq{.path = projectRoot};
    auto openInitialFuture = OpenProjectUseCase(stack.repository.get()).execute(openInitialReq, OperationContext{});
    QVERIFY(awaitFuture(openInitialFuture));
    auto openInitialRes = openInitialFuture.result();
    QVERIFY(openInitialRes.has_value());
    domain::ComicProject project = openInitialRes.value().project;

    const QString fixturesDir = QStringLiteral(MANGA_TEST_FIXTURES_DIR);
    const QString page1Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_001.png"));
    const QString page2Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_002.png"));

    ImportImagesRequest importReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .filePaths = {page1Path, page2Path},
    };
    auto importFuture = ImportImagesUseCase(stack.importer.get()).execute(importReq, OperationContext{});
    QVERIFY(awaitFuture(importFuture));
    auto importRes = importFuture.result();
    QVERIFY(importRes.has_value());

    ApplyImportResultRequest applyReq{
        .projectId = project.id(),
        .result = importRes.value(),
        .sessionGeneration = 0,
        .currentSessionGeneration = 0,
    };
    auto applyRes = ApplyImportResultUseCase(&project).execute(applyReq);
    QVERIFY(applyRes.has_value());

    AcknowledgeImportedAssetsRequest ackReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .batchId = importRes.value().batchId,
    };
    auto ackFuture = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackReq, OperationContext{});
    QVERIFY(awaitFuture(ackFuture));
    QVERIFY(ackFuture.result().has_value());

    SaveProjectRequest initialSaveReq{.project = project};
    auto initialSaveFuture = SaveProjectUseCase(stack.repository.get()).execute(initialSaveReq, OperationContext{});
    QVERIFY(awaitFuture(initialSaveFuture));
    auto initialSaveRes = initialSaveFuture.result();
    QVERIFY(initialSaveRes.has_value());
    QVERIFY(project.markCleanIfRevision(initialSaveRes.value().savedRevision));

    EditorViewModel viewModel(
        stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
    OpenedProjectSession session{
        .project = project,
        .projectRootPath = projectRoot,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    viewModel.openProjectSession(session);
    QVERIFY(viewModel.hasSession());

    const auto p0Id = viewModel.state().pages[0].id;
    const auto p1Id = viewModel.state().pages[1].id;

    // Add 2 regions on page 0
    viewModel.selectPage(p0Id);
    viewModel.createRegion(domain::RectGeometry{10.0, 10.0, 100.0, 80.0});
    viewModel.createRegion(domain::RectGeometry{120.0, 10.0, 100.0, 80.0});
    QCOMPARE(viewModel.state().regions.size(), 2);
    const auto r0Id = viewModel.state().regions[0].id;
    const auto r1Id = viewModel.state().regions[1].id;

    viewModel.commitInspectorFieldEdit(p0Id, {r0Id}, InspectorField::SourceText, QStringLiteral("セリフ1"));
    viewModel.commitInspectorFieldEdit(p0Id, {r0Id}, InspectorField::ManualTranslation, QStringLiteral("Line 1"));
    viewModel.commitInspectorFieldEdit(p0Id, {r1Id}, InspectorField::SourceText, QStringLiteral("セリフ2"));
    viewModel.commitInspectorFieldEdit(p0Id, {r1Id}, InspectorField::ManualTranslation, QStringLiteral("Line 2"));
    viewModel.sealTextEditSession();

    // Add 1 region on page 1
    viewModel.selectPage(p1Id);
    viewModel.createRegion(domain::RectGeometry{30.0, 40.0, 120.0, 90.0});
    QCOMPARE(viewModel.state().regions.size(), 1);
    const auto r2Id = viewModel.state().regions[0].id;
    viewModel.commitInspectorFieldEdit(p1Id, {r2Id}, InspectorField::SourceText, QStringLiteral("セリフ3"));
    viewModel.commitInspectorFieldEdit(p1Id, {r2Id}, InspectorField::ManualTranslation, QStringLiteral("Line 3"));
    viewModel.sealTextEditSession();

    // Save and capture expected snapshot
    QSignalSpy saveSpy(&viewModel, &EditorViewModel::saveCompleted);
    viewModel.saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!viewModel.state().saveState.isDirty);
    const domain::ComicProject expectedProject = *viewModel.projectAggregate();

    // Close
    QSignalSpy closeSpy(&viewModel, &EditorViewModel::projectClosed);
    viewModel.requestCloseProject();
    QVERIFY(closeSpy.wait(kAwaitTimeoutMs));
    QThreadPool::globalInstance()->waitForDone();

    // Reopen via OpenProjectUseCase
    OpenProjectRequest openReq{.path = projectRoot};
    auto openFuture = OpenProjectUseCase(stack.repository.get()).execute(openReq, OperationContext{});
    QVERIFY(awaitFuture(openFuture));
    auto openRes = openFuture.result();
    QVERIFY(openRes.has_value());
    const domain::ComicProject reopenedProject = openRes.value().project;

    // Assert domain equality
    QVERIFY(reopenedProject == expectedProject);

    // Open new session in ViewModel and verify empty undo history
    EditorViewModel reopenedVm(
        stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
    OpenedProjectSession reopenedSession{
        .project = reopenedProject,
        .projectRootPath = projectRoot,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    reopenedVm.openProjectSession(reopenedSession);
    QCOMPARE(reopenedVm.undoStackCountForTest(), 0);
    QVERIFY(!reopenedVm.state().canUndo);
    QVERIFY(!reopenedVm.state().canRedo);
    QVERIFY(!reopenedVm.state().saveState.isDirty);
}

void ManualOfflineScenarioTest::sourceImagesUnchangedAcrossMainline() {
    RealDataStack stack;
    const QString projectRoot = stack.projectRoot(QStringLiteral("e01_source_images_integrity"));

    CreateProjectRequest createReq{
        .name = QStringLiteral("SourceIntegrity"),
        .path = projectRoot,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createFuture = CreateProjectUseCase(stack.repository.get()).execute(createReq, OperationContext{});
    QVERIFY(awaitFuture(createFuture));
    auto createRes = createFuture.result();
    QVERIFY(createRes.has_value());

    OpenProjectRequest openInitialReq{.path = projectRoot};
    auto openInitialFuture = OpenProjectUseCase(stack.repository.get()).execute(openInitialReq, OperationContext{});
    QVERIFY(awaitFuture(openInitialFuture));
    auto openInitialRes = openInitialFuture.result();
    QVERIFY(openInitialRes.has_value());
    domain::ComicProject project = openInitialRes.value().project;

    const QString fixturesDir = QStringLiteral(MANGA_TEST_FIXTURES_DIR);
    const QString page1Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_001.png"));
    const QString page2Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_002.png"));
    const QString page3Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_003.png"));

    ImportImagesRequest importReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .filePaths = {page1Path, page2Path, page3Path},
    };
    auto importFuture = ImportImagesUseCase(stack.importer.get()).execute(importReq, OperationContext{});
    QVERIFY(awaitFuture(importFuture));
    auto importRes = importFuture.result();
    QVERIFY(importRes.has_value());

    ApplyImportResultRequest applyReq{
        .projectId = project.id(),
        .result = importRes.value(),
        .sessionGeneration = 0,
        .currentSessionGeneration = 0,
    };
    auto applyRes = ApplyImportResultUseCase(&project).execute(applyReq);
    QVERIFY(applyRes.has_value());

    AcknowledgeImportedAssetsRequest ackReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .batchId = importRes.value().batchId,
    };
    auto ackFuture = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackReq, OperationContext{});
    QVERIFY(awaitFuture(ackFuture));
    QVERIFY(ackFuture.result().has_value());

    // Compute initial hashes of assets/original
    const auto initialHashes = sha256Tree(projectRoot, QStringLiteral("assets/original"));
    QCOMPARE(initialHashes.size(), 3);

    // Run mainline editing operations
    SaveProjectRequest initialSaveReq{.project = project};
    auto initialSaveFuture = SaveProjectUseCase(stack.repository.get()).execute(initialSaveReq, OperationContext{});
    QVERIFY(awaitFuture(initialSaveFuture));
    auto initialSaveRes = initialSaveFuture.result();
    QVERIFY(initialSaveRes.has_value());
    QVERIFY(project.markCleanIfRevision(initialSaveRes.value().savedRevision));

    EditorViewModel viewModel(
        stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
    OpenedProjectSession session{
        .project = project,
        .projectRootPath = projectRoot,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    viewModel.openProjectSession(session);
    QVERIFY(viewModel.hasSession());

    const auto p0Id = viewModel.state().pages[0].id;
    QSignalSpy page0ImageSpy(&viewModel, &EditorViewModel::pageImageReady);
    viewModel.selectPage(p0Id);
    if (viewModel.currentPageImage().isNull()) {
        QVERIFY(page0ImageSpy.wait(kAwaitTimeoutMs));
    }

    viewModel.createRegion(domain::RectGeometry{50.0, 50.0, 160.0, 100.0});
    const auto r0Id = viewModel.state().regions[0].id;
    viewModel.commitInspectorFieldEdit(p0Id, {r0Id}, InspectorField::SourceText, QStringLiteral("テスト原文"));
    viewModel.commitInspectorFieldEdit(
        p0Id, {r0Id}, InspectorField::ManualTranslation, QStringLiteral("Test Translation"));
    viewModel.sealTextEditSession();

    // Two-stage clearing
    ClearRegionUseCase clearUseCase(stack.inpainter.get(), &stack.operationRegistry);
    ClearRequest clearReq{
        .projectId = project.id(),
        .pageId = p0Id,
        .regionId = r0Id,
        .pageImage = viewModel.currentPageImage(),
        .region = domain::RectGeometry{50.0, 50.0, 160.0, 100.0},
        .settings = domain::ClearSettings{.enabled = true},
    };
    OperationDescriptor clearDesc{
        .stage = OperationStage::Clear,
        .projectId = project.id(),
        .pageId = p0Id,
        .regionId = r0Id,
        .displayTarget = QStringLiteral("Clear"),
        .cancellable = true,
    };
    OperationId clearOpId;
    auto clearFuture = clearUseCase.execute(clearReq, clearDesc, viewModel.projectSessionGeneration(), clearOpId);
    QVERIFY(awaitFuture(clearFuture));
    auto clearRes = clearFuture.result();
    QVERIFY(clearRes.has_value());

    PrepareClearResultUseCase prepareClearUseCase(viewModel.projectAggregate(), stack.derivedAssetStore.get());
    PrepareClearResultRequest prepReq{
        .projectId = project.id(),
        .pageId = p0Id,
        .expectedRegionId = r0Id,
        .pageSize = QSize(viewModel.currentPageImage().width(), viewModel.currentPageImage().height()),
        .projectRoot = projectRoot,
        .result = clearRes.value(),
        .sessionGeneration = viewModel.projectSessionGeneration(),
        .currentSessionGeneration = viewModel.projectSessionGeneration(),
        .expectedRevision = viewModel.projectAggregate()->findPage(p0Id)->findRegion(r0Id)->contentRevision(),
        .context = OperationContext{},
    };
    auto prepFuture = prepareClearUseCase.execute(prepReq);
    QVERIFY(awaitFuture(prepFuture));
    auto prepRes = prepFuture.result();
    QVERIFY(prepRes.has_value());

    ApplyPreparedClearChangeRequest applyClearReq{
        .projectId = project.id(),
        .pageId = p0Id,
        .expectedRegionId = r0Id,
        .targetRef = prepRes.value().afterRef,
        .targetState = true,
        .sessionGeneration = viewModel.projectSessionGeneration(),
        .currentSessionGeneration = viewModel.projectSessionGeneration(),
        .expectedRevision = viewModel.projectAggregate()->findPage(p0Id)->findRegion(r0Id)->contentRevision(),
    };
    auto applyClearRes = ApplyPreparedClearChangeUseCase(viewModel.projectAggregate()).execute(applyClearReq);
    QVERIFY(applyClearRes.has_value());

    // Save and close
    QSignalSpy saveSpy(&viewModel, &EditorViewModel::saveCompleted);
    viewModel.saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));

    QSignalSpy closeSpy(&viewModel, &EditorViewModel::projectClosed);
    viewModel.requestCloseProject();
    QVERIFY(closeSpy.wait(kAwaitTimeoutMs));
    QThreadPool::globalInstance()->waitForDone();

    // Reopen and export
    OpenProjectRequest openReq{.path = projectRoot};
    auto openFuture = OpenProjectUseCase(stack.repository.get()).execute(openReq, OperationContext{});
    QVERIFY(awaitFuture(openFuture));
    auto openRes = openFuture.result();
    QVERIFY(openRes.has_value());
    domain::ComicProject reopenedProject = openRes.value().project;

    ExportSnapshotBuilder snapshotBuilder(&reopenedProject, stack.pageImageLoader.get(), stack.derivedAssetStore.get());
    ExportSnapshotBuilder::Request snapReq{
        .projectRoot = projectRoot,
        .pageIds = {},
        .logicalDpi = 96.0,
    };
    auto snapFuture = snapshotBuilder.execute(snapReq, OperationContext{});
    QVERIFY(awaitFuture(snapFuture));
    auto snapRes = snapFuture.result();
    QVERIFY(snapRes.has_value());

    QTemporaryDir exportOutDir;
    ExportPagesUseCase exportUseCase(stack.exporter.get(), &reopenedProject);
    ExportPagesRequest pngReq{
        .projectId = reopenedProject.id(),
        .scope = ExportScope::AllPages,
        .outputDir = exportOutDir.path(),
        .format = ExportFormat::Png,
        .pageSnapshots = snapRes.value(),
    };
    auto pngFuture = exportUseCase.execute(pngReq, OperationContext{});
    QVERIFY(awaitFuture(pngFuture));
    auto pngRes = pngFuture.result();
    QVERIFY(pngRes.has_value());

    // Compute after hashes and assert identical
    const auto finalHashes = sha256Tree(projectRoot, QStringLiteral("assets/original"));
    QCOMPARE(finalHashes, initialHashes);
}

QTEST_MAIN(ManualOfflineScenarioTest)
#include "e01_manual_offline_test.moc"
