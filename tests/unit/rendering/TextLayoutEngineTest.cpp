#include "TextLayoutEngineTest.h"

#include "rendering/TextLayoutEngine.h"

#include <QFontDatabase>
#include <QTest>

#include <iostream>

using namespace manga;

void TextLayoutEngineTest::layoutHorizontalWrapsCorrectly() {
    std::cout << "DEBUG: Running layoutHorizontalWrapsCorrectly" << std::endl;
    domain::RectGeometry bounds{0, 0, 200, 200};
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Horizontal;
    constraints.fontSizePt = 12.0;
    constraints.autoFit = false;

    rendering::LayoutResult result = rendering::TextLayoutEngine::layoutText(
        "A very very long text that should wrap multiple times in a 200x200 box", bounds, constraints, 96.0);

    QVERIFY(!result.overflow);
    std::cout << "DEBUG: inkBounds.height() = " << result.inkBounds.height() << std::endl;
    QVERIFY(result.inkBounds.height() > 16.0); // should be multiple lines
}

void TextLayoutEngineTest::layoutVerticalHasCorrectBounds() {
    domain::RectGeometry bounds{0, 0, 100, 100};
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Vertical;
    constraints.fontSizePt = 12.0;
    constraints.autoFit = false;

    rendering::LayoutResult result = rendering::TextLayoutEngine::layoutText("日本語", bounds, constraints, 96.0);

    QVERIFY(!result.overflow);
    QVERIFY(result.clusters.size() == 3);
    // Vertical layout positions from top to bottom
    QVERIFY(result.clusters[1].position.y() > result.clusters[0].position.y());
    // And from right to left
    QCOMPARE(result.clusters[0].position.x(), result.clusters[2].position.x());
}

void TextLayoutEngineTest::autoFitFindsBestSize() {
    domain::RectGeometry bounds{0, 0, 50, 50}; // small box
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Horizontal;
    constraints.autoFit = true;
    constraints.minFontSizePt = 6.0;
    constraints.maxFontSizePt = 24.0;

    rendering::LayoutResult result = rendering::TextLayoutEngine::layoutText(
        "This is a very long text that needs to shrink down.", bounds, constraints, 96.0);

    QVERIFY(result.resolvedLayout.fontSizePt < 24.0);
    QVERIFY(!result.overflow || result.resolvedLayout.fontSizePt == 6.0);
}
