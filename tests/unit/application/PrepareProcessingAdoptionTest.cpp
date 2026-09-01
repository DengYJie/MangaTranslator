#include "PrepareProcessingAdoptionTest.h"

#include "FakeDerivedAssetStore.h"
#include "application/usecase/apply/PrepareProcessingAdoptionUseCases.h"
#include "domain/model/ComicProject.h"

#include <QDir>
#include <QSignalSpy>
#include <QTest>

using namespace manga::domain;
using namespace manga::application;
using namespace manga::application::test;

class PrepareProcessingAdoptionTestPrivate {
public:
    ComicProject* project{nullptr};
    PageId pageId;
    RegionId regionId;
    RegionId regionId2;
};

void PrepareProcessingAdoptionTest::init() {
    d_ptr = new PrepareProcessingAdoptionTestPrivate();
    d_ptr->project = new ComicProject("Test Project", ProjectId::generate());
    d_ptr->pageId = PageId::generate();
    ComicPage page(d_ptr->pageId);
    page.setImageSize({800, 600});
    page.setOriginalAssetPath("test.png");

    d_ptr->regionId = RegionId::generate();
    TextRegion region(d_ptr->regionId, RectGeometry(10, 10, 100, 50));
    region.setContentRevision(10);
    page.addRegion(region);

    d_ptr->regionId2 = RegionId::generate();
    TextRegion region2(d_ptr->regionId2, RectGeometry(10, 100, 100, 50));
    region2.setContentRevision(10);
    page.addRegion(region2);

    d_ptr->project->addPage(std::move(page));
}

void PrepareProcessingAdoptionTest::cleanup() {
    delete d_ptr->project;
    delete d_ptr;
}

void PrepareProcessingAdoptionTest::testIdMismatch() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = ProjectId::generate(); // Wrong project ID
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Ocr;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Project);
}

void PrepareProcessingAdoptionTest::testStaleGeneration() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Ocr;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 2; // Stale

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Cancelled);
}

void PrepareProcessingAdoptionTest::testDeletedRegion() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Ocr;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;

    OcrResponse ocrResp;
    ocrResp.regionId = RegionId::generate(); // Unknown region
    ocrResp.text = "Hello";
    ocrResp.confidence = 0.9;
    req.ocr.append(ocrResp);

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    auto result = future.result().value();
    QVERIFY(result.changes.isEmpty());
    QCOMPARE(result.skipped.size(), 1);
    QCOMPARE(result.skipped[0].error.code, QString("project.region_not_found"));
}

void PrepareProcessingAdoptionTest::testContentRevisionConflict() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Ocr;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;

    OcrResponse ocrResp;
    ocrResp.regionId = d_ptr->regionId;
    ocrResp.text = "Hello";
    ocrResp.confidence = 0.9;
    req.expectedRevisions.insert(d_ptr->regionId, 9); // Stale revision, expected 10
    req.ocr.append(ocrResp);

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    auto result = future.result().value();
    QVERIFY(result.changes.isEmpty());
    QCOMPARE(result.skipped.size(), 1);
    QCOMPARE(result.skipped[0].error.code, QString("input.stale_revision"));
}

void PrepareProcessingAdoptionTest::testManualSourceOverride() {
    // Setup manual override
    TextContent content;
    content.setManualText("Manual");
    PageRegionEdit edit;
    edit.regionId = d_ptr->regionId;
    edit.source = content;
    PageRegionEdit edits[] = {edit};
    (void)d_ptr->project->applyPageRegionEdits(d_ptr->pageId, edits);

    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Ocr;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.replaceManualPolicy = ReplaceManualPolicy::Keep;

    OcrResponse ocrResp;
    ocrResp.regionId = d_ptr->regionId;
    ocrResp.text = "Hello";
    ocrResp.confidence = 0.9;
    req.expectedRevisions.insert(d_ptr->regionId, 11); // Matches new revision
    req.ocr.append(ocrResp);

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    auto result = future.result().value();
    QVERIFY(result.changes.isEmpty());
    QCOMPARE(result.skipped.size(), 1);
    QCOMPARE(result.skipped[0].error.code, QString("input.manual_override"));

    // Now test with ReplaceManualPolicy::Replace
    req.replaceManualPolicy = ReplaceManualPolicy::Replace;
    auto futureReplace = useCase.execute(req);
    QVERIFY(futureReplace.isFinished());
    QVERIFY(futureReplace.result().has_value());
    auto resultReplace = futureReplace.result().value();

    QCOMPARE(resultReplace.changes.size(), 1);
    QVERIFY(resultReplace.skipped.isEmpty());
}

