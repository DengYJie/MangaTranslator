#include "ui/manga/MangaCanvas.h"

#include "ui/manga/MangaTextItem.h"
#include "ui/manga/RegionOverlayItem.h"

#include <FluentQt/BasicInput.h>
#include <FluentQt/Layout.h>
#include <FluentQt/MenusToolbars.h>
#include <FluentQt/TextFields.h>
#include <QAction>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QCursor>
#include <QFocusEvent>
#include <QGraphicsItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSet>
#include <QShowEvent>
#include <QStyleOptionGraphicsItem>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>

namespace manga::ui::manga {

using ::manga::domain::ImageSize;
using ::manga::domain::RectGeometry;
using ::manga::domain::RegionId;

/// Paints the manual override layer straight from a canvas-owned QImage. The item references the
/// buffer and never copies it: during a stroke draft the rasterizer writes into that same buffer,
/// so a frame update costs the dirty rect instead of a full-page pixmap rebuild.
class OverrideLayerItem : public QGraphicsItem {
public:
    void setSource(const QImage* source) {
        if (m_source == source) {
            return;
        }
        prepareGeometryChange();
        m_source = source;
        update();
    }

    [[nodiscard]] const QImage* source() const noexcept {
        return m_source;
    }

    [[nodiscard]] QRectF boundingRect() const override {
        return (m_source != nullptr && !m_source->isNull()) ? QRectF(m_source->rect()) : QRectF();
    }

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override {
        Q_UNUSED(option);
        Q_UNUSED(widget);
        if (m_source != nullptr && !m_source->isNull()) {
            painter->drawImage(0, 0, *m_source);
        }
    }

private:
    const QImage* m_source{nullptr}; // non-owning, canvas-owned buffer
};

namespace {
constexpr double kZoomLevels[] = {0.05, 0.10, 0.25, 0.50, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0};
constexpr double kMinVisibleContentViewportPx = 64.0;
constexpr double kMinRemoveDiameter = 1.0;
constexpr double kMaxRemoveDiameter = 128.0;
const QColor kDarkDocumentWorkspaceBackground(QStringLiteral("#202124"));

canvas::HandlePosition arrowToPrimaryHandle(int key) noexcept {
    switch (key) {
        case Qt::Key_Up:
            return canvas::HandlePosition::North;
        case Qt::Key_Down:
            return canvas::HandlePosition::South;
        case Qt::Key_Left:
            return canvas::HandlePosition::West;
        default:
            return canvas::HandlePosition::East;
    }
}

constexpr int kColorPickerSupersample = 4;
constexpr int kColorPickerGlyphPixels = 28;
constexpr double kColorPickerOutlinePixels = 0.5;
constexpr int kColorPickerMarginPixels = 2;
constexpr int kColorPickerInkThreshold = 24;
constexpr int kColorPickerFillRgb = 26;

/// Rasterizes the eyedropper glyph into a QCursor whose hotspot sits exactly on
/// the pen tip, the point a sample is taken from. The mask is sampled a few
/// times per physical screen pixel and the pixmap carries the screen's device
/// pixel ratio, so the platform shows the cursor without resampling it.
QCursor buildColorPickerCursor(qreal devicePixelRatio) {
    const QString glyphText = Typography::Icons::glyph(QStringLiteral("ic_fluent_eyedropper_20_regular"));
    if (glyphText.isEmpty()) {
        return QCursor(Qt::CrossCursor);
    }

    const qreal deviceScale = std::max(1.0, static_cast<double>(devicePixelRatio));
    const int supersample = kColorPickerSupersample;
    const double maskPixelsPerDip = supersample * deviceScale;
    const int fontPixels = qRound(kColorPickerGlyphPixels * maskPixelsPerDip);
    const int canvasPx = fontPixels * 2;

    // Draw the glyph at its crisp optical size; the font is rasterized directly
    // at the mask resolution so the final downscale keeps thin strokes sharp.
    QImage glyphMask(canvasPx, canvasPx, QImage::Format_ARGB32_Premultiplied);
    glyphMask.fill(Qt::transparent);
    {
        const QColor fillColor(kColorPickerFillRgb, kColorPickerFillRgb, kColorPickerFillRgb, 255);
        QFont glyphFont = Typography::Icons::font(kColorPickerGlyphPixels);
        glyphFont.setPixelSize(fontPixels);
        QPainter maskPainter(&glyphMask);
        maskPainter.setRenderHint(QPainter::Antialiasing, true);
        maskPainter.setRenderHint(QPainter::TextAntialiasing, true);
        maskPainter.setFont(glyphFont);
        maskPainter.setPen(fillColor);
        maskPainter.setBrush(Qt::NoBrush);
        maskPainter.drawText(QRect(0, 0, canvasPx, canvasPx), Qt::AlignCenter, glyphText);
    }

    const QImage glyphAlpha = glyphMask.convertToFormat(QImage::Format_Alpha8);
    if (glyphAlpha.isNull()) {
        return QCursor(Qt::CrossCursor);
    }

    int inkMinX = canvasPx;
    int inkMinY = canvasPx;
    int inkMaxX = -1;
    int inkMaxY = -1;
    for (int y = 0; y < canvasPx; ++y) {
        const uchar* row = glyphAlpha.constScanLine(y);
        for (int x = 0; x < canvasPx; ++x) {
            if (row[x] >= kColorPickerInkThreshold) {
                inkMinX = std::min(inkMinX, x);
                inkMinY = std::min(inkMinY, y);
                inkMaxX = std::max(inkMaxX, x);
                inkMaxY = std::max(inkMaxY, y);
            }
        }
    }
    if (inkMaxX < 0) {
        return QCursor(Qt::CrossCursor);
    }

    // Dilate the ink so the white rim around the dark glyph stays visible over
    // both light and dark page content.
    const int radius = qMax(1, qRound(kColorPickerOutlinePixels * maskPixelsPerDip));
    const int minY = std::max(0, inkMinY - radius);
    const int maxY = std::min(canvasPx - 1, inkMaxY + radius);
    const int minX = std::max(0, inkMinX - radius);
    const int maxX = std::min(canvasPx - 1, inkMaxX + radius);
    QImage outlineMask(canvasPx, canvasPx, QImage::Format_Alpha8);
    outlineMask.fill(0);
    for (int y = minY; y <= maxY; ++y) {
        const int windowTop = std::max(0, y - radius);
        const int windowBottom = std::min(canvasPx - 1, y + radius);
        uchar* outRow = outlineMask.scanLine(y);
        for (int x = minX; x <= maxX; ++x) {
            const int windowLeft = std::max(0, x - radius);
            const int windowRight = std::min(canvasPx - 1, x + radius);
            uchar best = 0;
            for (int wy = windowTop; wy <= windowBottom; ++wy) {
                const uchar* row = glyphAlpha.constScanLine(wy);
                for (int wx = windowLeft; wx <= windowRight; ++wx) {
                    best = std::max(best, row[wx]);
                }
            }
            outRow[x] = best;
        }
    }

    // The rim bbox also defines the final image size.
    int rimMinX = canvasPx;
    int rimMinY = canvasPx;
    int rimMaxX = -1;
    int rimMaxY = -1;
    int tipX = 0;
    int tipY = 0;
    int bestProjection = std::numeric_limits<int>::min();
    for (int y = minY; y <= maxY; ++y) {
        const uchar* row = outlineMask.constScanLine(y);
        for (int x = minX; x <= maxX; ++x) {
            if (row[x] == 0) {
                continue;
            }
            rimMinX = std::min(rimMinX, x);
            rimMinY = std::min(rimMinY, y);
            rimMaxX = std::max(rimMaxX, x);
            rimMaxY = std::max(rimMaxY, y);
            // The pen tip is the rim pixel furthest toward the southwest, where
            // the sampling point of the cursor must coincide.
            const int projection = y - x;
            if (projection > bestProjection) {
                bestProjection = projection;
                tipX = x;
                tipY = y;
            }
        }
    }
    if (rimMaxX < 0) {
        return QCursor(Qt::CrossCursor);
    }

    const int marginPx = qMax(1, qRound(kColorPickerMarginPixels * maskPixelsPerDip));
    const int outLeft = std::max(0, rimMinX - marginPx);
    const int outTop = std::max(0, rimMinY - marginPx);
    const int outRight = std::min(canvasPx - 1, rimMaxX + marginPx);
    const int outBottom = std::min(canvasPx - 1, rimMaxY + marginPx);
    const int finalWidth = (outRight - outLeft + supersample) / supersample;
    const int finalHeight = (outBottom - outTop + supersample) / supersample;
    if (finalWidth <= 0 || finalHeight <= 0) {
        return QCursor(Qt::CrossCursor);
    }

    // Average the mask composite into one output pixel per physical screen
    // pixel, then tag the image with the screen's device pixel ratio.
    const double fillR = kColorPickerFillRgb / 255.0;
    const double fillG = kColorPickerFillRgb / 255.0;
    const double fillB = kColorPickerFillRgb / 255.0;
    QImage cursorImage(finalWidth, finalHeight, QImage::Format_RGBA8888);
    cursorImage.fill(0);
    for (int cellY = 0; cellY < finalHeight; ++cellY) {
        const int rowBegin = outTop + cellY * supersample;
        const int rowEnd = std::min(outBottom, rowBegin + supersample - 1);
        uchar* outRow = cursorImage.scanLine(cellY);
        for (int cellX = 0; cellX < finalWidth; ++cellX) {
            const int colBegin = outLeft + cellX * supersample;
            const int colEnd = std::min(outRight, colBegin + supersample - 1);
            double sumR = 0.0;
            double sumG = 0.0;
            double sumB = 0.0;
            double sumA = 0.0;
            int sampleCount = 0;
            for (int y = rowBegin; y <= rowEnd; ++y) {
                const uchar* alphaRow = glyphAlpha.constScanLine(y);
                const uchar* outlineRow = outlineMask.constScanLine(y);
                for (int x = colBegin; x <= colEnd; ++x) {
                    double premulR = 0.0;
                    double premulG = 0.0;
                    double premulB = 0.0;
                    double alpha = 0.0;
                    if (outlineRow[x] > 0) {
                        premulR = 1.0;
                        premulG = 1.0;
                        premulB = 1.0;
                        alpha = 1.0;
                    }
                    const double glyphAlphaValue = alphaRow[x] / 255.0;
                    if (glyphAlphaValue > 0.0) {
                        premulR = fillR * glyphAlphaValue + premulR * (1.0 - glyphAlphaValue);
                        premulG = fillG * glyphAlphaValue + premulG * (1.0 - glyphAlphaValue);
                        premulB = fillB * glyphAlphaValue + premulB * (1.0 - glyphAlphaValue);
                        alpha = glyphAlphaValue + alpha * (1.0 - glyphAlphaValue);
                    }
                    sumR += premulR;
                    sumG += premulG;
                    sumB += premulB;
                    sumA += alpha;
                    ++sampleCount;
                }
            }
            if (sampleCount == 0 || sumA <= 0.0) {
                continue;
            }
            const double inverseAlpha = 1.0 / sumA;
            uchar* pixel = outRow + cellX * 4;
            pixel[0] = static_cast<uchar>(std::lround(sumR * inverseAlpha * 255.0));
            pixel[1] = static_cast<uchar>(std::lround(sumG * inverseAlpha * 255.0));
            pixel[2] = static_cast<uchar>(std::lround(sumB * inverseAlpha * 255.0));
            pixel[3] = static_cast<uchar>(std::lround(sumA / sampleCount * 255.0));
        }
    }

    cursorImage.setDevicePixelRatio(deviceScale);
    const QPixmap cursorPixmap = QPixmap::fromImage(cursorImage);
    const int logicalWidth = qMax(1, qCeil(finalWidth / deviceScale));
    const int logicalHeight = qMax(1, qCeil(finalHeight / deviceScale));
    int hotX = qRound((tipX - outLeft) / maskPixelsPerDip);
    int hotY = qRound((tipY - outTop) / maskPixelsPerDip);
    hotX = qBound(0, hotX, logicalWidth - 1);
    hotY = qBound(0, hotY, logicalHeight - 1);
    return QCursor(cursorPixmap, hotX, hotY);
}

/// Returns the shared eyedropper cursor, rebuilt only when the device pixel
/// ratio of the screen the canvas lives on changes.
QCursor colorPickerCursor(qreal devicePixelRatio) {
    static QCursor s_cursor;
    static qreal s_devicePixelRatio = -1.0;
    if (qAbs(s_devicePixelRatio - devicePixelRatio) > 0.001) {
        s_cursor = buildColorPickerCursor(devicePixelRatio);
        s_devicePixelRatio = devicePixelRatio;
    }
    return s_cursor;
}
} // namespace

MangaCanvas::MangaCanvas(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setDragMode(ScrollHandDrag);
    setRenderHint(QPainter::Antialiasing, true);
    setRenderHint(QPainter::TextAntialiasing, true);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setMouseTracking(true);
    setAccessibleName(tr("漫画画布"));
    setFocusPolicy(Qt::StrongFocus);

    m_placeholderCard = new fluent::layout::Card(viewport());
    m_placeholderCard->setObjectName(QStringLiteral("canvasEmptyStateCard"));
    m_placeholderCard->setAppearance(fluent::layout::Card::LayerAlt);
    m_placeholderCard->setBorderVisible(false);
    m_placeholderCard->setFixedWidth(280);
    auto* placeholderLayout = new QVBoxLayout(m_placeholderCard);
    placeholderLayout->setContentsMargins(24, 20, 24, 20);
    placeholderLayout->setSpacing(8);

    m_placeholderIcon = new fluent::textfields::Label(m_placeholderCard);
    m_placeholderIcon->setFont(QFont(Typography::FontFamily::FluentIcons, 32));
    m_placeholderIcon->setText(Typography::Icons::Folder);
    m_placeholderIcon->setAlignment(Qt::AlignCenter);
    placeholderLayout->addWidget(m_placeholderIcon);

    m_placeholderTitle = new fluent::textfields::Label(tr("尚未打开项目"), m_placeholderCard);
    m_placeholderTitle->setFluentTypography(Typography::FontRole::Subtitle);
    m_placeholderTitle->setAlignment(Qt::AlignCenter);
    placeholderLayout->addWidget(m_placeholderTitle);

    m_placeholderDescription = new fluent::textfields::Label(m_placeholderCard);
    m_placeholderDescription->setFluentTypography(Typography::FontRole::Body);
    m_placeholderDescription->setTextColorRole(fluent::textfields::Label::TextColorRole::Secondary);
    m_placeholderDescription->setWordWrap(true);
    m_placeholderDescription->setAlignment(Qt::AlignCenter);
    placeholderLayout->addWidget(m_placeholderDescription);

    m_placeholderAction = new fluent::basicinput::Button(tr("打开项目"), m_placeholderCard);
    m_placeholderAction->setFluentStyle(fluent::basicinput::Button::Accent);
    m_placeholderAction->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    m_placeholderAction->setAccessibleName(tr("打开项目"));
    placeholderLayout->addWidget(m_placeholderAction, 0, Qt::AlignHCenter);
    m_placeholderCard->hide();
    connect(m_placeholderAction, &fluent::basicinput::Button::clicked, this, &MangaCanvas::placeholderActionActivated);

    const auto& colors = themeColorsRef();
    m_backgroundColor = effectiveTheme() == Dark ? kDarkDocumentWorkspaceBackground : colors.bgCanvas;
    m_placeholderTextColor = colors.textPrimary;
    setBackgroundBrush(m_backgroundColor);

    m_creationBox = new QGraphicsRectItem();
    m_creationBox->setZValue(canvas::kZToolCreation);
    QPen creationPen(colors.accentDefault, 0);
    creationPen.setCosmetic(true);
    creationPen.setStyle(Qt::DashLine);
    m_creationBox->setPen(creationPen);
    QColor creationFill = colors.accentDefault;
    creationFill.setAlpha(40);
    m_creationBox->setBrush(creationFill);
    m_creationBox->setVisible(false);
    m_scene->addItem(m_creationBox);

    applyThemeColorsToItems();
    updateAccessibleDescription();
}

MangaCanvas::~MangaCanvas() = default;

void MangaCanvas::onThemeUpdated() {
    const auto& colors = themeColorsRef();
    m_backgroundColor = effectiveTheme() == Dark ? kDarkDocumentWorkspaceBackground : colors.bgCanvas;
    m_placeholderTextColor = colors.textPrimary;
    setBackgroundBrush(m_backgroundColor);
    applyThemeColorsToItems();
    if (viewport()) {
        viewport()->update();
    }
}

void MangaCanvas::setPageImage(const QImage& image) {
    if (m_gesture.active) {
        cancelGesture();
    }
    cancelRemoveStroke();
    m_pagePixmap = QPixmap::fromImage(image);
    if (!m_pageItem) {
        m_pageItem = new QGraphicsPixmapItem();
        m_pageItem->setZValue(canvas::kZPageOriginal);
        m_scene->addItem(m_pageItem);
    }
    m_pageItem->setPixmap(m_pagePixmap);
    m_pageItem->setVisible(true);

    m_pageItem->setPos(0, 0);
    if (m_clearItem) {
        m_clearItem->setPos(0, 0);
    }
    if (m_overrideItem) {
        m_overrideItem->setPos(0, 0);
    }
    if (m_translationItem && m_previewMode == 2) {
        m_translationItem->setPos(m_pagePixmap.width() + 32.0, 0);
    }
    updateNavigationSceneRect();
    setContentStateInternal(ContentState::Image, QString(), QString(), false);
}

void MangaCanvas::setClearImage(const QImage& image) {
    m_clearPixmap = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    if (!m_clearItem) {
        m_clearItem = new QGraphicsPixmapItem();
        m_clearItem->setZValue(canvas::kZClearLayer);
        m_scene->addItem(m_clearItem);
    }
    m_clearItem->setPixmap(m_clearPixmap);
    m_clearItem->setPos(0, 0);
    m_clearItem->setVisible(!m_clearPixmap.isNull());
    if (viewport()) {
        viewport()->update();
    }
}

void MangaCanvas::setManualOverrideImage(const QImage& image, const QString& ref) {
    m_overrideImage = image;
    m_overrideRef = ref;
    if (m_strokeDraftActive) {
        // The draft stays on screen; settleStrokeDraft reveals this layer.
        return;
    }
    syncOverrideItem();
}

void MangaCanvas::syncOverrideItem() {
    if (!m_overrideItem) {
        m_overrideItem = new OverrideLayerItem();
        m_overrideItem->setZValue(canvas::kZManualOverrideLayer);
        m_scene->addItem(m_overrideItem);
    }
    const QImage* source = m_strokeDraftActive ? &m_strokeDraft : &m_overrideImage;
    m_overrideItem->setSource(source);
    m_overrideItem->setPos(0, 0);
    const bool visible =
        m_strokeDraftActive ? !m_strokeDraft.isNull() : (!m_overrideImage.isNull() && !m_overrideRef.isEmpty());
    m_overrideItem->setVisible(visible);
    if (viewport()) {
        viewport()->update();
    }
}

void MangaCanvas::beginStrokeDraft(rendering::StrokePaintMode mode, const QColor& color, double diameter) {
    if (m_pageBounds.width <= 0 || m_pageBounds.height <= 0 || diameter <= 0.0) {
        return;
    }
    if (m_overrideImage.isNull()) {
        m_strokeDraft = QImage(m_pageBounds.width, m_pageBounds.height, QImage::Format_RGBA8888);
        m_strokeDraft.fill(Qt::transparent);
    } else {
        m_strokeDraft = m_overrideImage.convertToFormat(QImage::Format_RGBA8888);
        // Force the scratch copy to own its buffer now, so the first segment write does not
        // detach mid-gesture and the buffer address stays stable for the whole draft.
        m_strokeDraft.detach();
    }
    m_strokeDraftParams = rendering::StrokePaintParams{mode, color, diameter};
    m_strokeDraftBaseRef = m_overrideRef;
    m_strokeDirtyRect = QRect();
    m_strokeLastPoint = QPointF();
    m_strokeDraftActive = true;
    syncOverrideItem();
}

void MangaCanvas::extendStrokeDraft(const QPointF& point) {
    if (!m_strokeDraftActive || m_strokeDraft.isNull()) {
        return;
    }
    const QPointF from = m_strokeLastPoint.isNull() ? point : m_strokeLastPoint;
    const QRect bounds = rendering::StrokePaintRasterizer::segmentBounds(
        m_strokeDraft.size(), from, point, m_strokeDraftParams.diameter);
    rendering::StrokePaintRasterizer::rasterizeSegment(m_strokeDraft, m_strokeDraftParams, from, point);
    m_strokeLastPoint = point;
    if (!bounds.isEmpty()) {
        m_strokeDirtyRect = m_strokeDirtyRect.isEmpty() ? bounds : m_strokeDirtyRect.united(bounds);
        if (m_overrideItem) {
            m_overrideItem->update(bounds);
        }
    }
}

void MangaCanvas::finishStrokeDraft() {
    if (!m_strokeDraftActive || m_strokeDraft.isNull() || m_strokeLastPoint.isNull()) {
        return;
    }
    rendering::StrokePaintRasterizer::rasterizeCap(m_strokeDraft, m_strokeDraftParams, m_strokeLastPoint);
    if (m_overrideItem) {
        m_overrideItem->update(rendering::StrokePaintRasterizer::segmentBounds(
            m_strokeDraft.size(), m_strokeLastPoint, m_strokeLastPoint, m_strokeDraftParams.diameter));
    }
}

void MangaCanvas::settleStrokeDraft() {
    if (!m_strokeDraftActive) {
        return;
    }
    m_strokeDraftActive = false;
    m_strokeDraft = QImage();
    m_strokeDirtyRect = QRect();
    m_strokeLastPoint = QPointF();
    m_strokeDraftBaseRef.clear();
    syncOverrideItem();
}

void MangaCanvas::setTranslatedPreview(const QImage& image) {
    m_translationPixmap = image.isNull() ? QPixmap() : QPixmap::fromImage(image);
    if (!m_translationItem) {
        m_translationItem = new QGraphicsPixmapItem();
        m_translationItem->setZValue(canvas::kZTextLayer);
        m_scene->addItem(m_translationItem);
    }
    m_translationItem->setPixmap(m_translationPixmap);
    const qreal gap = 32.0;
    const bool compare = m_previewMode == 2;
    if (m_pageItem == nullptr) {
        m_translationItem->setVisible(false);
        syncTextItemVisibility();
        return;
    }
    m_pageItem->setPos(0, 0);
    m_translationItem->setPos(compare ? m_pagePixmap.width() + gap : 0, 0);
    m_translationItem->setVisible(m_previewMode != 0 && !m_translationPixmap.isNull());
    updateNavigationSceneRect();
    syncTextItemVisibility();
}

void MangaCanvas::setPreviewMode(int mode) {
    if (m_previewMode == mode) {
        syncTextItemVisibility();
        return;
    }
    m_previewMode = mode;
    if (m_translationItem && m_pageItem) {
        const qreal gap = 32.0;
        const bool compare = mode == 2;
        m_pageItem->setPos(0, 0);
        m_translationItem->setPos(compare ? m_pagePixmap.width() + gap : 0, 0);
        m_translationItem->setVisible(mode != 0 && !m_translationPixmap.isNull());
        updateNavigationSceneRect();
    }
    syncTextItemVisibility();
}

void MangaCanvas::clearPageImage() {
    m_hasLastWindowPos = false;
    if (m_gesture.active) {
        cancelGesture();
    }
    cancelRemoveStroke();
    m_pagePixmap = QPixmap();
    m_clearPixmap = QPixmap();
    m_overrideImage = QImage();
    m_overrideRef.clear();
    m_translationPixmap = QPixmap();
    if (m_pageItem) {
        m_pageItem->setPixmap(m_pagePixmap);
        m_pageItem->setVisible(false);
    }
    if (m_clearItem) {
        m_clearItem->setPixmap(QPixmap());
        m_clearItem->setVisible(false);
    }
    if (m_overrideItem) {
        m_overrideItem->setSource(nullptr);
        m_overrideItem->setVisible(false);
    }
    if (m_translationItem) {
        m_translationItem->setPixmap(QPixmap());
        m_translationItem->setVisible(false);
    }
    // Keep the reconciled displays so the next state reconciliation can reveal them again,
    // but never allow text from the previous page to remain visible while clearing.
    for (auto it = m_textItems.cbegin(); it != m_textItems.cend(); ++it) {
        it.value()->setVisible(false);
    }
}

void MangaCanvas::setContentStateInternal(ContentState state,
                                          const QString& message,
                                          const QString& actionText,
                                          bool clearImage) {
    m_contentState = state;
    m_placeholderMessage = message;

    if (clearImage) {
        clearPageImage();
    }

    if (!actionText.isEmpty()) {
        const QString title = state == ContentState::NoProject ? tr("尚未打开项目")
                              : state == ContentState::Error   ? tr("页面加载失败")
                                                               : tr("没有可显示的页面");
        m_placeholderIcon->setText(state == ContentState::Error
                                       ? Typography::Icons::glyph(QStringLiteral("ic_fluent_error_circle_20_filled"))
                                       : Typography::Icons::Folder);
        m_placeholderTitle->setText(title);
        m_placeholderDescription->setText(m_placeholderMessage);
        m_placeholderAction->setText(actionText);
        m_placeholderAction->setAccessibleName(actionText);
        m_placeholderCard->show();
        m_placeholderCard->raise();
    } else {
        m_placeholderCard->hide();
    }

    layoutPlaceholderButton();
    if (viewport()) {
        viewport()->update();
    }
}

void MangaCanvas::setContentState(ContentState state, const QString& message, const QString& actionText) {
    bool shouldClear = (state == ContentState::Loading || state == ContentState::NoProject ||
                        state == ContentState::NoPage || state == ContentState::Error);
    setContentStateInternal(state, message, actionText, shouldClear);
}

void MangaCanvas::setZoomFactor(double factor) {
    applyZoom(factor);
}

void MangaCanvas::applyZoom(double factor) {
    factor = std::clamp(factor, kMinZoom, kMaxZoom);
    if (qFuzzyCompare(m_zoomFactor, factor)) {
        return;
    }

    m_zoomFactor = factor;
    setTransformationAnchor(QGraphicsView::AnchorViewCenter);
    setTransform(QTransform::fromScale(m_zoomFactor, m_zoomFactor));
    updateNavigationSceneRect();
    updateRemoveCursor();

    Q_EMIT zoomChanged(m_zoomFactor);
    emitViewTransform();
}

QRectF MangaCanvas::contentSceneRect() const {
    QRectF content;
    if (!m_pagePixmap.isNull()) {
        content = QRectF(0.0, 0.0, m_pagePixmap.width(), m_pagePixmap.height());
    }

    if (m_previewMode != 0 && !m_translationPixmap.isNull()) {
        const qreal translationX = m_previewMode == 2 ? m_pagePixmap.width() + 32.0 : 0.0;
        const QRectF translationRect(translationX, 0.0, m_translationPixmap.width(), m_translationPixmap.height());
        content = content.isEmpty() ? translationRect : content.united(translationRect);
    }
    return content;
}

void MangaCanvas::updateNavigationSceneRect(const QPointF& anchorScenePoint, const QPointF& targetViewportPos) {
    const QRectF content = contentSceneRect();
    if (content.isEmpty() || viewport() == nullptr) {
        m_scene->setSceneRect(content);
        return;
    }

    const QPointF preservedCenter = anchorScenePoint.isNull() ? viewCenterOnPage() : anchorScenePoint;
    const double viewportSceneWidth = static_cast<double>(viewport()->width()) / m_zoomFactor;
    const double viewportSceneHeight = static_cast<double>(viewport()->height()) / m_zoomFactor;
    const double minVisibleSceneWidth = std::min(content.width(), kMinVisibleContentViewportPx / m_zoomFactor);
    const double minVisibleSceneHeight = std::min(content.height(), kMinVisibleContentViewportPx / m_zoomFactor);
    const double horizontalMargin = std::max(0.0, viewportSceneWidth - minVisibleSceneWidth);
    const double verticalMargin = std::max(0.0, viewportSceneHeight - minVisibleSceneHeight);
    const QRectF navigationRect =
        content.adjusted(-horizontalMargin, -verticalMargin, horizontalMargin, verticalMargin);
    if (m_scene->sceneRect() == navigationRect) {
        return;
    }
    m_scene->setSceneRect(navigationRect);
    if (!targetViewportPos.isNull()) {
        anchorScenePointOnViewport(preservedCenter, targetViewportPos);
    } else {
        centerScenePointOnViewport(preservedCenter);
    }
}

void MangaCanvas::zoomIn() {
    for (double level : kZoomLevels) {
        if (level > m_zoomFactor + 0.001) {
            applyZoom(level);
            return;
        }
    }
    applyZoom(kMaxZoom);
}

void MangaCanvas::zoomOut() {
    for (int i = std::size(kZoomLevels) - 1; i >= 0; --i) {
        if (kZoomLevels[i] < m_zoomFactor - 0.001) {
            applyZoom(kZoomLevels[i]);
            return;
        }
    }
    applyZoom(kMinZoom);
}

void MangaCanvas::fitToPage() {
    if (!m_pageItem || m_pagePixmap.isNull()) {
        return;
    }
    const QRectF targetRect = contentSceneRect();
    if (targetRect.isEmpty()) {
        return;
    }

    double scaleX = viewport()->width() / targetRect.width();
    double scaleY = viewport()->height() / targetRect.height();
    double scale = std::min(scaleX, scaleY);

    applyZoom(scale);
    centerOn(targetRect.center());
    emitViewTransform();
}

void MangaCanvas::actualSize() {
    applyZoom(1.0);
}

QPointF MangaCanvas::viewCenterOnPage() const {
    return mapToScene(viewport()->rect().center());
}

void MangaCanvas::anchorScenePointOnViewport(const QPointF& scenePoint, const QPointF& targetViewportPos) {
    const QPointF viewPoint = transform().map(scenePoint);
    horizontalScrollBar()->setValue(qRound(viewPoint.x() - targetViewportPos.x()));
    verticalScrollBar()->setValue(qRound(viewPoint.y() - targetViewportPos.y()));
}

void MangaCanvas::centerScenePointOnViewport(const QPointF& scenePoint) {
    anchorScenePointOnViewport(scenePoint, viewport()->rect().center());
}

void MangaCanvas::centerOnPage(const QPointF& scenePos) {
    centerOn(scenePos);
    emitViewTransform();
}

void MangaCanvas::setTool(canvas::Tool tool) {
    if (m_tool != tool) {
        if (m_gesture.active) {
            cancelGesture();
        }
        cancelRemoveStroke();
    }
    m_tool = tool;
    if (m_spaceHeld) {
        setDragMode(ScrollHandDrag);
        return;
    }
    switch (tool) {
        case canvas::Tool::Pan:
            setDragMode(ScrollHandDrag);
            break;
        case canvas::Tool::Select:
            setDragMode(NoDrag);
            break;
        case canvas::Tool::Text:
            setDragMode(NoDrag);
            break;
        case canvas::Tool::Remove:
        case canvas::Tool::Brush:
        case canvas::Tool::Eraser:
        case canvas::Tool::Eyedropper:
            setDragMode(NoDrag);
            updateRemoveCursor();
            break;
    }
}

void MangaCanvas::setBrushColor(const QColor& color) {
    if (m_brushColor != color) {
        m_brushColor = color;
        Q_EMIT brushColorChanged(m_brushColor);
        if (m_tool == canvas::Tool::Brush) {
            viewport()->update();
        }
    }
}

void MangaCanvas::setRegions(const QVector<canvas::RegionDisplay>& regions) {
    QSet<domain::RegionId> incomingIds;
    incomingIds.reserve(regions.size());
    for (const auto& rd : regions) {
        incomingIds.insert(rd.id);
    }
    if (m_gesture.active) {
        for (const auto& target : m_gesture.targets) {
            if (target.id.isValid() && !incomingIds.contains(target.id)) {
                cancelGesture();
                break;
            }
        }
    }

    int selectedCount = 0;
    for (const auto& rd : regions) {
        if (rd.selected) {
            ++selectedCount;
        }
    }
    const bool singleSelected = (selectedCount == 1);

    int order = 1;
    for (const auto& rd : regions) {
        auto it = m_overlays.find(rd.id);
        if (it != m_overlays.end()) {
            std::optional<SourceRectGeometry> transientGeometry;
            if (m_gesture.active) {
                for (const auto& target : m_gesture.targets) {
                    if (target.id == rd.id) {
                        transientGeometry = it.value()->currentGeometry();
                        break;
                    }
                }
            }
            it.value()->updateFromDisplay(rd, singleSelected);
            if (transientGeometry.has_value()) {
                it.value()->setTransientGeometry(*transientGeometry);
            }
            it.value()->setOrder(order);
        } else {
            auto* item = new RegionOverlayItem(rd.id);
            item->updateFromDisplay(rd, singleSelected);
            item->setOrder(order);
            const auto& colors = themeColorsRef();
            item->setColors(colors.accentDefault, colors.strokeDefault, colors.accentDefault);
            m_scene->addItem(item);
            m_overlays.insert(rd.id, item);
        }
        ++order;
    }

    auto it = m_overlays.begin();
    while (it != m_overlays.end()) {
        if (!incomingIds.contains(it.key())) {
            m_scene->removeItem(it.value());
            delete it.value();
            it = m_overlays.erase(it);
        } else {
            ++it;
        }
    }

    updateAccessibleDescription();
}

void MangaCanvas::setTextLayers(const QVector<TextLayerDisplay>& layers) {
    QSet<domain::RegionId> incomingIds;
    incomingIds.reserve(layers.size());
    for (const auto& display : layers) {
        incomingIds.insert(display.id);
        auto it = m_textItems.find(display.id);
        if (it != m_textItems.end()) {
            it.value()->setDisplay(display);
        } else {
            auto* item = new MangaTextItem(display.id);
            item->setAcceptedMouseButtons(Qt::NoButton);
            item->setFlag(QGraphicsItem::ItemIsSelectable, false);
            item->setFlag(QGraphicsItem::ItemIsFocusable, false);
            item->setZValue(canvas::kZTextLayer);
            item->setDisplay(display);
            m_scene->addItem(item);
            m_textItems.insert(display.id, item);
        }
    }

    auto it = m_textItems.begin();
    while (it != m_textItems.end()) {
        if (!incomingIds.contains(it.key())) {
            m_scene->removeItem(it.value());
            delete it.value();
            it = m_textItems.erase(it);
        } else {
            ++it;
        }
    }
    syncTextItemVisibility();
}

void MangaCanvas::syncTextItemVisibility() {
    const bool translated = m_previewMode == 1;
    for (auto it = m_textItems.cbegin(); it != m_textItems.cend(); ++it) {
        auto* item = it.value();
        const auto& display = item->display();
        item->setVisible(translated && display.enabled && display.visible && !display.text.isEmpty());
    }
    if (m_translationItem != nullptr) {
        m_translationItem->setVisible(m_previewMode != 0 && !m_translationPixmap.isNull() && m_previewMode != 1);
    }
}

QWidget* MangaCanvas::placeholderActionButtonForTest() const {
    return m_placeholderAction;
}

bool MangaCanvas::hasPageImageForTest() const {
    return !m_pagePixmap.isNull();
}

int MangaCanvas::regionOverlayCountForTest() const {
    return m_overlays.size();
}

bool MangaCanvas::hasStrokeDraftForTest() const noexcept {
    return m_strokeDraftActive;
}

const uchar* MangaCanvas::strokeDraftBitsForTest() const noexcept {
    return m_strokeDraft.isNull() ? nullptr : m_strokeDraft.constBits();
}

QSize MangaCanvas::strokeDraftSizeForTest() const noexcept {
    return m_strokeDraft.size();
}

QString MangaCanvas::strokeDraftBaseRefForTest() const {
    return m_strokeDraftBaseRef;
}

QImage MangaCanvas::authoritativeOverrideForTest() const {
    return m_overrideImage;
}

QImage MangaCanvas::clearImageForTest() const {
    return m_clearPixmap.toImage();
}

RegionOverlayItem* MangaCanvas::overlayItemAtScenePos(const QPointF& scenePos) const {
    const QList<QGraphicsItem*> items = m_scene->items(scenePos);
    for (auto* item : items) {
        if (auto* overlay = dynamic_cast<RegionOverlayItem*>(item)) {
            return overlay;
        }
    }
    return nullptr;
}

void MangaCanvas::cancelGesture() {
    if (!m_gesture.active) {
        return;
    }

    if (m_gesture.activeHandle.has_value() || !m_gesture.targets.isEmpty()) {
        for (const auto& target : m_gesture.targets) {
            auto it = m_overlays.find(target.id);
            if (it != m_overlays.end()) {
                it.value()->setTransientGeometry(target.startGeometry);
            }
            syncTextItemGeometry(target.id, target.startGeometry);
        }
    }

    if (m_creationBox) {
        m_creationBox->setVisible(false);
    }

    m_gesture = {};
    Q_EMIT gestureCancelled();
}

void MangaCanvas::cancelRemoveStroke() {
    // A cancelled gesture also drops any stroke draft; discarding the draft is the rollback.
    settleStrokeDraft();
    if (!m_removeStrokeActive && m_removeStroke.isEmpty()) {
        return;
    }
    m_removeStroke.clear();
    m_removeStrokeActive = false;
    viewport()->update();
}

void MangaCanvas::syncTextItemGeometry(SourceRegionId id, const SourceRectGeometry& geometry) {
    const auto it = m_textItems.constFind(id);
    if (it != m_textItems.constEnd() && it.value() != nullptr) {
        it.value()->setTransientGeometry(geometry);
    }
}

void MangaCanvas::updateRemoveCursor() {
    if (!viewport()) {
        return;
    }
    if (m_spaceHeld) {
        viewport()->unsetCursor();
        return;
    }
    if (m_tool == canvas::Tool::Eyedropper) {
        viewport()->setCursor(colorPickerCursor(viewport()->devicePixelRatioF()));
        return;
    }
    if (m_tool != canvas::Tool::Remove && m_tool != canvas::Tool::Brush && m_tool != canvas::Tool::Eraser) {
        viewport()->unsetCursor();
        return;
    }
    viewport()->setCursor(Qt::BlankCursor);
}

QColor MangaCanvas::sampleContentColor(const QPointF& scenePos) const {
    // Sample the visible pixmap layers from bottom to top and alpha-composite them so the
    // returned color matches what the user sees. The clear, override, and compare preview
    // pixmaps are sparse alpha overlays over the original page, so a transparent pixel must
    // reveal the layer beneath it instead of being reported as the sampled color.
    struct Layer {
        const QGraphicsItem* item;
        const QPixmap* pixmap{nullptr};
        const QImage* image{nullptr};
    };
    const Layer layers[] = {
        {m_pageItem, &m_pagePixmap, nullptr},
        {m_clearItem, &m_clearPixmap, nullptr},
        {m_overrideItem, nullptr, m_overrideItem ? m_overrideItem->source() : nullptr},
        {m_translationItem, &m_translationPixmap, nullptr},
    };
    auto layerPixel = [&scenePos](const Layer& layer, QColor& outColor) {
        if (layer.item == nullptr || !layer.item->isVisible()) {
            return false;
        }
        int width = 0;
        int height = 0;
        if (layer.pixmap != nullptr && !layer.pixmap->isNull()) {
            width = layer.pixmap->width();
            height = layer.pixmap->height();
        } else if (layer.image != nullptr && !layer.image->isNull()) {
            width = layer.image->width();
            height = layer.image->height();
        } else {
            return false;
        }
        const QPointF local = scenePos - layer.item->pos();
        const QPoint pixel(static_cast<int>(std::floor(local.x())), static_cast<int>(std::floor(local.y())));
        if (pixel.x() < 0 || pixel.y() < 0 || pixel.x() >= width || pixel.y() >= height) {
            return false;
        }
        outColor = layer.image != nullptr ? layer.image->pixelColor(pixel) : layer.pixmap->toImage().pixelColor(pixel);
        return true;
    };

    QColor accumulated;
    for (const auto& layer : layers) {
        QColor top;
        if (!layerPixel(layer, top)) {
            continue;
        }
        const double topAlpha = top.alphaF();
        if (topAlpha <= 0.0) {
            continue;
        }
        if (!accumulated.isValid()) {
            accumulated = top;
            continue;
        }
        if (topAlpha >= 1.0) {
            accumulated = top;
            continue;
        }
        const double bottomAlpha = accumulated.alphaF();
        const double outAlpha = topAlpha + bottomAlpha * (1.0 - topAlpha);
        if (outAlpha <= 0.0) {
            continue;
        }
        QColor blended;
        blended.setRgbF((top.redF() * topAlpha + accumulated.redF() * bottomAlpha * (1.0 - topAlpha)) / outAlpha,
                        (top.greenF() * topAlpha + accumulated.greenF() * bottomAlpha * (1.0 - topAlpha)) / outAlpha,
                        (top.blueF() * topAlpha + accumulated.blueF() * bottomAlpha * (1.0 - topAlpha)) / outAlpha,
                        outAlpha);
        accumulated = blended;
    }
    if (accumulated.isValid()) {
        accumulated.setAlpha(255);
    }
    return accumulated;
}

void MangaCanvas::setRemoveDiameter(double diameter) {
    const double clamped = std::clamp(diameter, kMinRemoveDiameter, kMaxRemoveDiameter);
    if (qFuzzyCompare(m_removeDiameter, clamped)) {
        return;
    }
    const double oldRadius = m_removeDiameter * 0.5;
    m_removeDiameter = clamped;
    const double newRadius = m_removeDiameter * 0.5;
    const double maxRadius = std::max(oldRadius, newRadius);
    const double margin = 4.0 / std::max(0.01, m_zoomFactor);
    updateRemoveCursor();
    const QRectF cursorRect(m_currentMouseScenePos.x() - maxRadius - margin,
                            m_currentMouseScenePos.y() - maxRadius - margin,
                            (maxRadius + margin) * 2.0,
                            (maxRadius + margin) * 2.0);
    viewport()->update(mapFromScene(cursorRect).boundingRect());
    Q_EMIT removeDiameterChanged(m_removeDiameter);
}

void MangaCanvas::applyThemeColorsToItems() {
    const auto& colors = themeColorsRef();

    if (m_creationBox) {
        QPen creationPen(colors.accentDefault, 0);
        creationPen.setCosmetic(true);
        creationPen.setStyle(Qt::DashLine);
        m_creationBox->setPen(creationPen);
        QColor fill = colors.accentDefault;
        fill.setAlpha(40);
        m_creationBox->setBrush(fill);
    }

    for (auto it = m_overlays.constBegin(); it != m_overlays.constEnd(); ++it) {
        it.value()->setColors(colors.accentDefault, colors.strokeDefault, colors.accentDefault);
    }
}

void MangaCanvas::wheelEvent(QWheelEvent* event) {
    const auto modifiers = event->modifiers() | QGuiApplication::keyboardModifiers();
    const bool isAlt = (modifiers & Qt::AltModifier) != 0;
    const bool isBrushTool =
        (m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser);

    if (isAlt && isBrushTool && !m_spaceHeld) {
        int delta = event->angleDelta().y();
        if (delta == 0) {
            delta = event->angleDelta().x();
        }
        if (delta == 0) {
            delta = event->pixelDelta().y();
        }
        if (delta == 0) {
            delta = event->pixelDelta().x();
        }
        if (delta != 0) {
            const double step = (m_removeDiameter < 20.0) ? 2.0 : ((m_removeDiameter < 60.0) ? 4.0 : 8.0);
            const double direction = delta > 0 ? 1.0 : -1.0;
            setRemoveDiameter(m_removeDiameter + direction * step);
        }
        event->accept();
        return;
    }
    if (modifiers & Qt::ControlModifier) {
        double delta = event->angleDelta().y();
        if (delta == 0) {
            delta = event->angleDelta().x();
        }
        if (delta != 0) {
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            double steps = delta / 120.0;
            double factor = m_zoomFactor * std::pow(1.25, steps);

            factor = std::clamp(factor, kMinZoom, kMaxZoom);
            if (!qFuzzyCompare(m_zoomFactor, factor)) {
                m_zoomFactor = factor;
                setTransform(QTransform::fromScale(m_zoomFactor, m_zoomFactor));
                updateRemoveCursor();
                updateNavigationSceneRect();
                Q_EMIT zoomChanged(m_zoomFactor);
                emitViewTransform();
            }
        }
        event->accept();
    } else {
        QGraphicsView::wheelEvent(event);
        if (m_contentState == ContentState::Image) {
            emitViewTransform();
        }
    }
}

void MangaCanvas::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat() && !m_spaceHeld) {
            m_spaceHeld = true;
            if (m_tool == canvas::Tool::Text && m_gesture.active) {
                cancelGesture();
            }
            cancelRemoveStroke();
            setDragMode(ScrollHandDrag);
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape && m_gesture.active) {
        cancelGesture();
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && m_removeStrokeActive) {
        cancelRemoveStroke();
        Q_EMIT gestureCancelled();
        event->accept();
        return;
    }

    if (m_tool == canvas::Tool::Select) {
        if (event->key() == Qt::Key_Delete) {
            bool emitted = false;
            for (auto it = m_overlays.constBegin(); it != m_overlays.constEnd(); ++it) {
                if (it.value()->isSelected()) {
                    Q_EMIT regionDeleteRequested(it.key());
                    emitted = true;
                }
            }
            if (emitted) {
                event->accept();
            }
            return;
        }

        if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
            if (m_overlays.isEmpty()) {
                event->accept();
                return;
            }
            QList<domain::RegionId> ids = m_overlays.keys();
            int currentIndex = -1;
            for (int i = 0; i < ids.size(); ++i) {
                if (m_overlays[ids[i]]->isSelected()) {
                    currentIndex = i;
                    break;
                }
            }

            int nextIndex;
            if (event->key() == Qt::Key_Backtab) {
                nextIndex = (currentIndex <= 0) ? ids.size() - 1 : currentIndex - 1;
            } else {
                nextIndex = (currentIndex >= ids.size() - 1) ? 0 : currentIndex + 1;
            }

            QVector<canvas::RegionDisplay> updated;
            for (auto it = m_overlays.constBegin(); it != m_overlays.constEnd(); ++it) {
                auto geom = it.value()->currentGeometry();
                auto bubble = it.value()->bubbleGeometry();
                auto bubblePoly = it.value()->bubblePolygon();
                bool sel = (it.key() == ids[nextIndex]);
                canvas::RegionDisplay display;
                display.id = it.key();
                display.geometry = geom;
                display.bubbleGeometry = bubble;
                display.bubblePolygon = bubblePoly;
                display.selected = sel;
                updated.append(display);
            }
            setRegions(updated);
            Q_EMIT regionSelectionRequested(ids[nextIndex], canvas::SelectionIntent::Replace);
            event->accept();
            return;
        }

        const bool shiftHeld = event->modifiers() & Qt::ShiftModifier;
        if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down || event->key() == Qt::Key_Left ||
            event->key() == Qt::Key_Right) {
            for (auto it = m_overlays.constBegin(); it != m_overlays.constEnd(); ++it) {
                if (!it.value()->isSelected()) {
                    continue;
                }

                const double step = 1.0;
                QPointF delta{0, 0};
                if (event->key() == Qt::Key_Up) {
                    delta.setY(-step);
                } else if (event->key() == Qt::Key_Down) {
                    delta.setY(step);
                } else if (event->key() == Qt::Key_Left) {
                    delta.setX(-step);
                } else {
                    delta.setX(step);
                }

                if (shiftHeld) {
                    const auto handle = arrowToPrimaryHandle(event->key());
                    const auto result =
                        canvas::resizeRegion(it.value()->currentGeometry(), handle, delta, m_pageBounds);
                    if (result.has_value() && !result->approxEquals(it.value()->currentGeometry())) {
                        it.value()->setTransientGeometry(*result);
                        syncTextItemGeometry(it.key(), *result);
                        Q_EMIT regionGeometryCommitted(it.key(), *result, canvas::EditKind::Resize);
                    }
                } else {
                    const auto newGeom = canvas::moveRegion(it.value()->currentGeometry(), delta, m_pageBounds);
                    if (!newGeom.approxEquals(it.value()->currentGeometry())) {
                        it.value()->setTransientGeometry(newGeom);
                        syncTextItemGeometry(it.key(), newGeom);
                        Q_EMIT regionGeometryCommitted(it.key(), newGeom, canvas::EditKind::Move);
                    }
                }
                event->accept();
                return;
            }
        }
    }

    if (m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser) {
        if (event->key() == Qt::Key_BracketLeft) {
            const double step = (m_removeDiameter <= 20.0) ? 2.0 : ((m_removeDiameter <= 60.0) ? 4.0 : 8.0);
            setRemoveDiameter(m_removeDiameter - step);
            event->accept();
            return;
        }
        if (event->key() == Qt::Key_BracketRight) {
            const double step = (m_removeDiameter < 20.0) ? 2.0 : ((m_removeDiameter < 60.0) ? 4.0 : 8.0);
            setRemoveDiameter(m_removeDiameter + step);
            event->accept();
            return;
        }
    }

    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal) {
        zoomIn();
        event->accept();
    } else if (event->key() == Qt::Key_Minus) {
        zoomOut();
        event->accept();
    } else if (event->key() == Qt::Key_0) {
        fitToPage();
        event->accept();
    } else if (event->key() == Qt::Key_1) {
        actualSize();
        event->accept();
    } else {
        QGraphicsView::keyPressEvent(event);
    }
}

void MangaCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Space) {
        if (!event->isAutoRepeat()) {
            m_spaceHeld = false;
            setTool(m_tool);
            updateRemoveCursor();
        }
        event->accept();
        return;
    }
    QGraphicsView::keyReleaseEvent(event);
}

void MangaCanvas::focusOutEvent(QFocusEvent* event) {
    if (m_spaceHeld) {
        m_spaceHeld = false;
        setTool(m_tool);
    }
    if (m_gesture.active) {
        cancelGesture();
    }
    if (m_removeStrokeActive) {
        cancelRemoveStroke();
    }
    QGraphicsView::focusOutEvent(event);
}

void MangaCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        cancelRemoveStroke();
        QGraphicsView::mousePressEvent(event);
        return;
    }

    if (m_tool == canvas::Tool::Pan || m_spaceHeld) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    const QPointF devicePt = event->position();
    const QPointF scenePt = mapToScene(event->position().toPoint());

    if (m_tool == canvas::Tool::Eyedropper) {
        const QColor color = sampleContentColor(scenePt);
        if (color.isValid()) {
            Q_EMIT colorPicked(color);
        }
        event->accept();
        return;
    }

    if (m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser) {
        const double pageWidth = static_cast<double>(m_pageBounds.width);
        const double pageHeight = static_cast<double>(m_pageBounds.height);
        const QPointF clampedPt{
            std::clamp(scenePt.x(), 0.0, std::max(0.0, pageWidth)),
            std::clamp(scenePt.y(), 0.0, std::max(0.0, pageHeight)),
        };
        m_removeStroke.clear();
        m_removeStroke.append(clampedPt);
        m_removeStrokeActive = true;
        if (m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser) {
            beginStrokeDraft(m_tool == canvas::Tool::Eraser ? rendering::StrokePaintMode::Erase
                                                            : rendering::StrokePaintMode::Paint,
                             m_brushColor,
                             m_removeDiameter);
            // Seed the segment chain at the press point. No cap is rasterized here: the first
            // segment already covers the press point with its round cap, and the whole-stroke
            // commit path adds a cap only at the stroke end, so an extra cap would double-cover
            // the start and break pixel identity with the committed layer.
            m_strokeLastPoint = clampedPt;
        }
        const double radius = m_removeDiameter * 0.5;
        const double margin = 4.0 / std::max(0.01, m_zoomFactor);
        const QRectF dotRect(clampedPt.x() - radius - margin,
                             clampedPt.y() - radius - margin,
                             (radius + margin) * 2.0,
                             (radius + margin) * 2.0);
        viewport()->update(mapFromScene(dotRect).boundingRect());
        event->accept();
        return;
    }

    if (m_tool == canvas::Tool::Text) {
        const QRectF pageRect(
            0.0, 0.0, static_cast<double>(m_pageBounds.width), static_cast<double>(m_pageBounds.height));
        if (!pageRect.contains(scenePt)) {
            event->ignore();
            return;
        }
        m_gesture.active = true;
        m_gesture.dragging = false;
        m_gesture.pressDevicePx = devicePt;
        m_gesture.pressSourcePx = scenePt;
        m_gesture.targets.clear();
        m_gesture.targets.append(
            {{}, {scenePt.x(), scenePt.y(), domain::RectGeometry::kMinDimension, domain::RectGeometry::kMinDimension}});
        m_gesture.activeHandle = std::nullopt;

        m_creationBox->setRect(
            scenePt.x(), scenePt.y(), domain::RectGeometry::kMinDimension, domain::RectGeometry::kMinDimension);
        m_creationBox->setVisible(true);
        event->accept();
        return;
    }

    // Select tool: check handle hit first across selected regions, then region body.
    const QList<QGraphicsItem*> itemsUnderMouse = m_scene->items(scenePt);
    for (auto* item : itemsUnderMouse) {
        if (auto* handleItem = dynamic_cast<SelectionHandleItem*>(item)) {
            if (auto* overlay = dynamic_cast<RegionOverlayItem*>(handleItem->parentItem())) {
                if (overlay->isSelected()) {
                    m_gesture.active = true;
                    m_gesture.dragging = false;
                    m_gesture.pressDevicePx = devicePt;
                    m_gesture.targets = {{overlay->regionId(), overlay->currentGeometry()}};
                    m_gesture.activeHandle = handleItem->handlePosition();
                    event->accept();
                    return;
                }
            }
        }
    }

    // Test region body click.
    auto* clickedOverlay = overlayItemAtScenePos(scenePt);
    if (clickedOverlay) {
        const auto modifiers = event->modifiers();
        auto intent = canvas::SelectionIntent::Replace;
        if (modifiers & Qt::ControlModifier) {
            intent = canvas::SelectionIntent::Toggle;
        } else if (modifiers & Qt::ShiftModifier) {
            intent = canvas::SelectionIntent::Range;
        }

        m_gesture.targets.clear();
        if (!(modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) && clickedOverlay->isSelected()) {
            for (auto selectedIt = m_overlays.constBegin(); selectedIt != m_overlays.constEnd(); ++selectedIt) {
                if (selectedIt.value()->isSelected()) {
                    m_gesture.targets.append({selectedIt.key(), selectedIt.value()->currentGeometry()});
                }
            }
        }
        if (m_gesture.targets.isEmpty()) {
            m_gesture.targets.append({clickedOverlay->regionId(), clickedOverlay->currentGeometry()});
        }

        Q_EMIT regionSelectionRequested(clickedOverlay->regionId(), intent);

        // Modifier clicks change selection membership or range and must not
        // also begin a move gesture.
        if (modifiers & (Qt::ControlModifier | Qt::ShiftModifier)) {
            m_gesture.targets.clear();
            event->accept();
            return;
        }

        // Start potential move gesture after taking the geometry snapshot.
        m_gesture.active = true;
        m_gesture.dragging = false;
        m_gesture.pressDevicePx = devicePt;
        m_gesture.activeHandle = std::nullopt;
        event->accept();
        return;
    }

    // Clicked empty space with Select tool: deselect all.
    if (m_tool == canvas::Tool::Select) {
        Q_EMIT regionSelectionRequested(domain::RegionId{}, canvas::SelectionIntent::Replace);
    }

    QGraphicsView::mousePressEvent(event);
}

