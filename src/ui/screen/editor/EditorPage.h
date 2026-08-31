#pragma once

#include "ui/base/BasePage.h"
#include "ui/manga/MangaCanvas.h"

class QSplitter;

namespace manga::ui::manga {
class MangaCanvas;
}
namespace manga::ui::widget {
class InspectorPanel;
class PageThumbnailList;
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
    explicit EditorPage(EditorViewModel* viewModel, QWidget* parent = nullptr);

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

Q_SIGNALS:
    void openProjectRequested();
    void importPagesRequested();

private:
    void buildUi();
    void render(const EditorState& state);
    void handlePageImageReady(const PageImageDelivery& delivery);
    void handlePlaceholderAction();

    EditorViewModel* m_viewModel; // non-owning
    QSplitter* m_splitter{nullptr};
    ui::widget::PageThumbnailList* m_pageList{nullptr};
    ui::manga::MangaCanvas* m_canvas{nullptr};
    ui::widget::InspectorPanel* m_inspector{nullptr};
    ui::manga::MangaCanvas::ContentState m_lastPlaceholder{ui::manga::MangaCanvas::ContentState::NoProject};
};

} // namespace manga::ui::screen::editor
