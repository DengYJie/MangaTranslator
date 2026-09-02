#include "QtTextLayouterTest.h"

#include "services/execution/BoundedTaskExecutor.h"
#include "services/layout/QtTextLayouter.h"

#include <QSignalSpy>
#include <QTest>

using namespace manga;

void QtTextLayouterTest::testBasicLayout() {
    services::BoundedTaskExecutor executor(1);
    services::QtTextLayouter layouter(&executor);

    application::LayoutRequest req;
    // The result regionId is only compared against the request one, so a default identifier is enough.
    req.text = "Hello Manga";
    req.bounds = {0, 0, 200, 200};
    req.logicalDpi = 96.0;

    application::OperationContext ctx;

    auto future = layouter.layout(req, ctx);
    future.waitForFinished();

    QVERIFY(future.result().has_value());
    auto res = future.result().value();
    QVERIFY(res.regionId == req.regionId);
}

void QtTextLayouterTest::testCancellation() {
    services::BoundedTaskExecutor executor(1);
    services::QtTextLayouter layouter(&executor);

    application::LayoutRequest req;
    req.text = "Cancel me";
    req.bounds = {0, 0, 100, 100};

    std::stop_source stopSource;
    application::OperationContext ctx;
    ctx.stopToken = stopSource.get_token();
    stopSource.request_stop();

    auto future = layouter.layout(req, ctx);
    future.waitForFinished();

    QVERIFY(!future.result().has_value());
    QCOMPARE(future.result().error().code, QStringLiteral("cancelled.user_requested"));
}
