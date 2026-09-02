#include "ui/screen/home/HomePage.h"

#include "application/mapping/ProjectMapper.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeProjectPorts.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/screen/home/RecentProjectsModel.h"

#include <FluentQt/Collections.h>
#include <FluentQt/basicinput.h>
#include <QSignalSpy>
#include <QtTest>

using namespace manga;

class HomePageTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<manga::application::OpenedProjectSession>();
    }

    // --- ViewModel Tests ---

    void constructionLoadsPersistedRecentsIntoState() {
        test::FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100},
                              {QStringLiteral("B"), QStringLiteral("D:/b"), 200}};
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        QCOMPARE(viewModel.state().recentProjects.size(), 2);
    }

    void openSuccessAddsRecentAndEmitsProjectOpened() {
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        QSignalSpy openedSpy(&viewModel, &ui::screen::home::HomeViewModel::projectOpened);
        QSignalSpy toastSpy(&viewModel, &ui::screen::home::HomeViewModel::toastRequested);
        viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
        QTRY_VERIFY_WITH_TIMEOUT(openedSpy.size() == 1, 2000);
        QCOMPARE(recents.m_projects.size(), 1);
        QCOMPARE(recents.m_projects.first().name, QStringLiteral("Demo Project"));
        QCOMPARE(toastSpy.size(), 1);
        QVERIFY(!viewModel.state().isOpeningProject);
        QVERIFY(!viewModel.state().notice.has_value());
        const auto session = openedSpy.first().first().value<manga::application::OpenedProjectSession>();
        QCOMPARE(QString::fromStdString(session.project.name()), QStringLiteral("Demo Project"));
        QCOMPARE(session.projectRootPath, QStringLiteral("D:/manga/demo"));
    }

    void openedSessionCarriesEditableAggregate() {
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        repository.openPageCount = 1;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        QSignalSpy spy(&viewModel, &ui::screen::home::HomeViewModel::projectOpened);

        viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() == 1, 2000);
        const auto session = spy.takeFirst().at(0).value<manga::application::OpenedProjectSession>();
        QVERIFY(session.project.id().isValid());
        QCOMPARE(session.project.pages().size(), 1);
        QCOMPARE(session.projectRootPath, QStringLiteral("D:/manga/demo"));
    }

    void projectOpenedCarriesFullSessionDescriptor() {
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        repository.openPageCount = 3;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        QSignalSpy spy(&viewModel, &manga::ui::screen::home::HomeViewModel::projectOpened);
        viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() == 1, 2000);
        const auto session = spy.first().first().value<manga::application::OpenedProjectSession>();
        QVERIFY(session.project.id().isValid());
        QCOMPARE(session.projectRootPath, QStringLiteral("D:/manga/demo"));
        QCOMPARE(QString::fromStdString(session.project.name()), QStringLiteral("Demo Project"));
        QCOMPARE(session.project.pages().size(), 3);
        for (int i = 0; i < 3; ++i) {
            const auto& page = session.project.pages().at(i);
            QVERIFY(page.id().isValid());
            QCOMPARE(page.orderIndex(), i);
            QCOMPARE(page.imageSize(), (manga::domain::ImageSize{1200, 1800}));
            QVERIFY(QString::fromStdString(page.originalAssetPath()).startsWith(QStringLiteral("assets/original/")));
        }
    }

    void openFailureShowsErrorNoticeWithRetryPath() {
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        repository.failOpen = true;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        viewModel.openProjectPath(QStringLiteral("D:/manga/broken"));
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.state().notice.has_value(), 2000);
        QCOMPARE(viewModel.state().notice->severity, ui::screen::home::HomeNotice::Severity::Error);
        QCOMPARE(viewModel.state().notice->retryPath, QStringLiteral("D:/manga/broken"));
        QVERIFY(!viewModel.state().isOpeningProject);
        QVERIFY(recents.m_projects.isEmpty());
    }

    void concurrentOpenIsIgnored() {
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
        viewModel.openProjectPath(QStringLiteral("D:/manga/other")); // Ignored while opening
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isOpeningProject, 2000);
        QCOMPARE(repository.lastOpenedPath, QStringLiteral("D:/manga/demo"));
    }

    void removeRecentUpdatesState() {
        test::FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100}};
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        viewModel.refresh();
        viewModel.removeRecent(QStringLiteral("D:/a"));
        QVERIFY(viewModel.state().recentProjects.isEmpty());
    }

    // --- Page / UI Tests ---

    void emptyStateVisibleWithoutRecents() {
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        ui::screen::home::HomePage page(&viewModel);
        page.show();
        viewModel.refresh();
        QVERIFY(page.isEmptyStateVisibleForTest());
    }

    void listAppearsWithRecents() {
        test::FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100}};
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        ui::screen::home::HomePage page(&viewModel);
        page.show();
        viewModel.refresh();
        QVERIFY(!page.isEmptyStateVisibleForTest());
    }

    void importButtonEmitsImportRequested() {
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        ui::screen::home::HomePage page(&viewModel);
        page.show();
        QSignalSpy spy(&page, &ui::screen::home::HomePage::importRequested);
        page.importButtonForTest()->click();
        QCOMPARE(spy.size(), 1);
    }

    void clickingRecentProjectRequestsOpen() {
        test::FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100}};
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        ui::screen::home::HomePage page(&viewModel);
        page.show();
        viewModel.refresh();

        QSignalSpy spy(&viewModel, &ui::screen::home::HomeViewModel::projectOpenRequested);
        auto* grid = page.recentProjectsGridForTest();
        QVERIFY(grid != nullptr);
        const auto index = grid->model()->index(0, 0);
        QVERIFY(index.isValid());
        Q_EMIT grid->clicked(index);

        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.first().at(0).toString(), QStringLiteral("D:/a"));
    }
};

QTEST_MAIN(HomePageTest)
#include "HomePageTest.moc"
