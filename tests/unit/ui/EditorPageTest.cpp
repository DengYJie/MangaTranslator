#include "ui/screen/editor/EditorPage.h"

#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "rendering/paint/StrokePaintRasterizer.h"
#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorViewModel.h"
#include "ui/widget/EditorCommandBar.h"
#include "ui/widget/PageListModel.h"
#include "ui/widget/PageThumbnailList.h"

#include <FluentQt/FluentQt.h>
#include <QFrame>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>

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
        qRegisterMetaType<application::OperationId>();
        qRegisterMetaType<ui::screen::editor::PageImageDelivery>();
        qRegisterMetaType<ui::screen::editor::ManualOverrideDelivery>();
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

        // The disabled button carries a tooltip naming the missing text regions.
        QVERIFY(!translateBtn->isEnabled());
        QVERIFY(translateBtn->toolTip().contains(QStringLiteral("无文本区域")) ||
                translateBtn->toolTip().contains(QStringLiteral("区域")));

        ui::screen::editor::EditorProcessingState processing;
        processing.activePageId = pageAt(session, 0);
        processing.isRunning = true;
        viewModel.updateProcessingState(processing);
        QVERIFY(translateBtn->text().isEmpty());
    }

    void commandbarShowsOcrOnlyWhenRegionSelected() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        page.show();

        auto* commandBar = page.findChild<QWidget*>(QStringLiteral("editorCommandBar"));
        auto* ocrButton = commandBar->findChild<fluent::basicinput::Button*>(QStringLiteral("ocrButton"));
        QVERIFY(ocrButton != nullptr);
        QVERIFY(!ocrButton->isVisibleTo(commandBar));

        auto session = makeSession(1);
        domain::TextRegion region(domain::RegionId::generate(), domain::RectGeometry{10.0, 10.0, 80.0, 40.0}, 0);
        const auto regionId = region.id();
        session.project.addRegionToPage(pageAt(session, 0), std::move(region));
        viewModel.openProjectSession(session);
        viewModel.selectRegion(regionId, ui::screen::editor::RegionSelectionMode::Replace);

        QVERIFY(ocrButton->isVisibleTo(commandBar));
        QVERIFY(ocrButton->isEnabled());

        viewModel.selectRegion(regionId, ui::screen::editor::RegionSelectionMode::Replace);
        viewModel.deleteRegions({regionId});
        QVERIFY(!ocrButton->isVisibleTo(commandBar));
    }

    void unavailableOcrDisablesActionWithGuidance() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        viewModel.setOcrAvailability(false, QStringLiteral("manifest.json not found"));
        ui::screen::editor::EditorPage page(&viewModel);

        auto* ocrButton = page.findChild<fluent::basicinput::Button*>(QStringLiteral("ocrButton"));
        QVERIFY(ocrButton != nullptr);
        QVERIFY(!ocrButton->isEnabled());
        QVERIFY(ocrButton->toolTip().contains(QStringLiteral("manifest.json")));
        QVERIFY(ocrButton->toolTip().contains(QStringLiteral("设置 > OCR")));
    }

    void commandbarHidesSeparatorsForEmptyGroups() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::widget::EditorCommandBar commandBar(&viewModel);
        commandBar.resize(1568, 40);
        commandBar.show();
        QCoreApplication::processEvents();

        const auto separators =
            commandBar.findChildren<QFrame*>(QRegularExpression(QStringLiteral("^editorCommandBarSeparator")));
        QCOMPARE(separators.size(), 7);
        QCOMPARE(std::count_if(separators.cbegin(),
                               separators.cend(),
                               [](const auto* separator) { return separator->isVisible(); }),
                 5);
    }

    void appliedStrokeAdoptsDraftImmediately() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        application::RecognizeRegionUseCase* ocrUseCase = nullptr;
        application::TranslatePageUseCase* translateUseCase = nullptr;
        application::ClearRegionUseCase* clearUseCase = nullptr;
        application::LayoutRegionUseCase* layoutUseCase = nullptr;
        application::SmartTranslatePageWorkflow* smartWorkflow = nullptr;
        application::PrepareProcessingAdoptionUseCase* prepareAdoptionUseCase = nullptr;
        ui::screen::editor::EditorProcessingController controller(&viewModel,
                                                                  ocrUseCase,
                                                                  translateUseCase,
                                                                  clearUseCase,
                                                                  layoutUseCase,
                                                                  smartWorkflow,
                                                                  prepareAdoptionUseCase,
                                                                  &registry);
        ui::screen::editor::EditorProcessingState processing;
        processing.isRunning = true;
        viewModel.updateProcessingState(processing); // wires the controller connections via render

        auto* canvas = page.canvasForTest();
        canvas->setPageBounds(domain::ImageSize{64, 64});
        QImage overrideImage(64, 64, QImage::Format_RGBA8888);
        overrideImage.fill(QColor(0, 0, 255, 200));
        canvas->setManualOverrideImage(overrideImage, QStringLiteral("hash-a"));
        canvas->beginStrokeDraft(manga::rendering::StrokePaintMode::Paint, QColor(255, 0, 0, 255), 8.0);
        canvas->extendStrokeDraft(QPointF(16.5, 16.5));
        QVERIFY(canvas->hasStrokeDraftForTest());

        const auto pageId = domain::PageId::generate();
        const auto opId = application::OperationId::generate();
        page.trackPendingStrokeForTest(pageId, opId, 0);

        // A delivery that predates the applied stroke must not settle the draft.
        const ui::screen::editor::ManualOverrideDelivery staleDelivery{pageId, overrideImage, QStringLiteral("hash-a")};
        QMetaObject::invokeMethod(
            &viewModel, "manualOverrideReady", Q_ARG(ui::screen::editor::ManualOverrideDelivery, staleDelivery));
        QVERIFY(canvas->hasStrokeDraftForTest());

        // The apply event adopts the draft immediately; no delivery is required.
        QMetaObject::invokeMethod(&controller,
                                  "manualStrokeApplied",
                                  Q_ARG(domain::PageId, pageId),
                                  Q_ARG(QString, QStringLiteral("hash-b")),
                                  Q_ARG(application::OperationId, opId));
        QVERIFY(!canvas->hasStrokeDraftForTest());
        QCOMPARE(canvas->authoritativeOverrideForTest().pixelColor(16, 16), QColor(255, 0, 0, 255));

        // The rebuild's later delivery carries the same ref and is skipped as identical, so a
        // mismatched image with the adopted ref must not replace the adopted pixels.
        QImage wrongImage(64, 64, QImage::Format_RGBA8888);
        wrongImage.fill(QColor(10, 200, 40, 255));
        const ui::screen::editor::ManualOverrideDelivery appliedDelivery{pageId, wrongImage, QStringLiteral("hash-b")};
        QMetaObject::invokeMethod(
            &viewModel, "manualOverrideReady", Q_ARG(ui::screen::editor::ManualOverrideDelivery, appliedDelivery));
        QCOMPARE(canvas->authoritativeOverrideForTest().pixelColor(16, 16), QColor(255, 0, 0, 255));
    }

    void noOpStrokeStillSettlesDraft() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        application::RecognizeRegionUseCase* ocrUseCase = nullptr;
        application::TranslatePageUseCase* translateUseCase = nullptr;
        application::ClearRegionUseCase* clearUseCase = nullptr;
        application::LayoutRegionUseCase* layoutUseCase = nullptr;
        application::SmartTranslatePageWorkflow* smartWorkflow = nullptr;
        application::PrepareProcessingAdoptionUseCase* prepareAdoptionUseCase = nullptr;
        ui::screen::editor::EditorProcessingController controller(&viewModel,
                                                                  ocrUseCase,
                                                                  translateUseCase,
                                                                  clearUseCase,
                                                                  layoutUseCase,
                                                                  smartWorkflow,
                                                                  prepareAdoptionUseCase,
                                                                  &registry);
        ui::screen::editor::EditorProcessingState processing;
        processing.isRunning = true;
        viewModel.updateProcessingState(processing);

        auto* canvas = page.canvasForTest();
        canvas->setPageBounds(domain::ImageSize{64, 64});
        QImage overrideImage(64, 64, QImage::Format_RGBA8888);
        overrideImage.fill(QColor(0, 0, 255, 200));
        canvas->setManualOverrideImage(overrideImage, QStringLiteral("hash-a"));
        canvas->beginStrokeDraft(manga::rendering::StrokePaintMode::Paint, QColor(255, 0, 0, 255), 8.0);
        canvas->extendStrokeDraft(QPointF(16.5, 16.5));

        const auto pageId = domain::PageId::generate();
        const auto opId = application::OperationId::generate();
        page.trackPendingStrokeForTest(pageId, opId, 0);

        // The stroke changed nothing, so afterRef equals the starting ref; the draft must still
        // settle on the apply event alone, which is the case a ref-diff rule would hang on.
        QMetaObject::invokeMethod(&controller,
                                  "manualStrokeApplied",
                                  Q_ARG(domain::PageId, pageId),
                                  Q_ARG(QString, QStringLiteral("hash-a")),
                                  Q_ARG(application::OperationId, opId));
        QVERIFY(!canvas->hasStrokeDraftForTest());
    }

    void failedStrokeSettlesDraftAndKeepsAuthoritativeLayer() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        application::RecognizeRegionUseCase* ocrUseCase = nullptr;
        application::TranslatePageUseCase* translateUseCase = nullptr;
        application::ClearRegionUseCase* clearUseCase = nullptr;
        application::LayoutRegionUseCase* layoutUseCase = nullptr;
        application::SmartTranslatePageWorkflow* smartWorkflow = nullptr;
        application::PrepareProcessingAdoptionUseCase* prepareAdoptionUseCase = nullptr;
        ui::screen::editor::EditorProcessingController controller(&viewModel,
                                                                  ocrUseCase,
                                                                  translateUseCase,
                                                                  clearUseCase,
                                                                  layoutUseCase,
                                                                  smartWorkflow,
                                                                  prepareAdoptionUseCase,
                                                                  &registry);
        ui::screen::editor::EditorProcessingState processing;
        processing.isRunning = true;
        viewModel.updateProcessingState(processing);

        auto* canvas = page.canvasForTest();
        canvas->setPageBounds(domain::ImageSize{64, 64});
        QImage overrideImage(64, 64, QImage::Format_RGBA8888);
        overrideImage.fill(QColor(0, 0, 255, 200));
        canvas->setManualOverrideImage(overrideImage, QStringLiteral("hash-a"));
        canvas->beginStrokeDraft(manga::rendering::StrokePaintMode::Erase, QColor(0, 0, 0, 255), 8.0);
        canvas->extendStrokeDraft(QPointF(16.5, 16.5));

        const auto pageId = domain::PageId::generate();
        const auto opId = application::OperationId::generate();
        page.trackPendingStrokeForTest(pageId, opId, 0);

        QMetaObject::invokeMethod(&controller, "processingFinished", Q_ARG(domain::PageId, pageId), Q_ARG(bool, false));

        QVERIFY(!canvas->hasStrokeDraftForTest());
        QCOMPARE(canvas->authoritativeOverrideForTest(), overrideImage);
    }

    void staleArrivalsDoNotSettleDraft() {
        test::FakeThumbnailStore thumbnails;
        test::FakePageImageLoader images;
        application::OperationRegistry registry;
        ui::screen::editor::EditorViewModel viewModel(&thumbnails, &images, &registry);
        ui::screen::editor::EditorPage page(&viewModel);
        application::RecognizeRegionUseCase* ocrUseCase = nullptr;
        application::TranslatePageUseCase* translateUseCase = nullptr;
        application::ClearRegionUseCase* clearUseCase = nullptr;
        application::LayoutRegionUseCase* layoutUseCase = nullptr;
        application::SmartTranslatePageWorkflow* smartWorkflow = nullptr;
        application::PrepareProcessingAdoptionUseCase* prepareAdoptionUseCase = nullptr;
        ui::screen::editor::EditorProcessingController controller(&viewModel,
                                                                  ocrUseCase,
                                                                  translateUseCase,
                                                                  clearUseCase,
                                                                  layoutUseCase,
                                                                  smartWorkflow,
                                                                  prepareAdoptionUseCase,
                                                                  &registry);
        ui::screen::editor::EditorProcessingState processing;
        processing.isRunning = true;
        viewModel.updateProcessingState(processing);

        auto* canvas = page.canvasForTest();
        canvas->setPageBounds(domain::ImageSize{64, 64});
        QImage overrideImage(64, 64, QImage::Format_RGBA8888);
        overrideImage.fill(QColor(0, 0, 255, 200));
        canvas->setManualOverrideImage(overrideImage, QStringLiteral("hash-a"));
        canvas->beginStrokeDraft(manga::rendering::StrokePaintMode::Paint, QColor(255, 0, 0, 255), 8.0);
        canvas->extendStrokeDraft(QPointF(16.5, 16.5));

        const auto pageId = domain::PageId::generate();
        const auto opId = application::OperationId::generate();
        page.trackPendingStrokeForTest(pageId, opId, 0);

        // An apply event for a different operation is not ours.
        QMetaObject::invokeMethod(&controller,
                                  "manualStrokeApplied",
                                  Q_ARG(domain::PageId, pageId),
                                  Q_ARG(QString, QStringLiteral("hash-b")),
                                  Q_ARG(application::OperationId, application::OperationId::generate()));
        // A delivery for another page is not ours either.
        const ui::screen::editor::ManualOverrideDelivery otherPageDelivery{
            domain::PageId::generate(), overrideImage, QStringLiteral("hash-b")};
        QMetaObject::invokeMethod(
            &viewModel, "manualOverrideReady", Q_ARG(ui::screen::editor::ManualOverrideDelivery, otherPageDelivery));
        QVERIFY(canvas->hasStrokeDraftForTest());
    }
};

QTEST_MAIN(EditorPageTest)
#include "EditorPageTest.moc"
