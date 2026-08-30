#include "TextRasterizerTest.h"

#include "rendering/TextLayoutEngine.h"
#include "rendering/TextRasterizer.h"

#include <QImage>
#include <QTest>

using namespace manga;

void TextRasterizerTest::rendersHorizontalTextToImage() {
    domain::RectGeometry bounds{0, 0, 100, 100};
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Horizontal;
    constraints.fontSizePt = 12.0;

    rendering::LayoutResult layout = rendering::TextLayoutEngine::layoutText("Hello World", bounds, constraints, 96.0);

    QImage img = rendering::TextRasterizer::renderToImage(layout, 100, 100, 96.0);

    QVERIFY(img.width() == 100);
    QVERIFY(img.height() == 100);

    bool hasNonTransparent = false;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 0) {
                hasNonTransparent = true;
                break;
            }
        }
        if (hasNonTransparent)
            break;
    }

    QVERIFY(hasNonTransparent);
}

void TextRasterizerTest::rendersHorizontalTextAtResolvedPagePosition() {
    domain::RectGeometry bounds{80, 60, 200, 100};
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Horizontal;
    constraints.alignment = {domain::HorizontalAlignment::Left, domain::VerticalAlignment::Bottom};
    constraints.fontSizePt = 18.0;
    constraints.strokeWidthPx = 1.0;

    const auto layout = rendering::TextLayoutEngine::layoutText("Aligned", bounds, constraints, 96.0);
    const QImage image = rendering::TextRasterizer::renderToImage(layout, 360, 220, 96.0);

    bool foundInside = false;
    bool foundAboveRegion = false;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) == 0) {
                continue;
            }
            foundInside |= x >= 79 && x <= 281 && y >= 59 && y <= 161;
            foundAboveRegion |= y < 59;
        }
    }

    QVERIFY(foundInside);
    QVERIFY(!foundAboveRegion);
}

void TextRasterizerTest::rendersVerticalTextToImage() {
    domain::RectGeometry bounds{20, 10, 80, 120};
    domain::TextLayout constraints;
    constraints.orientation = domain::TextOrientation::Vertical;
    constraints.fontSizePt = 18.0;
    constraints.strokeWidthPx = 1.0;

    const auto layout = rendering::TextLayoutEngine::layoutText("日本語", bounds, constraints, 96.0);
    const QImage image = rendering::TextRasterizer::renderToImage(layout, 140, 160, 96.0);

    bool foundInk = false;
    for (int y = 0; y < image.height() && !foundInk; ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0) {
                foundInk = true;
                break;
            }
        }
    }

    QVERIFY(foundInk);
}
