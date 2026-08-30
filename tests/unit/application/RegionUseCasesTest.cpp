#include "RegionUseCasesTest.h"

#include "application/usecase/region/RegionUseCases.h"
#include "domain/model/ComicProject.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::domain;

void RegionUseCasesTest::testSuccessfulCreateRegion() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");
    project.addPage(page);

    CreateRegionUseCase useCase(&project);
    CreateRegionRequest req;
    req.pageId = page.id();
    req.rect = RectGeometry(10, 10, 50, 50);
    req.orderIndex = 0;

    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    const auto* projPage = project.findPage(page.id());
    QCOMPARE(projPage->regions().size(), 1);
    QCOMPARE(projPage->regions().front().id(), result.value());
    QCOMPARE(projPage->regions().front().geometry(), req.rect);
}

void RegionUseCasesTest::testInvalidCreateRegion() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");
    project.addPage(page);

    CreateRegionUseCase useCase(&project);
    CreateRegionRequest req;
    req.pageId = page.id();
    req.rect = RectGeometry(10, 10, -50, -50); // Invalid rect
    req.orderIndex = 0;

    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
}

void RegionUseCasesTest::testSuccessfulDeleteRegion() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");

    TextRegion region(RegionId::generate());
    region.setGeometry(RectGeometry(10, 10, 50, 50));
    page.addRegion(region);
    project.addPage(page);

    DeleteRegionUseCase useCase(&project);
    DeleteRegionRequest req;
    req.pageId = page.id();
    req.regionId = region.id();

    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    const auto* projPage = project.findPage(page.id());
    QVERIFY(projPage->regions().empty());
}

void RegionUseCasesTest::testMoveAndResizeRegion() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate(), "test.png", {1000, 1000});
    TextRegion region(RegionId::generate(), {10, 10, 50, 50});
    QVERIFY(page.addRegion(region));
    QVERIFY(project.addPage(page));

    MoveRegionUseCase move(&project);
    ResizeRegionUseCase resize(&project);

    QVERIFY(move.execute({page.id(), region.id(), {20, 30, 50, 50}}).has_value());
    QVERIFY(resize.execute({page.id(), region.id(), {20, 30, 80, 90}}).has_value());
    QCOMPARE(project.findPage(page.id())->findRegion(region.id())->geometry(), RectGeometry(20, 30, 80, 90));
}

void RegionUseCasesTest::testMissingRegionRejected() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate(), "test.png", {1000, 1000});
    QVERIFY(project.addPage(page));

    DeleteRegionUseCase remove(&project);
    MoveRegionUseCase move(&project);
    const auto missingId = RegionId::generate();

    QVERIFY(!remove.execute({page.id(), missingId}).has_value());
    QVERIFY(!move.execute({page.id(), missingId, {10, 10, 50, 50}}).has_value());
}

void RegionUseCasesTest::testInsertRegionAtExactOrder() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate(), "test.png", {1000, 1000});
    const RegionId r1 = RegionId::generate();
    const RegionId r3 = RegionId::generate();
    QVERIFY(page.addRegion(TextRegion(r1, {10, 10, 50, 50})));
    QVERIFY(page.addRegion(TextRegion(r3, {200, 200, 50, 50})));
    QVERIFY(project.addPage(page));

    InsertRegionUseCase useCase(&project);
    const RegionId r2 = RegionId::generate();
    TextRegion middle(r2, {100, 100, 50, 50});
    InsertRegionRequest req{.pageId = page.id(), .region = std::move(middle), .orderIndex = 1};
    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    const auto* projPage = project.findPage(page.id());
    QCOMPARE(projPage->regions().size(), size_t{3});
    QCOMPARE(projPage->regions()[0].id(), r1);
    QCOMPARE(projPage->regions()[1].id(), r2);
    QCOMPARE(projPage->regions()[2].id(), r3);
}

