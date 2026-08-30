#include "ComicProjectTest.h"

#include "domain/model/ComicProject.h"

#include <QtTest/QtTest>

#include <array>

using namespace manga::domain;

namespace {

ComicPage makePage(int orderIndex) {
    return ComicPage(PageId::generate(), "assets/original/page.png", {.width = 1000, .height = 1000}, orderIndex);
}

TextRegion makeRegion(RegionId id = RegionId::generate()) {
    return TextRegion(id, {.x = 10.0, .y = 10.0, .width = 100.0, .height = 100.0});
}

} // namespace

void ComicProjectTest::testProjectWideRegionIdUniqueness() {
    ComicProject project("Test");
    const PageId firstPageId = PageId::generate();
    const PageId secondPageId = PageId::generate();
    QVERIFY(project.addPage(ComicPage(firstPageId, "assets/original/page1.png", {.width = 1000, .height = 1000})));
    QVERIFY(project.addPage(ComicPage(secondPageId, "assets/original/page2.png", {.width = 1000, .height = 1000})));

    const RegionId sharedId = RegionId::generate();
    QVERIFY(project.addRegionToPage(firstPageId, makeRegion(sharedId)).has_value());
    project.markClean();

    const auto duplicateResult = project.addRegionToPage(secondPageId, makeRegion(sharedId));
    QVERIFY(!duplicateResult.has_value());
    QCOMPARE(duplicateResult.error().code, ProjectErrorCode::DuplicateId);
    QVERIFY(!project.isDirty());
    QCOMPARE(project.findPage(secondPageId)->regions().size(), size_t{0});
}

void ComicProjectTest::testBatchEditSuccess() {
    ComicProject project("Test");
    const PageId pageId = PageId::generate();
    QVERIFY(project.addPage(ComicPage(pageId, "assets/original/page.png", {.width = 1000, .height = 1000})));
    const RegionId regionId = RegionId::generate();
    QVERIFY(project.addRegionToPage(pageId, makeRegion(regionId)).has_value());
    project.markClean();

    const auto* before = project.findPage(pageId)->findRegion(regionId);
    const uint64_t initialRevision = before->contentRevision();

    TranslationEntry translation;
    translation.setManualText("translated");
    TextLayout layout;
    layout.fontFamily = "Test Font";
    const std::array edits{
        PageRegionEdit{.regionId = regionId,
                       .geometry = RectGeometry{.x = 950.0, .y = 950.0, .width = 100.0, .height = 100.0},
                       .translation = translation,
                       .layout = layout}};

    const auto result = project.applyPageRegionEdits(pageId, edits);
    QVERIFY(result.has_value());
    QVERIFY(project.isDirty());

    const auto* updated = project.findPage(pageId)->findRegion(regionId);
    QCOMPARE(updated->geometry().width, 50.0);
    QCOMPARE(updated->geometry().height, 50.0);
    QCOMPARE(QString::fromStdString(updated->translation().effectiveText()), "translated");
    QCOMPARE(QString::fromStdString(updated->layout().fontFamily), "Test Font");
    QCOMPARE(updated->contentRevision(), initialRevision + 3);
}

void ComicProjectTest::testBatchEditRollbackRestoresRevisionAndDirty() {
    ComicProject project("Test");
    const PageId pageId = PageId::generate();
    QVERIFY(project.addPage(ComicPage(pageId, "assets/original/page.png", {.width = 1000, .height = 1000})));
    const RegionId regionId = RegionId::generate();
    QVERIFY(project.addRegionToPage(pageId, makeRegion(regionId)).has_value());
    project.markClean();

    const TextRegion before = *project.findPage(pageId)->findRegion(regionId);
    TranslationEntry translation;
    translation.setManualText("must be rolled back");
    const std::array edits{
        PageRegionEdit{.regionId = regionId, .translation = translation},
        PageRegionEdit{.regionId = RegionId::generate(),
                       .geometry = RectGeometry{.x = 20.0, .y = 20.0, .width = 50.0, .height = 50.0}},
    };

    const auto result = project.applyPageRegionEdits(pageId, edits);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, ProjectErrorCode::EditGeometryRejected);
    QVERIFY(!project.isDirty());
    QVERIFY(*project.findPage(pageId)->findRegion(regionId) == before);
}

void ComicProjectTest::testEmptyBatchDoesNotMarkDirty() {
    ComicProject project("Test");
    const PageId pageId = PageId::generate();
    QVERIFY(project.addPage(ComicPage(pageId, "assets/original/page.png", {.width = 1000, .height = 1000})));
    project.markClean();

    const std::array<PageRegionEdit, 0> edits{};
    QVERIFY(project.applyPageRegionEdits(pageId, edits).has_value());
    QVERIFY(!project.isDirty());
}

void ComicProjectTest::testMarkCleanRejectsStaleSaveRevision() {
    ComicProject project("Test", ProjectId::generate());
    project.markClean();
    const auto savedRevision = project.revision();

    project.setName("Edited while saving");

    QVERIFY(!project.markCleanIfRevision(savedRevision));
    QVERIFY(project.isDirty());
    QVERIFY(project.markCleanIfRevision(project.revision()));
    QVERIFY(!project.isDirty());
}

