#include "FakeCredentialStore.h"
#include "FakeOcrEngine.h"
#include "FakeTranslator.h"
#include "IntegrationHarness.h"
#include "app/Application.h"
#include "app/ApplicationContext.h"
#include "application/dto/ProjectDto.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "data/settings/QSettingsAppSettingsStore.h"
#include "data/settings/QSettingsRecentProjectsStore.h"
#include "domain/model/ComicProject.h"
#include "domain/model/LanguageCode.h"
#include "services/ocr/OcrModelValidator.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/window/MainWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QObject>
#include <QPointer>
#include <QPromise>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <memory>

using namespace manga;
using namespace manga::test;
using namespace manga::application;
using namespace manga::ui::screen::editor;

namespace {

class ControllablePageLoader final : public application::IPageImageLoader {
public:
    struct PendingRequest {
        application::LoadPageImageRequest request;
        application::OperationContext context;
        std::shared_ptr<QPromise<application::Result<QImage>>> promise;
    };
    QVector<PendingRequest> pending;

    QFuture<application::Result<QImage>> loadPageImage(application::LoadPageImageRequest request,
                                                       application::OperationContext context) override {
        auto promise = std::make_shared<QPromise<application::Result<QImage>>>();
        auto future = promise->future();
        pending.append({std::move(request), std::move(context), std::move(promise)});
        return future;
    }

    void resolve(int index, const QImage& image) {
        if (index < 0 || index >= pending.size()) {
            return;
        }
        auto item = pending[index];
        item.promise->start();
        if (item.context.stopToken.stop_requested()) {
            item.promise->addResult(std::unexpected(application::makeApplicationError(
                application::ErrorCategory::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Cancelled"))));
        } else {
            item.promise->addResult(image);
        }
        item.promise->finish();
    }
};

struct SessionLifecycleFixture {
    RealDataStack stack;
    QString projectRoot;
    domain::ComicProject project{"SessionLifecycleFixture"};
    std::unique_ptr<manga::application::test::FakeOcrEngine> ocrEngine;
    std::unique_ptr<manga::application::test::FakeTranslator> translator;
    std::unique_ptr<EditorViewModel> viewModel;
    std::unique_ptr<EditorProcessingController> controller;
    QVector<domain::PageId> pageIds;

    [[nodiscard]] bool init(const QString& scenarioName, int pageCount = 2) {
        projectRoot = stack.projectRoot(scenarioName);
        ocrEngine = std::make_unique<manga::application::test::FakeOcrEngine>();
        translator = std::make_unique<manga::application::test::FakeTranslator>();

        CreateProjectRequest createReq{
            .name = scenarioName,
            .path = projectRoot,
            .sourceLanguage = domain::languages::Japanese,
            .targetLanguage = domain::languages::English,
            .readingDirection = domain::ReadingDirection::RightToLeft,
        };
        auto createFuture = CreateProjectUseCase(stack.repository.get()).execute(createReq, OperationContext{});
        if (!awaitFuture(createFuture) || !createFuture.result().has_value()) {
            return false;
        }

        OpenProjectRequest openReq{.path = projectRoot};
        auto openFuture = OpenProjectUseCase(stack.repository.get()).execute(openReq, OperationContext{});
        if (!awaitFuture(openFuture) || !openFuture.result().has_value()) {
            return false;
        }
        project = openFuture.result().value().project;

        QStringList pagePaths;
        for (int i = 1; i <= pageCount; ++i) {
            const QString p =
                QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_00%1.png").arg(i));
            if (!QFileInfo::exists(p)) {
                return false;
            }
            pagePaths.append(p);
        }

        ImportImagesRequest importReq{
            .projectId = project.id(),
            .projectRoot = projectRoot,
            .filePaths = pagePaths,
        };
        auto importFuture = ImportImagesUseCase(stack.importer.get()).execute(importReq, OperationContext{});
        if (!awaitFuture(importFuture) || !importFuture.result().has_value()) {
            return false;
        }

        ApplyImportResultRequest applyReq{
            .projectId = project.id(),
            .result = importFuture.result().value(),
            .sessionGeneration = 0,
            .currentSessionGeneration = 0,
        };
        auto applyRes = ApplyImportResultUseCase(&project).execute(applyReq);
        if (!applyRes.has_value() || applyRes.value().size() != pageCount) {
            return false;
        }

        AcknowledgeImportedAssetsRequest ackReq{
            .projectId = project.id(),
            .projectRoot = projectRoot,
            .batchId = importFuture.result().value().batchId,
        };
        auto ackFuture = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackReq, OperationContext{});
        if (!awaitFuture(ackFuture) || !ackFuture.result().has_value()) {
            return false;
        }

        SaveProjectRequest saveReq{.project = project};
        auto saveFuture = SaveProjectUseCase(stack.repository.get()).execute(saveReq, OperationContext{});
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
        viewModel->setExportDependencies(stack.exporter.get(), stack.derivedAssetStore.get());

        OpenedProjectSession session{
            .project = project,
            .projectRootPath = projectRoot,
            .recoverySource = RecoverySource::Current,
            .warnings = {},
        };
        viewModel->openProjectSession(std::move(session));
        if (!viewModel->hasSession()) {
            return false;
        }

        controller = std::make_unique<EditorProcessingController>(viewModel.get(),
                                                                  ocrEngine.get(),
                                                                  translator.get(),
                                                                  stack.inpainter.get(),
                                                                  stack.layouter.get(),
                                                                  stack.derivedAssetStore.get(),
                                                                  &stack.operationRegistry);
        return true;
    }
};

} // namespace

class SessionLifecycleTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<ProjectCloseSummary>();
        qRegisterMetaType<application::ApplicationError>();
        qRegisterMetaType<PageImageDelivery>();
        qRegisterMetaType<application::OpenedProjectSession>();
    }

