#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "ui/screen/settings/SettingsViewModel.h"
#include "unit/application/FakeCredentialStore.h"

#include <QSignalSpy>
#include <QTest>

namespace manga::ui::screen::settings {

namespace {

class FakeAppSettingsStore : public ::manga::application::IAppSettingsStore {
public:
    std::optional<QVariant> readSetting(const QString& key) const override {
        auto it = m_values.constFind(key);
        if (it != m_values.cend()) {
            return it.value();
        }
        return std::nullopt;
    }

    void writeSetting(const QString& key, const QVariant& value) override {
        m_values.insert(key, value);
    }

    void removeSetting(const QString& key) override {
        m_values.remove(key);
    }

    QHash<QString, QVariant> m_values;
};

class FakeOcrModelValidator : public ::manga::application::IOcrModelValidator {
public:
    bool shouldFail{false};

    ::manga::application::Result<::manga::application::OcrModelStatus>
    validate(const QString& modelDirectory) override {
        if (shouldFail) {
            return std::unexpected(
                ::manga::application::makeApplicationError(::manga::application::ErrorCategory::Model,
                                                           QStringLiteral("ocr.invalid"),
                                                           QStringLiteral("Invalid OCR model")));
        }
        if (modelDirectory.isEmpty()) {
            return std::unexpected(
                ::manga::application::makeApplicationError(::manga::application::ErrorCategory::Input,
                                                           QStringLiteral("ocr.empty"),
                                                           QStringLiteral("Empty directory")));
        }
        ::manga::application::OcrModelStatus status;
        status.isValid = true;
        status.id = QStringLiteral("baberu-onnx");
        status.version = QStringLiteral("1.0.0");
        status.opset = 17;
        status.modelDirectory = modelDirectory;
        return status;
    }
};

} // namespace

class SettingsViewModelTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testInitialObserveReplay();
    void testGeneralEditingPersistsImmediately();
    void testOcrEditingPersistsImmediatelyAndValidates();
    void testAiTranslationEditingPersistsImmediatelyAndCredentials();
    void testAppearanceEditingPersistsImmediatelyAndPreviewsTheme();
    void testRestoreDefaultsPersistsImmediately();
    void testUnavailableStoreReportsPersistenceFailure();
};

void SettingsViewModelTest::testInitialObserveReplay() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    bool observed = false;
    SettingsState receivedState;
    vm.observe(this, [&observed, &receivedState](const SettingsState& state) {
        observed = true;
        receivedState = state;
    });

    QVERIFY(observed);
    QVERIFY(!receivedState.hasFieldErrors());
    QCOMPARE(receivedState.persisted, ::manga::application::AppSettingsSnapshot{});
}

void SettingsViewModelTest::testGeneralEditingPersistsImmediately() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    vm.setRestoreLastProject(false);
    QCOMPARE(vm.state().persisted.restoreLastProject, false);
    QCOMPARE(store.m_values.value(QStringLiteral("general/restoreLastProject")).toBool(), false);

    vm.setRecentProjectLimit(20);
    QCOMPARE(vm.state().persisted.recentProjectLimit, 20);
    QCOMPARE(store.m_values.value(QStringLiteral("general/recentProjectLimit")).toInt(), 20);
    QVERIFY(!vm.state().hasFieldErrors());

    vm.setRecentProjectLimit(0);
    QCOMPARE(vm.state().persisted.recentProjectLimit, 0);
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("recentProjectLimit")));
    QCOMPARE(store.m_values.value(QStringLiteral("general/recentProjectLimit")).toInt(), 20);

    vm.setTheme(::manga::application::AppTheme::Dark);
    QCOMPARE(vm.state().persisted.theme, ::manga::application::AppTheme::Dark);
    QCOMPARE(store.m_values.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
}

void SettingsViewModelTest::testOcrEditingPersistsImmediatelyAndValidates() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    vm.setOcrModelDirectory(QStringLiteral("C:/models/baberu"));
    QCOMPARE(vm.state().persisted.ocrModelDirectory, QStringLiteral("C:/models/baberu"));
    QVERIFY(vm.state().modelStatus.isValid);
    QCOMPARE(store.m_values.value(QStringLiteral("ocr/modelDirectory")).toString(), QStringLiteral("C:/models/baberu"));

    validator.shouldFail = true;
    vm.setOcrModelDirectory(QStringLiteral("C:/invalid/path"));
    QCOMPARE(vm.state().persisted.ocrModelDirectory, QStringLiteral("C:/invalid/path"));
    QVERIFY(!vm.state().modelStatus.isValid);
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("ocrModelDirectory")));

    validator.shouldFail = false;
    vm.setOcrLanguage(QStringLiteral("zh"));
    QCOMPARE(vm.state().persisted.ocrLanguage, QStringLiteral("zh"));
    QCOMPARE(store.m_values.value(QStringLiteral("ocr/language")).toString(), QStringLiteral("zh"));
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("ocrLanguage")));

    vm.setOcrLanguage(QStringLiteral("   "));
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("ocrLanguage")));
}

