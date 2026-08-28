#include "ImportImagesUseCaseTest.h"

#include "FakeComicImporter.h"
#include "application/usecase/import/ImportUseCases.h"
#include "domain/model/Uuid.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;
using namespace manga::domain;

void ImportImagesUseCaseTest::testSuccessfulImportAndSorting() {
    FakeComicImporter importer;
    ImportImagesUseCase useCase(&importer);

    ImportImagesRequest req;
    req.projectId = ProjectId::generate();
    req.projectRoot = "C:/project";
    req.filePaths = {"img_10.png", "img_2.png", "img_1.png"};

    importer.nextResponse.importedPages.push_back(
        {ComicPage(PageId::generate(), "assets/original/page_0001.png", {100, 100}), "img_1.png", {}});

    OperationContext ctx;
    ctx.operationId = OperationId::generate();
    auto future = useCase.execute(req, ctx);

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());

    // Verify sorting (1, 2, 10 instead of 1, 10, 2)
    QCOMPARE(importer.lastRequest.filePaths.size(), 3);
    QCOMPARE(importer.lastRequest.filePaths[0], QString("img_1.png"));
    QCOMPARE(importer.lastRequest.filePaths[1], QString("img_2.png"));
    QCOMPARE(importer.lastRequest.filePaths[2], QString("img_10.png"));
}

void ImportImagesUseCaseTest::testDuplicatePathsRejected() {
    FakeComicImporter importer;
    ImportImagesUseCase useCase(&importer);

    ImportImagesRequest req;
    req.projectId = ProjectId::generate();
    req.projectRoot = "C:/project";
    req.filePaths = {"img_1.png", "img_1.png"};

    OperationContext ctx;
    ctx.operationId = OperationId::generate();
    auto future = useCase.execute(req, ctx);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Input);
    QCOMPARE(importer.callCount, 0); // Port should not be called
}

void ImportImagesUseCaseTest::testEquivalentPathsRejected() {
    FakeComicImporter importer;
    ImportImagesUseCase useCase(&importer);

    ImportImagesRequest request;
    request.projectId = ProjectId::generate();
    request.projectRoot = "C:/project";
    request.filePaths = {"folder/ＦＯＯ.png", "folder/foo.png"};

    OperationContext context;
    context.operationId = OperationId::generate();
    const auto future = useCase.execute(request, context);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("input.duplicate_import_path"));
    QCOMPARE(importer.callCount, 0);
}

void ImportImagesUseCaseTest::testImporterFailurePropagates() {
    FakeComicImporter importer;
    importer.shouldFail = true;
    importer.nextError = makeApplicationError(ErrorCategory::Persistence, "persistence.import_failed", "Import failed");
    ImportImagesUseCase useCase(&importer);
    ImportImagesRequest request;
    request.projectId = ProjectId::generate();
    request.projectRoot = "C:/project";
    request.filePaths = {"page.png"};

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Persistence);
    QCOMPARE(importer.callCount, 1);
}

void ImportImagesUseCaseTest::testCancellation() {
    FakeComicImporter importer;
    importer.deferred = true;
    ImportImagesUseCase useCase(&importer);

    ImportImagesRequest req;
    req.projectId = ProjectId::generate();
    req.projectRoot = "C:/project";
    req.filePaths = {"img_1.png"};

    std::stop_source stopSource;
    OperationContext ctx;
    ctx.operationId = OperationId::generate();
    ctx.stopToken = stopSource.get_token();

    auto future = useCase.execute(req, ctx);
    stopSource.request_stop();
    importer.complete();

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Cancelled);
}

void ImportImagesUseCaseTest::testApplyImportResultAddsPages() {
    ComicProject project("Test", ProjectId::generate());
    project.markClean();
    ApplyImportResultUseCase useCase(&project);
    ApplyImportResultRequest request;
    request.projectId = project.id();
    request.generation = 3;
    request.currentGeneration = 3;
    const auto firstId = PageId::generate();
    const auto secondId = PageId::generate();
    request.result.importedPages.append(
        {ComicPage(firstId, "assets/original/page_0001.png", {100, 100}), "first.png", {}});
    request.result.importedPages.append(
        {ComicPage(secondId, "assets/original/page_0002.png", {200, 100}), "second.png", {}});

    const auto result = useCase.execute(request);

    QVERIFY(result.has_value());
    QCOMPARE(result->size(), 2);
    QCOMPARE(project.pages().size(), 2);
    QCOMPARE(project.pages()[0].id(), firstId);
    QCOMPARE(project.pages()[0].orderIndex(), 0);
    QCOMPARE(project.pages()[1].id(), secondId);
    QCOMPARE(project.pages()[1].orderIndex(), 1);
    QVERIFY(project.isDirty());
}

void ImportImagesUseCaseTest::testApplyImportResultRejectsStaleGeneration() {
    ComicProject project("Test", ProjectId::generate());
    ApplyImportResultUseCase useCase(&project);
    ApplyImportResultRequest request;
    request.projectId = project.id();
    request.generation = 1;
    request.currentGeneration = 2;
    request.result.importedPages.append(
        {ComicPage(PageId::generate(), "assets/original/page_0001.png", {100, 100}), "page.png", {}});

    const auto result = useCase.execute(request);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QString("cancelled.stale_generation"));
    QVERIFY(project.pages().empty());
}

void ImportImagesUseCaseTest::testApplyImportResultRollsBackInvalidBatch() {
    ComicProject project("Test", ProjectId::generate());
    project.markClean();
    ApplyImportResultUseCase useCase(&project);
    ApplyImportResultRequest request;
    request.projectId = project.id();
    request.generation = 1;
    request.currentGeneration = 1;
    const auto duplicateId = PageId::generate();
    request.result.importedPages.append(
        {ComicPage(duplicateId, "assets/original/page_0001.png", {100, 100}), "first.png", {}});
    request.result.importedPages.append(
        {ComicPage(duplicateId, "assets/original/page_0002.png", {100, 100}), "second.png", {}});

    const auto result = useCase.execute(request);

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QString("protocol.invalid_import_result"));
    QVERIFY(project.pages().empty());
    QVERIFY(!project.isDirty());
}

void ImportImagesUseCaseTest::testDiscardImportedAssetsValidatesAndDispatches() {
    FakeComicImporter importer;
    DiscardImportedAssetsUseCase useCase(&importer);
    DiscardImportedAssetsRequest request;
    request.projectId = ProjectId::generate();
    request.projectRoot = "C:/project";
    request.assetPaths = {"assets/original/page_0001.png"};

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(result.has_value());
    QCOMPARE(importer.discardCallCount, 1);
    QCOMPARE(importer.lastDiscardRequest.assetPaths.front(), QString("assets/original/page_0001.png"));

    request.assetPaths = {"assets/original/../../project.json"};
    const auto invalidResult = useCase.execute(request, {}).result();
    QVERIFY(!invalidResult.has_value());
    QCOMPARE(invalidResult.error().code, QString("input.invalid_import_cleanup"));
    QCOMPARE(importer.discardCallCount, 1);
}
