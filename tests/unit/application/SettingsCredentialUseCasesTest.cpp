#include "SettingsCredentialUseCasesTest.h"

#include "application/dto/AppSettingsDto.h"
#include "application/ports/IAppSettingsStore.h"
#include "application/ports/ICredentialStore.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "application/usecase/settings/AppSettingsUseCases.h"

#include <QHash>
#include <QMap>
#include <QStringList>
#include <QtTest/QtTest>

using namespace manga::application;

namespace {

class FakeAppSettingsStore final : public IAppSettingsStore {
public:
    [[nodiscard]] std::optional<QVariant> readSetting(const QString& key) const override {
        auto it = m_values.find(key);
        if (it != m_values.end()) {
            return it.value();
        }
        return std::nullopt;
    }

    void writeSetting(const QString& key, const QVariant& value) override {
        m_values[key] = value;
        m_writtenKeys.append(key);
    }

    void removeSetting(const QString& key) override {
        m_values.remove(key);
    }

    [[nodiscard]] const QMap<QString, QVariant>& values() const {
        return m_values;
    }

    [[nodiscard]] const QStringList& writtenKeys() const {
        return m_writtenKeys;
    }

    void clearWrittenKeys() {
        m_writtenKeys.clear();
    }

private:
    QMap<QString, QVariant> m_values;
    QStringList m_writtenKeys;
};

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

/// Every key the typed settings are allowed to persist; nothing here carries credential material.
const QStringList kExpectedSettingKeys = {QStringLiteral("general/restoreLastProject"),
                                          QStringLiteral("general/recentProjectLimit"),
                                          QStringLiteral("ocr/modelDirectory"),
                                          QStringLiteral("ocr/language"),
                                          QStringLiteral("translation/providerId"),
                                          QStringLiteral("translation/endpoint"),
                                          QStringLiteral("translation/modelName"),
                                          QStringLiteral("translation/timeoutSeconds"),
                                          QStringLiteral("translation/retryCount"),
                                          QStringLiteral("appearance/theme"),
                                          QStringLiteral("appearance/density"),
                                          QStringLiteral("appearance/reduceMotion"),
                                          QStringLiteral("appearance/canvasBackground")};

} // namespace

void SettingsCredentialUseCasesTest::loadsDefaultSettingsWhenStoreEmpty() {
    FakeAppSettingsStore store;
    LoadAppSettingsUseCase useCase(&store);

    AppSettingsSnapshot snapshot = useCase.execute();
    AppSettingsSnapshot defaults{};

    QCOMPARE(snapshot.restoreLastProject, defaults.restoreLastProject);
    QCOMPARE(snapshot.recentProjectLimit, defaults.recentProjectLimit);
    QCOMPARE(snapshot.ocrModelDirectory, defaults.ocrModelDirectory);
    QCOMPARE(snapshot.ocrLanguage, defaults.ocrLanguage);
    QCOMPARE(snapshot.providerId, defaults.providerId);
    QCOMPARE(snapshot.endpoint, defaults.endpoint);
    QCOMPARE(snapshot.modelName, defaults.modelName);
    QCOMPARE(snapshot.timeoutSeconds, defaults.timeoutSeconds);
    QCOMPARE(snapshot.retryCount, defaults.retryCount);
    QCOMPARE(snapshot.theme, defaults.theme);
    QCOMPARE(snapshot.density, defaults.density);
    QCOMPARE(snapshot.reduceMotion, defaults.reduceMotion);
    QCOMPARE(snapshot.canvasBackground, defaults.canvasBackground);
}

void SettingsCredentialUseCasesTest::loadsDefaultSettingsWhenStoreNull() {
    LoadAppSettingsUseCase useCase(nullptr);
    AppSettingsSnapshot snapshot = useCase.execute();
    AppSettingsSnapshot defaults{};
    QCOMPARE(snapshot, defaults);
}

