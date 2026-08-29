#include "CredentialUseCasesTest.h"

#include "application/usecase/credential/CredentialUseCases.h"

using namespace manga::application;

class MockCredentialStore : public ICredentialStore {
public:
    Result<bool> hasApiKey(const QString& providerId) override {
        ++hasCount;
        return store.contains(providerId);
    }

    Result<SecretBytes> readApiKey(const QString& providerId) override {
        ++readCount;
        if (!store.contains(providerId)) {
            return std::unexpected(makeApplicationError(
                ErrorCategory::Internal, QStringLiteral("credential.not_found"), QStringLiteral("Not found")));
        }
        return SecretBytes(store.value(providerId));
    }

    Result<void> writeApiKey(const QString& providerId, SecretBytes key) override {
        store.insert(providerId, key.bytes());
        return {};
    }

    Result<void> deleteApiKey(const QString& providerId) override {
        if (store.remove(providerId) == 0) {
            return std::unexpected(makeApplicationError(
                ErrorCategory::Internal, QStringLiteral("credential.not_found"), QStringLiteral("Not found")));
        }
        return {};
    }

    int hasCount{0};
    int readCount{0};

    QHash<QString, QByteArray> store;
};

void CredentialUseCasesTest::testCheckCredentialStatus() {
    MockCredentialStore store;
    CheckCredentialStatusUseCase useCase(&store);

    CredentialStatusRequest req;
    req.providerId = QStringLiteral("openai");

    auto res = useCase.execute(req);
    QVERIFY(res.has_value());
    QVERIFY(!res->hasCredential);

    store.store.insert(QStringLiteral("openai"), QByteArray("sk-12345"));

    res = useCase.execute(req);
    QVERIFY(res.has_value());
    QVERIFY(res->hasCredential);

    // A status query must never read the secret blob.
    QCOMPARE(store.hasCount, 2);
    QCOMPARE(store.readCount, 0);
}

void CredentialUseCasesTest::testSetCredential() {
    MockCredentialStore store;
    SetCredentialUseCase useCase(&store);

    SetCredentialRequest req;
    req.providerId = QStringLiteral("anthropic");
    req.key = SecretBytes(QByteArray("sk-ant-123"));

    auto res = useCase.execute(std::move(req));
    QVERIFY(res.has_value());
    QCOMPARE(store.store.value(QStringLiteral("anthropic")), QByteArray("sk-ant-123"));
}

void CredentialUseCasesTest::testClearCredential() {
    MockCredentialStore store;
    store.store.insert(QStringLiteral("gemini"), QByteArray("AIzaSy"));

    ClearCredentialUseCase useCase(&store);
    ClearCredentialRequest req;
    req.providerId = QStringLiteral("gemini");

    auto res = useCase.execute(req);
    QVERIFY(res.has_value());
    QVERIFY(store.store.isEmpty());
}

void CredentialUseCasesTest::testValidation() {
    CheckCredentialStatusUseCase check(nullptr);
    auto res1 = check.execute(CredentialStatusRequest{QStringLiteral("prov")});
    QVERIFY(!res1.has_value());
    QCOMPARE(res1.error().category, ErrorCategory::Internal);

    MockCredentialStore store;
    CheckCredentialStatusUseCase check2(&store);
    auto res2 = check2.execute(CredentialStatusRequest{QStringLiteral(" ")});
    QVERIFY(!res2.has_value());
    QCOMPARE(res2.error().category, ErrorCategory::Input);

    SetCredentialUseCase set(&store);
    auto res3 = set.execute(SetCredentialRequest{QStringLiteral("prov"), SecretBytes()});
    QVERIFY(!res3.has_value());
    QCOMPARE(res3.error().category, ErrorCategory::Input);
}
