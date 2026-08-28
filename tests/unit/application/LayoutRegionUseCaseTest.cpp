#include "LayoutRegionUseCaseTest.h"

#include "FakeTextLayouter.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "domain/model/Uuid.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;
using namespace manga::domain;

void LayoutRegionUseCaseTest::testSuccessfulLayout() {
    FakeTextLayouter layouter;
    OperationRegistry registry;
    LayoutRegionUseCase useCase(&layouter, &registry);

    LayoutRequest req;
    ProjectId projectId = ProjectId::generate();
    req.pageId = PageId::generate();
    req.regionId = RegionId::generate();
    req.text = "Hello";
    req.bounds = RectGeometry(10, 10, 50, 50);
    req.constraints.fontSizePt = 12.0;
    req.constraints.lineSpacing = 1.0;
    req.logicalDpi = 96.0;
    req.contentRevision = 10;

    layouter.nextResponse.regionId = req.regionId;

    OperationId opId;
    auto future = useCase.execute(req, projectId, 1, opId);

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(future.result().value().regionId.toString(), req.regionId.toString());
    QCOMPARE(layouter.callCount, 1);
}

void LayoutRegionUseCaseTest::testLayoutFailure() {
    FakeTextLayouter layouter;
    OperationRegistry registry;
    layouter.shouldFail = true;
    layouter.nextError = makeApplicationError(ErrorCategory::Internal, "internal.layout", "Failed");

    LayoutRegionUseCase useCase(&layouter, &registry);

    LayoutRequest req;
    ProjectId projectId = ProjectId::generate();
    req.pageId = PageId::generate();
    req.regionId = RegionId::generate();
    req.text = "Hello";
    req.bounds = RectGeometry(10, 10, 50, 50);
    req.constraints.fontSizePt = 12.0;
    req.constraints.lineSpacing = 1.0;
    req.logicalDpi = 96.0;
    req.contentRevision = 10;

    OperationId opId;
    auto future = useCase.execute(req, projectId, 1, opId);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Internal);
}

void LayoutRegionUseCaseTest::testInvalidRequestDoesNotCallLayouter() {
    FakeTextLayouter layouter;
    OperationRegistry registry;
    LayoutRegionUseCase useCase(&layouter, &registry);
    LayoutRequest request;
    request.pageId = PageId::generate();
    request.regionId = RegionId::generate();
    request.bounds = {10, 10, 50, 50};
    request.constraints.fontSizePt = 12.0;
    request.constraints.lineSpacing = 1.0;
    request.logicalDpi = 96.0;

    OperationId operationId;
    const auto result = useCase.execute(request, ProjectId::generate(), 1, operationId).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
    QCOMPARE(layouter.callCount, 0);
    QVERIFY(!operationId.isValid());
}

void LayoutRegionUseCaseTest::testCancellation() {
    FakeTextLayouter layouter;
    OperationRegistry registry;
    layouter.deferred = true;

    LayoutRegionUseCase useCase(&layouter, &registry);

    LayoutRequest req;
    ProjectId projectId = ProjectId::generate();
    req.pageId = PageId::generate();
    req.regionId = RegionId::generate();
    req.text = "Hello";
    req.bounds = RectGeometry(10, 10, 50, 50);
    req.constraints.fontSizePt = 12.0;
    req.constraints.lineSpacing = 1.0;
    req.logicalDpi = 96.0;
    req.contentRevision = 10;

    OperationId opId;
    auto future = useCase.execute(req, projectId, 1, opId);

    registry.cancelOperation(opId);
    layouter.complete();

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Cancelled);
}