void SettingsCredentialUseCasesTest::savesAndLoadsValidSnapshotRoundtrip() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);
    LoadAppSettingsUseCase loadUseCase(&store);

    AppSettingsSnapshot custom;
    custom.restoreLastProject = false;
    custom.recentProjectLimit = 12;
    custom.ocrModelDirectory = QStringLiteral("/opt/models/baberu");
    custom.ocrLanguage = QStringLiteral("zh");
    custom.providerId = QStringLiteral("deepseek");
    custom.endpoint = QStringLiteral("https://api.deepseek.com/v1");
    custom.modelName = QStringLiteral("deepseek-chat");
    custom.timeoutSeconds = 45;
    custom.retryCount = 4;
    custom.theme = AppTheme::Dark;
    custom.density = InterfaceDensity::Comfortable;
    custom.reduceMotion = true;
    custom.canvasBackground = QStringLiteral("#1e1e1e");

    auto saveResult = saveUseCase.execute(custom);
    QVERIFY(saveResult.has_value());

    AppSettingsSnapshot loaded = loadUseCase.execute();
    QCOMPARE(loaded.restoreLastProject, custom.restoreLastProject);
    QCOMPARE(loaded.recentProjectLimit, custom.recentProjectLimit);
    QCOMPARE(loaded.ocrModelDirectory, custom.ocrModelDirectory);
    QCOMPARE(loaded.ocrLanguage, custom.ocrLanguage);
    QCOMPARE(loaded.providerId, custom.providerId);
    QCOMPARE(loaded.endpoint, custom.endpoint);
    QCOMPARE(loaded.modelName, custom.modelName);
    QCOMPARE(loaded.timeoutSeconds, custom.timeoutSeconds);
    QCOMPARE(loaded.retryCount, custom.retryCount);
    QCOMPARE(loaded.theme, custom.theme);
    QCOMPARE(loaded.density, custom.density);
    QCOMPARE(loaded.reduceMotion, custom.reduceMotion);
    QCOMPARE(loaded.canvasBackground, custom.canvasBackground);
}

void SettingsCredentialUseCasesTest::coercesUnknownThemeAndDensityValuesToDefaults() {
    FakeAppSettingsStore store;
    store.writeSetting(QStringLiteral("appearance/theme"), QStringLiteral("rainbow_mode"));
    store.writeSetting(QStringLiteral("appearance/density"), QStringLiteral("super_wide"));

    LoadAppSettingsUseCase loadUseCase(&store);
    AppSettingsSnapshot loaded = loadUseCase.execute();

    QCOMPARE(loaded.theme, AppTheme::System);
    QCOMPARE(loaded.density, InterfaceDensity::Compact);
}

void SettingsCredentialUseCasesTest::clampsOutOfRangePersistedNumbers() {
    FakeAppSettingsStore store;
    store.writeSetting(QStringLiteral("general/recentProjectLimit"), 500);
    store.writeSetting(QStringLiteral("translation/timeoutSeconds"), 0);
    store.writeSetting(QStringLiteral("translation/retryCount"), 99);

    AppSettingsSnapshot loaded = LoadAppSettingsUseCase(&store).execute();

    QCOMPARE(loaded.recentProjectLimit, 50);
    QCOMPARE(loaded.timeoutSeconds, 1);
    QCOMPARE(loaded.retryCount, 10);
}

void SettingsCredentialUseCasesTest::keepsDefaultsForUnreadablePersistedValues() {
    FakeAppSettingsStore store;
    store.writeSetting(QStringLiteral("general/recentProjectLimit"), QStringLiteral("many"));
    store.writeSetting(QStringLiteral("translation/timeoutSeconds"), QStringLiteral("soon"));
    store.writeSetting(QStringLiteral("translation/retryCount"), QVariant(QByteArrayLiteral("nope")));
    store.writeSetting(QStringLiteral("appearance/canvasBackground"), QStringLiteral("not_a_valid_hex_color"));
    store.writeSetting(QStringLiteral("translation/endpoint"), QStringLiteral("ftp://example.invalid"));

    const AppSettingsSnapshot loaded = LoadAppSettingsUseCase(&store).execute();
    const AppSettingsSnapshot defaults{};

    QCOMPARE(loaded, defaults);
}