void MangaCanvas::mouseMoveEvent(QMouseEvent* event) {
    const QPointF prevMouseScenePos = m_currentMouseScenePos;
    m_currentMouseScenePos = mapToScene(event->position().toPoint());

    if ((m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser) &&
        m_removeStrokeActive && !m_spaceHeld) {
        const double pageWidth = static_cast<double>(m_pageBounds.width);
        const double pageHeight = static_cast<double>(m_pageBounds.height);
        const QPointF scenePt = m_currentMouseScenePos;
        const QPointF clampedPt{
            std::clamp(scenePt.x(), 0.0, std::max(0.0, pageWidth)),
            std::clamp(scenePt.y(), 0.0, std::max(0.0, pageHeight)),
        };
        if (!m_removeStroke.isEmpty()) {
            const QPointF prev = m_removeStroke.last();
            const double dx = clampedPt.x() - prev.x();
            const double dy = clampedPt.y() - prev.y();
            if (std::hypot(dx, dy) < 0.25 / std::max(0.01, m_zoomFactor)) {
                event->accept();
                return;
            }
            m_removeStroke.append(clampedPt);
            if (m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser) {
                extendStrokeDraft(clampedPt);
            }
            const double radius = m_removeDiameter * 0.5;
            const double margin = 4.0 / std::max(0.01, m_zoomFactor);
            const QRectF segmentRect =
                QRectF(prev, clampedPt)
                    .normalized()
                    .adjusted(-radius - margin, -radius - margin, radius + margin, radius + margin);
            viewport()->update(mapFromScene(segmentRect).boundingRect());
            event->accept();
            return;
        }
        m_removeStroke.append(clampedPt);
        viewport()->update();
        event->accept();
        return;
    }

    if (!m_removeStrokeActive &&
        (m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser)) {
        const double radius = m_removeDiameter * 0.5;
        const double margin = 4.0 / std::max(0.01, m_zoomFactor);
        const QRectF oldRect(prevMouseScenePos.x() - radius - margin,
                             prevMouseScenePos.y() - radius - margin,
                             (radius + margin) * 2.0,
                             (radius + margin) * 2.0);
        const QRectF newRect(m_currentMouseScenePos.x() - radius - margin,
                             m_currentMouseScenePos.y() - radius - margin,
                             (radius + margin) * 2.0,
                             (radius + margin) * 2.0);
        viewport()->update(mapFromScene(oldRect | newRect).boundingRect());
    }

    if (!m_gesture.active) {
        if (m_tool == canvas::Tool::Select) {
            const QPointF scenePt = mapToScene(event->position().toPoint());
            auto* hovered = overlayItemAtScenePos(scenePt);
            const RegionId hoveredId = hovered != nullptr ? hovered->regionId() : RegionId{};
            if (m_hoveredRegionId != hoveredId) {
                if (auto previous = m_overlays.find(m_hoveredRegionId); previous != m_overlays.end()) {
                    previous.value()->setHovered(false);
                }
                m_hoveredRegionId = hoveredId;
                if (hovered != nullptr) {
                    hovered->setHovered(true);
                }
            }

            SelectionHandleItem* hoveredHandle = nullptr;
            const QList<QGraphicsItem*> itemsUnderMouse = m_scene->items(scenePt);
            for (auto* item : itemsUnderMouse) {
                if (auto* h = dynamic_cast<SelectionHandleItem*>(item)) {
                    if (auto* overlay = dynamic_cast<RegionOverlayItem*>(h->parentItem())) {
                        if (overlay->isSelected()) {
                            hoveredHandle = h;
                            break;
                        }
                    }
                }
            }

            if (hoveredHandle != nullptr) {
                switch (hoveredHandle->handlePosition()) {
                    case canvas::HandlePosition::NorthWest:
                    case canvas::HandlePosition::SouthEast:
                        viewport()->setCursor(Qt::SizeFDiagCursor);
                        break;
                    case canvas::HandlePosition::NorthEast:
                    case canvas::HandlePosition::SouthWest:
                        viewport()->setCursor(Qt::SizeBDiagCursor);
                        break;
                    case canvas::HandlePosition::North:
                    case canvas::HandlePosition::South:
                        viewport()->setCursor(Qt::SizeVerCursor);
                        break;
                    case canvas::HandlePosition::East:
                    case canvas::HandlePosition::West:
                        viewport()->setCursor(Qt::SizeHorCursor);
                        break;
                    case canvas::HandlePosition::Rotate:
                        viewport()->setCursor(Qt::PointingHandCursor);
                        break;
                }
            } else if (hovered != nullptr && hovered->isSelected()) {
                viewport()->setCursor(Qt::SizeAllCursor);
            } else {
                viewport()->setCursor(hovered != nullptr ? Qt::PointingHandCursor : Qt::ArrowCursor);
            }
        }
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    const QPointF devicePt = event->position();
    const QPointF deviceDelta = devicePt - m_gesture.pressDevicePx;

    if (m_tool == canvas::Tool::Text) {
        const QPointF rawScenePt = mapToScene(event->position().toPoint());
        const double pageWidth = static_cast<double>(m_pageBounds.width);
        const double pageHeight = static_cast<double>(m_pageBounds.height);
        const QPointF scenePt{std::clamp(rawScenePt.x(), 0.0, pageWidth), std::clamp(rawScenePt.y(), 0.0, pageHeight)};
        const double left = std::min(m_gesture.pressSourcePx.x(), scenePt.x());
        const double top = std::min(m_gesture.pressSourcePx.y(), scenePt.y());
        const double minDimension = domain::RectGeometry::kMinDimension;
        const double width = std::max(minDimension, std::abs(scenePt.x() - m_gesture.pressSourcePx.x()));
        const double height = std::max(minDimension, std::abs(scenePt.y() - m_gesture.pressSourcePx.y()));
        const double clampedWidth = std::min(width, pageWidth);
        const double clampedHeight = std::min(height, pageHeight);
        const double clampedX = std::clamp(left, 0.0, std::max(0.0, pageWidth - clampedWidth));
        const double clampedY = std::clamp(top, 0.0, std::max(0.0, pageHeight - clampedHeight));
        m_creationBox->setRect(clampedX, clampedY, clampedWidth, clampedHeight);
        m_gesture.dragging = true;
        event->accept();
        return;
    }

    if (!m_gesture.dragging && !canvas::exceedsDragThreshold(deviceDelta)) {
        event->accept();
        return;
    }
    m_gesture.dragging = true;

    const double dpr = viewport()->devicePixelRatio();
    const QPointF viewDelta = canvas::deviceToView(deviceDelta, dpr);
    const QPointF sourceDelta = canvas::viewToSource(viewDelta, m_zoomFactor);

    if (m_gesture.activeHandle.has_value()) {
        const auto result = canvas::resizeRegion(
            m_gesture.targets.first().startGeometry, *m_gesture.activeHandle, sourceDelta, m_pageBounds);
        if (result.has_value()) {
            auto it = m_overlays.find(m_gesture.targets.first().id);
            if (it != m_overlays.end()) {
                it.value()->setTransientGeometry(*result);
            }
            syncTextItemGeometry(m_gesture.targets.first().id, *result);
        }
        event->accept();
        return;
    }

    // Move gesture. Intersect every target's allowable delta so the selection
    // remains rigid when one region reaches a page edge first.
    double minDeltaX = -std::numeric_limits<double>::infinity();
    double maxDeltaX = std::numeric_limits<double>::infinity();
    double minDeltaY = -std::numeric_limits<double>::infinity();
    double maxDeltaY = std::numeric_limits<double>::infinity();
    const double pageWidth = static_cast<double>(m_pageBounds.width);
    const double pageHeight = static_cast<double>(m_pageBounds.height);
    for (const auto& target : m_gesture.targets) {
        minDeltaX = std::max(minDeltaX, -target.startGeometry.x);
        maxDeltaX = std::min(maxDeltaX, pageWidth - target.startGeometry.width - target.startGeometry.x);
        minDeltaY = std::max(minDeltaY, -target.startGeometry.y);
        maxDeltaY = std::min(maxDeltaY, pageHeight - target.startGeometry.height - target.startGeometry.y);
    }
    const QPointF sharedDelta{std::clamp(sourceDelta.x(), minDeltaX, maxDeltaX),
                              std::clamp(sourceDelta.y(), minDeltaY, maxDeltaY)};
    for (const auto& target : m_gesture.targets) {
        const auto newGeom = target.startGeometry;
        const auto movedGeom =
            RectGeometry{newGeom.x + sharedDelta.x(), newGeom.y + sharedDelta.y(), newGeom.width, newGeom.height};
        auto it = m_overlays.find(target.id);
        if (it != m_overlays.end()) {
            it.value()->setTransientGeometry(movedGeom);
        }
        syncTextItemGeometry(target.id, movedGeom);
    }
    event->accept();
}

void MangaCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton &&
        (m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser) &&
        m_removeStrokeActive) {
        const bool manualTool = m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser;
        const bool commit = !m_spaceHeld && !m_removeStroke.isEmpty();
        if (commit && m_tool == canvas::Tool::Remove) {
            Q_EMIT removeStrokeCommitted(m_removeStroke, m_removeDiameter);
        } else if (commit) {
            finishStrokeDraft();
            Q_EMIT manualStrokeCommitted(m_removeStroke, m_tool, m_brushColor, m_removeDiameter);
        }
        if (manualTool && commit) {
            // The draft stays visible after release; the committed layer replaces it on settle.
            m_removeStroke.clear();
            m_removeStrokeActive = false;
            viewport()->update();
        } else {
            cancelRemoveStroke();
        }
        event->accept();
        return;
    }
    if (event->button() != Qt::LeftButton || !m_gesture.active) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    if (m_tool == canvas::Tool::Text) {
        const QPointF releaseViewPx = event->position();
        const QPointF viewDelta = releaseViewPx - m_gesture.pressDevicePx;
        const bool pointText = std::abs(viewDelta.x()) < canvas::kDragThresholdDevicePx &&
                               std::abs(viewDelta.y()) < canvas::kDragThresholdDevicePx;
        if (!pointText && m_creationBox) {
            const QPointF rawScenePt = mapToScene(event->position().toPoint());
            const double pageWidth = static_cast<double>(m_pageBounds.width);
            const double pageHeight = static_cast<double>(m_pageBounds.height);
            const QPointF scenePt{std::clamp(rawScenePt.x(), 0.0, pageWidth),
                                  std::clamp(rawScenePt.y(), 0.0, pageHeight)};
            const double minDimension = domain::RectGeometry::kMinDimension;
            const double width =
                std::min(std::max(minDimension, std::abs(scenePt.x() - m_gesture.pressSourcePx.x())), pageWidth);
            const double height =
                std::min(std::max(minDimension, std::abs(scenePt.y() - m_gesture.pressSourcePx.y())), pageHeight);
            const double x =
                std::clamp(std::min(m_gesture.pressSourcePx.x(), scenePt.x()), 0.0, std::max(0.0, pageWidth - width));
            const double y =
                std::clamp(std::min(m_gesture.pressSourcePx.y(), scenePt.y()), 0.0, std::max(0.0, pageHeight - height));
            m_creationBox->setRect(x, y, width, height);
            const QRectF r = m_creationBox->rect();
            Q_EMIT textBoxCreateCommitted(SourceRectGeometry{r.x(), r.y(), r.width(), r.height()});
        } else if (pointText) {
            Q_EMIT pointTextCreateCommitted(m_gesture.pressSourcePx);
        }
        m_creationBox->setVisible(false);
        m_gesture = {};
        event->accept();
        return;
    }

    if (m_gesture.dragging) {
        const auto kind = m_gesture.activeHandle.has_value() ? canvas::EditKind::Resize : canvas::EditKind::Move;
        QVector<RegionId> changedIds;
        QVector<RectGeometry> changedGeometries;
        for (const auto& target : m_gesture.targets) {
            auto it = m_overlays.find(target.id);
            if (it != m_overlays.end()) {
                const auto finalGeom = it.value()->currentGeometry();
                if (!finalGeom.approxEquals(target.startGeometry)) {
                    changedIds.append(target.id);
                    changedGeometries.append(finalGeom);
                }
            }
        }
        if (changedIds.size() == 1) {
            Q_EMIT regionGeometryCommitted(changedIds.constFirst(), changedGeometries.constFirst(), kind);
        } else if (!changedIds.isEmpty()) {
            Q_EMIT regionsGeometryCommitted(changedIds, changedGeometries, kind);
        }
    }

    m_gesture = {};
    event->accept();
}

