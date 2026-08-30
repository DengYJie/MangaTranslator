#include "ApplyResultUseCaseTest.h"

#include "FakeDerivedAssetStore.h"
#include "application/usecase/apply/ApplyUseCases.h"
#include "application/usecase/text/TextUseCases.h"
#include "domain/model/ComicProject.h"

#include <QImage>
#include <QSize>
#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;
using namespace manga::domain;

class ApplyResultUseCaseTest::Private {
public:
    ComicProject* project{nullptr};
    PageId pageId;
    RegionId regionId;
};

void ApplyResultUseCaseTest::init() {
    d = new Private();
    d->project = new ComicProject("Test Project", ProjectId::generate());
    d->pageId = PageId::generate();
    ComicPage page(d->pageId);
    page.setImageSize({800, 600});
    page.setOriginalAssetPath("test.png");

    d->regionId = RegionId::generate();
    TextRegion region(d->regionId, RectGeometry(10, 10, 100, 50));
    region.setContentRevision(10);
    page.addRegion(region);

    d->project->addPage(std::move(page));
}

void ApplyResultUseCaseTest::cleanup() {
    delete d->project;
    delete d;
}

void ApplyResultUseCaseTest::testApplyOcrSuccess() {
    ApplyOcrResultUseCase useCase(d->project);

    ApplyOcrResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 10;
    req.expectedRegionId = d->regionId;
    req.result.regionId = d->regionId;
    req.result.text = "Hello";
    req.result.confidence = 0.9;

    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    auto page = d->project->findPage(d->pageId);
    auto region = page->findRegion(d->regionId);
    QCOMPARE(QString::fromStdString(region->source().text), QString("Hello"));
    QCOMPARE(region->source().sourceType, TextSourceType::OcrAuto);
    QCOMPARE(region->contentRevision(), 11);
}

void ApplyResultUseCaseTest::testApplyOcrFailsOnProjectMismatch() {
    ApplyOcrResultUseCase useCase(d->project);

    ApplyOcrResultRequest req;
    req.projectId = ProjectId::generate(); // Wrong project ID
    req.pageId = d->pageId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 10;
    req.expectedRegionId = d->regionId;
    req.result.regionId = d->regionId;
    req.result.text = "Hello";

    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Project);
}

void ApplyResultUseCaseTest::testApplyOcrFailsOnPageMismatch() {
    ApplyOcrResultUseCase useCase(d->project);

    ApplyOcrResultRequest req;
    req.projectId = d->project->id();
    req.pageId = PageId::generate();
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 10;
    req.expectedRegionId = d->regionId;
    req.result.regionId = d->regionId;
    req.result.text = "Hello";
    req.result.confidence = 0.9;

    const auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Project);
    QCOMPARE(result.error().code, QString("project.page_not_found"));
}

void ApplyResultUseCaseTest::testApplyOcrFailsOnRegionMismatch() {
    ApplyOcrResultUseCase useCase(d->project);

    ApplyOcrResultRequest request;
    request.projectId = d->project->id();
    request.pageId = d->pageId;
    request.expectedRegionId = d->regionId;
    request.result.regionId = RegionId::generate();
    request.result.text = "Wrong region";
    request.result.confidence = 0.9;
    request.sessionGeneration = 1;
    request.currentSessionGeneration = 1;
    request.expectedRevision = 10;

    const auto result = useCase.execute(request);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Protocol);
    QCOMPARE(result.error().code, QString("protocol.region_mismatch"));
    QCOMPARE(d->project->findPage(d->pageId)->findRegion(d->regionId)->source().text, std::string{});
}

void ApplyResultUseCaseTest::testApplyOcrFailsOnStaleRevision() {
    ApplyOcrResultUseCase useCase(d->project);

    ApplyOcrResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 9; // Stale revision (expected 10)
    req.expectedRegionId = d->regionId;
    req.result.regionId = d->regionId;
    req.result.text = "Hello";

    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
    QCOMPARE(result.error().code, "input.stale_revision");
}

void ApplyResultUseCaseTest::testApplyOcrFailsOnStaleGeneration() {
    ApplyOcrResultUseCase useCase(d->project);

    ApplyOcrResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 2;
    req.expectedRevision = 10;
    req.expectedRegionId = d->regionId;
    req.result.regionId = d->regionId;
    req.result.text = "Hello";

    const auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Cancelled);
    QCOMPARE(result.error().code, QString("cancelled.stale_generation"));
}