void SettingsCredentialUseCasesTest::readsTextFormsOfBooleanSettings() {
    FakeAppSettingsStore store;
    store.writeSetting(QStringLiteral("general/restoreLastProject"), QStringLiteral("false"));
    store.writeSetting(QStringLiteral("appearance/reduceMotion"), QStringLiteral("true"));

    const AppSettingsSnapshot loaded = LoadAppSettingsUseCase(&store).execute();

    QVERIFY(!loaded.restoreLastProject);
    QVERIFY(loaded.reduceMotion);
}

void SettingsCredentialUseCasesTest::rejectsInvalidEndpointAndWritesNothing() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.endpoint = QStringLiteral("ftp://not-http-url");

    auto result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_endpoint"));
    QVERIFY(store.writtenKeys().isEmpty());

    snapshot.endpoint = QStringLiteral("not_a_valid_url");
    result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_endpoint"));
    QVERIFY(store.writtenKeys().isEmpty());
}

void SettingsCredentialUseCasesTest::rejectsInvalidTimeoutAndWritesNothing() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.timeoutSeconds = 0;

    auto result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_timeout"));
    QVERIFY(store.writtenKeys().isEmpty());

    snapshot.timeoutSeconds = -10;
    result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_timeout"));
    QVERIFY(store.writtenKeys().isEmpty());

    snapshot.timeoutSeconds = 301;
    result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_timeout"));
    QVERIFY(store.writtenKeys().isEmpty());
}

void SettingsCredentialUseCasesTest::rejectsInvalidRetryCountAndWritesNothing() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.retryCount = -1;

    auto result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_retry_count"));
    QVERIFY(store.writtenKeys().isEmpty());

    snapshot.retryCount = 11;
    result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_retry_count"));
    QVERIFY(store.writtenKeys().isEmpty());
}

void SettingsCredentialUseCasesTest::rejectsInvalidRecentLimitAndWritesNothing() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.recentProjectLimit = 0;

    auto result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_recent_project_limit"));
    QVERIFY(store.writtenKeys().isEmpty());

    snapshot.recentProjectLimit = -5;
    result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_recent_project_limit"));
    QVERIFY(store.writtenKeys().isEmpty());

    snapshot.recentProjectLimit = 51;
    result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_recent_project_limit"));
    QVERIFY(store.writtenKeys().isEmpty());
}

void SettingsCredentialUseCasesTest::rejectsInvalidCanvasBackgroundAndWritesNothing() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.canvasBackground = QStringLiteral("not_a_valid_hex_color");

    auto result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_canvas_background"));
    QVERIFY(store.writtenKeys().isEmpty());
}

void SettingsCredentialUseCasesTest::rejectsEmptyOcrLanguageAndWritesNothing() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.ocrLanguage = QStringLiteral("   ");

    auto result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_ocr_language"));
    QVERIFY(store.writtenKeys().isEmpty());
}

void SettingsCredentialUseCasesTest::rejectsEmptyProviderIdAndWritesNothing() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.providerId = QStringLiteral("   ");

    auto result = saveUseCase.execute(snapshot);
    QVERIFY(!result.has_value());
    QCOMPARE(result.error().code, QStringLiteral("input.invalid_provider_id"));
    QVERIFY(store.writtenKeys().isEmpty());
}

void SettingsCredentialUseCasesTest::allowsEmptyModelDirectory() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);
    LoadAppSettingsUseCase loadUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.ocrModelDirectory = QString();

    auto result = saveUseCase.execute(snapshot);
    QVERIFY(result.has_value());

    AppSettingsSnapshot loaded = loadUseCase.execute();
    QVERIFY(loaded.ocrModelDirectory.isEmpty());
}

