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
        auto* translateBtn = commandBar->findChild<fluent::basicinput::Button*>(QStringLiteral("smartTranslateButton"));
        QVERIFY(translateBtn != nullptr);
        auto* adoptBtn = commandBar->findChild<fluent::basicinput::Button*>(QStringLiteral("adoptButton"));
        QVERIFY(adoptBtn != nullptr);
        auto* rejectBtn = commandBar->findChild<fluent::basicinput::Button*>(QStringLiteral("rejectButton"));
        QVERIFY(rejectBtn != nullptr);
    }

    void commandBarShowsAdoptRejectOnProposal() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.show();

        auto session = makeSession(1);
        viewModel.openProjectSession(session);

        auto* commandBar = page.findChild<QWidget*>(QStringLiteral("editorCommandBar"));
        auto* adoptBtn = commandBar->findChild<fluent::basicinput::Button*>(QStringLiteral("adoptButton"));
        auto* rejectBtn = commandBar->findChild<fluent::basicinput::Button*>(QStringLiteral("rejectButton"));

        QVERIFY(!adoptBtn->isVisible());
        QVERIFY(!rejectBtn->isVisible());

        // Update processing state to hasProposal = true
        ui::screen::editor::EditorProcessingState proc;
        proc.activePageId = pageAt(session, 0);
        proc.hasProposal = true;
        proc.status = ui::screen::editor::ProcessingStatus::NeedsReview;
        viewModel.updateProcessingState(proc);

        QVERIFY(adoptBtn->isVisible());
        QVERIFY(rejectBtn->isVisible());
    }

    void commandBarVisualStylingAndTooltip() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        auto* commandBar = page.findChild<QWidget*>(QStringLiteral("editorCommandBar"));
        auto* translateBtn = commandBar->findChild<fluent::basicinput::Button*>(QStringLiteral("smartTranslateButton"));
        QVERIFY(translateBtn != nullptr);
        QCOMPARE(translateBtn->fluentStyle(), fluent::basicinput::Button::Accent);

        // Open session with page containing 0 regions
        auto session = makeSession(1);
        viewModel.openProjectSession(session);

        // Page has 0 regions -> button should be disabled with descriptive tooltip
        QVERIFY(!translateBtn->isEnabled());
        QVERIFY(translateBtn->toolTip().contains(QStringLiteral("无文本区域")) ||
                translateBtn->toolTip().contains(QStringLiteral("区域")));

        ui::screen::editor::EditorProcessingState processing;
        processing.activePageId = pageAt(session, 0);
        processing.isRunning = true;
        viewModel.updateProcessingState(processing);
        QVERIFY(translateBtn->text().isEmpty());
    }

    void inspectorActionButtonsExist() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        auto* ocrBtn = page.findChild<fluent::basicinput::Button*>(QStringLiteral("inspectorSingleOcr"));
        auto* trBtn = page.findChild<fluent::basicinput::Button*>(QStringLiteral("inspectorSingleTranslate"));
        auto* clearBtn = page.findChild<fluent::basicinput::Button*>(QStringLiteral("inspectorSingleClear"));
        auto* layoutBtn = page.findChild<fluent::basicinput::Button*>(QStringLiteral("inspectorSingleLayout"));

        QVERIFY(ocrBtn != nullptr);
        QVERIFY(trBtn != nullptr);
        QVERIFY(clearBtn != nullptr);
        QVERIFY(layoutBtn != nullptr);
    }

    void unavailableOcrDisablesInspectorActionsWithGuidance() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        viewModel.setOcrAvailability(false, QStringLiteral("manifest.json not found"));
        ui::screen::editor::EditorPage page(&viewModel);

        auto* singleButton = page.findChild<fluent::basicinput::Button*>(QStringLiteral("inspectorSingleOcr"));
        auto* multiButton = page.findChild<fluent::basicinput::Button*>(QStringLiteral("inspectorMultiOcr"));
        QVERIFY(singleButton != nullptr);
        QVERIFY(multiButton != nullptr);
        QVERIFY(!singleButton->isEnabled());
        QVERIFY(!multiButton->isEnabled());
        QVERIFY(singleButton->toolTip().contains(QStringLiteral("manifest.json")));
        QVERIFY(singleButton->toolTip().contains(QStringLiteral("设置 > OCR")));
    }
};

QTEST_MAIN(EditorPageTest)
#include "editor_page_test.moc"
