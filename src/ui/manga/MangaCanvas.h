#pragma once

#include "ui/manga/CanvasZOrder.h"

#include <FluentQt/Foundation.h>
#include <QGraphicsView>
#include <QImage>
#include <QPixmap>

class QGraphicsPixmapItem;
class QResizeEvent;
class QWheelEvent;
class QKeyEvent;

namespace fluent::basicinput {
class Button;
}

namespace manga::ui::manga {

/// Manga page viewport: original image layer, pan/zoom and placeholder states.
/// P5-B scope: no region overlay, no tools beyond pan. Scene coordinates are
/// source image pixels; the page pixmap lives at (0,0) with kZPageOriginal.
class MangaCanvas : public QGraphicsView, public fluent::FluentElement, public fluent::QMLPlus {
    Q_OBJECT
public:
    enum class ContentState {
        NoProject,
        NoPage,
        Loading,
        Error,
        Image
    };

    static constexpr double kMinZoom = 0.05;
    static constexpr double kMaxZoom = 16.0;

    explicit MangaCanvas(QWidget* parent = nullptr);
    ~MangaCanvas() override;

    void onThemeUpdated() override;

    /// Replaces the page image and switches to the Image state. GUI thread only.
    void setPageImage(const QImage& image);
    void clearPageImage();

    /// Loading clears any previous page image so a stale page never poses as the new one.
    void setContentState(ContentState state, const QString& message = {}, const QString& actionText = {});
    [[nodiscard]] ContentState contentState() const {
        return m_contentState;
    }

    [[nodiscard]] double zoomFactor() const {
        return m_zoomFactor;
    }
    void setZoomFactor(double factor); // clamped to [kMinZoom, kMaxZoom]
    void zoomIn();                     // next discrete level, viewport-center anchor
    void zoomOut();                    // previous discrete level
    void fitToPage();                  // clamped to kMinZoom
    void actualSize();                 // 100%

    /// Center of the viewport in scene (source pixel) coordinates.
    [[nodiscard]] QPointF viewCenterOnPage() const;
    void centerOnPage(const QPointF& scenePos);

    /// Test-only observation hooks.
    [[nodiscard]] QWidget* placeholderActionButtonForTest() const;
    [[nodiscard]] bool hasPageImageForTest() const;

Q_SIGNALS:
    void zoomChanged(double factor);
    void viewTransformChanged(double zoom, const QPointF& centerOnPage);
    void placeholderActionActivated();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    void applyZoom(double factor);
    void emitViewTransform();
    void layoutPlaceholderButton();
    void
    setContentStateInternal(ContentState state, const QString& message, const QString& actionText, bool clearImage);

    QGraphicsScene* m_scene{nullptr};         // owned via QGraphicsView::setScene
    QGraphicsPixmapItem* m_pageItem{nullptr}; // scene-owned
    fluent::basicinput::Button* m_placeholderAction{nullptr};
    QPixmap m_pagePixmap;
    ContentState m_contentState{ContentState::NoProject};
    QString m_placeholderMessage;
    double m_zoomFactor{1.0};
    QColor m_backgroundColor;
    QColor m_placeholderTextColor;
};

} // namespace manga::ui::manga
