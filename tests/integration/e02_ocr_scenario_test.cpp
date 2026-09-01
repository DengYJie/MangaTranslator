#include "FakeOcrEngine.h"
#include "IntegrationHarness.h"
#include "application/dto/ImportDto.h"
#include "application/dto/OcrDto.h"
#include "application/dto/ProjectDto.h"
#include "application/usecase/apply/ApplyUseCases.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "domain/model/ComicProject.h"
#include "domain/model/LanguageCode.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace manga;
using namespace manga::test;
using namespace manga::application;
using namespace manga::ui::screen::editor;

namespace {

struct OcrScenarioFixture {
    RealDataStack stack;
    QString projectRoot;
    domain::ComicProject project{"OcrScenarioTestProject"};
    std::unique_ptr<manga::application::test::FakeOcrEngine> ocrEngine;
    std::unique_ptr<EditorViewModel> viewModel;
    std::unique_ptr<EditorProcessingController> controller;
    domain::PageId pageId;
    domain::RegionId regionId;

    bool init(const QString& scenarioName, const QString& initialSourceText = {}) {
        projectRoot = stack.projectRoot(scenarioName);
        ocrEngine = std::make_unique<manga::application::test::FakeOcrEngine>();

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

        const QString fixturesDir = QStringLiteral(MANGA_TEST_FIXTURES_DIR);
        const QString page1Path = QDir(fixturesDir).filePath(QStringLiteral("pages/page_001.png"));
        if (!QFileInfo::exists(page1Path)) {
            return false;
        }

        ImportImagesRequest importReq{
            .projectId = project.id(),
            .projectRoot = projectRoot,
            .filePaths = {page1Path},
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
        if (!applyRes.has_value() || applyRes.value().empty()) {
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

        viewModel = std::make_unique<EditorViewModel>(
            stack.thumbnailStore.get(), stack.pageImageLoader.get(), &stack.operationRegistry, stack.repository.get());

        OpenedProjectSession session{
            .project = project,
            .projectRootPath = projectRoot,
            .recoverySource = RecoverySource::Current,
            .warnings = {},
        };
        viewModel->openProjectSession(session);
        if (!viewModel->hasSession() || viewModel->state().pages.isEmpty()) {
            return false;
        }

        pageId = viewModel->state().pages.first().id;
        QSignalSpy pageImageSpy(viewModel.get(), &EditorViewModel::pageImageReady);
        viewModel->selectPage(pageId);
        if (viewModel->currentPageImage().isNull()) {
            if (!pageImageSpy.wait(kAwaitTimeoutMs)) {
                return false;
            }
        }

        domain::RectGeometry initGeom{50.0, 50.0, 160.0, 100.0};
        viewModel->createRegion(initGeom);
        if (viewModel->state().regions.isEmpty()) {
            return false;
        }
        regionId = viewModel->state().regions.first().id;

        if (!initialSourceText.isEmpty()) {
            viewModel->commitInspectorFieldEdit(pageId, {regionId}, InspectorField::SourceText, initialSourceText);
            viewModel->sealTextEditSession();
        }

        controller = std::make_unique<EditorProcessingController>(viewModel.get(),
                                                                  ocrEngine.get(),
                                                                  nullptr,
                                                                  stack.inpainter.get(),
                                                                  nullptr,
                                                                  stack.layouter.get(),
                                                                  stack.derivedAssetStore.get(),
                                                                  &stack.operationRegistry);

        return true;
    }
};

} // namespace

class OcrScenarioTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void unadoptedOcrResultDoesNotMutateDomain();
    void adoptionPushesSingleUndoableCommand();
    void manualSourceTextSurvivesLateOcrResult();
    void staleSessionGenerationResultIsRejected();
};

void OcrScenarioTest::initTestCase() {
    qRegisterMetaType<domain::PageId>();
    qRegisterMetaType<domain::RegionId>();
    qRegisterMetaType<application::OperationId>();
    qRegisterMetaType<ui::screen::editor::EditorState>();
}

void OcrScenarioTest::unadoptedOcrResultDoesNotMutateDomain() {
    OcrScenarioFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("unadopted_ocr")));

    const auto pageId = fixture.pageId;
    const auto regionId = fixture.regionId;
    auto* project = fixture.viewModel->projectAggregate();
    QVERIFY(project != nullptr);

    const quint64 initialProjectRev = fixture.viewModel->currentProjectRevision();
    const quint64 initialRegionRev = project->findPage(pageId)->findRegion(regionId)->contentRevision();
    const int initialUndoCount = fixture.viewModel->undoStackCountForTest();
    const std::string initialSourceText = project->findPage(pageId)->findRegion(regionId)->source().text;

    fixture.ocrEngine->nextResponse.text = QStringLiteral("Unadopted OCR Output");
    fixture.ocrEngine->nextResponse.confidence = 0.95f;
    fixture.ocrEngine->deferred = false;

    QSignalSpy proposalSpy(fixture.controller.get(), &EditorProcessingController::proposalReady);
    QSignalSpy finishSpy(fixture.controller.get(), &EditorProcessingController::processingFinished);

    fixture.controller->recognizeRegion(pageId, regionId);

    QVERIFY(proposalSpy.wait(kAwaitTimeoutMs));
    QCOMPARE(finishSpy.count(), 1);
    QVERIFY(finishSpy.first().at(1).toBool());
    QVERIFY(fixture.controller->hasProposal(pageId));
    QVERIFY(fixture.controller->preparedAdoption(pageId) != nullptr);
    QCOMPARE(fixture.controller->preparedAdoption(pageId)->changes.size(), 1);

    // Unadopted proposal: domain revision, region content revision, text and undo stack are unchanged
    QCOMPARE(fixture.viewModel->currentProjectRevision(), initialProjectRev);
    QCOMPARE(project->findPage(pageId)->findRegion(regionId)->contentRevision(), initialRegionRev);
    QCOMPARE(project->findPage(pageId)->findRegion(regionId)->source().text, initialSourceText);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), initialUndoCount);

    // Reject proposal
    QSignalSpy rejectSpy(fixture.controller.get(), &EditorProcessingController::proposalRejected);
    fixture.controller->rejectProposal(pageId);
    QCOMPARE(rejectSpy.count(), 1);
    QVERIFY(!fixture.controller->hasProposal(pageId));
    QVERIFY(fixture.controller->preparedAdoption(pageId) == nullptr);

    // Domain and undo stack remain completely unchanged after rejection
    QCOMPARE(fixture.viewModel->currentProjectRevision(), initialProjectRev);
    QCOMPARE(project->findPage(pageId)->findRegion(regionId)->contentRevision(), initialRegionRev);
    QCOMPARE(project->findPage(pageId)->findRegion(regionId)->source().text, initialSourceText);
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), initialUndoCount);
}

