#include "TextUseCasesTest.h"

#include "application/usecase/text/TextUseCases.h"
#include "domain/model/ComicProject.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::domain;

void TextUseCasesTest::testSuccessfulSourceTextEdit() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");

    TextRegion region(RegionId::generate());
    region.setGeometry({10, 10, 100, 50});
    region.setSource(TextContent{"Original"});
    page.addRegion(region);
    project.addPage(page);

    EditSourceTextUseCase useCase(&project);
    EditSourceTextRequest req;
    req.pageId = page.id();
    req.regionId = region.id();
    req.text = "New Source";
    req.expectedRevision = region.contentRevision();

    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    const auto* projPage = project.findPage(page.id());
    QCOMPARE(QString::fromStdString(projPage->regions().front().source().text), QString("New Source"));
}

void TextUseCasesTest::testSourceTextEditFailsOnStaleRevision() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");

    TextRegion region(RegionId::generate());
    region.setGeometry({10, 10, 100, 50});
    region.setSource(TextContent{"Original"});
    page.addRegion(region);
    project.addPage(page);

    EditSourceTextUseCase useCase(&project);
    EditSourceTextRequest req;
    req.pageId = page.id();
    req.regionId = region.id();
    req.text = "New Source";
    req.expectedRevision = region.contentRevision() - 1; // Stale

    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
}

void TextUseCasesTest::testSuccessfulTranslationEdit() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");

    TextRegion region(RegionId::generate());
    region.setGeometry({10, 10, 100, 50});
    TranslationEntry entry;
    entry.setManualText("Trans");
    region.setTranslation(entry);
    page.addRegion(region);
    project.addPage(page);

    EditTranslationUseCase useCase(&project);
    EditTranslationRequest req;
    req.pageId = page.id();
    req.regionId = region.id();
    req.translation = "New Trans";
    req.expectedRevision = region.contentRevision();

    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    const auto* projPage = project.findPage(page.id());
    QCOMPARE(QString::fromStdString(projPage->regions().front().translation().effectiveText()), QString("New Trans"));
}

void TextUseCasesTest::testTranslationEditFailsOnStaleRevision() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");

    TextRegion region(RegionId::generate());
    region.setGeometry({10, 10, 100, 50});
    page.addRegion(region);
    project.addPage(page);

    EditTranslationUseCase useCase(&project);
    EditTranslationRequest req;
    req.pageId = page.id();
    req.regionId = region.id();
    req.translation = "New Trans";
    req.expectedRevision = region.contentRevision() - 1;

    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
}

void TextUseCasesTest::testSuccessfulCandidateSelection() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");

    TextRegion region(RegionId::generate());
    region.setGeometry({10, 10, 100, 50});
    TranslationEntry entry;
    TranslationCandidate cand;
    cand.id = "cand-1";
    cand.text = "Cand Trans";
    entry.addCandidate(cand);
    region.setTranslation(entry);
    page.addRegion(region);
    project.addPage(page);

    SelectCandidateUseCase useCase(&project);
    SelectCandidateRequest req;
    req.pageId = page.id();
    req.regionId = region.id();
    req.candidateId = "cand-1";
    req.expectedRevision = region.contentRevision();

    auto result = useCase.execute(req);
    QVERIFY(result.has_value());

    const auto* projPage = project.findPage(page.id());
    QCOMPARE(QString::fromStdString(projPage->regions().front().translation().selectedCandidateId().value_or("")),
             QString("cand-1"));
    QCOMPARE(QString::fromStdString(projPage->regions().front().translation().effectiveText()), QString("Cand Trans"));
}

void TextUseCasesTest::testCandidateSelectionFailsOnInvalidCandidate() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");

    TextRegion region(RegionId::generate());
    region.setGeometry({10, 10, 100, 50});
    TranslationEntry entry;
    TranslationCandidate cand;
    cand.id = "cand-1";
    entry.addCandidate(cand);
    region.setTranslation(entry);
    page.addRegion(region);
    project.addPage(page);

    SelectCandidateUseCase useCase(&project);
    SelectCandidateRequest req;
    req.pageId = page.id();
    req.regionId = region.id();
    req.candidateId = "cand-2"; // Unknown
    req.expectedRevision = region.contentRevision();

    auto result = useCase.execute(req);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
}

void TextUseCasesTest::testCandidateSelectionFailsOnStaleRevision() {
    ComicProject project("Test", ProjectId::generate());
    ComicPage page(PageId::generate());
    page.setImageSize({1000, 1000});
    page.setOriginalAssetPath("test.png");

    TextRegion region(RegionId::generate());
    region.setGeometry({10, 10, 100, 50});
    TranslationEntry entry;
    entry.addCandidate({.id = "cand-1", .text = "Candidate"});
    region.setTranslation(entry);
    page.addRegion(region);
    project.addPage(page);

    SelectCandidateUseCase useCase(&project);
    SelectCandidateRequest request;
    request.pageId = page.id();
    request.regionId = region.id();
    request.candidateId = "cand-1";
    request.expectedRevision = region.contentRevision() - 1;

    const auto result = useCase.execute(request);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QString("input.stale_revision"));
}
