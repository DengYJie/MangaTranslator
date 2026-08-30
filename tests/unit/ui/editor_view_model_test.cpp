#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/base/DomainMetaTypes.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using manga::ui::screen::editor::EditorViewModel;
using manga::ui::screen::editor::PageImageDelivery;

namespace {

application::OpenedProjectSession makeSession(int pageCount) {
    application::OpenedProjectSession session;
    session.project = domain::ComicProject("Demo Project");
    session.projectRootPath = QStringLiteral("D:/manga/demo");
    for (int i = 0; i < pageCount; ++i) {
        session.project.addPage(
            domain::ComicPage(domain::PageId::generate(),
                              QStringLiteral("assets/original/page_%1.png").arg(1000 + i).toStdString(),
                              domain::ImageSize{1200, 1800},
                              i));
    }
    return session;
}

domain::PageId pageAt(const application::OpenedProjectSession& session, int index) {
    return session.project.pages().at(index).id();
}

struct Fixture {
    test::FakeThumbnailStore thumbnailStore;
    test::FakePageImageLoader imageLoader;
    application::OperationRegistry registry;
    explicit Fixture() {
        imageLoader.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        imageLoader.nextImage.fill(Qt::gray);
        thumbnailStore.readImage = QImage(16, 24, QImage::Format_RGBA8888);
    }
};

} // namespace

class EditorViewModelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<PageImageDelivery>();
    }

    void openProjectSessionSelectsFirstPageAndLoadsImage() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        viewModel.openProjectSession(makeSession(3));
        QCOMPARE(viewModel.state().pages.size(), 3);
        QCOMPARE(viewModel.state().currentPageId, viewModel.state().pages.first().id);
        QVERIFY(viewModel.state().isPageLoading);
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 1, 2000);
        QVERIFY(!viewModel.state().isPageLoading);
        QCOMPARE(pageSpy.first().first().value<PageImageDelivery>().pageId, viewModel.state().pages.first().id);
        QVERIFY(!pageSpy.first().first().value<PageImageDelivery>().hasSavedTransform);
    }

    void openEmptyProjectStaysIdle() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        viewModel.openProjectSession(makeSession(0));
        QVERIFY(viewModel.state().pages.isEmpty());
        QVERIFY(!viewModel.state().currentPageId.isValid());
        QVERIFY(!viewModel.state().isPageLoading);
        QCOMPARE(f.imageLoader.callCount, 0);
        QCOMPARE(pageSpy.size(), 0);
    }

    void pageSwitchBumpsViewGenerationAndCancelsPreviousLoad() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        const quint64 generationAfterOpen = viewModel.state().viewGeneration;
        viewModel.selectPage(pageAt(session, 1));
        QVERIFY(viewModel.state().viewGeneration > generationAfterOpen);
        QCOMPARE(viewModel.state().currentPageId, pageAt(session, 1));
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 1, 2000);
        QCOMPARE(pageSpy.first().first().value<PageImageDelivery>().pageId, pageAt(session, 1));
    }

    void cancelledLoadCompletionIsSilent() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        viewModel.selectPage(pageAt(session, 1));
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        QVERIFY(!viewModel.state().notice.has_value());
    }

    void failedPageLoadShowsRetryableNoticeAndRetryReloads() {
        Fixture f;
        f.imageLoader.shouldFail = true;
        f.imageLoader.nextError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                                    QStringLiteral("io.decode_failed"),
                                                                    QStringLiteral("无法解码页面图像"),
                                                                    application::RecoveryAction::Retry,
                                                                    true);
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.openProjectSession(makeSession(1));
        QTRY_VERIFY_WITH_TIMEOUT(viewModel.state().notice.has_value(), 2000);
        QVERIFY(viewModel.state().notice->retryable);
        QVERIFY(!viewModel.state().isPageLoading);

        f.imageLoader.shouldFail = false;
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        viewModel.retryPageLoad();
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 1, 2000);
        QVERIFY(!viewModel.state().notice.has_value());
    }

    void thumbnailConcurrencyIsCappedAtFourAndQueuePumps() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(6);
        viewModel.openProjectSession(session);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        for (const auto& page : session.project.pages()) {
            viewModel.requestThumbnail(page.id(), false);
        }
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 4, 2000);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 2, 2000);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(f.thumbnailStore.writeCount == 6, 2000);
    }

    void duplicateThumbnailRequestIsDeduplicated() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(1);
        viewModel.openProjectSession(session);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        const auto firstPageId = pageAt(session, 0);
        viewModel.requestThumbnail(firstPageId, false);
        viewModel.requestThumbnail(firstPageId, false);
        viewModel.requestThumbnail(firstPageId, false);
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 1, 2000);
    }

    void staleThumbnailCompletionIsDiscardedAfterPageSwitch() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        QSignalSpy readySpy(&viewModel, &EditorViewModel::thumbnailReady);
        QSignalSpy invalidatedSpy(&viewModel, &EditorViewModel::thumbnailsInvalidated);
        viewModel.requestThumbnail(pageAt(session, 0), false);
        viewModel.requestThumbnail(pageAt(session, 1), false);
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 2, 2000);
        viewModel.selectPage(pageAt(session, 1));
        QCOMPARE(invalidatedSpy.size(), 1);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        QCOMPARE(readySpy.size(), 0);
    }

    void perPageViewTransformIsRestoredOnRevisit() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        viewModel.noteViewTransform(2.5, QPointF(300.0, 450.0));
        viewModel.selectPage(pageAt(session, 1));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        viewModel.selectPage(pageAt(session, 0));
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 3, 2000);
        const auto delivery = pageSpy.at(2).first().value<PageImageDelivery>();
        QVERIFY(delivery.hasSavedTransform);
        QCOMPARE(delivery.zoom, 2.5);
        QCOMPARE(delivery.centerOnPage, QPointF(300.0, 450.0));
    }

    void closeProjectSessionResetsState() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.openProjectSession(makeSession(2));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        viewModel.closeProjectSession();
        QVERIFY(!viewModel.state().projectId.isValid());
        QVERIFY(viewModel.state().pages.isEmpty());
        QVERIFY(!viewModel.state().currentPageId.isValid());
    }

    void openSessionRetainsAggregateForEditing() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(2);
        const quint64 initialRevision = session.project.revision();
        viewModel.openProjectSession(std::move(session));
        QCOMPARE(viewModel.currentProjectRevision(), initialRevision);
        viewModel.closeProjectSession();
        QCOMPARE(viewModel.currentProjectRevision(), quint64{0});
    }
};

QTEST_GUILESS_MAIN(EditorViewModelTest)
#include "editor_view_model_test.moc"