void OcrScenarioTest::adoptionPushesSingleUndoableCommand() {
    OcrScenarioFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("adoption_undoable")));

    const auto pageId = fixture.pageId;
    const auto regionId = fixture.regionId;
    auto* project = fixture.viewModel->projectAggregate();
    QVERIFY(project != nullptr);

    const int initialUndoCount = fixture.viewModel->undoStackCountForTest();
    const std::string initialSourceText = project->findPage(pageId)->findRegion(regionId)->source().text;

    const QString recognizedText = QStringLiteral("漫画のテスト文字");
    const float recognizedConfidence = 0.96f;
    fixture.ocrEngine->nextResponse.text = recognizedText;
    fixture.ocrEngine->nextResponse.confidence = recognizedConfidence;
    fixture.ocrEngine->deferred = false;

    QSignalSpy proposalSpy(fixture.controller.get(), &EditorProcessingController::proposalReady);
    fixture.controller->recognizeRegion(pageId, regionId);
    QVERIFY(proposalSpy.wait(kAwaitTimeoutMs));
    QVERIFY(fixture.controller->hasProposal(pageId));

    const int callCountAfterRecognize = fixture.ocrEngine->callCount;
    QCOMPARE(callCountAfterRecognize, 1);

    // Adopt proposal
    QSignalSpy adoptSpy(fixture.controller.get(), &EditorProcessingController::proposalAdopted);
    fixture.controller->adoptProposal(pageId);
    QCOMPARE(adoptSpy.count(), 1);
    QVERIFY(!fixture.controller->hasProposal(pageId));

    // Adoption pushes exactly one undoable command
    QCOMPARE(fixture.viewModel->undoStackCountForTest(), initialUndoCount + 1);
    QVERIFY(fixture.viewModel->state().canUndo);

    // Domain state reflects recognized OCR text
    const auto* regionAdopted = project->findPage(pageId)->findRegion(regionId);
    QCOMPARE(QString::fromStdString(regionAdopted->source().text), recognizedText);
    QCOMPARE(regionAdopted->source().confidence, recognizedConfidence);

    // Undo restores prior source text state
    fixture.viewModel->undo();
    const auto* regionAfterUndo = project->findPage(pageId)->findRegion(regionId);
    QCOMPARE(regionAfterUndo->source().text, initialSourceText);
    QVERIFY(fixture.viewModel->state().canRedo);

    // Redo restores recognized text without calling OCR engine again
    fixture.viewModel->redo();
    const auto* regionAfterRedo = project->findPage(pageId)->findRegion(regionId);
    QCOMPARE(QString::fromStdString(regionAfterRedo->source().text), recognizedText);
    QCOMPARE(regionAfterRedo->source().confidence, recognizedConfidence);
    QCOMPARE(fixture.ocrEngine->callCount, callCountAfterRecognize);
}

