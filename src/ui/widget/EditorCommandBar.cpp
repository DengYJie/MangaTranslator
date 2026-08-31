#include "ui/widget/EditorCommandBar.h"

#include "ui/screen/editor/EditorViewModel.h"

#include <FluentQt/FluentQt.h>
#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QShortcut>
#include <QTextEdit>

using namespace manga::ui::screen::editor;

namespace manga::ui::widget {

EditorCommandBar::EditorCommandBar(EditorViewModel* viewModel, QWidget* parent)
    : QWidget(parent), m_viewModel(viewModel) {
    buildUi();
    setupShortcuts();

    connect(m_viewModel, &EditorViewModel::stateChanged, this, &EditorCommandBar::render);
    render(m_viewModel->state());
}

bool EditorCommandBar::shouldIgnoreShortcut() const {
    QWidget* fw = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw);
}

void EditorCommandBar::buildUi() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    m_undoButton = createIconButton(QStringLiteral("\uE7A7"), tr("Undo (Ctrl+Z)"), QStringLiteral("undoButton"));
    m_redoButton = createIconButton(QStringLiteral("\uE7A6"), tr("Redo (Ctrl+Y)"), QStringLiteral("redoButton"));
    m_selectButton = createIconButton(QStringLiteral("\uE8B0"), tr("Select (V)"), QStringLiteral("selectButton"));
    m_selectButton->setCheckable(true);
    m_panButton = createIconButton(QStringLiteral("\uE756"), tr("Pan (H)"), QStringLiteral("panButton"));
    m_panButton->setCheckable(true);
    m_createRegionButton =
        createIconButton(QStringLiteral("\uE710"), tr("Create Region (R)"), QStringLiteral("createRegionButton"));
    m_createRegionButton->setCheckable(true);
    m_previewSelectorButton = createIconButton(
        QStringLiteral("\uE890"), tr("Preview Original/Translation (Space)"), QStringLiteral("previewSelectorButton"));
    m_saveButton = createIconButton(QStringLiteral("\uE74E"), tr("Save (Ctrl+S)"), QStringLiteral("saveButton"));

    layout->addWidget(m_undoButton);
    layout->addWidget(m_redoButton);
    layout->addSpacing(8);
    layout->addWidget(m_selectButton);
    layout->addWidget(m_panButton);
    layout->addWidget(m_createRegionButton);
    layout->addSpacing(8);
    layout->addWidget(m_previewSelectorButton);
    layout->addStretch();
    layout->addWidget(m_saveButton);

    connect(m_undoButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::undo);
    connect(m_redoButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::redo);
    connect(m_selectButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->setActiveTool(CanvasTool::Select);
    });
    connect(m_panButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->setActiveTool(CanvasTool::Pan);
    });
    connect(m_createRegionButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_viewModel->setActiveTool(CanvasTool::CreateRegion);
    });
    connect(m_previewSelectorButton, &fluent::basicinput::Button::clicked, this, [this]() {
        auto mode = m_viewModel->state().previewMode == PreviewMode::Original ? PreviewMode::Translation
                                                                              : PreviewMode::Original;
        m_viewModel->setPreviewMode(mode);
    });
    connect(m_saveButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::saveProject);
}

fluent::basicinput::Button*
EditorCommandBar::createIconButton(const QString& glyph, const QString& tooltip, const QString& name) {
    auto* btn = new fluent::basicinput::Button(this);
    btn->setObjectName(name);
    btn->setFluentStyle(fluent::basicinput::Button::Subtle);
    btn->setFluentLayout(fluent::basicinput::Button::IconOnly);
    fluent::Icon icon;
    icon.setGlyph(glyph);
    btn->setIcon(icon);
    btn->setToolTip(tooltip);
    btn->setAccessibleName(tooltip);
    return btn;
}

void EditorCommandBar::render(const EditorState& state) {
    m_undoButton->setEnabled(state.canUndo);
    m_redoButton->setEnabled(state.canRedo);
    m_saveButton->setEnabled(state.saveState.isDirty && !state.isSaving);

    m_selectButton->setChecked(state.activeTool == CanvasTool::Select);
    m_panButton->setChecked(state.activeTool == CanvasTool::Pan);
    m_createRegionButton->setChecked(state.activeTool == CanvasTool::CreateRegion);
}

void EditorCommandBar::setupShortcuts() {
    auto makeGlobal = [this](const QString& seq, auto func) {
        auto* s = new QShortcut(QKeySequence(seq), this);
        s->setContext(Qt::ApplicationShortcut);
        connect(s, &QShortcut::activated, this, [this, func]() {
            if (!shouldIgnoreShortcut()) {
                func();
            }
        });
    };

    makeGlobal(QStringLiteral("V"), [this]() { m_viewModel->setActiveTool(CanvasTool::Select); });
    makeGlobal(QStringLiteral("R"), [this]() { m_viewModel->setActiveTool(CanvasTool::CreateRegion); });
    makeGlobal(QStringLiteral("H"), [this]() { m_viewModel->setActiveTool(CanvasTool::Pan); });
    makeGlobal(QStringLiteral("Space"), [this]() {
        auto mode = m_viewModel->state().previewMode == PreviewMode::Original ? PreviewMode::Translation
                                                                              : PreviewMode::Original;
        m_viewModel->setPreviewMode(mode);
    });
    makeGlobal(QStringLiteral("+"), [this]() { Q_EMIT zoomInRequested(); });
    makeGlobal(QStringLiteral("="), [this]() { Q_EMIT zoomInRequested(); });
    makeGlobal(QStringLiteral("-"), [this]() { Q_EMIT zoomOutRequested(); });
    makeGlobal(QStringLiteral("0"), [this]() { Q_EMIT fitToPageRequested(); });
    makeGlobal(QStringLiteral("1"), [this]() { Q_EMIT actualSizeRequested(); });
    makeGlobal(QStringLiteral("Delete"), [this]() { Q_EMIT deleteSelectedRegionsRequested(); });
    makeGlobal(QStringLiteral("Return"), [this]() { Q_EMIT commitRequested(); });
    makeGlobal(QStringLiteral("Enter"), [this]() { Q_EMIT commitRequested(); });

    auto* sSave = new QShortcut(QKeySequence::Save, this);
    sSave->setContext(Qt::ApplicationShortcut);
    connect(sSave, &QShortcut::activated, m_viewModel, &EditorViewModel::saveProject);

    auto* sUndo = new QShortcut(QKeySequence::Undo, this);
    sUndo->setContext(Qt::ApplicationShortcut);
    connect(sUndo, &QShortcut::activated, m_viewModel, &EditorViewModel::undo);

    auto* sRedo = new QShortcut(QKeySequence::Redo, this);
    sRedo->setContext(Qt::ApplicationShortcut);
    connect(sRedo, &QShortcut::activated, m_viewModel, &EditorViewModel::redo);
}

} // namespace manga::ui::widget