void ApplyResultUseCaseTest::testApplyOcrFailsOnManualOverride() {
    // Set manual text first
    TextContent content;
    content.setManualText("Manual");
    PageRegionEdit edit;
    edit.regionId = d->regionId;
    edit.source = content;
    PageRegionEdit edits[] = {edit};
    (void)d->project->applyPageRegionEdits(d->pageId, edits);

    ApplyOcrResultUseCase useCase(d->project);
    ApplyOcrResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 11;
    req.expectedRegionId = d->regionId;
    req.result.regionId = d->regionId;
    req.result.text = "Hello";

    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
    QCOMPARE(result.error().code, "input.manual_override");
}

void ApplyResultUseCaseTest::testApplyTranslationSuccess() {
    ApplyTranslationResultUseCase useCase(d->project);

    ApplyTranslationResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRegions.append({d->regionId, "", 10});

    TranslationResponseItem item;
    item.regionId = d->regionId;
    item.sourceText = ""; // Matches current empty source text
    item.translation = "Translated";
    item.candidates = {"Cand1", "Cand2"};
    req.result.items.push_back(item);

    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    auto page = d->project->findPage(d->pageId);
    auto region = page->findRegion(d->regionId);
    QCOMPARE(QString::fromStdString(region->translation().modelText().value()), QString("Translated"));
    QCOMPARE(region->translation().candidates().size(), 2);
}

void ApplyResultUseCaseTest::testApplyTranslationFailsOnDuplicateId() {
    ApplyTranslationResultUseCase useCase(d->project);

    ApplyTranslationResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRegions.append({d->regionId, "", 10});

    TranslationResponseItem item;
    item.regionId = d->regionId;
    item.sourceText = "";
    item.translation = "Translated";

    req.result.items.push_back(item);
    req.result.items.push_back(item); // Duplicate

    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Protocol);
    QCOMPARE(result.error().code, "protocol.duplicate_id");
}

void ApplyResultUseCaseTest::testApplyTranslationReportsPartialFailures() {
    ApplyTranslationResultUseCase useCase(d->project);

    ApplyTranslationResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.sessionGeneration = 3;
    req.currentSessionGeneration = 3;
    req.expectedRegions.append({d->regionId, "", 10});
    const auto missingRegionId = RegionId::generate();
    req.expectedRegions.append({missingRegionId, "missing", 0});

    req.result.items.append({d->regionId, "", "Translated", {}});
    const auto unknownRegionId = RegionId::generate();
    req.result.items.append({unknownRegionId, "unknown", "Ignored", {}});

    const auto result = useCase.execute(req);
    QVERIFY(result.has_value());
    QCOMPARE(result->appliedRegionIds.size(), 1);
    QCOMPARE(result->failures.size(), 2);
    QCOMPARE(result->failures[0].code, QString("protocol.unknown_id"));
    QCOMPARE(result->failures[1].code, QString("protocol.missing_id"));
}

void ApplyResultUseCaseTest::testApplyTranslationReportsSourceMismatch() {
    ApplyTranslationResultUseCase useCase(d->project);

    ApplyTranslationResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRegions.append({d->regionId, "", 10});
    req.result.items.append({d->regionId, "changed", "Ignored", {}});

    const auto result = useCase.execute(req);
    QVERIFY(result.has_value());
    QVERIFY(result->appliedRegionIds.isEmpty());
    QCOMPARE(result->failures.size(), 1);
    QCOMPARE(result->failures[0].code, QString("protocol.source_mismatch"));
    QVERIFY(!d->project->findPage(d->pageId)->findRegion(d->regionId)->translation().modelText().has_value());
}

void ApplyResultUseCaseTest::testApplyTranslationFailsOnStaleGeneration() {
    ApplyTranslationResultUseCase useCase(d->project);
    ApplyTranslationResultRequest request;
    request.projectId = d->project->id();
    request.pageId = d->pageId;
    request.sessionGeneration = 1;
    request.currentSessionGeneration = 2;
    request.expectedRegions.append({d->regionId, "", 10});
    request.result.items.append({d->regionId, "", "Ignored", {}});

    const auto result = useCase.execute(request);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QString("cancelled.stale_generation"));
    QVERIFY(!d->project->findPage(d->pageId)->findRegion(d->regionId)->translation().modelText().has_value());
}

