#include "WindowsCredentialStoreTest.h"

#include "network/credentials/WindowsCredentialStore.h"

#include <QUuid>
#include <QtTest/QtTest>

using namespace manga;
using manga::network::WindowsCredentialStore;

void WindowsCredentialStoreTest::init() {
    // A random provider id keeps the integration test target unique and never touches a real
    // provider credential. The id has to match the provider id rules.
    m_providerId = QUuid::createUuid().toString(QUuid::WithoutBraces).remove(QLatin1Char('-')).toLower();
}

void WindowsCredentialStoreTest::cleanup() {
    WindowsCredentialStore store;
    const auto deleted = store.deleteApiKey(m_providerId);
    QVERIFY2(deleted.has_value(), "the integration test credential must be removable");
}

void WindowsCredentialStoreTest::roundTripStoresReadsAndDeletes() {
    WindowsCredentialStore store;

    const auto before = store.hasApiKey(m_providerId);
    QVERIFY(before.has_value());
    QVERIFY(!*before);

    const QByteArray secret = QByteArrayLiteral("sk-integration-test");
    QVERIFY(store.writeApiKey(m_providerId, application::SecretBytes(secret)).has_value());

    const auto after = store.hasApiKey(m_providerId);
    QVERIFY(after.has_value());
    QVERIFY(*after);

    const auto read = store.readApiKey(m_providerId);
    QVERIFY(read.has_value());
    QCOMPARE(read->bytes(), secret);

    QVERIFY(store.deleteApiKey(m_providerId).has_value());
    const auto gone = store.hasApiKey(m_providerId);
    QVERIFY(gone.has_value());
    QVERIFY(!*gone);
}

void WindowsCredentialStoreTest::overwritingReplacesTheKeyImmediately() {
    WindowsCredentialStore store;
    QVERIFY(store.writeApiKey(m_providerId, application::SecretBytes(QByteArrayLiteral("first"))).has_value());
    QVERIFY(store.writeApiKey(m_providerId, application::SecretBytes(QByteArrayLiteral("second"))).has_value());

    const auto read = store.readApiKey(m_providerId);
    QVERIFY(read.has_value());
    QCOMPARE(read->bytes(), QByteArrayLiteral("second"));
}

void WindowsCredentialStoreTest::deleteIsIdempotent() {
    WindowsCredentialStore store;
    const auto first = store.deleteApiKey(m_providerId);
    QVERIFY(first.has_value());
    const auto second = store.deleteApiKey(m_providerId);
    QVERIFY(second.has_value());
}

void WindowsCredentialStoreTest::rejectsInvalidProviderId() {
    WindowsCredentialStore store;
    for (const QString& bad : {QString(), QStringLiteral("OpenAI"), QStringLiteral("bad id")}) {
        QVERIFY2(!store.hasApiKey(bad).has_value(), qPrintable(QStringLiteral("provider '%1' was accepted").arg(bad)));
        QVERIFY(!store.readApiKey(bad).has_value());
        QVERIFY(!store.writeApiKey(bad, application::SecretBytes(QByteArrayLiteral("x"))).has_value());
        QVERIFY(!store.deleteApiKey(bad).has_value());
    }
}

void WindowsCredentialStoreTest::rejectsEmptyAndNullContainingKeys() {
    WindowsCredentialStore store;
    const auto empty = store.writeApiKey(m_providerId, application::SecretBytes(QByteArray()));
    QVERIFY(!empty.has_value());
    QCOMPARE(empty.error().code, QStringLiteral("input.empty_credential"));

    QByteArray withNull = QByteArrayLiteral("key");
    withNull.append('\0');
    const auto nullKey = store.writeApiKey(m_providerId, application::SecretBytes(withNull));
    QVERIFY(!nullKey.has_value());
    QCOMPARE(nullKey.error().code, QStringLiteral("input.invalid_credential"));
}

#include "moc_WindowsCredentialStoreTest.cpp"
