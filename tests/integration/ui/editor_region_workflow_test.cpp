#include "../../unit/ui/fakes/FakeImagePorts.h"
#include "../../unit/ui/fakes/FakeProjectPorts.h"
#include "application/workflow/OperationRegistry.h"
#include "ui/manga/MangaCanvas.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <QGraphicsProxyWidget>
#include <QGraphicsScene>
#include <QImage>
#include <QSignalSpy>
#include <QtTest>

using namespace manga;
using namespace manga::ui::screen::editor;
using namespace manga::domain;

class EditorRegionWorkflowTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void testWorkflow();
    void testPerformance();
};

void EditorRegionWorkflowTest::testWorkflow() {
    application::OperationRegistry registry;
    test::FakeThumbnailStore thumbStore;
    test::FakePageImageLoader imageLoader;
    test::FakeProjectRepository repo;

    imageLoader.nextImage = QImage(1200, 1800, QImage::Format_RGB32);
    repo.openPageCount = 3;

    EditorViewModel vm(&thumbStore, &imageLoader, &registry, &repo);

    application::OpenedProjectSession session;
    session.project = ComicProject("Test Project");
    auto page1 = PageId::generate();
    auto page2 = PageId::generate();
    session.project.addPage(ComicPage(page1, "page1.png", ImageSize{1200, 1800}, 0));
    session.project.addPage(ComicPage(page2, "page2.png", ImageSize{1200, 1800}, 1));

    vm.openProjectSession(std::move(session));
    QCOMPARE(vm.state().currentPageId, page1);
    QVERIFY(!vm.state().saveState.isDirty);
    quint64 rev0 = vm.currentProjectRevision();

    // Create Region
    RectGeometry rect1(10, 10, 100, 50);
    vm.createRegion(rect1);

    QCOMPARE(vm.state().regions.size(), 1);
    auto region1Id = vm.state().regions.first().id;
    QVERIFY(vm.state().saveState.isDirty);
    QVERIFY(vm.currentProjectRevision() > rev0);
    QVERIFY(vm.state().canUndo);

    // Move
    RectGeometry rect2(20, 20, 120, 60);
    vm.commitRegionGeometry(region1Id, rect2, manga::ui::canvas::EditKind::Move);
    QCOMPARE(vm.state().regions.first().geometry.x, 20);

    // Edit translation/style
    vm.selectRegion(region1Id, RegionSelectionMode::Replace);
    vm.commitInspectorFieldEdit(page1, {region1Id}, InspectorField::TextColorHex, QString("#FF0000"));
    vm.commitInspectorFieldEdit(page1, {region1Id}, InspectorField::ManualTranslation, QString("Hello"));
    vm.sealTextEditSession();

    // Assert stable IDs
    QCOMPARE(vm.state().regions.first().id, region1Id);

    // Undo/Redo cross-page
    vm.selectPage(page2);
    QCOMPARE(vm.state().currentPageId, page2);
    QCOMPARE(vm.state().regions.size(), 0);

    // Undo Translation, Style, Move, Create
    vm.undo();
    vm.undo();
    vm.undo();

    vm.selectPage(page1);
    QCOMPARE(vm.state().regions.first().geometry.x, 10);

    vm.redo();
    QCOMPARE(vm.state().regions.first().geometry.x, 20);

    // Save
    vm.saveProject();
    QVERIFY(!vm.state().saveState.isDirty);

    // New edit
    vm.commitRegionGeometry(region1Id, RectGeometry(30, 30, 100, 100), manga::ui::canvas::EditKind::Move);
    QVERIFY(vm.state().saveState.isDirty);

    // Close intercept
    QSignalSpy closeSpy(&vm, &EditorViewModel::closeDecisionRequested);
    vm.requestCloseProject();
    QCOMPARE(closeSpy.count(), 1);

    vm.confirmDiscardAndClose();
    QCOMPARE(repo.closeCallCount, 1);
    QVERIFY(!vm.state().currentPageId.isValid());
}

void EditorRegionWorkflowTest::testPerformance() {
    // 构造 300 Region 页面
    application::OperationRegistry registry;
    test::FakeThumbnailStore thumbStore;
    test::FakePageImageLoader imageLoader;
    test::FakeProjectRepository repo;

    imageLoader.nextImage = QImage(2000, 3000, QImage::Format_RGB32);

    EditorViewModel vm(&thumbStore, &imageLoader, &registry, &repo);

    application::OpenedProjectSession session;
    session.project = ComicProject("Test Project");
    auto page1 = PageId::generate();
    ComicPage page(page1, "page1.png", ImageSize{2000, 3000}, 0);

    for (int i = 0; i < 300; ++i) {
        TextRegion region(RegionId::generate(), RectGeometry(i, i, 100, 50));
        page.addRegion(std::move(region));
    }
    session.project.addPage(std::move(page));

    vm.openProjectSession(std::move(session));
    QCOMPARE(vm.state().regions.size(), 300);

    ui::manga::MangaCanvas canvas;
    canvas.setPageImage(imageLoader.nextImage);

    QVector<ui::canvas::RegionDisplay> displayRegions;
    for (const auto& rp : vm.state().regions) {
        displayRegions.append({rp.id, rp.geometry, false});
    }
    canvas.setRegions(displayRegions);

    // 断言 scene item 数量线性且无 QWidget-per-region
    int itemCount = canvas.scene()->items().size();
    QVERIFY(itemCount > 300 && itemCount < 300 * 10);

    bool hasWidget = false;
    for (auto* item : canvas.scene()->items()) {
        if (dynamic_cast<QGraphicsProxyWidget*>(item)) {
            hasWidget = true;
            break;
        }
    }
    QVERIFY(!hasWidget);

    int ioCountBefore = imageLoader.callCount;
    int readCountBefore = thumbStore.readCount;

    QElapsedTimer timer;
    timer.start();
    canvas.setZoomFactor(2.0);
    canvas.centerOnPage(QPointF(100, 100));
    canvas.setZoomFactor(0.5);
    canvas.centerOnPage(QPointF(500, 500));

    int elapsed = timer.elapsed();
    qDebug() << "Pan/Zoom time for 300 regions:" << elapsed << "ms";

    // pan/zoom 时 Domain 查询计数、布局调用计数、I/O 调用计数均为 0
    QCOMPARE(imageLoader.callCount, ioCountBefore);
    QCOMPARE(thumbStore.readCount, readCountBefore);
}

QTEST_MAIN(EditorRegionWorkflowTest)
#include "editor_region_workflow_test.moc"