void ApplyResultUseCaseTest::testApplyTranslationRejectsStaleAndManualTargets() {
    ApplyTranslationResultUseCase useCase(d->project);
    ApplyTranslationResultRequest staleRequest;
    staleRequest.projectId = d->project->id();
    staleRequest.pageId = d->pageId;
    staleRequest.sessionGeneration = 1;
    staleRequest.currentSessionGeneration = 1;
    staleRequest.expectedRegions.append({d->regionId, "", 9});
    staleRequest.result.items.append({d->regionId, "", "Ignored", {}});

    const auto staleResult = useCase.execute(staleRequest);

    QVERIFY(staleResult.has_value());
    QCOMPARE(staleResult->failures.size(), 1);
    QCOMPARE(staleResult->failures.front().code, QString("input.stale_revision"));

    auto translation = d->project->findPage(d->pageId)->findRegion(d->regionId)->translation();
    translation.setManualText("Manual");
    PageRegionEdit edits[] = {{.regionId = d->regionId, .translation = translation}};
    QVERIFY(d->project->applyPageRegionEdits(d->pageId, edits).has_value());

    ApplyTranslationResultRequest manualRequest;
    manualRequest.projectId = d->project->id();
    manualRequest.pageId = d->pageId;
    manualRequest.sessionGeneration = 1;
    manualRequest.currentSessionGeneration = 1;
    manualRequest.expectedRegions.append({d->regionId, "", 11});
    manualRequest.result.items.append({d->regionId, "", "Ignored", {}});

    const auto manualResult = useCase.execute(manualRequest);

    QVERIFY(manualResult.has_value());
    QCOMPARE(manualResult->failures.size(), 1);
    QCOMPARE(manualResult->failures.front().code, QString("input.manual_override"));
    QCOMPARE(d->project->findPage(d->pageId)->findRegion(d->regionId)->translation().effectiveText(),
             std::string("Manual"));
}

void ApplyResultUseCaseTest::testApplyClearSuccess() {
    FakeDerivedAssetStore store;
    store.nextRelativePath = "assets/derived/clear-test/cleaning/v1/aaaa.png";
    PrepareClearResultUseCase useCase(d->project, &store);

    PrepareClearResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.expectedRegionId = d->regionId;
    req.pageSize = QSize(800, 600);
    req.projectRoot = QStringLiteral("unused");
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 10;
    req.result.regionId = d->regionId;
    req.result.affectedBounds = RectGeometry(10, 10, 100, 50);
    QImage patch(100, 50, QImage::Format_RGBA8888);
    patch.fill(0xFFFFFFFF);
    req.result.patch = patch;

    const auto future = useCase.execute(req);

    QVERIFY(future.isFinished());
    const auto result = future.result();
    QVERIFY(result.has_value());
    QCOMPARE(result->afterRef, store.nextRelativePath);
    QCOMPARE(store.commitCount, 1);
    QCOMPARE(store.lastCommitRequest.layer.size(), QSize(800, 600));

    // Phase 2: Apply
    ApplyPreparedClearChangeUseCase applyUseCase(d->project);
    ApplyPreparedClearChangeRequest applyReq;
    applyReq.projectId = req.projectId;
    applyReq.pageId = req.pageId;
    applyReq.expectedRegionId = req.expectedRegionId;
    applyReq.targetRef = result->afterRef;
    applyReq.targetState = result->afterState;
    applyReq.sessionGeneration = req.sessionGeneration;
    applyReq.currentSessionGeneration = req.currentSessionGeneration;
    applyReq.expectedRevision = req.expectedRevision;

    const auto applyResult = applyUseCase.execute(applyReq);
    QVERIFY(applyResult.has_value());

    const auto* page = d->project->findPage(d->pageId);
    QCOMPARE(QString::fromStdString(*page->clearLayerAsset()), store.nextRelativePath);
    QVERIFY(page->findRegion(d->regionId)->isCleared());
    QVERIFY(d->project->isDirty());
    // The isCleared flag is not a content change: contentRevision stays at 10.
    QCOMPARE(page->findRegion(d->regionId)->contentRevision(), 10u);
}

void ApplyResultUseCaseTest::testApplyClearSupportsUndoAndRedo() {
    ApplyPreparedClearChangeUseCase useCase(d->project);
    ApplyPreparedClearChangeRequest request;
    request.projectId = d->project->id();
    request.pageId = d->pageId;
    request.expectedRegionId = d->regionId;
    request.sessionGeneration = 1;
    request.currentSessionGeneration = 1;
    request.expectedRevision = 10;

    request.targetRef = QStringLiteral("assets/derived/page/cleaning/v1/result.png");
    request.targetState = true;
    QVERIFY(useCase.execute(request).has_value());
    QVERIFY(d->project->findPage(d->pageId)->clearLayerAsset().has_value());
    QVERIFY(d->project->findPage(d->pageId)->findRegion(d->regionId)->isCleared());

    request.targetRef.clear();
    request.targetState = false;
    QVERIFY(useCase.execute(request).has_value());
    QVERIFY(!d->project->findPage(d->pageId)->clearLayerAsset().has_value());
    QVERIFY(!d->project->findPage(d->pageId)->findRegion(d->regionId)->isCleared());

    request.targetRef = QStringLiteral("assets/derived/page/cleaning/v1/result.png");
    request.targetState = true;
    QVERIFY(useCase.execute(request).has_value());
    QCOMPARE(d->project->findPage(d->pageId)->findRegion(d->regionId)->contentRevision(), 10u);
}

