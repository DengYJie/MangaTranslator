#include "rendering/TextLayoutEngine.h"

#include <QChar>
#include <QFile>
#include <QFontDatabase>
#include <QFontInfo>
#include <QFontMetricsF>
#include <QGlyphRun>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QTextOption>

#include <cmath>

namespace manga::rendering {

namespace {

struct PunctuationOffset {
    double x;
    double y;
};

class PunctuationTable {
public:
    static PunctuationTable& instance() {
        static PunctuationTable table;
        return table;
    }

    PunctuationOffset getOffset(const QString& text) const {
        auto it = m_offsets.find(text);
        if (it != m_offsets.end()) {
            return *it;
        }
        return {0.0, 0.0};
    }

private:
    PunctuationTable() {
        QFile file(":/manga_rendering/text/vertical-punctuation-v1.json");
        if (file.open(QIODevice::ReadOnly)) {
            QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
            QJsonObject root = doc.object();
            QJsonObject offsets = root["offsets"].toObject();
            for (auto it = offsets.begin(); it != offsets.end(); ++it) {
                QJsonObject obj = it.value().toObject();
                m_offsets.insert(it.key(), {obj["x"].toDouble(), obj["y"].toDouble()});
            }
        }
    }

    QHash<QString, PunctuationOffset> m_offsets;
};

QImage createDummyDevice(double logicalDpi) {
    QImage dummyDevice(1, 1, QImage::Format_ARGB32_Premultiplied);
    double dpm = logicalDpi * 1000.0 / 25.4;
    dummyDevice.setDotsPerMeterX(qRound(dpm));
    dummyDevice.setDotsPerMeterY(qRound(dpm));
    return dummyDevice;
}

} // namespace

TextLayoutEngine::TextLayoutEngine() = default;
TextLayoutEngine::~TextLayoutEngine() = default;

LayoutResult TextLayoutEngine::layoutText(const QString& text,
                                          const domain::RectGeometry& bounds,
                                          const domain::TextLayout& constraints,
                                          double logicalDpi) {
    LayoutResult result;
    result.originalText = text;
    result.resolvedLayout = constraints;

    double availW = bounds.width - constraints.margins.left - constraints.margins.right;
    double availH = bounds.height - constraints.margins.top - constraints.margins.bottom;
    if (availW <= 0 || availH <= 0) {
        result.warnings.append(LayoutWarning{"layout.invalid_bounds", "Available bounds are negative or zero."});
        result.overflow = true;
        return result;
    }

    if (constraints.autoFit) {
        if (constraints.minFontSizePt <= 0 || constraints.minFontSizePt > constraints.maxFontSizePt) {
            result.warnings.append(LayoutWarning{"layout.invalid_font_range", "Invalid autoFit font range."});
            result.overflow = true;
            return result;
        }
        computeAutoFit(text, bounds, constraints, logicalDpi, result);
    } else {
        double pixelSize = constraints.fontSizePt * logicalDpi / 72.0;
        if (constraints.orientation == domain::TextOrientation::Horizontal) {
            result = layoutHorizontal(text, bounds, constraints, pixelSize, logicalDpi);
        } else {
            result = layoutVertical(text, bounds, constraints, pixelSize, logicalDpi);
        }
    }

    return result;
}

void TextLayoutEngine::computeAutoFit(const QString& text,
                                      const domain::RectGeometry& bounds,
                                      const domain::TextLayout& constraints,
                                      double logicalDpi,
                                      LayoutResult& outResult) {
    int lowQuarter = std::ceil(constraints.minFontSizePt * 4.0);
    int highQuarter = std::floor(constraints.maxFontSizePt * 4.0);

    if (lowQuarter > highQuarter) {
        outResult.warnings.append(LayoutWarning{"layout.invalid_font_range", "Quantized font range is invalid."});
        outResult.overflow = true;
        return;
    }

    int bestQuarter = lowQuarter;
    LayoutResult bestResult;
    bool foundFit = false;

    int left = lowQuarter;
    int right = highQuarter;

    while (left <= right) {
        int mid = left + (right - left) / 2;
        double testPt = mid / 4.0;
        double testPx = testPt * logicalDpi / 72.0;

        LayoutResult res;
        if (constraints.orientation == domain::TextOrientation::Horizontal) {
            res = layoutHorizontal(text, bounds, constraints, testPx, logicalDpi);
        } else {
            res = layoutVertical(text, bounds, constraints, testPx, logicalDpi);
        }
        res.resolvedLayout.fontSizePt = testPt;

        if (!res.overflow) {
            bestQuarter = mid;
            bestResult = res;
            foundFit = true;
            left = mid + 1;
        } else {
            right = mid - 1;
        }
    }

    if (!foundFit) {
        // If even the minimum size overflows, return it with overflow=true
        double minPx = lowQuarter / 4.0 * logicalDpi / 72.0;
        if (constraints.orientation == domain::TextOrientation::Horizontal) {
            outResult = layoutHorizontal(text, bounds, constraints, minPx, logicalDpi);
        } else {
            outResult = layoutVertical(text, bounds, constraints, minPx, logicalDpi);
        }
        outResult.resolvedLayout.fontSizePt = lowQuarter / 4.0;
        outResult.overflow = true;
    } else {
        outResult = bestResult;
    }
}

LayoutResult TextLayoutEngine::layoutHorizontal(const QString& text,
                                                const domain::RectGeometry& bounds,
                                                const domain::TextLayout& constraints,
                                                double pixelSize,
                                                double logicalDpi) {
    LayoutResult result;
    result.originalText = text;
    result.resolvedLayout = constraints;

    QImage dummyDevice = createDummyDevice(logicalDpi);
    QFont font(QString::fromStdString(constraints.fontFamily));
    font.setPixelSize(qRound(pixelSize));
    font.setWeight(static_cast<QFont::Weight>(constraints.fontWeight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, constraints.letterSpacingEm * pixelSize);

    QFontInfo info(font);
    if (info.family().compare(font.family(), Qt::CaseInsensitive) != 0) {
        if (!info.family().isEmpty()) {
            result.resolvedLayout.fontFamily = info.family().toStdString();
        }
        result.warnings.append(LayoutWarning{"layout.font_missing", "Requested font family not found."});
    }

    // Missing glyph check
    QFontMetricsF fm(font, &dummyDevice);
    int missingGlyphs = 0;
    for (int i = 0; i < text.length(); ++i) {
        if (!fm.inFont(text.at(i))) {
            missingGlyphs++;
        }
    }
    if (missingGlyphs > 0) {
        result.warnings.append(
            LayoutWarning{"layout.missing_glyph", QString::number(missingGlyphs) + " glyph(s) missing."});
    }

    QTextOption option;
    if (constraints.wordWrap) {
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    } else {
        option.setWrapMode(QTextOption::NoWrap);
    }

    switch (constraints.alignment.horizontal) {
        case domain::HorizontalAlignment::Left:
            option.setAlignment(Qt::AlignLeft);
            break;
        case domain::HorizontalAlignment::Right:
            option.setAlignment(Qt::AlignRight);
            break;
        case domain::HorizontalAlignment::Center:
            option.setAlignment(Qt::AlignHCenter);
            break;
        case domain::HorizontalAlignment::Justify:
            option.setAlignment(Qt::AlignJustify);
            break;
    }

    double availW = bounds.width - constraints.margins.left - constraints.margins.right;
    double availH = bounds.height - constraints.margins.top - constraints.margins.bottom;

    QTextLayout layout(text, font, &dummyDevice);
    layout.setTextOption(option);
    layout.beginLayout();

    double y = 0.0;
    double lineHeight = fm.height() * constraints.lineSpacing;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid())
            break;

        line.setLineWidth(availW);
        line.setPosition(QPointF(0, y));
        y += lineHeight;
    }
    layout.endLayout();