    void fastPageSwitchDiscardsLateViewResults();
    void projectBoundTaskSurvivesPageSwitch();
    void projectSwitchRejectsLateCompletions();
    void closeCancelDoesNotDestroySession();
    void confirmedCloseRunsTeardownInOrder();
    void applicationExitRunsSameProtocolAsWindowClose();
};

void SessionLifecycleTest::fastPageSwitchDiscardsLateViewResults() {
    RealDataStack stack;
    const QString projectRoot = stack.projectRoot(QStringLiteral("fast_switch_project"));

    CreateProjectRequest createReq{
        .name = QStringLiteral("fast_switch"),
        .path = projectRoot,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createFuture = CreateProjectUseCase(stack.repository.get()).execute(createReq, OperationContext{});
    QVERIFY(awaitFuture(createFuture));
    QVERIFY(createFuture.result().has_value());

    OpenProjectRequest openReq{.path = projectRoot};
    auto openFuture = OpenProjectUseCase(stack.repository.get()).execute(openReq, OperationContext{});
    QVERIFY(awaitFuture(openFuture));
    QVERIFY(openFuture.result().has_value());
    auto project = openFuture.result().value().project;

    const QString p1 = QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_001.png"));
    const QString p2 = QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_002.png"));
    QVERIFY(QFileInfo::exists(p1) && QFileInfo::exists(p2));

    ImportImagesRequest importReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .filePaths = {p1, p2},
    };
    auto importFuture = ImportImagesUseCase(stack.importer.get()).execute(importReq, OperationContext{});
    QVERIFY(awaitFuture(importFuture));
    QVERIFY(importFuture.result().has_value());

    ApplyImportResultRequest applyReq{
        .projectId = project.id(),
        .result = importFuture.result().value(),
        .sessionGeneration = 0,
        .currentSessionGeneration = 0,
    };
    auto applyRes = ApplyImportResultUseCase(&project).execute(applyReq);
    QVERIFY(applyRes.has_value() && applyRes.value().size() == 2);

    AcknowledgeImportedAssetsRequest ackReq{
        .projectId = project.id(),
        .projectRoot = projectRoot,
        .batchId = importFuture.result().value().batchId,
    };
    auto ackFuture = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackReq, OperationContext{});
    QVERIFY(awaitFuture(ackFuture));
    QVERIFY(ackFuture.result().has_value());

    SaveProjectRequest saveReq{.project = project};
    auto saveFuture = SaveProjectUseCase(stack.repository.get()).execute(saveReq, OperationContext{});
    QVERIFY(awaitFuture(saveFuture));
    QVERIFY(saveFuture.result().has_value());
    QVERIFY(project.markCleanIfRevision(saveFuture.result().value().savedRevision));

    const domain::PageId pageA = project.pages().at(0).id();
    const domain::PageId pageB = project.pages().at(1).id();

    ControllablePageLoader controllableLoader;
    auto viewModel = std::make_unique<EditorViewModel>(
        stack.thumbnailStore.get(), &controllableLoader, &stack.operationRegistry, stack.repository.get());

    OpenedProjectSession session{
        .project = project,
        .projectRootPath = projectRoot,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    viewModel->openProjectSession(std::move(session));
    QVERIFY(viewModel->hasSession());

    // openProjectSession calls selectPage(pageA), so pending[0] is for Page A
    QCOMPARE(controllableLoader.pending.size(), 1);
    QCOMPARE(controllableLoader.pending[0].request.pageId, pageA);

    // Rapid switch to Page B and back to Page A: A -> B -> A
    viewModel->selectPage(pageB);
    QCOMPARE(controllableLoader.pending.size(), 2);
    QCOMPARE(controllableLoader.pending[1].request.pageId, pageB);

    viewModel->selectPage(pageA);
    QCOMPARE(controllableLoader.pending.size(), 3);
    QCOMPARE(controllableLoader.pending[2].request.pageId, pageA);

    QSignalSpy imageReadySpy(viewModel.get(), &EditorViewModel::pageImageReady);

    // Create 3 distinct images
    QImage imageA_old(100, 100, QImage::Format_RGB32);
    imageA_old.fill(Qt::red);
    QImage imageB(200, 200, QImage::Format_RGB32);
    imageB.fill(Qt::blue);
    QImage imageA_final(300, 300, QImage::Format_RGB32);
    imageA_final.fill(Qt::green);

    // 1. Resolve pending[0] (old Page A request)
    controllableLoader.resolve(0, imageA_old);
    QCoreApplication::processEvents();
    QCOMPARE(imageReadySpy.count(), 0);
    QVERIFY(viewModel->currentPageImage().isNull());

    // 2. Resolve pending[1] (Page B request)
    controllableLoader.resolve(1, imageB);
    QCoreApplication::processEvents();
    QCOMPARE(imageReadySpy.count(), 0);
    QVERIFY(viewModel->currentPageImage().isNull());

    // 3. Resolve pending[2] (final Page A request)
    controllableLoader.resolve(2, imageA_final);
    QCoreApplication::processEvents();
    QCOMPARE(imageReadySpy.count(), 1);

    auto delivery = imageReadySpy.takeFirst().at(0).value<PageImageDelivery>();
    QCOMPARE(delivery.pageId, pageA);
    QCOMPARE(delivery.image.size(), imageA_final.size());
    QCOMPARE(viewModel->currentPageImage().size(), imageA_final.size());
    QCOMPARE(viewModel->state().currentPageId, pageA);
    QVERIFY(!viewModel->state().isPageLoading);

    viewModel->closeProjectSession();
}

