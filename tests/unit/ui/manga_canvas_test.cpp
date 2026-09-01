#include "ui/manga/CanvasZOrder.h"
#include "ui/manga/MangaCanvas.h"
#include "ui/manga/RegionInteraction.h"
#include "ui/manga/RegionOverlayItem.h"

#include <QApplication>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>

using manga::domain::ImageSize;
using manga::domain::RectGeometry;
using manga::domain::RegionId;
using manga::ui::canvas::EditKind;
using manga::ui::canvas::RegionDisplay;
using manga::ui::canvas::SelectionIntent;
using manga::ui::canvas::Tool;
using manga::ui::manga::MangaCanvas;

class MangaCanvasTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cleanup() {
        fluent::FluentElement::setTheme(fluent::FluentElement::Light);
    }

    void canvasBackgroundFollowsTheme() {
        fluent::FluentElement::setTheme(fluent::FluentElement::Light);
        MangaCanvas canvas;

        QCOMPARE(canvas.backgroundBrush().color(), QColor(QStringLiteral("#F3F3F3")));

        fluent::FluentElement::setTheme(fluent::FluentElement::Dark);
        QCOMPARE(canvas.backgroundBrush().color(), QColor(QStringLiteral("#202124")));
    }

    void zOrderConstantsAreStrictlyOrdered() {
        static_assert(manga::ui::canvas::kZBackground < manga::ui::canvas::kZPageOriginal);
        static_assert(manga::ui::canvas::kZPageOriginal < manga::ui::canvas::kZCleaningLayer);
        static_assert(manga::ui::canvas::kZCleaningLayer < manga::ui::canvas::kZTextLayer);
        static_assert(manga::ui::canvas::kZTextLayer < manga::ui::canvas::kZRegionNormal);
        static_assert(manga::ui::canvas::kZRegionNormal < manga::ui::canvas::kZSelectionHandles);
        static_assert(manga::ui::canvas::kZSelectionHandles < manga::ui::canvas::kZToolCreation);
        QVERIFY(true);
    }

    void initialStateIsNoProject() {
        MangaCanvas canvas;
        QCOMPARE(canvas.contentState(), MangaCanvas::ContentState::NoProject);
    }

    void setPageImageSwitchesToImageState() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        QCOMPARE(canvas.contentState(), MangaCanvas::ContentState::Image);
    }

    void zoomIsClampedToContractRange() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setZoomFactor(0.001);
        QCOMPARE(canvas.zoomFactor(), 0.05);
        canvas.setZoomFactor(100.0);
        QCOMPARE(canvas.zoomFactor(), 16.0);
    }

    void zoomStepsRoundToNearestDiscreteLevel() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setZoomFactor(1.0);
        canvas.zoomIn();
        QCOMPARE(canvas.zoomFactor(), 1.5);
        canvas.setZoomFactor(1.2);
        canvas.zoomIn();
        QCOMPARE(canvas.zoomFactor(), 1.5);
        canvas.setZoomFactor(1.2);
        canvas.zoomOut();
        QCOMPARE(canvas.zoomFactor(), 1.0);
    }

    void placeholderActionIsEmittedFromErrorState() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        canvas.setContentState(
            MangaCanvas::ContentState::Error, QStringLiteral("无法解码页面图像"), QStringLiteral("重试加载"));
        QSignalSpy spy(&canvas, &MangaCanvas::placeholderActionActivated);
        QTest::mouseClick(canvas.placeholderActionButtonForTest(), Qt::LeftButton);
        QCOMPARE(spy.size(), 1);
    }

    void loadingStateClearsPreviousPageImage() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setContentState(MangaCanvas::ContentState::Loading, QStringLiteral("page_0002.png"));
        QCOMPARE(canvas.contentState(), MangaCanvas::ContentState::Loading);
        QVERIFY(!canvas.hasPageImageForTest());
    }

    void fitToPageNeverFallsBelowMinimumZoom() {
        MangaCanvas canvas;
        canvas.resize(120, 90);
        canvas.show();
        QImage image(12000, 18000, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.fitToPage();
        QVERIFY(canvas.zoomFactor() >= 0.05);
    }

    void viewTransformRoundTripsThroughZoomAndCenter() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setZoomFactor(2.0);
        QCoreApplication::processEvents();
        canvas.centerOnPage(QPointF(600.0, 900.0));
        QCoreApplication::processEvents();
        const QPointF center = canvas.viewCenterOnPage();
        QVERIFY(qAbs(center.x() - 600.0) < 1.0);
        QVERIFY(qAbs(center.y() - 900.0) < 1.0);
    }

    // Tool state machine.

    void defaultToolIsPan() {
        MangaCanvas canvas;
        QCOMPARE(canvas.tool(), Tool::Pan);
    }

    void setToolSelectSwitchesToNoDrag() {
        MangaCanvas canvas;
        canvas.setTool(Tool::Select);
        QCOMPARE(canvas.dragMode(), QGraphicsView::NoDrag);
    }

    void setToolPanRestoresScrollHandDrag() {
        MangaCanvas canvas;
        canvas.setTool(Tool::Select);
        canvas.setTool(Tool::Pan);
        QCOMPARE(canvas.dragMode(), QGraphicsView::ScrollHandDrag);
    }

    void setToolCreateRegionSwitchesToNoDrag() {
        MangaCanvas canvas;
        canvas.setTool(Tool::CreateRegion);
        QCOMPARE(canvas.dragMode(), QGraphicsView::NoDrag);
    }

    // Region reconciliation.

    void setRegionsCreatesOverlayItems() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);

        const RegionId id1 = RegionId::generate();
        const RegionId id2 = RegionId::generate();
        QVector<RegionDisplay> regions = {{id1, {10, 10, 50, 50}, false}, {id2, {100, 100, 80, 80}, true}};
        canvas.setRegions(regions);
        QCOMPARE(canvas.regionOverlayCountForTest(), 2);
    }

    void setRegionsRemovesStaleOverlays() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);

        const RegionId id1 = RegionId::generate();
        canvas.setRegions({{id1, {10, 10, 50, 50}, false}});
        QCOMPARE(canvas.regionOverlayCountForTest(), 1);
        canvas.setRegions({});
        QCOMPARE(canvas.regionOverlayCountForTest(), 0);
    }

    void setRegionsUpdatesExistingOverlays() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);

        const RegionId id1 = RegionId::generate();
        canvas.setRegions({{id1, {10, 10, 50, 50}, false}});
        canvas.setRegions({{id1, {20, 20, 60, 60}, true}});
        QCOMPARE(canvas.regionOverlayCountForTest(), 1);
    }

    void regionOutlineContrastsDarkAndLightArtwork() {
        QGraphicsScene scene;
        scene.setSceneRect(0, 0, 64, 64);
        auto* overlay = new manga::ui::manga::RegionOverlayItem(RegionId::generate());
        overlay->setTransientGeometry({12, 12, 40, 40});
        overlay->setColors(
            QColor(QStringLiteral("#60CDFF")), QColor(QStringLiteral("#393939")), QColor(QStringLiteral("#60CDFF")));
        scene.addItem(overlay);

        const auto edgeContrast = [&scene](const QColor& background) {
            QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
            image.fill(background);
            QPainter painter(&image);
            scene.render(&painter, QRectF(0, 0, 64, 64), scene.sceneRect());
            painter.end();

            int maximumDifference = 0;
            for (int x = 10; x <= 14; ++x) {
                for (int y = 28; y <= 36; ++y) {
                    const QColor pixel = image.pixelColor(x, y);
                    maximumDifference = qMax(
                        maximumDifference,
                        qMax(qAbs(pixel.red() - background.red()),
                             qMax(qAbs(pixel.green() - background.green()), qAbs(pixel.blue() - background.blue()))));
                }
            }
            return maximumDifference;
        };

        QVERIFY(edgeContrast(QColor(QStringLiteral("#202124"))) >= 96);
        QVERIFY(edgeContrast(Qt::white) >= 96);
    }

    // Create region gesture.

    void createGestureCommitsValidRect() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setPageBounds(ImageSize{400, 400});
        canvas.setTool(Tool::CreateRegion);
        QCoreApplication::processEvents();

        QSignalSpy spy(&canvas, &MangaCanvas::regionCreateCommitted);

        // Press at (100, 100), drag to (200, 200), release.
        // Use viewport coordinates. The canvas maps these to scene via its transform.
        QWidget* vp = canvas.viewport();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &press);
        QCoreApplication::processEvents();

        // Move beyond threshold.
        QMouseEvent move(QEvent::MouseMove, QPointF(200, 200), Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &move);
        QCoreApplication::processEvents();

        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(200, 200), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &release);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 1);
        const auto geom = spy.at(0).at(0).value<RectGeometry>();
        QVERIFY(geom.width > 0);
        QVERIFY(geom.height > 0);
    }

    void belowMinCreateDiscardsSilently() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setPageBounds(ImageSize{400, 400});
        canvas.setTool(Tool::CreateRegion);
        QCoreApplication::processEvents();

        QSignalSpy spy(&canvas, &MangaCanvas::regionCreateCommitted);

        // Tiny drag that stays below minimum size.
        QWidget* vp = canvas.viewport();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &press);

        // Move just a tiny bit (below min region dimension in source pixels at zoom 1.0).
        QMouseEvent move(QEvent::MouseMove, QPointF(103, 103), Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &move);

        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(103, 103), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &release);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 0);
    }

    // Selection.

    void clickRegionEmitsSelectionRequest() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setTool(Tool::Select);
        QCoreApplication::processEvents();

        const RegionId id1 = RegionId::generate();
        canvas.setRegions({{id1, {50, 50, 100, 100}, false}});
        QCoreApplication::processEvents();

        QSignalSpy spy(&canvas, &MangaCanvas::regionSelectionRequested);

        // Click inside the region. Scene coords map through the current transform.
        // At zoom 1.0 and centered, we need to find the viewport position of scene (100, 100).
        const QPoint vpPos = canvas.mapFromScene(QPointF(100, 100));
        QWidget* vp = canvas.viewport();
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &release);
        QCoreApplication::processEvents();

        QVERIFY(spy.size() >= 1);
        const auto requestedId = spy.at(0).at(0).value<RegionId>();
        QCOMPARE(requestedId, id1);
    }

    // Escape cancels gesture.

    void escapeCancelsGestureAndEmitsCancelled() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setPageBounds(ImageSize{400, 400});
        canvas.setTool(Tool::CreateRegion);
        QCoreApplication::processEvents();

        QSignalSpy cancelSpy(&canvas, &MangaCanvas::gestureCancelled);
        QSignalSpy createSpy(&canvas, &MangaCanvas::regionCreateCommitted);

        QWidget* vp = canvas.viewport();
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(100, 100), Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &press);

        QKeyEvent escPress(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QApplication::sendEvent(vp, &escPress);
        QCoreApplication::processEvents();

        QCOMPARE(cancelSpy.size(), 1);
        QCOMPARE(createSpy.size(), 0);
    }

    // Sub-threshold drag commits nothing.

    void subThresholdDragCommitsNothing() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setPageBounds(ImageSize{400, 400});

        const RegionId id1 = RegionId::generate();
        canvas.setRegions({{id1, {50, 50, 100, 100}, true}});
        canvas.setTool(Tool::Select);
        QCoreApplication::processEvents();

        QSignalSpy geometrySpy(&canvas, &MangaCanvas::regionGeometryCommitted);

        // Click and release without moving beyond threshold.
        const QPoint vpPos = canvas.mapFromScene(QPointF(100, 100));
        QWidget* vp = canvas.viewport();
        QMouseEvent press(QEvent::MouseButtonPress, vpPos, Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &press);
        QMouseEvent release(QEvent::MouseButtonRelease, vpPos, Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &release);
        QCoreApplication::processEvents();

        QCOMPARE(geometrySpy.size(), 0);
    }

    // Move drag commits exactly once with EditKind Move.

    void moveDragCommitsOnce() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setPageBounds(ImageSize{400, 400});
        canvas.setTool(Tool::Select);

        const RegionId id1 = RegionId::generate();
        canvas.setRegions({{id1, {50, 50, 100, 100}, true}});
        QCoreApplication::processEvents();

        QSignalSpy spy(&canvas, &MangaCanvas::regionGeometryCommitted);

        const QPoint startVp = canvas.mapFromScene(QPointF(100, 100));
        QWidget* vp = canvas.viewport();

        QMouseEvent press(QEvent::MouseButtonPress, startVp, Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &press);

        const QPoint endVp = canvas.mapFromScene(QPointF(150, 150));
        QMouseEvent move(QEvent::MouseMove, endVp, Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &move);
        QCoreApplication::processEvents();

        QMouseEvent release(QEvent::MouseButtonRelease, endVp, Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &release);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 1);
        const auto committedId = spy.at(0).at(0).value<RegionId>();
        QCOMPARE(committedId, id1);
        const auto kind = spy.at(0).at(2).value<EditKind>();
        QCOMPARE(kind, EditKind::Move);
        const auto geom = spy.at(0).at(1).value<RectGeometry>();
        QVERIFY(geom.x >= 0);
        QVERIFY(geom.y >= 0);
    }

    // Resize drag commits exactly once with EditKind Resize.

    void resizeDragCommitsOnce() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setPageBounds(ImageSize{400, 400});
        canvas.setTool(Tool::Select);

        const RegionId id1 = RegionId::generate();
        const RectGeometry regionGeom{50, 50, 100, 100};
        canvas.setRegions({{id1, regionGeom, true}});
        QCoreApplication::processEvents();

        QSignalSpy spy(&canvas, &MangaCanvas::regionGeometryCommitted);

        // Compute the SE handle center in device pixels and use it directly as the
        // mouse press position so the hit test deterministically finds the handle.
        const double zoom = canvas.zoomFactor();
        const double dpr = canvas.viewport()->devicePixelRatio();
        const QRectF hitRect =
            manga::ui::canvas::handleHitRect(manga::ui::canvas::HandlePosition::SouthEast, regionGeom, zoom, dpr);
        const QPointF pressPos = hitRect.center();

        QWidget* vp = canvas.viewport();

        QMouseEvent press(QEvent::MouseButtonPress, pressPos, Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &press);

        // Drag further out to resize.
        const QPointF endPos = pressPos + QPointF(30, 30);
        QMouseEvent move(QEvent::MouseMove, endPos, Qt::NoButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &move);
        QCoreApplication::processEvents();

        QMouseEvent release(QEvent::MouseButtonRelease, endPos, Qt::LeftButton, Qt::LeftButton, {});
        QApplication::sendEvent(vp, &release);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 1);
        const auto kind = spy.at(0).at(2).value<EditKind>();
        QCOMPARE(kind, EditKind::Resize);
    }

    // Delete key emits regionDeleteRequested for selected ids only.

    void deleteKeyEmitsForSelectedOnly() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setTool(Tool::Select);

        const RegionId id1 = RegionId::generate();
        const RegionId id2 = RegionId::generate();
        const RegionId id3 = RegionId::generate();
        canvas.setRegions(
            {{id1, {10, 10, 50, 50}, false}, {id2, {70, 70, 50, 50}, true}, {id3, {130, 130, 50, 50}, false}});
        QCoreApplication::processEvents();

        QSignalSpy spy(&canvas, &MangaCanvas::regionDeleteRequested);

        QWidget* vp = canvas.viewport();
        QKeyEvent delPress(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
        QApplication::sendEvent(vp, &delPress);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 1);
        const auto deletedId = spy.at(0).at(0).value<RegionId>();
        QCOMPARE(deletedId, id2);
    }

    // Tab/Shift+Tab cycles selection through regions in order.

    void tabCyclesSelectionThroughRegions() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setTool(Tool::Select);

        // The canvas stores overlays in a QMap keyed by RegionId, so the Tab cycle follows
        // RegionId sort order rather than insertion order. Sort the generated ids to make
        // the expected cycle deterministic.
        QList<RegionId> ids{RegionId::generate(), RegionId::generate(), RegionId::generate()};
        std::sort(ids.begin(), ids.end());
        const RegionId id1 = ids[0];
        const RegionId id2 = ids[1];
        const RegionId id3 = ids[2];
        canvas.setRegions(
            {{id1, {10, 10, 50, 50}, true}, {id2, {70, 70, 50, 50}, false}, {id3, {130, 130, 50, 50}, false}});
        QCoreApplication::processEvents();

        QSignalSpy spy(&canvas, &MangaCanvas::regionSelectionRequested);

        QWidget* vp = canvas.viewport();

        // Tab from id1 should select id2.
        QKeyEvent tabPress(QEvent::KeyPress, Qt::Key_Tab, Qt::NoModifier);
        QApplication::sendEvent(vp, &tabPress);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 1);
        QCOMPARE(spy.at(0).at(0).value<RegionId>(), id2);

        // Tab again should select id3.
        QApplication::sendEvent(vp, &tabPress);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 2);
        QCOMPARE(spy.at(1).at(0).value<RegionId>(), id3);

        // Tab again should wrap to id1.
        QApplication::sendEvent(vp, &tabPress);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 3);
        QCOMPARE(spy.at(2).at(0).value<RegionId>(), id1);

        // Shift+Tab from id1 should wrap back to id3.
        QKeyEvent backtabPress(QEvent::KeyPress, Qt::Key_Backtab, Qt::ShiftModifier);
        QApplication::sendEvent(vp, &backtabPress);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 4);
        QCOMPARE(spy.at(3).at(0).value<RegionId>(), id3);
    }

    // Shift+Arrow adjusts primary handle by 1 source px and commits Resize.

    void shiftArrowResizesByOneSourcePx() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QCoreApplication::processEvents();
        QImage image(400, 400, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);
        canvas.setPageBounds(ImageSize{400, 400});
        canvas.setTool(Tool::Select);

        const RegionId id1 = RegionId::generate();
        canvas.setRegions({{id1, {50, 50, 100, 100}, true}});
        QCoreApplication::processEvents();

        QSignalSpy spy(&canvas, &MangaCanvas::regionGeometryCommitted);

        QWidget* vp = canvas.viewport();

        // Shift+Right should expand east edge by 1 source px.
        QKeyEvent shiftRight(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier);
        QApplication::sendEvent(vp, &shiftRight);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 1);
        const auto kind = spy.at(0).at(2).value<EditKind>();
        QCOMPARE(kind, EditKind::Resize);
        const auto geom = spy.at(0).at(1).value<RectGeometry>();
        QCOMPARE(geom.width, 101.0);
        QCOMPARE(geom.height, 100.0);

        // Shift+Down should expand south edge by 1 source px.
        QKeyEvent shiftDown(QEvent::KeyPress, Qt::Key_Down, Qt::ShiftModifier);
        QApplication::sendEvent(vp, &shiftDown);
        QCoreApplication::processEvents();

        QCOMPARE(spy.size(), 2);
        const auto geom2 = spy.at(1).at(1).value<RectGeometry>();
        QCOMPARE(geom2.height, 101.0);
    }
};

QTEST_MAIN(MangaCanvasTest)
#include "manga_canvas_test.moc"
