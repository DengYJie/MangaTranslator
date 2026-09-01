#include "ui/widget/EditorCommandBar.h"

#include "ui/screen/editor/EditorProcessingController.h"
#include "ui/screen/editor/EditorViewModel.h"

#include <FluentQt/FluentQt.h>
#include <FluentQt/TextFields.h>
#include <QApplication>
#include <QButtonGroup>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
#include <QShortcut>
#include <QTextEdit>

#include <initializer_list>

using namespace manga::ui::screen::editor;

namespace manga::ui::widget {

EditorCommandBar::EditorCommandBar(EditorViewModel* viewModel, QWidget* parent)
    : QWidget(parent), m_viewModel(viewModel) {
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(40);
    m_outputDirProvider = [this]() { return QFileDialog::getExistingDirectory(this, tr("选择导出目录")); };
    buildUi();
    setupShortcuts();

    connect(m_viewModel, &EditorViewModel::stateChanged, this, &EditorCommandBar::render);
    render(m_viewModel->state());
}

bool EditorCommandBar::shouldIgnoreShortcut() const {
    if (!m_viewModel->state().currentPageId.isValid()) {
        return true;
    }
    QWidget* fw = QApplication::focusWidget();
    return qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw);
}

void EditorCommandBar::buildUi() {
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_undoButton = createIconButton(Typography::Icons::Undo, tr("撤销 (Ctrl+Z)"), QStringLiteral("undoButton"));
    m_redoButton = createIconButton(Typography::Icons::Redo, tr("重做 (Ctrl+Y)"), QStringLiteral("redoButton"));

    m_zoomOutButton = createIconButton(Typography::Icons::ZoomOut, tr("缩小 (-)"), QStringLiteral("zoomOutButton"));
    m_zoomValueLabel = new fluent::textfields::Label(this);
    m_zoomValueLabel->setObjectName(QStringLiteral("zoomValueLabel"));
    m_zoomValueLabel->setFluentTypography(Typography::FontRole::Caption);
    m_zoomValueLabel->setMinimumWidth(44);
    m_zoomValueLabel->setAlignment(Qt::AlignCenter);
    m_zoomInButton = createIconButton(Typography::Icons::ZoomIn, tr("放大 (+)"), QStringLiteral("zoomInButton"));
    m_fitToPageButton =
        createIconButton(Typography::Icons::Zoom, tr("适应页面 (0)"), QStringLiteral("fitToPageButton"));
    m_actualSizeButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_text_font_size_20_regular")),
                         tr("实际大小 (1)"),
                         QStringLiteral("actualSizeButton"));

    m_selectButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_cursor_20_regular")),
                                      tr("选择工具 (V)"),
                                      QStringLiteral("selectButton"));
    m_selectButton->setCheckable(true);
    m_panButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_hand_left_20_regular")),
                                   tr("平移工具 (H)"),
                                   QStringLiteral("panButton"));
    m_panButton->setCheckable(true);
    m_createRegionButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_text_20_regular")),
                                            tr("创建文本区域 (R)"),
                                            QStringLiteral("createRegionButton"));
    m_createRegionButton->setCheckable(true);
    auto* toolGroup = new QButtonGroup(this);
    toolGroup->setExclusive(true);
    toolGroup->addButton(m_selectButton);
    toolGroup->addButton(m_panButton);
    toolGroup->addButton(m_createRegionButton);
    m_previewSelectorButton = createIconButton(
        Typography::Icons::View, tr("切换原图/译文预览 (Space)"), QStringLiteral("previewSelectorButton"));
    m_ocrButton = createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_scan_text_20_regular")),
                                   tr("重新 OCR"),
                                   QStringLiteral("ocrButton"));
    m_smartTranslateButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_translate_20_regular")),
                         tr("智能翻译 (Ctrl+T)"),
                         QStringLiteral("smartTranslateButton"));
    m_smartTranslateButton->setFluentStyle(fluent::basicinput::Button::Accent);
    m_adoptButton = createIconButton(Typography::Icons::CheckMark, tr("采纳建议"), QStringLiteral("adoptButton"));
    m_adoptButton->setFluentStyle(fluent::basicinput::Button::Accent);
    m_adoptButton->setVisible(false);
    m_rejectButton = createIconButton(Typography::Icons::Cancel, tr("拒绝建议"), QStringLiteral("rejectButton"));
    m_rejectButton->setVisible(false);
    m_pagePaneToggleButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_panel_left_text_20_regular")),
                         tr("页面列表"),
                         QStringLiteral("pagePaneToggleButton"));
    m_pagePaneToggleButton->setCheckable(true);
    m_pagePaneToggleButton->setVisible(false);
    m_inspectorPaneToggleButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_panel_right_20_regular")),
                         tr("属性面板"),
                         QStringLiteral("inspectorPaneToggleButton"));
    m_inspectorPaneToggleButton->setCheckable(true);
    m_inspectorPaneToggleButton->setVisible(false);
    m_saveButton = createIconButton(Typography::Icons::Save, tr("保存项目 (Ctrl+S)"), QStringLiteral("saveButton"));
    m_overflowButton =
        createIconButton(Typography::Icons::glyph(QStringLiteral("ic_fluent_more_horizontal_20_regular")),
                         tr("更多命令"),
                         QStringLiteral("overflowButton"));
    m_overflowButton->hide();
    m_overflowMenu = new fluent::menus_toolbars::FluentMenu(QString(), this);
    const auto addOverflowAction = [this](const QString& text, fluent::basicinput::Button* button) {
        auto* action = m_overflowMenu->addAction(text);
        connect(action, &QAction::triggered, button, &fluent::basicinput::Button::click);
        return action;
    };
    m_zoomOutOverflowAction = addOverflowAction(tr("缩小"), m_zoomOutButton);
    m_zoomInOverflowAction = addOverflowAction(tr("放大"), m_zoomInButton);
    m_fitToPageOverflowAction = addOverflowAction(tr("适应页面"), m_fitToPageButton);
    m_actualSizeOverflowAction = addOverflowAction(tr("实际大小"), m_actualSizeButton);
    m_previewOverflowAction = addOverflowAction(tr("切换原图/译文预览"), m_previewSelectorButton);
    m_adoptOverflowAction = addOverflowAction(tr("采纳建议"), m_adoptButton);
    m_rejectOverflowAction = addOverflowAction(tr("拒绝建议"), m_rejectButton);
    connect(m_overflowButton, &fluent::basicinput::Button::clicked, this, [this]() {
        m_overflowMenu->exec(m_overflowButton->mapToGlobal(QPoint(0, m_overflowButton->height())));
    });

    bool hasGroup = false;
    const auto addGroup = [this, layout, &hasGroup](std::initializer_list<QWidget*> widgets) {
        QFrame* separator = nullptr;
        if (hasGroup) {
            layout->addSpacing(8);
            separator = new QFrame(layout->parentWidget());
            separator->setObjectName(QStringLiteral("editorCommandBarSeparator%1").arg(m_commandSeparators.size()));
            separator->setFrameShape(QFrame::VLine);
            separator->setFrameShadow(QFrame::Sunken);
            separator->setFixedSize(1, 20);
            layout->addWidget(separator, 0, Qt::AlignVCenter);
            layout->addSpacing(8);
        }
        auto* group = new QWidget(layout->parentWidget());
        auto* groupLayout = new QHBoxLayout(group);
        groupLayout->setContentsMargins(0, 0, 0, 0);
        groupLayout->setSpacing(2);
        for (auto* widget : widgets) {
            groupLayout->addWidget(widget);
        }
        layout->addWidget(group);
        m_commandGroups.append(group);
        m_commandSeparators.append(separator);
        hasGroup = true;
    };

    addGroup({m_undoButton, m_redoButton});
    addGroup({m_zoomOutButton, m_zoomValueLabel, m_zoomInButton, m_fitToPageButton, m_actualSizeButton});
    addGroup({m_selectButton, m_panButton, m_createRegionButton});
    addGroup({m_previewSelectorButton});
    addGroup({m_ocrButton, m_smartTranslateButton, m_adoptButton, m_rejectButton});
    layout->addStretch();
    addGroup({m_pagePaneToggleButton, m_inspectorPaneToggleButton});
    addGroup({m_overflowButton});
    addGroup({m_saveButton});

    connect(m_undoButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::undo);
    connect(m_redoButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::redo);
    connect(m_zoomOutButton, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT zoomOutRequested(); });
    connect(m_zoomInButton, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT zoomInRequested(); });
    connect(m_fitToPageButton, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT fitToPageRequested(); });
    connect(m_actualSizeButton, &fluent::basicinput::Button::clicked, this, [this]() { Q_EMIT actualSizeRequested(); });
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
        const auto current = m_viewModel->state().previewMode;
        const auto mode = current == PreviewMode::Original
                              ? PreviewMode::Translated
                              : (current == PreviewMode::Translated ? PreviewMode::Compare : PreviewMode::Original);
        m_viewModel->setPreviewMode(mode);
    });
    connect(m_ocrButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (state.currentPageId.isValid() && !state.selectedRegionIds.isEmpty()) {
                ctrl->recognizeSelectedRegions();
            }
        }
    });
    connect(m_smartTranslateButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (state.processing.isRunning) {
                ctrl->cancelPageOperations(state.currentPageId);
            } else if (state.currentPageId.isValid()) {
                ctrl->smartTranslatePage(state.currentPageId);
            }
        }
    });
    connect(m_adoptButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            ctrl->adoptProposal(m_viewModel->state().currentPageId);
        }
    });
    connect(m_rejectButton, &fluent::basicinput::Button::clicked, this, [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            ctrl->rejectProposal(m_viewModel->state().currentPageId);
        }
    });
    connect(m_pagePaneToggleButton, &fluent::basicinput::Button::clicked, this, [this]() {
        Q_EMIT pagePaneToggleRequested();
    });
    connect(m_inspectorPaneToggleButton, &fluent::basicinput::Button::clicked, this, [this]() {
        Q_EMIT inspectorPaneToggleRequested();
    });
    connect(m_saveButton, &fluent::basicinput::Button::clicked, m_viewModel, &EditorViewModel::saveProject);
}