void SessionLifecycleTest::projectBoundTaskSurvivesPageSwitch() {
    SessionLifecycleFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("page_switch_ocr"), 2));

    const auto pageA = fixture.pageIds[0];
    const auto pageB = fixture.pageIds[1];

    // Create region on Page A
    fixture.viewModel->selectPage(pageA);
    const domain::RectGeometry geomA{50.0, 50.0, 120.0, 60.0};
    fixture.viewModel->createRegion(geomA);
    QVERIFY(!fixture.viewModel->state().regions.isEmpty());
    const domain::RegionId rA = fixture.viewModel->state().regions.first().id;

    // Create region on Page B
    fixture.viewModel->selectPage(pageB);
    const domain::RectGeometry geomB{60.0, 60.0, 140.0, 70.0};
    fixture.viewModel->createRegion(geomB);
    QVERIFY(!fixture.viewModel->state().regions.isEmpty());
    const domain::RegionId rB = fixture.viewModel->state().regions.first().id;

    // Switch back to Page A
    fixture.viewModel->selectPage(pageA);
    QCOMPARE(fixture.viewModel->state().currentPageId, pageA);

    // Setup deferred OCR on Page A
    fixture.ocrEngine->deferred = true;
    fixture.ocrEngine->callCount = 0;
    fixture.controller->recognizeRegion(pageA, rA);
    QCOMPARE(fixture.ocrEngine->callCount, 1);
    QVERIFY(fixture.controller->hasActiveOperation(pageA));

    // Switch to Page B while task is in flight
    fixture.viewModel->selectPage(pageB);
    QCOMPARE(fixture.viewModel->state().currentPageId, pageB);

    // Verify task on Page A is NOT cancelled and remains active
    QVERIFY(fixture.controller->hasActiveOperation(pageA));

    // Complete OCR for Page A
    fixture.ocrEngine->nextResponse.text = QStringLiteral("Recognized Text Page A");
    QSignalSpy finishSpy(fixture.controller.get(), &EditorProcessingController::processingFinished);
    QSignalSpy propSpy(fixture.controller.get(), &EditorProcessingController::proposalReady);
    fixture.ocrEngine->complete();

    QVERIFY(finishSpy.wait(kAwaitTimeoutMs));
    QCOMPARE(finishSpy.count(), 1);
    QCOMPARE(finishSpy.first().at(0).value<domain::PageId>(), pageA);
    QCOMPARE(finishSpy.first().at(1).toBool(), true);
    QCOMPARE(propSpy.count(), 1);
    QCOMPARE(propSpy.first().at(0).value<domain::PageId>(), pageA);

    // Verify proposal is bound to Page A and adoptable
    QVERIFY(fixture.controller->hasProposal(pageA));
    QVERIFY(!fixture.controller->hasProposal(pageB));
    const auto* prop = fixture.controller->proposal(pageA);
    QVERIFY(prop != nullptr);
    QCOMPARE(prop->pageId, pageA);

    // Adopt proposal for Page A (we are currently on Page B)
    QSignalSpy adoptSpy(fixture.controller.get(), &EditorProcessingController::proposalAdopted);
    fixture.controller->adoptProposal(pageA);
    QCOMPARE(adoptSpy.count(), 1);
    QCOMPARE(adoptSpy.first().at(0).value<domain::PageId>(), pageA);

    // Adoption automatically selects Page A and applies text
    QCOMPARE(fixture.viewModel->state().currentPageId, pageA);
    const auto* pageAgg = fixture.viewModel->projectAggregate()->findPage(pageA);
    QVERIFY(pageAgg != nullptr);
    const auto* regionAgg = pageAgg->findRegion(rA);
    QVERIFY(regionAgg != nullptr);
    QCOMPARE(QString::fromStdString(regionAgg->source().text), QStringLiteral("Recognized Text Page A"));

    // Undo is available and functions correctly
    QVERIFY(fixture.viewModel->state().canUndo);
    fixture.viewModel->undo();
    const auto* rAfterUndo = fixture.viewModel->projectAggregate()->findPage(pageA)->findRegion(rA);
    QVERIFY(rAfterUndo->source().text != "Recognized Text Page A");

    fixture.viewModel->closeProjectSession();
}

