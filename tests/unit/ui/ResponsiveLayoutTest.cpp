#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorPage.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/screen/home/HomePage.h"
#include "ui/screen/home/HomeViewModel.h"
#include "ui/screen/import/ImportPage.h"
#include "ui/screen/import/ImportViewModel.h"
#include "ui/screen/settings/SettingsPage.h"
#include "ui/screen/settings/SettingsViewModel.h"
#include "ui/widget/AppStatusTaskStrip.h"
#include "ui/widget/EditorCommandBar.h"
#include "ui/widget/InspectorPanel.h"
#include "ui/widget/PageThumbnailList.h"

#include <FluentQt/FluentQt.h>
#include <QSplitter>
#include <QtTest>

using namespace manga;

namespace {
application::OpenedProjectSession makeTestSession(int pageCount) {
    application::OpenedProjectSession session;
    session.project = domain::ComicProject("Responsive Test Project");
    session.projectRootPath = QStringLiteral("D:/manga/responsive_test");
    for (int i = 0; i < pageCount; ++i) {
        session.project.addPage(
            domain::ComicPage(domain::PageId::generate(),
                              QStringLiteral("assets/original/page_%1.png").arg(1000 + i).toStdString(),
                              domain::ImageSize{1200, 1800},
                              i));
    }
    return session;
}
} // namespace

class ResponsiveLayoutTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<ui::screen::editor::PageImageDelivery>();
    }

    void testEditorBreakpointsWideAndStandard() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        // Wide Breakpoint (>= 1440)
        page.resize(1600, 900);
        page.show();
        QCOMPARE(page.breakpointModeForTest(), ui::screen::editor::EditorPage::BreakpointMode::Wide);
        QVERIFY(page.pagePaneCardForTest()->isVisible());
        QVERIFY(page.inspectorCardForTest()->isVisible());
        auto sizes = page.splitterForTest()->sizes();
        QCOMPARE(sizes[0], 232);
        QCOMPARE(sizes[2], 352);

        // Standard Breakpoint (1200 - 1439)
        page.resize(1300, 900);
        QCOMPARE(page.breakpointModeForTest(), ui::screen::editor::EditorPage::BreakpointMode::Standard);
        QVERIFY(page.pagePaneCardForTest()->isVisible());
        QVERIFY(page.inspectorCardForTest()->isVisible());
        sizes = page.splitterForTest()->sizes();
        QCOMPARE(sizes[0], 220);
        QCOMPARE(sizes[2], 328);
    }

    void testEditorBreakpointCompact() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        // Compact Breakpoint (1008 - 1199)
        page.resize(1100, 800);
        page.show();
        QCOMPARE(page.breakpointModeForTest(), ui::screen::editor::EditorPage::BreakpointMode::Compact);
        QVERIFY(page.pagePaneCardForTest()->isVisible());
        QVERIFY(page.inspectorCardForTest()->isVisible());
        auto sizes = page.splitterForTest()->sizes();
        QVERIFY(sizes[0] >= 80 && sizes[0] <= 110); // Compact thumbnail pane
        QVERIFY(sizes[2] >= 300 && sizes[2] <= 340);
    }

    void testEditorBreakpointNarrowAndMinimum() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        // Narrow Breakpoint (720 - 1007)
        page.show();
        page.resize(800, 700);
        QApplication::processEvents();
        QCOMPARE(page.breakpointModeForTest(), ui::screen::editor::EditorPage::BreakpointMode::Narrow);
        // By default in Narrow, both drawers are closed to grant Canvas full viewport
        QVERIFY(!page.isPageDrawerVisible());
        QVERIFY(!page.isInspectorDrawerVisible());

        // Minimum Breakpoint (< 720)
        page.resize(600, 600);
        QApplication::processEvents();
        QCOMPARE(page.breakpointModeForTest(), ui::screen::editor::EditorPage::BreakpointMode::Minimum);
        QVERIFY(!page.isPageDrawerVisible());
        QVERIFY(!page.isInspectorDrawerVisible());
    }

    void testEditorDrawerExclusivityInNarrowMode() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        page.resize(850, 700);
        page.show();

        // Open Page Drawer
        page.setPageDrawerVisible(true);
        QVERIFY(page.isPageDrawerVisible());
        QVERIFY(!page.isInspectorDrawerVisible());

        // Opening the Inspector drawer closes the Page drawer, because the two are mutually exclusive.
        page.setInspectorDrawerVisible(true);
        QVERIFY(!page.isPageDrawerVisible());
        QVERIFY(page.isInspectorDrawerVisible());

        // Toggling the only open drawer closes it.
        page.toggleInspectorPane();
        QVERIFY(!page.isInspectorDrawerVisible());
        QVERIFY(!page.isPageDrawerVisible());
    }

    void testEditorOverlayDoesNotOverwriteDesktopWidth() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        page.resize(850, 700);
        page.show();
        page.setPageDrawerVisible(true);
        QVERIFY(page.isPageDrawerVisible());

        // Resize back to Wide (1600)
        page.resize(1600, 900);
        QCOMPARE(page.breakpointModeForTest(), ui::screen::editor::EditorPage::BreakpointMode::Wide);
        QVERIFY(page.pagePaneCardForTest()->isVisible());
        QVERIFY(page.inspectorCardForTest()->isVisible());
        auto sizes = page.splitterForTest()->sizes();
        QCOMPARE(sizes[0], 232);
        QCOMPARE(sizes[2], 352);
    }

    void testEditorCanvasPreservesViewportCenterOnResize() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        images.nextImage = QImage(1200, 1800, QImage::Format_RGBA8888);
        images.nextImage.fill(Qt::white);
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        page.resize(1440, 900);
        page.show();
        viewModel.openProjectSession(makeTestSession(1));
        QTRY_VERIFY_WITH_TIMEOUT(page.canvasForTest()->contentState() == ui::manga::MangaCanvas::ContentState::Image,
                                 2000);

        page.canvasForTest()->setZoomFactor(1.5);
        const QPointF targetCenter(600.0, 900.0);
        page.canvasForTest()->centerOnPage(targetCenter);

        // Resizing to narrow must keep the page center rather than moving the viewport to (0, 0).
        page.resize(800, 700);
        QPointF centerAfter = page.canvasForTest()->viewCenterOnPage();
        QVERIFY(centerAfter.x() > 0.0 && centerAfter.y() > 0.0);
    }

    void testPrimaryCommandsReachableAcrossBreakpoints() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        page.show();
        auto* bar = page.commandBarForTest();
        QVERIFY(bar != nullptr);
        QVERIFY(bar->smartTranslateButtonForTest() != nullptr);
        QVERIFY(bar->undoButtonForTest() != nullptr);
        QVERIFY(bar->redoButtonForTest() != nullptr);
        QVERIFY(bar->saveButtonForTest() != nullptr);

        const int testWidths[] = {1600, 1300, 1100, 800, 600};
        for (int w : testWidths) {
            page.resize(w, 700);
            QVERIFY(bar->smartTranslateButtonForTest()->isVisible());
            QVERIFY(bar->undoButtonForTest()->isVisible());
            QVERIFY(bar->redoButtonForTest()->isVisible());
            QVERIFY(bar->saveButtonForTest()->isVisible());
        }
    }

    void testNoDuplicateSimultaneouslyVisibleActions() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        auto* bar = page.commandBarForTest();

        // Only one smart translate button is visible
        auto allSmartTranslateButtons =
            bar->findChildren<fluent::basicinput::Button*>(QStringLiteral("smartTranslateButton"));
        QCOMPARE(allSmartTranslateButtons.size(), 1);
    }

    void testFlatHeightCompressesCommandBarWithoutDroppingActions() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);

        page.resize(1200, 500); // height < 560 (Flat height)
        page.show();

        auto* bar = page.commandBarForTest();
        QVERIFY(bar->isVisible());
        QVERIFY(bar->smartTranslateButtonForTest()->isVisible());
        QVERIFY(bar->undoButtonForTest()->isVisible());
        QVERIFY(bar->redoButtonForTest()->isVisible());
        QVERIFY(bar->saveButtonForTest()->isVisible());
    }

    void testHomeResponsiveLayout() {
        ui::screen::home::HomeViewModel viewModel(nullptr, nullptr, nullptr);
        ui::screen::home::HomePage page(&viewModel);

        page.resize(1200, 800);
        page.show();
        // At >= 1008, layout direction is LeftToRight (side rail on right)
        page.resize(800, 800);
        // At < 1008, layout direction is TopToBottom
        QVERIFY(page.isVisible());
    }

    void testImportResponsiveLayout() {
        ui::screen::import_::ImportViewModel viewModel(nullptr, nullptr, nullptr);
        ui::screen::import_::ImportPage page(&viewModel);

        page.resize(1200, 800);
        page.show();
        page.resize(800, 800);
        QVERIFY(page.isVisible());
    }

    void testTaskStripResponsiveBreakpoints() {
        ui::widget::AppStatusTaskStrip strip;
        strip.resize(1600, 32);
        strip.show();

        // 1600 (Wide)
        strip.setDocumentStatus({ui::widget::DocumentSaveStatus::Saved, QStringLiteral("已保存"), {}});
        strip.resize(1600, 32);

        // 800 (Medium)
        strip.resize(800, 32);

        // 500 (Narrow)
        strip.resize(500, 32);
        QVERIFY(strip.isVisible());
    }
};

QTEST_MAIN(ResponsiveLayoutTest)
#include "ResponsiveLayoutTest.moc"
