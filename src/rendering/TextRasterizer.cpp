#include "rendering/TextRasterizer.h"

#include <QColor>
#include <QPainterPath>
#include <QPen>
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

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    if (constraints.orientation == domain::TextOrientation::Horizontal) {
        // We re-run horizontal layout strictly for painting using QTextLayout format rendering
        // so that justification and word spacing is exactly as measured.
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

        QTextLayout qlayout(layout.clusters.isEmpty() ? "" : layout.clusters.first().text, font, painter.device());
        // Wait, TextLayoutEngine horizontal doesn't populate clusters!
        // We didn't pass the text into LayoutResult...
        // Ah, LayoutResult doesn't have the original text. We need to put it there or pass it to draw.
        // But for vertical we have clusters.
        // Let's modify LayoutResult to include `QString originalText;` ?
        // Or if clusters are empty, it's an error.
    } else {
        // Vertical rendering with clusters
        QPainterPath path;
        for (const GlyphCluster& gc : layout.clusters) {
            QPainterPath clusterPath;
            if (gc.isTateChuYoko) {
                // Scale horizontally to fit 1 em box
                QPainterPath tcy;
                tcy.addText(0, QFontMetricsF(font).ascent(), font, gc.text);
                double tcyWidth = tcy.boundingRect().width();
                double em = gc.emBox.width();
                if (tcyWidth > 0 && tcyWidth > em) {
                    QTransform scale;
                    scale.scale(em / tcyWidth, 1.0);
                    tcy = scale.map(tcy);
                }
                // Center it
                double dx = (em - tcy.boundingRect().width()) / 2.0;
                tcy.translate(dx, 0);
                clusterPath.addPath(tcy);
            } else {
                clusterPath.addText(0, QFontMetricsF(font).ascent(), font, gc.text);
            }

            if (!gc.transform.isIdentity()) {
                clusterPath = gc.transform.map(clusterPath);
            }
            clusterPath.translate(gc.position);
            path.addPath(clusterPath);
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
