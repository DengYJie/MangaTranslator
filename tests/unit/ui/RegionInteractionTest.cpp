#include "ui/manga/RegionInteraction.h"

#include <QtTest>

using namespace manga::ui::canvas;
using manga::domain::ImageSize;
using manga::domain::RectGeometry;

class RegionInteractionTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    // Coordinate conversion round trips.

    void sourceToViewAndBack() {
        const QPointF source(100.0, 200.0);
        const double zoom = 2.5;
        const QPointF view = sourceToView(source, zoom);
        QCOMPARE(view.x(), 250.0);
        QCOMPARE(view.y(), 500.0);
        const QPointF back = viewToSource(view, zoom);
        QVERIFY(qAbs(back.x() - source.x()) < 1e-9);
        QVERIFY(qAbs(back.y() - source.y()) < 1e-9);
    }

    void viewToDeviceAndBack() {
        const QPointF view(300.0, 400.0);
        const double dpr = 2.0;
        const QPointF device = viewToDevice(view, dpr);
        QCOMPARE(device.x(), 600.0);
        QCOMPARE(device.y(), 800.0);
        const QPointF back = deviceToView(device, dpr);
        QVERIFY(qAbs(back.x() - view.x()) < 1e-9);
        QVERIFY(qAbs(back.y() - view.y()) < 1e-9);
    }

    void fullRoundTripSourceToViewToDevice() {
        const QPointF source(50.0, 75.0);
        const double zoom = 1.5;
        const double dpr = 1.25;
        const QPointF view = sourceToView(source, zoom);
        const QPointF device = viewToDevice(view, dpr);
        const QPointF viewBack = deviceToView(device, dpr);
        const QPointF sourceBack = viewToSource(viewBack, zoom);
        QVERIFY(qAbs(sourceBack.x() - source.x()) < 1e-9);
        QVERIFY(qAbs(sourceBack.y() - source.y()) < 1e-9);
    }

    void zeroZoomReturnsZero() {
        const QPointF result = viewToSource(QPointF(100.0, 200.0), 0.0);
        QCOMPARE(result, QPointF(0.0, 0.0));
    }

    void zeroDprReturnsZero() {
        const QPointF result = deviceToView(QPointF(100.0, 200.0), 0.0);
        QCOMPARE(result, QPointF(0.0, 0.0));
    }

    // Handle hit rect tests at various zoom levels.

    void handleHitRectAt100Percent() {
        const RectGeometry geom{100.0, 100.0, 200.0, 300.0};
        const double zoom = 1.0;
        const double dpr = 1.0;
        const QRectF hit = handleHitRect(HandlePosition::NorthWest, geom, zoom, dpr);
        QCOMPARE(hit.width(), kHandleHitSizeDevicePx);
        QCOMPARE(hit.height(), kHandleHitSizeDevicePx);
        QVERIFY(hit.contains(QPointF(100.0, 100.0)));
    }

    void handleHitRectAt5Percent() {
        const RectGeometry geom{100.0, 100.0, 200.0, 300.0};
        const double zoom = 0.05;
        const double dpr = 1.0;
        const QRectF hit = handleHitRect(HandlePosition::SouthEast, geom, zoom, dpr);
        // At 5% zoom the scene point (300, 400) becomes view (15, 20), where the hit rect is centered.
        QVERIFY(hit.contains(QPointF(15.0, 20.0)));
        QCOMPARE(hit.width(), kHandleHitSizeDevicePx);
    }

    void handleHitRectAt1600Percent() {
        const RectGeometry geom{100.0, 100.0, 200.0, 300.0};
        const double zoom = 16.0;
        const double dpr = 2.0;
        const QRectF hit = handleHitRect(HandlePosition::East, geom, zoom, dpr);
        // The East handle at source (300, 250) becomes view (4800, 4000) and device (9600, 8000).
        QVERIFY(hit.contains(QPointF(9600.0, 8000.0)));
        QCOMPARE(hit.width(), kHandleHitSizeDevicePx);
    }

    void handleHitRectCoversMinimumSixteenDevicePixels() {
        const RectGeometry geom{0.0, 0.0, 50.0, 50.0};
        const double zoom = 0.05;
        const double dpr = 1.0;
        for (auto pos : {HandlePosition::NorthWest,
                         HandlePosition::North,
                         HandlePosition::NorthEast,
                         HandlePosition::East,
                         HandlePosition::SouthEast,
                         HandlePosition::South,
                         HandlePosition::SouthWest,
                         HandlePosition::West}) {
            const QRectF hit = handleHitRect(pos, geom, zoom, dpr);
            QVERIFY(hit.width() >= 16.0);
            QVERIFY(hit.height() >= 16.0);
        }
    }

    // Handle hit testing.

    void hitTestReturnsCorrectHandle() {
        const RectGeometry geom{100.0, 100.0, 200.0, 300.0};
        const double zoom = 1.0;
        const double dpr = 1.0;
        // Click exactly on SouthEast anchor (300, 400).
        const auto result = hitTestHandle(QPointF(300.0, 400.0), geom, zoom, dpr);
        QVERIFY(result.has_value());
        QCOMPARE(*result, HandlePosition::SouthEast);
    }

    void hitTestReturnsNulloptForMiss() {
        const RectGeometry geom{100.0, 100.0, 200.0, 300.0};
        const double zoom = 1.0;
        const double dpr = 1.0;
        const auto result = hitTestHandle(QPointF(500.0, 500.0), geom, zoom, dpr);
        QVERIFY(!result.has_value());
    }

    // Move math with clamping.

    void moveClampsToPageBounds() {
        const RectGeometry original{10.0, 10.0, 50.0, 50.0};
        const ImageSize bounds{100, 100};
        const auto result = moveRegion(original, QPointF(-20.0, -20.0), bounds);
        QCOMPARE(result.x, 0.0);
        QCOMPARE(result.y, 0.0);
        QCOMPARE(result.width, 50.0);
        QCOMPARE(result.height, 50.0);
    }

    void moveClampsRightEdge() {
        const RectGeometry original{80.0, 80.0, 50.0, 50.0};
        const ImageSize bounds{100, 100};
        const auto result = moveRegion(original, QPointF(30.0, 30.0), bounds);
        // Right edge would be 160, clamped to 100. So x = 100 - 50 = 50.
        QCOMPARE(result.x, 50.0);
        QCOMPARE(result.y, 50.0);
    }

    void movePreservesDimensions() {
        const RectGeometry original{10.0, 20.0, 30.0, 40.0};
        const ImageSize bounds{200, 200};
        const auto result = moveRegion(original, QPointF(5.0, 10.0), bounds);
        QCOMPARE(result.width, 30.0);
        QCOMPARE(result.height, 40.0);
    }

    // Resize math.

    void resizeFromSouthEastExpands() {
        const RectGeometry original{10.0, 10.0, 50.0, 50.0};
        const ImageSize bounds{200, 200};
        const auto result = resizeRegion(original, HandlePosition::SouthEast, QPointF(10.0, 20.0), bounds);
        QVERIFY(result.has_value());
        QCOMPARE(result->x, 10.0);
        QCOMPARE(result->y, 10.0);
        QCOMPARE(result->width, 60.0);
        QCOMPARE(result->height, 70.0);
    }

    void resizeFromNorthWestShrinks() {
        const RectGeometry original{10.0, 10.0, 50.0, 50.0};
        const ImageSize bounds{200, 200};
        const auto result = resizeRegion(original, HandlePosition::NorthWest, QPointF(10.0, 10.0), bounds);
        QVERIFY(result.has_value());
        QCOMPARE(result->x, 20.0);
        QCOMPARE(result->y, 20.0);
        QCOMPARE(result->width, 40.0);
        QCOMPARE(result->height, 40.0);
    }

    void resizeClampsToPageBounds() {
        const RectGeometry original{10.0, 10.0, 50.0, 50.0};
        const ImageSize bounds{100, 100};
        const auto result = resizeRegion(original, HandlePosition::SouthEast, QPointF(100.0, 100.0), bounds);
        QVERIFY(result.has_value());
        QCOMPARE(result->x, 10.0);
        QCOMPARE(result->y, 10.0);
        QCOMPARE(result->width, 90.0);
        QCOMPARE(result->height, 90.0);
    }

    void resizeBelowMinimumReturnsNullopt() {
        const RectGeometry original{10.0, 10.0, 50.0, 50.0};
        const ImageSize bounds{200, 200};
        // Shrink from SouthEast by more than width minus minimum.
        const auto result = resizeRegion(original, HandlePosition::NorthWest, QPointF(45.0, 45.0), bounds);
        // Resulting size would be 5x5, below kMinRegionDimension (8).
        QVERIFY(!result.has_value());
    }

    void resizeEnforcesEightByEightMinimum() {
        const RectGeometry original{0.0, 0.0, 100.0, 100.0};
        const ImageSize bounds{200, 200};
        // Shrink from NorthWest to exactly 8x8.
        const auto result = resizeRegion(original, HandlePosition::NorthWest, QPointF(92.0, 92.0), bounds);
        QVERIFY(result.has_value());
        QVERIFY(result->width >= kMinRegionDimension);
        QVERIFY(result->height >= kMinRegionDimension);
    }

    void resizeFromNorthOnlyAdjustsTopEdge() {
        const RectGeometry original{10.0, 20.0, 50.0, 60.0};
        const ImageSize bounds{200, 200};
        const auto result = resizeRegion(original, HandlePosition::North, QPointF(0.0, -10.0), bounds);
        QVERIFY(result.has_value());
        QCOMPARE(result->x, 10.0);
        QCOMPARE(result->y, 10.0);
        QCOMPARE(result->width, 50.0);
        QCOMPARE(result->height, 70.0);
    }

    void resizeFromWestOnlyAdjustsLeftEdge() {
        const RectGeometry original{20.0, 10.0, 60.0, 50.0};
        const ImageSize bounds{200, 200};
        const auto result = resizeRegion(original, HandlePosition::West, QPointF(-10.0, 0.0), bounds);
        QVERIFY(result.has_value());
        QCOMPARE(result->x, 10.0);
        QCOMPARE(result->y, 10.0);
        QCOMPARE(result->width, 70.0);
        QCOMPARE(result->height, 50.0);
    }

    // Drag threshold.

    void dragThresholdNotExceededBelowFourPixels() {
        QVERIFY(!exceedsDragThreshold(QPointF(3.0, 0.0)));
        QVERIFY(!exceedsDragThreshold(QPointF(0.0, 3.9)));
    }

    void dragThresholdExceededAtFourPixels() {
        QVERIFY(exceedsDragThreshold(QPointF(4.0, 0.0)));
        QVERIFY(exceedsDragThreshold(QPointF(0.0, 4.0)));
        QVERIFY(exceedsDragThreshold(QPointF(3.0, 3.0))); // sqrt(18) > 4
    }

    // Constants.

    void minimumRegionDimensionIsEight() {
        QCOMPARE(kMinRegionDimension, 8.0);
    }

    void handleVisualSizeIsEightDevicePixels() {
        QCOMPARE(kHandleVisualSizeDevicePx, 8.0);
    }

    void handleHitSizeIsSixteenDevicePixels() {
        QCOMPARE(kHandleHitSizeDevicePx, 16.0);
    }

    void dragThresholdIsFourDevicePixels() {
        QCOMPARE(kDragThresholdDevicePx, 4.0);
    }
};

QTEST_GUILESS_MAIN(RegionInteractionTest)
#include "RegionInteractionTest.moc"
