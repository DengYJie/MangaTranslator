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