void MangaCanvas::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton && m_tool == canvas::Tool::Select) {
        if (auto* overlay = overlayItemAtScenePos(mapToScene(event->position().toPoint())); overlay != nullptr) {
            Q_EMIT regionSelectionRequested(overlay->regionId(), canvas::SelectionIntent::Replace);
            Q_EMIT regionTextEditRequested(overlay->regionId());
            event->accept();
            return;
        }
    }
    QGraphicsView::mouseDoubleClickEvent(event);
}

void MangaCanvas::contextMenuEvent(QContextMenuEvent* event) {
    auto* overlay = overlayItemAtScenePos(mapToScene(event->pos()));
    if (overlay == nullptr || m_tool != canvas::Tool::Select) {
        QGraphicsView::contextMenuEvent(event);
        return;
    }

    const RegionId regionId = overlay->regionId();
    Q_EMIT regionSelectionRequested(regionId, canvas::SelectionIntent::Replace);
    fluent::menus_toolbars::FluentMenu menu(QString(), this);
    auto* editAction = menu.addAction(tr("编辑译文"));
    auto* ocrAction = menu.addAction(tr("重新识别"));
    auto* translateAction = menu.addAction(tr("重新翻译"));
    menu.addSeparator();
    auto* deleteAction = menu.addAction(tr("删除文字框"));
    const QAction* selected = menu.exec(event->globalPos());
    if (selected == editAction) {
        Q_EMIT regionTextEditRequested(regionId);
    } else if (selected == ocrAction) {
        Q_EMIT regionContextActionRequested(regionId, QStringLiteral("ocr"));
    } else if (selected == translateAction) {
        Q_EMIT regionContextActionRequested(regionId, QStringLiteral("translate"));
    } else if (selected == deleteAction) {
        Q_EMIT regionContextActionRequested(regionId, QStringLiteral("delete"));
    }
    event->accept();
}