    double textHeight = y;

    // Vertical alignment offset
    const QRectF availableRect(bounds.x + constraints.margins.left, bounds.y + constraints.margins.top, availW, availH);
    double yOffset = availableRect.top();
    switch (constraints.alignment.vertical) {
        case domain::VerticalAlignment::Top:
            break;
        case domain::VerticalAlignment::Middle:
            yOffset += (availH - textHeight) / 2.0;
            break;
        case domain::VerticalAlignment::Bottom:
            yOffset += (availH - textHeight);
            break;
    }

    const double xOffset = availableRect.left();
    QRectF unionInk;

    for (int i = 0; i < layout.lineCount(); ++i) {
        QTextLine line = layout.lineAt(i);
        foreach (const QGlyphRun& run, line.glyphRuns()) {
            QRectF rect = run.boundingRect();
            rect.translate(xOffset, yOffset);
            unionInk = unionInk.united(rect);
        }
    }

    // Add stroke expansion
    double strokeExt = constraints.strokeWidthPx / 2.0;
    if (strokeExt > 0.0) {
        unionInk.adjust(-strokeExt, -strokeExt, strokeExt, strokeExt);
    }

    result.inkBounds = unionInk;
    result.layoutBounds = availableRect;

    // Tolerance 0.5px
    if (!unionInk.isEmpty() &&
        (unionInk.left() < availableRect.left() - 0.5 || unionInk.right() > availableRect.right() + 0.5 ||
         unionInk.top() < availableRect.top() - 0.5 || unionInk.bottom() > availableRect.bottom() + 0.5)) {
        result.overflow = true;
    }

