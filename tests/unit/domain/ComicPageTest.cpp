#include "ComicPageTest.h"

#include "domain/model/ComicPage.h"
#include "domain/model/RehydrationBuilder.h"

#include <QtTest/QtTest>

using namespace manga::domain;

void ComicPageTest::testPageCreationAndDefaults() {
    const PageId id = PageId::generate();
    const ImageSize size{.width = 1000, .height = 1500};
    const ComicPage page(id, "assets/original/page_0001.png", size, 0);

    QCOMPARE(page.id(), id);
    QCOMPARE(QString::fromStdString(page.originalAssetPath()), "assets/original/page_0001.png");
    QCOMPARE(page.imageSize().width, 1000);
    QCOMPARE(page.imageSize().height, 1500);
    QCOMPARE(page.orderIndex(), 0);
    QVERIFY(page.regions().empty());
    QVERIFY(page.isValid());
}

void ComicPageTest::testAddAndRemoveRegion() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);

    const RegionId r1Id = RegionId::generate();
    TextRegion r1(r1Id, {.x = 10.0, .y = 10.0, .width = 100.0, .height = 100.0}, 0);

    QVERIFY(page.addRegion(r1));
    QCOMPARE(page.regions().size(), size_t{1});
    QVERIFY(page.hasRegion(r1Id));

    // Adding duplicate region ID returns false
    TextRegion r1Duplicate(r1Id, {.x = 50.0, .y = 50.0, .width = 50.0, .height = 50.0}, 1);
    QVERIFY(!page.addRegion(r1Duplicate));
    QCOMPARE(page.regions().size(), size_t{1});

    // Remove region
    QVERIFY(page.removeRegion(r1Id));
    QCOMPARE(page.regions().size(), size_t{0});
    QVERIFY(!page.hasRegion(r1Id));
}

void ComicPageTest::testCreationBoundaryRejection() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);

    // Out of bounds creation (x + width = 1050 > 1000) must be rejected
    TextRegion outOfBounds(RegionId::generate(), {.x = 950.0, .y = 100.0, .width = 100.0, .height = 100.0});
    QVERIFY(!page.addRegion(outOfBounds));
    QCOMPARE(page.regions().size(), size_t{0});

    // Invalid geometry (< 2x2) must be rejected
    TextRegion tooSmall(RegionId::generate(), {.x = 10.0, .y = 10.0, .width = 1.5, .height = 100.0});
    QVERIFY(!page.addRegion(tooSmall));
    QCOMPARE(page.regions().size(), size_t{0});
}

void ComicPageTest::testUpdateRegionGeometryClamping() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);
    const RegionId r1Id = RegionId::generate();
    page.addRegion(TextRegion(r1Id, {.x = 100.0, .y = 100.0, .width = 200.0, .height = 200.0}));

    // Edit geometry overflowing page boundaries -> safely clamped
    const auto clamped = page.updateRegionGeometry(r1Id, {.x = 900.0, .y = 1400.0, .width = 200.0, .height = 200.0});
    QVERIFY(clamped.has_value());
    QCOMPARE(clamped->x, 900.0);
    QCOMPARE(clamped->y, 1400.0);
    QCOMPARE(clamped->width, 100.0);
    QCOMPARE(clamped->height, 100.0);

    const auto* r = page.findRegion(r1Id);
    QVERIFY(r != nullptr);
    QCOMPARE(r->geometry().width, 100.0);
}

void ComicPageTest::testLoadRegionPreservesOrderIndex() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);

    // loadRegion with correct orderIndex == 0 (first slot) succeeds
    TextRegion r0(RegionId::generate(), {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}, 0);
    QVERIFY(PageRehydrationBuilder(page).loadRegion(r0));
    QCOMPARE(page.regions()[0].orderIndex(), 0);

    // loadRegion with correct orderIndex == 1 (second slot) succeeds
    TextRegion r1(RegionId::generate(), {.x = 70.0, .y = 10.0, .width = 50.0, .height = 50.0}, 1);
    QVERIFY(PageRehydrationBuilder(page).loadRegion(r1));
    QCOMPARE(page.regions()[1].orderIndex(), 1);
}

void ComicPageTest::testLoadRegionRejectsWrongOrderIndex() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);

    // loadRegion with orderIndex = 5 when page is empty (expected 0) must be rejected
    TextRegion rBad(RegionId::generate(), {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}, 5);
    QVERIFY(!PageRehydrationBuilder(page).loadRegion(rBad));
    QCOMPARE(page.regions().size(), size_t{0});

    // loadRegion with orderIndex = 1 when page is empty (expected 0) must be rejected
    TextRegion rSkip(RegionId::generate(), {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}, 1);
    QVERIFY(!PageRehydrationBuilder(page).loadRegion(rSkip));
    QCOMPARE(page.regions().size(), size_t{0});
}

