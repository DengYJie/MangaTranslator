#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeProjectPorts.h"
#include "ui/screen/home/HomePage.h"
#include "ui/screen/home/HomeViewModel.h"

#include <FluentQt/basicinput.h>
#include <QtTest>

class HomePageTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void emptyStateVisibleWithoutRecents() {
        manga::test::FakeRecentProjectsStore recents;
        manga::test::FakeProjectRepository repository;
        manga::application::OperationRegistry registry;
        manga::ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        manga::ui::screen::home::HomePage page(&viewModel);
        page.show();
        viewModel.refresh();
        QVERIFY(page.isEmptyStateVisibleForTest());
    }

    void listAppearsWithRecents() {
        manga::test::FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100}};
        manga::test::FakeProjectRepository repository;
        manga::application::OperationRegistry registry;
        manga::ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        manga::ui::screen::home::HomePage page(&viewModel);
        page.show();
        viewModel.refresh();
        QVERIFY(!page.isEmptyStateVisibleForTest());
    }

    void importButtonEmitsImportRequested() {
        manga::test::FakeRecentProjectsStore recents;
        manga::test::FakeProjectRepository repository;
        manga::application::OperationRegistry registry;
        manga::ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        manga::ui::screen::home::HomePage page(&viewModel);
        page.show();
        QSignalSpy spy(&page, &manga::ui::screen::home::HomePage::importRequested);
        page.importButtonForTest()->click();
        QCOMPARE(spy.size(), 1);
    }
};

QTEST_MAIN(HomePageTest)
#include "home_page_test.moc"
