#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorPage.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"

#include <FluentQt/FluentQt.h>
#include <QPushButton>
#include <QSignalSpy>
#include <QtTest>

using namespace manga;

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
} // namespace

class EditorPageTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<ui::screen::editor::PageImageDelivery>();
    }

    void noProjectShowsOpenPlaceholder() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.show();
        QCOMPARE(page.canvasForTest()->contentState(), ui::manga::MangaCanvas::ContentState::NoProject);
        QCOMPARE(page.pageListForTest()->model()->rowCount(), 0);
        QSignalSpy spy(&page, &ui::screen::editor::EditorPage::openProjectRequested);
        QTest::mouseClick(page.canvasForTest()->placeholderActionButtonForTest(), Qt::LeftButton);
        QCOMPARE(spy.size(), 1);
    }

    void pagePaneUsesFluentCardSurface() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        auto* pagePaneCard = page.findChild<fluent::layout::Card*>(QStringLiteral("editorPagePaneCard"));

        QVERIFY(pagePaneCard != nullptr);
        QCOMPARE(pagePaneCard->appearance(), fluent::layout::Card::Layer);
        QVERIFY(page.pageListForTest()->parentWidget() == pagePaneCard);
    }

    void openSessionPopulatesListAndCanvas() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        images.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        images.nextImage.fill(Qt::white);
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.resize(1000, 700);
        page.show();
        viewModel.openProjectSession(makeSession(3));
        QCOMPARE(page.pageListForTest()->model()->rowCount(), 3);
        QTRY_VERIFY_WITH_TIMEOUT(page.canvasForTest()->contentState() == ui::manga::MangaCanvas::ContentState::Image,
                                 2000);
    }

    void pageListSelectionSwitchesCurrentPage() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        images.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        images.nextImage.fill(Qt::white);
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.resize(1000, 700);
        page.show();
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        Q_EMIT page.pageListForTest()->pageActivated(pageAt(session, 1));
        QCOMPARE(viewModel.state().currentPageId, pageAt(session, 1));
    }

    void emptyProjectShowsImportPlaceholder() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.show();
        viewModel.openProjectSession(makeSession(0));
        QCOMPARE(page.canvasForTest()->contentState(), ui::manga::MangaCanvas::ContentState::NoPage);
        QSignalSpy spy(&page, &ui::screen::editor::EditorPage::importPagesRequested);
        QTest::mouseClick(page.canvasForTest()->placeholderActionButtonForTest(), Qt::LeftButton);
        QCOMPARE(spy.size(), 1);
    }
    void commandBarIsIntegrated() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        auto* commandBar = page.findChild<QWidget*>(QStringLiteral("editorCommandBar"));
        QVERIFY(commandBar != nullptr);
    }
};

QTEST_MAIN(EditorPageTest)
#include "editor_page_test.moc"