void SettingsViewModelTest::testAiTranslationEditingPersistsImmediatelyAndCredentials() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    vm.setProviderId(QStringLiteral("openai-compatible"));
    QCOMPARE(vm.state().credentialStatus, ::manga::application::CredentialState::Missing);
    QCOMPARE(store.m_values.value(QStringLiteral("translation/providerId")).toString(),
             QStringLiteral("openai-compatible"));

    vm.setCredential(::manga::application::SecretBytes(QByteArray("sk-test-secret-key")));
    QCOMPARE(vm.state().credentialStatus, ::manga::application::CredentialState::Configured);

    vm.removeCredential();
    QCOMPARE(vm.state().credentialStatus, ::manga::application::CredentialState::Missing);

    vm.setEndpoint(QStringLiteral("https://api.openai.com/v1"));
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("endpoint")));
    QCOMPARE(store.m_values.value(QStringLiteral("translation/endpoint")).toString(),
             QStringLiteral("https://api.openai.com/v1"));

    vm.setEndpoint(QStringLiteral("invalid-url-string"));
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("endpoint")));

    vm.setEndpoint(QString());
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("endpoint")));

    vm.setModelName(QStringLiteral("claude-3-5-sonnet"));
    QCOMPARE(vm.state().persisted.modelName, QStringLiteral("claude-3-5-sonnet"));
    QCOMPARE(store.m_values.value(QStringLiteral("translation/modelName")).toString(),
             QStringLiteral("claude-3-5-sonnet"));

    vm.setTimeoutSeconds(120);
    QCOMPARE(vm.state().persisted.timeoutSeconds, 120);
    vm.setTimeoutSeconds(400);
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("timeoutSeconds")));

    vm.setRetryCount(3);
    QCOMPARE(vm.state().persisted.retryCount, 3);
    vm.setRetryCount(20);
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("retryCount")));
}

void SettingsViewModelTest::testAppearanceEditingPersistsImmediatelyAndPreviewsTheme() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    QSignalSpy themeSpy(&vm, &SettingsViewModel::themePreviewRequested);

    vm.setTheme(::manga::application::AppTheme::Dark);
    QCOMPARE(vm.state().persisted.theme, ::manga::application::AppTheme::Dark);
    QCOMPARE(store.m_values.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
    QCOMPARE(themeSpy.count(), 1);
    QCOMPARE(themeSpy.at(0).at(0).value<::manga::application::AppTheme>(), ::manga::application::AppTheme::Dark);

    vm.setDensity(::manga::application::InterfaceDensity::Comfortable);
    QCOMPARE(vm.state().persisted.density, ::manga::application::InterfaceDensity::Comfortable);
    QCOMPARE(store.m_values.value(QStringLiteral("appearance/density")).toString(), QStringLiteral("comfortable"));

    vm.setReduceMotion(true);
    QCOMPARE(vm.state().persisted.reduceMotion, true);
    QCOMPARE(store.m_values.value(QStringLiteral("appearance/reduceMotion")).toBool(), true);

    vm.setCanvasBackground(QStringLiteral("#1e1e1e"));
    QCOMPARE(vm.state().persisted.canvasBackground, QStringLiteral("#1e1e1e"));
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("canvasBackground")));
    QCOMPARE(store.m_values.value(QStringLiteral("appearance/canvasBackground")).toString(), QStringLiteral("#1e1e1e"));

    vm.setCanvasBackground(QStringLiteral("not-a-color"));
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("canvasBackground")));
}

void SettingsViewModelTest::testRestoreDefaultsPersistsImmediately() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    vm.setTheme(::manga::application::AppTheme::Dark);
    vm.setRecentProjectLimit(25);
    vm.restoreDefaults();

    QCOMPARE(vm.state().persisted, ::manga::application::AppSettingsSnapshot{});
    QCOMPARE(store.m_values.value(QStringLiteral("general/recentProjectLimit")).toInt(), 8);
    QCOMPARE(store.m_values.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("system"));
}

void SettingsViewModelTest::testUnavailableStoreReportsPersistenceFailure() {
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;
    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(nullptr, &credentialOperations, &validator);

    vm.setReduceMotion(true);

    QCOMPARE(vm.state().persisted.reduceMotion, true);
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("persist")));
}

} // namespace manga::ui::screen::settings

QTEST_MAIN(manga::ui::screen::settings::SettingsViewModelTest)
#include "settings_view_model_test.moc"