void PrepareProcessingAdoptionTest::testManualTranslationOverride() {
    // Setup manual translation
    TranslationEntry trans;
    trans.setManualText("Manual Trans");
    PageRegionEdit edit;
    edit.regionId = d_ptr->regionId;
    edit.translation = trans;
    PageRegionEdit edits[] = {edit};
    (void)d_ptr->project->applyPageRegionEdits(d_ptr->pageId, edits);

    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Translate;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.replaceManualPolicy = ReplaceManualPolicy::Keep;

    TranslationResponseItem item;
    item.regionId = d_ptr->regionId;
    item.sourceText = "";
    item.translation = "Model Trans";

    TranslationResponse transResp;
    transResp.providerId = "provider";
    transResp.items.append(item);

    req.translation = transResp;
    req.expectedRevisions.insert(d_ptr->regionId, 11);

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    auto result = future.result().value();

    QVERIFY(result.changes.isEmpty());
    QCOMPARE(result.skipped.size(), 1);
    QCOMPARE(result.skipped[0].error.code, QString("input.manual_override"));

    // Now test with ReplaceManualPolicy::Replace
    req.replaceManualPolicy = ReplaceManualPolicy::Replace;
    auto futureReplace = useCase.execute(req);
    QVERIFY(futureReplace.isFinished());
    QVERIFY(futureReplace.result().has_value());
    auto resultReplace = futureReplace.result().value();

    QCOMPARE(resultReplace.changes.size(), 1);
    QVERIFY(resultReplace.skipped.isEmpty());
}

void PrepareProcessingAdoptionTest::testTranslationUnknownOrMissingId() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Translate;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;

    TranslationResponseItem unknownItem;
    unknownItem.regionId = RegionId::generate();
    unknownItem.sourceText = "";
    unknownItem.translation = "Unknown";

    TranslationResponse transResp;
    transResp.providerId = "provider";
    transResp.items.append(unknownItem);

    req.translation = transResp;

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    auto result = future.result().value();

    QVERIFY(result.changes.isEmpty());
    QCOMPARE(result.skipped.size(), 1);
    QCOMPARE(result.skipped[0].error.code, QString("project.region_not_found"));
}

void PrepareProcessingAdoptionTest::testTranslationDuplicateId() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Translate;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;

    TranslationResponseItem item;
    item.regionId = d_ptr->regionId;
    item.sourceText = "";
    item.translation = "Trans1";

    TranslationResponseItem dupItem;
    dupItem.regionId = d_ptr->regionId;
    dupItem.sourceText = "";
    dupItem.translation = "Trans2";

    TranslationResponse transResp;
    transResp.providerId = "provider";
    transResp.items.append(item);
    transResp.items.append(dupItem);

    req.translation = transResp;

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    auto result = future.result().value();

    // Both should be skipped due to protocol error
    QVERIFY(result.changes.isEmpty());
    QCOMPARE(result.skipped.size(), 2);
    QCOMPARE(result.skipped[0].error.code, QString("protocol.duplicate_id"));
}