void SessionLifecycleTest::projectSwitchRejectsLateCompletions() {
    RealDataStack stack;

    // Create P1
    const QString p1Root = stack.projectRoot(QStringLiteral("project_p1"));
    CreateProjectRequest createP1Req{
        .name = QStringLiteral("Project 1"),
        .path = p1Root,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createP1Fut = CreateProjectUseCase(stack.repository.get()).execute(createP1Req, OperationContext{});
    QVERIFY(awaitFuture(createP1Fut) && createP1Fut.result().has_value());
    OpenProjectRequest openP1Req{.path = p1Root};
    auto openP1Fut = OpenProjectUseCase(stack.repository.get()).execute(openP1Req, OperationContext{});
    QVERIFY(awaitFuture(openP1Fut) && openP1Fut.result().has_value());
    auto p1 = openP1Fut.result().value().project;
    const QString img1 = QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_001.png"));
    ImportImagesRequest impP1Req{.projectId = p1.id(), .projectRoot = p1Root, .filePaths = {img1}};
    auto impP1Fut = ImportImagesUseCase(stack.importer.get()).execute(impP1Req, OperationContext{});
    QVERIFY(awaitFuture(impP1Fut) && impP1Fut.result().has_value());
    ApplyImportResultRequest appP1Req{.projectId = p1.id(), .result = impP1Fut.result().value()};
    auto appP1Res = ApplyImportResultUseCase(&p1).execute(appP1Req);
    QVERIFY(appP1Res.has_value());
    AcknowledgeImportedAssetsRequest ackP1Req{
        .projectId = p1.id(), .projectRoot = p1Root, .batchId = impP1Fut.result().value().batchId};
    auto ackP1Fut = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackP1Req, OperationContext{});
    QVERIFY(awaitFuture(ackP1Fut) && ackP1Fut.result().has_value());
    SaveProjectRequest saveP1Req{.project = p1};
    auto saveP1Fut = SaveProjectUseCase(stack.repository.get()).execute(saveP1Req, OperationContext{});
    QVERIFY(awaitFuture(saveP1Fut) && saveP1Fut.result().has_value());
    QVERIFY(p1.markCleanIfRevision(saveP1Fut.result().value().savedRevision));
    // Create P2
    const QString p2Root = stack.projectRoot(QStringLiteral("project_p2"));
    CreateProjectRequest createP2Req{
        .name = QStringLiteral("Project 2"),
        .path = p2Root,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createP2Fut = CreateProjectUseCase(stack.repository.get()).execute(createP2Req, OperationContext{});
    QVERIFY(awaitFuture(createP2Fut) && createP2Fut.result().has_value());
    OpenProjectRequest openP2Req{.path = p2Root};
    auto openP2Fut = OpenProjectUseCase(stack.repository.get()).execute(openP2Req, OperationContext{});
    QVERIFY(awaitFuture(openP2Fut) && openP2Fut.result().has_value());
    auto p2 = openP2Fut.result().value().project;
    const QString img2 = QDir(QStringLiteral(MANGA_TEST_FIXTURES_DIR)).filePath(QStringLiteral("pages/page_002.png"));
    ImportImagesRequest impP2Req{.projectId = p2.id(), .projectRoot = p2Root, .filePaths = {img2}};
    auto impP2Fut = ImportImagesUseCase(stack.importer.get()).execute(impP2Req, OperationContext{});
    QVERIFY(awaitFuture(impP2Fut) && impP2Fut.result().has_value());
    ApplyImportResultRequest appP2Req{.projectId = p2.id(), .result = impP2Fut.result().value()};
    auto appP2Res = ApplyImportResultUseCase(&p2).execute(appP2Req);
    QVERIFY(appP2Res.has_value());
    AcknowledgeImportedAssetsRequest ackP2Req{
        .projectId = p2.id(), .projectRoot = p2Root, .batchId = impP2Fut.result().value().batchId};
    auto ackP2Fut = AcknowledgeImportedAssetsUseCase(stack.importer.get()).execute(ackP2Req, OperationContext{});
    QVERIFY(awaitFuture(ackP2Fut) && ackP2Fut.result().has_value());
    SaveProjectRequest saveP2Req{.project = p2};
    auto saveP2Fut = SaveProjectUseCase(stack.repository.get()).execute(saveP2Req, OperationContext{});
    QVERIFY(awaitFuture(saveP2Fut) && saveP2Fut.result().has_value());
    QVERIFY(p2.markCleanIfRevision(saveP2Fut.result().value().savedRevision));
    auto ocrEngine = std::make_unique<manga::application::test::FakeOcrEngine>();
    auto viewModel = std::make_unique<EditorViewModel>(
        stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());
    auto controller = std::make_unique<EditorProcessingController>(viewModel.get(),
                                                                   ocrEngine.get(),
                                                                   nullptr,
                                                                   stack.inpainter.get(),
                                                                   stack.layouter.get(),
                                                                   stack.derivedAssetStore.get(),
                                                                   &stack.operationRegistry);

    // 1. Open P1
    OpenedProjectSession sessionP1{
        .project = p1,
        .projectRootPath = p1Root,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    viewModel->openProjectSession(std::move(sessionP1));
    QVERIFY(viewModel->hasSession());
    const auto p1PageId = viewModel->state().pages.first().id;
    viewModel->selectPage(p1PageId);

    const domain::RectGeometry geom1{50.0, 50.0, 100.0, 50.0};
    viewModel->createRegion(geom1);
    const domain::RegionId p1RegionId = viewModel->state().regions.first().id;

    // Start deferred OCR on P1
    ocrEngine->deferred = true;
    controller->recognizeRegion(p1PageId, p1RegionId);
    QCOMPARE(ocrEngine->callCount, 1);
    const quint64 p1SessionGen = viewModel->projectSessionGeneration();

    // 2. Close P1 and Open P2
    QSignalSpy projectClosedSpy(viewModel.get(), &EditorViewModel::projectClosed);
    viewModel->closeProjectSession();

    OpenedProjectSession sessionP2{
        .project = p2,
        .projectRootPath = p2Root,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    viewModel->openProjectSession(std::move(sessionP2));
    QVERIFY(projectClosedSpy.wait(kAwaitTimeoutMs));
    QVERIFY(viewModel->hasSession());
    QCOMPARE(viewModel->projectAggregate()->id(), p2.id());
    const auto p2PageId = viewModel->state().pages.first().id;
    viewModel->selectPage(p2PageId);

    const domain::RectGeometry geom2{80.0, 80.0, 120.0, 60.0};
    viewModel->createRegion(geom2);
    const domain::RegionId p2RegionId = viewModel->state().regions.first().id;
    const quint64 p2SessionGen = viewModel->projectSessionGeneration();
    QVERIFY(p2SessionGen > p1SessionGen);

    const int p2UndoCountBefore = viewModel->undoStackCountForTest();
    QCOMPARE(p2UndoCountBefore, 1);
    const auto* p2RegionBefore = viewModel->projectAggregate()->findPage(p2PageId)->findRegion(p2RegionId);
    const std::string p2TextBefore = p2RegionBefore->source().text;

    // 3. Complete P1's deferred OCR task
    ocrEngine->nextResponse.text = QStringLiteral("Stale P1 OCR Completion");
    QSignalSpy propSpy(controller.get(), &EditorProcessingController::proposalReady);
    QSignalSpy finishSpy(controller.get(), &EditorProcessingController::processingFinished);
    ocrEngine->complete();
    QCoreApplication::processEvents();

    // 4. Verify P1's completion was rejected with stale generation and silently discarded
    QCOMPARE(propSpy.count(), 0);
    QCOMPARE(finishSpy.count(), 0);
    QVERIFY(!controller->hasProposal(p1PageId));
    QVERIFY(!controller->hasProposal(p2PageId));

    // P2 domain aggregate and undo stack are untouched
    const auto* p2RegionAfter = viewModel->projectAggregate()->findPage(p2PageId)->findRegion(p2RegionId);
    QCOMPARE(p2RegionAfter->source().text, p2TextBefore);
    QCOMPARE(viewModel->undoStackCountForTest(), p2UndoCountBefore);

    viewModel->closeProjectSession();
}

void SessionLifecycleTest::closeCancelDoesNotDestroySession() {
    SessionLifecycleFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("close_cancel_project"), 1));

    const auto pageId = fixture.pageIds[0];
    fixture.viewModel->selectPage(pageId);

    // Make an edit so session is dirty
    const domain::RectGeometry geom1{50.0, 50.0, 100.0, 50.0};
    fixture.viewModel->createRegion(geom1);
    QVERIFY(fixture.viewModel->state().saveState.isDirty);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), 1);
    const auto regionId = fixture.viewModel->state().regions.first().id;

    // Start in-flight deferred task
    fixture.ocrEngine->deferred = true;
    fixture.controller->recognizeRegion(pageId, regionId);
    QVERIFY(fixture.controller->hasActiveOperation(pageId));

    // Request close
    QSignalSpy closeDecisionSpy(fixture.viewModel.get(), &EditorViewModel::closeDecisionRequested);
    QSignalSpy projectClosedSpy(fixture.viewModel.get(), &EditorViewModel::projectClosed);
    fixture.viewModel->requestCloseProject();

    QCOMPARE(closeDecisionSpy.count(), 1);
    QCOMPARE(projectClosedSpy.count(), 0);
    const auto summary = closeDecisionSpy.first().at(0).value<ProjectCloseSummary>();
    QCOMPARE(summary.isDirty, true);

    // Cancel close
    fixture.viewModel->cancelClose();

    // Session, UndoStack, and in-flight task remain active
    QVERIFY(fixture.viewModel->hasSession());
    QVERIFY(fixture.viewModel->projectAggregate() != nullptr);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), 1);
    QVERIFY(fixture.controller->hasActiveOperation(pageId));

    // Make additional edits
    const domain::RectGeometry geom2{200.0, 200.0, 100.0, 50.0};
    fixture.viewModel->createRegion(geom2);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), 2);

    // Complete in-flight task
    fixture.ocrEngine->nextResponse.text = QStringLiteral("Survived OCR Text");
    QSignalSpy finishSpy(fixture.controller.get(), &EditorProcessingController::processingFinished);
    fixture.ocrEngine->complete();
    QVERIFY(finishSpy.wait(kAwaitTimeoutMs));
    QVERIFY(fixture.controller->hasProposal(pageId));

    // Save project successfully
    QSignalSpy saveSpy(fixture.viewModel.get(), &EditorViewModel::saveCompleted);
    fixture.viewModel->saveProject();
    QVERIFY(saveSpy.wait(kAwaitTimeoutMs));
    QVERIFY(!fixture.viewModel->state().saveState.isDirty);
    QVERIFY(fixture.viewModel->undoStackIsCleanForTest());

    fixture.viewModel->closeProjectSession();
}