fluent::basicinput::Button*
EditorCommandBar::createIconButton(const QString& glyph, const QString& tooltip, const QString& name) {
    auto* btn = new fluent::basicinput::Button(this);
    btn->setObjectName(name);
    btn->setFluentStyle(fluent::basicinput::Button::Subtle);
    btn->setFluentSize(fluent::basicinput::Button::StandardSize);
    btn->setFluentLayout(fluent::basicinput::Button::IconOnly);
    btn->setFixedHeight(32);
    btn->setIconGlyph(glyph, Typography::IconSize::Standard);
    btn->setToolTip(tooltip);
    btn->setAccessibleName(tooltip);
    return btn;
}

void EditorCommandBar::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);

    QPainter painter(this);
    const auto& colors = themeColorsRef();
    painter.fillRect(rect(), colors.bgLayer);
    painter.setPen(QPen(colors.strokeDivider, 1));
    painter.drawLine(0, height() - 1, width(), height() - 1);
}

void EditorCommandBar::onThemeUpdated() {
    update();
}

void EditorCommandBar::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    updateResponsiveLayout();
}

void EditorCommandBar::updateResponsiveLayout() {
    const bool overflowActive = width() < 1008;
    m_overflowActive = overflowActive;

    const bool narrow = width() < 720;
    m_zoomOutButton->setVisible(!narrow);
    m_zoomValueLabel->setVisible(!narrow);
    m_zoomInButton->setVisible(!narrow);
    m_fitToPageButton->setVisible(!overflowActive);
    m_actualSizeButton->setVisible(!overflowActive);
    m_previewSelectorButton->setVisible(!overflowActive);
    m_adoptButton->setVisible(m_hasProposal);
    m_rejectButton->setVisible(m_hasProposal);
    m_overflowButton->setVisible(overflowActive);

    m_zoomOutOverflowAction->setVisible(narrow);
    m_zoomInOverflowAction->setVisible(narrow);
    m_fitToPageOverflowAction->setVisible(overflowActive);
    m_actualSizeOverflowAction->setVisible(overflowActive);
    m_previewOverflowAction->setVisible(overflowActive);
    m_adoptOverflowAction->setVisible(overflowActive && !m_hasProposal);
    m_rejectOverflowAction->setVisible(overflowActive && !m_hasProposal);

    bool previousGroupVisible = false;
    for (int i = 0; i < m_commandGroups.size(); ++i) {
        const auto children = m_commandGroups.at(i)->findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        bool groupVisible = false;
        for (const auto* child : children) {
            if (!child->isHidden()) {
                groupVisible = true;
                break;
            }
        }
        m_commandGroups.at(i)->setVisible(groupVisible);
        if (m_commandSeparators.at(i) != nullptr) {
            m_commandSeparators.at(i)->setVisible(groupVisible && previousGroupVisible);
        }
        previousGroupVisible = previousGroupVisible || groupVisible;
    }
}

