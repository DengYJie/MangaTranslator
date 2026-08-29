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
