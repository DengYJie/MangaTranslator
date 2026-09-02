#include "ui/window/NavigationWindow.h"

#include "ui/navigation/INavigationRegistrar.h"
#include "ui/navigation/NavigationPanel.h"
#include "ui/navigation/NavigationTreeWidget.h"
#include "ui/window/TitleBar.h"

#include <QApplication>
#include <QtTest>

using namespace manga::ui;

class NavigationWindowTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void mainSurfaceIsCreatedAutomatically() {
        window::NavigationWindow window;
        QVERIFY(window.mainNavigationPanel() != nullptr);
        QCOMPARE(window.activeNavigationSurfaceId(), QStringLiteral("main"));
        QVERIFY(window.titleBar() != nullptr);
        QVERIFY(!window.titleBar()->isPaneToggleButtonVisible());
    }

    void presentRouteSwitchesPageAndSurface() {
        window::NavigationWindow window;
        auto* settingsPanel = new navigation::NavigationPanel;
        QVERIFY(window.registerSurface(QStringLiteral("settings"), settingsPanel));
        auto* homePage = new QWidget;
        auto* settingsPage = new QWidget;
        QVERIFY(window.registerRoute(QStringLiteral("home"), homePage, QStringLiteral("main")));
        QVERIFY(window.registerRoute(QStringLiteral("settings.general"), settingsPage, QStringLiteral("settings")));

        QVERIFY(window.canPresentRoute(QStringLiteral("home")));
        QVERIFY(!window.canPresentRoute(QStringLiteral("missing")));

        QVERIFY(window.presentRoute(QStringLiteral("home"), navigation::NavigationDirection::None));
        QCOMPARE(window.activeNavigationSurfaceId(), QStringLiteral("main"));
        QVERIFY(window.presentRoute(QStringLiteral("settings.general"), navigation::NavigationDirection::None));
        QCOMPARE(window.activeNavigationSurfaceId(), QStringLiteral("settings"));
        // After the surface switch and back, both page and panel stay alive.
        QVERIFY(window.presentRoute(QStringLiteral("home"), navigation::NavigationDirection::Back));
        QCOMPARE(window.activeNavigationSurfaceId(), QStringLiteral("main"));
    }

    void unknownRouteIsRefused() {
        window::NavigationWindow window;
        QVERIFY(!window.presentRoute(QStringLiteral("missing"), navigation::NavigationDirection::None));
    }

    void surfaceSwitchKeepsFocusOnNavigationTree() {
        // Regression: hiding the previous surface must not strand focus on the title
        // bar back button, which would draw a focus ring after a mouse-driven switch.
        window::NavigationWindow window;
        auto* settingsPanel = new navigation::NavigationPanel;
        QVERIFY(window.registerSurface(QStringLiteral("settings"), settingsPanel));

        window.mainNavigationPanel()->addItem(QStringLiteral("home"), QStringLiteral("H"), QStringLiteral("Home"));
        settingsPanel->addItem(QStringLiteral("settings.general"), QStringLiteral("S"), QStringLiteral("General"));

        window.titleBar()->setBackButtonVisible(true);
        window.show();
        QCoreApplication::processEvents();

        auto* mainItem = window.mainNavigationPanel()->tree()->findFirstSelectableItem();
        QVERIFY(mainItem != nullptr);
        mainItem->setFocus(Qt::OtherFocusReason);
        QCOMPARE(QApplication::focusWidget(), mainItem);

        QVERIFY(window.setActiveNavigationSurface(QStringLiteral("settings")));

        auto* settingsItem = settingsPanel->tree()->findFirstSelectableItem();
        QVERIFY(settingsItem != nullptr);
        QCOMPARE(QApplication::focusWidget(), settingsItem);
        QVERIFY(!window.titleBar()->isAncestorOf(QApplication::focusWidget()));
    }

    void panelsSurviveWindowDestructionOrder() {
        // Regression: NavigationWindow teardown must not let a panel destroyed()
        // signal touch already-destroyed members (ForgeAI ownership guard).
        auto* window = new window::NavigationWindow;
        window->registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
        window->show();
        QCoreApplication::processEvents();
        delete window; // must not crash
        QVERIFY(true);
    }
};

QTEST_MAIN(NavigationWindowTest)
#include "NavigationWindowTest.moc"
