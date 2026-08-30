#include "application/ports/IAppSettingsStore.h"
#include "ui/navigation/NavigationController.h"
#include "ui/navigation/NavigationPanel.h"
#include "ui/navigation/NavigationSurfaceHost.h"
#include "ui/window/MainWindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <QtTest>

namespace {

class FakeSettingsStore final : public manga::application::IAppSettingsStore {
public:
    [[nodiscard]] std::optional<QVariant> readSetting(const QString& key) const override {
        const auto it = m_values.constFind(key);
        return it == m_values.constEnd() ? std::nullopt : std::optional<QVariant>(it.value());
    }
    void writeSetting(const QString& key, const QVariant& value) override {
        m_values.insert(key, value);
    }
    void removeSetting(const QString& key) override {
        m_values.remove(key);
    }

    QHash<QString, QVariant> m_values;
};

struct ShellFixture {
    FakeSettingsStore settings;
    manga::ui::navigation::NavigationSurfaceHost host;
    manga::ui::navigation::NavigationController controller{&host};

    ShellFixture() {
        host.registerSurface(QStringLiteral("main"),
                             new manga::ui::navigation::NavigationPanel(
                                 {{QStringLiteral("home"), QStringLiteral("首页"), QStringLiteral("\uE80F")}}));
        host.registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
        controller.navigate(QStringLiteral("home"));
    }
};

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void geometryIsSavedAndRestored() {
        {
            ShellFixture fixture;
            manga::ui::window::MainWindow window(&fixture.settings, &fixture.host, &fixture.controller);
            window.resize(777, 555);
            window.show();
            QCoreApplication::processEvents();
            window.close();
            QVERIFY(fixture.settings.m_values.contains(QStringLiteral("ui/mainWindow/normalGeometry")));
            QCOMPARE(fixture.settings.m_values.value(QStringLiteral("ui/mainWindow/maximized")).toBool(), false);
        }
        {
            ShellFixture fixture;
            // Reuse the keys persisted by a previous session.
            fixture.settings.m_values = [&] {
                FakeSettingsStore seed;
                manga::ui::navigation::NavigationSurfaceHost seedHost;
                manga::ui::navigation::NavigationController seedController(&seedHost);
                seedHost.registerSurface(
                    QStringLiteral("main"),
                    new manga::ui::navigation::NavigationPanel(
                        {{QStringLiteral("home"), QStringLiteral("首页"), QStringLiteral("\uE80F")}}));
                seedHost.registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
                seedController.navigate(QStringLiteral("home"));
                manga::ui::window::MainWindow seedWindow(&seed, &seedHost, &seedController);
                seedWindow.resize(777, 555);
                seedWindow.show();
                seedWindow.close();
                return seed.m_values;
            }();
            manga::ui::window::MainWindow window(&fixture.settings, &fixture.host, &fixture.controller);
            window.show();
            QCOMPARE(window.size(), QSize(777, 555));
        }
    }

    void offscreenGeometryFallsBackToDefault() {
        ShellFixture fixture;
        // Seed a geometry that is clearly off every screen.
        manga::ui::window::MainWindow probe(&fixture.settings, &fixture.host, &fixture.controller);
        probe.setGeometry(-100000, -100000, 800, 600);
        fixture.settings.writeSetting(QStringLiteral("ui/mainWindow/normalGeometry"), probe.saveGeometry());

        manga::ui::window::MainWindow window(&fixture.settings, &fixture.host, &fixture.controller);
        window.show();
        const QRect frame = window.frameGeometry();
        bool intersects = false;
        for (const QScreen* screen : QGuiApplication::screens()) {
            const QRect overlap = screen->availableGeometry().intersected(frame);
            if (overlap.width() >= 64 && overlap.height() >= 64) {
                intersects = true;
            }
        }
        QVERIFY(intersects);
    }
};

QTEST_MAIN(MainWindowTest)
#include "main_window_test.moc"
