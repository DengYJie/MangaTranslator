#include "ui/navigation/NavigationPanel.h"
#include "ui/navigation/NavigationSurfaceHost.h"

#include <QAbstractButton>
#include <QSignalSpy>
#include <QtTest>

using namespace manga::ui::navigation;

namespace {

NavigationPanel* makeMainPanel() {
    return new NavigationPanel({{QStringLiteral("home"), QStringLiteral("首页"), QStringLiteral("\uE80F")}});
}

NavigationPanel* makeSettingsPanel() {
    return new NavigationPanel(
        {{QStringLiteral("settings.general"), QStringLiteral("常规"), QStringLiteral("\uE713")}});
}

} // namespace

class NavigationHostTest : public QObject {
    Q_OBJECT
private slots:
    void presentRouteSwitchesPageAndSurface() {
        NavigationSurfaceHost host;
        auto* mainPanel = makeMainPanel();
        auto* settingsPanel = makeSettingsPanel();
        auto* homePage = new QWidget;
        auto* settingsPage = new QWidget;
        host.registerSurface(QStringLiteral("main"), mainPanel);
        host.registerSurface(QStringLiteral("settings"), settingsPanel);
        host.registerRoute(QStringLiteral("home"), homePage, QStringLiteral("main"));
        host.registerRoute(QStringLiteral("settings.general"), settingsPage, QStringLiteral("settings"));

        QVERIFY(host.canPresentRoute(QStringLiteral("home")));
        QVERIFY(!host.canPresentRoute(QStringLiteral("missing")));

        QVERIFY(host.presentRoute(QStringLiteral("home"), NavigationDirection::None));
        QCOMPARE(host.activeSurfaceId(), QStringLiteral("main"));
        QCOMPARE(host.currentRouteKey(), QStringLiteral("home"));
        QCOMPARE(mainPanel->currentItem(), QStringLiteral("home"));

        QVERIFY(host.presentRoute(QStringLiteral("settings.general"), NavigationDirection::None));
        QCOMPARE(host.activeSurfaceId(), QStringLiteral("settings"));
        QVERIFY(!mainPanel->isVisibleTo(&host));
        QVERIFY(settingsPanel->isVisibleTo(&host));

        // Panels survive the take/set chrome cycle and can be switched back.
        QVERIFY(host.presentRoute(QStringLiteral("home"), NavigationDirection::Back));
        QCOMPARE(host.activeSurfaceId(), QStringLiteral("main"));
        QVERIFY(mainPanel->isVisibleTo(&host));
    }

    void unknownRouteIsRefused() {
        NavigationSurfaceHost host;
        host.registerSurface(QStringLiteral("main"), makeMainPanel());
        host.registerRoute(QStringLiteral("home"), new QWidget, QStringLiteral("main"));
        QVERIFY(!host.presentRoute(QStringLiteral("missing"), NavigationDirection::None));
        QVERIFY(host.currentRouteKey().isEmpty());
    }

    void panelClickEmitsItemSelected() {
        NavigationPanel panel({{QStringLiteral("home"), QStringLiteral("首页"), QStringLiteral("\uE80F")},
                               {QStringLiteral("editor"), QStringLiteral("编辑"), QStringLiteral("\uE70F")}});
        panel.show();
        QSignalSpy spy(&panel, &NavigationPanel::itemSelected);
        const auto buttons = panel.findChildren<QAbstractButton*>();
        QVERIFY(buttons.size() >= 2);
        QTest::mouseClick(buttons.at(1), Qt::LeftButton);
        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.takeFirst().at(0).toString(), QStringLiteral("editor"));
    }

    void setCurrentItemReflectsSelection() {
        NavigationPanel panel({{QStringLiteral("home"), QStringLiteral("首页"), QStringLiteral("\uE80F")}});
        panel.setCurrentItem(QStringLiteral("home"));
        QCOMPARE(panel.currentItem(), QStringLiteral("home"));
        panel.setCurrentItem(QStringLiteral("missing")); // Unknown item: ignored.
        QCOMPARE(panel.currentItem(), QStringLiteral("home"));
    }
};

QTEST_MAIN(NavigationHostTest)
#include "navigation_host_test.moc"
