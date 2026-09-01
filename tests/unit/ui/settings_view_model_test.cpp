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
    ::manga::application::OcrModelStatus statusToReturn;

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
    void testCategoryNavigationAndScrollPositions();
    void testGeneralDraftEditingAndValidation();
    void testOcrDraftEditingAndValidation();
    void testAiTranslationDraftEditingAndCredentials();
    void testAppearanceDraftEditingAndInstantThemePreview();
    void testSaveWorkflowSuccess();
    void testSaveWorkflowValidationFailure();
    void testRestoreDefaultsAndRevert();
    void testLeaveDecisionWorkflow();
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
    QCOMPARE(receivedState.currentCategory, SettingsCategory::General);
    QVERIFY(!receivedState.isDirty());
    QVERIFY(!receivedState.hasFieldErrors());
    QCOMPARE(receivedState.draft, receivedState.persisted);
}

void SettingsViewModelTest::testCategoryNavigationAndScrollPositions() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    vm.setCurrentCategory(SettingsCategory::Ocr);
    QCOMPARE(vm.state().currentCategory, SettingsCategory::Ocr);
    QVERIFY(!vm.isDirty());

    vm.setScrollPosition(SettingsCategory::Ocr, 150);
    QCOMPARE(vm.state().scrollPositions.value(SettingsCategory::Ocr), 150);

    vm.setCurrentCategory(SettingsCategory::AiTranslation);
    QCOMPARE(vm.state().currentCategory, SettingsCategory::AiTranslation);
    QCOMPARE(vm.state().scrollPositions.value(SettingsCategory::Ocr), 150);
}

void SettingsViewModelTest::testGeneralDraftEditingAndValidation() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    vm.setRestoreLastProject(false);
    QVERIFY(vm.isDirty());
    QVERIFY(vm.state().isCategoryDirty(SettingsCategory::General));

    vm.setRecentProjectLimit(20);
    QCOMPARE(vm.state().draft.recentProjectLimit, 20);
    QVERIFY(!vm.state().hasFieldErrors());

    // Invalid recent limit (< 1 or > 50)
    vm.setRecentProjectLimit(0);
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("recentProjectLimit")));

    vm.setRecentProjectLimit(55);
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("recentProjectLimit")));

    vm.setRecentProjectLimit(8);
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("recentProjectLimit")));
}

void SettingsViewModelTest::testOcrDraftEditingAndValidation() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    vm.setOcrModelDirectory(QStringLiteral("C:/models/baberu"));
    QVERIFY(vm.state().modelStatus.isValid);
    QCOMPARE(vm.state().modelStatus.id, QStringLiteral("baberu-onnx"));
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("ocrModelDirectory")));

    validator.shouldFail = true;
    vm.setOcrModelDirectory(QStringLiteral("C:/invalid/path"));
    QVERIFY(!vm.state().modelStatus.isValid);
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("ocrModelDirectory")));

    validator.shouldFail = false;
    vm.setOcrLanguage(QStringLiteral("zh"));
    QCOMPARE(vm.state().draft.ocrLanguage, QStringLiteral("zh"));
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("ocrLanguage")));

    vm.setOcrLanguage(QStringLiteral("   "));
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("ocrLanguage")));
}

void SettingsViewModelTest::testAiTranslationDraftEditingAndCredentials() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    vm.setProviderId(QStringLiteral("openai-compatible"));
    QCOMPARE(vm.state().credentialStatus, ::manga::application::CredentialState::Missing);

    // Set credential
    vm.setCredential(::manga::application::SecretBytes(QByteArray("sk-test-secret-key")));
    QCOMPARE(vm.state().credentialStatus, ::manga::application::CredentialState::Configured);

    // Remove credential
    vm.removeCredential();
    QCOMPARE(vm.state().credentialStatus, ::manga::application::CredentialState::Missing);

    // Endpoint validation
    vm.setEndpoint(QStringLiteral("https://api.openai.com/v1"));
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("endpoint")));

    vm.setEndpoint(QStringLiteral("invalid-url-string"));
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("endpoint")));

    vm.setEndpoint(QString()); // empty endpoint allowed (falls back to provider default)
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("endpoint")));

    // Model name, timeout, retry count
    vm.setModelName(QStringLiteral("claude-3-5-sonnet"));
    QCOMPARE(vm.state().draft.modelName, QStringLiteral("claude-3-5-sonnet"));

    vm.setTimeoutSeconds(120);
    QCOMPARE(vm.state().draft.timeoutSeconds, 120);

    vm.setTimeoutSeconds(400); // invalid > 300
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("timeoutSeconds")));

    vm.setTimeoutSeconds(60);
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("timeoutSeconds")));

    vm.setRetryCount(3);
    QCOMPARE(vm.state().draft.retryCount, 3);

    vm.setRetryCount(20); // invalid > 10
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("retryCount")));
}

