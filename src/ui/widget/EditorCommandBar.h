#pragma once

#include "ui/screen/editor/EditorState.h"

#include <FluentQt/Foundation.h>
#include <QWidget>

class QPaintEvent;
class QResizeEvent;
class QAction;

namespace fluent::basicinput {
class Button;
}
namespace fluent::textfields {
class Label;
}
namespace fluent::menus_toolbars {
class FluentMenu;
}

namespace manga::ui::screen::editor {
class EditorViewModel;
}

namespace manga::ui::widget {

class EditorCommandBar : public QWidget, public fluent::FluentElement {
    Q_OBJECT
public:
    explicit EditorCommandBar(screen::editor::EditorViewModel* viewModel, QWidget* parent = nullptr);

    [[nodiscard]] fluent::basicinput::Button* undoButtonForTest() const noexcept {
        return m_undoButton;
    }
    [[nodiscard]] fluent::basicinput::Button* redoButtonForTest() const noexcept {
        return m_redoButton;
    }
    [[nodiscard]] fluent::basicinput::Button* smartTranslateButtonForTest() const noexcept {
        return m_smartTranslateButton;
    }
    [[nodiscard]] fluent::basicinput::Button* saveButtonForTest() const noexcept {
        return m_saveButton;
    }
    [[nodiscard]] fluent::basicinput::Button* pagePaneToggleButtonForTest() const noexcept {
        return m_pagePaneToggleButton;
    }
    [[nodiscard]] fluent::basicinput::Button* inspectorPaneToggleButtonForTest() const noexcept {
        return m_inspectorPaneToggleButton;
    }

Q_SIGNALS:
    void zoomInRequested();
    void zoomOutRequested();
    void fitToPageRequested();
    void actualSizeRequested();
    void deleteSelectedRegionsRequested();
    void commitRequested();
    void pagePaneToggleRequested();
    void inspectorPaneToggleRequested();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void onThemeUpdated() override;

private:
    void buildUi();
    void render(const screen::editor::EditorState& state);
    void setupShortcuts();
    void updateResponsiveLayout();
    fluent::basicinput::Button* createIconButton(const QString& glyph, const QString& tooltip, const QString& name);
    bool shouldIgnoreShortcut() const;

    screen::editor::EditorViewModel* m_viewModel;

    fluent::basicinput::Button* m_undoButton{nullptr};
    fluent::basicinput::Button* m_redoButton{nullptr};
    fluent::basicinput::Button* m_zoomOutButton{nullptr};
    fluent::textfields::Label* m_zoomValueLabel{nullptr};
    fluent::basicinput::Button* m_zoomInButton{nullptr};
    fluent::basicinput::Button* m_fitToPageButton{nullptr};
    fluent::basicinput::Button* m_actualSizeButton{nullptr};
    fluent::basicinput::Button* m_selectButton{nullptr};
    fluent::basicinput::Button* m_panButton{nullptr};
    fluent::basicinput::Button* m_createRegionButton{nullptr};
    fluent::basicinput::Button* m_previewSelectorButton{nullptr};
    fluent::basicinput::Button* m_smartTranslateButton{nullptr};
    fluent::basicinput::Button* m_adoptButton{nullptr};
    fluent::basicinput::Button* m_rejectButton{nullptr};
    fluent::basicinput::Button* m_pagePaneToggleButton{nullptr};
    fluent::basicinput::Button* m_inspectorPaneToggleButton{nullptr};
    fluent::basicinput::Button* m_saveButton{nullptr};
    fluent::basicinput::Button* m_overflowButton{nullptr};
    fluent::menus_toolbars::FluentMenu* m_overflowMenu{nullptr};
    QAction* m_zoomOutOverflowAction{nullptr};
    QAction* m_zoomInOverflowAction{nullptr};
    QAction* m_fitToPageOverflowAction{nullptr};
    QAction* m_actualSizeOverflowAction{nullptr};
    QAction* m_previewOverflowAction{nullptr};
    QAction* m_adoptOverflowAction{nullptr};
    QAction* m_rejectOverflowAction{nullptr};
    bool m_overflowActive{false};
    bool m_hasProposal{false};
};

} // namespace manga::ui::widget
