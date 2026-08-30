#include "ui/manga/CanvasZOrder.h"
#include "ui/manga/MangaCanvas.h"

#include <QtTest>

using manga::ui::manga::MangaCanvas;

class MangaCanvasTest : public QObject {
    Q_OBJECT
private slots:
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
        canvas.setZoomFactor(1.2); // 两档之间
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
        canvas.resize(120, 90); // 极小视口
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
};

QTEST_MAIN(MangaCanvasTest)
#include "manga_canvas_test.moc"
