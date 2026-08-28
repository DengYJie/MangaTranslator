// Placeholder test — verifies the test framework boots and the ui target links.
// Replace with real ViewModel State transition and canvas math tests as the layer grows.

#include <QtTest>

class UiInitTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void placeholderInvariant();
};

void UiInitTest::initTestCase() {
    QVERIFY2(true, "UI test framework initialises");
}

void UiInitTest::placeholderInvariant() {
    QVERIFY2(true, "Placeholder invariant holds");
}

QTEST_MAIN(UiInitTest)
#include "ui_init_test.moc"
