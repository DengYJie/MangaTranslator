#include "FakeDerivedAssetStore.h"
#include "FakeInpainter.h"
#include "FakeOcrEngine.h"
#include "FakeTextLayouter.h"
#include "FakeTranslator.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "fakes/FakeProjectPorts.h"
#include "ui/screen/editor/EditorPage.h"
#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using namespace manga::domain;
using namespace manga::application;
using namespace manga::ui::screen::editor;

namespace {

OpenedProjectSession createSessionWithRegions(int pageCount = 2, int regionsPerPage = 2) {
    OpenedProjectSession session;
    session.project = ComicProject("Processing Test Project");
    session.projectRootPath = QStringLiteral("D:/manga/test");

    for (int p = 0; p < pageCount; ++p) {
        PageId pageId = PageId::generate();
        ComicPage page(pageId, QStringLiteral("assets/page_%1.png").arg(p).toStdString(), ImageSize{1000, 1500}, p);

        for (int r = 0; r < regionsPerPage; ++r) {
            RegionId rId = RegionId::generate();
            TextRegion region(rId, RectGeometry{10.0 + r * 100.0, 10.0 + r * 100.0, 80.0, 80.0});
            region.setOrderIndex(r);
            page.addRegion(region);
        }
        session.project.addPage(std::move(page));
    }
    return session;
}

} // namespace

class EditorProcessingControllerTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<manga::test::FakeThumbnailStore> m_thumbnails;
    std::unique_ptr<manga::test::FakePageImageLoader> m_images;
    std::unique_ptr<OperationRegistry> m_registry;
    std::unique_ptr<manga::test::FakeProjectRepository> m_repo;
    std::unique_ptr<manga::application::test::FakeOcrEngine> m_ocr;
    std::unique_ptr<manga::application::test::FakeTranslator> m_tr;
    std::unique_ptr<manga::application::test::FakeInpainter> m_inpaint;
    std::unique_ptr<manga::application::test::FakeTextLayouter> m_layout;
    std::unique_ptr<manga::application::test::FakeDerivedAssetStore> m_assetStore;
    std::unique_ptr<EditorViewModel> m_viewModel;
    std::unique_ptr<EditorProcessingController> m_controller;

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<domain::PageId>();
        qRegisterMetaType<domain::RegionId>();
        qRegisterMetaType<application::OperationId>();
        qRegisterMetaType<ui::screen::editor::EditorState>();
    }

    void init() {
        m_thumbnails = std::make_unique<manga::test::FakeThumbnailStore>();
        m_images = std::make_unique<manga::test::FakePageImageLoader>();
        m_registry = std::make_unique<OperationRegistry>();
        m_repo = std::make_unique<manga::test::FakeProjectRepository>();
        m_ocr = std::make_unique<manga::application::test::FakeOcrEngine>();
        m_ocr->nextResponse.text = QStringLiteral("Sample Japanese text");
        m_tr = std::make_unique<manga::application::test::FakeTranslator>();
        m_inpaint = std::make_unique<manga::application::test::FakeInpainter>();
        m_layout = std::make_unique<manga::application::test::FakeTextLayouter>();
        m_assetStore = std::make_unique<manga::application::test::FakeDerivedAssetStore>();

        m_viewModel =
            std::make_unique<EditorViewModel>(m_thumbnails.get(), m_images.get(), m_registry.get(), m_repo.get());
        m_controller = std::make_unique<EditorProcessingController>(m_viewModel.get(),
                                                                    m_ocr.get(),
                                                                    m_tr.get(),
                                                                    m_inpaint.get(),
                                                                    m_layout.get(),
                                                                    m_assetStore.get(),
                                                                    m_registry.get());
    }

    void cleanup() {
        if (m_controller) {
            m_controller->cancelAll();
        }
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();

        m_controller.reset();
        m_viewModel.reset();
        m_registry.reset();
        m_thumbnails.reset();
        m_images.reset();
        m_repo.reset();
        m_ocr.reset();
        m_tr.reset();
        m_inpaint.reset();
        m_layout.reset();
        m_assetStore.reset();

        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
    }

    void testEmptyRegionsCanvasNotice() {
        // Open session with a page that has 0 regions
        auto session = createSessionWithRegions(1, 0);
        m_viewModel->openProjectSession(session);

        const auto pageId = m_viewModel->state().currentPageId;
        QVERIFY(pageId.isValid());
        QCOMPARE(m_viewModel->state().regions.size(), 0);

        // Trigger Smart Translate
        m_controller->smartTranslatePage(pageId);

        // State should reflect notice / safe message about no regions
        const auto& state = m_viewModel->state();
        QVERIFY(!state.processing.isRunning);
        QVERIFY(state.processing.safeMessage.contains(QStringLiteral("区域")) ||
                state.processing.safeMessage.contains(QStringLiteral("region")) ||
                (state.notice.has_value() && state.notice->safeMessage.contains(QStringLiteral("区域"))));
    }

    void testSameTargetCancelsOldOperation() {
        m_tr->deferred = true; // Make translation stay in-flight

        auto session = createSessionWithRegions(1, 2);
        m_viewModel->openProjectSession(session);
        const auto pageId = m_viewModel->state().currentPageId;

        // Start first operation
        m_controller->smartTranslatePage(pageId);
        QVERIFY(m_controller->hasActiveOperation(pageId));
        const auto firstOpId = m_viewModel->state().processing.activeOperationId;
        QVERIFY(firstOpId.isValid());

        // Wait until translation step begins
        while (m_tr->callCount == 0 && m_controller->hasActiveOperation(pageId)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        // Start second operation on the same target
        m_controller->smartTranslatePage(pageId);
        const auto secondOpId = m_viewModel->state().processing.activeOperationId;
        QVERIFY(secondOpId.isValid());
        QVERIFY(secondOpId != firstOpId);

        // First operation should be cancelling or cancelled in registry
        const auto snapshots = m_registry->snapshot();
        auto it = std::find_if(snapshots.begin(), snapshots.end(), [&](const auto& s) { return s.id == firstOpId; });
        if (it != snapshots.end()) {
            QVERIFY(it->state == OperationState::Cancelling || it->state == OperationState::Cancelled);
        }

        // Clean up pending operation
        m_controller->cancelAll();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }

    void testPageSwitchKeepsTaskRunning() {
        m_tr->deferred = true;

        auto session = createSessionWithRegions(2, 2);
        m_viewModel->openProjectSession(session);
        const auto page1Id = m_viewModel->state().pages.at(0).id;
        const auto page2Id = m_viewModel->state().pages.at(1).id;

        // Start operation on Page 1
        m_viewModel->selectPage(page1Id);
        m_controller->smartTranslatePage(page1Id);
        QVERIFY(m_controller->hasActiveOperation(page1Id));

        // Pump events until translation starts
        while (m_tr->callCount == 0 && m_controller->hasActiveOperation(page1Id)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        // Switch to Page 2
        m_viewModel->selectPage(page2Id);
        QCOMPARE(m_viewModel->state().currentPageId, page2Id);
        // Operation on Page 1 is still running in background
        QVERIFY(m_controller->hasActiveOperation(page1Id));

        // Setup translation response for completion
        for (const auto& r : session.project.pages().at(0).regions()) {
            TranslationResponseItem item;
            item.regionId = r.id();
            item.translation = QStringLiteral("Translated Text");
            m_tr->nextResponse.items.append(item);
        }

        // Now complete the translation
        m_tr->complete();
        while (m_controller->hasActiveOperation(page1Id)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        // Controller should now have a proposal for Page 1
        QVERIFY(m_controller->hasProposal(page1Id));

        // Switch back to Page 1
        m_viewModel->selectPage(page1Id);
        QVERIFY(m_viewModel->state().processing.hasProposal);
        QCOMPARE(m_viewModel->state().processing.needsReviewRegionIds.size(), 2);
    }

    void testCloseProjectCancelsAllTasksAndClearsProposals() {
        m_tr->deferred = true;

        auto session = createSessionWithRegions(2, 2);
        m_viewModel->openProjectSession(session);
        const auto page1Id = m_viewModel->state().pages.at(0).id;

        m_controller->smartTranslatePage(page1Id);
        const auto opId = m_viewModel->state().processing.activeOperationId;
        QVERIFY(opId.isValid());

        // Wait until translation is in-flight
        while (m_tr->callCount == 0 && m_controller->hasActiveOperation(page1Id)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        // Close project session
        m_viewModel->closeProjectSession();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

        // All tasks should be cancelled and proposals cleared
        QVERIFY(!m_controller->hasActiveOperation(page1Id));
        QVERIFY(!m_controller->hasProposal(page1Id));
        const auto snapshots = m_registry->snapshot();
        auto it = std::find_if(snapshots.begin(), snapshots.end(), [&](const auto& s) { return s.id == opId; });
        if (it != snapshots.end()) {
            QVERIFY(it->state == OperationState::Cancelling || it->state == OperationState::Cancelled);
        }
    }

    void testConflictShowsReviewNotice() {
        auto session = createSessionWithRegions(1, 2);
        // Set manual translation on region 0 to cause a conflict / review note
        const auto pageId = session.project.pages().at(0).id();
        const auto regionId = session.project.pages().at(0).regions().at(0).id();

        TranslationEntry trans;
        trans.setManualText("User manual text");
        PageRegionEdit edit;
        edit.regionId = regionId;
        edit.translation = trans;
        const PageRegionEdit edits[] = {edit};
        auto applied = session.project.applyPageRegionEdits(pageId, edits);
        QVERIFY(applied.has_value());

        m_viewModel->openProjectSession(session);
        m_viewModel->selectPage(pageId);

        // Run smart translate
        m_controller->smartTranslatePage(pageId);
        while (m_controller->hasActiveOperation(pageId)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        // Conflict should be detected: safeMessage or issue summary displays notice
        const auto& state = m_viewModel->state();
        QVERIFY(state.processing.hasProposal);
        QVERIFY(state.processing.conflictRegionIds.contains(regionId) ||
                state.processing.safeMessage.contains(QStringLiteral("有新的 AI 译文可比较")) ||
                state.processing.safeMessage.contains(QStringLiteral("AI")));
    }

    void testAdoptClearsProposalAndPushesUndo() {
        m_ocr->nextResponse.text = QStringLiteral("Recognized OCR Text");

        auto session = createSessionWithRegions(1, 1);
        const auto targetRegionId = session.project.pages().at(0).regions().at(0).id();
        TranslationResponseItem trItem;
        trItem.regionId = targetRegionId;
        trItem.translation = QStringLiteral("Translated Text");
        m_tr->nextResponse.items.append(trItem);

        m_viewModel->openProjectSession(session);
        const auto pageId = m_viewModel->state().currentPageId;

        m_controller->smartTranslatePage(pageId);
        while (m_controller->hasActiveOperation(pageId)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        QVERIFY(m_controller->hasProposal(pageId));
        QVERIFY(m_viewModel->state().processing.hasProposal);
        const int initialUndoCount = m_viewModel->undoStackCountForTest();

        // Adopt proposal
        m_controller->adoptProposal(pageId);

        // Proposal cleared in controller & state
        QVERIFY(!m_controller->hasProposal(pageId));
        QVERIFY(!m_viewModel->state().processing.hasProposal);
        QCOMPARE(m_viewModel->undoStackCountForTest(), initialUndoCount + 1);

        // Can undo
        QVERIFY(m_viewModel->state().canUndo);
        m_viewModel->undo();
        QCOMPARE(m_viewModel->undoStackCountForTest(), initialUndoCount + 1);
    }

    void testRejectClearsProposalWithoutUndo() {
        m_ocr->nextResponse.text = QStringLiteral("Recognized OCR Text");

        auto session = createSessionWithRegions(1, 1);
        const auto targetRegionId = session.project.pages().at(0).regions().at(0).id();
        TranslationResponseItem trItem;
        trItem.regionId = targetRegionId;
        trItem.translation = QStringLiteral("Translated Text");
        m_tr->nextResponse.items.append(trItem);

        m_viewModel->openProjectSession(session);
        const auto pageId = m_viewModel->state().currentPageId;

        m_controller->smartTranslatePage(pageId);
        while (m_controller->hasActiveOperation(pageId)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        QVERIFY(m_controller->hasProposal(pageId));
        const int initialUndoCount = m_viewModel->undoStackCountForTest();

        // Reject proposal
        m_controller->rejectProposal(pageId);

        QVERIFY(!m_controller->hasProposal(pageId));
        QVERIFY(!m_viewModel->state().processing.hasProposal);
        QCOMPARE(m_viewModel->undoStackCountForTest(), initialUndoCount);
    }

    void testSingleStageCommands() {
        m_ocr->nextResponse.text = QStringLiteral("Recognized OCR Text");

        auto session = createSessionWithRegions(1, 1);
        m_viewModel->openProjectSession(session);
        const auto pageId = m_viewModel->state().currentPageId;
        const auto regionId = m_viewModel->state().regions.at(0).id;

        // Select region
        m_viewModel->selectRegion(regionId, RegionSelectionMode::Replace);

        // OCR selected region
        m_controller->recognizeSelectedRegions();
        while (m_controller->hasActiveOperation(pageId)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        QVERIFY(m_controller->hasProposal(pageId));
        m_controller->adoptProposal(pageId);
        QVERIFY(!m_controller->hasProposal(pageId));
    }

    void testClearRegionAsyncAdoption() {
        auto session = createSessionWithRegions(1, 1);
        m_viewModel->openProjectSession(session);
        const auto pageId = m_viewModel->state().currentPageId;
        const auto regionId = m_viewModel->state().regions.at(0).id;

        m_controller->clearRegion(pageId, regionId);
        while (m_controller->hasActiveOperation(pageId)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        // Drains adoption preparation future
        for (int i = 0; i < 20; ++i) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        QVERIFY(m_controller->hasProposal(pageId));
        const int initialUndo = m_viewModel->undoStackCountForTest();
        m_controller->adoptProposal(pageId);
        QVERIFY(!m_controller->hasProposal(pageId));
        // Verify undo command was created
        QCOMPARE(m_viewModel->undoStackCountForTest(), initialUndo + 1);
    }

    void testSingleStageFailureHandling() {
        m_layouter->failNext = true;

        auto session = createSessionWithRegions(1, 1);
        m_viewModel->openProjectSession(session);
        const auto pageId = m_viewModel->state().currentPageId;
        const auto regionId = m_viewModel->state().regions.at(0).id;

        m_controller->layoutRegion(pageId, regionId);
        while (m_controller->hasActiveOperation(pageId)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        }

        QVERIFY(!m_controller->hasProposal(pageId));
        QCOMPARE(m_viewModel->state().processing.status, ProcessingStatus::Failed);
        QVERIFY(m_viewModel->state().processing.failedRegionIds.contains(regionId));
    }
};

QTEST_MAIN(EditorProcessingControllerTest)
#include "editor_processing_controller_test.moc"
