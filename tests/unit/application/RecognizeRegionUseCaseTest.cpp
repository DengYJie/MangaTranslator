#include "RecognizeRegionUseCaseTest.h"

#include "FakeOcrEngine.h"
#include "application/usecase/processing/ProcessingUseCases.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;

namespace {

OcrRequest validRequest() {
    OcrRequest request;
    request.pageId = manga::domain::PageId::generate();
    request.regionId = manga::domain::RegionId::generate();
    request.image = QImage(32, 32, QImage::Format_RGBA8888);
    request.rect = {0.0, 0.0, 16.0, 16.0};
    request.language = manga::domain::languages::Japanese;
    return request;
}

} // namespace

void RecognizeRegionUseCaseTest::testSuccessfulRecognition() {
    FakeOcrEngine engine;
    OperationRegistry registry;
    engine.nextResponse.text = "Hello";
    engine.nextResponse.confidence = 0.9;
    RecognizeRegionUseCase useCase(&engine, &registry);

    OperationId operationId;
    const auto future = useCase.execute(validRequest(), manga::domain::ProjectId::generate(), 1, operationId);

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(future.result()->text, QString("Hello"));
    QVERIFY(operationId.isValid());
    QCOMPARE(engine.callCount, 1);
    QCOMPARE(registry.inFlightCount(), 0);
}

void RecognizeRegionUseCaseTest::testCancellation() {
    FakeOcrEngine engine;
    engine.deferred = true;
    OperationRegistry registry;
    RecognizeRegionUseCase useCase(&engine, &registry);

    OperationId operationId;
    const auto future = useCase.execute(validRequest(), manga::domain::ProjectId::generate(), 1, operationId);
    QVERIFY(!future.isFinished());
    QCOMPARE(registry.inFlightCount(), 1);

    useCase.cancel(operationId);
    engine.complete();
    QCOMPARE(registry.inFlightCount(), 0);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Cancelled);
    QVERIFY(future.result().error().isCancelled());
    QCOMPARE(future.result().error().recovery, RecoveryAction::None);
    QVERIFY(!future.result().error().retryable);
}

void RecognizeRegionUseCaseTest::testEngineFailure() {
    FakeOcrEngine engine;
    engine.shouldFail = true;
    engine.nextError =
        makeApplicationError(ErrorCategory::Inference, "inference.failed", "OCR failed", RecoveryAction::Retry, true);
    OperationRegistry registry;
    RecognizeRegionUseCase useCase(&engine, &registry);

    OperationId operationId;
    const auto future = useCase.execute(validRequest(), manga::domain::ProjectId::generate(), 1, operationId);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Inference);
    QCOMPARE(registry.inFlightCount(), 0);
}

void RecognizeRegionUseCaseTest::testInvalidRequestDoesNotCallEngine() {
    FakeOcrEngine engine;
    OperationRegistry registry;
    RecognizeRegionUseCase useCase(&engine, &registry);
    auto request = validRequest();
    request.image = {};

    OperationId operationId;
    const auto future = useCase.execute(request, manga::domain::ProjectId::generate(), 1, operationId);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Input);
    QCOMPARE(engine.callCount, 0);
    QVERIFY(!operationId.isValid());
}