void MangaCanvas::enterEvent(QEnterEvent* event) {
    m_mouseInsideCanvas = true;
    m_currentMouseScenePos = mapToScene(event->position().toPoint());
    if (m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser) {
        const double radius = m_removeDiameter * 0.5;
        const double margin = 4.0 / std::max(0.01, m_zoomFactor);
        const QRectF cursorRect(m_currentMouseScenePos.x() - radius - margin,
                                m_currentMouseScenePos.y() - radius - margin,
                                (radius + margin) * 2.0,
                                (radius + margin) * 2.0);
        viewport()->update(mapFromScene(cursorRect).boundingRect());
    } else if (m_tool == canvas::Tool::Eyedropper) {
        viewport()->setCursor(colorPickerCursor(viewport()->devicePixelRatioF()));
    }
    QGraphicsView::enterEvent(event);
}

void MangaCanvas::leaveEvent(QEvent* event) {
    if (auto previous = m_overlays.find(m_hoveredRegionId); previous != m_overlays.end()) {
        previous.value()->setHovered(false);
    }
    m_hoveredRegionId = {};
    m_mouseInsideCanvas = false;
    if (m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser) {
        const double radius = m_removeDiameter * 0.5;
        const double margin = 4.0 / std::max(0.01, m_zoomFactor);
        const QRectF cursorRect(m_currentMouseScenePos.x() - radius - margin,
                                m_currentMouseScenePos.y() - radius - margin,
                                (radius + margin) * 2.0,
                                (radius + margin) * 2.0);
        viewport()->update(mapFromScene(cursorRect).boundingRect());
    } else {
        viewport()->setCursor(Qt::ArrowCursor);
    }
    QGraphicsView::leaveEvent(event);
}