void SessionLifecycleTest::confirmedCloseRunsTeardownInOrder() {
    SessionLifecycleFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("confirmed_close_order"), 1));

    const auto pageId = fixture.pageIds[0];
    fixture.viewModel->selectPage(pageId);

    const domain::RectGeometry geom{40.0, 40.0, 100.0, 50.0};
    fixture.viewModel->createRegion(geom);
    const auto regionId = fixture.viewModel->state().regions.first().id;

    // Verify lock file exists while project is open
    const QString lockFilePath = fixture.projectRoot + QStringLiteral("/.mangatranslator.lock");
    QVERIFY(QFileInfo::exists(lockFilePath));

    // Start deferred background task
    fixture.ocrEngine->deferred = true;
    fixture.controller->recognizeRegion(pageId, regionId);
    QVERIFY(fixture.controller->hasActiveOperation(pageId));

    // Execute confirmed close
    QSignalSpy projectClosedSpy(fixture.viewModel.get(), &EditorViewModel::projectClosed);
    fixture.viewModel->confirmDiscardAndClose();
    QVERIFY(projectClosedSpy.wait(kAwaitTimeoutMs));

    // 1. Session is destroyed and stops accepting commands
    QVERIFY(!fixture.viewModel->hasSession());
    QVERIFY(fixture.viewModel->projectAggregate() == nullptr);

    // 2. Undo stack is destroyed
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), 0);

    // Pushing/creating region is rejected / no-op
    fixture.viewModel->createRegion(geom);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), 0);

    // 3. Repository close released the file lease lock
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(lockFilePath), kAwaitTimeoutMs);

    // 4. Project tasks received stop token / cleared from controller
    QVERIFY(!fixture.controller->hasActiveOperation(pageId));

    // 5. Complete deferred future after confirmed teardown
    fixture.ocrEngine->nextResponse.text = QStringLiteral("Text After Teardown");
    fixture.ocrEngine->complete();
    QCoreApplication::processEvents();

    // Verify no callbacks triggered on destroyed objects and no crash
    QVERIFY(!fixture.controller->hasProposal(pageId));
    QVERIFY(!fixture.viewModel->hasSession());
}

