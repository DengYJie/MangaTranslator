#include "ApplicationUseCasesTest.h"

#include "application/mapping/ProjectMapper.h"
#include "application/operation/AsyncResult.h"
#include "application/usecase/export/ExportUseCases.h"
#include "application/usecase/import/ImportUseCases.h"
#include "application/usecase/project/ProjectUseCases.h"
#include "application/usecase/region/RegionUseCases.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::domain;

namespace {

class FakeComicExporter : public IComicExporter {
public:
    int callCount{0};
    ExportRequest lastRequest;

    QFuture<Result<ExportResponse>> exportPages(ExportRequest request, OperationContext) override {
        ++callCount;
        lastRequest = request;
        return makeReadyFuture<ExportResponse>(ExportResponse{});
    }
};

ComicProject makeProject(PageId& firstPageId,
                         PageId& secondPageId,
                         RegionId* firstRegionId = nullptr,
                         RegionId* secondRegionId = nullptr) {
    ComicProject project("Phase 2", ProjectId::generate());
    firstPageId = PageId::generate();
    secondPageId = PageId::generate();

    ComicPage firstPage(firstPageId, "first.png", {100, 100});
    if (firstRegionId != nullptr && secondRegionId != nullptr) {
        *firstRegionId = RegionId::generate();
        *secondRegionId = RegionId::generate();
        if (!firstPage.addRegion(TextRegion(*firstRegionId, {0, 0, 10, 10})) ||
            !firstPage.addRegion(TextRegion(*secondRegionId, {20, 20, 10, 10}))) {
            qFatal("Failed to create test regions");
        }
    }
    ComicPage secondPage(secondPageId, "second.png", {100, 100});
    if (!project.addPage(std::move(firstPage)) || !project.addPage(std::move(secondPage))) {
        qFatal("Failed to create test pages");
    }
    return project;
}

ExportPageSnapshot snapshot(PageId pageId) {
    ExportPageSnapshot result;
    result.pageId = pageId;
    result.outputStem = QStringLiteral("page_0001_translated");
    result.sourceImage = QImage(100, 100, QImage::Format_RGBA8888);
    return result;
}

} // namespace

void ApplicationUseCasesTest::testReorderRegionChangesReadingOrder() {
    PageId firstPageId;
    PageId secondPageId;
    RegionId firstRegionId;
    RegionId secondRegionId;
    auto project = makeProject(firstPageId, secondPageId, &firstRegionId, &secondRegionId);
    ReorderRegionUseCase useCase(&project);

    const auto result = useCase.execute({firstPageId, secondRegionId, 0});

    QVERIFY(result.has_value());
    const auto& regions = project.findPage(firstPageId)->regions();
    QCOMPARE(regions[0].id(), secondRegionId);
    QCOMPARE(regions[0].orderIndex(), 0);
    QCOMPARE(regions[1].id(), firstRegionId);
    QCOMPARE(regions[1].orderIndex(), 1);
    QVERIFY(project.isDirty());
}

void ApplicationUseCasesTest::testReorderRegionRejectsInvalidIndex() {
    PageId firstPageId;
    PageId secondPageId;
    RegionId firstRegionId;
    RegionId secondRegionId;
    auto project = makeProject(firstPageId, secondPageId, &firstRegionId, &secondRegionId);
    ReorderRegionUseCase useCase(&project);

    const auto result = useCase.execute({firstPageId, firstRegionId, 2});

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QString("input.invalid_order"));
}

void ApplicationUseCasesTest::testExportResolvesSelectedPages() {
    PageId firstPageId;
    PageId secondPageId;
    auto project = makeProject(firstPageId, secondPageId);
    FakeComicExporter exporter;
    ExportPagesUseCase useCase(&exporter, &project);
    ExportPagesRequest request;
    request.projectId = project.id();
    request.scope = ExportScope::SelectedPages;
    request.outputDir = "output";
    request.pageSnapshots = {snapshot(firstPageId), snapshot(secondPageId)};
    request.selectedPageIds = {secondPageId};

    const auto future = useCase.execute(request, {});

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(exporter.callCount, 1);
    QCOMPARE(exporter.lastRequest.pages.size(), 1);
    QCOMPARE(exporter.lastRequest.pages[0].pageId, secondPageId);
}

void ApplicationUseCasesTest::testExportRejectsMissingSnapshot() {
    PageId firstPageId;
    PageId secondPageId;
    auto project = makeProject(firstPageId, secondPageId);
    FakeComicExporter exporter;
    ExportPagesUseCase useCase(&exporter, &project);
    ExportPagesRequest request;
    request.projectId = project.id();
    request.scope = ExportScope::SelectedPages;
    request.outputDir = "output";
    request.pageSnapshots = {snapshot(firstPageId)};
    request.selectedPageIds = {secondPageId};

    const auto future = useCase.execute(request, {});

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("input.missing_page_snapshot"));
    QCOMPARE(exporter.callCount, 0);
}

void ApplicationUseCasesTest::testExportRejectsInvalidOutputStem() {
    PageId firstPageId;
    PageId secondPageId;
    auto project = makeProject(firstPageId, secondPageId);
    FakeComicExporter exporter;
    ExportPagesUseCase useCase(&exporter, &project);
    ExportPagesRequest request;
    request.projectId = project.id();
    request.scope = ExportScope::SelectedPages;
    request.outputDir = "output";
    auto badSnapshot = snapshot(firstPageId);
    badSnapshot.outputStem = QStringLiteral("bad stem!");
    request.pageSnapshots = {badSnapshot};
    request.selectedPageIds = {firstPageId};

    const auto future = useCase.execute(request, {});

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("export.invalid_output_name"));
    QCOMPARE(exporter.callCount, 0);
}

void ApplicationUseCasesTest::testExportRejectsMismatchedLayer() {
    PageId firstPageId;
    PageId secondPageId;
    auto project = makeProject(firstPageId, secondPageId);
    FakeComicExporter exporter;
    ExportPagesUseCase useCase(&exporter, &project);
    ExportPagesRequest request;
    request.projectId = project.id();
    request.scope = ExportScope::SelectedPages;
    request.outputDir = "output";
    auto badSnapshot = snapshot(firstPageId);
    badSnapshot.clearLayer = QImage(50, 50, QImage::Format_RGBA8888);
    request.pageSnapshots = {badSnapshot};
    request.selectedPageIds = {firstPageId};

    const auto future = useCase.execute(request, {});

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("export.invalid_layer"));
    QCOMPARE(exporter.callCount, 0);
}

void ApplicationUseCasesTest::testProjectMapperProducesQtDto() {
    const ComicProject project("Mapping", ProjectId::generate());

    const auto dto = mapping::ProjectMapper::toDto(project);

    QCOMPARE(dto.id, QString::fromStdString(project.id().toString()));
    QCOMPARE(dto.name, QString("Mapping"));
}