void ComicPageTest::testExplicitUpdateRegionSource() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);
    const RegionId rId = RegionId::generate();
    page.addRegion(TextRegion(rId, {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}));

    const uint64_t rev0 = page.findRegion(rId)->contentRevision();

    TextContent src;
    src.text = "Hello";
    src.sourceType = TextSourceType::PureManual;
    const auto res = page.updateRegionSource(rId, src);
    QVERIFY(res.has_value());

    const auto* r = page.findRegion(rId);
    QCOMPARE(QString::fromStdString(r->source().text), "Hello");
    QVERIFY(r->contentRevision() > rev0);
}

void ComicPageTest::testExplicitUpdateRegionTranslation() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);
    const RegionId rId = RegionId::generate();
    page.addRegion(TextRegion(rId, {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}));

    const uint64_t rev0 = page.findRegion(rId)->contentRevision();

    TranslationEntry trans;
    trans.setManualText("翻訳テスト");
    const auto res = page.updateRegionTranslation(rId, trans);
    QVERIFY(res.has_value());

    const auto* r = page.findRegion(rId);
    QCOMPARE(QString::fromStdString(r->translation().effectiveText()), "翻訳テスト");
    QVERIFY(r->contentRevision() > rev0);
}

void ComicPageTest::testExplicitUpdateRegionLayout() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);
    const RegionId rId = RegionId::generate();
    page.addRegion(TextRegion(rId, {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}));

    // Valid layout update
    TextLayout layout;
    layout.fontFamily = "Arial";
    layout.fontSizePt = 14.0;
    QVERIFY(layout.isValid());
    const auto res = page.updateRegionLayout(rId, layout);
    QVERIFY(res.has_value());
    QCOMPARE(QString::fromStdString(page.findRegion(rId)->layout().fontFamily), "Arial");

    // Invalid layout (fontSizePt <= 0) must be rejected
    TextLayout badLayout;
    badLayout.fontSizePt = 0.0;
    QVERIFY(!badLayout.isValid());
    const auto badRes = page.updateRegionLayout(rId, badLayout);
    QVERIFY(!badRes.has_value());
    QVERIFY(badRes.error().code == ProjectErrorCode::EditLayoutRejected);
}

void ComicPageTest::testSetRegionFlagsDontNeedUpdateCallback() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);
    const RegionId rId = RegionId::generate();
    page.addRegion(TextRegion(rId, {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}));

    QVERIFY(page.setRegionEnabled(rId, false));
    QVERIFY(!page.findRegion(rId)->isEnabled());

    QVERIFY(page.setRegionLocked(rId, true));
    QVERIFY(page.findRegion(rId)->isLocked());

    QVERIFY(page.setRegionTranslationVisible(rId, false));
    QVERIFY(!page.findRegion(rId)->isTranslationVisible());

    QVERIFY(page.setRegionIsCleared(rId, true));
    QVERIFY(page.findRegion(rId)->isCleared());

    // Non-existent region ID returns false for all flag setters
    const RegionId fakeId = RegionId::generate();
    QVERIFY(!page.setRegionEnabled(fakeId, true));
    QVERIFY(!page.setRegionLocked(fakeId, true));
    QVERIFY(!page.setRegionTranslationVisible(fakeId, true));
    QVERIFY(!page.setRegionIsCleared(fakeId, true));
}

void ComicPageTest::testNormalizeRegionOrder() {
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);

    const RegionId r1 = RegionId::generate();
    const RegionId r2 = RegionId::generate();
    const RegionId r3 = RegionId::generate();

    page.addRegion(TextRegion(r1, {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}));
    page.addRegion(TextRegion(r2, {.x = 70.0, .y = 10.0, .width = 50.0, .height = 50.0}));
    page.addRegion(TextRegion(r3, {.x = 130.0, .y = 10.0, .width = 50.0, .height = 50.0}));

    QCOMPARE(page.regions()[0].orderIndex(), 0);
    QCOMPARE(page.regions()[1].orderIndex(), 1);
    QCOMPARE(page.regions()[2].orderIndex(), 2);

    // After removing r2, indices should automatically re-normalize
    page.removeRegion(r2);
    QCOMPARE(page.regions().size(), size_t{2});
    QCOMPARE(page.regions()[0].orderIndex(), 0);
    QCOMPARE(page.regions()[1].orderIndex(), 1);
}

void ComicPageTest::testImageReplacementInvariant() {
    const ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);

    // Matching dimensions allowed
    QVERIFY(page.canReplaceOriginalImage({.width = 1000, .height = 1500}));

    // Mismatched dimensions rejected
    QVERIFY(!page.canReplaceOriginalImage({.width = 1000, .height = 1600}));
    QVERIFY(!page.canReplaceOriginalImage({.width = 800, .height = 1500}));
}
