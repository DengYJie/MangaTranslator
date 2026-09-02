#include "rendering/TextLayoutEngine.h"

#include "rendering/FontRegistry.h"

#include <QChar>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
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
    FontRegistry::bundledFontFamilies();
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
    qDebug().noquote() << "[TextLayoutEngine] layoutHorizontal: reqFamily="
                       << QString::fromStdString(constraints.fontFamily) << "actFamily=" << info.family()
                       << "reqWeight=" << constraints.fontWeight << "actWeight=" << static_cast<int>(info.weight())
                       << "styleName=" << info.styleName();
    if (info.family().compare(font.family(), Qt::CaseInsensitive) != 0) {
        if (!info.family().isEmpty()) {
            result.resolvedLayout.fontFamily = info.family().toStdString();
        }
        result.warnings.append(LayoutWarning{"layout.font_missing", "Requested font family not found."});
    }

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

    FontRegistry::bundledFontFamilies();

    QImage dummyDevice = createDummyDevice(logicalDpi);
    QFont font(QString::fromStdString(constraints.fontFamily));
    font.setPixelSize(qRound(pixelSize));
    font.setWeight(static_cast<QFont::Weight>(constraints.fontWeight));

    QFontInfo info(font);
    qDebug().noquote() << "[TextLayoutEngine] layoutVertical: reqFamily="
                       << QString::fromStdString(constraints.fontFamily) << "actFamily=" << info.family()
                       << "reqWeight=" << constraints.fontWeight << "actWeight=" << static_cast<int>(info.weight())
                       << "styleName=" << info.styleName();
    if (info.family().compare(font.family(), Qt::CaseInsensitive) != 0) {
        if (!info.family().isEmpty()) {
            result.resolvedLayout.fontFamily = info.family().toStdString();
        }
        result.warnings.append(LayoutWarning{"layout.font_missing", "Requested font family not found."});
    }

    QFontMetricsF fm(font, &dummyDevice);
    int missingGlyphs = 0;

    const double emSize = fm.height();
    const double colAdvance = emSize * constraints.lineSpacing;
    const double ascent = fm.ascent();

    const double availW = bounds.width - constraints.margins.left - constraints.margins.right;
    const double availH = bounds.height - constraints.margins.top - constraints.margins.bottom;

    struct ColItem {
        GlyphCluster cluster;
        double advanceY{0.0};
    };

    struct Column {
        QVector<ColItem> items;
        double height{0.0};
    };

    QVector<Column> columns;
    columns.append(Column{});

    int i = 0;
    while (i < text.length()) {
        int nextValid = i + 1;
        if (text.at(i).isHighSurrogate() && i + 1 < text.length() && text.at(i + 1).isLowSurrogate()) {
            nextValid = i + 2;
        }

        const QString clusterStr = text.mid(i, nextValid - i);
        i = nextValid;

        if (clusterStr == QStringLiteral("\n")) {
            columns.append(Column{});
            continue;
        }

        if (!fm.inFont(clusterStr.at(0))) {
            missingGlyphs++;
        }

        const QChar ch = clusterStr.at(0);
        const bool isAsciiDigit = clusterStr.length() == 1 && ch.isDigit() && ch.unicode() <= 0x7F;
        const bool isAsciiLetter = clusterStr.length() == 1 && ch.isLetter() && ch.unicode() <= 0x7F;
        const bool isAsciiPunct = clusterStr.length() == 1 && (ch.isPunct() || ch.isSymbol()) && ch.unicode() <= 0x7F;

        QString finalStr = clusterStr;
        bool isTateChuYoko = false;
        bool rotate90 = false;

        // Check for 2-digit Tate-chu-yoko such as "12" or "99", and "!?"
        if (isAsciiDigit && i < text.length() && text.at(i).isDigit() && text.at(i).unicode() <= 0x7F) {
            if (i + 1 >= text.length() || !text.at(i + 1).isDigit() || text.at(i + 1).unicode() > 0x7F) {
                finalStr = text.mid(i - 1, 2);
                i += 1;
                isTateChuYoko = true;
            }
        } else if ((ch == '!' || ch == '?') && i < text.length() && (text.at(i) == '!' || text.at(i) == '?')) {
            finalStr = text.mid(i - 1, 2);
            i += 1;
            isTateChuYoko = true;
        } else if (isAsciiLetter || isAsciiDigit || isAsciiPunct) {
            rotate90 = true;
        }

        const double glyphW = fm.horizontalAdvance(finalStr);
        // A rotated cluster turns its horizontal advance into its vertical extent, so the cell takes that
        // advance, floored at a share of the em so a narrow glyph cannot collapse the column.
        const double cellH = isTateChuYoko ? emSize : (rotate90 ? qMax(glyphW, emSize * 0.8) : emSize);

        if (constraints.wordWrap && !columns.last().items.isEmpty() && columns.last().height + cellH > availH) {
            columns.append(Column{});
        }

        GlyphCluster gc;
        gc.text = finalStr;
        gc.isVertical = true;
        gc.isTateChuYoko = isTateChuYoko;
        gc.emBox = QRectF(0, 0, emSize, cellH);

        const double localX = (emSize - glyphW) / 2.0;
        const double localY = columns.last().height;

        if (rotate90) {
            const double cx = emSize / 2.0;
            const double cy = localY + cellH / 2.0;
            QTransform t;
            t.translate(cx, cy);
            t.rotate(90);
            t.translate(-glyphW / 2.0, -ascent + cellH / 2.0);
            gc.transform = t;
            gc.position = QPointF(0, 0);

            QPainterPath path;
            path.addText(0, 0, font, finalStr);
            gc.inkBounds = gc.transform.mapRect(path.boundingRect());
        } else if (isTateChuYoko) {
            gc.position = QPointF(localX, localY);
            QPainterPath path;
            path.addText(0, ascent, font, finalStr);
            gc.inkBounds = path.boundingRect().translated(gc.position);
        } else {
            PunctuationOffset offset = PunctuationTable::instance().getOffset(finalStr);
            gc.position = QPointF(localX + offset.x * emSize, localY + offset.y * emSize);
            QPainterPath path;
            path.addText(0, ascent, font, finalStr);
            gc.inkBounds = path.boundingRect().translated(gc.position);
        }

        columns.last().items.append(ColItem{gc, cellH});
        columns.last().height += cellH;
    }

    if (missingGlyphs > 0) {
        result.warnings.append(
            LayoutWarning{"layout.missing_glyph", QString::number(missingGlyphs) + " glyph(s) missing."});
    }

    const int numCols = columns.size();
    const double totalBlockWidth = numCols * colAdvance;

    double blockStartX = bounds.x + constraints.margins.left;
    switch (constraints.alignment.horizontal) {
        case domain::HorizontalAlignment::Center:
            blockStartX += (availW - totalBlockWidth) / 2.0;
            break;
        case domain::HorizontalAlignment::Left:
            break;
        case domain::HorizontalAlignment::Right:
        case domain::HorizontalAlignment::Justify:
            blockStartX += (availW - totalBlockWidth);
            break;
    }

    QRectF unionInk;
    for (int colIdx = 0; colIdx < numCols; ++colIdx) {
        const Column& col = columns[colIdx];
        const double colX = blockStartX + (numCols - 1 - colIdx) * colAdvance;

        double colY = bounds.y + constraints.margins.top;
        switch (constraints.alignment.vertical) {
            case domain::VerticalAlignment::Middle:
                colY += (availH - col.height) / 2.0;
                break;
            case domain::VerticalAlignment::Bottom:
                colY += (availH - col.height);
                break;
            case domain::VerticalAlignment::Top:
                break;
        }

        for (const ColItem& item : col.items) {
            GlyphCluster gc = item.cluster;
            if (gc.transform.isIdentity()) {
                gc.position.rx() += colX;
                gc.position.ry() += colY;
                gc.inkBounds.translate(colX, colY);
            } else {
                QTransform shift;
                shift.translate(colX, colY);
                gc.transform = gc.transform * shift;
                gc.inkBounds.translate(colX, colY);
            }

            unionInk = unionInk.united(gc.inkBounds);
            result.clusters.append(gc);
        }
    }

    const double strokeExt = constraints.strokeWidthPx / 2.0;
    if (strokeExt > 0.0) {
        unionInk.adjust(-strokeExt, -strokeExt, strokeExt, strokeExt);
    }

    result.inkBounds = unionInk;
    result.layoutBounds =
        QRectF(bounds.x + constraints.margins.left, bounds.y + constraints.margins.top, availW, availH);

    const QRectF availRect = result.layoutBounds;
    if (!unionInk.isEmpty() &&
        (unionInk.left() < availRect.left() - 0.5 || unionInk.right() > availRect.right() + 0.5 ||
         unionInk.top() < availRect.top() - 0.5 || unionInk.bottom() > availRect.bottom() + 0.5)) {
        result.overflow = true;
    }

    return result;
}

} // namespace manga::rendering
