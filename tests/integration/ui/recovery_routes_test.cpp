#include "FakeComicImporter.h"
#include "application/error/ApplicationError.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "fakes/FakeProjectPorts.h"
#include "ui/error/RecoveryPresenter.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/screen/import/ImportViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using namespace manga::ui::error;

class RecoveryRoutesTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testHomeOpenProjectLockedRetryRoute() {
        qRegisterMetaType<manga::application::OpenedProjectSession>();
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;

        ui::screen::home::HomeViewModel vm(&recents, &repository, &registry);
        QSignalSpy stateSpy(&vm, &ui::screen::home::HomeViewModel::stateChanged);
        QSignalSpy openedSpy(&vm, &ui::screen::home::HomeViewModel::projectOpened);

        // 1. Fail with persistence.locked
        repository.failOpen = true;
        repository.failOpenError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                                     QStringLiteral("persistence.locked"),
                                                                     QStringLiteral("Locked by another instance"),
                                                                     application::RecoveryAction::Retry,
                                                                     true);

        vm.openProjectPath(QStringLiteral("D:/manga/locked_proj"));
        QTRY_VERIFY_WITH_TIMEOUT(vm.state().notice.has_value(), 2000);

        const auto& notice = *vm.state().notice;
        QCOMPARE(notice.severity, ui::screen::home::HomeNotice::Severity::Error);
        QCOMPARE(notice.action, application::RecoveryAction::Retry);
        QCOMPARE(notice.actionText, QStringLiteral("重试"));
        QCOMPARE(notice.retryPath, QStringLiteral("D:/manga/locked_proj"));
        QCOMPARE(notice.message, QStringLiteral("项目已在另一个 MangaTranslator 实例中打开"));

        // 2. Clear failure and retry
        repository.failOpen = false;
        vm.openProjectPath(notice.retryPath);
        QTRY_VERIFY_WITH_TIMEOUT(openedSpy.count() == 1, 2000);

        QVERIFY(!vm.state().isOpeningProject);
        QVERIFY(!vm.state().notice.has_value());
    }

    void testHomeOpenProjectNotFoundChoosePathRoute() {
        test::FakeRecentProjectsStore recents;
        test::FakeProjectRepository repository;
        application::OperationRegistry registry;

        ui::screen::home::HomeViewModel vm(&recents, &repository, &registry);
        QSignalSpy dialogSpy(&vm, &ui::screen::home::HomeViewModel::openProjectDialogRequested);

        // Fail with persistence.not_found
        repository.failOpen = true;
        repository.failOpenError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                                     QStringLiteral("persistence.not_found"),
                                                                     QStringLiteral("Not found"),
                                                                     application::RecoveryAction::ChoosePath);

        vm.openProjectPath(QStringLiteral("D:/manga/missing_proj"));
        QTRY_VERIFY_WITH_TIMEOUT(vm.state().notice.has_value(), 2000);

        const auto& notice = *vm.state().notice;
        QCOMPARE(notice.action, application::RecoveryAction::ChoosePath);
        QCOMPARE(notice.actionText, QStringLiteral("选择其他位置"));
        QCOMPARE(notice.message, QStringLiteral("项目文件夹不存在或已移动"));

        // Trigger dialog request
        vm.requestOpenProjectDialog();
        QCOMPARE(dialogSpy.count(), 1);

        // If user cancels dialog (empty path), openProjectPath is not invoked with empty path
        const int beforeOpenCount = repository.openCallCount;
        vm.openProjectPath(QString());
        QCOMPARE(repository.openCallCount, beforeOpenCount);
    }

    void testImportFailurePreservesFormAndOffersCorrection() {
        test::FakeProjectRepository repository;
        application::test::FakeComicImporter importer;
        application::OperationRegistry registry;

        ui::screen::import_::ImportViewModel vm(&repository, &importer, &registry);

        vm.addSourcePaths({QStringLiteral("D:/manga/page01.png")});
        vm.setProjectName(QStringLiteral("TestProject"));
        vm.setSaveLocation(QStringLiteral("D:/manga/output"));

        // 1. Fail at create stage with target_not_empty
        repository.failCreate = true;
        repository.failCreateError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                                       QStringLiteral("persistence.target_not_empty"),
                                                                       QStringLiteral("Directory exists"),
                                                                       application::RecoveryAction::ChoosePath);

        vm.submit();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.state().importInProgress, 2000);

        // Form preserved
        QCOMPARE(vm.state().projectName, QStringLiteral("TestProject"));
        QCOMPARE(vm.state().saveLocation, QStringLiteral("D:/manga/output"));
        QCOMPARE(vm.state().sources.size(), 1);
        QCOMPARE(vm.state().locationError, QStringLiteral("目标文件夹非空"));

        // 2. Fix location and fail at import stage with disk full
        repository.failCreate = false;
        importer.shouldFail = true;
        importer.nextError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                               QStringLiteral("persistence.disk_full"),
                                                               QStringLiteral("Disk full"),
                                                               application::RecoveryAction::FreeDiskSpace);

        vm.setSaveLocation(QStringLiteral("D:/manga/output_new"));
        vm.submit();
        QTRY_VERIFY_WITH_TIMEOUT(!vm.state().importInProgress, 2000);

        QVERIFY(vm.state().notice.has_value());
        QCOMPARE(vm.state().notice->severity, ui::screen::import_::ImportNotice::Severity::Error);
        QCOMPARE(vm.state().notice->action, application::RecoveryAction::FreeDiskSpace);
        QCOMPARE(vm.state().notice->actionText, QStringLiteral("查看位置"));
        QCOMPARE(vm.state().notice->message, QStringLiteral("磁盘空间不足，无法完成操作"));

        // Form is still preserved
        QCOMPARE(vm.state().projectName, QStringLiteral("TestProject"));
        QCOMPARE(vm.state().sources.size(), 1);
    }

    void testEditorPageLoadFailureAndRecoveryNotice() {
        application::OperationRegistry registry;
        test::FakeThumbnailStore thumbStore;
        test::FakePageImageLoader imageLoader;
        test::FakeProjectRepository repo;

        ui::screen::editor::EditorViewModel vm(&thumbStore, &imageLoader, &registry, &repo);

        application::OpenedProjectSession session;
        session.project = domain::ComicProject("Test Project");
        session.projectRootPath = QStringLiteral("D:/manga/test");
        auto page1 = domain::PageId::generate();
        session.project.addPage(domain::ComicPage(page1, "page1.png", domain::ImageSize{1200, 1800}, 0));
        static_cast<void>(session.project.markCleanIfRevision(session.project.revision()));

        imageLoader.shouldFail = true;
        imageLoader.nextError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                                  QStringLiteral("persistence.io"),
                                                                  QStringLiteral("IO error"),
                                                                  application::RecoveryAction::Retry,
                                                                  true);

        vm.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(vm.state().notice.has_value(), 2000);

        QVERIFY(vm.state().notice->retryable);
        QCOMPARE(vm.state().notice->safeMessage, QStringLiteral("暂时无法访问项目文件夹"));

        // Retry page load
        imageLoader.shouldFail = false;
        imageLoader.nextImage = QImage(1200, 1800, QImage::Format_RGB32);
        vm.retryPageLoad();

        QTRY_VERIFY_WITH_TIMEOUT(!vm.state().isPageLoading, 2000);
        QVERIFY(!vm.state().notice.has_value());
    }

    void testCancelledOperationDoesNotProduceErrorNotice() {
        application::OperationRegistry registry;
        test::FakeThumbnailStore thumbStore;
        test::FakePageImageLoader imageLoader;
        test::FakeProjectRepository repo;

        ui::screen::editor::EditorViewModel vm(&thumbStore, &imageLoader, &registry, &repo);

        application::OpenedProjectSession session;
        session.project = domain::ComicProject("Test Project");
        session.projectRootPath = QStringLiteral("D:/manga/test");
        auto page1 = domain::PageId::generate();
        session.project.addPage(domain::ComicPage(page1, "page1.png", domain::ImageSize{1200, 1800}, 0));
        static_cast<void>(session.project.markCleanIfRevision(session.project.revision()));

        imageLoader.shouldFail = true;
        imageLoader.nextError = application::makeApplicationError(
            application::ErrorCategory::Cancelled, QStringLiteral("cancelled"), QStringLiteral("Cancelled"));

        vm.openProjectSession(std::move(session));
        QTest::qWait(100);

        // Cancelled errors should NOT populate notice in EditorState
        QVERIFY(!vm.state().notice.has_value());
    }
};

QTEST_MAIN(RecoveryRoutesTest)
#include "recovery_routes_test.moc"
