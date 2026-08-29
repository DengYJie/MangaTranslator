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
    PageId pageIdA{PageId::generate()};
    PageId pageIdB{PageId::generate()};

    ExportFixture() {
        if (!project.addPage(ComicPage(pageIdA, "page_a.png", {100, 100})) ||
            !project.addPage(ComicPage(pageIdB, "page_b.png", {100, 100}))) {
            qFatal("Failed to create export fixture");
        }
    }

    [[nodiscard]] ExportPageSnapshot snapshot(const PageId& id, const QString& stem) const {
        ExportPageSnapshot snapshot;
        snapshot.pageId = id;
        snapshot.outputStem = stem;
        snapshot.sourceImage = QImage(100, 100, QImage::Format_RGBA8888);
        return snapshot;
    }

    [[nodiscard]] ExportPagesRequest request() const {
        ExportPagesRequest request;
        request.projectId = project.id();
        request.outputDir = "output";
        request.scope = ExportScope::SelectedPages;
        request.selectedPageIds = {pageIdA};
        request.pageSnapshots = {snapshot(pageIdA, QStringLiteral("page_0001_translated"))};
        return request;
    }
};

} // namespace

void ExportPagesUseCaseTest::testSuccessfulExportForwardsFilteredRequest() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    exporter.nextResponse.exported.append({fixture.pageIdA, "output/page_0001.png"});

    const auto future = useCase.execute(request, {});

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(exporter.callCount, 1);
    // The port must receive exactly the filtered/validated snapshot with all options forwarded.
    QCOMPARE(exporter.lastRequest.pages.size(), 1);
    QCOMPARE(exporter.lastRequest.pages[0].pageId, fixture.pageIdA);
    QCOMPARE(exporter.lastRequest.pages[0].outputStem, QStringLiteral("page_0001_translated"));
    QCOMPARE(exporter.lastRequest.outputDir, QStringLiteral("output"));
    QVERIFY(exporter.lastRequest.format == ExportFormat::Png);
    QCOMPARE(exporter.lastRequest.jpegQuality, 95);
}

void ExportPagesUseCaseTest::testSelectedScopeFiltersSnapshots() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    // Provide both snapshots but only select page A: page B must be filtered out.
    request.pageSnapshots = {fixture.snapshot(fixture.pageIdA, QStringLiteral("a")),
                             fixture.snapshot(fixture.pageIdB, QStringLiteral("b"))};

    const auto future = useCase.execute(request, {});

    QVERIFY(future.result().has_value());
    QCOMPARE(exporter.lastRequest.pages.size(), 1);
    QCOMPARE(exporter.lastRequest.pages[0].pageId, fixture.pageIdA);
}

void ExportPagesUseCaseTest::testCurrentPageScopeRequiresCurrentPageId() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    request.scope = ExportScope::CurrentPage;
    request.selectedPageIds.clear();

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.missing_current_page"));
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testCurrentPageScopeExportsOnlyCurrentPage() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    request.scope = ExportScope::CurrentPage;
    request.currentPageId = fixture.pageIdB;
    request.selectedPageIds.clear();
    request.pageSnapshots = {fixture.snapshot(fixture.pageIdA, QStringLiteral("a")),
                             fixture.snapshot(fixture.pageIdB, QStringLiteral("b"))};

    const auto future = useCase.execute(request, {});

    QVERIFY(future.result().has_value());
    QCOMPARE(exporter.lastRequest.pages.size(), 1);
    QCOMPARE(exporter.lastRequest.pages[0].pageId, fixture.pageIdB);
}

void ExportPagesUseCaseTest::testAllPagesScopeRequiresAllSnapshots() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    request.scope = ExportScope::AllPages;
    request.selectedPageIds.clear();
    // Project has 2 pages but only 1 snapshot is provided.
    request.pageSnapshots = {fixture.snapshot(fixture.pageIdA, QStringLiteral("a"))};

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.missing_page_snapshot"));
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testMissingSnapshotForSelectedPage() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    // Select both pages but provide only one snapshot.
    request.selectedPageIds = {fixture.pageIdA, fixture.pageIdB};
    request.pageSnapshots = {fixture.snapshot(fixture.pageIdA, QStringLiteral("a"))};

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.missing_page_snapshot"));
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testInvalidOutputStemRejected() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    request.pageSnapshots = {fixture.snapshot(fixture.pageIdA, QStringLiteral("x."))};

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("export.invalid_output_name"));
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testInvalidLayerRejected() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    auto snapshot = fixture.snapshot(fixture.pageIdA, QStringLiteral("a"));
    snapshot.clearLayer = QImage(50, 50, QImage::Format_RGBA8888);
    request.pageSnapshots = {snapshot};

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("export.invalid_layer"));
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testProjectMismatchRejected() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    request.projectId = ProjectId::generate();

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("project.mismatch"));
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testQualityOutOfRangeRejected() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);

    auto low = fixture.request();
    low.jpegQuality = 0;
    auto lowResult = useCase.execute(low, {}).result();
    QVERIFY(!lowResult.has_value());
    QCOMPARE(lowResult.error().code, QStringLiteral("input.invalid_export"));

    auto high = fixture.request();
    high.jpegQuality = 101;
    auto highResult = useCase.execute(high, {}).result();
    QVERIFY(!highResult.has_value());
    QCOMPARE(highResult.error().code, QStringLiteral("input.invalid_export"));
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testDuplicateSnapshotRejected() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    auto request = fixture.request();
    request.pageSnapshots = {fixture.snapshot(fixture.pageIdA, QStringLiteral("a")),
                             fixture.snapshot(fixture.pageIdA, QStringLiteral("a"))};

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_page_snapshot"));
    QCOMPARE(exporter.callCount, 0);
}

void ExportPagesUseCaseTest::testCancelledBeforeStart() {
    FakeComicExporter exporter;
    ExportFixture fixture;
    ExportPagesUseCase useCase(&exporter, &fixture.project);
    std::stop_source stopSource;
    stopSource.request_stop();
    OperationContext context;
    context.stopToken = stopSource.get_token();

    const auto result = useCase.execute(fixture.request(), context).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("cancelled.before_start"));
    QCOMPARE(exporter.callCount, 0);
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
    QCOMPARE(future.result().error().code, QStringLiteral("input.invalid_export"));
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
