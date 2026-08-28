#include "GeometryTest.h"

#include "domain/model/Geometry.h"

#include <QtTest/QtTest>

using namespace manga::domain;

void GeometryTest::testRectValidity() {
    // Valid 2x2 minimum size
    const RectGeometry minRect{.x = 0.0, .y = 0.0, .width = 2.0, .height = 2.0};
    QVERIFY(minRect.isValid());

    // Larger valid rect
    const RectGeometry validRect{.x = 10.5, .y = 20.0, .width = 100.0, .height = 50.0};
    QVERIFY(validRect.isValid());

    // Under minimum width (< 2.0)
    const RectGeometry smallW{.x = 0.0, .y = 0.0, .width = 1.99, .height = 10.0};
    QVERIFY(!smallW.isValid());

    // Under minimum height (< 2.0)
    const RectGeometry smallH{.x = 0.0, .y = 0.0, .width = 10.0, .height = 1.99};
    QVERIFY(!smallH.isValid());

    // Negative coordinates
    const RectGeometry negX{.x = -1.0, .y = 0.0, .width = 10.0, .height = 10.0};
    QVERIFY(!negX.isValid());

    const RectGeometry negY{.x = 0.0, .y = -0.1, .width = 10.0, .height = 10.0};
    QVERIFY(!negY.isValid());

    // NaN / Inf
    const RectGeometry nanRect{.x = 0.0, .y = 0.0, .width = std::numeric_limits<double>::quiet_NaN(), .height = 10.0};
    QVERIFY(!nanRect.isValid());
}

void GeometryTest::testRectWithinBounds() {
    const ImageSize imageBounds{.width = 1000, .height = 1500};

    // Exactly touching boundary
    const RectGeometry fullyInside{.x = 0.0, .y = 0.0, .width = 1000.0, .height = 1500.0};
    QVERIFY(fullyInside.isWithinBounds(imageBounds));

    // Internal
    const RectGeometry internal{.x = 100.0, .y = 200.0, .width = 300.0, .height = 400.0};
    QVERIFY(internal.isWithinBounds(imageBounds));

    // Exceeds width
    const RectGeometry overflowW{.x = 800.0, .y = 100.0, .width = 201.0, .height = 100.0};
    QVERIFY(!overflowW.isWithinBounds(imageBounds));

    // Exceeds height
    const RectGeometry overflowH{.x = 100.0, .y = 1400.0, .width = 100.0, .height = 101.0};
    QVERIFY(!overflowH.isWithinBounds(imageBounds));
}

void GeometryTest::testRectClamping() {
    const ImageSize imageBounds{.width = 1000, .height = 1500};

    // Partially overflowing rect
    const RectGeometry overflow{.x = 900.0, .y = 1400.0, .width = 200.0, .height = 200.0};
    const auto clamped = overflow.clampedToBounds(imageBounds);
    QVERIFY(clamped.has_value());
    QCOMPARE(clamped->x, 900.0);
    QCOMPARE(clamped->y, 1400.0);
    QCOMPARE(clamped->width, 100.0);
    QCOMPARE(clamped->height, 100.0);

    // Completely out of bounds (to the right)
    const RectGeometry completelyOut{.x = 1050.0, .y = 100.0, .width = 50.0, .height = 50.0};
    const auto outClamped = completelyOut.clampedToBounds(imageBounds);
    QVERIFY(!outClamped.has_value());

    // Overflowing with remaining width < 2px
    const RectGeometry tinyRemaining{.x = 999.0, .y = 100.0, .width = 50.0, .height = 50.0};
    const auto tinyClamped = tinyRemaining.clampedToBounds(imageBounds);
    QVERIFY(!tinyClamped.has_value());
}

void GeometryTest::testMarginsValidity() {
    const Margins valid{.top = 5.0, .right = 5.0, .bottom = 5.0, .left = 5.0};
    QVERIFY(valid.isValid());

    const Margins neg{.top = -1.0, .right = 5.0, .bottom = 5.0, .left = 5.0};
    QVERIFY(!neg.isValid());
}

void GeometryTest::testImageSize() {
    const ImageSize valid{.width = 1920, .height = 1080};
    QVERIFY(valid.isValid());

    const ImageSize zeroW{.width = 0, .height = 100};
    QVERIFY(!zeroW.isValid());

    const ImageSize negH{.width = 100, .height = -5};
    QVERIFY(!negH.isValid());
}
