#include "TextLayoutTest.h"

#include "domain/model/TextLayout.h"

#include <QtTest/QtTest>

using namespace manga::domain;

void TextLayoutTest::testDefaults() {
    const TextLayout layout;
    QVERIFY(layout.isValid());
    // TODO: default fontSizePt updated from 14.0 to 24.0
    QCOMPARE(layout.fontSizePt, 24.0);
    QCOMPARE(layout.fontWeight, 400.0);
    QCOMPARE(layout.letterSpacingEm, 0.0);
    QCOMPARE(layout.textColorHex, QStringLiteral("#000000"));
    QCOMPARE(layout.strokeColorHex, QStringLiteral("#FFFFFF"));
    QCOMPARE(layout.strokeWidthPx, 0.0);
    QCOMPARE(layout.lineSpacing, 1.2);
    QCOMPARE(layout.orientation, TextOrientation::Horizontal);
    QCOMPARE(layout.alignment.horizontal, HorizontalAlignment::Center);
    QCOMPARE(layout.alignment.vertical, VerticalAlignment::Middle);
    QCOMPARE(layout.wordWrap, true);
    QCOMPARE(layout.autoFit, false);
}

void TextLayoutTest::testValidation() {
    TextLayout layout;
    QVERIFY(layout.isValid());

    // Zero or negative font size
    layout.fontSizePt = 0.0;
    QVERIFY(!layout.isValid());
    layout.fontSizePt = -5.0;
    QVERIFY(!layout.isValid());
    layout.fontSizePt = 12.0;

    // Zero or negative line spacing
    layout.lineSpacing = 0.0;
    QVERIFY(!layout.isValid());
    layout.lineSpacing = 1.2;

    // minFontSize > maxFontSize
    layout.minFontSizePt = 20.0;
    layout.maxFontSizePt = 10.0;
    QVERIFY(!layout.isValid());
    layout.minFontSizePt = 6.0;
    layout.maxFontSizePt = 72.0;
    QVERIFY(layout.isValid());

    // Negative margin
    layout.margins.left = -1.0;
    QVERIFY(!layout.isValid());
    layout.margins.left = 0.0;

    // fontWeight bounds
    layout.fontWeight = 50.0;
    QVERIFY(!layout.isValid());
    layout.fontWeight = 950.0;
    QVERIFY(!layout.isValid());
    layout.fontWeight = 700.0;
    QVERIFY(layout.isValid());

    // letterSpacingEm bounds
    layout.letterSpacingEm = -1.0;
    QVERIFY(!layout.isValid());
    layout.letterSpacingEm = 3.0;
    QVERIFY(!layout.isValid());
    layout.letterSpacingEm = 0.5;
    QVERIFY(layout.isValid());

    // strokeWidthPx bounds
    layout.strokeWidthPx = -1.0;
    QVERIFY(!layout.isValid());
    layout.strokeWidthPx = 40.0;
    QVERIFY(!layout.isValid());
    layout.strokeWidthPx = 2.0;
    QVERIFY(layout.isValid());
}

void TextLayoutTest::testOrientationAndAlignment() {
    TextLayout vertLayout;
    vertLayout.orientation = TextOrientation::Vertical;
    vertLayout.alignment = {HorizontalAlignment::Right, VerticalAlignment::Top};

    QVERIFY(vertLayout.isValid());
    QCOMPARE(vertLayout.orientation, TextOrientation::Vertical);
    QCOMPARE(vertLayout.alignment.horizontal, HorizontalAlignment::Right);
    QCOMPARE(vertLayout.alignment.vertical, VerticalAlignment::Top);
}

void TextLayoutTest::testEquality() {
    const TextLayout l1;
    TextLayout l2;
    QVERIFY(l1 == l2);

    l2.fontSizePt = 16.0;
    QVERIFY(l1 != l2);
    l2.fontSizePt = 14.0;

    l2.fontWeight = 700.0;
    QVERIFY(l1 != l2);
    l2.fontWeight = 400.0;
    QVERIFY(l1 == l2);
}
