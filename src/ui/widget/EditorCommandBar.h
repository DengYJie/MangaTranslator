#pragma once

#include "ui/screen/editor/EditorState.h"

#include <QWidget>

namespace fluent::basicinput {
class Button;
}
namespace fluent::textfields {
class Label;
}

namespace manga::ui::screen::editor {
class EditorViewModel;
}

namespace manga::ui::widget {

class EditorCommandBar : public QWidget {
    Q_OBJECT
public:
    explicit EditorCommandBar(screen::editor::EditorViewModel* viewModel, QWidget* parent = nullptr);

Q_SIGNALS:
    void zoomInRequested();
    void zoomOutRequested();
    void fitToPageRequested();
    void actualSizeRequested();
    void deleteSelectedRegionsRequested();
    void commitRequested();

private:
    void buildUi();
    void render(const screen::editor::EditorState& state);
    void setupShortcuts();
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
    fluent::basicinput::Button* m_saveButton{nullptr};
};

} // namespace manga::ui::widget
