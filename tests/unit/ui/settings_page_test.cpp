#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "ui/screen/settings/SettingsPage.h"
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
    ::manga::application::Result<::manga::application::OcrModelStatus>
    validate(const QString& modelDirectory) override {
        ::manga::application::OcrModelStatus status;
        status.isValid = true;
        status.id = QStringLiteral("baberu-onnx");
        status.version = QStringLiteral("1.0.0");
        status.modelDirectory = modelDirectory;
        return status;
    }
};

} // namespace

class SettingsPageTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testInitialRendering();
    void testCategorySwitching();
    void testResponsiveModePreservesWidgetAndFocus();
    void testRowErrorDisplayExpandsContent();
    void testImmediatePersistenceAndRestoreDefaults();
};

void SettingsPageTest::testInitialRendering() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);
    SettingsPage page(&vm);
    page.resize(800, 600);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));

    QCOMPARE(page.currentCategory(), SettingsCategory::General);
    auto* limitRow = page.rowForField(QStringLiteral("recentProjectLimit"));
    QVERIFY(limitRow != nullptr);
    QVERIFY(!limitRow->hasError());
    QVERIFY(page.findChild<fluent::basicinput::Button*>(QStringLiteral("settingsSaveButton")) == nullptr);
}

void SettingsPageTest::testCategorySwitching() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);
    SettingsPage page(&vm);
    page.resize(800, 600);

    vm.setCurrentCategory(SettingsCategory::AiTranslation);
    QCOMPARE(page.currentCategory(), SettingsCategory::AiTranslation);

    auto* endpointRow = page.rowForField(QStringLiteral("endpoint"));
    QVERIFY(endpointRow != nullptr);

    vm.setCurrentCategory(SettingsCategory::Appearance);
    QCOMPARE(page.currentCategory(), SettingsCategory::Appearance);
    auto* themeRow = page.rowForField(QStringLiteral("theme"));
    QVERIFY(themeRow != nullptr);
}

void SettingsPageTest::testResponsiveModePreservesWidgetAndFocus() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);
    SettingsPage page(&vm);
    page.resize(800, 600);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));

    auto* limitRow = page.rowForField(QStringLiteral("recentProjectLimit"));
    QVERIFY(limitRow != nullptr);
    auto* editor = limitRow->editorWidget();
    QVERIFY(editor != nullptr);

    // Give editor focus
    editor->setFocus();
    QVERIFY(editor->hasFocus());

    // Switch to narrow width (< 680px)
    page.resize(600, 600);
    QTest::qWait(50);

    // Verify same widget instance is preserved and retains focus
    QCOMPARE(limitRow->editorWidget(), editor);
    QVERIFY(editor->hasFocus());

    // Switch back to wide width (>= 680px)
    page.resize(900, 600);
    QTest::qWait(50);

    QCOMPARE(limitRow->editorWidget(), editor);
    QVERIFY(editor->hasFocus());
}

void SettingsPageTest::testRowErrorDisplayExpandsContent() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);
    SettingsPage page(&vm);
    page.resize(800, 600);
    page.show();
    QVERIFY(QTest::qWaitForWindowExposed(&page));

    auto* limitRow = page.rowForField(QStringLiteral("recentProjectLimit"));
    QVERIFY(limitRow != nullptr);

    // Trigger error
    vm.setRecentProjectLimit(999);
    QVERIFY(limitRow->hasError());
    QCOMPARE(limitRow->error(), vm.state().fieldErrors.value(QStringLiteral("recentProjectLimit")));

    // Clear error
    vm.setRecentProjectLimit(8);
    QVERIFY(!limitRow->hasError());
}

void SettingsPageTest::testImmediatePersistenceAndRestoreDefaults() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    FakeOcrModelValidator validator;

    ::manga::application::CredentialOperations credentialOperations(&credStore);
    SettingsViewModel vm(&store, &credentialOperations, &validator);
    SettingsPage page(&vm);
    page.resize(800, 600);

    QVERIFY(page.findChild<fluent::basicinput::Button*>(QStringLiteral("settingsSaveButton")) == nullptr);

    auto* limitRow = page.rowForField(QStringLiteral("recentProjectLimit"));
    QVERIFY(limitRow != nullptr);
    auto* limitBox = qobject_cast<fluent::textfields::NumberBox*>(limitRow->editorWidget());
    QVERIFY(limitBox != nullptr);
    limitBox->setValue(12);
    QCOMPARE(vm.state().persisted.recentProjectLimit, 12);
    QCOMPARE(store.m_values.value(QStringLiteral("general/recentProjectLimit")).toInt(), 12);

    // Test restore defaults
    auto* restoreBtn = page.restoreDefaultsButtonForTest();
    QVERIFY(restoreBtn != nullptr);
    vm.setRecentProjectLimit(25);
    restoreBtn->click();
    QCOMPARE(vm.state().persisted.recentProjectLimit, 8);
    QCOMPARE(store.m_values.value(QStringLiteral("general/recentProjectLimit")).toInt(), 8);
}

} // namespace manga::ui::screen::settings

QTEST_MAIN(manga::ui::screen::settings::SettingsPageTest)
#include "settings_page_test.moc"
