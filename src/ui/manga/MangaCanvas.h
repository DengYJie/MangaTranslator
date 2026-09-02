#pragma once

#include "ui/base/DomainMetaTypes.h"
#include "ui/manga/CanvasZOrder.h"
#include "ui/manga/RegionInteraction.h"
#include "ui/manga/TextLayerDisplay.h"

#include <FluentQt/Foundation.h>
#include <QGraphicsView>
#include <QImage>
#include <QMap>
#include <QPixmap>

class QGraphicsPixmapItem;
class QGraphicsRectItem;
class QResizeEvent;
class QWheelEvent;
class QKeyEvent;
class QMouseEvent;
class QContextMenuEvent;
class QFocusEvent;

namespace fluent::basicinput {
class Button;
}
namespace fluent::layout {
class Card;
}
namespace fluent::textfields {
class Label;
}

namespace manga::ui::manga {

class RegionOverlayItem;
class MangaTextItem;

// Type aliases for MOC-compatible signal parameter types.
using CanvasTool = canvas::Tool;
using CanvasSelectionIntent = canvas::SelectionIntent;
using CanvasEditKind = canvas::EditKind;
using CanvasHandlePosition = canvas::HandlePosition;
using CanvasRegionDisplay = canvas::RegionDisplay;
using SourceRectGeometry = domain::RectGeometry;
using SourceRegionId = domain::RegionId;
using SourceImageSize = domain::ImageSize;

/// Manga page viewport: original image layer, pan/zoom, region overlays, and
/// tool-driven interaction. Scene coordinates are source image pixels; the page
/// pixmap lives at (0,0) with kZPageOriginal.
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
    void setCleaningImage(const QImage& image);
    void setTranslatedPreview(const QImage& image);
    void setPreviewMode(int mode);
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
    /// Centers scenePoint using the same integer viewport metric as viewCenterOnPage, so a
    /// save-center/restore-center round trip does not shift the scroll bars.
    void centerScenePointOnViewport(const QPointF& scenePoint);

    /// Sets the active canvas tool. Pan keeps ScrollHandDrag; Select switches to
    /// NoDrag for item interaction; Text enables text creation gesture.
    void setTool(CanvasTool tool);
    [[nodiscard]] CanvasTool tool() const noexcept {
        return m_tool;
    }

    [[nodiscard]] double removeDiameter() const noexcept {
        return m_removeDiameter;
    }
    void setRemoveDiameter(double diameter);

    [[nodiscard]] QColor brushColor() const noexcept {
        return m_brushColor;
    }
    void setBrushColor(const QColor& color);

    /// Reconciles overlay items with the given region projections. Creates, updates,
    /// or removes items to match. Items hold geometry and selection state only.
    void setRegions(const QVector<CanvasRegionDisplay>& regions);

    /// Reconciles independently rendered text items with the current region projections.
    void setTextLayers(const QVector<TextLayerDisplay>& layers);

    /// Sets the page bounds used for clamping region geometry during move/resize.
    void setPageBounds(SourceImageSize bounds) noexcept {
        m_pageBounds = bounds;
    }

    /// Test-only observation hooks.
    [[nodiscard]] QWidget* placeholderActionButtonForTest() const;
    [[nodiscard]] bool hasPageImageForTest() const;
    [[nodiscard]] int regionOverlayCountForTest() const;

Q_SIGNALS:
    void zoomChanged(double factor);
    void viewTransformChanged(double zoom, const QPointF& centerOnPage);
    void placeholderActionActivated();

    /// Emitted when a point-text gesture completes.
    void pointTextCreateCommitted(QPointF anchor);

    /// Emitted when a paragraph-text gesture completes with its source-space frame.
    void textBoxCreateCommitted(SourceRectGeometry geometry);

    /// Emitted when a region is clicked to request selection change.
    void regionSelectionRequested(SourceRegionId id, CanvasSelectionIntent intent);

    /// Emitted when a move or resize gesture completes with changed geometry.
    void regionGeometryCommitted(SourceRegionId id, SourceRectGeometry geometry, CanvasEditKind kind);
    void
    regionsGeometryCommitted(QVector<SourceRegionId> ids, QVector<SourceRectGeometry> geometries, CanvasEditKind kind);

