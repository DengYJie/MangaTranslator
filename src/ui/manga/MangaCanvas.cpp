#include "ui/manga/MangaCanvas.h"

#include "ui/manga/RegionOverlayItem.h"

#include <FluentQt/BasicInput.h>
#include <QCoreApplication>
#include <QGraphicsPixmapItem>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QShowEvent>
#include <QWheelEvent>

#include <algorithm>

namespace manga::ui::manga {

using ::manga::domain::ImageSize;
using ::manga::domain::RectGeometry;
using ::manga::domain::RegionId;

namespace {
constexpr double kZoomLevels[] = {0.05, 0.10, 0.25, 0.50, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0};
const QColor kDefaultCanvasBackground(QStringLiteral("#202124"));
const QColor kDefaultCanvasForeground(QStringLiteral("#F3F3F3"));

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

    m_creationBox = new QGraphicsRectItem();
    m_creationBox->setZValue(canvas::kZToolCreation);
    QPen creationPen(QColor(0, 120, 215), 0);
    creationPen.setCosmetic(true);
    creationPen.setStyle(Qt::DashLine);
    m_creationBox->setPen(creationPen);
    m_creationBox->setBrush(QColor(0, 120, 215, 40));
    m_creationBox->setVisible(false);
    m_scene->addItem(m_creationBox);
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

void MangaCanvas::setTool(canvas::Tool tool) {
    m_tool = tool;
    switch (tool) {
        case canvas::Tool::Pan:
            setDragMode(ScrollHandDrag);
            break;
        case canvas::Tool::Select:
            setDragMode(NoDrag);
            break;
        case canvas::Tool::CreateRegion:
            setDragMode(NoDrag);
            break;
    }
}

void MangaCanvas::setRegions(const QVector<canvas::RegionDisplay>& regions) {
    QSet<domain::RegionId> incomingIds;
    int order = 1;
    for (const auto& rd : regions) {
        incomingIds.insert(rd.id);
        auto it = m_overlays.find(rd.id);
        if (it != m_overlays.end()) {
            it.value()->updateFromDisplay(rd);
            it.value()->setOrder(order);
        } else {
            auto* item = new RegionOverlayItem(rd.id);
            item->updateFromDisplay(rd);
            item->setOrder(order);
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

QWidget* MangaCanvas::placeholderActionButtonForTest() const {
    return m_placeholderAction;
}

bool MangaCanvas::hasPageImageForTest() const {
    return !m_pagePixmap.isNull();
}

int MangaCanvas::regionOverlayCountForTest() const {
    return m_overlays.size();
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

    if (m_gesture.activeHandle.has_value() || !m_gesture.targetRegionId.value.isNil()) {
        auto it = m_overlays.find(m_gesture.targetRegionId);
        if (it != m_overlays.end()) {
            it.value()->setTransientGeometry(m_gesture.startGeometry);
        }
    }

    if (m_creationBox) {
        m_creationBox->setVisible(false);
    }

    m_gesture = {};
    Q_EMIT gestureCancelled();
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
    if (event->key() == Qt::Key_Escape && m_gesture.active) {
        cancelGesture();
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
                bool sel = (it.key() == ids[nextIndex]);
                updated.append({it.key(), geom, sel});
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
                        Q_EMIT regionGeometryCommitted(it.key(), *result, canvas::EditKind::Resize);
                    }
                } else {
                    const auto newGeom = canvas::moveRegion(it.value()->currentGeometry(), delta, m_pageBounds);
                    if (!newGeom.approxEquals(it.value()->currentGeometry())) {
                        it.value()->setTransientGeometry(newGeom);
                        Q_EMIT regionGeometryCommitted(it.key(), newGeom, canvas::EditKind::Move);
                    }
                }
                event->accept();
                return;
            }
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

void MangaCanvas::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    if (m_tool == canvas::Tool::Pan) {
        QGraphicsView::mousePressEvent(event);
        return;
    }

    const QPointF devicePt = event->position();
    const QPointF scenePt = mapToScene(event->pos());
    const double dpr = viewport()->devicePixelRatio();

    if (m_tool == canvas::Tool::CreateRegion) {
        m_gesture.active = true;
        m_gesture.dragging = false;
        m_gesture.pressDevicePx = devicePt;
        m_gesture.startGeometry = {scenePt.x(), scenePt.y(), 0, 0};
        m_gesture.targetRegionId = {};
        m_gesture.activeHandle = std::nullopt;

        m_creationBox->setRect(scenePt.x(), scenePt.y(), 0, 0);
        m_creationBox->setVisible(true);
        event->accept();
        return;
    }

    // Select tool: check handle hit first, then region body.
    // Convert press to device coords for handle hit testing.
    for (auto it = m_overlays.constBegin(); it != m_overlays.constEnd(); ++it) {
        auto* overlay = it.value();
        const auto geom = overlay->currentGeometry();

        if (overlay->isSelected()) {
            const auto hitHandle = canvas::hitTestHandle(devicePt, geom, m_zoomFactor, dpr);
            if (hitHandle.has_value()) {
                m_gesture.active = true;
                m_gesture.dragging = false;
                m_gesture.pressDevicePx = devicePt;
                m_gesture.startGeometry = geom;
                m_gesture.targetRegionId = it.key();
                m_gesture.activeHandle = hitHandle;
                event->accept();
                return;
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

        Q_EMIT regionSelectionRequested(clickedOverlay->regionId(), intent);

        // Start potential move gesture.
        m_gesture.active = true;
        m_gesture.dragging = false;
        m_gesture.pressDevicePx = devicePt;
        m_gesture.startGeometry = clickedOverlay->currentGeometry();
        m_gesture.targetRegionId = clickedOverlay->regionId();
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
    if (!m_gesture.active) {
        QGraphicsView::mouseMoveEvent(event);
        return;
    }

    const QPointF devicePt = event->position();
    const QPointF deviceDelta = devicePt - m_gesture.pressDevicePx;

    if (!m_gesture.dragging && !canvas::exceedsDragThreshold(deviceDelta)) {
        event->accept();
        return;
    }
    m_gesture.dragging = true;

    const double dpr = viewport()->devicePixelRatio();
    const QPointF viewDelta = canvas::deviceToView(deviceDelta, dpr);
    const QPointF sourceDelta = canvas::viewToSource(viewDelta, m_zoomFactor);

    if (m_tool == canvas::Tool::CreateRegion) {
        const QPointF scenePt = mapToScene(event->pos());
        const double x = std::min(m_gesture.startGeometry.x, scenePt.x());
        const double y = std::min(m_gesture.startGeometry.y, scenePt.y());
        const double w = std::abs(scenePt.x() - m_gesture.startGeometry.x);
        const double h = std::abs(scenePt.y() - m_gesture.startGeometry.y);
        m_creationBox->setRect(x, y, w, h);
        event->accept();
        return;
    }

    if (m_gesture.activeHandle.has_value()) {
        const auto result =
            canvas::resizeRegion(m_gesture.startGeometry, *m_gesture.activeHandle, sourceDelta, m_pageBounds);
        if (result.has_value()) {
            auto it = m_overlays.find(m_gesture.targetRegionId);
            if (it != m_overlays.end()) {
                it.value()->setTransientGeometry(*result);
            }
        }
        event->accept();
        return;
    }

    // Move gesture.
    const auto newGeom = canvas::moveRegion(m_gesture.startGeometry, sourceDelta, m_pageBounds);
    auto it = m_overlays.find(m_gesture.targetRegionId);
    if (it != m_overlays.end()) {
        it.value()->setTransientGeometry(newGeom);
    }
    event->accept();
}

void MangaCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !m_gesture.active) {
        QGraphicsView::mouseReleaseEvent(event);
        return;
    }

    if (m_tool == canvas::Tool::CreateRegion) {
        if (m_gesture.dragging && m_creationBox) {
            const QRectF r = m_creationBox->rect();
            const domain::RectGeometry geom{r.x(), r.y(), r.width(), r.height()};
            if (geom.width >= canvas::kMinRegionDimension && geom.height >= canvas::kMinRegionDimension) {
                Q_EMIT regionCreateCommitted(geom);
            }
            // Below minimum size discards silently.
        }
        m_creationBox->setVisible(false);
        m_gesture = {};
        event->accept();
        return;
    }

    if (m_gesture.dragging) {
        auto it = m_overlays.find(m_gesture.targetRegionId);
        if (it != m_overlays.end()) {
            const auto finalGeom = it.value()->currentGeometry();
            if (!finalGeom.approxEquals(m_gesture.startGeometry)) {
                const auto kind =
                    m_gesture.activeHandle.has_value() ? canvas::EditKind::Resize : canvas::EditKind::Move;
                Q_EMIT regionGeometryCommitted(m_gesture.targetRegionId, finalGeom, kind);
            }
        }
    }

    m_gesture = {};
    event->accept();
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
        y += 40;
    }

    m_placeholderAction->move(x, y);
}

void MangaCanvas::updateAccessibleDescription() {
    const int total = m_overlays.size();
    int selected = 0;
    for (auto it = m_overlays.constBegin(); it != m_overlays.constEnd(); ++it) {
        if (it.value()->isSelected()) {
            ++selected;
        }
    }
    setAccessibleDescription(tr("%1 regions, %2 selected").arg(total).arg(selected));
}

void MangaCanvas::showEvent(QShowEvent* event) {
    QGraphicsView::showEvent(event);
    layoutPlaceholderButton();
}

} // namespace manga::ui::manga