void SessionLifecycleTest::applicationExitRunsSameProtocolAsWindowClose() {
    RealDataStack stack;
    const QString projRoot = stack.projectRoot(QStringLiteral("app_exit_scenario"));

    CreateProjectRequest createReq{
        .name = QStringLiteral("app_exit_project"),
        .path = projRoot,
        .sourceLanguage = domain::languages::Japanese,
        .targetLanguage = domain::languages::English,
        .readingDirection = domain::ReadingDirection::RightToLeft,
    };
    auto createFuture = CreateProjectUseCase(stack.repository.get()).execute(createReq, OperationContext{});
    QVERIFY(awaitFuture(createFuture) && createFuture.result().has_value());

    auto repo = std::make_unique<data::FileSystemProjectRepository>(stack.ioCoordinator.get());
    auto recents = std::make_unique<data::QSettingsRecentProjectsStore>();
    auto settings = std::make_unique<data::QSettingsAppSettingsStore>();
    auto thumbs = std::make_unique<data::FileSystemThumbnailStore>(stack.ioCoordinator.get());
    auto loader = std::make_unique<data::FileSystemPageImageLoader>(stack.ioCoordinator.get());
    auto importer = std::make_unique<data::FileSystemComicImporter>(stack.ioCoordinator.get());
    auto ocr = std::make_unique<manga::application::test::FakeOcrEngine>();
    auto trans = std::make_unique<manga::application::test::FakeTranslator>();
    auto inpainter = std::make_unique<services::SolidColorInpainter>(stack.executor.get());
    auto layouter = std::make_unique<services::QtTextLayouter>(stack.executor.get());
    auto derived = std::make_unique<data::FileSystemDerivedAssetStore>(stack.ioCoordinator.get());
    auto creds = std::make_unique<manga::application::test::FakeCredentialStore>();
    auto validator = std::make_unique<services::OcrModelValidator>();
    auto exporter = std::make_unique<data::FileSystemComicExporter>();

    auto context = std::make_unique<manga::app::ApplicationContext>(std::move(repo),
                                                                    std::move(recents),
                                                                    std::move(settings),
                                                                    std::move(thumbs),
                                                                    std::move(loader),
                                                                    std::move(importer),
                                                                    std::move(ocr),
                                                                    std::move(trans),
                                                                    std::move(inpainter),
                                                                    std::move(layouter),
                                                                    std::move(derived),
                                                                    std::move(creds),
                                                                    std::move(validator),
                                                                    std::move(exporter));

    int argc = 1;
    char* argv[] = {const_cast<char*>("manga_session_test")};
    auto app = std::make_unique<manga::app::Application>(argc, argv, std::move(context));

    QTimer::singleShot(0, [&]() {
        QVERIFY(app->mainWindow() != nullptr);
        QVERIFY(app->context() != nullptr);

        // Window close triggers MainWindow::closeEvent -> closeRequested -> editorViewModel->requestCloseProject
        // -> projectClosed -> handleProjectClose (mainWindow->forceClose, context->shutdown, app->quit)
        app->mainWindow()->close();
    });

    const int exitCode = app->run();
    QCOMPARE(exitCode, EXIT_SUCCESS);

    app.reset();
}

QTEST_MAIN(SessionLifecycleTest)
#include "session_lifecycle_test.moc"
