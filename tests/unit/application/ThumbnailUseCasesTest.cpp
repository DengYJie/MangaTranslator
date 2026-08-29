#include "ThumbnailUseCasesTest.h"

#include "application/usecase/thumbnail/ThumbnailUseCases.h"

using namespace manga::application;
using namespace manga::domain;

class MockThumbnailStore : public IThumbnailStore {
public:
    QFuture<Result<QImage>> readThumbnail(ReadThumbnailRequest request, OperationContext) override {
        auto promise = std::make_shared<QPromise<Result<QImage>>>();
        promise->start();
        auto future = promise->future();

        if (shouldFailRead) {
            promise->addResult(std::unexpected(makeApplicationError(
                ErrorCategory::Internal, QStringLiteral("not_found"), QStringLiteral("Not found"))));
        } else {
            promise->addResult(storedImage);
        }
        promise->finish();
        return future;
    }

    QFuture<Result<void>> writeThumbnail(WriteThumbnailRequest request, OperationContext) override {
        storedImage = request.thumbnail;
        auto promise = std::make_shared<QPromise<Result<void>>>();
        promise->start();
        promise->addResult({});
        promise->finish();
        return promise->future();
    }

    bool shouldFailRead{false};
    QImage storedImage;
};

class MockPageImageLoader : public IPageImageLoader {
public:
    QFuture<Result<QImage>> loadPageImage(LoadPageImageRequest request, OperationContext) override {
        auto promise = std::make_shared<QPromise<Result<QImage>>>();
        promise->start();
        auto future = promise->future();

        if (shouldFailLoad) {
            promise->addResult(std::unexpected(makeApplicationError(
                ErrorCategory::Internal, QStringLiteral("load_error"), QStringLiteral("Load error"))));
        } else {
            promise->addResult(loadedImage);
        }
        promise->finish();
        return future;
    }

    bool shouldFailLoad{false};
    QImage loadedImage;
};

void ThumbnailUseCasesTest::testLoadExistingThumbnail() {
    MockThumbnailStore store;
    store.storedImage = QImage(256, 256, QImage::Format_RGB32);

    MockPageImageLoader loader;
    loader.shouldFailLoad = true; // should not be called

    LoadOrGenerateThumbnailUseCase useCase(&store, &loader);

    LoadOrGenerateThumbnailRequest req{ProjectId::generate(),
                                       QStringLiteral("/fake/path"),
                                       PageId::generate(),
                                       QStringLiteral("assets/original/1.jpg"),
                                       QSize(1000, 1000),
                                       QSize(256, 256)};

    OperationContext ctx;
    auto future = useCase.execute(req, ctx);
    future.waitForFinished();

    auto res = future.result();
    QVERIFY(res.has_value());
    QCOMPARE(res->size(), QSize(256, 256));
}

void ThumbnailUseCasesTest::testGenerateAndSaveThumbnail() {
    MockThumbnailStore store;
    store.shouldFailRead = true; // simulate missing

    MockPageImageLoader loader;
    loader.loadedImage = QImage(1000, 2000, QImage::Format_RGB32);

    LoadOrGenerateThumbnailUseCase useCase(&store, &loader);

    LoadOrGenerateThumbnailRequest req{ProjectId::generate(),
                                       QStringLiteral("/fake/path"),
                                       PageId::generate(),
                                       QStringLiteral("assets/original/1.jpg"),
                                       QSize(1000, 2000),
                                       QSize(256, 256)};

    OperationContext ctx;
    auto future = useCase.execute(req, ctx);
    future.waitForFinished();

    auto res = future.result();
    QVERIFY(res.has_value());
    // Scaled preserving aspect ratio: 1000x2000 into 256x256 -> 128x256
    QCOMPARE(res->size(), QSize(128, 256));

    // Check if store was updated
    QCOMPARE(store.storedImage.size(), QSize(128, 256));
}

void ThumbnailUseCasesTest::testGenerateFailsIfLoaderFails() {
    MockThumbnailStore store;
    store.shouldFailRead = true; // simulate missing

    MockPageImageLoader loader;
    loader.shouldFailLoad = true; // simulate failure

    LoadOrGenerateThumbnailUseCase useCase(&store, &loader);

    LoadOrGenerateThumbnailRequest req{ProjectId::generate(),
                                       QStringLiteral("/fake/path"),
                                       PageId::generate(),
                                       QStringLiteral("assets/original/1.jpg"),
                                       QSize(1000, 1000),
                                       QSize(256, 256)};

    OperationContext ctx;
    auto future = useCase.execute(req, ctx);
    future.waitForFinished();

    auto res = future.result();
    QVERIFY(!res.has_value());
    QCOMPARE(res.error().code, QStringLiteral("load_error"));
}

void ThumbnailUseCasesTest::testValidation() {
    MockThumbnailStore store;
    MockPageImageLoader loader;

    LoadOrGenerateThumbnailUseCase useCase(&store, &loader);
    OperationContext ctx;

    // Missing project root
    LoadOrGenerateThumbnailRequest req1{ProjectId::generate(),
                                        QStringLiteral("   "),
                                        PageId::generate(),
                                        QStringLiteral("assets/original/1.jpg"),
                                        QSize(1000, 1000),
                                        QSize(256, 256)};
    auto future1 = useCase.execute(req1, ctx);
    QVERIFY(!future1.result().has_value());
    QCOMPARE(future1.result().error().category, ErrorCategory::Input);

    // Null ports
    LoadOrGenerateThumbnailUseCase nullUseCase(nullptr, nullptr);
    auto future2 = nullUseCase.execute(req1, ctx);
    QVERIFY(!future2.result().has_value());
    QCOMPARE(future2.result().error().category, ErrorCategory::Internal);
}
