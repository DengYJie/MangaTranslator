// Placeholder test — verifies the test framework boots and the application target links.
// Replace with real UseCase success/failure/cancellation tests as the layer grows.

#include <QtTest>

class ApplicationInitTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void placeholderInvariant();
};

void ApplicationInitTest::initTestCase()
{
    QVERIFY2(true, "Application test framework initialises");
}

void ApplicationInitTest::placeholderInvariant()
{
    QVERIFY2(true, "Placeholder invariant holds");
}

QTEST_MAIN(ApplicationInitTest)
#include "application_init_test.moc"
