// Placeholder test — verifies the contract test target links.
// Contract tests validate that repository, provider, importer, and model-adapter
// implementations satisfy their interface contracts. Replace with real contract
// tests as ports and adapters are implemented.

#include <QtTest>

class ContractInitTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void placeholderInvariant();
};

void ContractInitTest::initTestCase() {
    QVERIFY2(true, "Contract test framework initialises");
}

void ContractInitTest::placeholderInvariant() {
    QVERIFY2(true, "Placeholder invariant holds");
}

QTEST_MAIN(ContractInitTest)
#include "contract_init_test.moc"