void MangaCanvas::resizeEvent(QResizeEvent* event) {
    const QPoint currentWinPos = (window() != nullptr) ? mapTo(window(), QPoint(0, 0)) : mapToGlobal(QPoint(0, 0));
    const QPointF oldViewportCenter = viewport()->rect().center();
    const QPointF oldCenter = (m_contentState == ContentState::Image) ? viewCenterOnPage() : QPointF();

    QGraphicsView::resizeEvent(event);
    layoutPlaceholderButton();

    if (m_contentState == ContentState::Image) {
        if (m_hasLastWindowPos && isVisible() && event->oldSize().isValid()) {
            const int deltaX = currentWinPos.x() - m_lastWindowPos.x();
            const int deltaY = currentWinPos.y() - m_lastWindowPos.y();
            const QPointF target(oldViewportCenter.x() - deltaX, oldViewportCenter.y() - deltaY);

            updateNavigationSceneRect(oldCenter, target);
            anchorScenePointOnViewport(oldCenter, target);
        } else {
            updateNavigationSceneRect();
            centerScenePointOnViewport(oldCenter);
        }
        emitViewTransform();
    }

    if (isVisible()) {
        m_lastWindowPos = currentWinPos;
        m_hasLastWindowPos = true;
    } else {
        m_hasLastWindowPos = false;
    }
}