void OcrScenarioTest::manualSourceTextSurvivesLateOcrResult() {
    OcrScenarioFixture fixture;
    const QString initialManualText = QStringLiteral("Initial Text");
    QVERIFY(fixture.init(QStringLiteral("manual_text_survives"), initialManualText));

    const auto pageId = fixture.pageId;
    const auto regionId = fixture.regionId;
    auto* project = fixture.viewModel->projectAggregate();
    QVERIFY(project != nullptr);

    const quint64 initialRegionRev = project->findPage(pageId)->findRegion(regionId)->contentRevision();

    // Configure deferred OCR engine
    fixture.ocrEngine->deferred = true;
    fixture.ocrEngine->nextResponse.text = QStringLiteral("Late Arriving OCR Text");
    fixture.ocrEngine->nextResponse.confidence = 0.99f;

    // Trigger OCR request with expectedRevision equal to initialRegionRev
    fixture.controller->recognizeRegion(pageId, regionId);
    QVERIFY(fixture.controller->hasActiveOperation(pageId));

    // Manually edit source text before OCR engine completes
    const QString modifiedManualText = QStringLiteral("Manual Modified Text");
    fixture.viewModel->commitInspectorFieldEdit(pageId, {regionId}, InspectorField::SourceText, modifiedManualText);
    fixture.viewModel->sealTextEditSession();

    const quint64 modifiedRegionRev = project->findPage(pageId)->findRegion(regionId)->contentRevision();
    QVERIFY(modifiedRegionRev > initialRegionRev);
    QCOMPARE(QString::fromStdString(project->findPage(pageId)->findRegion(regionId)->source().text),
             modifiedManualText);

    // Complete deferred OCR
    QSignalSpy finishSpy(fixture.controller.get(), &EditorProcessingController::processingFinished);
    fixture.ocrEngine->complete();
    QVERIFY(finishSpy.wait(kAwaitTimeoutMs));

    // Controller prepared adoption has empty changes and skipped item due to stale revision
    const auto* prepared = fixture.controller->preparedAdoption(pageId);
    QVERIFY(prepared != nullptr);
    QCOMPARE(prepared->changes.size(), 0);
    QCOMPARE(prepared->skipped.size(), 1);
    QCOMPARE(prepared->skipped.first().error.code, QStringLiteral("input.stale_revision"));

    // Attempting to adopt proposal does not overwrite manual text
    fixture.controller->adoptProposal(pageId);

    // Direct adoption check on stale revision payload returns input.stale_revision
    PreparedProcessingAdoption staleAdoption;
    staleAdoption.stage = OperationStage::Ocr;
    PreparedRegionChange staleChange;
    staleChange.pageId = pageId;
    staleChange.regionId = regionId;
    staleChange.expectedRevision = initialRegionRev;
    staleChange.before.regionId = regionId;
    staleChange.before.source = domain::TextContent(initialManualText.toStdString());
    staleChange.after.regionId = regionId;
    staleChange.after.source = domain::TextContent("Overwritten");
    staleAdoption.changes.append(staleChange);

    auto staleResult = fixture.viewModel->adoptProcessingProposal(staleAdoption);
    QVERIFY(!staleResult.has_value());
    QCOMPARE(staleResult.error().code, QStringLiteral("input.stale_revision"));
    QCOMPARE(staleResult.error().category, ErrorCategory::Input);

    // Manual source text is preserved in aggregate and UI state has no lingering error notice
    const auto* regionFinal = project->findPage(pageId)->findRegion(regionId);
    QCOMPARE(QString::fromStdString(regionFinal->source().text), modifiedManualText);
    QVERIFY(!fixture.viewModel->state().undoHistoryFaulted);
}

