#include "LoadPageImageUseCaseTest.h"

#include "FakePageImageLoader.h"
#include "application/usecase/import/ImportUseCases.h"
#include "domain/model/Uuid.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;
using namespace manga::domain;

void LoadPageImageUseCaseTest::testSuccessfulLoad() {
    FakePageImageLoader loader;
    LoadPageImageUseCase useCase(&loader);

    LoadPageImageRequest req;
    req.projectId = ProjectId::generate();
    req.projectRoot = "C:/project";
    req.pageId = PageId::generate();
    req.assetPath = "page_01.png";

    loader.nextImage = QImage(100, 100, QImage::Format_ARGB32);

    OperationContext ctx;
    auto future = useCase.execute(req, ctx);

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(future.result().value().width(), 100);
    QCOMPARE(loader.callCount, 1);
}

void LoadPageImageUseCaseTest::testLoadFailure() {
    FakePageImageLoader loader;
    LoadPageImageUseCase useCase(&loader);

    LoadPageImageRequest req;
    // Empty path should fail

    OperationContext ctx;
    auto future = useCase.execute(req, ctx);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Input);
    QCOMPARE(loader.callCount, 0);
}

void LoadPageImageUseCaseTest::testLoaderFailurePropagates() {
    FakePageImageLoader loader;
    loader.shouldFail = true;
    loader.nextError = makeApplicationError(ErrorCategory::Persistence, "persistence.load_failed", "Image load failed");
    LoadPageImageUseCase useCase(&loader);
    LoadPageImageRequest request;
    request.projectId = ProjectId::generate();
    request.projectRoot = "C:/project";
    request.pageId = PageId::generate();
    request.assetPath = "page.png";

    const auto result = useCase.execute(request, {}).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Persistence);
    QCOMPARE(loader.callCount, 1);
}

void LoadPageImageUseCaseTest::testPreCancelledLoadRejected() {
    FakePageImageLoader loader;
    LoadPageImageUseCase useCase(&loader);
    LoadPageImageRequest request;
    request.projectId = ProjectId::generate();
    request.projectRoot = "C:/project";
    request.pageId = PageId::generate();
    request.assetPath = "page.png";
    std::stop_source stopSource;
    stopSource.request_stop();
    OperationContext context;
    context.stopToken = stopSource.get_token();

    const auto result = useCase.execute(request, context).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Cancelled);
    QCOMPARE(loader.callCount, 0);
}