    return result;
}

LayoutResult TextLayoutEngine::layoutVertical(const QString& text,
                                              const domain::RectGeometry& bounds,
                                              const domain::TextLayout& constraints,
                                              double pixelSize,
                                              double logicalDpi) {
    LayoutResult result;
    result.originalText = text;
    result.resolvedLayout = constraints;

    QImage dummyDevice = createDummyDevice(logicalDpi);
    QFont font(QString::fromStdString(constraints.fontFamily));
    font.setPixelSize(qRound(pixelSize));
    font.setWeight(static_cast<QFont::Weight>(constraints.fontWeight));

    QFontInfo info(font);
    if (info.family().compare(font.family(), Qt::CaseInsensitive) != 0) {
        if (!info.family().isEmpty()) {
            result.resolvedLayout.fontFamily = info.family().toStdString();
        }
        result.warnings.append(LayoutWarning{"layout.font_missing", "Requested font family not found."});
    }

    QFontMetricsF fm(font, &dummyDevice);
    int missingGlyphs = 0;

    double emSize = fm.height(); // approximation of em box
    double colAdvance = emSize * constraints.lineSpacing;

    double availW = bounds.width - constraints.margins.left - constraints.margins.right;
    double availH = bounds.height - constraints.margins.top - constraints.margins.bottom;

    struct Col {
        QVector<GlyphCluster> clusters;
        double height{0};
    };
    QVector<Col> columns;
    columns.append(Col());

    int i = 0;
    while (i < text.length()) {
        int nextValid = i + 1;
        if (text.at(i).isHighSurrogate() && i + 1 < text.length() && text.at(i + 1).isLowSurrogate()) {
            nextValid = i + 2;
        }

        QString clusterStr = text.mid(i, nextValid - i);
        i = nextValid;

        if (clusterStr == "\n") {
            columns.append(Col());
            continue;
        }

        if (!fm.inFont(clusterStr.at(0))) {
            missingGlyphs++;
        }

        bool isAsciiDigit =
            clusterStr.length() == 1 && clusterStr.at(0).isDigit() && clusterStr.at(0).unicode() <= 0x7F;
        bool isAsciiLetter =
            clusterStr.length() == 1 && clusterStr.at(0).isLetter() && clusterStr.at(0).unicode() <= 0x7F;

        // Tate-chu-yoko logic
        if (isAsciiDigit) {
            int digitsCount = 1;
            int lookahead = i;
            while (lookahead < text.length() && text.at(lookahead).isDigit() && text.at(lookahead).unicode() <= 0x7F) {
                digitsCount++;
                lookahead++;
            }
            if (digitsCount == 2) {
                clusterStr = text.mid(i - 1, 2);
                i = lookahead;
            }
        }

        GlyphCluster gc;
        gc.text = clusterStr;
        gc.isVertical = true;
        gc.emBox = QRectF(0, 0, emSize, emSize);

        if (clusterStr.length() == 2 && clusterStr.at(0).isDigit()) {
            gc.isTateChuYoko = true;
        } else if (isAsciiLetter || (clusterStr.length() == 1 && clusterStr.at(0).isDigit())) {
            gc.transform.rotate(90);
            gc.transform.translate(0, -emSize);
        } else {
            PunctuationOffset offset = PunctuationTable::instance().getOffset(clusterStr);
            if (offset.x != 0.0 || offset.y != 0.0) {
                gc.transform.translate(offset.x * emSize, offset.y * emSize);
            }
        }

        // Measure ink rect
        QPainterPath path;
        path.addText(0, fm.ascent(), font, clusterStr);
        QRectF ink = path.boundingRect();
        if (gc.transform.isIdentity()) {
            gc.inkBounds = ink;
        } else {
            gc.inkBounds = gc.transform.mapRect(ink);
        }

        // Check column overflow
        if (columns.last().height + emSize > availH && constraints.wordWrap && !columns.last().clusters.isEmpty()) {
            columns.append(Col());
        }

        gc.position = QPointF(0, columns.last().height);
        columns.last().clusters.append(gc);
        columns.last().height += emSize;
    }

    if (missingGlyphs > 0) {
        result.warnings.append(
            LayoutWarning{"layout.missing_glyph", QString::number(missingGlyphs) + " glyph(s) missing."});
    }

    double totalWidth = columns.size() * colAdvance;
    double xOffset = bounds.x + bounds.width - constraints.margins.right - colAdvance;

    switch (constraints.alignment.horizontal) {
        case domain::HorizontalAlignment::Right:
            break;
        case domain::HorizontalAlignment::Center:
            xOffset -= (availW - totalWidth) / 2.0;
            break;
        case domain::HorizontalAlignment::Left:
            xOffset -= (availW - totalWidth);
            break;
        case domain::HorizontalAlignment::Justify:
            break; // not fully supported in simple columns
    }

    QRectF unionInk;
    for (int colIdx = 0; colIdx < columns.size(); ++colIdx) {
        const Col& col = columns[colIdx];
        double yOffset = bounds.y + constraints.margins.top;
        switch (constraints.alignment.vertical) {
            case domain::VerticalAlignment::Top:
                break;
            case domain::VerticalAlignment::Middle:
                yOffset += (availH - col.height) / 2.0;
                break;
            case domain::VerticalAlignment::Bottom:
                yOffset += (availH - col.height);
                break;
        }

        for (const GlyphCluster& gcOrig : col.clusters) {
            GlyphCluster gc = gcOrig;
            gc.position.rx() += xOffset - colIdx * colAdvance;
            gc.position.ry() += yOffset;

            QRectF globalInk = gc.inkBounds.translated(gc.position);
            unionInk = unionInk.united(globalInk);
            result.clusters.append(gc);
        }
    }

    double strokeExt = constraints.strokeWidthPx / 2.0;
    if (strokeExt > 0.0) {
        unionInk.adjust(-strokeExt, -strokeExt, strokeExt, strokeExt);
    }

    result.inkBounds = unionInk;
    result.layoutBounds =
        QRectF(bounds.x + constraints.margins.left, bounds.y + constraints.margins.top, availW, availH);

    QRectF availRect(bounds.x + constraints.margins.left, bounds.y + constraints.margins.top, availW, availH);

    if (!unionInk.isEmpty() &&
        (unionInk.left() < availRect.left() - 0.5 || unionInk.right() > availRect.right() + 0.5 ||
         unionInk.top() < availRect.top() - 0.5 || unionInk.bottom() > availRect.bottom() + 0.5)) {
        result.overflow = true;
    }

    return result;
}

} // namespace manga::rendering
