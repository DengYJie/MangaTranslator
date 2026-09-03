#include "ui/manga/MangaCanvas.h"

#include "rendering/paint/StrokePaintRasterizer.h"
#include "ui/manga/CanvasZOrder.h"
#include "ui/manga/RegionInteraction.h"
#include "ui/manga/RegionOverlayItem.h"

#include <QApplication>
#include <QColorSpace>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <QScrollBar>
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
        static_assert(manga::ui::canvas::kZPageOriginal < manga::ui::canvas::kZClearLayer);
        static_assert(manga::ui::canvas::kZClearLayer < manga::ui::canvas::kZManualOverrideLayer);
        static_assert(manga::ui::canvas::kZManualOverrideLayer < manga::ui::canvas::kZRegionNormal);
        static_assert(manga::ui::canvas::kZRegionNormal < manga::ui::canvas::kZSelectionHandles);
        static_assert(manga::ui::canvas::kZSelectionHandles < manga::ui::canvas::kZToolCreation);
        static_assert(manga::ui::canvas::kZToolCreation < manga::ui::canvas::kZTextLayer);
        // TODO(Defect 10): Replace QVERIFY(true) placeholder with actual verifiable assertions.
        QVERIFY(true);
    }

    void splitLayersCompositeIdenticallyToMergedCleaningImage() {
        const QSize size(96, 96);

        QImage clearLayer(size, QImage::Format_RGBA8888);
        clearLayer.fill(Qt::transparent);
        {
            QPainter painter(&clearLayer);
            painter.fillRect(QRect(8, 8, 40, 60), QColor(250, 250, 250, 255));
            QRadialGradient blob(QPointF(64, 32), 20.0);
            blob.setColorAt(0.0, QColor(255, 255, 255, 255));
            blob.setColorAt(1.0, QColor(255, 255, 255, 0));
            painter.fillRect(QRect(44, 12, 40, 40), blob);
        }

        QImage overrideLayer(size, QImage::Format_RGBA8888);
        overrideLayer.fill(Qt::transparent);
        {
            QPainter painter(&overrideLayer);
            painter.setPen(QPen(QColor(17, 17, 17, 160), 6.0, Qt::SolidLine, Qt::RoundCap));
            painter.drawLine(QPointF(10, 70), QPointF(86, 20));
            painter.drawLine(QPointF(20, 84), QPointF(70, 60));
        }

        // The merged cleaning image the canvas received before the layer split.
        QImage merged(size, QImage::Format_RGBA8888_Premultiplied);
        merged.setColorSpace(QColorSpace(QColorSpace::SRgb));
        merged.fill(Qt::transparent);
        {
            QPainter painter(&merged);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.drawImage(0, 0, clearLayer);
            painter.drawImage(0, 0, overrideLayer);
        }
        merged = merged.convertToFormat(QImage::Format_RGBA8888);

        // The two layers composited separately must reproduce it pixel for pixel.
        QImage twoPass(size, QImage::Format_RGBA8888_Premultiplied);
        twoPass.setColorSpace(QColorSpace(QColorSpace::SRgb));
        twoPass.fill(Qt::transparent);
        {
            QPainter painter(&twoPass);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.drawImage(0, 0, clearLayer);
            painter.drawImage(0, 0, overrideLayer);
        }
        twoPass = twoPass.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(twoPass, merged);

        // Rendering the two canvas items in z order must equal the merged single item.
        QImage transparentPage(size, QImage::Format_RGBA8888);
        transparentPage.fill(Qt::transparent);

        const auto renderCanvas = [&](MangaCanvas& canvas) {
            canvas.resize(400, 400);
            canvas.show();
            canvas.setPageImage(transparentPage);
            QImage sceneRender(size, QImage::Format_RGBA8888);
            sceneRender.fill(Qt::transparent);
            {
                QPainter painter(&sceneRender);
                canvas.scene()->render(
                    &painter, QRectF(0, 0, size.width(), size.height()), QRectF(0, 0, size.width(), size.height()));
            }
            return sceneRender;
        };

        MangaCanvas mergedCanvas;
        mergedCanvas.setClearImage(merged);
        const QImage mergedRender = renderCanvas(mergedCanvas);

        MangaCanvas splitCanvas;
        splitCanvas.setClearImage(clearLayer);
        splitCanvas.setManualOverrideImage(overrideLayer, QStringLiteral("ref-a"));
        const QImage splitRender = renderCanvas(splitCanvas);

        QCOMPARE(splitRender, mergedRender);
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

    void removeDiameterClampsAndNotifies() {
        MangaCanvas canvas;
        QSignalSpy spy(&canvas, &MangaCanvas::removeDiameterChanged);

        QCOMPARE(canvas.removeDiameter(), 48.0);
        canvas.setRemoveDiameter(200.0);
        QCOMPARE(canvas.removeDiameter(), 128.0);
        QCOMPARE(spy.size(), 1);

        canvas.setRemoveDiameter(-1.0);
        QCOMPARE(canvas.removeDiameter(), 1.0);
        QCOMPARE(spy.size(), 2);
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

        // Map the SE anchor (150, 150) from scene to viewport so the press hits the handle.
        const QPointF pressPos =
            canvas.mapFromScene(QPointF(regionGeom.x + regionGeom.width, regionGeom.y + regionGeom.height));

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

    void anchorScenePointOnViewportMatchesCenteringWhenTargetIsCenter() {
        MangaCanvas canvas;
        canvas.resize(800, 600);
        canvas.show();
        QImage image(1200, 1800, QImage::Format_RGBA8888);
        image.fill(Qt::white);
        canvas.setPageImage(image);

        const QPointF scenePoint(600.0, 900.0);
        canvas.centerScenePointOnViewport(scenePoint);
        const int hCenter = canvas.horizontalScrollBar()->value();
        const int vCenter = canvas.verticalScrollBar()->value();

        canvas.anchorScenePointOnViewport(scenePoint, canvas.viewport()->rect().center());
        QCOMPARE(canvas.horizontalScrollBar()->value(), hCenter);
        QCOMPARE(canvas.verticalScrollBar()->value(), vCenter);

        canvas.anchorScenePointOnViewport(scenePoint, canvas.viewport()->rect().center() + QPointF(50.0, 0.0));
        QCOMPARE(canvas.horizontalScrollBar()->value(), hCenter - 50);
    }

    void draftDoesNotTouchTheClearLayer() {
        MangaCanvas canvas;
        canvas.setPageBounds(ImageSize{64, 64});

        QImage clearImage(64, 64, QImage::Format_RGBA8888);
        clearImage.fill(QColor(255, 255, 255, 255));
        {
            QPainter painter(&clearImage);
            painter.fillRect(QRect(8, 8, 32, 48), QColor(220, 30, 30, 255));
        }
        canvas.setClearImage(clearImage);

        QImage overrideImage(64, 64, QImage::Format_RGBA8888);
        overrideImage.fill(Qt::transparent);
        const QRect strokeDirty = manga::rendering::StrokePaintRasterizer::rasterizeStroke(
            overrideImage,
            {manga::rendering::StrokePaintMode::Paint, QColor(20, 40, 220, 255), 8.0},
            {QPointF(16.5, 32.5), QPointF(48.5, 32.5)});
        QVERIFY(!strokeDirty.isEmpty());
        canvas.setManualOverrideImage(overrideImage, QStringLiteral("ref-a"));

        canvas.beginStrokeDraft(manga::rendering::StrokePaintMode::Erase, QColor(0, 0, 0, 255), 16.0);
        canvas.extendStrokeDraft(QPointF(32.5, 32.5));

        // The eraser only subtracts brush paint: the clear item is untouched.
        QCOMPARE(canvas.clearImageForTest(), clearImage);
        // The authoritative override layer is untouched mid-gesture.
        QCOMPARE(canvas.authoritativeOverrideForTest(), overrideImage);

        const uchar* bits = canvas.strokeDraftBitsForTest();
        QVERIFY(bits != nullptr);
        QCOMPARE(canvas.strokeDraftSizeForTest(), QSize(64, 64));
        const auto draftAlpha = [bits](int x, int y) { return bits[(y * 64 + x) * 4 + 3]; };
        QCOMPARE(draftAlpha(32, 32), 0);   // inside the erased band
        QCOMPARE(draftAlpha(16, 32), 255); // brush paint outside the eraser survives
    }

    void eraseScalesExistingBrushAlpha() {
        MangaCanvas canvas;
        canvas.setPageBounds(ImageSize{64, 64});
        QImage overrideImage(64, 64, QImage::Format_RGBA8888);
        overrideImage.fill(QColor(0, 0, 255, 200));
        canvas.setManualOverrideImage(overrideImage, QStringLiteral("ref-a"));

        canvas.beginStrokeDraft(manga::rendering::StrokePaintMode::Erase, QColor(0, 0, 0, 255), 8.0);
        canvas.extendStrokeDraft(QPointF(16.5, 16.5));

        const uchar* bits = canvas.strokeDraftBitsForTest();
        QVERIFY(bits != nullptr);
        const auto draftAlpha = [bits](int x, int y) { return bits[(y * 64 + x) * 4 + 3]; };
        QCOMPARE(draftAlpha(16, 16), 0);   // full coverage removes the stroke alpha
        QCOMPARE(draftAlpha(20, 16), 100); // half coverage scales it by one half
        QCOMPARE(draftAlpha(21, 16), 200); // outside the radius the layer is untouched
    }

    void draftBufferIsNotCopiedPerSegment() {
        MangaCanvas canvas;
        canvas.setPageBounds(ImageSize{64, 64});
        canvas.beginStrokeDraft(manga::rendering::StrokePaintMode::Paint, QColor(17, 17, 17, 255), 8.0);
        canvas.extendStrokeDraft(QPointF(8.5, 8.5));
        const uchar* first = canvas.strokeDraftBitsForTest();
        canvas.extendStrokeDraft(QPointF(24.5, 24.5));
        const uchar* second = canvas.strokeDraftBitsForTest();
        QVERIFY(first != nullptr);
        QCOMPARE(second, first);
    }

    void settleRestoresAuthoritativeLayer() {
        MangaCanvas canvas;
        canvas.setPageBounds(ImageSize{64, 64});
        QImage overrideImage(64, 64, QImage::Format_RGBA8888);
        overrideImage.fill(QColor(0, 0, 255, 200));
        canvas.setManualOverrideImage(overrideImage, QStringLiteral("ref-a"));

        canvas.beginStrokeDraft(manga::rendering::StrokePaintMode::Erase, QColor(0, 0, 0, 255), 8.0);
        canvas.extendStrokeDraft(QPointF(16.5, 16.5));
        QVERIFY(canvas.hasStrokeDraftForTest());

        canvas.settleStrokeDraft();
        QVERIFY(!canvas.hasStrokeDraftForTest());
        QCOMPARE(canvas.authoritativeOverrideForTest(), overrideImage);
    }

    void finishKeepsDraftVisible() {
        MangaCanvas canvas;
        canvas.setPageBounds(ImageSize{64, 64});
        canvas.beginStrokeDraft(manga::rendering::StrokePaintMode::Paint, QColor(255, 0, 0, 255), 8.0);
        canvas.extendStrokeDraft(QPointF(16.5, 16.5));
        canvas.finishStrokeDraft();
        QVERIFY(canvas.hasStrokeDraftForTest());
        const uchar* bits = canvas.strokeDraftBitsForTest();
        QVERIFY(bits != nullptr);
        QCOMPARE(static_cast<int>(bits[(16 * 64 + 16) * 4 + 3]), 255);
    }

    void pageSwitchSettlesDraft() {
        MangaCanvas canvas;
        canvas.setPageBounds(ImageSize{64, 64});
        canvas.beginStrokeDraft(manga::rendering::StrokePaintMode::Paint, QColor(255, 0, 0, 255), 8.0);
        canvas.extendStrokeDraft(QPointF(16.5, 16.5));
        QVERIFY(canvas.hasStrokeDraftForTest());

        QImage nextPage(64, 64, QImage::Format_RGBA8888);
        nextPage.fill(Qt::white);
        canvas.setPageImage(nextPage);
        QVERIFY(!canvas.hasStrokeDraftForTest());
    }
};

QTEST_MAIN(MangaCanvasTest)
#include "MangaCanvasTest.moc"
