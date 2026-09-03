#pragma once

#include "application/operation/OperationSnapshot.h"
#include "ui/base/BasePage.h"
#include "ui/manga/MangaCanvas.h"

#include <QVector>

class QPaintEvent;
class QFrame;
class QSplitter;
class QAbstractButton;

namespace manga::ui::manga {
class MangaCanvas;
}
namespace manga::ui::widget {
class ActivityCenter;
class InspectorPanel;
class PageThumbnailList;
class EditorCommandBar;
class PaneSplitter;
} // namespace manga::ui::widget
namespace fluent::textfields {
class Label;
}
namespace fluent::basicinput {
class Button;
}

namespace manga::ui::screen::editor {

class EditorViewModel;
struct EditorState;
struct PageImageDelivery;

/// Editor skeleton page: page thumbnail pane, manga canvas workspace and the
/// selection-driven inspector column.
class EditorPage : public base::BasePage {
    Q_OBJECT
public:
    enum class BreakpointMode {
        Wide,
        Standard,
        Compact,
        Narrow,
        Minimum
    };

    explicit EditorPage(EditorViewModel* viewModel, QWidget* parent = nullptr);

    /// Drawer and pane toggles
    void setPageDrawerVisible(bool visible);
    void setInspectorDrawerVisible(bool visible);
    void togglePagePane();
    void toggleInspectorPane();
    [[nodiscard]] bool isPageDrawerVisible() const;
    [[nodiscard]] bool isInspectorDrawerVisible() const;

    /// Keyboard focus pane cycling (F6 / Shift+F6)
    void cyclePaneFocus(bool forward = true);

    /// Test-only observation hooks.
    [[nodiscard]] EditorViewModel* viewModelForTest() const noexcept {
        return m_viewModel;
    }
    [[nodiscard]] ui::manga::MangaCanvas* canvasForTest() const {
        return m_canvas;
    }
    [[nodiscard]] ui::widget::PageThumbnailList* pageListForTest() const {
        return m_pageList;
    }
    [[nodiscard]] ui::widget::InspectorPanel* inspectorForTest() const {
        return m_inspector;
    }
    [[nodiscard]] QSplitter* splitterForTest() const;
    [[nodiscard]] QWidget* pagePaneCardForTest() const {
        return m_pagePaneCard;
    }
    [[nodiscard]] QWidget* inspectorCardForTest() const {
        return m_inspectorCard;
    }
    [[nodiscard]] ui::widget::EditorCommandBar* commandBarForTest() const {
        return m_commandBar;
    }
    [[nodiscard]] ui::widget::ActivityCenter* activityCenter() const noexcept {
        return m_activityCenter;
    }
    void setOperations(const QVector<application::OperationSnapshot>& operations);

Q_SIGNALS:
    void openProjectRequested();
    void importPagesRequested();
    void chooseExportPathRequested();
    void settingsRequested(const QString& routeKey);
    void cancelOperationRequested(const application::OperationId& id);

protected:
    void paintEvent(QPaintEvent* event) override;
    void updateResponsiveLayout(int availableWidth) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    /// Canvas tool rail entry: the rail button plus the capability that gates it.
    struct CanvasToolEntry {
        manga::CanvasTool tool{manga::CanvasTool::Select};
        fluent::basicinput::Button* button{nullptr};
        bool requiresInpaint{false};
        /// True when the tool must stay disabled while any operation runs, so a second stroke
        /// cannot start on top of a draft whose commit is still in flight.
        bool requiresIdle{false};
    };

    void buildUi();
    void render(const EditorState& state);
    void handlePageImageReady(const PageImageDelivery& delivery);
    void handlePlaceholderAction();
    void applySplitterLayout();

    EditorViewModel* m_viewModel; // non-owning
    widget::EditorCommandBar* m_commandBar{nullptr};
    widget::PaneSplitter* m_splitter{nullptr};
    QWidget* m_pagePaneCard{nullptr};
    widget::PageThumbnailList* m_pageList{nullptr};
    manga::MangaCanvas* m_canvas{nullptr};
    QWidget* m_canvasToolRail{nullptr};
    QVector<CanvasToolEntry> m_canvasToolButtons;
    widget::ActivityCenter* m_activityCenter{nullptr};
    QFrame* m_removeBrushSeparator{nullptr};
    fluent::basicinput::Button* m_removeBrushSizeButton{nullptr};
    QAbstractButton* m_brushColorButton{nullptr};
    QWidget* m_canvasStatusBar{nullptr};
    fluent::textfields::Label* m_canvasZoomValueLabel{nullptr};
    fluent::textfields::Label* m_canvasPageSizeLabel{nullptr};
    QWidget* m_inspectorCard{nullptr};
    widget::InspectorPanel* m_inspector{nullptr};
    manga::MangaCanvas::ContentState m_lastPlaceholder{manga::MangaCanvas::ContentState::NoProject};

    BreakpointMode m_breakpointMode{BreakpointMode::Wide};
    bool m_pageDrawerOpen{false};
    bool m_inspectorDrawerOpen{false};
    int m_lastAvailableWidth{1440};
};

} // namespace manga::ui::screen::editor
