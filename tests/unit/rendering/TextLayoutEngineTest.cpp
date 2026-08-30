#include "TextLayoutEngineTest.h"

#include "rendering/TextLayoutEngine.h"

#include <QFontDatabase>
#include <QTest>

using namespace manga;

void TextLayoutEngineTest::layoutHorizontalWrapsCorrectly() {
    domain::RectGeometry bounds{0, 0, 200, 200};
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Horizontal;
    constraints.fontSizePt = 12.0;
    constraints.autoFit = false;

    rendering::LayoutResult result = rendering::TextLayoutEngine::layoutText(
        "A very very long text that should wrap multiple times in a 200x200 box", bounds, constraints, 96.0);

    QVERIFY(!result.overflow);
    QVERIFY(result.inkBounds.height() > 16.0); // should be multiple lines
}

void TextLayoutEngineTest::layoutHorizontalHonorsPageCoordinatesAndDpi() {
    domain::RectGeometry bounds{80, 60, 300, 180};
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Horizontal;
    constraints.alignment = {domain::HorizontalAlignment::Left, domain::VerticalAlignment::Top};
    constraints.fontSizePt = 12.0;
    constraints.autoFit = false;

    const auto layout96 = rendering::TextLayoutEngine::layoutText("Positioned text", bounds, constraints, 96.0);
    const auto layout192 = rendering::TextLayoutEngine::layoutText("Positioned text", bounds, constraints, 192.0);

    QCOMPARE(layout96.layoutBounds.topLeft(), QPointF(82.0, 62.0));
    QVERIFY(layout96.inkBounds.left() >= layout96.layoutBounds.left() - 0.5);
    QVERIFY(layout96.inkBounds.top() >= layout96.layoutBounds.top() - 0.5);
    QVERIFY(layout192.inkBounds.height() > layout96.inkBounds.height());
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

void TextLayoutEngineTest::fixedSizeReportsOverflow() {
    domain::RectGeometry bounds{0, 0, 20, 10};
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Horizontal;
    constraints.wordWrap = false;
    constraints.fontSizePt = 24.0;
    constraints.autoFit = false;

    const auto result = rendering::TextLayoutEngine::layoutText("This cannot fit", bounds, constraints, 96.0);

    QVERIFY(result.overflow);
}

void TextLayoutEngineTest::reportsFontFallbackAndMissingGlyph() {
    domain::RectGeometry bounds{0, 0, 200, 100};
    domain::TextLayout constraints;
    constraints.fontFamily = "MangaTranslator Definitely Missing Font";
    constraints.fontSizePt = 12.0;
    constraints.autoFit = false;

    const QString text(QChar(0x0378));
    const auto result = rendering::TextLayoutEngine::layoutText(text, bounds, constraints, 96.0);
    const auto hasWarning = [&result](QStringView code) {
        for (const auto& warning : result.warnings) {
            if (warning.code == code) {
                return true;
            }
        }
        return false;
    };

    QVERIFY(hasWarning(u"layout.font_missing"));
    QVERIFY(hasWarning(u"layout.missing_glyph"));
    QVERIFY(!result.resolvedLayout.fontFamily.empty());
}
