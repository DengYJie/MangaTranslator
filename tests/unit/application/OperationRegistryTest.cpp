#include "OperationRegistryTest.h"

#include "application/workflow/OperationRegistry.h"

#include <QtTest/QtTest>

using namespace manga;
using namespace manga::application;

namespace {

OperationDescriptor descriptorFor(const domain::ProjectId& projectId) {
    return OperationDescriptor{
        .stage = OperationStage::Ocr,
        .projectId = projectId,
        .pageId = domain::PageId::generate(),
        .displayTarget = QStringLiteral("Page 1"),
    };
}

} // namespace

void OperationRegistryTest::createsAndCompletesOperation() {
    OperationRegistry registry;
    QSignalSpy changed(&registry, &OperationRegistry::operationsChanged);
    const auto context = registry.createOperation(descriptorFor(domain::ProjectId::generate()), 3, 7);

    QCOMPARE(registry.snapshot().size(), 1);
    QCOMPARE(registry.snapshot().front().state, OperationState::Queued);

    registry.markRunning(context.operationId);
    registry.markFinished(context.operationId);

    const auto operations = registry.snapshot();
    QCOMPARE(operations.size(), 1);
    QCOMPARE(operations.front().state, OperationState::Completed);
    QCOMPARE(changed.count(), 3);
}

void OperationRegistryTest::cancelsOnlyMatchingProject() {
    OperationRegistry registry;
    const auto firstProject = domain::ProjectId::generate();
    const auto secondProject = domain::ProjectId::generate();
    const auto first = registry.createOperation(descriptorFor(firstProject), 1, 1);
    const auto second = registry.createOperation(descriptorFor(secondProject), 1, 1);

    registry.cancelProject(firstProject);

    const auto operations = registry.snapshot();
    const auto firstSnapshot = std::find_if(
        operations.cbegin(), operations.cend(), [&first](const auto& entry) { return entry.id == first.operationId; });
    const auto secondSnapshot = std::find_if(operations.cbegin(), operations.cend(), [&second](const auto& entry) {
        return entry.id == second.operationId;
    });
    QVERIFY(first.stopToken.stop_requested());
    QVERIFY(!second.stopToken.stop_requested());
    QCOMPARE(firstSnapshot->state, OperationState::Cancelling);
    QCOMPARE(secondSnapshot->state, OperationState::Queued);
}

void OperationRegistryTest::retainsAtMostThirtyTwoSettledOperations() {
    OperationRegistry registry;
    for (int index = 0; index < 34; ++index) {
        const auto context = registry.createOperation(descriptorFor(domain::ProjectId::generate()), 1, 1);
        registry.markFinished(context.operationId);
    }

    QCOMPARE(registry.snapshot().size(), 32);
}

void OperationRegistryTest::skippedCarriesReasonCode() {
    OperationRegistry registry;
    const auto context = registry.createOperation(descriptorFor(domain::ProjectId::generate()), 1, 1);

    registry.markSkipped(context.operationId, QStringLiteral("no_regions"));

    const auto operations = registry.snapshot();
    QCOMPARE(operations.size(), 1);
    QCOMPARE(operations.front().state, OperationState::Skipped);
    QCOMPARE(operations.front().reasonCode, QStringLiteral("no_regions"));
}

void OperationRegistryTest::staleStateIsDistinct() {
    OperationRegistry registry;
    const auto context = registry.createOperation(descriptorFor(domain::ProjectId::generate()), 1, 1);

    registry.markStale(context.operationId);

    const auto operations = registry.snapshot();
    QCOMPARE(operations.size(), 1);
    QCOMPARE(operations.front().state, OperationState::Stale);
    QVERIFY(operations.front().reasonCode.startsWith(QStringLiteral("stale.")));
}

void OperationRegistryTest::progressIsMonotonic() {
    OperationRegistry registry;
    const auto context = registry.createOperation(descriptorFor(domain::ProjectId::generate()), 1, 1);
    registry.markRunning(context.operationId);

    context.progressReporter->setRange(100);
    QTest::qWait(55); // wait for throttle
    context.progressReporter->setValue(50);

    auto ops = registry.snapshot();
    QCOMPARE(ops.size(), 1);
    QCOMPARE(ops.front().completed, 50);

    // attempt to reverse progress
    context.progressReporter->setValue(40);
    QTest::qWait(55);
    context.progressReporter->setValue(40);

    ops = registry.snapshot();
    QCOMPARE(ops.front().completed, 50); // should ignore lower values
}

void OperationRegistryTest::safeErrorHandling() {
    OperationRegistry registry;
    const auto context = registry.createOperation(descriptorFor(domain::ProjectId::generate()), 1, 1);

    ApplicationError error =
        makeApplicationError(ErrorCategory::Internal, QStringLiteral("code"), QStringLiteral("msg"));
    context.progressReporter->markFinished(error);

    auto ops = registry.snapshot();
    QCOMPARE(ops.front().state, OperationState::Failed);
    QVERIFY(ops.front().error.has_value());
    QCOMPARE(ops.front().error->code, QStringLiteral("code"));
}

void OperationRegistryTest::crossThreadPublication() {
    OperationRegistry registry;
    const auto context = registry.createOperation(descriptorFor(domain::ProjectId::generate()), 1, 1);

    QSignalSpy spy(&registry, &OperationRegistry::operationsChanged);

    QThread* thread = QThread::create([&]() {
        context.progressReporter->setRange(100);
        context.progressReporter->setValue(100);
        context.progressReporter->markFinished(std::nullopt);
    });
    thread->start();
    thread->wait();
    delete thread;

    // The publisher should queue updates to the registry thread
    QCoreApplication::processEvents();

    auto ops = registry.snapshot();
    QCOMPARE(ops.front().state, OperationState::Completed);
    QCOMPARE(ops.front().completed, 100);
    QVERIFY(spy.count() > 0);
}

#include "moc_OperationRegistryTest.cpp"