void RegionUseCasesTest::testInsertRegionRejectsInvalidInput() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate(), "test.png", {1000, 1000});
    QVERIFY(project.addPage(page));

    InsertRegionUseCase useCase(&project);

    // Invalid geometry
    TextRegion badGeo(RegionId::generate(), {10, 10, 1, 1});
    auto result = useCase.execute({page.id(), std::move(badGeo), 0});
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);

    // Out-of-range order
    TextRegion badOrder(RegionId::generate(), {10, 10, 50, 50});
    result = useCase.execute({page.id(), std::move(badOrder), 99});
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
}

void RegionUseCasesTest::testRemoveRegionReturnsFullRegionAndOrder() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate(), "test.png", {1000, 1000});
    const RegionId r1 = RegionId::generate();
    const RegionId r2 = RegionId::generate();
    TextRegion region1(r1, {10, 10, 50, 50}, 0);
    TextRegion region2(r2, {100, 100, 50, 50}, 1);
    QVERIFY(page.addRegion(region1));
    QVERIFY(page.addRegion(region2));
    QVERIFY(project.addPage(page));

    RemoveRegionUseCase useCase(&project);
    auto result = useCase.execute({page.id(), r1});
    QVERIFY(result.has_value());
    QCOMPARE(result->region.id(), r1);
    QCOMPARE(result->orderIndex, 0);
    QCOMPARE(result->region.geometry(), RectGeometry(10, 10, 50, 50));

    // Verify region was actually removed
    const auto* projPage = project.findPage(page.id());
    QCOMPARE(projPage->regions().size(), size_t{1});
    QCOMPARE(projPage->regions()[0].id(), r2);
}

void RegionUseCasesTest::testRemoveRegionNotFound() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate(), "test.png", {1000, 1000});
    QVERIFY(project.addPage(page));

    RemoveRegionUseCase useCase(&project);
    auto result = useCase.execute({page.id(), RegionId::generate()});
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
}

void RegionUseCasesTest::testApplyRegionSnapshotsSuccess() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate(), "test.png", {1000, 1000});
    const RegionId r1 = RegionId::generate();
    QVERIFY(page.addRegion(TextRegion(r1, {10, 10, 50, 50})));
    QVERIFY(project.addPage(page));

    ApplyRegionSnapshotsUseCase useCase(&project);
    TranslationEntry translation;
    translation.setManualText("snapshot text");
    TextLayout layout;
    layout.fontFamily = "Arial";
    layout.fontSizePt = 14.0;

    ApplyRegionSnapshotsRequest req{
        .pageId = page.id(),
        .edits = {
            {.regionId = r1, .geometry = RectGeometry{20, 20, 80, 80}, .translation = translation, .layout = layout}}};
    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    const auto* region = project.findPage(page.id())->findRegion(r1);
    QCOMPARE(region->geometry(), RectGeometry(20, 20, 80, 80));
    QCOMPARE(QString::fromStdString(region->translation().effectiveText()), "snapshot text");
    QCOMPARE(QString::fromStdString(region->layout().fontFamily), "Arial");
}

void RegionUseCasesTest::testApplyRegionSnapshotsBatchValidationFailureModifiesNothing() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate(), "test.png", {1000, 1000});
    const RegionId r1 = RegionId::generate();
    QVERIFY(page.addRegion(TextRegion(r1, {10, 10, 50, 50})));
    QVERIFY(project.addPage(page));

    const auto snapshotBefore = *project.findPage(page.id());

    ApplyRegionSnapshotsUseCase useCase(&project);
    // First edit is valid, second references a non-existent region
    ApplyRegionSnapshotsRequest req{
        .pageId = page.id(),
        .edits = {{.regionId = r1, .geometry = RectGeometry{20, 20, 80, 80}},
                  {.regionId = RegionId::generate(), .geometry = RectGeometry{10, 10, 50, 50}}}};
    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, "internal.undo_invariant_broken");

    // Page must be completely unmodified
    QVERIFY(*project.findPage(page.id()) == snapshotBefore);
}
