#include "ui/window/MainWindow.h"

#include "application/ports/IAppSettingsStore.h"
#include "ui/navigation/NavigationController.h"
#include "ui/navigation/NavigationPanel.h"
#include "ui/window/TitleBar.h"

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

} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void geometryIsSavedAndRestored() {
        {
            FakeSettingsStore settings;
            manga::ui::window::MainWindow window(&settings);
            window.registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
            window.resize(777, 555);
            window.show();
            QCoreApplication::processEvents();
            window.close();
            QVERIFY(settings.m_values.contains(QStringLiteral("ui/mainWindow/normalGeometry")));
            QCOMPARE(settings.m_values.value(QStringLiteral("ui/mainWindow/maximized")).toBool(), false);
        }
        {
            FakeSettingsStore settings;
            // Reuse the keys persisted by a previous session.
            {
                manga::ui::window::MainWindow seedWindow(&settings);
                seedWindow.registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
                seedWindow.resize(777, 555);
                seedWindow.show();
                seedWindow.close();
            }
            manga::ui::window::MainWindow window(&settings);
            window.registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
            window.show();
            QCOMPARE(window.size(), QSize(777, 555));
        }
    }

    void offscreenGeometryFallsBackToDefault() {
        FakeSettingsStore settings;
        // Seed a geometry that is clearly off every screen.
        manga::ui::window::MainWindow probe(&settings);
        probe.setGeometry(-100000, -100000, 800, 600);
        settings.writeSetting(QStringLiteral("ui/mainWindow/normalGeometry"), probe.saveGeometry());

        manga::ui::window::MainWindow window(&settings);
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

    void backButtonFollowsController() {
        FakeSettingsStore settings;
        manga::ui::window::MainWindow window(&settings);
        window.registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
        window.registerRoute(QStringLiteral("settings"), new QWidget, QStringLiteral("main"));
        manga::ui::navigation::NavigationController controller(&window);
        window.setNavigationController(&controller);

        QVERIFY(!window.titleBar()->isBackButtonVisible());
        controller.navigate(QStringLiteral("home"));
        controller.navigate(QStringLiteral("settings"));
        QVERIFY(window.titleBar()->isBackButtonVisible());
        controller.goBack();
        QVERIFY(!window.titleBar()->isBackButtonVisible());
    }

    void closeInterceptionEmitsSignal() {
        FakeSettingsStore settings;
        manga::ui::window::MainWindow window(&settings);
        window.show();
        QCoreApplication::processEvents();

        QSignalSpy spy(&window, &manga::ui::window::MainWindow::closeRequested);
        QObject::connect(&window, &manga::ui::window::MainWindow::closeRequested, []() {});

        window.close();
        QCOMPARE(spy.count(), 1);
        QVERIFY(window.isVisible());

        window.forceClose();
        QVERIFY(!window.isVisible());
    }
};

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
