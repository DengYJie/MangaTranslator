#include "application/mapping/ProjectMapper.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeProjectPorts.h"
#include "ui/screen/home/HomeViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;

class HomeViewModelTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void refreshLoadsRecentsIntoState() {
        test::FakeRecentProjectsStore recents;
        recents.m_projects = {{QStringLiteral("A"), QStringLiteral("D:/a"), 100},
                              {QStringLiteral("B"), QStringLiteral("D:/b"), 200}};
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        QSignalSpy spy(&viewModel, &ui::screen::home::HomeViewModel::stateChanged);
        viewModel.refresh();
        QVERIFY(spy.size() >= 1);
        QCOMPARE(viewModel.state().recentProjects.size(), 2);
    }

    void openSuccessAddsRecentAndEmitsProjectOpened() {
        qRegisterMetaType<manga::application::ProjectSessionDescriptor>();
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
        const auto session = openedSpy.first().first().value<manga::application::ProjectSessionDescriptor>();
        QCOMPARE(session.projectName, QStringLiteral("Demo Project"));
    }

    void projectOpenedCarriesFullSessionDescriptor() {
        qRegisterMetaType<manga::application::ProjectSessionDescriptor>();
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        repository.openPageCount = 3; // 见 FakeProjectPorts.h 修订
        application::OperationRegistry registry;
        ui::screen::home::HomeViewModel viewModel(&recents, &repository, &registry);
        QSignalSpy spy(&viewModel, &manga::ui::screen::home::HomeViewModel::projectOpened);
        viewModel.openProjectPath(QStringLiteral("D:/manga/demo"));
        QTRY_VERIFY_WITH_TIMEOUT(spy.size() == 1, 2000);
        const auto session = spy.first().first().value<manga::application::ProjectSessionDescriptor>();
        QVERIFY(session.projectId.isValid());
        QCOMPARE(session.projectRootPath, QStringLiteral("D:/manga/demo"));
        QCOMPARE(session.projectName, QStringLiteral("Demo Project"));
        QCOMPARE(session.pages.size(), 3);
        for (int i = 0; i < 3; ++i) {
            QVERIFY(session.pages.at(i).id.isValid());
            QCOMPARE(session.pages.at(i).orderIndex, i);
            QCOMPARE(session.pages.at(i).imageSize, (manga::domain::ImageSize{1200, 1800}));
            QVERIFY(session.pages.at(i).originalAssetPath.startsWith(QStringLiteral("assets/original/")));
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
};

QTEST_GUILESS_MAIN(HomeViewModelTest)
#include "home_view_model_test.moc"
