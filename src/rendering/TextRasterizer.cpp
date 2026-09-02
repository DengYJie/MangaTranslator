#include "rendering/TextRasterizer.h"

#include <QColor>
#include <QDebug>
#include <QFontInfo>
#include <QGlyphRun>
#include <QPainterPath>
#include <QPen>
#include <QRawFont>
#include <QTextLayout>

namespace manga::rendering {

void TextRasterizer::draw(QPainter& painter, const LayoutResult& layout, double logicalDpi) {
    const auto& constraints = layout.resolvedLayout;

    QColor textColor(QString::fromStdString(constraints.textColorHex));
    QColor strokeColor(QString::fromStdString(constraints.strokeColorHex));
    double strokeWidth = constraints.strokeWidthPx;

    QFont font(QString::fromStdString(constraints.fontFamily));
    double pixelSize = constraints.fontSizePt * logicalDpi / 72.0;
    font.setPixelSize(qRound(pixelSize));
    font.setWeight(static_cast<QFont::Weight>(constraints.fontWeight));
    font.setLetterSpacing(QFont::AbsoluteSpacing, constraints.letterSpacingEm * pixelSize);

    QFontInfo info(font);
    qDebug().noquote() << "[TextRasterizer] draw: reqFamily=" << QString::fromStdString(constraints.fontFamily)
                       << "actFamily=" << info.family() << "reqWeight=" << constraints.fontWeight
                       << "actWeight=" << static_cast<int>(info.weight()) << "pixelSize=" << pixelSize << "orientation="
                       << (constraints.orientation == domain::TextOrientation::Horizontal ? "H" : "V");

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (constraints.orientation == domain::TextOrientation::Horizontal) {
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

        QTextLayout qlayout(layout.originalText, font, painter.device());
        qlayout.setTextOption(option);
        qlayout.beginLayout();

        double availW = layout.layoutBounds.width();
        double availH = layout.layoutBounds.height();
        double y = 0.0;
        QFontMetricsF fm(font);
        double lineHeight = fm.height() * constraints.lineSpacing;

        while (true) {
            QTextLine line = qlayout.createLine();
            if (!line.isValid())
                break;

            line.setLineWidth(availW);
            line.setPosition(QPointF(0, y));
            y += lineHeight;
        }
        qlayout.endLayout();

        double textHeight = y;
        double yOffset = layout.layoutBounds.top();
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
        const double xOffset = layout.layoutBounds.left();

        QPainterPath path;
        for (int i = 0; i < qlayout.lineCount(); ++i) {
            const QTextLine line = qlayout.lineAt(i);
            for (const QGlyphRun& run : line.glyphRuns()) {
                const QRawFont rawFont = run.rawFont();
                const QList<quint32> glyphIndexes = run.glyphIndexes();
                const QList<QPointF> positions = run.positions();
                const qsizetype glyphCount = qMin(glyphIndexes.size(), positions.size());
                for (qsizetype glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex) {
                    QPainterPath glyphPath = rawFont.pathForGlyph(glyphIndexes.at(glyphIndex));
                    glyphPath.translate(QPointF(xOffset, yOffset) + positions.at(glyphIndex));
                    path.addPath(glyphPath);
                }
            }
        }

        if (strokeWidth > 0.0) {
            QPen pen(strokeColor, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

        painter.setPen(textColor);
        painter.setBrush(Qt::NoBrush);
        qlayout.draw(&painter, QPointF(xOffset, yOffset));
    } else {
        // Vertical rendering with clusters
        QPainterPath path;
        QFontMetricsF fm(font);
        const double ascent = fm.ascent();

        for (const GlyphCluster& gc : layout.clusters) {
            if (gc.isTateChuYoko) {
                // Scale horizontally to fit 1 em box
                QPainterPath tcy;
                tcy.addText(0, ascent, font, gc.text);
                const double tcyWidth = tcy.boundingRect().width();
                const double em = gc.emBox.width();
                if (tcyWidth > 0 && tcyWidth > em) {
                    QTransform scale;
                    scale.scale(em / tcyWidth, 1.0);
                    tcy = scale.map(tcy);
                }
                const double dx = (em - tcy.boundingRect().width()) / 2.0;
                tcy.translate(gc.position.x() + dx, gc.position.y());
                path.addPath(tcy);
            } else if (!gc.transform.isIdentity()) {
                QPainterPath p;
                p.addText(0, 0, font, gc.text);
                p = gc.transform.map(p);
                path.addPath(p);
            } else {
                QPainterPath p;
                p.addText(gc.position.x(), gc.position.y() + ascent, font, gc.text);
                path.addPath(p);
            }
        }

        if (strokeWidth > 0.0) {
            QPen pen(strokeColor, strokeWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(path);
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(textColor);
        painter.drawPath(path);
    }

    painter.restore();
}

QImage TextRasterizer::renderToImage(const LayoutResult& layout, int width, int height, double logicalDpi) {
    QImage img(width, height, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);

    double dpm = logicalDpi * 1000.0 / 25.4;
    img.setDotsPerMeterX(qRound(dpm));
    img.setDotsPerMeterY(qRound(dpm));

    QPainter painter(&img);
    draw(painter, layout, logicalDpi);
    painter.end();

    return img;
}

} // namespace manga::rendering
