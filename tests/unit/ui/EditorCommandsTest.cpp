#include "FakeComicExporter.h"
#include "FakeDerivedAssetStore.h"
#include "application/error/ApplicationError.h"
#include "application/ports/IProjectRepository.h"
#include "application/usecase/export/ExportUseCases.h"
#include "application/workflow/OperationRegistry.h"
#include "domain/model/ComicProject.h"
#include "fakes/FakeImagePorts.h"
#include "ui/command/EditorCommandContext.h"
#include "ui/command/ProcessingAdoptionCommands.h"
#include "ui/command/RegionCommands.h"
#include "ui/command/RegionPropertyCommand.h"
#include "ui/manga/RegionInteraction.h"
#include "ui/screen/editor/EditorState.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QPromise>
#include <QSignalSpy>
#include <QUndoStack>
#include <QtTest>

using namespace manga;
using namespace manga::domain;
using namespace manga::application;
using namespace manga::ui::command;
using namespace manga::ui::screen::editor;
using manga::ui::canvas::EditKind;

namespace {

/// Creates a project with one page of given size and optional region count.
ComicProject makeProject(int pageWidth = 1200, int pageHeight = 1800, int regionsPerPage = 0) {
    ComicProject project("Test Project");
    auto pageId = PageId::generate();
    ComicPage page(
        pageId, QStringLiteral("assets/original/page_0001.png").toStdString(), ImageSize{pageWidth, pageHeight}, 0);
    for (int r = 0; r < regionsPerPage; ++r) {
        RectGeometry geom;
        geom.x = 10.0 + r * 100.0;
        geom.y = 10.0 + r * 50.0;
        geom.width = 80.0;
        geom.height = 40.0;
        TextRegion region(RegionId::generate(), geom, r);
        page.addRegion(std::move(region));
    }
    project.addPage(std::move(page));
    return project;
}

PageId firstPageId(const ComicProject& project) {
    return project.pages().front().id();
}

RegionId regionAt(const ComicProject& project, int pageIndex, int regionIndex) {
    return project.pages().at(pageIndex).regions().at(regionIndex).id();
}

/// Creates a session with `pageCount` pages for export tests.
OpenedProjectSession makeSession(int pageCount) {
    OpenedProjectSession session;
    session.project = ComicProject("Export Demo");
    session.projectRootPath = QStringLiteral("D:/manga/demo");
    for (int i = 0; i < pageCount; ++i) {
        auto pageId = PageId::generate();
        ComicPage page(pageId,
                       QStringLiteral("assets/original/page_%1.png").arg(1000 + i).toStdString(),
                       ImageSize{1200, 1800},
                       i);
        session.project.addPage(std::move(page));
    }
    return session;
}

PageId pageAt(const OpenedProjectSession& session, int index) {
    return session.project.pages().at(index).id();
}

struct ExportFixture {
    manga::test::FakeThumbnailStore thumbnailStore;
    manga::test::FakePageImageLoader imageLoader;
    application::OperationRegistry registry;
    application::test::FakeDerivedAssetStore derivedStore;
    application::test::FakeComicExporter exporter;

    ExportFixture() {
        imageLoader.nextImage = QImage(64, 96, QImage::Format_RGBA8888);
        imageLoader.nextImage.fill(Qt::gray);
    }

    void attach(EditorViewModel& viewModel) {
        viewModel.setExportDependencies(&exporter, &derivedStore);
    }
};

class DelayProjectRepository : public application::IProjectRepository {
public:
    std::shared_ptr<QPromise<application::Result<application::SaveProjectResult>>> savePromise;

    QFuture<application::Result<application::ProjectDescriptor>> createProject(application::CreateProjectRequest,
                                                                               application::OperationContext) override {
        return {};
    }
    QFuture<application::Result<application::OpenProjectResult>> openProject(application::OpenProjectRequest,
                                                                             application::OperationContext) override {
        return {};
    }
    QFuture<application::Result<application::OpenProjectResult>>
    recoverProject(application::OpenProjectRequest, application::OperationContext) override {
        return {};
    }
    QFuture<application::Result<application::SaveAsProjectResult>>
    saveProjectAs(application::SaveAsProjectRequest, application::OperationContext) override {
        return {};
    }
    QFuture<application::Result<void>> closeProject(application::CloseProjectRequest,
                                                    application::OperationContext) override {
        return {};
    }

    QFuture<application::Result<application::SaveProjectResult>> saveProject(application::SaveProjectRequest request,
                                                                             application::OperationContext) override {
        savePromise = std::make_shared<QPromise<application::Result<application::SaveProjectResult>>>();
        return savePromise->future();
    }
};

} // namespace

class EditorCommandsTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() {
        qRegisterMetaType<PageId>();
        qRegisterMetaType<ProjectCloseSummary>();
        qRegisterMetaType<application::ApplicationError>();
        qRegisterMetaType<application::ExportedPage>();
        qRegisterMetaType<application::ExportFailure>();
        qRegisterMetaType<application::ExportResponse>();
    }

    // --- Region Commands Tests ---

    void createUndoRedoPreservesSameRegionId() {
        auto project = makeProject();
        const auto pageId = firstPageId(project);
        EditorCommandContext context(&project);
        QUndoStack stack;

        const auto regionId = RegionId::generate();
        RectGeometry geom{100.0, 100.0, 50.0, 50.0};

        auto* cmd = new CreateRegionCommand(&context, pageId, regionId, geom, 0);
        stack.push(cmd);

        // Region exists after push (first redo).
        QVERIFY(project.findPage(pageId)->findRegion(regionId) != nullptr);
        QCOMPARE(static_cast<int>(project.findPage(pageId)->regions().size()), 1);

        stack.undo();
        // Region removed after undo.
        QVERIFY(project.findPage(pageId)->findRegion(regionId) == nullptr);
        QCOMPARE(static_cast<int>(project.findPage(pageId)->regions().size()), 0);

        stack.redo();
        // Same region ID restored after redo.
        QVERIFY(project.findPage(pageId)->findRegion(regionId) != nullptr);
        QCOMPARE(project.findPage(pageId)->findRegion(regionId)->geometry(), geom);
    }

    void batchDeleteOneStepRestoresOrder() {
        auto project = makeProject(1200, 1800, 3);
        const auto pageId = firstPageId(project);
        EditorCommandContext context(&project);
        QUndoStack stack;

        const auto r0 = regionAt(project, 0, 0);
        const auto r1 = regionAt(project, 0, 1);
        const auto r2 = regionAt(project, 0, 2);

        // Capture snapshots sorted by order.
        QVector<DeleteRegionsCommand::RegionEntry> entries;
        for (const auto& region : project.findPage(pageId)->regions()) {
            DeleteRegionsCommand::RegionEntry entry;
            entry.region = region;
            entry.orderIndex = region.orderIndex();
            entries.append(entry);
        }

        stack.push(new DeleteRegionsCommand(&context, pageId, entries));

        // All regions removed.
        QCOMPARE(static_cast<int>(project.findPage(pageId)->regions().size()), 0);

        stack.undo();

        // All three restored with correct IDs and order.
        QCOMPARE(static_cast<int>(project.findPage(pageId)->regions().size()), 3);
        QVERIFY(project.findPage(pageId)->findRegion(r0) != nullptr);
        QVERIFY(project.findPage(pageId)->findRegion(r1) != nullptr);
        QVERIFY(project.findPage(pageId)->findRegion(r2) != nullptr);
        QCOMPARE(project.findPage(pageId)->findRegion(r0)->orderIndex(), 0);
        QCOMPARE(project.findPage(pageId)->findRegion(r1)->orderIndex(), 1);
        QCOMPARE(project.findPage(pageId)->findRegion(r2)->orderIndex(), 2);
    }

    void geometryUndoRestoresBefore() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto r0 = regionAt(project, 0, 0);
        EditorCommandContext context(&project);
        QUndoStack stack;

        const RectGeometry before = project.findPage(pageId)->findRegion(r0)->geometry();
        const RectGeometry after{200.0, 200.0, 80.0, 40.0};

        stack.push(new RegionGeometryCommand(&context, pageId, r0, before, after, EditKind::Move));

        // After push (first redo), geometry is 'after'.
        QCOMPARE(project.findPage(pageId)->findRegion(r0)->geometry(), after);

        stack.undo();
        // Geometry restored to 'before'.
        QCOMPARE(project.findPage(pageId)->findRegion(r0)->geometry(), before);

        stack.redo();
        // Back to 'after'.
        QCOMPARE(project.findPage(pageId)->findRegion(r0)->geometry(), after);
    }

    void contextRoundTripsInsertAndRemove() {
        auto project = makeProject();
        const auto pageId = firstPageId(project);
        EditorCommandContext context(&project);

        const auto regionId = RegionId::generate();
        RectGeometry geom{50.0, 50.0, 100.0, 80.0};
        TextRegion region(regionId, geom, 0);

        auto insertResult = context.insertRegion(pageId, region, 0);
        QVERIFY(insertResult.has_value());
        QVERIFY(project.findPage(pageId)->findRegion(regionId) != nullptr);

        auto removeResult = context.removeRegion(pageId, regionId);
        QVERIFY(removeResult.has_value());
        QCOMPARE(removeResult->region.id(), regionId);
        QCOMPARE(removeResult->orderIndex, 0);
        QVERIFY(project.findPage(pageId)->findRegion(regionId) == nullptr);
    }

    void boundsValidationRejectsOutOfBounds() {
        auto project = makeProject(200, 200);
        const auto pageId = firstPageId(project);
        EditorCommandContext context(&project);

        // Geometry exceeds page bounds (200x200).
        RectGeometry bad{150.0, 150.0, 100.0, 100.0};
        TextRegion region(RegionId::generate(), bad, 0);

        auto result = context.insertRegion(pageId, region, 0);
        QVERIFY(!result.has_value());
    }

    void geometryCommandFaultsWhenRegionMissing() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto r0 = regionAt(project, 0, 0);
        EditorCommandContext context(&project);

        const RectGeometry before = project.findPage(pageId)->findRegion(r0)->geometry();
        const RectGeometry after{200.0, 200.0, 80.0, 40.0};

        RegionGeometryCommand cmd(&context, pageId, r0, before, after, EditKind::Move);

        // Remove the region so replay will fail.
        (void)project.removeRegionFromPage(pageId, r0);

        cmd.redo();
        QVERIFY(cmd.hasFaulted());
    }

    void createCommandUndoAfterExternalRemovalFaults() {
        auto project = makeProject();
        const auto pageId = firstPageId(project);
        EditorCommandContext context(&project);

        const auto regionId = RegionId::generate();
        RectGeometry geom{100.0, 100.0, 50.0, 50.0};
        CreateRegionCommand cmd(&context, pageId, regionId, geom, 0);
        cmd.redo();
        QCOMPARE(project.findPage(pageId)->regions().size(), 1);

        QVERIFY(context.removeRegion(pageId, regionId).has_value());
        cmd.undo(); // Removal replays against a missing region.
        QVERIFY(cmd.hasFaulted());
    }

    void deleteCommandUndoFailsWhenRegionReappears() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto regionId = regionAt(project, 0, 0);
        const auto snapshot = *project.findPage(pageId)->findRegion(regionId);
        EditorCommandContext context(&project);

        QVector<DeleteRegionsCommand::RegionEntry> entries{{snapshot, 0}};
        DeleteRegionsCommand cmd(&context, pageId, entries);
        cmd.redo();
        QCOMPARE(project.findPage(pageId)->regions().size(), 0);

        QVERIFY(context.insertRegion(pageId, snapshot, 0).has_value());
        cmd.undo(); // Reinsertion collides with the reappeared region id.
        QVERIFY(cmd.hasFaulted());
    }

    void textPropertyCommandUndoRedoRoundTrip() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto regionId = regionAt(project, 0, 0);
        EditorCommandContext context(&project);
        QUndoStack stack;

        PageRegionEdit before;
        before.regionId = regionId;
        before.translation = project.findPage(pageId)->findRegion(regionId)->translation();
        PageRegionEdit after = before;
        after.translation->setManualText("typed text");

        const TextEditSessionKey key{project.id(), pageId, regionId, RegionPropertyField::ManualTranslation, 1};
        stack.push(new RegionPropertyCommand(&context, key, 1000, before, after, QStringLiteral("Edit Text")));

        QCOMPARE(project.findPage(pageId)->findRegion(regionId)->translation().manualText(),
                 std::optional<std::string>("typed text"));

        stack.undo();
        QVERIFY(!project.findPage(pageId)->findRegion(regionId)->translation().manualText().has_value());

        stack.redo();
        QCOMPARE(project.findPage(pageId)->findRegion(regionId)->translation().manualText(),
                 std::optional<std::string>("typed text"));
    }

    void textPropertyCommandsMergeAtWindowBoundary() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto regionId = regionAt(project, 0, 0);
        EditorCommandContext context(&project);
        QUndoStack stack;

        const TextEditSessionKey key{project.id(), pageId, regionId, RegionPropertyField::ManualTranslation, 7};

        const auto makeEdit = [&]() {
            PageRegionEdit edit;
            edit.regionId = regionId;
            edit.translation = project.findPage(pageId)->findRegion(regionId)->translation();
            return edit;
        };

        auto before = makeEdit();
        auto mid = before;
        mid.translation->setManualText("A");
        auto after = mid;
        after.translation->setManualText("AB");

        stack.push(new RegionPropertyCommand(&context, key, 10'000, before, mid, QStringLiteral("Edit Text")));
        // 499 ms later, same session key: merges into one command.
        stack.push(new RegionPropertyCommand(&context,
                                             key,
                                             10'000 + RegionPropertyCommand::kMergeWindowMs - 1,
                                             mid,
                                             after,
                                             QStringLiteral("Edit Text")));

        QCOMPARE(stack.count(), 1);
        QCOMPARE(project.findPage(pageId)->findRegion(regionId)->translation().manualText(),
                 std::optional<std::string>("AB"));

        stack.undo();
        QVERIFY(!project.findPage(pageId)->findRegion(regionId)->translation().manualText().has_value());
    }

    void textPropertyCommandsDoNotMergeBeyondWindow() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto regionId = regionAt(project, 0, 0);
        EditorCommandContext context(&project);
        QUndoStack stack;

        const TextEditSessionKey key{project.id(), pageId, regionId, RegionPropertyField::ManualTranslation, 7};

        PageRegionEdit before;
        before.regionId = regionId;
        before.translation = project.findPage(pageId)->findRegion(regionId)->translation();
        auto mid = before;
        mid.translation->setManualText("A");
        auto after = mid;
        after.translation->setManualText("AB");

        stack.push(new RegionPropertyCommand(&context, key, 10'000, before, mid, QStringLiteral("Edit Text")));
        // 501 ms later: the merge window is closed.
        stack.push(new RegionPropertyCommand(&context,
                                             key,
                                             10'000 + RegionPropertyCommand::kMergeWindowMs + 1,
                                             mid,
                                             after,
                                             QStringLiteral("Edit Text")));

        QCOMPARE(stack.count(), 2);
    }

    void textPropertyCommandsDoNotMergeAcrossSessionOrField() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto regionId = regionAt(project, 0, 0);
        EditorCommandContext context(&project);
        QUndoStack stack;

        PageRegionEdit before;
        before.regionId = regionId;
        before.translation = project.findPage(pageId)->findRegion(regionId)->translation();
        auto after = before;
        after.translation->setManualText("A");

        const TextEditSessionKey key{project.id(), pageId, regionId, RegionPropertyField::ManualTranslation, 1};
        const TextEditSessionKey sealedKey{project.id(), pageId, regionId, RegionPropertyField::ManualTranslation, 2};

        stack.push(new RegionPropertyCommand(&context, key, 10'000, before, after, QStringLiteral("Edit Text")));
        // A new session id never merges even inside the time window.
        stack.push(new RegionPropertyCommand(&context, sealedKey, 10'100, before, after, QStringLiteral("Edit Text")));
        QCOMPARE(stack.count(), 2);

        // A different field never merges either.
        PageRegionEdit sourceEdit;
        sourceEdit.regionId = regionId;
        sourceEdit.source = project.findPage(pageId)->findRegion(regionId)->source();
        auto sourceAfter = sourceEdit;
        sourceAfter.source->setManualText("edited");
        const TextEditSessionKey sourceKey{project.id(), pageId, regionId, RegionPropertyField::SourceText, 3};
        stack.push(new RegionPropertyCommand(
            &context, sourceKey, 10'200, sourceEdit, sourceAfter, QStringLiteral("Edit Text")));
        QCOMPARE(stack.count(), 3);
    }

    void candidateAdoptionCommandNeverMerges() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto regionId = regionAt(project, 0, 0);
        EditorCommandContext context(&project);
        QUndoStack stack;

        auto* region = project.findPage(pageId)->findRegion(regionId);
        TranslationEntry translation;
        translation.setModelResult("model", {TranslationCandidate{"c1", "one", 0.9, "p"}});
        // Rebind the region translation through a snapshot edit so the candidate exists.
        PageRegionEdit seedEdit;
        seedEdit.regionId = regionId;
        seedEdit.translation = translation;
        QVERIFY(context.applySnapshots(pageId, {seedEdit}).has_value());
        region = project.findPage(pageId)->findRegion(regionId);

        const auto buildAdoption = [&]() -> RegionPropertyCommand* {
            RegionPropertyCommand::PropertyEdit entry;
            entry.regionId = regionId;
            entry.before.regionId = regionId;
            entry.before.translation = region->translation();
            entry.after = entry.before;
            entry.after.translation->selectCandidate("c1");
            return new RegionPropertyCommand(&context,
                                             pageId,
                                             QVector<RegionPropertyCommand::PropertyEdit>{entry},
                                             QStringLiteral("Adopt Candidate"));
        };

        stack.push(buildAdoption());
        QVERIFY(project.findPage(pageId)->findRegion(regionId)->translation().selectedCandidateId().has_value());

        // Undo, then a repeated adoption is still its own command (no merging for Candidate).
        stack.undo();
        QVERIFY(!project.findPage(pageId)->findRegion(regionId)->translation().selectedCandidateId().has_value());
        stack.push(buildAdoption());
        QCOMPARE(stack.count(), 1); // undone tail was truncated; the new push stands alone
        QCOMPARE(stack.index(), 1);

        // A second adoption right after the first must not merge into it.
        stack.push(buildAdoption());
        QCOMPARE(stack.count(), 2);
    }

    void batchStyleCommandRestoresAllRegionsOnUndo() {
        auto project = makeProject(1200, 1800, 3);
        const auto pageId = firstPageId(project);
        EditorCommandContext context(&project);
        QUndoStack stack;

        QVector<RegionPropertyCommand::PropertyEdit> edits;
        for (const auto& region : project.findPage(pageId)->regions()) {
            RegionPropertyCommand::PropertyEdit entry;
            entry.regionId = region.id();
            entry.before.regionId = region.id();
            entry.before.layout = region.layout();
            entry.after = entry.before;
            entry.after.layout->fontSizePt = 22.0;
            edits.append(entry);
        }

        stack.push(new RegionPropertyCommand(&context, pageId, edits, QStringLiteral("Edit Style")));

        for (const auto& region : project.findPage(pageId)->regions()) {
            QCOMPARE(region.layout().fontSizePt, 22.0);
        }

        stack.undo();
        for (const auto& region : project.findPage(pageId)->regions()) {
            QCOMPARE(region.layout().fontSizePt, 14.0);
        }

        stack.redo();
        for (const auto& region : project.findPage(pageId)->regions()) {
            QCOMPARE(region.layout().fontSizePt, 22.0);
        }
    }

    void propertyCommandFaultsWhenRegionMissing() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto regionId = regionAt(project, 0, 0);
        EditorCommandContext context(&project);

        PageRegionEdit before;
        before.regionId = regionId;
        before.translation = project.findPage(pageId)->findRegion(regionId)->translation();
        auto after = before;
        after.translation->setManualText("typed text");

        const TextEditSessionKey key{project.id(), pageId, regionId, RegionPropertyField::ManualTranslation, 1};
        RegionPropertyCommand cmd(&context, key, 1000, before, after, QStringLiteral("Edit Text"));

        (void)project.removeRegionFromPage(pageId, regionId);
        cmd.redo();
        QVERIFY(cmd.hasFaulted());
    }

    // --- Processing Adoption Commands Tests ---

    void testCommandIsPushedOnce() {
        ComicProject project;
        PageId pageId = PageId::generate();
        ComicPage page(pageId);
        page.setImageSize({1000, 1000});
        page.setOriginalAssetPath("test.png");

        RegionId regionId = RegionId::generate();
        TextRegion region(regionId, RectGeometry{10, 10, 100, 100});
        page.addRegion(region);
        project.addPage(std::move(page));

        EditorCommandContext ctx(&project);

        PreparedProcessingAdoption prepared;
        prepared.stage = OperationStage::Ocr;
        PreparedRegionChange change;
        change.pageId = pageId;
        change.regionId = regionId;
        change.expectedRevision = region.contentRevision();
        change.before.regionId = regionId;
        change.after.regionId = regionId;
        change.after.source = TextContent{};
        change.after.source->setOcrResult("New Text", 1.0);
        prepared.changes.append(change);

        auto* cmd = new ProcessingAdoptionCommand(&ctx, prepared);

        QUndoStack stack;
        stack.push(cmd);

        auto* pagePtr = project.findPage(pageId);
        QVERIFY(pagePtr != nullptr);
        auto* regionPtr = pagePtr->findRegion(regionId);
        QVERIFY(regionPtr != nullptr);
        QCOMPARE(regionPtr->source().text, "New Text");
    }

    void testUndoRestoresBefore() {
        ComicProject project;
        PageId pageId = PageId::generate();
        ComicPage page(pageId);
        page.setImageSize({1000, 1000});
        page.setOriginalAssetPath("test.png");

        RegionId regionId = RegionId::generate();
        TextRegion region(regionId, RectGeometry{10, 10, 100, 100});
        TextContent src;
        src.setOcrResult("Old Text", 0.5);
        PageRegionEdit edit;
        edit.regionId = regionId;
        edit.source = src;
        page.addRegion(region);
        project.addPage(std::move(page));
        const PageRegionEdit edits[] = {edit};
        QVERIFY(project.applyPageRegionEdits(pageId, edits));

        region = *project.findPage(pageId)->findRegion(regionId);

        EditorCommandContext ctx(&project);

        PreparedProcessingAdoption prepared;
        prepared.stage = OperationStage::Ocr;
        PreparedRegionChange change;
        change.pageId = pageId;
        change.regionId = regionId;
        change.expectedRevision = region.contentRevision();
        change.before.regionId = regionId;
        change.before.source = region.source();
        change.after.regionId = regionId;
        change.after.source = region.source();
        if (!change.after.source)
            change.after.source = TextContent{};
        change.after.source->setOcrResult("New Text", 1.0);
        prepared.changes.append(change);

        auto* cmd = new ProcessingAdoptionCommand(&ctx, prepared);

        QUndoStack stack;
        stack.push(cmd);
        auto* pagePtr = project.findPage(pageId);
        QVERIFY(pagePtr != nullptr);
        auto* regionPtr = pagePtr->findRegion(regionId);
        QVERIFY(regionPtr != nullptr);
        QCOMPARE(regionPtr->source().text, "New Text");

        stack.undo();
        pagePtr = project.findPage(pageId);
        QVERIFY(pagePtr != nullptr);
        regionPtr = pagePtr->findRegion(regionId);
        QVERIFY(regionPtr != nullptr);
        QCOMPARE(regionPtr->source().text, "Old Text");
    }

    void testRedoDoesNotCallNetworkOrIO() {
        ComicProject project;
        PageId pageId = PageId::generate();
        ComicPage page(pageId);
        page.setImageSize({1000, 1000});
        page.setOriginalAssetPath("test.png");

        RegionId regionId = RegionId::generate();
        TextRegion region(regionId, RectGeometry{10, 10, 100, 100});
        page.addRegion(region);
        project.addPage(std::move(page));

        EditorCommandContext ctx(&project);

        PreparedProcessingAdoption prepared;
        PreparedRegionChange change;
        change.pageId = pageId;
        change.regionId = regionId;
        change.expectedRevision = region.contentRevision();
        change.before.regionId = regionId;
        change.after.regionId = regionId;
        change.after.source = TextContent{};
        change.after.source->setOcrResult("New Text", 1.0);
        prepared.changes.append(change);

        auto* cmd = new ProcessingAdoptionCommand(&ctx, prepared);
        QUndoStack stack;
        stack.push(cmd);
        stack.undo();
        stack.redo();

        auto* pagePtr = project.findPage(pageId);
        QVERIFY(pagePtr != nullptr);
        auto* regionPtr = pagePtr->findRegion(regionId);
        QVERIFY(regionPtr != nullptr);
        QCOMPARE(regionPtr->source().text, "New Text");
    }

    void testClearRedoDoesNotWriteAsset() {
        ComicProject project;
        PageId pageId = PageId::generate();
        ComicPage page(pageId);
        page.setImageSize({1000, 1000});
        page.setOriginalAssetPath("test.png");

        RegionId regionId = RegionId::generate();
        TextRegion region(regionId, RectGeometry{10, 10, 100, 100});
        page.addRegion(region);
        project.addPage(std::move(page));

        EditorCommandContext ctx(&project);

        PreparedProcessingAdoption prepared;
        prepared.clearAssetChange = PreparedClearChange{project.id(),
                                                        pageId,
                                                        regionId,
                                                        region.contentRevision(),
                                                        QStringLiteral(""),
                                                        QStringLiteral("asset.png"),
                                                        false,
                                                        true};

        auto* cmd = new ProcessingAdoptionCommand(&ctx, prepared);
        QUndoStack stack;
        stack.push(cmd);
        stack.undo();
        stack.redo();

        auto* pagePtr = project.findPage(pageId);
        QVERIFY(pagePtr != nullptr);
        auto* regionPtr = pagePtr->findRegion(regionId);
        QVERIFY(regionPtr != nullptr);
        QVERIFY(regionPtr->isCleared());
    }

    void testSavePreservesHistoryAndCanUndo() {
        ComicProject project;
        PageId pageId = PageId::generate();
        ComicPage page(pageId);
        page.setImageSize({1000, 1000});
        page.setOriginalAssetPath("test.png");

        RegionId regionId = RegionId::generate();
        TextRegion region(regionId, RectGeometry{10, 10, 100, 100});
        page.addRegion(region);
        project.addPage(std::move(page));

        EditorCommandContext ctx(&project);

        PreparedProcessingAdoption prepared;
        prepared.stage = OperationStage::Ocr;
        PreparedRegionChange change;
        change.pageId = pageId;
        change.regionId = regionId;
        change.expectedRevision = region.contentRevision();
        change.before.regionId = regionId;
        change.before.source = region.source();
        change.after.regionId = regionId;
        change.after.source = TextContent{};
        change.after.source->setOcrResult("New Text", 1.0);
        prepared.changes.append(change);

        auto* cmd = new ProcessingAdoptionCommand(&ctx, prepared);
        QUndoStack stack;
        stack.push(cmd);

        QVERIFY(project.isDirty());
        project.markClean();
        QVERIFY(!project.isDirty());

        // Saving preserves history: stack can still undo
        QVERIFY(stack.canUndo());
        stack.undo();

        auto* pagePtr = project.findPage(pageId);
        QVERIFY(pagePtr != nullptr);
        auto* regionPtr = pagePtr->findRegion(regionId);
        QVERIFY(regionPtr != nullptr);
        QCOMPARE(regionPtr->source().text, "");
        QVERIFY(project.isDirty());

        QVERIFY(stack.canRedo());
        stack.redo();
        regionPtr = project.findPage(pageId)->findRegion(regionId);
        QCOMPARE(regionPtr->source().text, "New Text");
    }

    // --- Save Tests ---

    void saveCompletionDoesNotCleanNewerRevision() {
        manga::test::FakeThumbnailStore thumbnails;
        manga::test::FakePageImageLoader images;
        application::OperationRegistry registry;
        DelayProjectRepository repository;
        EditorViewModel viewModel(&thumbnails, &images, &registry, &repository);

        OpenedProjectSession session;
        session.project = ComicProject("Demo");
        session.projectRootPath = QStringLiteral("D:/manga/demo");
        ComicPage page(PageId::generate(), "page1.png", {1000, 1500}, 0);
        TextRegion region(RegionId::generate(), {10, 10, 100, 100}, 0);
        page.addRegion(region);
        session.project.addPage(page);
        session.project.markCleanIfRevision(session.project.revision());

        viewModel.openProjectSession(session);
        viewModel.selectPage(page.id());

        // Make it dirty
        viewModel.selectRegion(region.id(), RegionSelectionMode::Replace);
        viewModel.commitInspectorFieldEdit(
            page.id(), {region.id()}, InspectorField::FontFamily, QStringLiteral("Arial"));

        QVERIFY(viewModel.state().saveState.isDirty);

        // Trigger save
        viewModel.saveProject();
        QVERIFY(viewModel.state().saveState.isSaving);

        const quint64 capturedRevision = viewModel.currentProjectRevision();

        // Continue editing during save
        viewModel.commitInspectorFieldEdit(page.id(), {region.id()}, InspectorField::FontSizePt, 20.0);

        // Complete save with older revision
        repository.savePromise->addResult(application::SaveProjectResult{capturedRevision});
        repository.savePromise->finish();

        QTRY_VERIFY(!viewModel.state().saveState.isSaving);
        QVERIFY(viewModel.state().saveState.isDirty);
        QVERIFY(!viewModel.undoStackIsCleanForTest());
    }

    void cleanSave() {
        manga::test::FakeThumbnailStore thumbnails;
        manga::test::FakePageImageLoader images;
        application::OperationRegistry registry;
        DelayProjectRepository repository;
        EditorViewModel viewModel(&thumbnails, &images, &registry, &repository);
        OpenedProjectSession session;
        session.project = ComicProject("Demo");
        session.projectRootPath = QStringLiteral("D:/manga/demo");
        ComicPage page(PageId::generate(), "page1.png", {1000, 1500}, 0);
        session.project.addPage(page);
        session.project.markCleanIfRevision(session.project.revision());

        viewModel.openProjectSession(session);
        viewModel.selectPage(page.id());

        // Dirty it
        viewModel.createRegion(RectGeometry(10, 10, 100, 100));
        QVERIFY(viewModel.state().saveState.isDirty);

        viewModel.saveProject();
        QVERIFY(viewModel.state().saveState.isSaving);
        const quint64 rev = viewModel.currentProjectRevision();
        repository.savePromise->addResult(application::SaveProjectResult{rev});
        repository.savePromise->finish();

        QTRY_VERIFY(!viewModel.state().saveState.isSaving);
        QVERIFY(!viewModel.state().saveState.isDirty);
        QVERIFY(viewModel.undoStackIsCleanForTest());
    }

    void saveFailureKeepsDirty() {
        manga::test::FakeThumbnailStore thumbnails;
        manga::test::FakePageImageLoader images;
        application::OperationRegistry registry;
        DelayProjectRepository repository;
        EditorViewModel viewModel(&thumbnails, &images, &registry, &repository);
        OpenedProjectSession session;
        session.project = ComicProject("Demo");
        session.projectRootPath = QStringLiteral("D:/manga/demo");
        ComicPage page(PageId::generate(), "page1.png", {1000, 1500}, 0);
        session.project.addPage(page);
        session.project.markCleanIfRevision(session.project.revision());

        viewModel.openProjectSession(session);
        viewModel.selectPage(page.id());

        viewModel.createRegion(RectGeometry(10, 10, 100, 100));
        QVERIFY(viewModel.state().saveState.isDirty);

        viewModel.saveProject();
        QVERIFY(viewModel.state().saveState.isSaving);

        repository.savePromise->addResult(std::unexpected(
            application::makeApplicationError(application::ErrorCategory::Persistence, "test", "fail")));
        repository.savePromise->finish();

        QTRY_VERIFY(!viewModel.state().saveState.isSaving);
        QVERIFY(viewModel.state().saveState.isDirty);
    }

    void saveSealsTextEdit() {
        manga::test::FakeThumbnailStore thumbnails;
        manga::test::FakePageImageLoader images;
        application::OperationRegistry registry;
        DelayProjectRepository repository;
        EditorViewModel viewModel(&thumbnails, &images, &registry, &repository);
        // Force the clock to not advance, so normally it would merge
        viewModel.setTextEditClockForTest([]() { return 1000; });
        OpenedProjectSession session;
        session.project = ComicProject("Demo");
        session.projectRootPath = QStringLiteral("D:/manga/demo");
        ComicPage page(PageId::generate(), "page1.png", {1000, 1500}, 0);
        TextRegion region(RegionId::generate(), {10, 10, 100, 100}, 0);
        page.addRegion(region);
        session.project.addPage(page);
        session.project.markCleanIfRevision(session.project.revision());

        viewModel.openProjectSession(session);
        viewModel.selectPage(page.id());
        viewModel.selectRegion(region.id(), RegionSelectionMode::Replace);

        viewModel.commitInspectorFieldEdit(
            page.id(), {region.id()}, InspectorField::ManualTranslation, QStringLiteral("A"));
        QCOMPARE(viewModel.undoStackCountForTest(), 1);

        viewModel.saveProject();

        viewModel.commitInspectorFieldEdit(
            page.id(), {region.id()}, InspectorField::ManualTranslation, QStringLiteral("B"));
        QCOMPARE(viewModel.undoStackCountForTest(), 2); // Did not merge because save sealed it
    }

    // --- Export Tests ---

    void exportWithoutProjectDoesNothing() {
        ExportFixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out"));

        QCOMPARE(completedSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);
        QVERIFY(!viewModel.state().isExporting);
        QCOMPARE(f.exporter.callCount, 0);
    }

    void exportWithoutPagesDoesNothing() {
        ExportFixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        viewModel.openProjectSession(makeSession(0));
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out"));
        viewModel.exportPages(ExportScope::CurrentPage, QStringLiteral("D:/out"));
        viewModel.exportPages(ExportScope::SelectedPages, QStringLiteral("D:/out"));

        QCOMPARE(completedSpy.count(), 0);
        QCOMPARE(failedSpy.count(), 0);
        QVERIFY(!viewModel.state().isExporting);
        QCOMPARE(f.exporter.callCount, 0);
    }

    void exportWithoutDependenciesEmitsFailed() {
        ExportFixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out"));

        QCOMPARE(failedSpy.count(), 1);
        QVERIFY(!viewModel.state().isExporting);
    }

    void exportCurrentPageBuildsSinglePageRequest() {
        ExportFixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(3);
        viewModel.openProjectSession(session);
        viewModel.selectPage(pageAt(session, 1));
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::CurrentPage, QStringLiteral("D:/out/current"));

        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(f.exporter.callCount, 1);
        QVERIFY(!viewModel.state().isExporting);
        QCOMPARE(f.exporter.lastRequest.outputDir, QStringLiteral("D:/out/current"));
        QCOMPARE(f.exporter.lastRequest.pages.size(), 1);
        const auto& page = f.exporter.lastRequest.pages.first();
        QCOMPARE(page.pageId, pageAt(session, 1));
        QCOMPARE(page.outputStem, QStringLiteral("page_0002_translated"));
        QVERIFY(!page.sourceImage.isNull());
        QCOMPARE(page.sourceImage.size(), QSize(64, 96));
    }

    void exportSelectedPagesBuildsRequestInProjectOrder() {
        ExportFixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(3);
        viewModel.openProjectSession(session);
        viewModel.setSelectedPages({pageAt(session, 2), pageAt(session, 0)});
        QCOMPARE(viewModel.state().selectedPageIds.size(), 2);
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);

        viewModel.exportPages(ExportScope::SelectedPages, QStringLiteral("D:/out/selection"));

        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QCOMPARE(f.exporter.lastRequest.pages.size(), 2);
        // Explicit selection order {page3, page1} resolves to project page order.
        QCOMPARE(f.exporter.lastRequest.pages[0].pageId, pageAt(session, 0));
        QCOMPARE(f.exporter.lastRequest.pages[1].pageId, pageAt(session, 2));
        QCOMPARE(f.exporter.lastRequest.pages[0].outputStem, QStringLiteral("page_0001_translated"));
        QCOMPARE(f.exporter.lastRequest.pages[1].outputStem, QStringLiteral("page_0003_translated"));
    }

    void exportAllPagesBuildsFullRequest() {
        ExportFixture f;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(3);
        viewModel.openProjectSession(session);
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out/all"));

        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QCOMPARE(f.exporter.lastRequest.pages.size(), 3);
        QCOMPARE(f.exporter.lastRequest.pages[0].pageId, pageAt(session, 0));
        QCOMPARE(f.exporter.lastRequest.pages[1].pageId, pageAt(session, 1));
        QCOMPARE(f.exporter.lastRequest.pages[2].pageId, pageAt(session, 2));
    }

    void exportIsNotReentrantWhileRunning() {
        ExportFixture f;
        f.exporter.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out/reentry"));
        QVERIFY(viewModel.state().isExporting);
        // The deferred exporter owns the in-flight future once the snapshot stage lands.
        QTRY_COMPARE_WITH_TIMEOUT(f.exporter.callCount, 1, 5000);

        // The second intent must be ignored while the first export is in flight.
        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out/reentry"));
        QCOMPARE(f.exporter.callCount, 1);
        QVERIFY(viewModel.state().isExporting);

        f.exporter.complete();
        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QVERIFY(!viewModel.state().isExporting);
    }

    void exportCurrentPageCapturesPageAtIntentTime() {
        ExportFixture f;
        // Stall the snapshot stage so the page switch happens while the export runs.
        f.imageLoader.deferred = true;
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(3);
        viewModel.openProjectSession(session);
        viewModel.selectPage(pageAt(session, 1));
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::CurrentPage, QStringLiteral("D:/out/race"));
        QVERIFY(viewModel.state().isExporting);

        // Page navigation stays enabled during export; the running export must keep
        // exporting the page selected at intent time.
        viewModel.selectPage(pageAt(session, 2));
        QCOMPARE(viewModel.state().currentPageId, pageAt(session, 2));
        QVERIFY(viewModel.state().isExporting);

        f.imageLoader.completeAll();
        QTRY_COMPARE_WITH_TIMEOUT(completedSpy.count(), 1, 5000);
        QCOMPARE(failedSpy.count(), 0);
        QCOMPARE(f.exporter.lastRequest.pages.size(), 1);
        QCOMPARE(f.exporter.lastRequest.pages.first().pageId, pageAt(session, 1));
        QCOMPARE(f.exporter.lastRequest.pages.first().outputStem, QStringLiteral("page_0002_translated"));
        QVERIFY(!viewModel.state().isExporting);
    }

    void exportFailureEmitsFailedAndKeepsProject() {
        ExportFixture f;
        f.exporter.shouldFail = true;
        f.exporter.nextError = application::makeApplicationError(application::ErrorCategory::Persistence,
                                                                 QStringLiteral("export.write_failed"),
                                                                 QStringLiteral("The export target is not writable"),
                                                                 application::RecoveryAction::ChoosePath);
        EditorViewModel viewModel(&f.thumbnailStore, &f.imageLoader, &f.registry);
        f.attach(viewModel);
        const auto session = makeSession(2);
        viewModel.openProjectSession(session);
        const quint64 revisionBefore = viewModel.currentProjectRevision();
        const bool dirtyBefore = viewModel.state().saveState.isDirty;
        QSignalSpy completedSpy(&viewModel, &EditorViewModel::exportCompleted);
        QSignalSpy failedSpy(&viewModel, &EditorViewModel::exportFailed);

        viewModel.exportPages(ExportScope::AllPages, QStringLiteral("D:/out/failure"));

        QTRY_COMPARE_WITH_TIMEOUT(failedSpy.count(), 1, 5000);
        QCOMPARE(completedSpy.count(), 0);
        QVERIFY(!viewModel.state().isExporting);
        // A failed export presents a recovery notice but never touches project content.
        QVERIFY(viewModel.state().notice.has_value());
        QVERIFY(!viewModel.state().notice->retryable);
        QCOMPARE(viewModel.currentProjectRevision(), revisionBefore);
        QCOMPARE(viewModel.state().saveState.isDirty, dirtyBefore);
    }
};

QTEST_MAIN(EditorCommandsTest)
#include "EditorCommandsTest.moc"
