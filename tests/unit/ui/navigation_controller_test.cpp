#include "ui/navigation/INavigationPresenter.h"
#include "ui/navigation/NavigationController.h"

#include <QSignalSpy>
#include <QtTest>

namespace {

class FakePresenter final : public manga::ui::navigation::INavigationPresenter {
public:
    QVector<QString> presented;
    QVector<manga::ui::navigation::NavigationDirection> directions;
    QVector<QString> knownRoutes{QStringLiteral("home"), QStringLiteral("settings.general")};
    bool failNextPresent{false};
    manga::ui::navigation::NavigationController* reentrancyProbe{nullptr};
    QString reentrantRoute;

    bool canPresentRoute(const QString& routeKey) const override {
        return knownRoutes.contains(routeKey);
    }
    bool presentRoute(const QString& routeKey, manga::ui::navigation::NavigationDirection direction) override {
        if (failNextPresent) {
            failNextPresent = false;
            return false;
        }
        presented.append(routeKey);
        directions.append(direction);
        if (reentrancyProbe && !reentrantRoute.isEmpty()) {
            auto route = reentrantRoute;
            reentrantRoute.clear();
            reentrancyProbe->navigate(route); // reentry from the render callback must be ignored
        }
        return true;
    }
};

} // namespace

class NavigationControllerTest : public QObject {
    Q_OBJECT
private slots:
    void navigateCommitsHistoryAfterRender() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        QSignalSpy routeSpy(&controller, &manga::ui::navigation::NavigationController::routeChanged);
        QSignalSpy backSpy(&controller, &manga::ui::navigation::NavigationController::canGoBackChanged);

        controller.navigate(QStringLiteral("home"));
        controller.navigate(QStringLiteral("settings.general"));
        QCOMPARE(presenter.presented.size(), 2);
        QCOMPARE(presenter.directions.size(), 2);
        QCOMPARE(presenter.directions.at(0), manga::ui::navigation::NavigationDirection::Forward);
        QCOMPARE(presenter.directions.at(1), manga::ui::navigation::NavigationDirection::Forward);
        QCOMPARE(routeSpy.size(), 2);
        QVERIFY(controller.canGoBack());
        QCOMPARE(backSpy.size(), 1); // false->true flips only once
    }

    void unknownRouteIsRejected() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        QSignalSpy routeSpy(&controller, &manga::ui::navigation::NavigationController::routeChanged);
        controller.navigate(QStringLiteral("missing"));
        QVERIFY(presenter.presented.isEmpty());
        QCOMPARE(routeSpy.size(), 0);
        QVERIFY(!controller.canGoBack());
    }

    void failedPresentDoesNotCommitHistory() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        controller.navigate(QStringLiteral("home"));
        presenter.failNextPresent = true;
        controller.navigate(QStringLiteral("settings.general"));
        QVERIFY(!controller.canGoBack()); // the failed target never entered history
    }

    void backAndForwardRestoreRoutes() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        QSignalSpy forwardSpy(&controller, &manga::ui::navigation::NavigationController::canGoForwardChanged);
        controller.navigate(QStringLiteral("home"));
        controller.navigate(QStringLiteral("settings.general"));
        QCOMPARE(forwardSpy.size(), 0);
        controller.goBack();
        QCOMPARE(presenter.presented.last(), QStringLiteral("home"));
        QCOMPARE(forwardSpy.size(), 1);
        controller.goForward();
        QCOMPARE(presenter.presented.last(), QStringLiteral("settings.general"));
        QCOMPARE(forwardSpy.size(), 2);
    }

    void navigateAfterGoBackTruncatesForward() {
        FakePresenter presenter;
        presenter.knownRoutes.append(QStringLiteral("editor"));
        manga::ui::navigation::NavigationController controller(&presenter);
        controller.navigate(QStringLiteral("home"));
        controller.navigate(QStringLiteral("settings.general"));
        controller.goBack();
        QVERIFY(controller.canGoForward());
        controller.navigate(QStringLiteral("editor"));
        QVERIFY(!controller.canGoForward());
    }

    void reentrantNavigateIsIgnored() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        presenter.reentrancyProbe = &controller;
        presenter.reentrantRoute = QStringLiteral("settings.general");
        controller.navigate(QStringLiteral("home"));
        QCOMPARE(presenter.presented.size(), 1); // reentry is ignored
    }

    void sameRouteNavigateIsNoOp() {
        FakePresenter presenter;
        manga::ui::navigation::NavigationController controller(&presenter);
        controller.navigate(QStringLiteral("home"));
        controller.navigate(QStringLiteral("home"));
        QCOMPARE(presenter.presented.size(), 1);
    }
};

QTEST_GUILESS_MAIN(NavigationControllerTest)
#include "navigation_controller_test.moc"