void EditorCommandBar::render(const EditorState& state) {
    m_undoButton->setEnabled(state.canUndo);
    m_redoButton->setEnabled(state.canRedo);
    m_saveButton->setEnabled(state.saveState.isDirty && !state.saveState.isSaving);

    m_zoomValueLabel->setText(QStringLiteral("%1%").arg(qRound(state.zoomFactor * 100.0)));

    m_selectButton->setChecked(state.activeTool == CanvasTool::Select);
    m_panButton->setChecked(state.activeTool == CanvasTool::Pan);
    m_createRegionButton->setChecked(state.activeTool == CanvasTool::CreateRegion);

    // Disable viewport and canvas tools if no page is active
    const bool hasPage = state.currentPageId.isValid();
    const bool hasRegions = !state.regions.isEmpty();
    m_zoomOutButton->setEnabled(hasPage);
    m_zoomInButton->setEnabled(hasPage);
    m_fitToPageButton->setEnabled(hasPage);
    m_actualSizeButton->setEnabled(hasPage);
    m_selectButton->setEnabled(hasPage);
    m_panButton->setEnabled(hasPage);
    m_createRegionButton->setEnabled(hasPage);
    m_previewSelectorButton->setEnabled(hasPage);
    const bool hasSelectedRegions = hasPage && !state.selectedRegionIds.isEmpty();
    m_ocrButton->setVisible(hasSelectedRegions);
    m_ocrButton->setEnabled(hasSelectedRegions && state.isOcrAvailable);
    m_ocrButton->setToolTip(state.isOcrAvailable ? tr("重新识别选中区域的文字")
                                                 : tr("OCR 模型不可用，请先在设置 > OCR 中配置模型"));
    m_ocrButton->setAccessibleName(state.isOcrAvailable ? tr("重新 OCR") : tr("重新 OCR（不可用）"));

    if (state.processing.isRunning) {
        m_smartTranslateButton->setEnabled(true);
        m_smartTranslateButton->setIconGlyph(Typography::Icons::Cancel, Typography::IconSize::Standard);
        m_smartTranslateButton->setToolTip(tr("取消处理"));
        m_smartTranslateButton->setAccessibleName(tr("取消处理"));
    } else if (!state.isTranslationAvailable) {
        m_smartTranslateButton->setEnabled(false);
        m_smartTranslateButton->setIconGlyph(Typography::Icons::glyph(QStringLiteral("ic_fluent_translate_20_regular")),
                                             Typography::IconSize::Standard);
        m_smartTranslateButton->setToolTip(state.translationUnavailableReason);
        m_smartTranslateButton->setAccessibleName(tr("智能翻译（未配置）"));
    } else if (hasPage && !hasRegions) {
        m_smartTranslateButton->setEnabled(false);
        m_smartTranslateButton->setIconGlyph(Typography::Icons::glyph(QStringLiteral("ic_fluent_translate_20_regular")),
                                             Typography::IconSize::Standard);
        m_smartTranslateButton->setToolTip(tr("当前页无文本区域，请先创建或检测区域"));
        m_smartTranslateButton->setAccessibleName(tr("智能翻译 (当前页无文本区域)"));
    } else {
        m_smartTranslateButton->setEnabled(hasPage && hasRegions);
        m_smartTranslateButton->setIconGlyph(Typography::Icons::glyph(QStringLiteral("ic_fluent_translate_20_regular")),
                                             Typography::IconSize::Standard);
        m_smartTranslateButton->setToolTip(tr("智能翻译 (Ctrl+T)"));
        m_smartTranslateButton->setAccessibleName(tr("智能翻译"));
    }

    m_hasProposal = state.processing.hasProposal;
    updateResponsiveLayout();
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
        const auto current = m_viewModel->state().previewMode;
        const auto mode = current == PreviewMode::Original
                              ? PreviewMode::Translated
                              : (current == PreviewMode::Translated ? PreviewMode::Compare : PreviewMode::Original);
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
    makeGlobal(QStringLiteral("Ctrl+T"), [this]() {
        if (auto* ctrl = m_viewModel->processingController()) {
            const auto& state = m_viewModel->state();
            if (state.isTranslationAvailable && !state.processing.isRunning && state.currentPageId.isValid() &&
                !state.regions.isEmpty()) {
                ctrl->smartTranslatePage(state.currentPageId);
            }
        }
    });

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
