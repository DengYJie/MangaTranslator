#include "ApplyResultUseCaseTest.h"

#include "application/usecase/text/TextUseCases.h"
#include "domain/model/ComicProject.h"

#include <QtTest/QtTest>

using namespace manga::application;
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
    req.generation = 1;
    req.currentGeneration = 1;
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
    req.generation = 1;
    req.currentGeneration = 1;
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
    req.generation = 1;
    req.currentGeneration = 1;
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
    request.generation = 1;
    request.currentGeneration = 1;
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
    req.generation = 1;
    req.currentGeneration = 1;
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
    req.generation = 1;
    req.currentGeneration = 2;
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
    req.generation = 1;
    req.currentGeneration = 1;
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
    req.generation = 1;
    req.currentGeneration = 1;
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
    req.generation = 1;
    req.currentGeneration = 1;
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
    req.generation = 3;
    req.currentGeneration = 3;
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
    req.generation = 1;
    req.currentGeneration = 1;
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
    request.generation = 1;
    request.currentGeneration = 2;
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
    staleRequest.generation = 1;
    staleRequest.currentGeneration = 1;
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
    manualRequest.generation = 1;
    manualRequest.currentGeneration = 1;
    manualRequest.expectedRegions.append({d->regionId, "", 11});
    manualRequest.result.items.append({d->regionId, "", "Ignored", {}});

    const auto manualResult = useCase.execute(manualRequest);

    QVERIFY(manualResult.has_value());
    QCOMPARE(manualResult->failures.size(), 1);
    QCOMPARE(manualResult->failures.front().code, QString("input.manual_override"));
    QCOMPARE(d->project->findPage(d->pageId)->findRegion(d->regionId)->translation().effectiveText(),
             std::string("Manual"));
}