void OcrScenarioTest::staleSessionGenerationResultIsRejected() {
    OcrScenarioFixture fixture;
    QVERIFY(fixture.init(QStringLiteral("stale_session_generation")));

    const auto pageId = fixture.pageId;
    const auto regionId = fixture.regionId;
    auto* project = fixture.viewModel->projectAggregate();
    QVERIFY(project != nullptr);

    // 1. Verify PrepareProcessingAdoptionUseCase rejects mismatched session generation
    PrepareProcessingAdoptionUseCase prepUseCase(project, nullptr);
    PrepareProcessingAdoptionRequest staleReq{
        .projectId = project->id(),
        .pageId = pageId,
        .stage = OperationStage::Ocr,
        .sessionGeneration = 1,
        .currentSessionGeneration = 2,
    };
    OcrResponse ocrResp;
    ocrResp.regionId = regionId;
    ocrResp.text = QStringLiteral("Stale text");
    staleReq.ocr.append(ocrResp);

    auto prepFuture = prepUseCase.execute(staleReq);
    QVERIFY(awaitFuture(prepFuture));
    auto prepRes = prepFuture.result();
    QVERIFY(!prepRes.has_value());
    QCOMPARE(prepRes.error().category, ErrorCategory::Cancelled);
    QCOMPARE(prepRes.error().code, QStringLiteral("cancelled.stale_generation"));

    // 2. Test in-flight controller task when session generation is incremented
    fixture.ocrEngine->deferred = true;
    fixture.ocrEngine->nextResponse.text = QStringLiteral("Deferred OCR Result");
    fixture.ocrEngine->nextResponse.confidence = 0.95f;

    fixture.controller->recognizeRegion(pageId, regionId);
    QVERIFY(fixture.controller->hasActiveOperation(pageId));

    // Reopen session to bump session generation
    OpenedProjectSession newSession{
        .project = *project,
        .projectRootPath = fixture.projectRoot,
        .recoverySource = RecoverySource::Current,
        .warnings = {},
    };
    fixture.viewModel->openProjectSession(newSession);

    // Complete deferred OCR from old generation
    fixture.ocrEngine->complete();
    QCoreApplication::processEvents();

    // Result is silently discarded without setting proposal or error notice
    QVERIFY(!fixture.controller->hasProposal(pageId));
    QVERIFY(fixture.controller->preparedAdoption(pageId) == nullptr);
    QVERIFY(!fixture.viewModel->state().processing.hasProposal);
    QVERIFY(!fixture.viewModel->state().notice.has_value());
}

QTEST_MAIN(OcrScenarioTest)
#include "e02_ocr_scenario_test.moc"
