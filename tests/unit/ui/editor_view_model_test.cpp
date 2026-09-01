#include "application/usecase/region/RegionUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "fakes/FakeImagePorts.h"
#include "ui/base/DomainMetaTypes.h"
#include "ui/manga/RegionInteraction.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using manga::ui::canvas::EditKind;
using manga::ui::screen::editor::CanvasTool;
using manga::ui::screen::editor::DocumentSaveState;
using manga::ui::screen::editor::EditorViewModel;
using manga::ui::screen::editor::InspectorMode;
using manga::ui::screen::editor::PageImageDelivery;
using manga::ui::screen::editor::PreviewMode;
using manga::ui::screen::editor::RegionProjection;
using manga::ui::screen::editor::RegionSelectionMode;

namespace {

/// Creates a session with `pageCount` pages, each page having `regionsPerPage` regions.
application::OpenedProjectSession makeSession(int pageCount, int regionsPerPage = 0) {
    application::OpenedProjectSession session;
    session.project = domain::ComicProject("Demo Project");
    session.projectRootPath = QStringLiteral("D:/manga/demo");
    for (int i = 0; i < pageCount; ++i) {
        auto pageId = domain::PageId::generate();
        domain::ComicPage page(pageId,
                               QStringLiteral("assets/original/page_%1.png").arg(1000 + i).toStdString(),
                               domain::ImageSize{1200, 1800},
                               i);
        for (int r = 0; r < regionsPerPage; ++r) {
            domain::RectGeometry geom;
            geom.x = 10.0 + r * 100.0;
            geom.y = 10.0 + r * 50.0;
            geom.width = 80.0;
            geom.height = 40.0;
            domain::TextRegion region(domain::RegionId::generate(), geom, r);
            page.addRegion(std::move(region));
        }
        session.project.addPage(std::move(page));
    }
    return session;
}

domain::PageId pageAt(const application::OpenedProjectSession& session, int index) {
    return session.project.pages().at(index).id();
}

domain::RegionId regionAt(const application::OpenedProjectSession& session, int pageIndex, int regionIndex) {
    return session.project.pages().at(pageIndex).regions().at(regionIndex).id();
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

    void thumbnailCompletionSurvivesPageSwitch() {
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
        QCOMPARE(invalidatedSpy.size(), 0);
        f.imageLoader.completeAll();
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        QTRY_COMPARE_WITH_TIMEOUT(readySpy.size(), 2, 2000);
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

    // ---- Region projection tests ----

    void openPageProjectsRegionsSortedByOrderWithGeometry() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 3);
        const auto r0 = regionAt(session, 0, 0);
        const auto r1 = regionAt(session, 0, 1);
        const auto r2 = regionAt(session, 0, 2);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        const auto& state = viewModel.state();
        QCOMPARE(state.regions.size(), 3);
        // Sorted by orderIndex.
        QCOMPARE(state.regions[0].orderIndex, 0);
        QCOMPARE(state.regions[1].orderIndex, 1);
        QCOMPARE(state.regions[2].orderIndex, 2);
        // IDs match.
        QCOMPARE(state.regions[0].id, r0);
        QCOMPARE(state.regions[1].id, r1);
        QCOMPARE(state.regions[2].id, r2);
        // Geometry projected correctly.
        QCOMPARE(state.regions[0].geometry.x, 10.0);
        QCOMPARE(state.regions[1].geometry.x, 110.0);
        QCOMPARE(state.regions[2].geometry.x, 210.0);
    }