void MangaCanvas::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);

    // Live brush circle cursor for the paint tools.
    if (m_mouseInsideCanvas && !m_spaceHeld && !m_removeStrokeActive &&
        (m_tool == canvas::Tool::Remove || m_tool == canvas::Tool::Brush || m_tool == canvas::Tool::Eraser)) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const double radius = m_removeDiameter / 2.0;

        // Outer contrast drop shadow / dark ring
        QPen shadowPen(QColor(0, 0, 0, 160), 2.5);
        shadowPen.setCosmetic(true);
        painter->setPen(shadowPen);
        painter->setBrush(Qt::NoBrush);
        painter->drawEllipse(m_currentMouseScenePos, radius, radius);

        // Inner bright white circle
        QPen circlePen(Qt::white, 1.2);
        circlePen.setCosmetic(true);
        painter->setPen(circlePen);
        painter->drawEllipse(m_currentMouseScenePos, radius, radius);

        painter->restore();
    }

    // Placeholder polyline for the Remove tool only. Brush and Eraser paint real pixels through
    // the stroke draft, so they need no placeholder.
    if (m_removeStrokeActive && m_tool == canvas::Tool::Remove && !m_removeStroke.isEmpty()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QColor strokeColor = themeColorsRef().accentDefault;
        strokeColor.setAlpha(100);
        painter->setPen(QPen(strokeColor, m_removeDiameter, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (m_removeStroke.size() == 1) {
            painter->setBrush(strokeColor);
            painter->drawEllipse(m_removeStroke.constFirst(), m_removeDiameter / 2.0, m_removeDiameter / 2.0);
        } else {
            painter->drawPolyline(m_removeStroke.constData(), m_removeStroke.size());
        }
        painter->restore();
    }

    if (m_contentState == ContentState::Image) {
        return;
    }
    if (m_placeholderCard && m_placeholderCard->isVisible()) {
        return;
    }

    painter->save();
    painter->setPen(m_placeholderTextColor);

    QFont font = painter->font();
    if (font.pointSizeF() > 0.0) {
        font.setPointSizeF(font.pointSizeF() * 1.5);
    } else {
        const int basePixelSize = font.pixelSize() > 0 ? font.pixelSize() : painter->fontMetrics().height();
        font.setPixelSize(std::max(1, qRound(basePixelSize * 1.5)));
    }
    painter->setFont(font);

    QString text = m_placeholderMessage;
    if (m_contentState == ContentState::Loading) {
        if (!text.isEmpty()) {
            text += QLatin1Char('\n');
        }
        text += tr("正在加载");
    }

    if (!text.isEmpty()) {
        QRectF textRect = rect;
        if (m_placeholderAction->isVisible()) {
            textRect.translate(0, -20);
        }
        painter->drawText(textRect, Qt::AlignCenter | Qt::TextWordWrap, text);
    }

    painter->restore();
}

void MangaCanvas::scrollContentsBy(int dx, int dy) {
    QGraphicsView::scrollContentsBy(dx, dy);
    if (m_contentState == ContentState::Image) {
        emitViewTransform();
    }
}

void MangaCanvas::emitViewTransform() {
    if (m_contentState == ContentState::Image) {
        Q_EMIT viewTransformChanged(m_zoomFactor, viewCenterOnPage());
    }
}

void MangaCanvas::layoutPlaceholderButton() {
    if (!m_placeholderCard || m_placeholderCard->isHidden() || !viewport()) {
        return;
    }

    m_placeholderCard->adjustSize();
    const int x = (viewport()->width() - m_placeholderCard->width()) / 2;
    const int y = (viewport()->height() - m_placeholderCard->height()) / 2;
    m_placeholderCard->move(x, y);
}

void MangaCanvas::updateAccessibleDescription() {
    const int total = m_overlays.size();
    int selected = 0;
    for (auto it = m_overlays.constBegin(); it != m_overlays.constEnd(); ++it) {
        if (it.value()->isSelected()) {
            ++selected;
        }
    }
    setAccessibleDescription(tr("漫画画布：包含 %1 个文本区域，已选中 %2 个").arg(total).arg(selected));
}

void MangaCanvas::showEvent(QShowEvent* event) {
    QGraphicsView::showEvent(event);
    layoutPlaceholderButton();
}

} // namespace manga::ui::manga
