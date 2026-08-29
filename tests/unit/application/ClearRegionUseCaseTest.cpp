#include "ClearRegionUseCaseTest.h"

#include "FakeInpainter.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "domain/model/Uuid.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;
using namespace manga::domain;

void ClearRegionUseCaseTest::testSuccessfulClear() {
    FakeInpainter inpainter;
    OperationRegistry registry;
    ClearRegionUseCase useCase(&inpainter, &registry);

    ClearRequest req;
    req.projectId = ProjectId::generate();
    req.pageId = PageId::generate();
    req.regionId = RegionId::generate();
    req.pageImage = QImage(100, 100, QImage::Format_ARGB32);
    req.region = RectGeometry(10, 10, 50, 50);
    req.contentRevision = 10;

    inpainter.nextResponse.regionId = req.regionId;

    OperationId opId;
    auto future = useCase.execute(req, 1, opId);

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(future.result().value().regionId.toString(), req.regionId.toString());
    QCOMPARE(inpainter.callCount, 1);
}

void ClearRegionUseCaseTest::testClearFailure() {
    FakeInpainter inpainter;
    OperationRegistry registry;
    inpainter.shouldFail = true;
    inpainter.nextError = makeApplicationError(ErrorCategory::Inference, "inference.failed", "Failed");

    ClearRegionUseCase useCase(&inpainter, &registry);

    ClearRequest req;
    req.projectId = ProjectId::generate();
    req.pageId = PageId::generate();
    req.regionId = RegionId::generate();
    req.pageImage = QImage(100, 100, QImage::Format_ARGB32);
    req.region = RectGeometry(10, 10, 50, 50);
    req.contentRevision = 10;

    OperationId opId;
    auto future = useCase.execute(req, 1, opId);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Inference);
}

void ClearRegionUseCaseTest::testInvalidRequestDoesNotCallInpainter() {
    FakeInpainter inpainter;
    OperationRegistry registry;
    ClearRegionUseCase useCase(&inpainter, &registry);
    ClearRequest request;
    request.projectId = ProjectId::generate();
    request.pageId = PageId::generate();
    request.regionId = RegionId::generate();
    request.region = {10, 10, 50, 50};

    OperationId operationId;
    const auto result = useCase.execute(request, 1, operationId).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
    QCOMPARE(inpainter.callCount, 0);
    QVERIFY(!operationId.isValid());
}

void ClearRegionUseCaseTest::testDisabledClearingNeverReachesInpainter() {
    FakeInpainter inpainter;
    OperationRegistry registry;
    ClearRegionUseCase useCase(&inpainter, &registry);

    ClearRequest req;
    req.projectId = ProjectId::generate();
    req.pageId = PageId::generate();
    req.regionId = RegionId::generate();
    req.pageImage = QImage(100, 100, QImage::Format_ARGB32);
    req.region = RectGeometry(10, 10, 50, 50);
    req.contentRevision = 10;
    // Disabled clearing must not reach the adapter, and must never fall back to
    // automatic color picking.
    req.settings.enabled = false;

    OperationId opId;
    auto future = useCase.execute(req, 1, opId);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("clear.disabled"));
    QCOMPARE(inpainter.callCount, 0);
    QVERIFY(!opId.isValid());
}

void ClearRegionUseCaseTest::testCancellation() {
    FakeInpainter inpainter;
    OperationRegistry registry;
    inpainter.deferred = true;

    ClearRegionUseCase useCase(&inpainter, &registry);

    ClearRequest req;
    req.projectId = ProjectId::generate();
    req.pageId = PageId::generate();
    req.regionId = RegionId::generate();
    req.pageImage = QImage(100, 100, QImage::Format_ARGB32);
    req.region = RectGeometry(10, 10, 50, 50);
    req.contentRevision = 10;

    OperationId opId;
    auto future = useCase.execute(req, 1, opId);

    registry.cancelOperation(opId);
    inpainter.complete();

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Cancelled);
}
