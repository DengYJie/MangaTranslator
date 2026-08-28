#include "TranslatePageUseCaseTest.h"

#include "FakeTranslator.h"
#include "application/usecase/processing/ProcessingUseCases.h"
#include "domain/model/Uuid.h"

#include <QtTest/QtTest>

using namespace manga::application;
using namespace manga::application::test;
using namespace manga::domain;

namespace {

TranslationRequest validTranslationRequest() {
    TranslationRequest request;
    request.sourceLanguage = languages::Japanese;
    request.targetLanguage = languages::English;
    request.regions.append({RegionId::generate(), "source"});
    return request;
}

} // namespace

void TranslatePageUseCaseTest::testSuccessfulTranslation() {
    FakeTranslator translator;
    OperationRegistry registry;
    TranslatePageUseCase useCase(&translator, &registry);

    auto req = validTranslationRequest();
    ProjectId projectId = ProjectId::generate();
    PageId pageId = PageId::generate();

    translator.nextResponse.items.push_back({RegionId::generate(), "src", "trans", {}});

    OperationId opId;
    auto future = useCase.execute(req, projectId, pageId, 1, opId);

    QVERIFY(future.isFinished());
    QVERIFY(future.result().has_value());
    QCOMPARE(future.result().value().items.size(), 1);
    QCOMPARE(translator.callCount, 1);
    QCOMPARE(registry.inFlightCount(), 0);
}

void TranslatePageUseCaseTest::testTranslatorFailure() {
    FakeTranslator translator;
    OperationRegistry registry;
    translator.shouldFail = true;
    translator.nextError = makeApplicationError(ErrorCategory::Network, "network.timeout", "Timeout");

    TranslatePageUseCase useCase(&translator, &registry);

    auto req = validTranslationRequest();
    OperationId opId;
    auto future = useCase.execute(req, ProjectId::generate(), PageId::generate(), 1, opId);

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Network);
    QCOMPARE(registry.inFlightCount(), 0);
}

void TranslatePageUseCaseTest::testInvalidRequestDoesNotCallTranslator() {
    FakeTranslator translator;
    OperationRegistry registry;
    TranslatePageUseCase useCase(&translator, &registry);
    auto request = validTranslationRequest();
    request.regions.append(request.regions.front());

    OperationId operationId;
    const auto result = useCase.execute(request, ProjectId::generate(), PageId::generate(), 1, operationId).result();

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Input);
    QCOMPARE(translator.callCount, 0);
    QVERIFY(!operationId.isValid());
}

void TranslatePageUseCaseTest::testCancellation() {
    FakeTranslator translator;
    OperationRegistry registry;
    translator.deferred = true;

    TranslatePageUseCase useCase(&translator, &registry);

    auto req = validTranslationRequest();
    OperationId opId;
    auto future = useCase.execute(req, ProjectId::generate(), PageId::generate(), 1, opId);
    QCOMPARE(registry.inFlightCount(), 1);

    registry.cancelOperation(opId);
    translator.complete();

    QVERIFY(future.isFinished());
    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().category, ErrorCategory::Cancelled);
    QCOMPARE(registry.inFlightCount(), 0);
}
