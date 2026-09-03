#include "StrokePaintRasterizerTest.h"

#include "rendering/paint/StrokePaintRasterizer.h"

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QtTest>

using manga::rendering::StrokePaintMode;
using manga::rendering::StrokePaintParams;
using manga::rendering::StrokePaintRasterizer;

namespace {

QImage transparentImage(const QSize& size) {
    QImage image(size, QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    return image;
}

QRgb alphaAt(const QImage& image, int x, int y) {
    return image.pixelColor(x, y).alpha();
}

} // namespace

void StrokePaintRasterizerTest::invalidInputLeavesImageUntouched() {
    QImage image = transparentImage(QSize(32, 32));
    const QImage before = image;
    const StrokePaintParams params{StrokePaintMode::Paint, QColor(255, 0, 0, 128), 8.0};

    QVERIFY(StrokePaintRasterizer::rasterizeStroke(image, params, {}).isEmpty());
    QCOMPARE(image, before);

    const StrokePaintParams zero{StrokePaintMode::Paint, QColor(255, 0, 0, 128), 0.0};
    QVERIFY(StrokePaintRasterizer::rasterizeStroke(image, zero, {QPointF(4.0, 4.0)}).isEmpty());
    QCOMPARE(image, before);

    QImage nullImage;
    StrokePaintRasterizer::rasterizeStroke(nullImage, params, {QPointF(4.0, 4.0)});
    QVERIFY(nullImage.isNull());
}

void StrokePaintRasterizerTest::singlePointPaintsRoundCap() {
    QImage image = transparentImage(QSize(32, 32));
    const StrokePaintParams params{StrokePaintMode::Paint, QColor(255, 0, 0, 255), 8.0};

    const QRect dirty = StrokePaintRasterizer::rasterizeStroke(image, params, {QPointF(16.5, 16.5)});

    QCOMPARE(alphaAt(image, 16, 16), 255);
    QCOMPARE(image.pixelColor(16, 16), QColor(255, 0, 0, 255));
    // radius 4: coverage reaches zero at distance 4.5, so the disc spans 12..20.
    QVERIFY(dirty.contains(QPoint(16, 16)));
    QVERIFY(dirty.width() <= 10);
    QVERIFY(dirty.height() <= 10);
}

void StrokePaintRasterizerTest::paintCompositesStraightAlphaOverDestination() {
    QImage image(QSize(32, 32), QImage::Format_RGBA8888);
    image.fill(QColor(0, 0, 255, 200));
    const StrokePaintParams params{StrokePaintMode::Paint, QColor(255, 0, 0, 128), 8.0};

    StrokePaintRasterizer::rasterizeStroke(image, params, {QPointF(16.5, 16.5)});

    // srcA = 128/255 at the centre; outA = srcA + dstA*(1-srcA) with dstA = 200/255.
    // centre -> srcA 0.50196, dstA 0.78431 -> outA 0.89362 -> alpha 228
    QCOMPARE(alphaAt(image, 16, 16), 228);
    // distance 4.0 -> coverage 0.5 -> srcA 0.25098 -> outA 0.83922 -> alpha 214
    QCOMPARE(alphaAt(image, 20, 16), 214);
    // outside radius+0.5 the pixel keeps the destination
    QCOMPARE(alphaAt(image, 21, 16), 200);
}

void StrokePaintRasterizerTest::eraseScalesDestinationAlpha() {
    QImage image(QSize(32, 32), QImage::Format_RGBA8888);
    image.fill(QColor(0, 0, 255, 200));
    const StrokePaintParams params{StrokePaintMode::Erase, QColor(0, 0, 0, 255), 8.0};

    StrokePaintRasterizer::rasterizeStroke(image, params, {QPointF(16.5, 16.5)});

    QCOMPARE(alphaAt(image, 16, 16), 0);   // coverage 1 -> alpha * 0
    QCOMPARE(alphaAt(image, 20, 16), 100); // coverage 0.5 -> alpha * 0.5
    QCOMPARE(alphaAt(image, 21, 16), 200); // untouched
}

void StrokePaintRasterizerTest::strokeIsClippedToImageBounds() {
    QImage image = transparentImage(QSize(24, 24));
    const StrokePaintParams params{StrokePaintMode::Paint, QColor(255, 0, 0, 255), 10.0};

    const QRect dirty = StrokePaintRasterizer::rasterizeStroke(
        image, params, {QPointF(-50.0, -50.0), QPointF(0.0, 0.0), QPointF(200.0, 200.0)});

    QVERIFY(dirty.isEmpty() || QRect(0, 0, 24, 24).contains(dirty));
    QCOMPARE(alphaAt(image, 0, 0), 255);
    QCOMPARE(alphaAt(image, 23, 23), 255);
}

void StrokePaintRasterizerTest::incrementalSegmentsMatchWholeStrokeExactly() {
    const QVector<QPointF> points{
        QPointF(6.5, 6.5), QPointF(40.5, 18.25), QPointF(70.0, 60.0), QPointF(120.5, 40.5), QPointF(160.25, 120.75)};
    const StrokePaintParams paint{StrokePaintMode::Paint, QColor(20, 200, 90, 160), 13.0};
    const StrokePaintParams erase{StrokePaintMode::Erase, QColor(0, 0, 0, 255), 7.5};

    for (const auto& params : {paint, erase}) {
        QImage whole(QSize(200, 200), QImage::Format_RGBA8888);
        whole.fill(QColor(90, 40, 10, 220));
        StrokePaintRasterizer::rasterizeStroke(whole, params, points);

        QImage incremental = whole;
        // Rebuild from the same baseline, feeding one segment per pointer move.
        QImage draft(QSize(200, 200), QImage::Format_RGBA8888);
        draft.fill(QColor(90, 40, 10, 220));
        for (qsizetype i = 0; i + 1 < points.size(); ++i) {
            StrokePaintRasterizer::rasterizeSegment(draft, params, points[i], points[i + 1]);
        }
        StrokePaintRasterizer::rasterizeCap(draft, params, points.last());

        QVERIFY(draft == whole);
    }
}

void StrokePaintRasterizerTest::wholeStrokeReturnsUnionOfTouchedPixels() {
    QImage image = transparentImage(QSize(200, 200));
    const StrokePaintParams params{StrokePaintMode::Paint, QColor(255, 0, 0, 255), 10.0};
    const QVector<QPointF> points{QPointF(20.5, 20.5), QPointF(120.5, 20.5)};

    const QRect dirty = StrokePaintRasterizer::rasterizeStroke(image, params, points);

    QVERIFY(dirty.contains(QRect(15, 15, 111, 11)));
    // Nothing outside the returned rect may have changed.
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (!dirty.contains(QPoint(x, y))) {
                QCOMPARE(alphaAt(image, x, y), 0);
            }
        }
    }
}