    /// Emitted when an in-progress gesture is cancelled (Escape key).
    void gestureCancelled();

    /// Emitted when Delete is pressed with a selected region.
    void regionDeleteRequested(SourceRegionId id);

    /// Emitted when the user requests inline editing from the canvas.
    void regionTextEditRequested(SourceRegionId id);

    /// Emitted after a region context menu action is chosen.
    void regionContextActionRequested(SourceRegionId id, QString action);

    /// Emitted once when a Remove stroke is released.
    void removeStrokeCommitted(QVector<QPointF> points, double diameter);
    void removeDiameterChanged(double diameter);

    /// Emitted once when a manual Brush or Eraser stroke is released.
    void manualStrokeCommitted(QVector<QPointF> points, CanvasTool tool, QColor color, double diameter);
    void brushColorChanged(QColor color);

protected:
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void drawForeground(QPainter* painter, const QRectF& rect) override;
    void scrollContentsBy(int dx, int dy) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void applyZoom(double factor);
    [[nodiscard]] QRectF contentSceneRect() const;
    void updateNavigationSceneRect();
    void emitViewTransform();
    void layoutPlaceholderButton();
    void updateAccessibleDescription();
    void
    setContentStateInternal(ContentState state, const QString& message, const QString& actionText, bool clearImage);

    /// Returns the overlay item under the given scene position, or nullptr.
    [[nodiscard]] RegionOverlayItem* overlayItemAtScenePos(const QPointF& scenePos) const;

    /// Cancels any in-progress gesture and restores the start geometry.
    void cancelGesture();
    void cancelRemoveStroke();
    void updateRemoveCursor();
    void syncTextItemGeometry(SourceRegionId id, const SourceRectGeometry& geometry);

    /// Propagates resolved FluentQt semantic colors to overlay items and the creation box.
    void applyThemeColorsToItems();
    void syncTextItemVisibility();

    QGraphicsScene* m_scene{nullptr};                // owned via QGraphicsView::setScene
    QGraphicsPixmapItem* m_pageItem{nullptr};        // scene-owned
    QGraphicsPixmapItem* m_cleaningItem{nullptr};    // scene-owned
    QGraphicsPixmapItem* m_translationItem{nullptr}; // scene-owned
    fluent::layout::Card* m_placeholderCard{nullptr};
    fluent::textfields::Label* m_placeholderIcon{nullptr};
    fluent::textfields::Label* m_placeholderTitle{nullptr};
    fluent::textfields::Label* m_placeholderDescription{nullptr};
    fluent::basicinput::Button* m_placeholderAction{nullptr};
    QPixmap m_pagePixmap;
    QPixmap m_cleaningPixmap;
    QPixmap m_translationPixmap;
    int m_previewMode{1};
    ContentState m_contentState{ContentState::NoProject};
    QString m_placeholderMessage;
    double m_zoomFactor{1.0};
    QColor m_backgroundColor;
    QColor m_placeholderTextColor;

    CanvasTool m_tool{CanvasTool::Pan};
    SourceImageSize m_pageBounds{};
    QMap<SourceRegionId, RegionOverlayItem*> m_overlays;
    QMap<domain::RegionId, MangaTextItem*> m_textItems;

    /// Transient text draft frame shown during a Text gesture.
    QGraphicsRectItem* m_creationBox{nullptr}; // scene-owned

    /// Gesture tracking state for move/resize/text drags.
    struct GestureState {
        struct Target {
            SourceRegionId id;
            SourceRectGeometry startGeometry;
        };

        bool active{false};
        bool dragging{false};
        QPointF pressDevicePx;
        QPointF pressSourcePx;
        QVector<Target> targets;
        std::optional<CanvasHandlePosition> activeHandle;
    };
    GestureState m_gesture;
    QVector<QPointF> m_removeStroke;
    bool m_removeStrokeActive{false};
    double m_removeDiameter{32.0};
    QColor m_brushColor{17, 17, 17, 255};
    bool m_spaceHeld{false};
    SourceRegionId m_hoveredRegionId{};
    QPointF m_currentMouseScenePos{};
    bool m_mouseInsideCanvas{false};
};

} // namespace manga::ui::manga