void SettingsCredentialUseCasesTest::writtenKeysDoNotContainApiKeyOrSecret() {
    FakeAppSettingsStore store;
    SaveAppSettingsUseCase saveUseCase(&store);

    AppSettingsSnapshot snapshot;
    snapshot.endpoint = QStringLiteral("https://api.example.invalid/v1");
    snapshot.modelName = QStringLiteral("example-chat");
    auto result = saveUseCase.execute(snapshot);
    QVERIFY(result.has_value());

    const QStringList written = store.writtenKeys();
    QCOMPARE(written.size(), kExpectedSettingKeys.size());
    for (const QString& key : kExpectedSettingKeys) {
        QVERIFY2(written.contains(key), qPrintable(QStringLiteral("missing setting key: ") + key));
    }

    static const QStringList kAllowedNamespaces = {QStringLiteral("general/"),
                                                   QStringLiteral("ocr/"),
                                                   QStringLiteral("translation/"),
                                                   QStringLiteral("appearance/")};
    for (const QString& key : written) {
        bool namespaced = false;
        for (const QString& prefix : kAllowedNamespaces) {
            namespaced = namespaced || key.startsWith(prefix);
        }
        QVERIFY2(namespaced, qPrintable(QStringLiteral("key escapes the settings namespaces: ") + key));

        const QString lower = key.toLower();
        for (const QString& forbidden : {QStringLiteral("apikey"),
                                         QStringLiteral("api_key"),
                                         QStringLiteral("credential"),
                                         QStringLiteral("secret"),
                                         QStringLiteral("password"),
                                         QStringLiteral("token")}) {
            QVERIFY2(!lower.contains(forbidden), qPrintable(key + QStringLiteral(" looks like secret storage")));
        }
    }
}

void SettingsCredentialUseCasesTest::rejectsSaveWithoutStoreAndWritesNothing() {
    SaveAppSettingsUseCase saveUseCase(nullptr);

    const auto result = saveUseCase.execute(AppSettingsSnapshot{});

    QVERIFY(!result.has_value());
    QCOMPARE(result.error().category, ErrorCategory::Internal);
    QCOMPARE(result.error().code, QStringLiteral("internal.null_app_settings_store"));
}

void SettingsCredentialUseCasesTest::testCheckCredentialStatus() {
    MockCredentialStore store;
    CheckCredentialStatusUseCase useCase(&store);

    CredentialStatusRequest req;
    req.providerId = QStringLiteral("openai");

    auto res = useCase.execute(req);
    QVERIFY(res.has_value());
    QCOMPARE(res->state, CredentialState::Missing);

    store.store.insert(QStringLiteral("openai"), QByteArray("sk-12345"));

    res = useCase.execute(req);
    QVERIFY(res.has_value());
    QCOMPARE(res->state, CredentialState::Configured);

    // A status query must never read the secret blob.
    QCOMPARE(store.hasCount, 2);
    QCOMPARE(store.readCount, 0);
}

void SettingsCredentialUseCasesTest::testSetCredential() {
    MockCredentialStore store;
    SetCredentialUseCase useCase(&store);

    SetCredentialRequest req;
    req.providerId = QStringLiteral("anthropic");
    req.key = SecretBytes(QByteArray("sk-ant-123"));

    auto res = useCase.execute(std::move(req));
    QVERIFY(res.has_value());
    QCOMPARE(store.store.value(QStringLiteral("anthropic")), QByteArray("sk-ant-123"));
}

void SettingsCredentialUseCasesTest::testClearCredential() {
    MockCredentialStore store;
    store.store.insert(QStringLiteral("gemini"), QByteArray("AIzaSy"));

    ClearCredentialUseCase useCase(&store);
    ClearCredentialRequest req;
    req.providerId = QStringLiteral("gemini");

    auto res = useCase.execute(req);
    QVERIFY(res.has_value());
    QVERIFY(store.store.isEmpty());
}

void SettingsCredentialUseCasesTest::testValidation() {
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
