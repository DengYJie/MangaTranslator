#include "ui/command/EditorCommandContext.h"
#include "ui/command/RegionCommands.h"
#include "ui/manga/RegionInteraction.h"

#include <QUndoStack>
#include <QtTest>

using namespace manga;
using namespace manga::ui::command;
using manga::ui::canvas::EditKind;

namespace {

/// Creates a project with one page of given size and optional region count.
domain::ComicProject makeProject(int pageWidth = 1200, int pageHeight = 1800, int regionsPerPage = 0) {
    domain::ComicProject project("Test Project");
    auto pageId = domain::PageId::generate();
    domain::ComicPage page(pageId,
                           QStringLiteral("assets/original/page_0001.png").toStdString(),
                           domain::ImageSize{pageWidth, pageHeight},
                           0);
    for (int r = 0; r < regionsPerPage; ++r) {
        domain::RectGeometry geom;
        geom.x = 10.0 + r * 100.0;
        geom.y = 10.0 + r * 50.0;
        geom.width = 80.0;
        geom.height = 40.0;
        domain::TextRegion region(domain::RegionId::generate(), geom, r);
        page.addRegion(std::move(region));
    }
    project.addPage(std::move(page));
    return project;
}

domain::PageId firstPageId(const domain::ComicProject& project) {
    return project.pages().front().id();
}

domain::RegionId regionAt(const domain::ComicProject& project, int pageIndex, int regionIndex) {
    return project.pages().at(pageIndex).regions().at(regionIndex).id();
}

} // namespace

class EditorCommandsTest : public QObject {
    Q_OBJECT
private slots:
    void createUndoRedoPreservesSameRegionId() {
        auto project = makeProject();
        const auto pageId = firstPageId(project);
        EditorCommandContext context(&project);
        QUndoStack stack;

        const auto regionId = domain::RegionId::generate();
        domain::RectGeometry geom{100.0, 100.0, 50.0, 50.0};

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

        const domain::RectGeometry before = project.findPage(pageId)->findRegion(r0)->geometry();
        const domain::RectGeometry after{200.0, 200.0, 80.0, 40.0};

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

        const auto regionId = domain::RegionId::generate();
        domain::RectGeometry geom{50.0, 50.0, 100.0, 80.0};
        domain::TextRegion region(regionId, geom, 0);

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
        domain::RectGeometry bad{150.0, 150.0, 100.0, 100.0};
        domain::TextRegion region(domain::RegionId::generate(), bad, 0);

        auto result = context.insertRegion(pageId, region, 0);
        QVERIFY(!result.has_value());
    }

    void geometryCommandFaultsWhenRegionMissing() {
        auto project = makeProject(1200, 1800, 1);
        const auto pageId = firstPageId(project);
        const auto r0 = regionAt(project, 0, 0);
        EditorCommandContext context(&project);

        const domain::RectGeometry before = project.findPage(pageId)->findRegion(r0)->geometry();
        const domain::RectGeometry after{200.0, 200.0, 80.0, 40.0};

        RegionGeometryCommand cmd(&context, pageId, r0, before, after, EditKind::Move);

        // Remove the region so replay will fail.
        (void)project.removeRegionFromPage(pageId, r0);

        cmd.redo();
        QVERIFY(cmd.hasFaulted());
    }
};

QTEST_GUILESS_MAIN(EditorCommandsTest)
#include "editor_commands_test.moc"