void ApplyResultUseCaseTest::testApplyClearRejectsStaleRevision() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase useCase(d->project, &store);

    PrepareClearResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.expectedRegionId = d->regionId;
    req.pageSize = QSize(800, 600);
    req.projectRoot = QStringLiteral("unused");
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 9;
    req.result.regionId = d->regionId;
    req.result.affectedBounds = RectGeometry(10, 10, 100, 50);
    req.result.patch = QImage(100, 50, QImage::Format_RGBA8888);

    const auto future = useCase.execute(req);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("input.stale_revision"));
    QCOMPARE(store.commitCount, 0);
}

void ApplyResultUseCaseTest::testApplyClearPropagatesCommitFailure() {
    FakeDerivedAssetStore store;
    store.shouldFailCommit = true;
    store.nextError = makeApplicationError(ErrorCategory::Persistence, "clear.layer_commit_failed", "Commit failed");
    PrepareClearResultUseCase useCase(d->project, &store);

    PrepareClearResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.expectedRegionId = d->regionId;
    req.pageSize = QSize(800, 600);
    req.projectRoot = QStringLiteral("unused");
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 10;
    req.result.regionId = d->regionId;
    req.result.affectedBounds = RectGeometry(10, 10, 100, 50);
    req.result.patch = QImage(100, 50, QImage::Format_RGBA8888);

    const auto future = useCase.execute(req);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("clear.layer_commit_failed"));
    // The Domain is untouched: no layer reference and no cleared flag.
    QVERIFY(!d->project->findPage(d->pageId)->clearLayerAsset().has_value());
    QVERIFY(!d->project->findPage(d->pageId)->findRegion(d->regionId)->isCleared());
}

void ApplyResultUseCaseTest::testApplyClearRejectsPatchBoundsMismatch() {
    FakeDerivedAssetStore store;
    PrepareClearResultUseCase useCase(d->project, &store);

    PrepareClearResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.expectedRegionId = d->regionId;
    req.pageSize = QSize(800, 600);
    req.projectRoot = QStringLiteral("unused");
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 10;
    req.result.regionId = d->regionId;
    req.result.affectedBounds = RectGeometry(10, 10, 100, 50);
    req.result.patch = QImage(64, 64, QImage::Format_RGBA8888);

    const auto future = useCase.execute(req);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("protocol.invalid_clear_result"));
    QCOMPARE(store.commitCount, 0);
}

void ApplyResultUseCaseTest::testApplyLayoutSuccess() {
    ApplyLayoutResultUseCase useCase(d->project);

    ApplyLayoutResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.expectedRegionId = d->regionId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 1;
    req.expectedRevision = 10;
    req.result.regionId = d->regionId;
    req.result.resolvedLayout.fontFamily = "Golden Font";
    req.result.resolvedLayout.fontSizePt = 14.0;

    const auto result = useCase.execute(req);

    QVERIFY(result.has_value());
    const auto* region = d->project->findPage(d->pageId)->findRegion(d->regionId);
    QCOMPARE(QString::fromStdString(region->layout().fontFamily), QString("Golden Font"));
    QVERIFY(region->contentRevision() == 11u);
}

void ApplyResultUseCaseTest::testApplyLayoutRejectsStaleGeneration() {
    ApplyLayoutResultUseCase useCase(d->project);

    ApplyLayoutResultRequest req;
    req.projectId = d->project->id();
    req.pageId = d->pageId;
    req.expectedRegionId = d->regionId;
    req.sessionGeneration = 1;
    req.currentSessionGeneration = 2;
    req.expectedRevision = 10;
    req.result.regionId = d->regionId;
    req.result.resolvedLayout.fontSizePt = 14.0;

    const auto result = useCase.execute(req);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Cancelled);
    QCOMPARE(result.error().code, QString("cancelled.stale_generation"));
}
