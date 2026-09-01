#pragma once

#include "ui/base/BasePage.h"
#include "ui/manga/MangaCanvas.h"

class QSplitter;
class QScrollArea;

namespace manga::ui::manga {
class MangaCanvas;
}
namespace manga::ui::widget {
class InspectorPanel;
class PageThumbnailList;
class EditorCommandBar;
} // namespace manga::ui::widget

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
    [[nodiscard]] ui::manga::MangaCanvas* canvasForTest() const {
        return m_canvas;
    }
    [[nodiscard]] ui::widget::PageThumbnailList* pageListForTest() const {
        return m_pageList;
    }
    [[nodiscard]] ui::widget::InspectorPanel* inspectorForTest() const {
        return m_inspector;
    }
    [[nodiscard]] QSplitter* splitterForTest() const {
        return m_splitter;
    }
    [[nodiscard]] QWidget* pagePaneCardForTest() const {
        return m_pagePaneCard;
    }
    [[nodiscard]] QWidget* inspectorCardForTest() const {
        return m_inspectorCard;
    }
    [[nodiscard]] ui::widget::EditorCommandBar* commandBarForTest() const {
        return m_commandBar;
    }
    [[nodiscard]] BreakpointMode breakpointModeForTest() const noexcept {
        return m_breakpointMode;
    }

Q_SIGNALS:
    void openProjectRequested();
    void importPagesRequested();

protected:
    void updateResponsiveLayout(int availableWidth) override;
    void keyPressEvent(QKeyEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void render(const EditorState& state);
    void handlePageImageReady(const PageImageDelivery& delivery);
    void handlePlaceholderAction();
    void applySplitterLayout();

    EditorViewModel* m_viewModel; // non-owning
    ui::widget::EditorCommandBar* m_commandBar{nullptr};
    QScrollArea* m_commandBarScrollArea{nullptr};
    QSplitter* m_splitter{nullptr};
    QWidget* m_pagePaneCard{nullptr};
    ui::widget::PageThumbnailList* m_pageList{nullptr};
    ui::manga::MangaCanvas* m_canvas{nullptr};
    QWidget* m_inspectorCard{nullptr};
    ui::widget::InspectorPanel* m_inspector{nullptr};
    ui::manga::MangaCanvas::ContentState m_lastPlaceholder{ui::manga::MangaCanvas::ContentState::NoProject};

    BreakpointMode m_breakpointMode{BreakpointMode::Wide};
    bool m_pageDrawerOpen{false};
    bool m_inspectorDrawerOpen{false};
    int m_lastAvailableWidth{1440};
};

} // namespace manga::ui::screen::editor