void SettingsViewModelTest::testAppearanceDraftEditingAndInstantThemePreview() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    QSignalSpy themeSpy(&vm, &SettingsViewModel::themePreviewRequested);

    vm.setTheme(::manga::application::AppTheme::Dark);
    QCOMPARE(vm.state().draft.theme, ::manga::application::AppTheme::Dark);
    QVERIFY(vm.state().isCategoryDirty(SettingsCategory::Appearance));
    QCOMPARE(themeSpy.count(), 1);
    QCOMPARE(themeSpy.at(0).at(0).value<::manga::application::AppTheme>(), ::manga::application::AppTheme::Dark);

    vm.setDensity(::manga::application::InterfaceDensity::Comfortable);
    QCOMPARE(vm.state().draft.density, ::manga::application::InterfaceDensity::Comfortable);

    vm.setReduceMotion(true);
    QCOMPARE(vm.state().draft.reduceMotion, true);

    vm.setCanvasBackground(QStringLiteral("#1e1e1e"));
    QCOMPARE(vm.state().draft.canvasBackground, QStringLiteral("#1e1e1e"));
    QVERIFY(!vm.state().fieldErrors.contains(QStringLiteral("canvasBackground")));

    vm.setCanvasBackground(QStringLiteral("not-a-color"));
    QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("canvasBackground")));
}

void SettingsViewModelTest::testSaveWorkflowSuccess() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    QSignalSpy savedSpy(&vm, &SettingsViewModel::settingsSaved);

    vm.setRecentProjectLimit(15);
    vm.setOcrLanguage(QStringLiteral("zh"));
    vm.setTheme(::manga::application::AppTheme::Light);

    QVERIFY(vm.isDirty());

    vm.saveSettings();

    QCOMPARE(savedSpy.count(), 1);
    QVERIFY(!vm.isDirty());
    QCOMPARE(vm.state().persisted.recentProjectLimit, 15);
    QCOMPARE(vm.state().persisted.ocrLanguage, QStringLiteral("zh"));
    QCOMPARE(vm.state().persisted.theme, ::manga::application::AppTheme::Light);
    QCOMPARE(vm.state().draft, vm.state().persisted);
}

void SettingsViewModelTest::testSaveWorkflowValidationFailure() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    QSignalSpy savedSpy(&vm, &SettingsViewModel::settingsSaved);
    QSignalSpy focusSpy(&vm, &SettingsViewModel::focusFieldRequested);

    vm.setRecentProjectLimit(100); // invalid
    QVERIFY(vm.isDirty());

    vm.saveSettings();

    QCOMPARE(savedSpy.count(), 0);
    QVERIFY(vm.isDirty());
    QVERIFY(vm.state().saveError.has_value());
    QCOMPARE(vm.state().currentCategory, SettingsCategory::General);
    QCOMPARE(focusSpy.count(), 1);
    QCOMPARE(focusSpy.at(0).at(0).toString(), QStringLiteral("recentProjectLimit"));
}

void SettingsViewModelTest::testRestoreDefaultsAndRevert() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    QSignalSpy themeSpy(&vm, &SettingsViewModel::themePreviewRequested);

    vm.setTheme(::manga::application::AppTheme::Dark);
    vm.setRecentProjectLimit(25);
    QVERIFY(vm.isDirty());

    // Revert draft back to persisted
    vm.revertDraft();
    QVERIFY(!vm.isDirty());
    QCOMPARE(vm.state().draft.theme, ::manga::application::AppTheme::System);
    QCOMPARE(vm.state().draft.recentProjectLimit, 8);

    // Modify and restore defaults
    vm.setRecentProjectLimit(35);
    vm.restoreDefaults();
    QCOMPARE(vm.state().draft.recentProjectLimit, 8);
}

void SettingsViewModelTest::testLeaveDecisionWorkflow() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);

    // Clean state -> immediately proceeds
    bool proceedResult = false;
    vm.requestLeave([&proceedResult](bool proceed) { proceedResult = proceed; });
    QVERIFY(proceedResult);

    // Dirty state -> emits leaveDecisionRequested
    vm.setRecentProjectLimit(12);
    QSignalSpy leaveSpy(&vm, &SettingsViewModel::leaveDecisionRequested);

    bool decided = false;
    vm.requestLeave([&decided](bool /*proceed*/) { decided = true; });
    QCOMPARE(leaveSpy.count(), 1);
    QVERIFY(!decided); // blocked until user confirms

    // Confirm without save
    bool proceedAfterDiscard = false;
    vm.confirmLeaveWithoutSave([&proceedAfterDiscard](bool proceed) { proceedAfterDiscard = proceed; });
    QVERIFY(proceedAfterDiscard);
    QVERIFY(!vm.isDirty());
}

} // namespace manga::ui::screen::settings

QTEST_MAIN(manga::ui::screen::settings::SettingsViewModelTest)
#include "settings_view_model_test.moc"