    void selectRegionReplaceSetsPrimaryAndSelection() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 3);
        const auto r0 = regionAt(session, 0, 0);
        const auto r1 = regionAt(session, 0, 1);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);
        QCOMPARE(viewModel.state().selectedRegionIds.first(), r0);
        QCOMPARE(viewModel.state().primaryRegionId, r0);
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Single);

        viewModel.selectRegion(r1, RegionSelectionMode::Replace);
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);
        QCOMPARE(viewModel.state().selectedRegionIds.first(), r1);
        QCOMPARE(viewModel.state().primaryRegionId, r1);
    }

    void selectRegionToggleAddsAndRemoves() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 3);
        const auto r0 = regionAt(session, 0, 0);
        const auto r1 = regionAt(session, 0, 1);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        viewModel.selectRegion(r1, RegionSelectionMode::Toggle);
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 2);
        QCOMPARE(viewModel.state().primaryRegionId, r1);
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Multi);

        // Toggle off r0.
        viewModel.selectRegion(r0, RegionSelectionMode::Toggle);
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);
        QCOMPARE(viewModel.state().selectedRegionIds.first(), r1);
        QCOMPARE(viewModel.state().primaryRegionId, r1);

        // Toggle off the primary.
        viewModel.selectRegion(r1, RegionSelectionMode::Toggle);
        QVERIFY(viewModel.state().selectedRegionIds.isEmpty());
        QVERIFY(!viewModel.state().primaryRegionId.isValid());
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Page);
    }

    void selectRegionRangeSelectsContiguousSpan() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 4);
        const auto r0 = regionAt(session, 0, 0);
        const auto r1 = regionAt(session, 0, 1);
        const auto r2 = regionAt(session, 0, 2);
        const auto r3 = regionAt(session, 0, 3);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        viewModel.selectRegion(r3, RegionSelectionMode::Range);
        // Should include r0, r1, r2, r3 (indices 0..3).
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 4);
        // Primary stays as r0.
        QCOMPARE(viewModel.state().primaryRegionId, r0);
        // Sorted by order.
        QCOMPARE(viewModel.state().selectedRegionIds[0], r0);
        QCOMPARE(viewModel.state().selectedRegionIds[1], r1);
        QCOMPARE(viewModel.state().selectedRegionIds[2], r2);
        QCOMPARE(viewModel.state().selectedRegionIds[3], r3);
    }

    void selectRegionRangeSamePrimaryAndTargetSelectsExactlyOne() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 4);
        const auto r1 = regionAt(session, 0, 1);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        // Establish primary at r1.
        viewModel.selectRegion(r1, RegionSelectionMode::Replace);
        // Range from primary to itself selects exactly that one region.
        viewModel.selectRegion(r1, RegionSelectionMode::Range);
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);
        QCOMPARE(viewModel.state().selectedRegionIds.first(), r1);
        QCOMPARE(viewModel.state().primaryRegionId, r1);
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Single);
    }

    void selectRegionRangeReverseDirectionSelectsSameSpan() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 4);
        const auto r0 = regionAt(session, 0, 0);
        const auto r1 = regionAt(session, 0, 1);
        const auto r2 = regionAt(session, 0, 2);
        const auto r3 = regionAt(session, 0, 3);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        // Primary at r3 (higher order), range to r0 (lower order).
        viewModel.selectRegion(r3, RegionSelectionMode::Replace);
        viewModel.selectRegion(r0, RegionSelectionMode::Range);
        // Same contiguous span as forward direction: r0 through r3.
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 4);
        QCOMPARE(viewModel.state().primaryRegionId, r3);
        QCOMPARE(viewModel.state().selectedRegionIds[0], r0);
        QCOMPARE(viewModel.state().selectedRegionIds[1], r1);
        QCOMPARE(viewModel.state().selectedRegionIds[2], r2);
        QCOMPARE(viewModel.state().selectedRegionIds[3], r3);
    }

    void selectRegionRangeWithNoPriorPrimaryFallsBackToReplace() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 4);
        const auto r2 = regionAt(session, 0, 2);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        // No prior selection, range select on r2.
        viewModel.selectRegion(r2, RegionSelectionMode::Range);
        // Falls back to Replace: only the target is selected and becomes primary.
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);
        QCOMPARE(viewModel.state().selectedRegionIds.first(), r2);
        QCOMPARE(viewModel.state().primaryRegionId, r2);
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Single);
    }

    void selectRegionUnknownIdIsIgnored() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 2);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        const auto unknownId = domain::RegionId::generate();
        viewModel.selectRegion(unknownId, RegionSelectionMode::Replace);
        QVERIFY(viewModel.state().selectedRegionIds.isEmpty());
        QVERIFY(!viewModel.state().primaryRegionId.isValid());
    }

    void clearRegionSelectionEmptiesSelection() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 2);
        const auto r0 = regionAt(session, 0, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        QVERIFY(!viewModel.state().selectedRegionIds.isEmpty());
        viewModel.clearRegionSelection();
        QVERIFY(viewModel.state().selectedRegionIds.isEmpty());
        QVERIFY(!viewModel.state().primaryRegionId.isValid());
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Page);
    }

    void selectAllRegionsOnCurrentPage() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 3);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.selectAllRegionsOnCurrentPage();
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 3);
        QCOMPARE(viewModel.state().primaryRegionId, viewModel.state().regions.first().id);
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Multi);
    }

    void pageSwitchClearsSelection() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(2, 2);
        const auto r0 = regionAt(session, 0, 0);
        const auto secondPageId = pageAt(session, 1);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        QVERIFY(!viewModel.state().selectedRegionIds.isEmpty());

        viewModel.selectPage(secondPageId);
        QVERIFY(viewModel.state().selectedRegionIds.isEmpty());
        QVERIFY(!viewModel.state().primaryRegionId.isValid());
        QCOMPARE(viewModel.state().inspectorMode, InspectorMode::Page);
    }

    void removeRegionForTestDropsStaleSelectedIds() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 3);
        const auto r0 = regionAt(session, 0, 0);
        const auto r1 = regionAt(session, 0, 1);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        viewModel.selectRegion(r1, RegionSelectionMode::Toggle);
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 2);

        // Remove r0 via test hook.
        QVERIFY(viewModel.removeRegionForTest(r0));
        // r0 should be dropped from selection.
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);
        QCOMPARE(viewModel.state().selectedRegionIds.first(), r1);
        QCOMPARE(viewModel.state().primaryRegionId, r1);
        // Regions count updated.
        QCOMPARE(viewModel.state().regions.size(), 2);
    }

    void equalStateSuppressionNoSignalForIdempotentIntents() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 2);
        const auto r0 = regionAt(session, 0, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        QSignalSpy spy(&viewModel, &EditorViewModel::stateChanged);
        const int countBefore = spy.size();

        // Select same region twice in Replace mode.
        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        const int countAfterFirst = spy.size();
        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        // Second call produces no new signal because state is equal.
        QCOMPARE(spy.size(), countAfterFirst);

        // setPreviewMode with same value.
        viewModel.setPreviewMode(PreviewMode::Original);
        QCOMPARE(spy.size(), countAfterFirst);

        // setActiveTool with same value.
        viewModel.setActiveTool(CanvasTool::Select);
        QCOMPARE(spy.size(), countAfterFirst);
    }

    void isDirtyFalseOnFreshOpenAndTrueAfterMutation() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 2);
        const auto r0 = regionAt(session, 0, 0);
        // Mark clean so the initial projection shows isDirty=false.
        session.project.markClean();
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        QVERIFY(!viewModel.state().saveState.isDirty);

        // Mutate via test hook.
        QVERIFY(viewModel.removeRegionForTest(r0));
        QVERIFY(viewModel.state().saveState.isDirty);
    }

    void saveStateDefaultsAreCorrect() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1);
        session.project.markClean();
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        const auto& ss = viewModel.state().saveState;
        QVERIFY(!ss.isDirty);
        QVERIFY(!ss.isSaving);
        QCOMPARE(ss.undoText, QStringLiteral("Undo"));
        QCOMPARE(ss.redoText, QStringLiteral("Redo"));
        QVERIFY(!ss.saveError.has_value());
        QVERIFY(!viewModel.state().canUndo);
        QVERIFY(!viewModel.state().canRedo);
    }

    void toolSwitchDoesNotClearSelection() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 2);
        const auto r0 = regionAt(session, 0, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.selectRegion(r0, RegionSelectionMode::Replace);
        QVERIFY(!viewModel.state().selectedRegionIds.isEmpty());

        viewModel.setActiveTool(CanvasTool::Pan);
        QCOMPARE(viewModel.state().activeTool, CanvasTool::Pan);
        // Selection preserved across tool switch.
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);
        QCOMPARE(viewModel.state().selectedRegionIds.first(), r0);
    }

    void previewModeSwitchUpdatesState() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.openProjectSession(makeSession(1));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        QCOMPARE(viewModel.state().previewMode, PreviewMode::Original);
        viewModel.setPreviewMode(PreviewMode::Translated);
        QCOMPARE(viewModel.state().previewMode, PreviewMode::Translated);
        viewModel.setPreviewMode(PreviewMode::Compare);
        QCOMPARE(viewModel.state().previewMode, PreviewMode::Compare);
    }

    // ---- Undo/redo command integration tests ----

    void createRegionPushesCommandAndFlipsCanUndo() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 0);
        session.project.markClean();
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        QVERIFY(!viewModel.state().canUndo);
        QVERIFY(!viewModel.state().canRedo);

        domain::RectGeometry geom{100.0, 100.0, 50.0, 50.0};
        viewModel.createRegion(geom);

        QVERIFY(viewModel.state().canUndo);
        QVERIFY(!viewModel.state().canRedo);
        QCOMPARE(viewModel.state().regions.size(), 1);
        QCOMPARE(viewModel.state().saveState.undoText, QStringLiteral("Create Region"));
        QVERIFY(viewModel.state().saveState.isDirty);
    }

    void undoCreateRemovesRegionAndFlipsCanRedo() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        domain::RectGeometry geom{100.0, 100.0, 50.0, 50.0};
        viewModel.createRegion(geom);
        QCOMPARE(viewModel.state().regions.size(), 1);

        viewModel.undo();
        QVERIFY(!viewModel.state().canUndo);
        QVERIFY(viewModel.state().canRedo);
        QCOMPARE(viewModel.state().regions.size(), 0);
        QCOMPARE(viewModel.state().saveState.redoText, QStringLiteral("Create Region"));

        viewModel.redo();
        QVERIFY(viewModel.state().canUndo);
        QVERIFY(!viewModel.state().canRedo);
        QCOMPARE(viewModel.state().regions.size(), 1);
    }

    void deleteRegionsPushesBatchCommand() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 3);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        QVector<domain::RegionId> ids;
        for (const auto& rp : viewModel.state().regions) {
            ids.append(rp.id);
        }
        viewModel.deleteRegions(ids);

        QVERIFY(viewModel.state().canUndo);
        QCOMPARE(viewModel.state().regions.size(), 0);
        QCOMPARE(viewModel.state().saveState.undoText, QStringLiteral("Delete Regions"));

        viewModel.undo();
        QCOMPARE(viewModel.state().regions.size(), 3);
    }

    void commitRegionGeometryPushesCommand() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 1);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        const auto r0 = viewModel.state().regions.first().id;
        const auto oldGeom = viewModel.state().regions.first().geometry;
        domain::RectGeometry newGeom{200.0, 200.0, 80.0, 40.0};

        viewModel.commitRegionGeometry(r0, newGeom, EditKind::Move);

        QVERIFY(viewModel.state().canUndo);
        QCOMPARE(viewModel.state().regions.first().geometry, newGeom);

        viewModel.undo();
        QCOMPARE(viewModel.state().regions.first().geometry, oldGeom);
    }

    void noOpGeometryDoesNotPushCommand() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 1);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        const auto r0 = viewModel.state().regions.first().id;
        const auto currentGeom = viewModel.state().regions.first().geometry;

        // Same geometry is a no-op.
        viewModel.commitRegionGeometry(r0, currentGeom, EditKind::Move);
        QVERIFY(!viewModel.state().canUndo);
    }

    void boundsValidationRejectsCreateRegion() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        // Exceeds page bounds (1200x1800).
        domain::RectGeometry bad{1100.0, 1700.0, 200.0, 200.0};
        viewModel.createRegion(bad);
        QVERIFY(!viewModel.state().canUndo);
        QCOMPARE(viewModel.state().regions.size(), 0);
    }

    void smallRegionRejectedByCreateRegion() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        // Below minimum dimension (8x8).
        domain::RectGeometry tiny{100.0, 100.0, 4.0, 4.0};
        viewModel.createRegion(tiny);
        QVERIFY(!viewModel.state().canUndo);
        QCOMPARE(viewModel.state().regions.size(), 0);
    }

    void crossPageUndoNavigatesToAffectedPage() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(2, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        const auto firstPage = viewModel.state().currentPageId;

        // Create region on first page.
        domain::RectGeometry geom{100.0, 100.0, 50.0, 50.0};
        viewModel.createRegion(geom);
        QCOMPARE(viewModel.state().regions.size(), 1);

        // Switch to second page.
        const auto secondPage = viewModel.state().pages[1].id;
        viewModel.selectPage(secondPage);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);
        QCOMPARE(viewModel.state().currentPageId, secondPage);
        QCOMPARE(viewModel.state().regions.size(), 0);

        // Undo should navigate back to first page.
        viewModel.undo();
        QCOMPARE(viewModel.state().currentPageId, firstPage);
        QCOMPARE(viewModel.state().regions.size(), 0);
    }

    void closeResetsStackState() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        domain::RectGeometry geom{100.0, 100.0, 50.0, 50.0};
        viewModel.createRegion(geom);
        QVERIFY(viewModel.state().canUndo);

        viewModel.closeProjectSession();
        QVERIFY(!viewModel.state().canUndo);
        QVERIFY(!viewModel.state().canRedo);
        QVERIFY(!viewModel.state().undoHistoryFaulted);
    }

    void equalStateSuppressionHoldsForUndoIntents() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 0);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        QSignalSpy spy(&viewModel, &EditorViewModel::stateChanged);
        const int countBefore = spy.size();

        // Undo with nothing to undo should not emit.
        viewModel.undo();
        QCOMPARE(spy.size(), countBefore);

        // Redo with nothing to redo should not emit.
        viewModel.redo();
        QCOMPARE(spy.size(), countBefore);
    }

    void faultedHistoryBlocksSubsequentUndoRedo() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(1, 1);
        viewModel.openProjectSession(std::move(session));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        const auto r0 = viewModel.state().regions.first().id;
        const auto oldGeom = viewModel.state().regions.first().geometry;
        domain::RectGeometry newGeom{200.0, 200.0, 80.0, 40.0};

        // Push a geometry command.
        viewModel.commitRegionGeometry(r0, newGeom, EditKind::Move);
        QVERIFY(viewModel.state().canUndo);
        QVERIFY(!viewModel.state().undoHistoryFaulted);

        // Remove the region via test hook so replay will fail.
        QVERIFY(viewModel.removeRegionForTest(r0));

        // Undo should detect the fault.
        viewModel.undo();
        QVERIFY(viewModel.state().undoHistoryFaulted);

        // Subsequent undo/redo are no-ops while faulted.
        const int regionCountAfterFault = viewModel.state().regions.size();
        viewModel.undo();
        QCOMPARE(viewModel.state().regions.size(), regionCountAfterFault);
        viewModel.redo();
        QCOMPARE(viewModel.state().regions.size(), regionCountAfterFault);
    }

    void createReplayFaultSetsHistoryFaulted() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        viewModel.openProjectSession(makeSession(1, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        viewModel.createRegion(domain::RectGeometry{100.0, 100.0, 50.0, 50.0});
        QVERIFY(viewModel.state().canUndo);
        const auto regionId = viewModel.state().regions.first().id;

        QVERIFY(viewModel.removeRegionForTest(regionId));
        viewModel.undo(); // Create replay removes a region that no longer exists.
        QVERIFY(viewModel.state().undoHistoryFaulted);

        viewModel.redo(); // No-op while faulted.
        QVERIFY(viewModel.state().undoHistoryFaulted);
        QVERIFY(viewModel.state().regions.isEmpty());
    }

    void testProcessingAdoptionCrossPageNavigates() {
        Fixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        auto session = makeSession(2, 1);
        viewModel.openProjectSession(session);
        QTRY_VERIFY_WITH_TIMEOUT(!viewModel.state().isPageLoading, 2000);

        auto page1 = session.project.pages()[0].id();
        auto page2 = session.project.pages()[1].id();
        auto region2 = session.project.pages()[1].regions()[0].id();

        viewModel.selectPage(page1);
        QCOMPARE(viewModel.state().currentPageId, page1);

        application::PreparedProcessingAdoption prepared;
        prepared.stage = application::OperationStage::Ocr;
        application::PreparedRegionChange change;
        change.pageId = page2;
        change.regionId = region2;
        change.expectedRevision = session.project.pages()[1].regions()[0].contentRevision();
        change.before.regionId = region2;
        change.after.regionId = region2;
        change.after.source = domain::TextContent{};
        change.after.source->setOcrResult("Cross Page", 1.0);
        prepared.changes.append(change);

        auto result = viewModel.adoptProcessingProposal(prepared);
        QVERIFY(result.has_value());

        QCOMPARE(viewModel.state().currentPageId, page2);
        QCOMPARE(viewModel.state().selectedRegionIds.size(), 1);
        QCOMPARE(viewModel.state().selectedRegionIds.first(), region2);

        // Switch to page 1, then undo should switch back to page 2
        viewModel.selectPage(page1);
        QCOMPARE(viewModel.state().currentPageId, page1);

        viewModel.undo();
        QCOMPARE(viewModel.state().currentPageId, page2);

        viewModel.selectPage(page1);
        viewModel.redo();
        QCOMPARE(viewModel.state().currentPageId, page2);

        viewModel.rejectProcessingProposal({});
    }
};

QTEST_GUILESS_MAIN(EditorViewModelTest)
#include "editor_view_model_test.moc"