void PrepareProcessingAdoptionTest::testLayoutWarningOrOverflow() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Layout;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;

    LayoutResponse resp;
    resp.regionId = d_ptr->regionId;
    resp.overflow = true;
    resp.resolvedLayout.fontSizePt = 14.0;
    resp.resolvedLayout.fontFamily = "Arial";
    resp.warnings.append(LayoutWarning{QStringLiteral("layout.font_missing"), QStringLiteral("Font not found")});
    req.layout.append(resp);
    req.expectedRevisions.insert(d_ptr->regionId, 10);

    auto future = useCase.execute(req);
    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    auto result = future.result().value();

    QCOMPARE(result.changes.size(), 1);
    QCOMPARE(result.changes[0].regionId, d_ptr->regionId);
    QCOMPARE(result.skipped.size(), 1);
    QCOMPARE(result.skipped[0].error.code, QStringLiteral("layout.font_missing"));

    // Test with invalid layout result
    PrepareProcessingAdoptionRequest invalidReq;
    invalidReq.projectId = d_ptr->project->id();
    invalidReq.pageId = d_ptr->pageId;
    invalidReq.stage = OperationStage::Layout;
    invalidReq.sessionGeneration = 1;
    invalidReq.currentSessionGeneration = 1;

    LayoutResponse invalidResp;
    invalidResp.regionId = d_ptr->regionId;
    invalidResp.resolvedLayout.fontSizePt = -5.0; // Invalid
    invalidReq.layout.append(invalidResp);
    invalidReq.expectedRevisions.insert(d_ptr->regionId, 10);

    auto invalidFuture = useCase.execute(invalidReq);
    QVERIFY(invalidFuture.isFinished());
    QVERIFY(invalidFuture.result().has_value());
    auto invalidResult = invalidFuture.result().value();

    QVERIFY(invalidResult.changes.isEmpty());
    QCOMPARE(invalidResult.skipped.size(), 1);
    QCOMPARE(invalidResult.skipped[0].error.code, QStringLiteral("protocol.invalid_layout_result"));
}

void PrepareProcessingAdoptionTest::testClearPrepareIoFailure() {
    FakeDerivedAssetStore store;
    store.shouldFailCommit = true;
    store.nextError = makeApplicationError(ErrorCategory::Persistence, "clear.layer_commit_failed", "Commit failed");

    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Clear;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.pageSize = QSize(800, 600);

    ClearResponse clearResp;
    clearResp.regionId = d_ptr->regionId;
    clearResp.affectedBounds = RectGeometry(10, 10, 100, 50);
    QImage patch(100, 50, QImage::Format_RGBA8888);
    patch.fill(0xFFFFFFFF);
    clearResp.patch = patch;

    req.clear.append(clearResp);

    auto future = useCase.execute(req);
    future.waitForFinished();
    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("clear.layer_commit_failed"));
}

void PrepareProcessingAdoptionTest::testPipelinePreparesAllStages() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase clearUseCase(d_ptr->project, &store);
    PrepareProcessingAdoptionUseCase useCase(d_ptr->project, &clearUseCase);

    PrepareProcessingAdoptionRequest req;
    req.projectId = d_ptr->project->id();
    req.pageId = d_ptr->pageId;
    req.stage = OperationStage::Translate;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.pageSize = QSize(800, 600);
    req.projectRoot = QDir::tempPath();
    req.expectedRevisions.insert(d_ptr->regionId, 10);

    OcrResponse ocr;
    ocr.regionId = d_ptr->regionId;
    ocr.text = "OCR text";
    ocr.confidence = 0.95;
    req.ocr.append(ocr);

    TranslationResponseItem translated;
    translated.regionId = d_ptr->regionId;
    translated.translation = "Translated text";
    TranslationResponse translation;
    translation.providerId = "provider";
    translation.items.append(translated);
    req.translation = translation;

    ClearResponse clear;
    clear.regionId = d_ptr->regionId;
    clear.affectedBounds = RectGeometry(10, 10, 100, 50);
    clear.patch = QImage(100, 50, QImage::Format_RGBA8888);
    clear.patch.fill(0xFFFFFFFF);
    req.clear.append(clear);

    LayoutResponse layout;
    layout.regionId = d_ptr->regionId;
    layout.resolvedLayout.fontSizePt = 14.0;
    layout.resolvedLayout.fontFamily = "Arial";
    req.layout.append(layout);

    auto future = useCase.execute(req);
    future.waitForFinished();
    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    const auto prepared = future.result().value();
    QCOMPARE(prepared.changes.size(), 3);
    QVERIFY(prepared.clearAssetChange.has_value());
}
