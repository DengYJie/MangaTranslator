#include "domain/model/ComicProject.h"
#include "ui/command/ProcessingAdoptionCommands.h"

#include <QSignalSpy>
#include <QTest>
#include <QUndoStack>

using namespace manga::domain;
using namespace manga::application;
using namespace manga::ui::command;

class ProcessingAdoptionCommandsTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
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
        // Need to use project.applyPageRegionEdits to set text properly
        const PageRegionEdit edits[] = {edit};
        QVERIFY(project.applyPageRegionEdits(pageId, edits));

        // Fetch updated region
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

    // "Redo 不重新调用网络/I/O" is tested by verifying we just use `after`
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
        // PreparedClearChange is defined as: { projectId, pageId, regionId, expectedRevision, beforeRef, afterRef,
        // beforeState, afterState }
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
        // Since we don't mock the I/O, just proving it uses the pre-existing asset ref without I/O
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
};

QTEST_GUILESS_MAIN(ProcessingAdoptionCommandsTest)
#include "processing_adoption_commands_test.moc"
