#include "ui/manga/MangaCanvas.h"

#include <FluentQt/BasicInput.h>
#include <QCoreApplication>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>

namespace manga::ui::manga {

namespace {
constexpr double kZoomLevels[] = {0.05, 0.10, 0.25, 0.50, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0};
// The document workspace remains dark in both application themes so page edges stay visible.
const QColor kDefaultCanvasBackground(QStringLiteral("#202124"));
const QColor kDefaultCanvasForeground(QStringLiteral("#F3F3F3"));
} // namespace

MangaCanvas::MangaCanvas(QWidget* parent) : QGraphicsView(parent) {
    m_scene = new QGraphicsScene(this);
    setScene(m_scene);
    setDragMode(ScrollHandDrag);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter);

    m_placeholderAction = new fluent::basicinput::Button(viewport());
    m_placeholderAction->hide();
    connect(m_placeholderAction, &fluent::basicinput::Button::clicked, this, &MangaCanvas::placeholderActionActivated);

    m_backgroundColor = kDefaultCanvasBackground;
    m_placeholderTextColor = kDefaultCanvasForeground;
    setBackgroundBrush(m_backgroundColor);
}

MangaCanvas::~MangaCanvas() = default;

void MangaCanvas::onThemeUpdated() {
    m_backgroundColor = kDefaultCanvasBackground;
    m_placeholderTextColor = kDefaultCanvasForeground;
    setBackgroundBrush(m_backgroundColor);
    if (viewport()) {
        viewport()->update();
    }
}

void MangaCanvas::setPageImage(const QImage& image) {
    m_pagePixmap = QPixmap::fromImage(image);
    if (!m_pageItem) {
        m_pageItem = new QGraphicsPixmapItem();
        m_pageItem->setZValue(canvas::kZPageOriginal);
        m_scene->addItem(m_pageItem);
    }
    m_pageItem->setPixmap(m_pagePixmap);
    m_pageItem->setVisible(true);

    m_scene->setSceneRect(QRectF(m_pagePixmap.rect()).adjusted(-24, -24, 24, 24));
    setContentStateInternal(ContentState::Image, QString(), QString(), false);
}

void MangaCanvas::clearPageImage() {
    m_pagePixmap = QPixmap();
    if (m_pageItem) {
        m_pageItem->setPixmap(m_pagePixmap);
        m_pageItem->setVisible(false);
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

    if (state == ContentState::NoProject || state == ContentState::NoPage || state == ContentState::Error) {
        if (!actionText.isEmpty()) {
            m_placeholderAction->setText(actionText);
            m_placeholderAction->setAccessibleName(actionText);
            m_placeholderAction->show();
            m_placeholderAction->raise();
        } else {
            m_placeholderAction->hide();
        }
    } else {
        m_placeholderAction->hide();
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

    Q_EMIT zoomChanged(m_zoomFactor);
    emitViewTransform();
}

void MangaCanvas::zoomIn() {
    for (double level : kZoomLevels) {
        if (level > m_zoomFactor + 0.001) { // fuzzy compare margin
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
    QRectF targetRect = m_scene->sceneRect();
    if (targetRect.isEmpty()) {
        return;
    }

    double scaleX = viewport()->width() / targetRect.width();
    double scaleY = viewport()->height() / targetRect.height();
    double scale = std::min(scaleX, scaleY);

    applyZoom(scale);
}

void MangaCanvas::actualSize() {
    applyZoom(1.0);
}

QPointF MangaCanvas::viewCenterOnPage() const {
    return mapToScene(viewport()->rect().center());
}

void MangaCanvas::centerOnPage(const QPointF& scenePos) {
    centerOn(scenePos);
    emitViewTransform();
}

QWidget* MangaCanvas::placeholderActionButtonForTest() const {
    return m_placeholderAction;
}

bool MangaCanvas::hasPageImageForTest() const {
    return !m_pagePixmap.isNull();
}

void MangaCanvas::wheelEvent(QWheelEvent* event) {
    if (event->modifiers() & Qt::ControlModifier) {
        double delta = event->angleDelta().y();
        if (delta != 0) {
            setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
            double steps = delta / 120.0;
            double factor = m_zoomFactor * std::pow(1.25, steps);

            factor = std::clamp(factor, kMinZoom, kMaxZoom);
            if (!qFuzzyCompare(m_zoomFactor, factor)) {
                m_zoomFactor = factor;
                setTransform(QTransform::fromScale(m_zoomFactor, m_zoomFactor));
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

void MangaCanvas::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    layoutPlaceholderButton();
    if (m_contentState == ContentState::Image) {
        emitViewTransform();
    }
}

void MangaCanvas::drawForeground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawForeground(painter, rect);

    if (m_contentState == ContentState::Image) {
        return;
    }

    painter->save();
    painter->setPen(m_placeholderTextColor);

    QFont font = painter->font();
    font.setPointSizeF(font.pointSizeF() * 1.5);
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
    if (m_placeholderAction->isHidden() || !viewport()) {
        return;
    }

    m_placeholderAction->adjustSize();
    int x = (viewport()->width() - m_placeholderAction->width()) / 2;
    int y = (viewport()->height() - m_placeholderAction->height()) / 2;

    if (!m_placeholderMessage.isEmpty()) {
        y += 40; // Offset below text
    }

    m_placeholderAction->move(x, y);
}

void MangaCanvas::showEvent(QShowEvent* event) {
    QGraphicsView::showEvent(event);
    layoutPlaceholderButton();
}

} // namespace manga::ui::manga
