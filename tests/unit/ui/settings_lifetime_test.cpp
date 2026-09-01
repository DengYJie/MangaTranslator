#include "application/ports/IAppSettingsStore.h"
#include "application/ports/IOcrModelValidator.h"
#include "application/usecase/credential/CredentialUseCases.h"
#include "ui/screen/settings/SettingsPage.h"
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
        if (it != m_values.cend())
            return it.value();
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

class SettingsLifetimeTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void testViewModelPageDestructionCleanliness();
    void testThemePersistenceAndPreview();
    void testCredentialOperationsDestructionSafety();
};

void SettingsLifetimeTest::testViewModelPageDestructionCleanliness() {
    FakeAppSettingsStore store;
    ::manga::application::test::FakeCredentialStore credStore;
    ::manga::application::CredentialOperations credentialOperations(&credStore);
    FakeOcrModelValidator validator;
    {
        auto* vm = new SettingsViewModel(&store, &credentialOperations, &validator);
        auto* page = new SettingsPage(vm);
        page->resize(800, 600);
        page->show();
        QTest::qWait(20);
        vm->setRecentProjectLimit(15);
        QCOMPARE(vm->state().persisted.recentProjectLimit, 15);
        QCOMPARE(store.m_values.value(QStringLiteral("general/recentProjectLimit")).toInt(), 15);
        delete page;
        delete vm;
    }
}

void SettingsLifetimeTest::testThemePersistenceAndPreview() {
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

void SettingsLifetimeTest::testCredentialOperationsDestructionSafety() {
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

} // namespace manga::ui::screen::settings

QTEST_MAIN(manga::ui::screen::settings::SettingsLifetimeTest)
#include "settings_lifetime_test.moc"
