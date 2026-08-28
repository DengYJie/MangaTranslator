#include "TextLayoutTest.h"

#include "domain/model/TextLayout.h"

#include <QtTest/QtTest>

using namespace manga::domain;

void TextLayoutTest::testDefaults() {
    const TextLayout layout;
    QVERIFY(layout.isValid());
    QCOMPARE(layout.fontSizePt, 14.0);
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
}
