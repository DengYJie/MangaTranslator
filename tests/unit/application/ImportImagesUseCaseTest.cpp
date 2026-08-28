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
    req.filePaths = {"img_10.png", "img_2.png", "img_1.png"};

    // Fake response
    importer.nextResponse.importedPages.push_back({PageId::generate(), "page_0001.png"});

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
    request.filePaths = {"folder/ＦＯＯ.png", "folder/foo.png"};

    OperationContext context;
    context.operationId = OperationId::generate();
    const auto future = useCase.execute(request, context);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QString("input.invalid_import_path"));
    QCOMPARE(importer.callCount, 0);
}

void ImportImagesUseCaseTest::testImporterFailurePropagates() {
    FakeComicImporter importer;
    importer.shouldFail = true;
    importer.nextError = makeApplicationError(ErrorCategory::Persistence, "persistence.import_failed", "Import failed");
    ImportImagesUseCase useCase(&importer);
    ImportImagesRequest request;
    request.projectId = ProjectId::generate();
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
