#include "ui/screen/settings/SettingsPage.h"

#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "ui/screen/settings/SettingsViewModel.h"
#include "ui/widget/SettingsGroup.h"
#include "ui/widget/SettingsRow.h"
#include "unit/application/FakeCredentialStore.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/TextFields.h>
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

class SettingsPageTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    // ViewModel tests

    void testInitialObserveReplay() {
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

    void testModelsEditingPersistsImmediatelyAndValidates() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        FakeOcrModelValidator validator;

        ::manga::application::CredentialOperations credentialOperations(&credStore);
        SettingsViewModel vm(&store, &credentialOperations, &validator);

        vm.setOcrModelDirectory(QStringLiteral("C:/models/baberu"));
        QCOMPARE(vm.state().persisted.ocrModelDirectory, QStringLiteral("C:/models/baberu"));
        QVERIFY(vm.state().modelStatus.isValid);
        QCOMPARE(store.m_values.value(QStringLiteral("ocr/modelDirectory")).toString(),
                 QStringLiteral("C:/models/baberu"));

        validator.shouldFail = true;
        vm.setOcrModelDirectory(QStringLiteral("C:/invalid/path"));
        QCOMPARE(vm.state().persisted.ocrModelDirectory, QStringLiteral("C:/invalid/path"));
        QVERIFY(!vm.state().modelStatus.isValid);
        QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("ocrModelDirectory")));
    }

    void testAiTranslationEditingPersistsImmediatelyAndCredentials() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        FakeOcrModelValidator validator;

        ::manga::application::CredentialOperations credentialOperations(&credStore);
        SettingsViewModel vm(&store, &credentialOperations, &validator);

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
        // TODO: timeout upper bound changed to 600s
        vm.setTimeoutSeconds(700);
        QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("timeoutSeconds")));

        vm.setRetryCount(3);
        QCOMPARE(vm.state().persisted.retryCount, 3);
        vm.setRetryCount(20);
        QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("retryCount")));
    }

    void testAppearanceEditingPersistsImmediatelyAndPreviewsTheme() {
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
    }

    void testRestoreDefaultsPersistsImmediately() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        FakeOcrModelValidator validator;

        ::manga::application::CredentialOperations credentialOperations(&credStore);
        SettingsViewModel vm(&store, &credentialOperations, &validator);

        vm.setTheme(::manga::application::AppTheme::Dark);
        vm.setTimeoutSeconds(120);
        vm.restoreDefaults();

        QCOMPARE(vm.state().persisted, ::manga::application::AppSettingsSnapshot{});
        QCOMPARE(store.m_values.value(QStringLiteral("translation/timeoutSeconds")).toInt(), 60);
        QCOMPARE(store.m_values.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("system"));
    }

    void testUnavailableStoreReportsPersistenceFailure() {
        ::manga::application::test::FakeCredentialStore credStore;
        FakeOcrModelValidator validator;
        ::manga::application::CredentialOperations credentialOperations(&credStore);
        SettingsViewModel vm(nullptr, &credentialOperations, &validator);

        vm.setTimeoutSeconds(90);

        QCOMPARE(vm.state().persisted.timeoutSeconds, 90);
        QVERIFY(vm.state().fieldErrors.contains(QStringLiteral("persist")));
    }

    // Page and widget tests

    void testInitialRendering() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        FakeOcrModelValidator validator;

        ::manga::application::CredentialOperations credentialOperations(&credStore);
        SettingsViewModel vm(&store, &credentialOperations, &validator);
        SettingsPage page(&vm, SettingsCategory::Models);
        page.resize(800, 600);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        QCOMPARE(page.currentCategory(), SettingsCategory::Models);
        auto* ocrRow = page.rowForField(QStringLiteral("ocrModelDirectory"));
        QVERIFY(ocrRow != nullptr);
        QVERIFY(!ocrRow->hasError());
        QVERIFY(page.findChild<fluent::basicinput::Button*>(QStringLiteral("settingsSaveButton")) == nullptr);
    }

    void testResponsiveModePreservesWidgetAndFocus() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        FakeOcrModelValidator validator;

        ::manga::application::CredentialOperations credentialOperations(&credStore);
        SettingsViewModel vm(&store, &credentialOperations, &validator);
        SettingsPage page(&vm, SettingsCategory::Models);
        page.resize(800, 600);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        auto* ocrRow = page.rowForField(QStringLiteral("ocrModelDirectory"));
        QVERIFY(ocrRow != nullptr);
        auto* editor = ocrRow->editorWidget();
        QVERIFY(editor != nullptr);

        // Give editor focus
        editor->setFocus();
        QVERIFY(editor->hasFocus());

        // Switch to narrow width (< 800px)
        page.resize(600, 600);
        QTest::qWait(50);

        // A responsive relayout reuses the editor widget, so focus survives the width change.
        QCOMPARE(ocrRow->editorWidget(), editor);
        QVERIFY(editor->hasFocus());

        // Switch back to wide width (>= 800px)
        page.resize(900, 600);
        QTest::qWait(50);

        QCOMPARE(ocrRow->editorWidget(), editor);
        QVERIFY(editor->hasFocus());
    }

    void testRowErrorDisplayExpandsContent() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        FakeOcrModelValidator validator;

        ::manga::application::CredentialOperations credentialOperations(&credStore);
        SettingsViewModel vm(&store, &credentialOperations, &validator);
        SettingsPage page(&vm, SettingsCategory::Models);
        page.resize(800, 600);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));

        auto* ocrRow = page.rowForField(QStringLiteral("ocrModelDirectory"));
        QVERIFY(ocrRow != nullptr);

        // Trigger error
        validator.shouldFail = true;
        vm.setOcrModelDirectory(QStringLiteral("C:/bad/path"));
        QVERIFY(ocrRow->hasError());
        QCOMPARE(ocrRow->error(), vm.state().fieldErrors.value(QStringLiteral("ocrModelDirectory")));

        // Clear error
        validator.shouldFail = false;
        vm.setOcrModelDirectory(QStringLiteral("C:/good/path"));
        QVERIFY(!ocrRow->hasError());
    }

    void testImmediatePersistenceAndRestoreDefaults() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        FakeOcrModelValidator validator;

        ::manga::application::CredentialOperations credentialOperations(&credStore);
        SettingsViewModel vm(&store, &credentialOperations, &validator);
        SettingsPage page(&vm, SettingsCategory::AiTranslation);
        page.resize(800, 600);

        QVERIFY(page.findChild<fluent::basicinput::Button*>(QStringLiteral("settingsSaveButton")) == nullptr);

        auto* timeoutRow = page.rowForField(QStringLiteral("timeoutSeconds"));
        QVERIFY(timeoutRow != nullptr);
        auto* timeoutBox = qobject_cast<fluent::textfields::NumberBox*>(timeoutRow->editorWidget());
        QVERIFY(timeoutBox != nullptr);
        timeoutBox->setValue(90);
        QCOMPARE(vm.state().persisted.timeoutSeconds, 90);
        QCOMPARE(store.m_values.value(QStringLiteral("translation/timeoutSeconds")).toInt(), 90);

        // Restore falls back to the AppSettingsDto default timeout of 60.
        auto* restoreBtn = page.restoreDefaultsButtonForTest();
        QVERIFY(restoreBtn != nullptr);
        vm.setTimeoutSeconds(150);
        restoreBtn->click();
        QCOMPARE(vm.state().persisted.timeoutSeconds, 60);
        QCOMPARE(store.m_values.value(QStringLiteral("translation/timeoutSeconds")).toInt(), 60);
    }

    void testViewModelPageDestructionCleanliness() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        ::manga::application::CredentialOperations credentialOperations(&credStore);
        FakeOcrModelValidator validator;
        {
            auto* vm = new SettingsViewModel(&store, &credentialOperations, &validator);
            auto* page = new SettingsPage(vm, SettingsCategory::AiTranslation);
            page->resize(800, 600);
            page->show();
            QTest::qWait(20);
            vm->setTimeoutSeconds(45);
            QCOMPARE(vm->state().persisted.timeoutSeconds, 45);
            QCOMPARE(store.m_values.value(QStringLiteral("translation/timeoutSeconds")).toInt(), 45);
            delete page;
            delete vm;
        }
    }

    void testThemePersistenceAndPreview() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        ::manga::application::CredentialOperations credentialOperations(&credStore);
        FakeOcrModelValidator validator;
        SettingsViewModel vm(&store, &credentialOperations, &validator);
        QSignalSpy themeSpy(&vm, &SettingsViewModel::themePreviewRequested);
        vm.setTheme(::manga::application::AppTheme::Dark);
        QCOMPARE(themeSpy.count(), 1);
        QCOMPARE(themeSpy.at(0).at(0).value<::manga::application::AppTheme>(), ::manga::application::AppTheme::Dark);
        QCOMPARE(vm.state().persisted.theme, ::manga::application::AppTheme::Dark);
        QCOMPARE(store.m_values.value(QStringLiteral("appearance/theme")).toString(), QStringLiteral("dark"));
    }

    void testLazyPageBuildsOnce() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        ::manga::application::CredentialOperations credentialOperations(&credStore);
        FakeOcrModelValidator validator;
        SettingsViewModel vm(&store, &credentialOperations, &validator);

        auto* lazyPage = SettingsPage::createLazyPage(&vm, SettingsCategory::Models);
        QCOMPARE(lazyPage->findChildren<SettingsPage*>().size(), 0);
        lazyPage->resize(800, 600);
        lazyPage->show();
        QTest::qWait(20);

        const auto pages = lazyPage->findChildren<SettingsPage*>();
        QCOMPARE(pages.size(), 1);
        auto* page = pages.constFirst();
        lazyPage->hide();
        lazyPage->show();
        QTest::qWait(20);
        QCOMPARE(lazyPage->findChildren<SettingsPage*>().constFirst(), page);

        delete lazyPage;
    }

    void testCredentialOperationsDestructionSafety() {
        FakeAppSettingsStore store;
        ::manga::application::test::FakeCredentialStore credStore;
        ::manga::application::CredentialOperations credentialOperations(&credStore);
        FakeOcrModelValidator validator;
        SettingsViewModel vm(&store, &credentialOperations, &validator);
        ::manga::application::SecretBytes key(QByteArray("my-secret-key"));
        vm.setCredential(std::move(key));
        QCOMPARE(vm.state().credentialStatus, ::manga::application::CredentialState::Configured);
        vm.removeCredential();
        QCOMPARE(vm.state().credentialStatus, ::manga::application::CredentialState::Missing);
    }
};

} // namespace manga::ui::screen::settings

QTEST_MAIN(manga::ui::screen::settings::SettingsPageTest)
#include "SettingsPageTest.moc"
