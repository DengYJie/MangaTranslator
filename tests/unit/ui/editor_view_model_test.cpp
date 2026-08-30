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

application::ProjectSessionDescriptor makeSession(int pageCount) {
    application::ProjectSessionDescriptor session;
    session.projectId = domain::ProjectId::generate();
    session.projectRootPath = QStringLiteral("D:/manga/demo");
    session.projectName = QStringLiteral("Demo Project");
    for (int i = 0; i < pageCount; ++i) {
        application::PageDescriptor page;
        page.id = domain::PageId::generate();
        page.orderIndex = i;
        page.imageSize = domain::ImageSize{1200, 1800};
        page.originalAssetPath = QStringLiteral("assets/original/page_%1.png").arg(1000 + i);
        session.pages.append(page);
    }
    return session;
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
        viewModel.selectPage(session.pages.at(1).id);
        QVERIFY(viewModel.state().viewGeneration > generationAfterOpen);
        QCOMPARE(viewModel.state().currentPageId, session.pages.at(1).id);
        f.imageLoader.completeAll(); // 旧页完成：取消后静默；新页完成：投递
        QTRY_VERIFY_WITH_TIMEOUT(pageSpy.size() == 1, 2000);
        QCOMPARE(pageSpy.first().first().value<PageImageDelivery>().pageId, session.pages.at(1).id);
    }

    void cancelledLoadCompletionIsSilent() {
        Fixture f;
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        viewModel.selectPage(session.pages.at(1).id); // 取消第一页在途加载
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        QVERIFY(!viewModel.state().notice.has_value()); // 取消不产生错误提示
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
        f.imageLoader.deferred = true; // store miss -> generate path -> loader 在途
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(6);
        viewModel.openProjectSession(session);
        f.imageLoader.completeAll(); // 让首页原图加载完成，不影响缩略图统计
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        for (const auto& page : session.pages) {
            viewModel.requestThumbnail(page.id, false);
        }
        // 4 个在途（loader pending），2 个在队列
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 4, 2000);
        f.imageLoader.completeAll(); // 完成一批，泵入剩余
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
        const auto pageId = session.pages.first().id;
        viewModel.requestThumbnail(pageId, false);
        viewModel.requestThumbnail(pageId, false); // 在途去重
        viewModel.requestThumbnail(pageId, false); // 再试一次
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
        viewModel.requestThumbnail(session.pages.at(0).id, false);
        viewModel.requestThumbnail(session.pages.at(1).id, false);
        QTRY_VERIFY_WITH_TIMEOUT(f.imageLoader.pendingCount() == 2, 2000);
        viewModel.selectPage(session.pages.at(1).id); // bump viewGeneration
        QCOMPARE(invalidatedSpy.size(), 1);
        f.imageLoader.completeAll(); // 缩略图完成但 generation 已过 + 新页原图加载
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        QCOMPARE(readySpy.size(), 0); // 缩略图迟到结果被静默丢弃
    }

    void perPageViewTransformIsRestoredOnRevisit() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QSignalSpy pageSpy(&viewModel, &EditorViewModel::pageImageReady);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        viewModel.noteViewTransform(2.5, QPointF(300.0, 450.0));
        viewModel.selectPage(session.pages.at(1).id);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        viewModel.selectPage(session.pages.at(0).id);
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
};

QTEST_GUILESS_MAIN(EditorViewModelTest)
#include "editor_view_model_test.moc"
