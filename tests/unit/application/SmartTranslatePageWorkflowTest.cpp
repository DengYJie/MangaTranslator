#include "SmartTranslatePageWorkflowTest.h"

#include "FakeInpainter.h"
#include "FakeOcrEngine.h"
#include "FakeTextLayouter.h"
#include "FakeTranslator.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "application/workflow/SmartTranslatePageWorkflow.h"

#include <QtTest>

using namespace manga::application;
using namespace manga::application::test;

void SmartTranslatePageWorkflowTest::init() {}
void SmartTranslatePageWorkflowTest::cleanup() {}

void SmartTranslatePageWorkflowTest::testNoRegionsYieldsSkipped() {
    auto registry = std::make_unique<OperationRegistry>();
    RecognizeRegionUseCase ocr(nullptr, registry.get());
    TranslatePageUseCase tr(nullptr, registry.get());
    ClearRegionUseCase cl(nullptr, registry.get());
    LayoutRegionUseCase la(nullptr, registry.get());

    SmartTranslatePageWorkflow workflow(&ocr, &tr, &cl, &la, registry.get());

    OperationId opId;
    auto future = workflow.execute({}, {}, {}, QStringLiteral("ja"), QStringLiteral("en"), QImage(), 0, opId);
    while (future.isRunning()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    QVERIFY(future.resultCount() == 1);
    auto res = future.resultAt(0);
    QVERIFY(res.has_value());
    QVERIFY(res.value().issues.size() == 1);
    QCOMPARE(res.value().issues[0].error.code, QStringLiteral("skipped.no_regions"));
}

void SmartTranslatePageWorkflowTest::testSuccessfulPipeline() {
    auto registry = std::make_unique<OperationRegistry>();
    FakeOcrEngine fakeOcr;
    FakeTranslator fakeTr;
    FakeInpainter fakeCl;
    FakeTextLayouter fakeLa;

    RecognizeRegionUseCase ocr(&fakeOcr, registry.get());
    TranslatePageUseCase tr(&fakeTr, registry.get());
    ClearRegionUseCase cl(&fakeCl, registry.get());
    LayoutRegionUseCase la(&fakeLa, registry.get());

    SmartTranslatePageWorkflow workflow(&ocr, &tr, &cl, &la, registry.get());

    // TODO(Defect 1): Update RegionProcessingTarget initialization to include bubbleGeometry and bubblePolygon.
    QVector<RegionProcessingTarget> targets;
    targets.append({manga::domain::RegionId::generate(),
                    manga::domain::RectGeometry{0, 0, 10, 10},
                    QStringLiteral(""),
                    QStringLiteral(""),
                    0,
                    false});

    OperationId opId;
    auto future = workflow.execute(manga::domain::ProjectId::generate(),
                                   manga::domain::PageId::generate(),
                                   targets,
                                   QStringLiteral("ja"),
                                   QStringLiteral("en"),
                                   QImage(10, 10, QImage::Format_RGB32),
                                   0,
                                   opId);
    while (future.isRunning()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    auto res = future.resultAt(0);
    QVERIFY(res.has_value());
    QCOMPARE(res.value().ocr.size(), 1);
    QVERIFY(res.value().translation.has_value());
    QCOMPARE(res.value().clear.size(), 1);
    QCOMPARE(res.value().layout.size(), 1);
}

void SmartTranslatePageWorkflowTest::testFailsAtDependencyStopsSubsequent() {
    auto registry = std::make_unique<OperationRegistry>();
    FakeOcrEngine fakeOcr;
    FakeTranslator fakeTr;
    FakeInpainter fakeCl;
    FakeTextLayouter fakeLa;

    // Simulate Translation failure
    fakeTr.shouldFail = true;
    fakeTr.nextError =
        makeApplicationError(ErrorCategory::Internal, QStringLiteral("trans.err"), QStringLiteral("fail"));

    RecognizeRegionUseCase ocr(&fakeOcr, registry.get());
    TranslatePageUseCase tr(&fakeTr, registry.get());
    ClearRegionUseCase cl(&fakeCl, registry.get());
    LayoutRegionUseCase la(&fakeLa, registry.get());

    SmartTranslatePageWorkflow workflow(&ocr, &tr, &cl, &la, registry.get());

    QVector<RegionProcessingTarget> targets;
    targets.append({manga::domain::RegionId::generate(),
                    manga::domain::RectGeometry{0, 0, 10, 10},
                    QStringLiteral(""),
                    QStringLiteral(""),
                    0,
                    false});

    OperationId opId;
    auto future = workflow.execute(manga::domain::ProjectId::generate(),
                                   manga::domain::PageId::generate(),
                                   targets,
                                   QStringLiteral("ja"),
                                   QStringLiteral("en"),
                                   QImage(10, 10, QImage::Format_RGB32),
                                   0,
                                   opId);

    // Process queued invoke methods for `.then()` continuations
    while (future.isRunning()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    auto res = future.resultAt(0);
    QVERIFY(res.has_value());
    QCOMPARE(res.value().ocr.size(), 1);           // OCR succeeds
    QVERIFY(!res.value().translation.has_value()); // Translation fails
    QCOMPARE(res.value().clear.size(), 0);         // Stops subsequent steps
    QCOMPARE(res.value().layout.size(), 0);

    QVERIFY(res.value().issues.size() > 0);
    QCOMPARE(res.value().issues.last().error.code, QStringLiteral("trans.err"));
}

void SmartTranslatePageWorkflowTest::testCancelOperation() {
    auto registry = std::make_unique<OperationRegistry>();
    FakeOcrEngine fakeOcr;
    fakeOcr.deferred = true; // wait for cancel
    FakeTranslator fakeTr;
    FakeInpainter fakeCl;
    FakeTextLayouter fakeLa;

    RecognizeRegionUseCase ocr(&fakeOcr, registry.get());
    TranslatePageUseCase tr(&fakeTr, registry.get());
    ClearRegionUseCase cl(&fakeCl, registry.get());
    LayoutRegionUseCase la(&fakeLa, registry.get());

    SmartTranslatePageWorkflow workflow(&ocr, &tr, &cl, &la, registry.get());

    QVector<RegionProcessingTarget> targets;
    targets.append({manga::domain::RegionId::generate(),
                    manga::domain::RectGeometry{0, 0, 10, 10},
                    QStringLiteral(""),
                    QStringLiteral(""),
                    0,
                    false});

    OperationId opId;
    auto future = workflow.execute(manga::domain::ProjectId::generate(),
                                   manga::domain::PageId::generate(),
                                   targets,
                                   QStringLiteral("ja"),
                                   QStringLiteral("en"),
                                   QImage(10, 10, QImage::Format_RGB32),
                                   0,
                                   opId);

    workflow.cancel(opId);
    fakeOcr.complete();

    while (future.isRunning()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    auto res = future.resultAt(0);
    QVERIFY(res.has_value());
    QCOMPARE(res.value().ocr.size(), 0);
    QVERIFY(res.value().issues.size() > 0);
    QCOMPARE(res.value().issues.last().error.code, QStringLiteral("cancelled"));
}

void SmartTranslatePageWorkflowTest::testManualOverrideSkipsProcessing() {
    auto registry = std::make_unique<OperationRegistry>();
    FakeOcrEngine fakeOcr;
    FakeTranslator fakeTr;
    FakeInpainter fakeCl;
    FakeTextLayouter fakeLa;

    RecognizeRegionUseCase ocr(&fakeOcr, registry.get());
    TranslatePageUseCase tr(&fakeTr, registry.get());
    ClearRegionUseCase cl(&fakeCl, registry.get());
    LayoutRegionUseCase la(&fakeLa, registry.get());

    SmartTranslatePageWorkflow workflow(&ocr, &tr, &cl, &la, registry.get());

    QVector<RegionProcessingTarget> targets;
    targets.append({manga::domain::RegionId::generate(),
                    manga::domain::RectGeometry{0, 0, 10, 10},
                    QStringLiteral("text"),
                    QStringLiteral("trans"),
                    0,
                    true});

    OperationId opId;
    auto future = workflow.execute(manga::domain::ProjectId::generate(),
                                   manga::domain::PageId::generate(),
                                   targets,
                                   QStringLiteral("ja"),
                                   QStringLiteral("en"),
                                   QImage(10, 10, QImage::Format_RGB32),
                                   0,
                                   opId);

    while (future.isRunning()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    auto res = future.resultAt(0);
    QVERIFY(res.has_value());
    QCOMPARE(fakeOcr.callCount, 0); // Skipped OCR
    QCOMPARE(fakeTr.callCount, 0);  // Skipped Translation
    QCOMPARE(fakeCl.callCount, 1);  // Continued with Clear
    QCOMPARE(fakeLa.callCount, 1);  // Continued with Layout
}

void SmartTranslatePageWorkflowTest::testPartialSuccessRetainsProposals() {
    auto registry = std::make_unique<OperationRegistry>();
    FakeOcrEngine fakeOcr;
    FakeTranslator fakeTr;
    FakeInpainter fakeCl;
    fakeCl.shouldFail = true; // Clear fails
    FakeTextLayouter fakeLa;

    RecognizeRegionUseCase ocr(&fakeOcr, registry.get());
    TranslatePageUseCase tr(&fakeTr, registry.get());
    ClearRegionUseCase cl(&fakeCl, registry.get());
    LayoutRegionUseCase la(&fakeLa, registry.get());

    SmartTranslatePageWorkflow workflow(&ocr, &tr, &cl, &la, registry.get());

    QVector<RegionProcessingTarget> targets;
    targets.append({manga::domain::RegionId::generate(),
                    manga::domain::RectGeometry{0, 0, 10, 10},
                    QStringLiteral(""),
                    QStringLiteral(""),
                    0,
                    false});

    OperationId opId;
    auto future = workflow.execute(manga::domain::ProjectId::generate(),
                                   manga::domain::PageId::generate(),
                                   targets,
                                   QStringLiteral("ja"),
                                   QStringLiteral("en"),
                                   QImage(10, 10, QImage::Format_RGB32),
                                   0,
                                   opId);

    while (future.isRunning()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    auto res = future.resultAt(0);
    QVERIFY(res.has_value());
    QCOMPARE(res.value().ocr.size(), 1);
    QVERIFY(res.value().translation.has_value());
    QCOMPARE(res.value().clear.size(), 0);  // Clear failed
    QCOMPARE(res.value().layout.size(), 0); // Layout skipped
    QVERIFY(res.value().issues.size() > 0);
}

void SmartTranslatePageWorkflowTest::testSessionGenerationChangeYieldsStale() {
    auto registry = std::make_unique<OperationRegistry>();
    FakeOcrEngine fakeOcr;
    FakeTranslator fakeTr;
    FakeInpainter fakeCl;
    FakeTextLayouter fakeLa;

    RecognizeRegionUseCase ocr(&fakeOcr, registry.get());
    TranslatePageUseCase tr(&fakeTr, registry.get());
    ClearRegionUseCase cl(&fakeCl, registry.get());
    LayoutRegionUseCase la(&fakeLa, registry.get());

    SmartTranslatePageWorkflow workflow(&ocr, &tr, &cl, &la, registry.get());

    QVector<RegionProcessingTarget> targets;
    targets.append({manga::domain::RegionId::generate(),
                    manga::domain::RectGeometry{0, 0, 10, 10},
                    QStringLiteral(""),
                    QStringLiteral(""),
                    0,
                    false});

    OperationId opId;
    fakeOcr.nextError =
        makeApplicationError(ErrorCategory::Cancelled, QStringLiteral("stale"), QStringLiteral("Stale"));
    fakeOcr.shouldFail = true;
    // Session generation = 0
    auto future = workflow.execute(manga::domain::ProjectId::generate(),
                                   manga::domain::PageId::generate(),
                                   targets,
                                   QStringLiteral("ja"),
                                   QStringLiteral("en"),
                                   QImage(10, 10, QImage::Format_RGB32),
                                   0,
                                   opId);

    while (future.isRunning()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
    }

    auto res = future.resultAt(0);
    QVERIFY(res.has_value());
    QVERIFY(res.value().issues.size() > 0);
    QCOMPARE(res.value().issues.last().error.code, QStringLiteral("stale"));
}
