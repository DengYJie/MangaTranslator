#include "SemanticValidatorTest.h"

#include "domain/model/ComicProject.h"
#include "domain/validation/SemanticValidator.h"

#include <QtTest/QtTest>

using namespace manga::domain;

namespace {

ComicProject createMinimalValidProject() {
    ComicProject project("Valid Project");
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);

    TextRegion region(RegionId::generate(), {.x = 100.0, .y = 200.0, .width = 300.0, .height = 150.0}, 0);
    TextContent source;
    source.setOcrResult("こんにちは", 0.95, "manga-ocr");
    region.setSource(source);

    TranslationEntry trans;
    trans.setModelResult("Hello",
                         {{.id = "c1", .text = "Hello", .confidence = 0.95, .provider = "openai"},
                          {.id = "c2", .text = "Hi", .confidence = 0.85, .provider = "openai"}});
    trans.selectCandidate("c1");
    region.setTranslation(trans);

    page.addRegion(region);
    project.addPage(page);
    return project;
}

} // namespace

void SemanticValidatorTest::testValidProject() {
    const ComicProject project = createMinimalValidProject();
    const auto result = SemanticValidator::validate(project);
    QVERIFY2(result.has_value(), "Expected minimal valid project to pass semantic validation");
}

void SemanticValidatorTest::testInvalidProjectId() {
    // Use nil UUID
    ComicProject invalidProj("Test", ProjectId{Uuid{}});
    ComicPage page(PageId::generate(), "assets/original/page_0001.png", {.width = 1000, .height = 1500}, 0);
    invalidProj.addPage(page);

    const auto result = SemanticValidator::validate(invalidProj);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().front().code, ProjectErrorCode::InvalidId);
}

void SemanticValidatorTest::testEmptyProjectName() {
    ComicProject project = createMinimalValidProject();
    project.setName("");

    const auto result = SemanticValidator::validate(project);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().front().code, ProjectErrorCode::InvalidSemanticData);
}

void SemanticValidatorTest::testUnsupportedVersions() {
    ComicProject project = createMinimalValidProject();
    project.setSchemaVersion(99);

    const auto result = SemanticValidator::validate(project);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().front().code, ProjectErrorCode::UnsupportedSchemaVersion);
}

void SemanticValidatorTest::testDuplicatePageIds() {
    ComicProject project = createMinimalValidProject();
    const PageId dupId = project.pages().front().id();
    ComicPage dupPage(dupId, "assets/original/page_0002.png", {.width = 1000, .height = 1500}, 1);

    // Invariant: addPage rejects duplicate PageId
    QVERIFY(!project.addPage(dupPage));
}

void SemanticValidatorTest::testDuplicateRegionIds() {
    ComicProject project = createMinimalValidProject();
    const RegionId dupRegId = project.pages().front().regions().front().id();

    // Adding duplicate region ID into same page is rejected
    ComicPage samePage = project.pages().front();
    TextRegion dupRegion(dupRegId, {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}, 0);
    QVERIFY(!samePage.addRegion(dupRegion));

    // When duplicate region ID exists across different pages, SemanticValidator catches it
    ComicProject multiPage("Multi");
    ComicPage p1(PageId::generate(), "assets/original/p1.png", {.width = 1000, .height = 1000}, 0);
    p1.addRegion(TextRegion(dupRegId, {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}, 0));
    multiPage.addPage(p1);

    ComicPage p2(PageId::generate(), "assets/original/p2.png", {.width = 1000, .height = 1000}, 1);
    p2.addRegion(TextRegion(dupRegId, {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}, 0));
    multiPage.addPage(p2);

    const auto dupRes = SemanticValidator::validate(multiPage);
    QVERIFY(!dupRes.has_value());
    QCOMPARE(dupRes.error().front().code, ProjectErrorCode::DuplicateId);
}

void SemanticValidatorTest::testDuplicateOrderIndices() {
    ComicProject project = createMinimalValidProject();
    ComicPage page2(PageId::generate(),
                    "assets/original/page_0002.png",
                    {.width = 1000, .height = 1500},
                    0); // Duplicate orderIndex 0

    // addPage normalizes orderIndex, so page2 becomes index 1
    project.addPage(page2);
    QCOMPARE(project.pages()[1].orderIndex(), 1);
}

void SemanticValidatorTest::testOutOfBoundsRegion() {
    ComicPage page(PageId::generate(), "assets/original/p1.png", {.width = 1000, .height = 1500}, 0);

    // Invariant: addRegion rejects out-of-bounds geometry
    TextRegion outOfBounds(RegionId::generate(), {.x = 900.0, .y = 100.0, .width = 200.0, .height = 100.0}, 0);
    QVERIFY(!page.addRegion(outOfBounds));
    QVERIFY(!outOfBounds.geometry().isWithinBounds(page.imageSize()));
}

void SemanticValidatorTest::testInvalidCandidateSelection() {
    TranslationEntry invalidTrans;
    invalidTrans.setModelResult("Test");
    // selectCandidate returns false and does not set selection when candidate ID doesn't exist
    QVERIFY(!invalidTrans.selectCandidate("non_existent_id"));
}

void SemanticValidatorTest::testInvalidLayoutAndClear() {
    TextRegion invalidReg(RegionId::generate(), {.x = 10.0, .y = 10.0, .width = 50.0, .height = 50.0}, 0);
    TextLayout invalidLayout;
    invalidLayout.fontSizePt = -10.0;
    invalidReg.setLayout(invalidLayout);
    QVERIFY(!invalidReg.isValid());

    ComicPage page(PageId::generate(), "assets/original/p1.png", {.width = 1000, .height = 1500}, 0);
    // Invariant: addRegion rejects invalid region
    QVERIFY(!page.addRegion(invalidReg));
}
