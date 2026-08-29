#include "ExportPagesUseCaseTest.h"

#include "FakeComicExporter.h"
#include "application/usecase/export/ExportUseCases.h"
#include "domain/model/ComicProject.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;
using namespace manga::domain;

namespace {

struct ExportFixture {
    ComicProject project{"Test", ProjectId::generate()};
    PageId pageId{PageId::generate()};

    ExportFixture() {
        if (!project.addPage(ComicPage(pageId, "page.png", {100, 100}))) {
            qFatal("Failed to create export fixture");
        }
    }

    [[nodiscard]] ExportPagesRequest request() const {
        ExportPagesRequest request;
        request.projectId = project.id();
        request.outputDir = "output";
        request.scope = ExportScope::SelectedPages;
        request.selectedPageIds = {pageId};
        ExportPageSnapshot snapshot;
        snapshot.pageId = pageId;
        snapshot.outputStem = QStringLiteral("page_0001_translated");
        snapshot.sourceImage = QImage(100, 100, QImage::Format_RGBA8888);
        request.pageSnapshots.append(snapshot);
        return request;
    }
};

} // namespace

void ExportPagesUseCaseTest::testSuccessfulExport() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    exporter.nextResponse.exported.append({fixture.pageId, "output/page_0001.png"});

    const auto future = useCase.execute(request, {});

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(exporter.callCount, 1);
}

void ExportPagesUseCaseTest::testInvalidOptionsRejected() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    request.outputDir.clear();

    const auto future = useCase.execute(request, {});

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Input);
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testExporterFailurePropagates() {
    FakeComicExporter exporter;
    exporter.shouldFail = true;
    exporter.nextError = makeApplicationError(ErrorCategory::Export, "export.failed", "Export failed");
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);

    const auto result = useCase.execute(fixture.request(), {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Export);
    QCOMPARE(exporter.callCount, 1);
}

void ExportPagesUseCaseTest::testCancellation() {
    FakeComicExporter exporter;
    exporter.deferred = true;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    const auto request = fixture.request();
    std::stop_source stopSource;
    OperationContext context;
    context.stopToken = stopSource.get_token();

    const auto future = useCase.execute(request, context);
    stopSource.request_stop();
    exporter.complete();

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Cancelled);
}
