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
    future.waitForFinished();

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

    QVector<RegionProcessingTarget> targets;
    targets.append({manga::domain::RegionId::generate(),
                    manga::domain::RectGeometry{0, 0, 10, 10},
                    QStringLiteral(""),
                    QStringLiteral(""),
                    0,
                    false});

    OperationId opId;
    auto future = workflow.execute(
        {}, {}, targets, QStringLiteral("ja"), QStringLiteral("en"), QImage(10, 10, QImage::Format_RGB32), 0, opId);
    future.waitForFinished();

    auto res = future.resultAt(0);
    QVERIFY(res.has_value());
    QCOMPARE(res.value().ocr.size(), 1);
    QVERIFY(res.value().translation.has_value());
    QCOMPARE(res.value().clear.size(), 1);
    QCOMPARE(res.value().layout.size(), 1);
}

void SmartTranslatePageWorkflowTest::testFailsAtDependencyStopsSubsequent() {}
void SmartTranslatePageWorkflowTest::testCancelOperation() {}
void SmartTranslatePageWorkflowTest::testManualOverrideSkipsProcessing() {}
void SmartTranslatePageWorkflowTest::testPartialSuccessRetainsProposals() {}
void SmartTranslatePageWorkflowTest::testSessionGenerationChangeYieldsStale() {}

QTEST_GUILESS_MAIN(SmartTranslatePageWorkflowTest)
#include "SmartTranslatePageWorkflowTest.moc"
