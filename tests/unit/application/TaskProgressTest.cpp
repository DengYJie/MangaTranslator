#include "TaskProgressTest.h"

#include "application/operation/TaskProgress.h"

#include <QtTest/QtTest>

using namespace manga::application;

namespace {

struct Harness {
    std::vector<qint64> published;
    bool throttleOpen{true};
    TaskProgress progress = TaskProgress([](qint64, qint64) {},
                                         [this](qint64 value) { published.push_back(value); },
                                         [](const QString&) {},
                                         [this]() { return throttleOpen; });
};

} // namespace

void TaskProgressTest::testPublishesThroughFacade() {
    Harness harness;
    qint64 reportedFrom = 0;
    qint64 reportedTo = 0;
    harness.progress = TaskProgress(
        [&](qint64 from, qint64 to) {
            reportedFrom = from;
            reportedTo = to;
        },
        [&](qint64 value) { harness.published.push_back(value); },
        [](const QString&) {},
        []() { return true; });

    harness.progress.setRange(0, 10);
    harness.progress.setValue(4);

    QCOMPARE(reportedFrom, qint64(0));
    QCOMPARE(reportedTo, qint64(10));
    QVERIFY(harness.published == std::vector<qint64>{4});
}

void TaskProgressTest::testThrottleSuppressesIntermediateValues() {
    Harness harness;
    harness.throttleOpen = false;
    harness.progress.setRange(0, 10);

    harness.progress.setValue(3);
    harness.progress.setValue(4);

    QVERIFY(harness.published.empty());
}

void TaskProgressTest::testFinalValueBypassesThrottle() {
    Harness harness;
    harness.throttleOpen = false;
    harness.progress.setRange(0, 10);

    harness.progress.setValue(9);  // Suppressed: throttled and not final.
    harness.progress.setValue(10); // Final: published even with the throttle closed.

    QVERIFY(harness.published == std::vector<qint64>{10});
}

void TaskProgressTest::testDecreasingValuesAreClamped() {
    Harness harness;
    harness.progress.setRange(0, 10);

    harness.progress.setValue(7);
    harness.progress.setValue(3);
    harness.progress.setValue(9);

    QVERIFY((harness.published == std::vector<qint64>{7, 7, 9}));
}

void TaskProgressTest::testNoRangeIsNeverFinal() {
    Harness harness;
    harness.throttleOpen = false;

    // Without a range there is no "final" value, so the throttle always decides.
    harness.progress.setValue(5);

    QVERIFY(harness.published.empty());
}

#include "moc_TaskProgressTest.cpp"