void ComicProjectTest::testApplyClearChangeOnPageSuccess() {
    ComicProject project("Test");
    const PageId pageId = PageId::generate();
    QVERIFY(project.addPage(ComicPage(pageId, "assets/original/page.png", {.width = 1000, .height = 1000})));
    const RegionId regionId = RegionId::generate();
    QVERIFY(project.addRegionToPage(pageId, makeRegion(regionId)).has_value());
    project.markClean();

    const std::string layerPath = "assets/derived/clear.png";
    const auto result = project.applyClearChangeOnPage(pageId, regionId, layerPath, true);
    QVERIFY(result.has_value());

    const auto* updatedRegion = project.findPage(pageId)->findRegion(regionId);
    QVERIFY(updatedRegion->isCleared());
    QCOMPARE(project.findPage(pageId)->clearLayerAsset().value(), layerPath);
    QVERIFY(project.isDirty());
}

void ComicProjectTest::testApplyClearChangeOnPageRejectsUnknownIds() {
    ComicProject project("Test");
    const PageId pageId = PageId::generate();
    QVERIFY(project.addPage(ComicPage(pageId, "assets/original/page.png", {.width = 1000, .height = 1000})));
    const RegionId regionId = RegionId::generate();
    QVERIFY(project.addRegionToPage(pageId, makeRegion(regionId)).has_value());
    project.markClean();

    const auto missingPage = project.applyClearChangeOnPage(PageId::generate(), regionId, "assets/derived/x.png", true);
    QVERIFY(!missingPage.has_value());
    QCOMPARE(missingPage.error().code, ProjectErrorCode::EditNotFound);

    const auto missingRegion =
        project.applyClearChangeOnPage(pageId, RegionId::generate(), "assets/derived/x.png", true);
    QVERIFY(!missingRegion.has_value());
    QCOMPARE(missingRegion.error().code, ProjectErrorCode::EditNotFound);
    QVERIFY(!project.isDirty());
}

void ComicProjectTest::testInsertRegionOnPageSuccessAndRevision() {
    ComicProject project("Test");
    const PageId pageId = PageId::generate();
    QVERIFY(project.addPage(ComicPage(pageId, "assets/original/page.png", {.width = 1000, .height = 1000})));
    const RegionId r1 = RegionId::generate();
    QVERIFY(project.addRegionToPage(pageId, makeRegion(r1)).has_value());
    project.markClean();
    const auto revBefore = project.revision();

    const RegionId r2 = RegionId::generate();
    TextRegion newRegion(r2, {.x = 200.0, .y = 200.0, .width = 50.0, .height = 50.0});
    const auto result = project.insertRegionOnPage(pageId, std::move(newRegion), 0);
    QVERIFY(result.has_value());
    QVERIFY(project.isDirty());
    QCOMPARE(project.revision(), revBefore + 1);

    const auto* page = project.findPage(pageId);
    QCOMPARE(page->regions().size(), size_t{2});
    QCOMPARE(page->regions()[0].id(), r2);
    QCOMPARE(page->regions()[1].id(), r1);
}

void ComicProjectTest::testInsertRegionOnPageDuplicateIdRejected() {
    ComicProject project("Test");
    const PageId page1Id = PageId::generate();
    const PageId page2Id = PageId::generate();
    QVERIFY(project.addPage(ComicPage(page1Id, "assets/original/page1.png", {.width = 1000, .height = 1000})));
    QVERIFY(project.addPage(ComicPage(page2Id, "assets/original/page2.png", {.width = 1000, .height = 1000})));
    const RegionId sharedId = RegionId::generate();
    QVERIFY(project.addRegionToPage(page1Id, makeRegion(sharedId)).has_value());
    project.markClean();
    const auto revBefore = project.revision();

    TextRegion duplicate(sharedId, {.x = 200.0, .y = 200.0, .width = 50.0, .height = 50.0});
    const auto result = project.insertRegionOnPage(page2Id, std::move(duplicate), 0);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, ProjectErrorCode::DuplicateId);
    QVERIFY(!project.isDirty());
    QCOMPARE(project.revision(), revBefore);
    QCOMPARE(project.findPage(page2Id)->regions().size(), size_t{0});
}

void ComicProjectTest::testInsertRegionOnPageInvalidInputLeavesProjectUnmodified() {
    ComicProject project("Test");
    const PageId pageId = PageId::generate();
    QVERIFY(project.addPage(ComicPage(pageId, "assets/original/page.png", {.width = 1000, .height = 1000})));
    project.markClean();
    const auto revBefore = project.revision();

    // Out-of-range order index
    TextRegion badOrder(RegionId::generate(), {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0});
    const auto result = project.insertRegionOnPage(pageId, std::move(badOrder), 99);
    QVERIFY(!result.has_value());
    QVERIFY(!project.isDirty());
    QCOMPARE(project.revision(), revBefore);
    QCOMPARE(project.findPage(pageId)->regions().size(), size_t{0});
}
