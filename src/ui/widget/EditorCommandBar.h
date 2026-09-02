#pragma once

#include "application/usecase/export/ExportUseCases.h"
#include "ui/screen/editor/EditorState.h"

#include <FluentQt/Foundation.h>
#include <QVector>
#include <QWidget>

#include <functional>

class QPaintEvent;
class QResizeEvent;
class QAction;
class QFrame;
class QMenu;

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
    [[nodiscard]] fluent::basicinput::Button* detectButtonForTest() const noexcept {
        return m_detectButton;
    }
    [[nodiscard]] fluent::basicinput::Button* ocrButtonForTest() const noexcept {
        return m_ocrButton;
    }
    [[nodiscard]] fluent::basicinput::Button* inpaintButtonForTest() const noexcept {
        return m_inpaintButton;
    }
    [[nodiscard]] fluent::basicinput::Button* translateButtonForTest() const noexcept {
        return m_translateButton;
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
    [[nodiscard]] fluent::basicinput::Button* exportButtonForTest() const noexcept {
        return m_exportButton;
    }
    /// Test hook: replaces the directory chooser so export tests never open a real dialog.
    void setOutputDirProviderForTest(std::function<QString()> provider) {
        m_outputDirProvider = std::move(provider);
    }

Q_SIGNALS:
    void chooseExportPathRequested();
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
    fluent::basicinput::Button* m_textButton{nullptr};
    fluent::basicinput::Button* m_previewSelectorButton{nullptr};
    fluent::basicinput::Button* m_detectButton{nullptr};
    fluent::basicinput::Button* m_ocrButton{nullptr};
    fluent::basicinput::Button* m_inpaintButton{nullptr};
    fluent::basicinput::Button* m_translateButton{nullptr};
    fluent::basicinput::Button* m_smartTranslateButton{nullptr};
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
    QVector<QWidget*> m_commandGroups;
    // Members of each group in m_commandGroups. Recorded when the group is built because the
    // responsive layout pass asks for them on every state update.
    QVector<QVector<QWidget*>> m_commandGroupWidgets;
    QVector<QFrame*> m_commandSeparators;
    bool m_overflowActive{false};
    fluent::basicinput::Button* m_exportButton{nullptr};
    QMenu* m_exportMenu{nullptr};
    QAction* m_exportCurrentPageAction{nullptr};
    QAction* m_exportSelectedPagesAction{nullptr};
    QAction* m_exportAllPagesAction{nullptr};
    std::function<QString()> m_outputDirProvider;
};

} // namespace manga::ui::widget
